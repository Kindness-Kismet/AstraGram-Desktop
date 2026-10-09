#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

namespace SessionDetails {

// A user send's served row may name only the address it was sent to.
void KeepSubmittedRecipient(
		TransferItem &item,
		const SubmittedTransferRecord &record) {
	using Kind = TransferItem::Kind;
	if (!record.recipient
		|| item.counterpartyPeer
		|| item.incoming
		|| (item.kind != Kind::Transfer && item.kind != Kind::Collectible)
		|| item.counterparty != record.destination) {
		return;
	}
	if (item.kind == Kind::Transfer) {
		item.kind = Kind::PeerTransfer;
	}
	item.counterpartyPeer = peerFromUser(record.recipient).value;
}

[[nodiscard]] std::optional<Gram::NftWebDocument> WebDocumentFromServer(
		const tl::conditional<MTPWebDocument> &document) {
	if (!document) {
		return std::nullopt;
	}
	return document->match([](const MTPDwebDocument &web) {
		return std::make_optional(Gram::NftWebDocument{
			.url = web.vurl().v,
			.accessHash = web.vaccess_hash().v,
			.mimeType = qs(web.vmime_type()),
		});
	}, [](const MTPDwebDocumentNoProxy &) {
		// A direct fetch would reveal the user's IP to the media host.
		return std::optional<Gram::NftWebDocument>();
	});
}

[[nodiscard]] std::optional<Gram::NftItem> CollectibleFromServer(
		const MTPwallet_NftItem &item) {
	const auto &data = item.data();
	const auto address = CanonicalAddress(qs(data.vaddress()));
	if (address.isEmpty()) {
		LOG(("Wallet Error: wallet.NftItem address is not parseable."));
		return std::nullopt;
	}
	auto result = Gram::NftItem();
	result.address = address;
	if (const auto collection = data.vcollection_address()) {
		result.collection = CanonicalAddress(qs(*collection));
	}
	result.index = qs(data.vindex());
	if (const auto name = data.vname()) {
		result.name = qs(*name);
	}
	result.image = WebDocumentFromServer(data.vimage());
	result.imageSmall = WebDocumentFromServer(data.vimage_small());
	result.contentUrl = WebDocumentFromServer(data.vcontent_url());
	result.lottie = WebDocumentFromServer(data.vlottie());
	Gram::ClassifyNftKind(result);
	return result;
}

// Every surface that paints a transfer prefixes a direction character of
// its own, so the amount and the direction are decided together here and
// never left as two fields a view has to reconcile: the served key-change
// row arrives as -300000000 nanograms with `incoming` clear, and the row,
// the details header and its fiat line each sign it a second time. When
// the two disagree the sign wins, because it is the source's arithmetic
// about this wallet's balance while the flag only summarizes it, and a
// row that removed value shown with a plus is the reading a user acts on.
void SetDirectedAmount(
		TransferItem &item,
		int64 nanograms,
		bool incoming) {
	const auto smallest = std::numeric_limits<int64>::min();
	if (nanograms == smallest) {
		// Negating it is undefined and its magnitude is not an int64, so
		// the one amount the record cannot hold is stored exactly as it
		// was sent instead of clamped into an amount nobody sent.
		LOG(("Wallet Error: transaction amount magnitude does not fit."));
	}
	const auto fold = (nanograms < 0) && (nanograms != smallest);
	item.amountNano = fold ? -nanograms : nanograms;
	item.incoming = fold ? false : incoming;
}

[[nodiscard]] std::optional<TransferItem> HistoryItemFromEngine(
		const engine::ActivityItem &item,
		const std::optional<TransferWalletIdentity> &identity) {
	constexpr auto kMaxTimestamp = uint64(std::numeric_limits<TimeId>::max());
	const auto amount = DecimalInt64(item.amount_nanograms);
	const auto fee = DecimalInt64(item.transaction_fee_nanograms);
	const auto lt = DecimalUint64(item.logical_time);
	if (!amount || !fee || !lt || (item.timestamp > kMaxTimestamp)) {
		LOG(("Wallet Error: engine activity item %1 has a bad number."
			).arg(QString::fromStdString(item.id)));
		return std::nullopt;
	}
	auto result = TransferItem();
	result.source = TransferItem::Source::Engine;
	result.id = QString::fromStdString(item.id);
	result.walletIdentity = identity;
	result.kind = TransferItem::Kind::Transfer;
	if (item.counterparty) {
		result.counterparty = CanonicalAddress(
			QString::fromStdString(*item.counterparty));
	}
	SetDirectedAmount(
		result,
		*amount,
		(item.direction == engine::ActivityDirection::kReceived));
	result.feeNano = *fee;
	if (item.encrypted_comment) {
		result.commentEncrypted = true;
		result.encryptedPayload = QByteArray::fromStdString(
			*item.encrypted_comment);
		if (!result.encryptedPayload.isEmpty()) {
			result.encryptedFormat = TransferItem::EncryptedFormat::EngineBodyBoc;
		}
	} else if (item.comment) {
		result.comment = QString::fromStdString(*item.comment);
	}
	result.date = TimeId(item.timestamp);
	result.lt = *lt;
	result.traceId = QByteArray::fromBase64(
		QByteArray::fromStdString(item.transaction_hash));
	result.status = (item.status == engine::ActivityStatus::kSuccess)
		? TransferItem::Status::Success
		: TransferItem::Status::Failure;
	return result;
}

[[nodiscard]] TransferItem HistoryItemFromServer(
		const MTPWalletTransaction &item,
		const std::optional<TransferWalletIdentity> &identity) {
	const auto &data = item.data();
	auto result = TransferItem();
	result.source = TransferItem::Source::Server;
	result.id = qs(data.vid());
	result.walletIdentity = identity;
	SetDirectedAmount(result, data.vamount().v, data.is_incoming());
	result.feeNano = data.vfee().v;
	result.gasless = data.is_gasless();
	result.date = data.vdate().v;
	result.status = data.is_failed()
		? TransferItem::Status::Failure
		: TransferItem::Status::Success;
	if (data.is_key_change()) {
		// The peer is not read on purpose: a key change names no
		// counterparty, and whatever the server puts there (the wallet's
		// own address, for one) would render the row as a transfer to or
		// from someone, which is exactly the reading this kind exists to
		// prevent. The direction is decided here, once: a key change is
		// the wallet's own outgoing transaction whatever the server's
		// incoming bit says.
		result.kind = TransferItem::Kind::KeyChange;
		result.incoming = false;
	} else {
		const auto setCounterparty = [&](const auto &data) {
			const auto parsed = ParseAddress(qs(data.vaddress()));
			result.counterparty = parsed ? parsed->raw : QString();
			result.counterpartyBounceable = parsed
				&& parsed->friendly
				&& parsed->bounceable;
			if (const auto domain = data.vdomain()) {
				result.counterpartyName = qs(*domain);
			}
			if (result.counterparty.isEmpty()) {
				LOG(("Wallet Error: wallet.getTransactions sent an unusable "
					"counterparty address."));
			}
		};
		data.vpeer().match([&](const MTPDwalletTransactionPeerUser &data) {
			result.kind = TransferItem::Kind::PeerTransfer;
			result.counterpartyPeer = peerFromUser(data.vuser_id()).value;
			setCounterparty(data);
		}, [&](const MTPDwalletTransactionPeerAddress &data) {
			setCounterparty(data);
		}, [&](const MTPDwalletTransactionPeerOnramp &data) {
			setCounterparty(data);
			// Without a provider or an address it reads as the address peer.
			const auto provider = qs(data.vprovider_name()).trimmed();
			if (!provider.isEmpty() && !result.counterparty.isEmpty()) {
				result.kind = TransferItem::Kind::Onramp;
				result.provider = provider;
			}
		}, [](const MTPDwalletTransactionPeerUnsupported &) {
			// Nothing is written, because the defaults are the row: a
			// Kind::Transfer with no counterparty renders through
			// RowContentFromItem's fall-through as a Deposit or a
			// Withdrawal by direction, with the date and the amount. That
			// is exactly the graceful degradation this constructor exists
			// for, so no lang key is invented for it.
		});
		if (const auto nft = data.vnft()) {
			if (auto record = CollectibleFromServer(*nft)) {
				result.kind = TransferItem::Kind::Collectible;
				result.collectible = record->address;
				result.collectibleRecord = std::move(*record);
				result.provider = QString();
			}
		}
	}
	result.commentEncrypted = data.is_comment_encrypted();
	if (const auto comment = data.vcomment()) {
		if (result.commentEncrypted) {
			result.encryptedPayload = DecodeServerEncryptedComment(
				qs(*comment));
			if (!result.encryptedPayload.isEmpty()) {
				result.encryptedFormat
					= TransferItem::EncryptedFormat::ServerPayload;
			}
		} else {
			result.comment = qs(*comment);
		}
	}
	if (const auto hash = data.vtx_hash()) {
		result.traceId = TransactionHashFromServer(qs(*hash));
	}
	return result;
}

[[nodiscard]] std::optional<TransferReceipt> ReceiptFromServer(
		const MTPDupdateSentWalletTransaction &data) {
	// The contract names msg_hash a string and fixes no encoding for
	// it, so the only rule this client may impose is that a receipt
	// addresses a message at all: the bytes are kept exactly as they
	// arrived and echoed unchanged into the lookup. Reading them as
	// text would be a guess, and a guess that refused a token the
	// server accepts would leave an accepted payment unresolvable and
	// refuse the next send for the whole resolution window.
	const auto &hash = data.vmsg_hash().v;
	if (hash.isEmpty()) {
		return std::nullopt;
	}
	return TransferReceipt{
		.messageHash = hash,
		.gasless = data.is_gasless(),
	};
}

[[nodiscard]] const MTPDupdateSentWalletTransaction *SentUpdateFromServer(
		const MTPUpdates &updates) {
	auto result = static_cast<const MTPUpdate*>(nullptr);
	auto conflicting = false;
	const auto inspect = [&](const MTPUpdate &update) {
		if (update.type() != mtpc_updateSentWalletTransaction || conflicting) {
			return;
		} else if (!result) {
			result = &update;
			return;
		}
		auto previous = mtpBuffer();
		auto next = mtpBuffer();
		result->write(previous);
		update.write(next);
		conflicting = (previous != next);
	};
	const auto inspectVector = [&](const MTPVector<MTPUpdate> &list) {
		for (const auto &update : list.v) {
			inspect(update);
		}
	};
	updates.match([&](const MTPDupdates &data) {
		inspectVector(data.vupdates());
	}, [&](const MTPDupdatesCombined &data) {
		inspectVector(data.vupdates());
	}, [&](const MTPDupdateShort &data) {
		inspect(data.vupdate());
	}, [](const auto &) {
	});
	return (result && !conflicting)
		? &result->c_updateSentWalletTransaction()
		: nullptr;
}

} // namespace SessionDetails

} // namespace Wallet
