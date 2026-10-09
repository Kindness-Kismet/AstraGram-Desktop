#include "wallet/wallet_session_internal.h"

namespace Wallet {
using namespace SessionDetails;

void Session::refreshCollectibles(bool force) {
	ensureLoaded();
	if (_presence.current() != Presence::Ready) {
		return;
	}
	// A forced ask refused below is owed until a head request goes out.
	_collectiblesForced = _collectiblesForced || force;
	const auto forced = _collectiblesForced
		|| !_collectibleFollowUps.empty();
	const auto interval = forced
		? kForcedCollectiblesInterval
		: kCollectiblesPollInterval;
	if (_collectiblesRequest
		|| _collectiblesPaged
		|| (_collectiblesRefreshedAt
			&& (crl::now() - _collectiblesRefreshedAt < interval))) {
		return;
	}
	_collectiblesForced = false;
	_collectiblesRefreshedAt = crl::now();
	requestCollectibles(false);
}

bool Session::collectiblesHasNext() const {
	return _collectiblesHasMore;
}

void Session::loadMoreCollectibles() {
	ensureLoaded();
	if (_presence.current() != Presence::Ready
		|| _collectiblesRequest
		|| !_collectiblesHasMore) {
		return;
	}
	requestCollectibles(true);
}

void Session::requestCollectibles(bool more) {
	if (_collectiblesRequest
		|| (more && _collectiblesNextOffset.isEmpty())) {
		return;
	}
	const auto request = std::make_shared<CollectiblesRequest>(
		CollectiblesRequest{
			.identity = transferWalletIdentity(),
			.identityRevision = _walletIdentityRevision,
			.generation = _networkGeneration,
			.offset = more ? _collectiblesNextOffset : QString(),
			.more = more,
		});
	_collectiblesRequest = request;
	request->id = _stateApi.request(MTPwallet_GetNfts(
		MTP_string(request->offset),
		MTP_int(kCollectiblesPerPage)
	)).done([=](const MTPwallet_NftItems &result) {
		const auto current = (_collectiblesRequest == request)
			&& listRequestCurrent(
				request->identity,
				request->identityRevision,
				request->generation);
		if (_collectiblesRequest == request) {
			_collectiblesRequest = nullptr;
		}
		if (current) {
			applyCollectibles(result, *request);
		}
	}).fail([=](const MTP::Error &error) {
		const auto current = (_collectiblesRequest == request)
			&& listRequestCurrent(
				request->identity,
				request->identityRevision,
				request->generation);
		if (_collectiblesRequest == request) {
			_collectiblesRequest = nullptr;
		}
		if (current) {
			LOG(("Wallet Error: wallet.getNfts failed: %1, "
				"keeping last-good collectibles."
				).arg(error.type()));
			if (!request->more) {
				spendCollectibleFollowUps(nullptr);
			}
		}
	}).handleAllErrors().send();
}

void Session::applyCollectibles(
		const MTPwallet_NftItems &result,
		const CollectiblesRequest &request) {
	const auto &data = result.data();
	const auto served = data.vnext_offset();
	auto next = served ? qs(*served) : QString();
	if (!next.isEmpty() && (next == request.offset)) {
		LOG(("Wallet Error: wallet.getNfts repeated its offset."));
		next = QString();
	}
	auto loaded = CollectiblesFromServer(data.vitems().v);
	_collectiblesNextOffset = next;
	_collectiblesHasMore = !next.isEmpty();
	_collectiblesCompletedAt = crl::now();
	_collectiblesPaged = request.more
		&& (_panel != nullptr)
		&& collectiblesTab();
	auto list = std::vector<Gram::NftItem>();
	if (request.more) {
		auto fresh = UnheldCollectibles(_collectibles, std::move(loaded));
		list = _collectibles;
		list.insert(
			end(list),
			std::make_move_iterator(begin(fresh)),
			std::make_move_iterator(end(fresh)));
	} else {
		list = std::move(loaded);
		spendCollectibleFollowUps(&list);
	}
	if (!SameCollectibles(_collectibles, list)) {
		setCollectibles(std::move(list));
	}
}

void Session::setCollectibles(std::vector<Gram::NftItem> &&list) {
	_collectibles = std::move(list);
	if (_collectibles.empty()) {
		_collectiblesTab = false;
		refreshStaleHistory();
	}
	for (const auto &item : _collectibles) {
		_collectibleInfo[item.address] = item;
	}
	_collectiblesUpdates.fire({});
}

void Session::rememberCollectibles(const std::vector<TransferItem> &items) {
	for (const auto &item : items) {
		if (const auto &record = item.collectibleRecord) {
			_collectibleInfo.emplace(record->address, *record);
		}
	}
}

void Session::followCollectibles(const std::vector<TransferItem> &arrived) {
	auto followed = false;
	for (const auto &row : arrived) {
		auto sent = false;
		if (!row.incoming) {
			for (auto &entry : _submitted) {
				if (entry.collectible != row.collectible
					|| entry.generation != _networkGeneration
					|| !transferWalletIdentityCurrent(entry.identity)
					|| (!entry.canonicalId.isEmpty()
						&& entry.canonicalId != row.id)) {
					continue;
				}
				sent = sent || entry.leaving;
				entry.leaving = true;
			}
		}
		// This client's own send already armed that transfer's follow-up.
		if (!sent) {
			followCollectible(row.collectible, row.incoming);
			followed = true;
		}
	}
	if (followed) {
		refreshCollectibles(true);
	}
}

void Session::followCollectible(const QString &address, bool incoming) {
	auto &followUp = _collectibleFollowUps[address];
	if (!followUp.left) {
		followUp.left = kCollectibleFollowUpRefreshes;
	}
	followUp.incoming = incoming;
}

void Session::spendCollectibleFollowUps(
		const std::vector<Gram::NftItem> *list) {
	auto &followUps = _collectibleFollowUps;
	for (auto i = begin(followUps); i != end(followUps);) {
		const auto listed = list
			&& ranges::contains(*list, i->first, &Gram::NftItem::address);
		const auto reflected = list && (listed == i->second.incoming);
		if (!reflected && (--i->second.left > 0)) {
			++i;
		} else {
			i = followUps.erase(i);
		}
	}
}

void Session::updateListsGate() {
	const auto weak = base::make_weak(_engine.get());
	const auto revision = _walletIdentityRevision;
	const auto presence = _presence.current();
	const auto unknown = (presence == Presence::Unknown);
	const auto ready = (presence == Presence::Ready);
	_stateUnreachable = (unknown
		&& (_stateFailures >= kStateFailuresBeforeStated))
		|| (ready && _historyUnreachable)
		|| (presence == Presence::AddressUnreadable);
	_listsGated = (unknown && !_stateUnreachable)
		|| (presence == Presence::Provisioning)
		|| (ready && !_historySettled && submittedTransactions().empty());
	if (weak && revision == _walletIdentityRevision) {
		_listsStateUpdates.fire({});
	}
}

bool Session::listsConfirmedEmpty() const {
	return !_listsGated.current()
		&& submittedTransactions().empty()
		&& historyVisibleEmpty()
		&& !_historyHasNext
		&& _collectibles.empty();
}

void Session::resolveCollectibleInfo(
		const QString &item,
		Fn<void(const Gram::NftItem &)> done) {
	const auto i = _collectibleInfo.find(item);
	if (i != end(_collectibleInfo)) {
		done(i->second);
		return;
	}
	auto &waiters = _collectibleInfoWaiters[item];
	const auto first = waiters.empty();
	waiters.push_back(std::move(done));
	if (!first) {
		return;
	}
	const auto finish = [=](Gram::NftItem found, bool remember) {
		const auto i = _collectibleInfo.find(item);
		if (i != end(_collectibleInfo)) {
			found = i->second;
		} else if (remember) {
			_collectibleInfo.emplace(item, found);
		}
		auto &waiting = _collectibleInfoWaiters[item];
		for (const auto &callback : base::take(waiting)) {
			callback(found);
		}
		_collectibleInfoWaiters.remove(item);
	};
	_api.request(
		Gram::NftItemByAddressRequest(item),
		[=](const QByteArray &json) {
			auto found = Gram::NftItem();
			if (const auto page = Gram::ParseNftItems(json, 0)) {
				for (const auto &entry : page->list) {
					if (entry.address == item) {
						found = entry;
						break;
					}
				}
			}
			finish(found, true);
		},
		[=](const Gram::ApiError &) {
			finish(Gram::NftItem(), false);
		});
}

} // namespace Wallet
