#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

SendError Session::sendRefusal(
		const std::shared_ptr<const PreparedSend> &prepared,
		const KeyAuthorization &auth) {
	if (!prepared || (!prepared->intent && !prepared->nft) || !_preview) {
		return SendError::InvalidRequest;
	}
	const auto terms = gaslessTerms();
	const auto owner = _preview->owners.find(prepared->owner);
	if (owner == end(_preview->owners)
		|| owner->second != prepared->revision) {
		return SendError::QuoteExpired;
	}
	const auto &args = prepared->args;
	const auto tonConnect = prepared->tonConnect;
	const auto ordinary = !tonConnect && !prepared->nft;
	if (!SendCommentFits(args.comment.text)) {
		return SendError::CommentTooLong;
	}
	if ((!tonConnect
			&& (args.amountNano <= 0
				|| FormatFriendly(args.destination, args.bounce).isEmpty()))
		|| prepared->feeNano < 0
		|| (!args.comment.text.isEmpty()
			&& !args.comment.isPublic
			&& !prepared->privateEpoch)
		|| (prepared->nft && prepared->operationId.empty())) {
		return SendError::InvalidRequest;
	}
	if (ordinary
		&& TransferAmountBelowMinimum(
			args.amountNano,
			TransferMinNanos(_session))) {
		return SendError::AmountTooSmall;
	}
	if (prepared->privateEpoch
		&& *prepared->privateEpoch != vault().clearEpoch()) {
		return SendError::Locked;
	}
	if (_presence.current() != Presence::Ready
		|| prepared->generation != _networkGeneration
		|| !transferWalletIdentityCurrent(prepared->identity)) {
		return SendError::Failed;
	}
	if (ordinary
		&& (prepared->terms != terms
			|| terms.identity != prepared->identity)) {
		return SendError::QuoteExpired;
	}
	if (_clientStopping
		|| !transferClientMatches(prepared->identity, prepared->client)) {
		return SendError::SigningUnavailable;
	}
	if (_sendState.current() != SendState::Idle
		|| _rotating
		|| custody().pendingRotation) {
		return SendError::AlreadySending;
	}
	if (_pending || _sendUnresolved) {
		return SendError::PreviousUnresolved;
	}
	if (!_sendRecoveryReady) {
		return SendError::Failed;
	}
	if (!ReadAuthorized(*this, auth)) {
		return SendError::Locked;
	}
	const auto paired = ordinary
		&& terms.eligible(args.amountNano, args.destination);
	const auto balance = _balanceNano.current();
	if (args.amountNano > balance) {
		return SendError::InsufficientBalance;
	}
	if (!paired && prepared->feeNano > balance - args.amountNano) {
		// The relayer pays a fee-free transfer's fee, so nothing of the
		// balance is kept back for it and the whole of it can be sent.
		return SendError::InsufficientFees;
	}
	return SendError::None;
}

void Session::send(
		KeyAuthorization auth,
		std::shared_ptr<const PreparedSend> prepared,
		Fn<void(SendError)> done,
		Fn<void(SendStarted)> started) {
	startSend(
		std::move(auth),
		std::move(prepared),
		std::move(done),
		std::move(started),
		nullptr,
		{});
}

void Session::sendTonConnect(
		KeyAuthorization auth,
		std::shared_ptr<const PreparedSend> prepared,
		TonConnectSendLink link,
		Fn<void(TonConnectSendResult)> done,
		Fn<void(SendError)> settled) {
	if (!prepared
		|| !prepared->tonConnect
		|| link.operationId.empty()
		|| link.operationId.size() > kTonConnectOperationIdMaxBytes
		|| !link.handoff) {
		if (done) {
			done({ .error = SendError::InvalidRequest });
		}
		return;
	}
	startSend(
		std::move(auth),
		std::move(prepared),
		std::move(settled),
		nullptr,
		std::move(done),
		std::move(link));
}

void Session::startSend(
		KeyAuthorization auth,
		std::shared_ptr<const PreparedSend> prepared,
		Fn<void(SendError)> done,
		Fn<void(SendStarted)> started,
		Fn<void(TonConnectSendResult)> tonConnect,
		TonConnectSendLink tonConnectLink) {
	done = [done = std::move(done)](SendError error) {
		if (error != SendError::None) {
			LOG(("Wallet Error: the send answered %1."
				).arg(SendErrorName(error)));
		}
		if (done) {
			done(error);
		}
	};
	const auto fail = [&](SendError error) {
		if (done) {
			done(error);
		}
		if (tonConnect) {
			tonConnect({ .error = error });
		}
	};
	const auto refusal = sendRefusal(prepared, auth);
	if (refusal != SendError::None) {
		fail(refusal);
		return;
	}
	const auto terms = gaslessTerms();
	const auto &args = prepared->args;
	const auto ordinary = !prepared->tonConnect && !prepared->nft;
	const auto paired = ordinary
		&& terms.eligible(args.amountNano, args.destination);
	const auto linked = !tonConnectLink.operationId.empty();
	const auto preset = linked || !prepared->operationId.empty();
	const auto operationId = linked
		? tonConnectLink.operationId
		: preset
		? prepared->operationId
		: NewRecordId();
	const auto client = prepared->client;
	const auto generation = prepared->generation;
	const auto identity = prepared->identity;
	const auto custodyRecord = custody().current(
		identity.address,
		identity.publicKey);
	if (!custodyRecord
		|| custodyRecord->recordId != _clientRecordId
		|| (preset && submittedTransferRecord(operationId, identity))) {
		fail(SendError::Failed);
		return;
	}
	// The record this send signs with, named now: a swap can rebind the
	// session's own id before the engine answers.
	const auto signingRecordId = custodyRecord->recordId;
	const auto signingKey = custodyRecord->signingKey.isEmpty()
		? custodyRecord->publicKey
		: custodyRecord->signingKey;
	submittedTransferStore().records.push_back(SubmittedTransferRecord{
		.recordId = custodyRecord->recordId,
		.address = identity.address,
		.publicKey = identity.publicKey,
		.operationId = operationId,
		.destination = CanonicalAddress(args.destination),
		.comment = args.comment.isPublic ? args.comment.text : QString(),
		.collectible = args.collectible,
		.amountNano = args.amountNano,
		.recipient = args.userId,
		.posted = base::unixtime::now(),
		.network = custodyRecord->network,
		.paired = paired,
		.bounce = args.bounce,
	});
	_submittedTransfersDirty = true;
	if (!persistSubmittedTransfers()) {
		retireSubmittedTransferRecord(operationId, identity);
		LOG(("Wallet Error: transfer preparation could not be stored."));
		fail(SendError::Failed);
		return;
	}
	const auto stored = submittedTransferRecord(operationId, identity);
	if (!stored) {
		fail(SendError::Failed);
		return;
	}
	const auto pending = PendingSendInfo{
		.operationId = operationId,
		.walletIdentity = identity,
		.posted = stored->posted,
		.amountNano = stored->amountNano,
		.destination = stored->destination,
		.collectible = stored->collectible,
		.comment = stored->comment,
		.recipient = stored->recipient,
		.bounce = stored->bounce,
	};
	const auto owner = _preview->owners.find(prepared->owner);
	if (owner != end(_preview->owners)) {
		++owner->second;
	}
	++_sendRevision;
	_lastReceipt.reset();
	const auto userId = args.userId;
	const auto weak = base::make_weak(_engine.get());
	const auto current = [=, this] {
		return weak && submissionCurrent(operationId, prepared);
	};
	const auto recordPending = [=, this](SendError answer) {
		if (!current() || _sendState.current() != SendState::Sending) {
			return;
		}
		const auto record = submittedTransferRecord(operationId, identity);
		const auto held = submittedTransfer(operationId);
		if (!record
			&& (!held
				|| held->client.lock() != client
				|| held->canonicalId.isEmpty())) {
			return;
		}
		if (record && record->handoff != TransferHandoff::Possible) {
			record->handoff = TransferHandoff::Possible;
			_submittedTransfersDirty = true;
		}
		_pending = pending;
		const auto entry = held ? held : upsertSubmittedTransfer(
			operationId,
			identity,
			generation,
			client);
		if (entry
			&& !entry->receipt
			&& entry->canonicalId.isEmpty()
			&& _submission->receipt) {
			entry->receipt = _submission->receipt;
		}
		if (!persistSubmittedTransfers()) {
			LOG(("Wallet Error: submitted transfer facts remain dirty."));
		}
		_sendUnresolved = true;
		_unresolvedOperationId = operationId;
		dropSubmittedIfListed();
		_sendState = SendState::Pending;
		if (!current()) {
			return;
		}
		_historyUpdates.fire({});
		if (!current()) {
			return;
		}
		updateListsGate();
		if (!current()) {
			return;
		}
		startSubmittedLookup();
		if (!current()) {
			return;
		}
		requestEngineRefresh();
		if (current() && done) {
			done(answer);
		}
	};
	const auto recordUnknown = [=] {
		recordPending(SendError::SubmissionUnknown);
	};
	auto request = std::optional<engine::SendRequest>();
	if (prepared->intent) {
		request = engine::SendRequest{
			.operation_id = operationId,
			.force = false,
			.intent = *prepared->intent,
		};
	}
	const auto route = std::make_shared<TransferSubmission>([=, this](
			TransferSubmissionData data,
			Fn<void(TransferSubmissionAnswer)> answer) {
		if (!weak) {
			answer({ TransferSubmissionOutcome::Rejected });
			return;
		}
		submitTransfer(
			operationId,
			prepared,
			std::move(data),
			std::move(answer));
	});
	_submission = TransferSubmissionState{
		.operationId = operationId,
		.prepared = prepared,
		.started = std::move(started),
		.tonConnect = std::move(tonConnect),
		.tonConnectHandoff = std::move(tonConnectLink.handoff),
		.posted = stored->posted,
		.paired = paired,
		.normalFeeAuthorized = true,
	};
	_engine->run([
		client,
		request = std::move(request),
		nft = prepared->nft,
		operationId,
		route,
		paired
	] {
		if (nft) {
			const auto recording = route->record();
			return client->send_nft_transfer(engine::NftTransferRequest{
				.operation_id = operationId,
				.force = false,
				.intent = *nft,
			});
		} else if (!paired) {
			const auto recording = route->record();
			return client->send(*request);
		}
		// A fee-free offer needs both delivery forms of the same transfer.
		// prepare_transfer() reads the account once, so the pair it signs
		// covers one sequence number and one validity window and the two
		// forms stay mutually exclusive; it submits and journals neither.
		// The external form then goes through the ordinary durable send_boc
		// workflow, which keeps the journal, phases and resolution exactly
		// as an ordinary send has them, while the relayer alternative rides
		// the recording to the routed host so that one wallet.sendTransfer
		// carries both and the server picks the form it will execute.
		const auto pair = client->prepare_transfer(
			engine::PrepareTransferRequest{
				.operation_id = request->operation_id,
				.intent = request->intent,
			});
		// A Boc crosses the engine boundary as its standard padded Base64
		// text: send_boc() takes the external form back as it came, while
		// wallet.sendTransfer carries raw bytes. An undecodable alternative
		// is recorded empty, which submitTransfer() refuses.
		auto gasless = QByteArray::fromBase64Encoding(
			QByteArray::fromStdString(pair.internal_boc),
			QByteArray::Base64Encoding
				| QByteArray::AbortOnBase64DecodingErrors);
		const auto recording = route->record(gasless
			? std::move(gasless.decoded)
			: QByteArray());
		return client->send_boc(engine::SendBocRequest{
			.operation_id = pair.operation_id,
			.force = request->force,
			.signed_boc = pair.external_boc,
			.seqno = pair.seqno,
			.valid_until = pair.valid_until,
		});
	}, [=, this, grant = auth.grant](engine::SendResult result) {
		if (!current() || _sendState.current() != SendState::Sending) {
			return;
		}
		if (auto report = base::take(_submission->tonConnect)) {
			report(TonConnectSendOutcome(
				result,
				operationId,
				_submission->rpcStarted,
				_submission->normal));
			if (!current()) {
				return;
			}
		}
		if (result.operation_id != operationId) {
			LOG(("Wallet Error: engine send result names another operation."));
			recordUnknown();
			return;
		}
		switch (result.phase) {
		case engine::SendPhase::kSubmitted:
			if (userId) {
				const auto user = _session->data().userLoaded(userId);
				if (user && !user->isSelf()) {
					_session->recentMoneyRecipients().bump(user);
					if (!current()) {
						return;
					}
				}
			}
			recordPending(SendError::None);
			return;
		case engine::SendPhase::kSubmissionUnknown:
		case engine::SendPhase::kHandedOff:
		case engine::SendPhase::kIdle:
		case engine::SendPhase::kValidating:
		case engine::SendPhase::kAuthorizing:
		case engine::SendPhase::kPreparing:
		case engine::SendPhase::kPersisting:
		case engine::SendPhase::kReadyToSubmit:
		case engine::SendPhase::kSubmitting:
			recordUnknown();
			return;
		case engine::SendPhase::kFailed:
		case engine::SendPhase::kCancelled:
		case engine::SendPhase::kConfirmed:
		case engine::SendPhase::kReplaced:
		case engine::SendPhase::kSequenceNumberConsumed:
		case engine::SendPhase::kExpired:
		case engine::SendPhase::kSuperseded: {
			const auto record = submittedTransferRecord(operationId, identity);
			if (record && record->handoff == TransferHandoff::Possible) {
				const auto entry = upsertSubmittedTransfer(
					operationId,
					identity,
					generation,
					client);
				if (entry) {
					entry->terminal = result.phase;
					if (entry->fallback && FailedTransferTerminal(
							StoredTransferTerminal(result.phase))) {
						entry->fallback->status = TransferItem::Status::Failure;
					}
				}
			} else {
				retireSubmittedTransferRecord(operationId, identity);
			}
			if (!persistSubmittedTransfers()) {
				LOG(("Wallet Error: terminal transfer facts remain dirty."));
			}
			const auto submission = base::take(_submission);
			if (submission
				&& submission->rpcStarted
				&& FailedTransferTerminal(StoredTransferTerminal(
					PairedSendPhase(result.phase, paired)))) {
				_transferMessages->failSending(
					submission->draft,
					TransferTerminalCode(StoredTransferTerminal(
						PairedSendPhase(result.phase, paired))));
			}
			// A pair refused before its broadcast started names an offer
			// that expired under the confirmed operation, not the fee the
			// user authorized for the normal variant.
			const auto refusal = submission
				? submission->refusal.value_or(
					(paired && !submission->rpcStarted)
						? SendError::QuoteExpired
						: SendError::Failed)
				: SendError::Failed;
			_sendState = SendState::Idle;
			LOG(("Wallet Error: engine send ended in phase %1 (%2)."
				).arg(int(result.phase)).arg(int(refusal)));
			if (!weak) {
				return;
			}
			syncEngineClient();
			if (weak) {
				_historyUpdates.fire({});
			}
			if (!weak) {
				return;
			} else if (refusal == SendError::KeyMismatch) {
				settleKeyMismatch(identity, signingKey, done);
			} else if (done) {
				done(refusal);
			}
		} return;
		}
	}, [=, this, grant = auth.grant](EngineError error) {
		// The signing read happened before any of the bookkeeping below, so
		// what it says about the key is recorded whatever this send becomes.
		noteSecretReadFailure(ProtectedSecretFailure(error), signingRecordId);
		if (!current() || _sendState.current() != SendState::Sending) {
			return;
		} else if (IsSubmissionUnknown(error)) {
			SettleTonConnect(*_submission, SendError::SubmissionUnknown);
			recordUnknown();
			return;
		}
		if (auto report = base::take(_submission->tonConnect)) {
			report({
				.error = _submission->refusal.value_or(SendErrorFrom(error)),
			});
		}
		auto possible = false;
		if (const auto record = submittedTransferRecord(operationId, identity)) {
			possible = (record->handoff == TransferHandoff::Possible);
			if (!possible) {
				retireSubmittedTransferRecord(operationId, identity);
				if (!persistSubmittedTransfers()) {
					LOG(("Wallet Error: unused transfer preparation remains dirty."));
				}
			}
		}
		auto listed = false;
		if (_submission && _submission->rpcStarted && possible) {
			const auto entry = upsertSubmittedTransfer(
				operationId,
				identity,
				generation,
				client);
			if (entry) {
				if (entry->fallback) {
					entry->fallback->status = TransferItem::Status::Failure;
				}
				listed = true;
			}
			if (!persistSubmittedTransfers()) {
				LOG(("Wallet Error: failed transfer facts remain dirty."));
			}
		}
		const auto submission = base::take(_submission);
		if (submission && submission->rpcStarted) {
			_transferMessages->failSending(
				submission->draft,
				error.message);
		}
		auto failed = SendErrorFrom(error);
		if (submission && submission->refusal) {
			failed = *submission->refusal;
		} else if (paired
			&& submission
			&& !submission->rpcStarted
			&& (failed == SendError::Failed
				|| failed == SendError::InvalidRequest
				|| failed == SendError::DataInvalid)) {
			failed = SendError::QuoteExpired;
		}
		LOG(("Wallet Error: engine send failed (%1).").arg(int(failed)));
		_sendState = SendState::Idle;
		if (!weak) {
			return;
		}
		syncEngineClient();
		if (weak && listed) {
			_historyUpdates.fire({});
		}
		if (!weak) {
			return;
		} else if (failed == SendError::KeyMismatch) {
			settleKeyMismatch(identity, signingKey, done);
		} else if (done) {
			done(failed);
		}
	});
	_sendState = SendState::Sending;
}

bool Session::submissionCurrent(
		const std::string &operationId,
		const std::shared_ptr<const PreparedSend> &prepared) const {
	return _submission
		&& prepared
		&& (_submission->operationId == operationId)
		&& (_submission->prepared == prepared)
		&& transferOperationCurrent(
			prepared->identity,
			prepared->generation,
			prepared->client);
}

void Session::submitTransfer(
		std::string operationId,
		std::shared_ptr<const PreparedSend> prepared,
		TransferSubmissionData data,
		Fn<void(TransferSubmissionAnswer)> done) {
	const auto weak = base::make_weak(_engine.get());
	const auto current = [=, this] {
		return weak && submissionCurrent(operationId, prepared);
	};
	const auto refuse = [=, this](SendError error, const QString &diagnostic) {
		if (current()) {
			_submission->refusal = error;
		}
		done({ TransferSubmissionOutcome::Rejected, diagnostic });
	};
	if (!current()) {
		LOG(("Wallet Error: transfer submission refused for a stale "
			"operation or wallet."));
		done({
			TransferSubmissionOutcome::Rejected,
			u"WALLET_TRANSFER_STALE"_q,
		});
		return;
	} else if (_submission->rpcStarted) {
		done({
			TransferSubmissionOutcome::Uncertain,
			u"WALLET_TRANSFER_ALREADY_SUBMITTED"_q,
		});
		return;
	} else if (_clientStopping
		|| !transferClientMatches(prepared->identity, prepared->client)) {
		refuse(
			SendError::SigningUnavailable,
			u"WALLET_TRANSFER_SIGNING_UNAVAILABLE"_q);
		return;
	}
	const auto terms = gaslessTerms();
	if (!current()) {
		refuse(SendError::QuoteExpired, u"WALLET_TRANSFER_STALE"_q);
		return;
	} else if (_clientStopping
		|| !transferClientMatches(prepared->identity, prepared->client)) {
		refuse(
			SendError::SigningUnavailable,
			u"WALLET_TRANSFER_SIGNING_UNAVAILABLE"_q);
		return;
	}
	const auto amount = prepared->args.amountNano;
	const auto ordinary = !prepared->tonConnect && !prepared->nft;
	if (ordinary
		&& TransferAmountBelowMinimum(amount, TransferMinNanos(_session))) {
		refuse(SendError::AmountTooSmall, u"WALLET_TRANSFER_AMOUNT_TOO_SMALL"_q);
		return;
	} else if ((ordinary
			&& (prepared->terms != terms
				|| terms.identity != prepared->identity
				|| _submission->paired != terms.eligible(
					amount,
					prepared->args.destination)))
		|| !_submission->normalFeeAuthorized
		|| _clientStopping) {
		refuse(SendError::QuoteExpired, u"WALLET_TRANSFER_QUOTE_EXPIRED"_q);
		return;
	} else if (data.gasless.has_value() != _submission->paired
		|| data.normal.isEmpty()
		|| data.normal.size() > kTransferDataMaxBytes
		|| (data.gasless
			&& (data.gasless->isEmpty()
				|| data.gasless->size() > kTransferDataMaxBytes))) {
		refuse(
			_submission->paired
				? SendError::QuoteExpired
				: SendError::DataInvalid,
			u"WALLET_TRANSFER_DATA_INVALID"_q);
		return;
	}
	const auto balance = _balanceNano.current();
	if (amount > balance) {
		refuse(SendError::InsufficientBalance, u"WALLET_TRANSFER_BALANCE_LOW"_q);
		return;
	} else if (!_submission->paired
		&& prepared->feeNano > balance - amount) {
		// A paired transfer's fee is the relayer's, so the balance is not
		// asked to cover it.
		refuse(SendError::InsufficientFees, u"WALLET_TRANSFER_FEES_LOW"_q);
		return;
	}
	const auto identity = prepared->identity;
	const auto record = submittedTransferRecord(operationId, identity);
	if (!record) {
		refuse(SendError::Failed, u"WALLET_TRANSFER_STORAGE_FAILED"_q);
		return;
	}
	const auto was = record->handoff;
	record->handoff = TransferHandoff::Possible;
	_submittedTransfersDirty = _submittedTransfersDirty
		|| (was != TransferHandoff::Possible);
	if (!persistSubmittedTransfers()) {
		if (const auto retained = submittedTransferRecord(operationId, identity)) {
			retained->handoff = was;
		}
		LOG(("Wallet Error: transfer handoff could not be stored."));
		refuse(SendError::Failed, u"WALLET_TRANSFER_STORAGE_FAILED"_q);
		return;
	} else if (const auto handoff = _submission->tonConnectHandoff) {
		if (!handoff(QString::fromLatin1(data.normal.toBase64()))) {
			LOG(("Wallet Error: TON Connect transfer could not be stored."));
			refuse(SendError::Failed, u"WALLET_TRANSFER_STORAGE_FAILED"_q);
			return;
		}
	}
	// The request id is not remembered on purpose. The broadcast must
	// reach the server, and the transport's automatic resend of a request
	// answered with a negative or 500-class code repeats the identical
	// body: a resent broadcast is at best redundant and at worst refused
	// for a message the first copy delivered, so every error reaches the
	// fail arm here instead. The answer must bind however late it lands,
	// because it is the only source of the receipt, so the host's timeout
	// never cancels it either; the sender's destructor is the one cancel,
	// and a request still queued at that moment is recovered by the
	// engine journal on the next launch.
	_submission->normal = data.normal;
	_submission->rpcStarted = true;
	const auto weakSession = base::make_weak(_session);
	auto randomId = base::RandomValue<uint64>();
	while (!randomId) {
		randomId = base::RandomValue<uint64>();
	}
	const auto messageId = prepared->nft
		? FullMsgId()
		: _transferMessages->create(prepared->args, randomId);
	_submission->draft = messageId;
	if (const auto report = _submission->started) {
		const auto started = SendStarted{
			.operationId = operationId,
			.message = messageId,
		};
		crl::on_main(_session, [=] { report(started); });
	}
	DEBUG_LOG(("Wallet Info: wallet.sendTransfer data_normal: %1"
		).arg(QString::fromLatin1(data.normal.toBase64())));
	if (data.gasless) {
		DEBUG_LOG(("Wallet Info: wallet.sendTransfer data_gasless: %1"
			).arg(QString::fromLatin1(data.gasless->toBase64())));
	}
	using Flag = MTPwallet_SendTransfer::Flag;
	_stateApi.request(MTPwallet_SendTransfer(
		MTP_flags(data.gasless ? Flag::f_data_gasless : Flag(0)),
		MTP_bytes(data.normal),
		data.gasless ? MTP_bytes(*data.gasless) : MTPbytes(),
		TransferRecipientInput(_session, prepared->args.userId),
		MTP_long(randomId)
	)).done([=, this](const MTPUpdates &result) {
		const auto account = weakSession;
		const auto finish = done;
		const auto sent = SentUpdateFromServer(result);
		const auto receipt = sent ? ReceiptFromServer(*sent) : std::nullopt;
		if (receipt) {
			DEBUG_LOG(("Wallet Info: wallet.sendTransfer accepted, gasless: %1, "
				"msg_hash: %2").arg(Logs::b(receipt->gasless)).arg(
					QString::fromLatin1(receipt->messageHash.toBase64())));
		}
		auto accepted = receipt.has_value();
		if (accepted && weak) {
			accepted = bindTransferReceipt(operationId, prepared, *sent);
		}
		if (account) {
			account->api().applyUpdates(result);
		}
		if (!accepted) {
			LOG(("Wallet Error: wallet.sendTransfer receipt unusable."));
			finish({
				TransferSubmissionOutcome::Uncertain,
				u"WALLET_TRANSFER_RECEIPT_INVALID"_q,
			});
			return;
		}
		finish({ TransferSubmissionOutcome::Accepted });
	}).fail([=, this](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.sendTransfer failed: %1"
			).arg(error.type()));
		const auto refusal = DefiniteTransferRefusal(error);
		if (!refusal) {
			done({ TransferSubmissionOutcome::Uncertain, error.type() });
			return;
		} else if (current()) {
			_submission->refusal = *refusal;
		}
		_transferMessages->failSending(messageId, error.type());
		done({ TransferSubmissionOutcome::Rejected, error.type() });
	}).handleAllErrors().send();
}

void Session::settleKeyMismatch(
		TransferWalletIdentity identity,
		QByteArray signingKey,
		Fn<void(SendError)> done) {
	requestState([=, this](const MTPWalletState &state) {
		applyState(state, false);
		const auto changed = (_address == identity.address)
			&& (_publicKey != signingKey)
			&& (deviceCustodyState().mode != DeviceMode::Full)
			&& !custody().current(_address, _publicKey);
		done(changed ? SendError::KeyChanged : SendError::KeyMismatch);
	}, [=] {
		done(SendError::KeyMismatch);
	});
}

bool Session::bindTransferReceipt(
		const std::string &operationId,
		const std::shared_ptr<const PreparedSend> &prepared,
		const MTPDupdateSentWalletTransaction &data) {
	if (!prepared || !transferOperationCurrent(
			prepared->identity,
			prepared->generation,
			prepared->client)) {
		return true;
	}
	const auto entry = submittedTransfer(operationId);
	if (!submissionCurrent(operationId, prepared)
		&& (!entry || entry->client.lock() != prepared->client)) {
		return true;
	}
	return applySubmittedUpdate(operationId, data);
}

} // namespace Wallet
