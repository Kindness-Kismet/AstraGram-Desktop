#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

namespace SessionDetails {

void FinishShareFetch(
		MTP::Sender &api,
		base::Timer &deadline,
		const std::shared_ptr<ShareFetch> &state) {
	deadline.cancel();
	for (auto &id : state->requests) {
		api.request(base::take(id)).cancel();
	}
	for (const auto shiftedDcId : base::take(state->sessions)) {
		api.instance().killSession(shiftedDcId);
	}
}

void FailShareFetch(
		MTP::Sender &api,
		base::Timer &deadline,
		const std::shared_ptr<ShareFetch> &state,
		const QString &error) {
	LOG(("Wallet Error: share fetch failed error=%1 export_request=%2 "
		"elapsed_ms=%3 pending=%4 total=%5."
		).arg(error
		).arg(state->exportRequestId
		).arg(crl::now() - state->startedAt
		).arg(state->pending
		).arg(state->shares.size()));
	for (auto i = 0; i != state->shares.size(); ++i) {
		LOG(("Wallet Error: share fetch holder export_request=%1 "
			"index=%2 dc=%3 request=%4 share_bytes=%5."
			).arg(state->exportRequestId
			).arg(i
			).arg(state->dcs[i]
			).arg(state->requests[i]
			).arg(state->shares[i].size()));
	}
	FinishShareFetch(api, deadline, state);
	if (const auto fail = base::take(state->fail)) {
		fail(error);
	}
}

[[nodiscard]] bool OpenSharePart(
		const std::shared_ptr<ShareFetch> &state,
		int index,
		const QByteArray &data) {
	auto share = PhraseShares::DecryptShare(state->keys, data);
	if (!share) {
		LOG(("Wallet Error: share part could not be opened "
			"export_request=%1 index=%2 dc=%3 encrypted_bytes=%4."
			).arg(state->exportRequestId
			).arg(index
			).arg(state->dcs[index]
			).arg(data.size()));
		return false;
	}
	state->shares[index] = std::move(*share);
	return true;
}

[[nodiscard]] QString TransferTerminalCode(TransferTerminal terminal) {
	switch (terminal) {
	case TransferTerminal::Failed:
		return u"WALLET_TRANSFER_FAILED"_q;
	case TransferTerminal::Cancelled:
		return u"WALLET_TRANSFER_CANCELLED"_q;
	case TransferTerminal::Expired:
		return u"WALLET_TRANSFER_EXPIRED"_q;
	case TransferTerminal::Replaced:
		return u"WALLET_TRANSFER_REPLACED"_q;
	case TransferTerminal::SequenceNumberConsumed:
		return u"WALLET_TRANSFER_SEQNO_CONSUMED"_q;
	case TransferTerminal::Superseded:
		return u"WALLET_TRANSFER_SUPERSEDED"_q;
	case TransferTerminal::None:
	case TransferTerminal::Confirmed:
		return QString();
	}
	Unexpected("Terminal value in TransferTerminalCode.");
}

[[nodiscard]] SendError SendErrorFrom(const EngineError &error) {
	if (!error.underlying) {
		return SendError::Failed;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_client_error::LocalSigningUnavailable &) {
		return SendError::SigningUnavailable;
	} catch (const engine::wallet_client_error::InsufficientBalance &) {
		return SendError::InsufficientBalance;
	} catch (const engine::wallet_client_error::InsufficientBalanceForFees &) {
		return SendError::InsufficientFees;
	} catch (const engine::wallet_client_error
			::PreviousSubmissionUnresolved &) {
		return SendError::PreviousUnresolved;
	} catch (const engine::wallet_client_error::WalletSeqnoNotAdvanced &) {
		return SendError::PreviousUnresolved;
	} catch (const engine::wallet_client_error::SendAlreadyInProgress &) {
		return SendError::AlreadySending;
	} catch (const engine::wallet_client_error
			::SendPreviewAlreadyInProgress &) {
		return SendError::AlreadySending;
	} catch (const engine::wallet_client_error::InvalidSendRequest &) {
		return SendError::InvalidRequest;
	} catch (const engine::wallet_client_error
			::EncryptedCommentUnavailable &) {
		return SendError::CommentEncryptionUnavailable;
	} catch (const engine::wallet_client_error::NftTransferUnavailable &) {
		return SendError::CollectibleUnavailable;
	} catch (const engine::wallet_client_error
			::NftTransferEmulationRejected &) {
		return SendError::CollectibleRejected;
	} catch (...) {
	}
	return SendError::Failed;
}

[[nodiscard]] bool IsInsufficientForFees(const EngineError &error) {
	if (!error.underlying) {
		return false;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_client_error
			::InsufficientBalanceForFees &) {
		return true;
	} catch (...) {
	}
	return false;
}

[[nodiscard]] bool IsSubmissionUnknown(const EngineError &error) {
	if (!error.underlying) {
		return false;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_client_error::SubmissionUnknown &) {
		return true;
	} catch (...) {
	}
	return false;
}

// A 4xx answer is the server refusing the method without executing it,
// the same reading the engine gives its own explicit-rejection status
// list, so nothing was broadcast and a fresh signature for the next
// attempt is safe. A 5xx, a negative or a local code (a transport
// timeout, for one) may have executed the method before the answer was
// lost, so it stays uncertain and the engine's journal keeps the
// operation unresolved instead of freeing the slot.
[[nodiscard]] std::optional<SendError> DefiniteTransferRefusal(
		const MTP::Error &error) {
	const auto code = error.code();
	if (code < 400 || code >= 500) {
		return std::nullopt;
	} else if (MTP::IgnoreError(error)) {
		return SendError::Silent;
	}
	const auto &type = error.type();
	return (type == u"WALLET_TRANSFER_SEND_FAILED"_q)
		? SendError::Rejected
		: (type == u"WALLET_TRANSFER_DATA_INVALID"_q)
		? SendError::DataInvalid
		: (type == u"WALLET_KEY_MISMATCH"_q)
		? SendError::KeyMismatch
		: SendError::Failed;
}

[[nodiscard]] MTPInputUser TransferRecipientInput(
		not_null<Main::Session*> session,
		UserId id) {
	const auto user = id ? session->data().userLoaded(id) : nullptr;
	return (user && !user->isSelf() && user->accessHash())
		? MTP_inputUser(MTP_long(id.bare), MTP_long(user->accessHash()))
		: MTP_inputUserEmpty();
}

[[nodiscard]] QString RotationErrorToken(const EngineError &error) {
	if (!error.underlying) {
		return u"ROTATION_FAILED"_q;
	}
	try {
		std::rethrow_exception(error.underlying);
	} catch (const engine::wallet_client_error::KeyRotationUnavailable &) {
		return u"ROTATION_PREPARE_FAILED"_q;
	} catch (const engine::wallet_client_error::InvalidProtectedSecret &) {
		return u"ROTATION_PREPARE_FAILED"_q;
	} catch (const engine::wallet_client_error::LocalSigningUnavailable &) {
		return u"ROTATION_SIGNING_UNAVAILABLE"_q;
	} catch (const engine::wallet_client_error::SendAlreadyInProgress &) {
		return u"ROTATION_ALREADY_SENDING"_q;
	} catch (const engine::wallet_client_error
			::PreviousSubmissionUnresolved &) {
		return u"ROTATION_ALREADY_SENDING"_q;
	} catch (const engine::wallet_client_error::WalletSeqnoNotAdvanced &) {
		return u"ROTATION_ALREADY_SENDING"_q;
	} catch (const engine::wallet_client_error::InsufficientBalanceForFees &) {
		return u"ROTATION_FEES"_q;
	} catch (const engine::wallet_client_error::SendFailed &) {
		return u"ROTATION_REFUSED"_q;
	} catch (...) {
	}
	return u"ROTATION_FAILED"_q;
}

[[nodiscard]] engine::SendIntent IntentFromArgs(
		const SendArgs &args,
		engine::SendMessageBody body) {
	auto message = engine::SendMessage{
		.destination = FormatFriendly(
			args.destination,
			args.bounce).toStdString(),
		.amount = engine::SendAmount(engine::SendAmount::kExact{
			.nanograms = QString::number(args.amountNano).toStdString(),
		}),
		.body = std::move(body),
		.bounce = args.bounce,
		.state_init = std::nullopt,
	};
	return engine::SendIntent{
		.expiration = engine::SendExpiration(
			engine::SendExpiration::kEngineDefault{}),
		.messages = { std::move(message) },
	};
}

[[nodiscard]] engine::NftTransferIntent CollectibleTransferIntent(
		const SendArgs &args) {
	auto payload = args.comment.text.isEmpty()
		? engine::NftTransferPayload(engine::NftTransferPayload::kEmpty{})
		: engine::NftTransferPayload(engine::NftTransferPayload::kComment{
			.text = args.comment.text.toUtf8().toStdString(),
		});
	return engine::NftTransferIntent{
		.nft_address = FormatFriendly(args.collectible, true).toStdString(),
		.recipient = FormatFriendly(
			args.destination,
			args.bounce).toStdString(),
		.funding = engine::NftTransferFunding(
			engine::NftTransferFunding::kExact{
				.attached_nanograms = QString::number(
					args.amountNano).toStdString(),
				.forward_nanograms = QString::number(
					kCollectibleTransferForwardNanos).toStdString(),
			}),
		.payload = std::move(payload),
		.expiration = engine::SendExpiration(
			engine::SendExpiration::kEngineDefault{}),
	};
}

[[nodiscard]] std::optional<TonConnectTransfer> TonConnectTransferFromEngine(
		const engine::SendRequest &request) {
	auto result = TonConnectTransfer{
		.request = std::make_shared<const engine::SendRequest>(request),
	};
	for (const auto &message : request.intent.messages) {
		const auto &amount = message.amount.get_variant();
		const auto exact = std::get_if<engine::SendAmount::kExact>(&amount);
		const auto nano = exact
			? DecimalInt64(exact->nanograms)
			: std::optional<int64>();
		if (!nano
			|| *nano < 0
			|| *nano > std::numeric_limits<int64>::max() - result.totalNano) {
			return std::nullopt;
		}
		result.totalNano += *nano;
		auto entry = TonConnectMessage{
			.destination = QString::fromStdString(message.destination),
			.amountNano = *nano,
			.deploys = message.state_init.has_value(),
		};
		const auto &body = message.body.get_variant();
		const auto comment = std::get_if<
			engine::SendMessageBody::kComment>(&body);
		const auto raw = std::get_if<
			engine::SendMessageBody::kRawPayload>(&body);
		if (comment) {
			entry.comment = QString::fromStdString(comment->text);
		} else if (raw) {
			const auto boc = QString::fromStdString(raw->boc);
			if (const auto text = Gram::TextCommentFromBoc(boc)) {
				entry.comment = *text;
			} else {
				entry.payload = boc;
			}
		}
		result.messages.push_back(std::move(entry));
	}
	const auto &expiration = request.intent.expiration.get_variant();
	const auto until = std::get_if<
		engine::SendExpiration::kExact>(&expiration);
	if (until) {
		result.validUntil = TimeId(std::min<uint64>(
			until->unix_timestamp,
			std::numeric_limits<TimeId>::max()));
	}
	return result;
}

[[nodiscard]] std::vector<TonConnectSignDataField> DecodedCellFields(
		engine::TonConnectDerivedSession &session,
		const std::string &schema,
		const std::string &cell) {
	using Decoding = engine::TonConnectSignDataCellDecoding;
	auto result = std::vector<TonConnectSignDataField>();
	try {
		const auto decoding = session.decode_sign_data_cell(schema, cell);
		const auto decoded = std::get_if<Decoding::kDecoded>(
			&decoding.get_variant());
		if (!decoded) {
			return result;
		}
		result.reserve(decoded->fields.size());
		for (const auto &field : decoded->fields) {
			result.push_back({
				.depth = int(std::min(
					field.depth,
					uint32_t(std::numeric_limits<int>::max()))),
				.name = QString::fromStdString(field.name),
				.value = QString::fromStdString(field.value),
			});
		}
	} catch (...) {
		// Display only: a failed decode must not cost the request its answer.
		return {};
	}
	return result;
}

[[nodiscard]] TonConnectSignData TonConnectSignDataFromEngine(
		engine::TonConnectDerivedSession &session,
		const engine::TonConnectSignDataRequest &request) {
	using Payload = engine::TonConnectSignDataPayload;
	using Type = TonConnectSignDataType;
	auto result = TonConnectSignData{
		.request = std::make_shared<const engine::TonConnectSignDataRequest>(
			request),
	};
	std::visit([&](const auto &data) {
		using T = std::decay_t<decltype(data)>;
		if constexpr (std::is_same_v<T, Payload::kText>) {
			result.type = Type::Text;
			result.data = QString::fromStdString(data.text);
		} else if constexpr (std::is_same_v<T, Payload::kBinary>) {
			result.type = Type::Binary;
			result.data = QString::fromStdString(data.bytes);
		} else if constexpr (std::is_same_v<T, Payload::kCell>) {
			result.type = Type::Cell;
			result.data = QString::fromStdString(data.cell);
			result.schema = QString::fromStdString(data.schema);
			result.fields = DecodedCellFields(session, data.schema, data.cell);
		}
	}, request.payload.get_variant());
	return result;
}

[[nodiscard]] int TonConnectProtocolCode(
		engine::TonConnectRpcErrorCode code) {
	using Code = engine::TonConnectRpcErrorCode;
	switch (code) {
	case Code::kUnknown: return 0;
	case Code::kBadRequest: return 1;
	case Code::kUnknownApp: return 100;
	case Code::kUserDeclined: return 300;
	case Code::kMethodNotSupported: return 400;
	}
	return -1;
}

[[nodiscard]] TonConnectAppRequest TonConnectAppRequestFromEngine(
		engine::TonConnectDerivedSession &session,
		engine::TonConnectDerivedRequest derived) {
	using Kind = TonConnectRequestKind;
	using Incoming = engine::TonConnectIncomingRequest;
	auto result = TonConnectAppRequest();
	const auto &variant = derived.request.get_variant();
	std::visit([&](const auto &data) {
		result.id = QString::fromStdString(data.id);
		result.method = QString::fromStdString(data.method);
	}, variant);
	if (const auto send = std::get_if<Incoming::kSendTransaction>(&variant)) {
		auto transfer = TonConnectTransferFromEngine(send->request);
		result.kind = transfer ? Kind::SendTransaction : Kind::Invalid;
		if (transfer) {
			result.transfer = std::make_shared<const TonConnectTransfer>(
				std::move(*transfer));
		}
	} else if (const auto sign = std::get_if<Incoming::kSignData>(&variant)) {
		result.kind = Kind::SignData;
		result.signData = std::make_shared<const TonConnectSignData>(
			TonConnectSignDataFromEngine(session, sign->request));
	} else if (std::get_if<Incoming::kSignMessage>(&variant)) {
		result.kind = Kind::Unsupported;
		result.rejection = TonConnectError::MethodNotSupported;
	} else if (std::get_if<Incoming::kDisconnect>(&variant)) {
		result.kind = Kind::Disconnect;
	} else {
		using Code = engine::TonConnectRpcErrorCode;
		const auto bad = std::get_if<Incoming::kUnsupported>(&variant);
		if (bad) {
			LOG(("Wallet Error: TON Connect %1 request not handled, "
				"code %2: %3"
				).arg(result.method
				).arg(TonConnectProtocolCode(bad->error_code)
				).arg(QString::fromStdString(bad->error_message)));
		}
		const auto known = (result.method == u"sendTransaction"_q)
			|| (result.method == u"signData"_q);
		result.kind = known
			? Kind::Invalid
			: (result.method == u"disconnect"_q)
			? Kind::Disconnect
			: Kind::Unsupported;
		if (!known
			|| (bad && bad->error_code == Code::kMethodNotSupported)) {
			result.rejection = TonConnectError::MethodNotSupported;
		}
	}
	return result;
}

[[nodiscard]] bool TerminalSendPhase(engine::SendPhase phase) {
	switch (phase) {
	case engine::SendPhase::kIdle:
	case engine::SendPhase::kConfirmed:
	case engine::SendPhase::kReplaced:
	case engine::SendPhase::kSequenceNumberConsumed:
	case engine::SendPhase::kExpired:
	case engine::SendPhase::kSuperseded:
	case engine::SendPhase::kFailed:
	case engine::SendPhase::kCancelled:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] TransferTerminal StoredTransferTerminal(
		engine::SendPhase phase) {
	switch (phase) {
	case engine::SendPhase::kConfirmed:
		return TransferTerminal::Confirmed;
	case engine::SendPhase::kReplaced:
		return TransferTerminal::Replaced;
	case engine::SendPhase::kSequenceNumberConsumed:
		return TransferTerminal::SequenceNumberConsumed;
	case engine::SendPhase::kExpired:
		return TransferTerminal::Expired;
	case engine::SendPhase::kSuperseded:
		return TransferTerminal::Superseded;
	case engine::SendPhase::kFailed:
		return TransferTerminal::Failed;
	case engine::SendPhase::kCancelled:
		return TransferTerminal::Cancelled;
	default:
		return TransferTerminal::None;
	}
}

[[nodiscard]] std::optional<engine::SendPhase> RestoredTransferTerminal(
		TransferTerminal terminal) {
	switch (terminal) {
	case TransferTerminal::Confirmed:
		return engine::SendPhase::kConfirmed;
	case TransferTerminal::Replaced:
		return engine::SendPhase::kReplaced;
	case TransferTerminal::SequenceNumberConsumed:
		return engine::SendPhase::kSequenceNumberConsumed;
	case TransferTerminal::Expired:
		return engine::SendPhase::kExpired;
	case TransferTerminal::Superseded:
		return engine::SendPhase::kSuperseded;
	case TransferTerminal::Failed:
		return engine::SendPhase::kFailed;
	case TransferTerminal::Cancelled:
		return engine::SendPhase::kCancelled;
	case TransferTerminal::None:
		return std::nullopt;
	}
	Unexpected("Invalid stored transfer terminal.");
}

[[nodiscard]] std::optional<TimeId> OldestHistoryDate(
		const std::vector<TransferItem> &history) {
	auto result = std::optional<TimeId>();
	for (const auto &item : history) {
		if (item.date && (!result || *item.date < *result)) {
			result = item.date;
		}
	}
	return result;
}

[[nodiscard]] bool StaleSubmittedRecord(
		const SubmittedTransferRecord &record,
		TimeId now) {
	constexpr auto kWindow = TimeId(kClientSendValiditySeconds
		+ kClientResolutionMarginSeconds);
	return (record.posted > 0) && (now - record.posted > kWindow);
}

[[nodiscard]] bool FailedTransferTerminal(TransferTerminal terminal) {
	switch (terminal) {
	case TransferTerminal::Replaced:
	case TransferTerminal::Expired:
	case TransferTerminal::Failed:
	case TransferTerminal::Cancelled:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] engine::SendPhase PairedSendPhase(
		engine::SendPhase phase,
		bool paired) {
	return (paired && phase == engine::SendPhase::kReplaced)
		? engine::SendPhase::kSequenceNumberConsumed
		: phase;
}

[[nodiscard]] TonConnectSendResult TonConnectSendOutcome(
		const engine::SendResult &result,
		const std::string &operationId,
		bool rpcStarted,
		const QByteArray &normal) {
	const auto unknown = [&] {
		return TonConnectSendResult{
			rpcStarted ? QString::fromLatin1(normal.toBase64()) : QString(),
			SendError::SubmissionUnknown,
		};
	};
	if (result.operation_id != operationId) {
		return unknown();
	}
	const auto boc = QString::fromStdString(result.signed_boc);
	switch (result.phase) {
	case engine::SendPhase::kSubmitted:
	case engine::SendPhase::kConfirmed:
		return { boc, SendError::None };
	case engine::SendPhase::kSubmissionUnknown:
		return { boc, SendError::SubmissionUnknown };
	case engine::SendPhase::kHandedOff:
	case engine::SendPhase::kIdle:
	case engine::SendPhase::kValidating:
	case engine::SendPhase::kAuthorizing:
	case engine::SendPhase::kPreparing:
	case engine::SendPhase::kPersisting:
	case engine::SendPhase::kReadyToSubmit:
	case engine::SendPhase::kSubmitting:
		return unknown();
	case engine::SendPhase::kFailed:
	case engine::SendPhase::kCancelled:
	case engine::SendPhase::kReplaced:
	case engine::SendPhase::kSequenceNumberConsumed:
	case engine::SendPhase::kExpired:
	case engine::SendPhase::kSuperseded:
		break;
	}
	return { QString(), SendError::Failed };
}

[[nodiscard]] SubmittedTransferProjection StoredTransferProjection(
		const TransferItem &item) {
	const auto peer = (item.kind == TransferItem::Kind::PeerTransfer);
	const auto collectible = (item.kind == TransferItem::Kind::Collectible)
		? item.collectible
		: QString();
	return SubmittedTransferProjection{
		.id = item.id,
		.counterparty = item.counterparty,
		.counterpartyName = item.counterpartyName,
		.comment = item.commentEncrypted ? QString() : item.comment,
		.collectible = collectible,
		.counterpartyPeer = ((peer || !collectible.isEmpty())
			? item.counterpartyPeer
			: 0),
		.amountNano = item.amountNano,
		.feeNano = item.feeNano,
		.date = item.date,
		.peerTransfer = peer,
		.failed = (item.status == TransferItem::Status::Failure),
		.commentEncrypted = item.commentEncrypted,
		.gasless = item.gasless,
		.counterpartyBounceable = item.counterpartyBounceable,
	};
}

} // namespace SessionDetails

} // namespace Wallet
