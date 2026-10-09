#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

namespace SessionDetails {

ResetClientCompletion::~ResetClientCompletion() {
	if (done) {
		crl::on_main(std::move(done));
	}
}

[[nodiscard]] int64 MinNanosFromConfig(
		float64 configured,
		int64 fallback) {
	// The value arrives as `jsonNumber value:double`, so it is judged
	// in the double domain before any cast: NaN, an infinity or an
	// out-of-range magnitude would otherwise abort or convert with
	// undefined behaviour. The ceiling is 2^53, the largest integer a
	// double represents exactly, because `double(kMaxAmountNano)` rounds
	// up to 1e18 and would let an out-of-range value through. A served
	// integer above 2^53 is indistinguishable from its nearest
	// representable neighbour: 2^53 + 1 arrives as 2^53 and is accepted,
	// and 2^53 + 2 is the first value that falls back to the default.
	const auto valid = std::isfinite(configured)
		&& (configured >= 1.)
		&& (configured <= float64(kTransferMinNanosMax))
		&& (configured == std::floor(configured));
	return valid ? int64(configured) : fallback;
}

[[nodiscard]] int64 GaslessMinNanos(not_null<Main::Session*> session) {
	const auto configured = session->appConfig().get<float64>(
		u"wallet_gasless_min_nanos"_q,
		float64(kGaslessMinNanosDefault));
	return MinNanosFromConfig(configured, kGaslessMinNanosDefault);
}

[[nodiscard]] GaslessInfo GaslessInfoFromServer(
		const MTPDupdateWalletGaslessInfo &data) {
	const auto relayer = ParseAddress(qs(data.vrelayer_address()));
	return GaslessInfo{
		.relayer = (relayer && !relayer->testnet)
			? relayer->raw
			: QString(),
		.minAmount = int64(data.vmin_amount().v),
		.resetAt = data.vreset_at().v,
		.left = data.vleft().v,
		.available = data.is_available(),
	};
}

[[nodiscard]] std::optional<int64> DecimalInt64(const std::string &value) {
	auto ok = false;
	const auto result = QString::fromStdString(value).toLongLong(&ok);
	return ok ? std::make_optional(result) : std::nullopt;
}

[[nodiscard]] std::optional<uint64> DecimalUint64(
		const std::string &value) {
	auto ok = false;
	const auto result = QString::fromStdString(value).toULongLong(&ok);
	return ok ? std::make_optional(result) : std::nullopt;
}

void FinishHistoryWaiters(std::vector<Fn<void()>> callbacks) {
	for (const auto &callback : callbacks) {
		callback();
	}
}

[[nodiscard]] bool SameHistory(
		const std::vector<TransferItem> &was,
		const std::vector<TransferItem> &now) {
	return (was == now);
}

[[nodiscard]] base::flat_set<QString> HistoryNamedIds(
		const std::vector<TransferItem> &list) {
	auto result = base::flat_set<QString>();
	result.reserve(list.size());
	for (const auto &item : list) {
		if (!item.id.isEmpty()) {
			result.emplace(item.id);
		}
	}
	return result;
}

// The merged list is the served page in server order, then the rows that page
// does not name, in the order the list already had, so it is a superset of
// `was`. How many of those rows were kept and whether the page named the row
// `was` ends with come back with it, because the caller decides the cursor
// from the second of those and both fall out of the one scan below: the module
// keeps a single row-identity rule and the caller reads no answer out of the
// output vector's layout.
[[nodiscard]] MergedHead MergedHeadHistory(
		const std::vector<TransferItem> &was,
		std::vector<TransferItem> &&head) {
	const auto ids = HistoryNamedIds(head);
	auto list = std::move(head);
	auto retained = std::vector<TransferItem>();
	retained.reserve(was.size());
	// A list with no rows has no tail for the page to reach past, so the
	// coverage question is answered vacuously here and the loop below never
	// leaves it undecided.
	auto namedLast = was.empty();
	for (const auto &item : was) {
		// The server names every transaction and that name is the only
		// key this feed has, so a row the head page covers is the head
		// page's and a row it does not reach is kept exactly where the
		// reader already has it. A row the server left unnamed cannot be
		// looked up by name at all, so it is compared by value against
		// the page instead: carried again, it would otherwise be kept
		// twice. The page is a whole vector here and grows no rows until
		// the loop is over, so that lookup never reads a retained row and
		// two equal unnamed rows the list already held both survive.
		const auto listed = item.id.isEmpty()
			? ranges::contains(list, item)
			: ids.contains(item.id);
		if (!listed) {
			retained.push_back(item);
		}
		// Only the final iteration's answer survives, and that is the one
		// the cursor decision asks for: whether the page reached the row
		// the loaded list ends with. Taking it from the same `listed`
		// keeps that question on the one identity rule stated above.
		namedLast = listed;
	}
	const auto count = int(retained.size());
	list.insert(
		end(list),
		std::make_move_iterator(begin(retained)),
		std::make_move_iterator(end(retained)));
	return {
		.list = std::move(list),
		.retained = count,
		.namedLast = namedLast,
	};
}

// The identity is the one MergedHeadHistory() uses - the server's
// transaction id - and here it is the only key that can answer this
// direction at all: a served row the server left unnamed is added, because
// TransferItem's defaulted operator== makes two genuinely distinct
// transfers equal when they share a counterparty, an amount, a fee, a
// comment and a date and carry neither an id nor a tx_hash, and dropping
// one would lose a transaction the server is delivering now that no later
// page offers again at this cursor. The head arm's value test decides the
// opposite question - whether a row the list already holds is about to be
// carried forward beside an equal copy the page already contains - where
// the same equality can only drop a loaded copy the next head page brings
// back.
[[nodiscard]] std::vector<TransferItem> UnheldHistory(
		const std::vector<TransferItem> &was,
		std::vector<TransferItem> &&page) {
	const auto ids = HistoryNamedIds(page);
	auto held = base::flat_set<QString>();
	held.reserve(ids.size());
	for (const auto &item : was) {
		if (!item.id.isEmpty() && ids.contains(item.id)) {
			held.emplace(item.id);
		}
	}
	if (held.empty()) {
		return std::move(page);
	}
	auto result = std::vector<TransferItem>();
	result.reserve(page.size());
	for (auto &item : page) {
		if (item.id.isEmpty() || !held.contains(item.id)) {
			result.push_back(std::move(item));
		}
	}
	return result;
}

[[nodiscard]] std::vector<TransferItem> ArrivedCollectibles(
		const std::vector<TransferItem> &was,
		const std::vector<TransferItem> &head) {
	auto result = std::vector<TransferItem>();
	auto seen = base::flat_set<QString>();
	for (const auto &item : head) {
		if (item.kind != TransferItem::Kind::Collectible
			|| item.collectible.isEmpty()
			|| item.status == TransferItem::Status::Failure
			|| seen.contains(item.collectible)) {
			continue;
		}
		seen.emplace(item.collectible);
		const auto held = item.id.isEmpty()
			? ranges::contains(was, item)
			: ranges::contains(was, item.id, &TransferItem::id);
		if (!held) {
			result.push_back(item);
		}
	}
	return result;
}

[[nodiscard]] std::vector<Gram::NftItem> UnheldCollectibles(
		const std::vector<Gram::NftItem> &was,
		std::vector<Gram::NftItem> &&page) {
	auto held = base::flat_set<QString>();
	held.reserve(was.size());
	for (const auto &item : was) {
		held.emplace(item.address);
	}
	auto result = std::vector<Gram::NftItem>();
	result.reserve(page.size());
	for (auto &item : page) {
		if (!held.contains(item.address)) {
			result.push_back(std::move(item));
		}
	}
	return result;
}

[[nodiscard]] bool SameCollectibles(
		const std::vector<Gram::NftItem> &was,
		const std::vector<Gram::NftItem> &now) {
	return (was == now);
}

[[nodiscard]] std::string NewRecordId() {
	return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

[[nodiscard]] CustodyRecord RecordFromDescriptor(
		const engine::WalletDescriptor &descriptor) {
	return CustodyRecord{
		.recordId = QString::fromStdString(descriptor.record_id),
		.address = QString::fromStdString(descriptor.address),
		.publicKey = QByteArray(
			reinterpret_cast<const char*>(descriptor.public_key.data()),
			descriptor.public_key.size()),
		.network = int(descriptor.network),
		.secretRef = QString::fromStdString(descriptor.secret_ref.value),
	};
}

[[nodiscard]] auto EngineKey(const QByteArray &key)
-> std::optional<std::vector<uint8_t>> {
	if (key.isEmpty()) {
		return std::nullopt;
	}
	return std::vector<uint8_t>(
		key.constData(),
		key.constData() + key.size());
}

[[nodiscard]] std::vector<uint8_t> EngineBytes(const QByteArray &bytes) {
	return std::vector<uint8_t>(
		bytes.constData(),
		bytes.constData() + bytes.size());
}

[[nodiscard]] QByteArray BytesFromEngine(const std::vector<uint8_t> &bytes) {
	return QByteArray(
		reinterpret_cast<const char*>(bytes.data()),
		bytes.size());
}

[[nodiscard]] engine::WalletDescriptor DescriptorFromRecord(
		const CustodyRecord &record) {
	return engine::WalletDescriptor{
		.record_id = record.recordId.toStdString(),
		.address = record.address.toStdString(),
		.public_key = std::vector<uint8_t>(
			record.publicKey.constData(),
			record.publicKey.constData() + record.publicKey.size()),
		.network = engine::Network(record.network),
		.secret_ref = engine::ProtectedSecretRef{
			.value = record.secretRef.toStdString(),
		},
	};
}

// A record of another wallet, or an unresolved one of the served wallet:
// what the conflict box lists, what the parked reveal and drop accept, and
// what raises the device's conflict, so the three cannot disagree.
[[nodiscard]] bool RecordParked(
		const CustodyRecord &record,
		const QString &canonicalAddress,
		const QByteArray &servedKey) {
	return (CanonicalAddress(record.address) != canonicalAddress)
		|| record.unresolved(servedKey);
}

// WHY: a parked record at the served address is an older key of this
// same account, so its balance is the one the card already shows.
[[nodiscard]] QString CheckableParkedAddress(
		const CustodyRecord &record,
		const QString &servedAddress) {
	const auto address = CanonicalAddress(record.address);
	return (record.network == int(engine::Network::kMainnet)
		&& address != servedAddress)
		? address
		: QString();
}

// Public keys and addresses name a wallet; the words never reach the log.
[[nodiscard]] QString LogKey(const QByteArray &key) {
	return key.isEmpty() ? u"(none)"_q : QString::fromLatin1(key.toHex());
}

[[nodiscard]] QString AwaitingKeyRefusal(
		const CustodyStore &store,
		const PhraseIdentity &identity,
		const QString &outdated,
		const QString &changing) {
	const auto held = store.byAnchor(identity.anchor);
	if (!held || !held->awaitingServerKey || held->signingKey.isEmpty()) {
		return QString();
	}
	LOG(("Wallet Error: the record of anchor %1 awaits the server key "
		"for signing key %2."
		).arg(LogKey(identity.anchor)
		).arg(LogKey(held->signingKey)));
	return (held->signingKey == identity.signing) ? changing : outdated;
}

[[nodiscard]] QString LogWalletState(const MTPWalletState &state) {
	return state.match([](const MTPDwalletState &data) {
		return u"address=%1 key=%2 backup_enabled=%3 "
			"can_export_phrase=%4 can_enable_backup=%5"_q
			.arg(qs(data.vaddress()))
			.arg(LogKey(data.vpublic_key().v))
			.arg(data.is_backup_enabled())
			.arg(data.is_can_export_phrase())
			.arg(data.is_can_enable_backup());
	}, [](const MTPDwalletStateEmpty &data) {
		return u"empty creating=%1"_q.arg(data.is_creating());
	});
}

[[nodiscard]] QString SendErrorName(SendError error) {
	switch (error) {
	case SendError::None: return u"None"_q;
	case SendError::InvalidRequest: return u"InvalidRequest"_q;
	case SendError::AmountTooSmall: return u"AmountTooSmall"_q;
	case SendError::CommentTooLong: return u"CommentTooLong"_q;
	case SendError::CommentEncryptionUnavailable:
		return u"CommentEncryptionUnavailable"_q;
	case SendError::InsufficientBalance: return u"InsufficientBalance"_q;
	case SendError::InsufficientFees: return u"InsufficientFees"_q;
	case SendError::PreviousUnresolved: return u"PreviousUnresolved"_q;
	case SendError::AlreadySending: return u"AlreadySending"_q;
	case SendError::SigningUnavailable: return u"SigningUnavailable"_q;
	case SendError::Locked: return u"Locked"_q;
	case SendError::Failed: return u"Failed"_q;
	case SendError::Rejected: return u"Rejected"_q;
	case SendError::DataInvalid: return u"DataInvalid"_q;
	case SendError::KeyMismatch: return u"KeyMismatch"_q;
	case SendError::KeyChanged: return u"KeyChanged"_q;
	case SendError::QuoteExpired: return u"QuoteExpired"_q;
	case SendError::LinkExpired: return u"LinkExpired"_q;
	case SendError::CollectibleUnavailable:
		return u"CollectibleUnavailable"_q;
	case SendError::CollectibleRejected: return u"CollectibleRejected"_q;
	case SendError::Silent: return u"Silent"_q;
	case SendError::SubmissionUnknown: return u"SubmissionUnknown"_q;
	}
	return u"Unknown"_q;
}

// Every custody flow names its stage and refusal code in one log.txt line.
[[nodiscard]] Fn<void(const QString &)> LoggedFail(
		const QString &stage,
		Fn<void(const QString &)> fail) {
	return [stage, fail = std::move(fail)](const QString &error) {
		LOG(("Wallet Error: %1 refused: %2").arg(stage, error));
		if (fail) {
			fail(error);
		}
	};
}

[[nodiscard]] Fn<void(FeeResult)> LoggedFeeDone(Fn<void(FeeResult)> done) {
	return [done = std::move(done)](FeeResult result) {
		if (result.error != SendError::None) {
			LOG(("Wallet Error: the fee estimate answered %1."
				).arg(SendErrorName(result.error)));
		}
		if (done) {
			done(std::move(result));
		}
	};
}

} // namespace SessionDetails

} // namespace Wallet
