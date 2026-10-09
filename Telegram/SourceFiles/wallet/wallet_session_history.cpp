#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

void Session::refreshHistory(Fn<void()> done) {
	ensureLoaded();
	if ((_presence.current() != Presence::Ready) || _historyPaged) {
		if (done) {
			done();
		}
		return;
	}
	requestTransactions(false, std::move(done));
}

void Session::refreshStaleHistory() {
	// _historyPaged is deliberately not consulted — a push is the server
	// stating that this wallet's history moved, which a poll and a stream
	// hint are not — and the applied head page clears that term itself.
	if (!_historyStale
		|| (_presence.current() != Presence::Ready)
		|| !pollingRequested()
		|| collectiblesTab()
		|| _historyRequest) {
		return;
	}
	requestTransactions(false);
}

void Session::applyTransferMinNanos() {
	const auto now = TransferMinNanos(_session);
	if (_transferMinNanos == now) {
		return;
	}
	_transferMinNanos = now;
	// The canonical list did not move, so nothing is reloaded and no
	// identity changes: what moved is which of its rows the feed may show,
	// which is exactly what an applied page publishes. _historyUpdates is
	// the one stream every consumer of the projection already rides -
	// rebuildList, HistoryShownValue and listsEmptyStateValue all merge it -
	// so the gate itself is not recomputed here: none of its terms moved.
	// The paging bound is re-armed rather than inherited, because a moved
	// threshold changes what the reader can see at all: pages that were
	// spent looking for rows under the old one say nothing about the new.
	_historyUpdates.fire({});
	resetHiddenHistoryPages();
	continueHiddenHistory(!historyVisibleEmpty());
}

void Session::applyWalletAvailable() {
	const auto now = _session->appConfig().walletAvailable();
	if (_walletAvailable == now) {
		return;
	}
	_walletAvailable = now;
	if (!now) {
		return;
	}
	// WHY: WALLET_UNAVAILABLE answers latched before the server made the
	// wallet available describe the old state, so both lanes ask again.
	_userAddresses->resetUnavailable();
	if (_presence.current() != Presence::Unavailable) {
		return;
	}
	_stateFailures = 0;
	_stateRequestedAt = 0;
	_stateRefreshedAt = 0;
	setPresence(Presence::Unknown);
	if (_loaded) {
		refreshState();
	}
}

void Session::setHistory(std::vector<TransferItem> &&list) {
	_history = std::move(list);
	dropSubmittedIfListed();
	_listedBoundary = historyCanPage()
		? OldestHistoryDate(_history)
		: std::nullopt;
	_historyUpdates.fire({});
}

void Session::requestTransactions(bool more, Fn<void()> done) {
	if (_historyRequest) {
		if (done) {
			_historyRequest->done.push_back(std::move(done));
		}
		return;
	}
	const auto request = std::make_shared<HistoryRequest>(HistoryRequest{
		.identity = transferWalletIdentity(),
		.identityRevision = _walletIdentityRevision,
		.generation = _networkGeneration,
		.offset = more ? _historyNextOffset : QString(),
	});
	if (done) {
		request->done.push_back(std::move(done));
	}
	_historyRequest = request;
	// A head page issued after an invalidation is what the marker asked for,
	// whoever issued it, so it is spent here at the issue and not at the
	// success: a failed forced page therefore retries nothing by itself, and
	// a push landing during the flight re-arms the marker so that flight's
	// older answer can never satisfy it. A more page never spends it,
	// because it does not refresh the head.
	if (!more) {
		_historyStale = false;
	}
	_historyRequestedAt = crl::now();
	// The inbound and outbound flags stay unset on purpose: the overview
	// shows one undivided feed and offers no direction filter, so asking the
	// server for half of the list would invent a UI this task does not add.
	// They stay available for a filter that is actually designed.
	request->id = _stateApi.request(MTPwallet_GetTransactions(
		MTP_flags(0),
		MTP_string(request->offset),
		MTP_int(kTransactionsPerPage)
	)).done([=](const MTPwallet_Transactions &result) {
		const auto current = (_historyRequest == request)
			&& historyRequestCurrent(*request);
		if (_historyRequest == request) {
			_historyRequest = nullptr;
		}
		const auto weak = base::make_weak(_engine.get());
		if (current) {
			applyTransactions(result, more, *request);
		}
		FinishHistoryWaiters(base::take(request->done));
		if (weak && current && historyRequestCurrent(*request)) {
			refreshStaleHistory();
		}
	}).fail([=](const MTP::Error &error) {
		const auto current = (_historyRequest == request)
			&& historyRequestCurrent(*request);
		if (_historyRequest == request) {
			_historyRequest = nullptr;
		}
		const auto weak = base::make_weak(_engine.get());
		if (current) {
			LOG(("Wallet Error: wallet.getTransactions failed: %1"
				).arg(error.type()));
			if (!more) {
				_historyUnreachable = true;
			}
			_historySettled = true;
			updateListsGate();
			releaseDeferredRows();
		}
		FinishHistoryWaiters(base::take(request->done));
		if (weak && current && historyRequestCurrent(*request)) {
			refreshStaleHistory();
		}
	}).handleAllErrors().send();
}

bool Session::listRequestCurrent(
		const std::optional<TransferWalletIdentity> &identity,
		uint64 identityRevision,
		int generation) const {
	return generation == _networkGeneration
		&& identityRevision == _walletIdentityRevision
		&& _presence.current() == Presence::Ready
		&& identity == transferWalletIdentity();
}

bool Session::historyRequestCurrent(const HistoryRequest &request) const {
	return listRequestCurrent(
		request.identity,
		request.identityRevision,
		request.generation);
}

void Session::resolveTransaction(
		const QString &id,
		Fn<void(ResolvedTransaction)> done) {
	if (id.isEmpty()) {
		if (done) {
			done({ .failed = true });
		}
		return;
	}
	const auto identity = transferWalletIdentity();
	_stateApi.request(MTPwallet_GetTransactionsByIDs(
		MTP_vector<MTPstring>(1, MTP_string(id.toStdString()))
	)).done([=](const MTPwallet_Transactions &result) {
		const auto &data = result.data();
		// The peers come first, for the same reason the feed stores them
		// first: a transaction whose user is missing from Data::Session
		// falls back to its address instead of the Telegram identity.
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());
		auto list = HistoryFromServer(data.vtransactions().v, identity);
		rememberCollectibles(list);
		// WHY: the id a message carries is the transfer's trace id, while
		// the served row is named by its own lt:hash, so an answer to one
		// id is its only row, and anything else names no transaction.
		if (done) {
			done({ .item = (list.size() == 1)
				? std::make_optional(std::move(list.front()))
				: std::nullopt });
		}
	}).fail([=](const MTP::Error &error) {
		LOG(("Wallet Error: wallet.getTransactionsByIDs failed: %1"
			).arg(error.type()));
		if (done) {
			done({ .failed = true });
		}
	}).send();
}

void Session::applyTransactions(
		const MTPwallet_Transactions &result,
		bool more,
		const HistoryRequest &request) {
	if (!historyRequestCurrent(request)) {
		return;
	}
	const auto weak = base::make_weak(_engine.get());
	const auto &data = result.data();
	// The peers are stored before anything resolves one, because a row whose
	// user is missing from Data::Session falls through to the address
	// and domain presentation. That is also legitimate for an address-only
	// peer, so a dropped users vector would look exactly like a working
	// client while losing the Telegram identity.
	_session->data().processUsers(data.vusers());
	if (!weak || !historyRequestCurrent(request)) {
		return;
	}
	_session->data().processChats(data.vchats());
	if (!weak || !historyRequestCurrent(request)) {
		return;
	}
	const auto wasNextOffset = _historyNextOffset;
	const auto wasHasNext = _historyHasNext;
	const auto next = data.vnext_offset();
	// An empty next_offset is byte-identical to a first-page request, so
	// paging on it would read the same rows forever. It ends the list exactly
	// as an absent one does; the value itself is opaque and never parsed.
	_historyNextOffset = next ? qs(*next) : QString();
	_historyHasNext = !_historyNextOffset.isEmpty();
	if (_historyHasNext && (_historyNextOffset == request.offset)) {
		// A cursor that comes back byte-identical to the one just spent
		// is the server making no progress: the next request would read
		// the same rows and append them a second time, which is a
		// duplicate-row defect as much as a request loop. It ends the
		// list exactly as an absent cursor does. A head request sends an
		// empty offset, which _historyHasNext already excludes, so this
		// can only ever fire for a `more` page.
		LOG(("Wallet Error: wallet.getTransactions repeated its offset."));
		_historyNextOffset = QString();
		_historyHasNext = false;
	}
	const auto opened = (_historyRefreshedAt != 0);
	_historyRefreshedAt = crl::now();
	_historyUnreachable = false;
	// checkLoadMore() pages only the transactions tab, but the answer is a
	// round trip late, so a page landing after the reader moved to
	// Collectibles would re-arm the term setCollectiblesTab() just released
	// and refuse the head refresh for a list nobody is looking at.
	// collectiblesTab() is the effective selection, never the raw request.
	_historyPaged = more
		&& (_panel != nullptr)
		&& !collectiblesTab();
	_historySettled = true;
	auto loaded = HistoryFromServer(data.vtransactions().v, request.identity);
	rememberCollectibles(loaded);
	const auto shown = ranges::any_of(loaded, [&](const TransferItem &i) {
		return !historyItemHidden(i);
	});
	if (shown) {
		_historyHiddenPages = 0;
	}
	auto arrived = std::vector<TransferItem>();
	if (more) {
		auto fresh = UnheldHistory(_history, std::move(loaded));
		if (!fresh.empty()) {
			auto list = _history;
			list.insert(
				end(list),
				std::make_move_iterator(begin(fresh)),
				std::make_move_iterator(end(fresh)));
			setHistory(std::move(list));
		}
	} else {
		const auto served = int(loaded.size());
		const auto full = (served == kTransactionsPerPage);
		// WHY: the list asked for when a wallet opens covers its first page,
		// and a row the history already held was followed when it arrived,
		// so only a row this history never held asks for the list again.
		if (opened) {
			arrived = ArrivedCollectibles(_history, loaded);
		}
		auto merged = MergedHeadHistory(_history, std::move(loaded));
		if ((merged.retained > 0)
			&& full
			&& (merged.retained == int(_history.size()))) {
			// A full page naming none of the loaded rows cannot be shown
			// to touch them: a page's worth of transfers has arrived
			// since the reader last paged, so keeping both halves would
			// leave a hole between them that no cursor reaches. The
			// served window is the truthful list there, which is what a
			// head page has always been, and its own cursor stands.
			merged.list.resize(served);
		} else if (!merged.namedLast) {
			// The page did not name the row the loaded list ends with, so
			// it did not reach past that tail and the cursor it carries
			// points back inside rows the merged list still holds:
			// spending it would re-read them, the merge would drop them
			// again as duplicates, and the older pages behind them would
			// never be reached. The cursor the list already had points
			// past its whole tail, so that is the one paging must keep,
			// and an exhausted list stays exhausted for the same reason.
			// A page that did reach the tail is past every merged row, so
			// there its own cursor stands.
			_historyNextOffset = wasNextOffset;
			_historyHasNext = wasHasNext;
		}
		if (!SameHistory(_history, merged.list)) {
			setHistory(std::move(merged.list));
		}
	}
	if (weak && historyRequestCurrent(request)) {
		updateListsGate();
		if (weak && historyRequestCurrent(request)) {
			continueHiddenHistory(shown);
		}
		if (weak && historyRequestCurrent(request)) {
			followCollectibles(arrived);
		}
		if (weak && historyRequestCurrent(request)) {
			updatePollingState();
		}
		if (weak && historyRequestCurrent(request)) {
			releaseDeferredRows();
		}
	}
}

void Session::continueHiddenHistory(bool progressed) {
	if (progressed) {
		resetHiddenHistoryPages();
		return;
	}
	if ((_panel == nullptr) || collectiblesTab()) {
		return;
	}
	loadMoreHistory();
}

void Session::clearHistory() {
	const auto request = base::take(_historyRequest);
	if (request) {
		_stateApi.request(request->id).cancel();
	}
	clearSubmittedTransfers();
	_history.clear();
	_historyHasNext = false;
	_historyNextOffset = QString();
	_historyRefreshedAt = 0;
	_historyRequestedAt = 0;
	_historySettled = false;
	_historyUnreachable = false;
	_historyPaged = false;
	_historyStale = false;
	resetHiddenHistoryPages();
	// _historySettled and _historyUnreachable, cleared just above, are the
	// gate's two history terms, so this drain is the only point at which an
	// emptied list and the gate the previous page settled could be read
	// together. Recomputing here keeps this lane's own publication from
	// ever being evaluated against the gate of the wallet whose rows just
	// left: an open gate over two empty lists is listsConfirmedEmpty().
	const auto weak = base::make_weak(_engine.get());
	const auto sendRevision = _sendRevision;
	updateListsGate();
	if (weak && sendRevision == _sendRevision) {
		_sendState = SendState::Idle;
	}
	if (weak && sendRevision == _sendRevision) {
		_historyUpdates.fire({});
	}
	if (request) {
		FinishHistoryWaiters(base::take(request->done));
	}
}

void Session::clearCollectibles() {
	if (const auto request = base::take(_collectiblesRequest)) {
		_stateApi.request(request->id).cancel();
	}
	_collectibles.clear();
	_collectibleFollowUps.clear();
	_collectiblesRefreshedAt = 0;
	_collectiblesCompletedAt = 0;
	_collectiblesForced = false;
	_collectiblesHasMore = false;
	_collectiblesNextOffset = QString();
	_collectiblesPaged = false;
	_collectiblesTab = false;
	_collectiblesUpdates.fire({});
}

bool Session::historyHasNext() const {
	return _historyHasNext;
}

bool Session::historyCanPage() const {
	return _historyHasNext && (_historyHiddenPages < kMaxHiddenPagesInRow);
}

void Session::releaseDeferredRows() {
	if (historyCanPage() || !_listedBoundary) {
		return;
	}
	_listedBoundary = std::nullopt;
	_historyUpdates.fire({});
}

bool Session::historyLoadingMore() const {
	// The feed has nothing it can show and the server says more exists, so
	// the walk that looks for a row worth a line is either running or owed.
	// An empty _history answers true as well, which is the same statement:
	// a page that carried a cursor and no row at all is still being looked
	// past. The two streams below carry every move of either term -
	// setHistory() fires _historyUpdates, and applyTransactions() ends in
	// updateListsGate(), which fires _listsStateUpdates even for the head
	// page that matched what was already loaded and wrote no rows.
	return _historyHasNext && historyVisibleEmpty();
}

rpl::producer<bool> Session::historyLoadingMoreValue() const {
	return rpl::single(rpl::empty) | rpl::then(rpl::merge(
		historyUpdates(),
		_listsStateUpdates.events()
	)) | rpl::map([=, this] {
		return historyLoadingMore();
	}) | rpl::distinct_until_changed();
}

void Session::loadMoreHistory() {
	ensureLoaded();
	if (_presence.current() != Presence::Ready
		|| _historyRequest
		|| !_historyHasNext) {
		return;
	}
	// The bound is spent here, where the page is actually asked for, and
	// not at either pager's own entry: a hidden row adds no height, so the
	// list can keep believing it is short of content and ask again through
	// checkLoadMore(), while continueHiddenHistory() asks for the same lane
	// from the answer side. Counting requests is what makes both finite.
	if (_historyHiddenPages >= kMaxHiddenPagesInRow) {
		return;
	}
	++_historyHiddenPages;
	if (_historyHiddenPages == kMaxHiddenPagesInRow) {
		LOG(("Wallet: transaction paging bound of %1 pages spent."
			).arg(kMaxHiddenPagesInRow));
	}
	requestTransactions(true);
}

void Session::resetHiddenHistoryPages() {
	_historyHiddenPages = 0;
	releaseDeferredRows();
}

} // namespace Wallet
