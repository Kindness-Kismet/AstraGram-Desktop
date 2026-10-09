#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

// The TL declares tx_hash as an unqualified string and states no encoding,
// while ExplorerTransactionUrl() hexes whatever is stored here straight into
// a URL path with no validation and no escaping. The shape is therefore
// decided here, and a string of neither recognized shape stores nothing: an
// empty traceId hides the explorer entry instead of pointing it at a hash
// this client cannot be sure of. Base64 of 32 bytes is 43 or 44 characters
// and can therefore never also be 64 hex digits, so the two tests cannot
// collide and their order is a cheapness choice, not a correctness one.
QByteArray TransactionHashFromServer(const QString &value) {
	constexpr auto kHashBytes = 32;
	const auto latin = value.toLatin1();
	const auto hex = (latin.size() == 2 * kHashBytes)
		&& ranges::all_of(latin, [](char ch) {
			return (ch >= '0' && ch <= '9')
				|| (ch >= 'a' && ch <= 'f')
				|| (ch >= 'A' && ch <= 'F');
		});
	if (hex) {
		return QByteArray::fromHex(latin);
	}
	const auto decode = [&](QByteArray::Base64Option encoding) {
		return QByteArray::fromBase64Encoding(
			latin,
			encoding | QByteArray::AbortOnBase64DecodingErrors);
	};
	auto standard = decode(QByteArray::Base64Encoding);
	if (standard && (standard.decoded.size() == kHashBytes)) {
		return std::move(standard.decoded);
	}
	auto url = decode(QByteArray::Base64UrlEncoding);
	if (url && (url.decoded.size() == kHashBytes)) {
		return std::move(url.decoded);
	}
	LOG(("Wallet Error: Unusable transaction hash: %1").arg(value));
	return QByteArray();
}

bool EncryptedCommentPending(const TransferItem &item) {
	return item.commentEncrypted
		&& item.id.isEmpty()
		&& item.encryptedPayload.isEmpty();
}

bool EncryptedCommentUnusable(const TransferItem &item) {
	if (!item.commentEncrypted || EncryptedCommentPending(item)) {
		return false;
	} else if (item.id.isEmpty()
		|| (item.incoming && CanonicalAddress(item.counterparty).isEmpty())) {
		return true;
	}
	return !ValidEncryptedCommentBody(EncryptedCommentBody(item));
}

bool EncryptedCommentRevealable(const TransferItem &item) {
	return item.commentEncrypted
		&& !EncryptedCommentPending(item)
		&& !EncryptedCommentUnusable(item);
}

QByteArray DecodeServerEncryptedComment(const QString &encoded) {
	constexpr auto kMinBytes = 64;
	constexpr auto kMaxBytes = 1024;
	constexpr auto kEncodedLimit = 4 * ((kMaxBytes + 2) / 3);
	constexpr auto kHeaderBytes = 48;
	constexpr auto kBlockBytes = 16;
	if (encoded.isEmpty()
		|| encoded.size() > kEncodedLimit
		|| (encoded.size() % 4)) {
		return QByteArray();
	}
	const auto latin = encoded.toLatin1();
	auto decoded = QByteArray::fromBase64Encoding(
		latin,
		QByteArray::AbortOnBase64DecodingErrors);
	if (!decoded
		|| decoded.decoded.size() < kMinBytes
		|| decoded.decoded.size() > kMaxBytes
		|| ((decoded.decoded.size() - kHeaderBytes) % kBlockBytes)
		|| decoded.decoded.toBase64() != latin) {
		return QByteArray();
	}
	return std::move(decoded.decoded);
}

std::vector<TransferItem> HistoryFromEngine(
		const std::vector<engine::ActivityItem> &items,
		std::optional<TransferWalletIdentity> identity) {
	auto result = std::vector<TransferItem>();
	result.reserve(items.size());
	for (const auto &item : items) {
		if (auto mapped = HistoryItemFromEngine(item, identity)) {
			result.push_back(std::move(*mapped));
		}
	}
	return result;
}

std::vector<TransferItem> HistoryFromServer(
		const QVector<MTPWalletTransaction> &list,
		std::optional<TransferWalletIdentity> identity) {
	auto result = std::vector<TransferItem>();
	result.reserve(list.size());
	for (const auto &item : list) {
		result.push_back(HistoryItemFromServer(item, identity));
	}
	return result;
}

std::vector<Gram::NftItem> CollectiblesFromServer(
		const QVector<MTPwallet_NftItem> &list) {
	auto result = std::vector<Gram::NftItem>();
	result.reserve(list.size());
	auto seen = base::flat_set<QString>();
	for (const auto &item : list) {
		if (auto mapped = CollectibleFromServer(item)) {
			if (seen.emplace(mapped->address).second) {
				result.push_back(std::move(*mapped));
			}
		}
	}
	return result;
}

bool IsWordlistWord(const QString &word) {
	const auto &list = Wordlist();
	const auto normalized = NormalizeWord(word);
	return std::binary_search(list.begin(), list.end(), normalized);
}

std::vector<QString> WordlistSuggestions(
		const QString &prefix,
		int limit) {
	auto result = std::vector<QString>();
	const auto normalized = NormalizeWord(prefix);
	if (normalized.isEmpty() || limit <= 0) {
		return result;
	}
	const auto &list = Wordlist();
	auto i = std::lower_bound(list.begin(), list.end(), normalized);
	while (i != list.end()
		&& int(result.size()) != limit
		&& i->startsWith(normalized)) {
		result.push_back(*i);
		++i;
	}
	return result;
}

PhraseMatch DetectPhraseMatch(const std::vector<QString> &words) {
	auto engineWords = std::vector<std::string>();
	engineWords.reserve(words.size());
	for (const auto &word : words) {
		engineWords.push_back(NormalizeWord(word).toStdString());
	}
	try {
		const auto schemes = engine::detect_mnemonic_schemes(engineWords);
		auto rotation = false;
		auto foreign = false;
		for (const auto scheme : schemes) {
			if (scheme == engine::MnemonicScheme::kRotation) {
				rotation = true;
			} else if (scheme == engine::MnemonicScheme::kTon
				|| scheme == engine::MnemonicScheme::kBip39) {
				foreign = true;
			}
		}
		if (rotation) {
			return PhraseMatch::Rotation;
		} else if (foreign) {
			return PhraseMatch::Foreign;
		}
		return PhraseMatch::None;
	} catch (...) {
		return PhraseMatch::Rotation;
	}
}

void WalletLoss::add(const WalletLoss &other) {
	unbacked += other.unbacked;
	parked += other.parked;
	rotating += other.rotating;
	holdsRecords = other.holdsRecords || holdsRecords;
	unknown = other.unknown || unknown;
}

WalletLoss WalletLossOnLogout(not_null<Main::Account*> account) {
	auto result = WalletLoss();
	const auto store = ReadCustodyStore(account->local());
	if (!store) {
		// Broken, or written by a newer format: what this device holds
		// cannot be read, and Account::reset() destroys it either way.
		result.unknown = true;
		return result;
	}
	result.holdsRecords = !store->records.empty();
	if (store->pendingRotation) {
		++result.rotating;
	}
	const auto session = account->maybeSession();
	const auto identity = session
		? session->wallet().transferWalletIdentity()
		: std::nullopt;
	const auto known = session
		&& session->wallet().presenceCurrent() == Presence::Ready
		&& identity;
	const auto backed = known
		&& session->wallet().capabilities().backupEnabled;
	for (const auto &record : store->records) {
		const auto sameWallet = known
			&& CanonicalAddress(record.address) == identity->address;
		const auto active = known
			? (sameWallet && record.signsWith(identity->publicKey))
			: record.active;
		const auto obsolete = sameWallet
			&& !active
			&& !record.signingKey.isEmpty();
		if (known ? !sameWallet : !active) {
			++result.parked;
		} else if (!known) {
			result.unknown = true;
		} else if (!obsolete && (!backed || record.rotatedSinceBackup)) {
			++result.unbacked;
		}
	}
	return result;
}

QString WalletLossWarning(WalletLoss loss) {
	auto result = QString();
	const auto append = [&](const QString &line) {
		if (!result.isEmpty()) {
			result += u"\n\n"_q;
		}
		result += line;
	};
	if (loss.unbacked > 0) {
		append(tr::lng_sure_logout_wallet_local(
			tr::now,
			lt_count,
			loss.unbacked));
	}
	if (loss.parked > 0) {
		append(tr::lng_sure_logout_wallet_parked(
			tr::now,
			lt_count,
			loss.parked));
	}
	if (loss.rotating > 0) {
		append(tr::lng_sure_logout_wallet_rotating(
			tr::now,
			lt_count,
			loss.rotating));
	}
	if (loss.unknown) {
		append(tr::lng_sure_logout_wallet_unknown(tr::now));
	}
	return result;
}

CommentScope::CommentScope(std::shared_ptr<State> state)
: _state(std::move(state)) {
}

void CommentScope::cancel() {
	if (!_state->cancelled.exchange(true)) {
		if (auto cancel = base::take(_state->cancelPending)) {
			crl::on_main(std::move(cancel));
		}
		_cancelledChanges.fire({});
	}
}

bool CommentScope::cancelled() const {
	return _state->cancelled;
}

rpl::producer<> CommentScope::cancelledChanges() const {
	return _cancelledChanges.events();
}

TransferItem ItemFromPending(const PendingSendInfo &pending) {
	return TransferItem{
		.walletIdentity = pending.walletIdentity,
		.kind = (!pending.collectible.isEmpty()
			? TransferItem::Kind::Collectible
			: pending.recipient
			? TransferItem::Kind::PeerTransfer
			: TransferItem::Kind::Transfer),
		.incoming = false,
		.counterparty = pending.destination,
		.counterpartyBounceable = pending.bounce,
		.counterpartyPeer = (pending.recipient
			? peerFromUser(pending.recipient).value
			: quint64()),
		.collectible = pending.collectible,
		.amountNano = pending.amountNano,
		.comment = pending.comment,
		.date = pending.posted,
		.status = TransferItem::Status::Pending,
	};
}

int SendCommentBytes(const QString &text) {
	return text.toUtf8().size();
}

bool SendCommentFits(const QString &text) {
	return SendCommentBytes(text) <= kSendCommentMaxBytes;
}

int64 CollectibleTransferAttachedNanos() {
	return kCollectibleTransferAttachedNanos;
}

int64 TransferMinNanosFromConfig(float64 configured) {
	return MinNanosFromConfig(configured, kTransferMinNanosDefault);
}

int64 TransferMinNanos(not_null<Main::Session*> session) {
	return TransferMinNanosFromConfig(session->appConfig().get<float64>(
		u"wallet_transfer_min_nanos"_q,
		float64(kTransferMinNanosDefault)));
}

bool TransferMagnitudeBelowMinimum(int64 amountNano, int64 minNanos) {
	// Negating the smallest int64 is undefined and its magnitude does not
	// fit the type, so the one amount that cannot be measured is answered
	// from what is known about it instead: its magnitude is 2^63, which is
	// larger than every minimum an int64 can hold, so it is never below
	// one whatever this policy is configured with.
	constexpr auto smallest = std::numeric_limits<int64>::min();
	if (amountNano == smallest) {
		return false;
	}
	const auto magnitude = (amountNano < 0) ? -amountNano : amountNano;
	return (magnitude < minNanos);
}

bool TransferAmountBelowMinimum(int64 amountNano, int64 minNanos) {
	return (amountNano > 0)
		&& TransferMagnitudeBelowMinimum(amountNano, minNanos);
}

bool HistoryTransferHidden(const TransferItem &item, int64 minNanos) {
	using Kind = TransferItem::Kind;
	// Only an ordinary monetary transfer is judged. A key change, a
	// collectible, an on-ramp and a contract interaction are activity
	// the feed states for reasons of their own, and the amount threshold
	// says nothing about whether they are worth a row.
	const auto transfer = (item.kind == Kind::Transfer)
		|| (item.kind == Kind::PeerTransfer);
	return transfer
		&& TransferMagnitudeBelowMinimum(item.amountNano, minNanos);
}

} // namespace Wallet
