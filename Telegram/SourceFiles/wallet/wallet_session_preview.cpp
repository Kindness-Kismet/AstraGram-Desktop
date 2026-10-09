#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

uint64 Session::createPreviewOwner(rpl::lifetime &lifetime) {
	if (!_preview) {
		_preview = std::make_unique<PreviewState>();
	}
	const auto owner = ++_preview->lastOwner;
	_preview->owners.emplace(owner, 0);
	lifetime.add(crl::guard(_preview.get(), [=, this] {
		retirePreviewOwner(owner);
	}));
	refreshGaslessInfo(true);
	return owner;
}

void Session::estimateFee(
		KeyAuthorization auth,
		uint64 owner,
		const SendArgs &args,
		Fn<void(FeeResult)> done) {
	if (!_preview || !_preview->owners.contains(owner)) {
		return;
	}
	done = LoggedFeeDone(std::move(done));
	const auto transfersCollectible = !args.collectible.isEmpty();
	const auto collectible = transfersCollectible
		? CanonicalAddress(args.collectible)
		: QString();
	const auto recipient = transfersCollectible
		? CanonicalAddress(args.destination)
		: QString();
	const auto collectibleInvalid = transfersCollectible
		&& (collectible.isEmpty()
			|| recipient.isEmpty()
			|| (recipient == _address)
			|| (recipient == collectible)
			|| (!args.comment.text.isEmpty() && !args.comment.isPublic));
	const auto inputError = !SendCommentFits(args.comment.text)
		? SendError::CommentTooLong
		: collectibleInvalid
		? SendError::InvalidRequest
		: transfersCollectible
		? SendError::None
		: (args.amountNano <= 0
			|| FormatFriendly(args.destination, args.bounce).isEmpty())
		? SendError::InvalidRequest
		: TransferAmountBelowMinimum(
			args.amountNano,
			TransferMinNanos(_session))
		? SendError::AmountTooSmall
		: SendError::None;
	if (inputError != SendError::None) {
		cancelFeeEstimate(owner);
		if (done) {
			done(FeeResult{ .error = inputError });
		}
		return;
	}
	const auto isPrivate = !args.comment.text.isEmpty()
		&& !args.comment.isPublic;
	const auto keyed = isPrivate && auth.valid();
	const auto privateEpoch = keyed
		? std::make_optional(vault().clearEpoch())
		: std::nullopt;
	if (keyed && !ReadAuthorized(*this, auth)) {
		cancelFeeEstimate(owner);
		if (done) {
			done(FeeResult{ .error = SendError::Locked });
		}
		return;
	}
	ensureLoaded();
	const auto terms = gaslessTerms();
	const auto i = _preview->owners.find(owner);
	if (i == end(_preview->owners)) {
		return;
	}
	auto request = PreviewRequest{
		.identity = transferWalletIdentity(),
		.terms = terms,
		.owner = owner,
		.revision = ++i->second,
		.generation = _networkGeneration,
		.privateEpoch = privateEpoch,
		.client = _engine->client(),
		.auth = keyed ? std::move(auth) : KeyAuthorization(),
		.args = args,
		.operationId = transfersCollectible ? NewRecordId() : std::string(),
		.done = std::move(done),
		.feeOnly = isPrivate && !keyed,
	};
	if (transfersCollectible) {
		request.args.collectible = collectible;
		request.args.amountNano = kCollectibleTransferAttachedNanos;
	}
	enqueuePreview(std::move(request));
}

void Session::enqueuePreview(PreviewRequest request) {
	const auto owner = request.owner;
	const auto queued = ranges::find(
		_preview->queue,
		owner,
		&PreviewRequest::owner);
	if (queued != end(_preview->queue)) {
		*queued = std::move(request);
	} else {
		_preview->queue.push_back(std::move(request));
	}
	_previewPending = true;
	if (_preview->active && _preview->active->request.owner == owner) {
		_preview->active->request.done = nullptr;
		cancelPreview();
	}
	startPreview();
}

void Session::estimateTonConnect(
		uint64 owner,
		std::shared_ptr<const TonConnectTransfer> transfer,
		Fn<void(FeeResult)> done) {
	if (!_preview || !_preview->owners.contains(owner)) {
		return;
	}
	done = LoggedFeeDone(std::move(done));
	if (!transfer || !transfer->request || transfer->messages.empty()) {
		cancelFeeEstimate(owner);
		done(FeeResult{ .error = SendError::InvalidRequest });
		return;
	}
	ensureLoaded();
	const auto terms = gaslessTerms();
	const auto i = _preview->owners.find(owner);
	if (i == end(_preview->owners)) {
		return;
	}
	const auto &first = transfer->messages.front();
	const auto parsed = ParseAddress(first.destination);
	auto request = PreviewRequest{
		.identity = transferWalletIdentity(),
		.terms = terms,
		.owner = owner,
		.revision = ++i->second,
		.generation = _networkGeneration,
		.client = _engine->client(),
		.args = SendArgs{
			.destination = CanonicalAddress(first.destination),
			.amountNano = transfer->totalNano,
			.bounce = parsed ? parsed->bounceable : true,
		},
		.done = std::move(done),
		.tonConnect = transfer->request,
	};
	enqueuePreview(std::move(request));
}

void Session::cancelFeeEstimate(uint64 owner) {
	if (!_preview) {
		return;
	}
	const auto i = _preview->owners.find(owner);
	if (i == end(_preview->owners)) {
		return;
	}
	++i->second;
	const auto queued = ranges::find(
		_preview->queue,
		owner,
		&PreviewRequest::owner);
	if (queued != end(_preview->queue)) {
		_preview->queue.erase(queued);
	}
	if (_preview->active && _preview->active->request.owner == owner) {
		_preview->active->request.done = nullptr;
		cancelPreview();
	}
	_previewPending = _preview->active.has_value() || !_preview->queue.empty();
}

void Session::resolveCommentRecipient(
		const QString &destination,
		bool bounce,
		const QByteArray &recipientPublicKey,
		Fn<void(CommentRecipient)> done) {
	resolveCommentRecipientAttempt(
		destination,
		bounce,
		recipientPublicKey,
		std::move(done),
		0);
}

void Session::resolveCommentRecipientAttempt(
		const QString &destination,
		bool bounce,
		const QByteArray &recipientPublicKey,
		Fn<void(CommentRecipient)> done,
		int attempt) {
	// WHY: only the recipient's own answer may turn a private comment into a
	// public one, so a busy engine slot, a client swap or a provider that did
	// not answer is asked again a few times and then stated as Unknown.
	const auto retry = [=, this] {
		if (attempt >= kCommentRecipientRetries) {
			done(CommentRecipient::Unknown);
			return;
		}
		base::call_delayed(kCommentRecipientRetryDelay, _session, [=, this] {
			resolveCommentRecipientAttempt(
				destination,
				bounce,
				recipientPublicKey,
				done,
				attempt + 1);
		});
	};
	const auto recipient = FormatFriendly(destination, bounce);
	const auto client = _engine->client();
	if (recipient.isEmpty()) {
		done(CommentRecipient::Unknown);
		return;
	} else if (!client || _clientStopping) {
		retry();
		return;
	}
	auto request = engine::EncryptedCommentRecipientRequest{
		.recipient = recipient.toStdString(),
		.recipient_public_key = EngineKey(recipientPublicKey),
	};
	_engine->run([client, request = std::move(request)] {
		return client->resolve_encrypted_comment_recipient(request);
	}, [=](std::vector<uint8_t>) {
		done(CommentRecipient::Encryptable);
	}, [=](EngineError error) {
		if (SendErrorFrom(error) == SendError::CommentEncryptionUnavailable) {
			done(CommentRecipient::PlainOnly);
		} else {
			retry();
		}
	});
}

void Session::resolveDnsName(
		const QString &name,
		Fn<void(std::optional<QString>)> done,
		Fn<void(DnsLookupError)> fail) {
	const auto client = _engine->client();
	if (!client || _clientStopping) {
		fail(DnsLookupError::Failed);
		return;
	}
	_engine->run([client, name = name.toStdString()] {
		return client->resolve_dns(name);
	}, [=](std::optional<std::string> address) {
		done(address
			? std::make_optional(QString::fromStdString(*address))
			: std::nullopt);
	}, [=, this](EngineError error) {
		LOG(("Wallet Error: dns lookup failed: %1").arg(error.message));
		// A comment decryption may hold the slot; mid-transfer it is final.
		const auto busy = (SendErrorFrom(error) == SendError::AlreadySending)
			&& (_sendState.current() != SendState::Sending);
		fail(busy ? DnsLookupError::Busy : DnsLookupError::Failed);
	});
}

void Session::retirePreviewOwner(uint64 owner) {
	cancelFeeEstimate(owner);
	_preview->owners.remove(owner);
	if (_preview->owners.empty()) {
		retireGaslessRequest();
	}
}

bool Session::previewCurrent(const PreviewRequest &request) const {
	const auto i = _preview->owners.find(request.owner);
	return i != end(_preview->owners) && i->second == request.revision;
}

bool Session::transferClientMatches(
		const TransferWalletIdentity &identity,
		const std::shared_ptr<engine::WalletClient> &client) const {
	if (!client || client != _engine->client() || !_custody) {
		return false;
	}
	const auto record = _custody->current(
		identity.address,
		identity.publicKey);
	return record
		&& record->recordId == _clientRecordId
		&& record->network == int(engine::Network::kMainnet);
}

bool Session::previewClientMatches(
		const TransferWalletIdentity &identity,
		const std::shared_ptr<engine::WalletClient> &client) const {
	return transferClientMatches(identity, client)
		|| (client
			&& client == _engine->client()
			&& _clientRecordId.isEmpty()
			&& _clientPreviewIdentity
			&& _clientPreviewIdentity->address == identity.address
			&& _clientPreviewIdentity->publicKey == identity.publicKey);
}

auto Session::signingClient() const
-> std::shared_ptr<engine::WalletClient> {
	return _clientRecordId.isEmpty() ? nullptr : _engine->client();
}

bool Session::signingReady() const {
	return _signingReady.current();
}

rpl::producer<bool> Session::signingReadyValue() const {
	return _signingReady.value();
}

void Session::updateSigningReady() {
	_signingReady = (signingClient() != nullptr)
		&& !_clientStopping
		&& _sendRecoveryReady;
}

void Session::settleDeferredDecrypts() {
	if (_clientStopping || _deferredDecrypts.empty()) {
		return;
	}
	for (auto &deferred : base::take(_deferredDecrypts)) {
		decryptComment(std::move(deferred));
	}
}

SendError Session::previewError(const PreviewRequest &request) {
	const auto terms = gaslessTerms();
	const auto ordinary = !request.tonConnect
		&& request.args.collectible.isEmpty();
	if (!previewCurrent(request)) {
		return SendError::QuoteExpired;
	} else if (request.privateEpoch
		&& (*request.privateEpoch != vault().clearEpoch()
			|| !ReadAuthorized(*this, request.auth))) {
		return SendError::Locked;
	} else if (request.generation != _networkGeneration
		|| !request.identity
		|| !transferWalletIdentityCurrent(*request.identity)) {
		return SendError::Failed;
	} else if (_presence.current() != Presence::Ready
		|| request.identity->publicKey.size() != kCustodyPublicKeySize
		|| (!request.tonConnect && request.args.amountNano <= 0)
		|| request.args.destination.isEmpty()) {
		return SendError::InvalidRequest;
	} else if (ordinary
		&& TransferAmountBelowMinimum(
			request.args.amountNano,
			TransferMinNanos(_session))) {
		return SendError::AmountTooSmall;
	} else if (ordinary
		&& (request.terms != terms || terms.identity != request.identity)) {
		return SendError::QuoteExpired;
	} else if (_clientStopping
		|| !previewClientMatches(*request.identity, request.client)) {
		return SendError::SigningUnavailable;
	} else if (_sendState.current() == SendState::Sending
		|| _rotating
		|| custody().pendingRotation) {
		return SendError::AlreadySending;
	} else if (_pending || _sendUnresolved) {
		return SendError::PreviousUnresolved;
	}
	return SendError::None;
}

void Session::startPreview() {
	if (_preview->dispatching || _preview->active) {
		return;
	}
	_preview->dispatching = true;
	const auto weak = base::make_weak(_preview.get());
	while (!_preview->queue.empty()) {
		auto next = std::move(_preview->queue.front());
		_preview->queue.pop_front();
		_previewPending = !_preview->queue.empty();
		if (!previewCurrent(next)) {
			continue;
		}
		const auto error = previewError(next);
		if (!weak) {
			return;
		} else if (!previewCurrent(next)) {
			continue;
		} else if (error != SendError::None) {
			if (const auto done = base::take(next.done)) {
				done(FeeResult{ .error = error });
				if (!weak) {
					return;
				}
			}
			continue;
		}
		const auto flight = ++_preview->lastFlight;
		_preview->active = PreviewState::Flight{
			.id = flight,
			.request = std::move(next),
		};
		_previewPending = true;
		const auto &request = _preview->active->request;
		if (!request.args.collectible.isEmpty()) {
			previewCollectible(flight);
		} else if (request.tonConnect || request.args.comment.text.isEmpty()) {
			previewPrepared(flight, engine::SendMessageBody::kEmpty{});
		} else if (request.args.comment.isPublic) {
			previewPrepared(flight, engine::SendMessageBody::kComment{
				.text = request.args.comment.text.toUtf8().toStdString(),
			});
		} else if (request.feeOnly) {
			previewPrepared(flight, engine::SendMessageBody::kRawPayload{
				.boc = EncryptedCommentFeeBody(
					request.args.comment.text).toStdString(),
			});
		} else {
			const auto client = request.client;
			const auto signingRecordId = _clientRecordId;
			auto encrypt = engine::CreateEncryptedCommentRequest{
				.recipient = FormatFriendly(
					request.args.destination,
					request.args.bounce).toStdString(),
				.comment = request.args.comment.text.toUtf8().toStdString(),
				.recipient_public_key = EngineKey(
					request.args.recipientPublicKey),
			};
			_engine->run([client, encrypt = std::move(encrypt)] {
				return client->create_encrypted_comment(encrypt);
			}, [=, this](engine::Boc body) {
				previewPrepared(flight, engine::SendMessageBody::kRawPayload{
					.boc = std::move(body),
				});
			}, [=, this](EngineError error) {
				// Encrypting for the recipient reads this wallet's own key,
				// so a refused read here says the same thing about it as a
				// refused signature does.
				noteSecretReadFailure(
					ProtectedSecretFailure(error),
					signingRecordId);
				finishPreview(flight, FeeResult{
					.error = SendErrorFrom(error),
				});
			});
		}
		if (!weak) {
			return;
		} else if (_preview->active) {
			break;
		}
	}
	_preview->dispatching = false;
}

void Session::previewPrepared(uint64 flight, engine::SendMessageBody body) {
	if (!_preview->active || _preview->active->id != flight) {
		return;
	}
	auto &active = *_preview->active;
	if (!active.request.done || !previewCurrent(active.request)) {
		finishPreview(flight, FeeResult{ .error = SendError::Failed });
		return;
	}
	const auto error = previewError(active.request);
	if (error != SendError::None) {
		finishPreview(flight, FeeResult{ .error = error });
		return;
	}
	const auto tonConnect = active.request.tonConnect;
	active.intent = tonConnect
		? std::make_shared<const engine::SendIntent>(tonConnect->intent)
		: std::make_shared<const engine::SendIntent>(
			IntentFromArgs(active.request.args, std::move(body)));
	active.stage = PreviewState::Flight::Stage::Previewing;
	const auto client = active.request.client;
	const auto paired = !tonConnect && active.request.terms.eligible(
		active.request.args.amountNano,
		active.request.args.destination);
	const auto own = active.request.identity
		? active.request.identity->address
		: QString();
	const auto total = active.request.args.amountNano;
	auto request = engine::SendPreviewRequest{ .intent = *active.intent };
	_engine->run([client, tonConnect, request = std::move(request)] {
		return tonConnect
			? client->preview_ton_connect(*tonConnect)
			: client->preview_send(request);
	}, [=, this](engine::SendPreview preview) {
		const auto fee = DecimalInt64(preview.emulation.wallet_fees_nanograms);
		auto result = (fee && *fee >= 0)
			? FeeResult{ .feeNano = *fee }
			: FeeResult{ .error = SendError::Failed };
		if (tonConnect && result.error == SendError::None) {
			result.emulation = std::make_shared<const TonConnectEmulation>(
				ParseTonConnectEmulation(preview.emulation, own, total));
		}
		finishPreview(flight, std::move(result));
	}, [=, this](EngineError error) {
		// The preview emulates the ordinary form of the transfer, so it
		// refuses an amount that would leave the wallet without its fee.
		// A fee-free transfer is not paid for by the wallet, and the engine
		// asks nothing but the amount of it when it signs the pair, so the
		// refusal is the fee reserve alone and this transfer carries none.
		if (paired && IsInsufficientForFees(error)) {
			finishPreview(flight, FeeResult{ .feeNano = 0 });
			return;
		}
		finishPreview(flight, FeeResult{ .error = SendErrorFrom(error) });
	});
}

void Session::previewCollectible(uint64 flight) {
	if (!_preview->active || _preview->active->id != flight) {
		return;
	}
	auto &active = *_preview->active;
	if (!active.request.done || !previewCurrent(active.request)) {
		finishPreview(flight, FeeResult{ .error = SendError::Failed });
		return;
	}
	const auto error = previewError(active.request);
	if (error != SendError::None) {
		finishPreview(flight, FeeResult{ .error = error });
		return;
	}
	active.nft = std::make_shared<const engine::NftTransferIntent>(
		CollectibleTransferIntent(active.request.args));
	active.stage = PreviewState::Flight::Stage::Previewing;
	const auto client = active.request.client;
	auto request = engine::NftTransferPreviewRequest{
		.operation_id = active.request.operationId,
		.intent = *active.nft,
	};
	_engine->run([client, request = std::move(request)] {
		return client->preview_nft_transfer(request);
	}, [=, this](engine::SendPreview preview) {
		const auto fee = DecimalInt64(
			preview.emulation.trace_fees_nanograms);
		finishPreview(flight, (fee && *fee >= 0)
			? FeeResult{ .feeNano = *fee }
			: FeeResult{ .error = SendError::Failed });
	}, [=, this](EngineError error) {
		finishPreview(flight, FeeResult{ .error = SendErrorFrom(error) });
	});
}

void Session::finishPreview(uint64 flight, FeeResult result) {
	if (!_preview->active || _preview->active->id != flight) {
		return;
	}
	_preview->active->finished = true;
	_preview->active->result = result;
	settlePreview();
}

void Session::cancelPreview() {
	if (!_preview->active
		|| _preview->active->stage != PreviewState::Flight::Stage::Previewing
		|| _preview->active->cancelIssued) {
		return;
	}
	_preview->active->cancelIssued = true;
	const auto flight = _preview->active->id;
	const auto client = _preview->active->request.client;
	_engine->runQuick([client] {
		client->cancel_send_preview();
	}, [=, this] {
		finishPreviewCancel(flight);
	}, [=, this](EngineError) {
		finishPreviewCancel(flight);
	});
}

void Session::finishPreviewCancel(uint64 flight) {
	if (!_preview->active || _preview->active->id != flight) {
		return;
	}
	_preview->active->cancelFinished = true;
	settlePreview();
}

void Session::settlePreview() {
	if (!_preview->active
		|| !_preview->active->finished
		|| (_preview->active->cancelIssued
			&& !_preview->active->cancelFinished)) {
		return;
	}
	auto flight = *base::take(_preview->active);
	_previewPending = !_preview->queue.empty();
	const auto weak = base::make_weak(_preview.get());
	if (flight.request.done && previewCurrent(flight.request)) {
		const auto error = previewError(flight.request);
		if (!weak) {
			return;
		} else if (!previewCurrent(flight.request)) {
			startPreview();
			return;
		} else if (error != SendError::None) {
			flight.result = FeeResult{ .error = error };
		} else if (flight.result.error == SendError::None
			&& flight.intent
			&& !flight.cancelIssued
			&& flight.request.feeOnly) {
			flight.intent = nullptr;
		} else if (flight.result.error == SendError::None
			&& (flight.intent || flight.nft)
			&& !flight.cancelIssued) {
			flight.result.prepared = std::make_shared<const PreparedSend>(
				PreparedSend{
					.args = flight.request.args,
					.identity = *flight.request.identity,
					.terms = flight.request.terms,
					.intent = std::move(flight.intent),
					.nft = std::move(flight.nft),
					.operationId = flight.request.operationId,
					.feeNano = flight.result.feeNano,
					.owner = flight.request.owner,
					.revision = flight.request.revision,
					.generation = flight.request.generation,
					.privateEpoch = flight.request.privateEpoch,
					.client = flight.request.client,
					.tonConnect = (flight.request.tonConnect != nullptr),
				});
		} else if (flight.result.error == SendError::None) {
			flight.result = FeeResult{ .error = SendError::Failed };
		}
		if (const auto done = base::take(flight.request.done)) {
			done(std::move(flight.result));
			if (!weak) {
				return;
			}
		}
	}
	startPreview();
}

void Session::retirePreviews(SendError error) {
	if (!_preview) {
		return;
	}
	auto retired = base::take(_preview->queue);
	if (_preview->active) {
		auto request = _preview->active->request;
		request.done = base::take(_preview->active->request.done);
		retired.push_front(std::move(request));
		cancelPreview();
	}
	_previewPending = _preview->active.has_value();
	const auto weak = base::make_weak(_preview.get());
	for (auto &request : retired) {
		if (previewCurrent(request)) {
			if (const auto done = base::take(request.done)) {
				done(FeeResult{ .error = error });
				if (!weak) {
					return;
				}
			}
		}
	}
}

} // namespace Wallet
