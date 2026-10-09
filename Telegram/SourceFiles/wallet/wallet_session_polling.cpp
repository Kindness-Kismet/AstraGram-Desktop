#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

void Session::startPolling() {
	++_pollingCount;
	updatePollingState();
	refreshStaleHistory();
}

void Session::stopPolling() {
	if (_pollingCount > 0) {
		--_pollingCount;
	}
	updatePollingState();
}

void Session::updatePollingState() {
	const auto wanted = (_pollingCount > 0)
		|| _pending
		|| _sendUnresolved
		|| sendRecoveryNeeded()
		|| submittedLookupNeeded()
		|| custody().pendingRotation
		|| custody().anyAwaitingServerKey();
	if (!wanted) {
		_pollTimer.cancel();
	} else if (!_pollTimer.isActive()) {
		_pollTimer.callEach(kPollInterval);
		pollTick();
	}
	if (wanted && (_presence.current() == Presence::Ready)) {
		_stream->start(_address);
	} else {
		_stream->stop();
	}
}

bool Session::pollingRequested() const {
	return _pollingCount > 0;
}

void Session::pollTick() {
	ensureLoaded();
	expireStaleSubmittedTransfers();
	updatePollingState();
	if (!_pollTimer.isActive()) {
		return;
	}
	refreshState();
	if (_presence.current() != Presence::Ready) {
		return;
	}
	const auto streaming = _stream->healthy();
	const auto stale = [&](crl::time at) {
		return !at || (crl::now() - at >= kStreamResyncInterval);
	};
	if (!streaming
		|| stale(std::max(_stateRefreshedAt, _engineRefreshedAt))) {
		requestEngineRefresh();
	}
	// The history leg deliberately carries no !streaming disjunct. Every
	// refresh that disjunct reaches above self-floors, but refreshHistory()
	// does not: a stream hint means a transaction touched this address and
	// flooring it would delay a just-received transfer. With the disjunct the
	// lane would send a real wallet.getTransactions on every tick for the
	// whole time the stream is not live, which is every cold open, acquire,
	// reconnect and backoff. stale() is true for a zero stamp, so the first
	// tick still requests at once and the lane then settles to one request
	// per resync interval, plus the unfloored stream hints.
	const auto historyStale = stale(
		std::max(_historyRequestedAt, _historyRefreshedAt));
	const auto historyIdle = !_historyRequest;
	if (historyStale) {
		refreshHistory();
	}
	// A feed whose loaded pages are all hidden gives the reader nothing to
	// scroll, so there is no gesture it could ask for more with. The walk
	// that looks for a row it can show is resumed here instead, on the same
	// floor the head refresh uses, so it is paced by this client's clock and
	// never by how often a sender pushes. Idleness is read before that
	// refresh, because the term asks whether a walk is already running: a
	// head page issued by this very tick is not one, and the answer it
	// brings starts the walk itself through continueHiddenHistory().
	if (historyStale
		&& historyIdle
		&& (_panel != nullptr)
		&& !collectiblesTab()
		&& historyLoadingMore()) {
		resetHiddenHistoryPages();
		loadMoreHistory();
	}
	refreshCollectibles();
	if ((_pending
			|| _sendUnresolved
			|| sendRecoveryNeeded()
			|| custody().pendingRotation)
		&& !_resolveRequestPending) {
		resolvePending();
	}
	lookupSubmittedTransaction();
}

void Session::applyStreamRefresh(StreamRefresh wanted) {
	if (wanted.state) {
		refreshState();
	}
	if (wanted.history) {
		if (_historyRequest) {
			// The flight was sent before this hint, so it may miss its rows.
			const auto generation = _networkGeneration;
			const auto revision = _walletIdentityRevision;
			_historyRequest->done.push_back([=] {
				if (generation == _networkGeneration
					&& revision == _walletIdentityRevision) {
					refreshHistory();
				}
			});
		} else {
			refreshHistory();
		}
	}
	if (wanted.collectibles) {
		refreshCollectibles(true);
	}
}

int64 Session::balanceNano() const {
	return _balanceNano.current();
}

rpl::producer<int64> Session::balanceNanoValue() const {
	return _balanceNano.value();
}

rpl::producer<bool> Session::stateKnownValue() const {
	return _presence.value() | rpl::map([](Presence presence) {
		return (presence == Presence::Ready);
	});
}

AccountStatus Session::status() const {
	return _engineStatus;
}

const std::vector<TransferItem> &Session::history() const {
	return _history;
}

int64 Session::transferMinNanos() const {
	return _transferMinNanos;
}

bool Session::historyItemHidden(const TransferItem &item) const {
	return HistoryTransferHidden(item, _transferMinNanos);
}

bool Session::historyVisibleEmpty() const {
	return ranges::all_of(_history, [&](const TransferItem &item) {
		return historyItemHidden(item);
	});
}

rpl::producer<> Session::historyUpdates() const {
	return _historyUpdates.events();
}

bool Session::listsGated() const {
	return _listsGated.current();
}

rpl::producer<bool> Session::listsGatedValue() const {
	return _listsGated.value();
}

rpl::producer<ListsEmptyState> Session::listsEmptyStateValue() const {
	return rpl::single(rpl::empty) | rpl::then(rpl::merge(
		historyUpdates(),
		collectiblesUpdates(),
		_listsStateUpdates.events()
	)) | rpl::map([=, this] {
		return ListsEmptyState{
			.confirmedEmpty = listsConfirmedEmpty(),
			.unreachable = _stateUnreachable,
		};
	}) | rpl::distinct_until_changed();
}

const std::vector<Gram::NftItem> &Session::collectibles() const {
	return _collectibles;
}

rpl::producer<> Session::collectiblesUpdates() const {
	return _collectiblesUpdates.events();
}

bool Session::collectiblesTab() const {
	return _collectiblesTab.current();
}

rpl::producer<bool> Session::collectiblesTabValue() const {
	return _collectiblesTab.value();
}

void Session::setCollectiblesTab(bool value) {
	const auto tab = value && !_collectibles.empty();
	// Each lane's paged term is a fact about one visible list and ends with
	// that view: the tab strip scrolls the returning list back to its top,
	// so the head refresh a switch releases truncates nothing the reader
	// can still see. The clear follows tab, the effective selection, so
	// asking for a tab that cannot be shown deselects and clears nothing.
	// applyTransactions() reads that same effective selection before it
	// arms the history term, so a page landing later cannot undo this clear.
	if (tab) {
		_historyPaged = false;
	} else {
		_collectiblesPaged = false;
		resetHiddenHistoryPages();
	}
	_collectiblesTab = tab;
	refreshStaleHistory();
}

void Session::setWindowSend(std::string operationId) {
	_windowSend = std::move(operationId);
	_historyUpdates.fire({});
}

const std::string &Session::windowSend() const {
	return _windowSend;
}

SendState Session::sendState() const {
	return _sendState.current();
}

rpl::producer<SendState> Session::sendStateValue() const {
	return _sendState.value();
}

std::optional<PendingSendInfo> Session::pendingSend() const {
	return (_pending
		&& transferWalletIdentityCurrent(_pending->walletIdentity))
		? _pending
		: std::nullopt;
}

auto Session::lastTransferReceipt() const
-> const std::optional<TransferReceipt> & {
	return _lastReceipt;
}

const TransferItem *Session::submittedShown(
		const SubmittedTransfer &entry) const {
	if (entry.generation != _networkGeneration
		|| !transferWalletIdentityCurrent(entry.identity)
		|| (!entry.canonicalId.isEmpty()
			&& ranges::contains(
				_history,
				entry.canonicalId,
				&TransferItem::id))) {
		return nullptr;
	}
	return entry.item ? entry.item.get() : entry.fallback.get();
}

std::vector<TransferItem> Session::submittedTransactions() const {
	auto result = std::vector<TransferItem>();
	for (const auto &entry : _submitted) {
		if (const auto item = submittedShown(entry)) {
			result.push_back(*item);
		}
	}
	return result;
}

auto Session::listedSubmittedTransactions() const
-> std::vector<ListedSubmittedTransfer> {
	auto result = std::vector<ListedSubmittedTransfer>();
	for (const auto &entry : _submitted) {
		const auto item = submittedShown(entry);
		if (!item
			|| (_listedBoundary
				&& item->date
				&& (*item->date < *_listedBoundary))) {
			continue;
		}
		result.push_back({ entry.operationId, *item });
	}
	return result;
}

TonConnectSendFate Session::tonConnectSendFate(
		const std::string &operationId) {
	const auto identity = transferWalletIdentity();
	if (!_sendRecoveryReady || _clientStopping || !identity) {
		return TonConnectSendFate::Unknown;
	} else if (_submission && _submission->operationId == operationId) {
		return TonConnectSendFate::Sending;
	} else if ((_pending && _pending->operationId == operationId)
		|| (_sendUnresolved
			&& (_unresolvedOperationId.empty()
				|| _unresolvedOperationId == operationId))) {
		return TonConnectSendFate::Unresolved;
	}
	const auto record = submittedTransferRecord(operationId, *identity);
	if (!record) {
		return TonConnectSendFate::Absent;
	} else if (record->terminal == TransferTerminal::None) {
		return TonConnectSendFate::Unresolved;
	}
	return FailedTransferTerminal(record->terminal)
		? TonConnectSendFate::NotExecuted
		: TonConnectSendFate::Settled;
}

std::optional<TransferItem> Session::submittedTransaction(
		const std::string &operationId) const {
	const auto entry = ranges::find(
		_submitted,
		operationId,
		&SubmittedTransfer::operationId);
	if (entry == end(_submitted)
		|| entry->generation != _networkGeneration
		|| !transferWalletIdentityCurrent(entry->identity)) {
		return std::nullopt;
	}
	if (!entry->canonicalId.isEmpty()) {
		const auto item = ranges::find(
			_history,
			entry->canonicalId,
			&TransferItem::id);
		if (item != end(_history)) {
			return *item;
		}
	}
	return entry->item ? std::make_optional(*entry->item) : std::nullopt;
}

std::optional<TransferItem> Session::trackedTransaction(
		const std::string &operationId) const {
	if (auto result = submittedTransaction(operationId)) {
		return result;
	}
	const auto entry = ranges::find(
		_submitted,
		operationId,
		&SubmittedTransfer::operationId);
	const auto shown = (entry != end(_submitted))
		? submittedShown(*entry)
		: nullptr;
	return shown ? std::make_optional(*shown) : std::nullopt;
}

std::optional<TransferItem> Session::sendingTransaction(
		const std::string &operationId) const {
	if (operationId.empty()
		|| _sendState.current() != SendState::Sending
		|| !_submission
		|| _submission->operationId != operationId
		|| !_submission->rpcStarted
		|| !transferWalletIdentityCurrent(_submission->prepared->identity)) {
		return std::nullopt;
	}
	const auto &args = _submission->prepared->args;
	auto result = ItemFromPending({
		.walletIdentity = _submission->prepared->identity,
		.posted = _submission->posted,
		.amountNano = args.amountNano,
		.destination = CanonicalAddress(args.destination),
		.collectible = args.collectible,
		.comment = (args.comment.isPublic ? args.comment.text : QString()),
		.recipient = args.userId,
		.bounce = args.bounce,
	});
	const auto entry = ranges::find(
		_submitted,
		operationId,
		&SubmittedTransfer::operationId);
	if (entry != end(_submitted)
		&& entry->generation == _networkGeneration
		&& transferWalletIdentityCurrent(entry->identity)) {
		result.id = entry->canonicalId;
	}
	if (!result.id.isEmpty() || result.counterparty.isEmpty()) {
		return result;
	}
	const auto same = [&](const TransferItem &item) {
		return result.collectible.isEmpty()
			? (item.counterparty == result.counterparty
				&& item.amountNano == result.amountNano)
			: (item.kind == TransferItem::Kind::Collectible
				&& item.collectible == result.collectible);
	};
	const auto ambiguous = ranges::any_of(_submitted, [&](const auto &other) {
		return other.operationId != operationId
			&& other.generation == _networkGeneration
			&& other.canonicalId.isEmpty()
			&& (other.item || other.fallback)
			&& same(other.item ? *other.item : *other.fallback);
	});
	if (ambiguous) {
		return result;
	}
	const auto from = _submission->posted - kSendingMatchSkew;
	const auto candidate = [&](const TransferItem &item) {
		return !item.incoming
			&& item.kind != TransferItem::Kind::KeyChange
			&& !item.id.isEmpty()
			&& item.walletIdentity == result.walletIdentity
			&& same(item)
			&& item.date
			&& *item.date >= from
			&& !ranges::contains(
				_submitted,
				item.id,
				&SubmittedTransfer::canonicalId);
	};
	const auto found = ranges::find_if(_history, candidate);
	if (found == end(_history)) {
		return result;
	}
	// Identical transfers cannot be told apart, so none of them is hidden.
	const auto single = ranges::none_of(_history, [&](const auto &item) {
		return candidate(item) && (item.id != found->id);
	});
	if (single) {
		result.id = found->id;
	}
	return result;
}

} // namespace Wallet
