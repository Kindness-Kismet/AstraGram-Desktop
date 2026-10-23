/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_messages_search_merged.h"

#include "history/history.h"

namespace Api {

MessagesSearchMerged::MessagesSearchMerged(
	not_null<History*> history,
	LocalSource local)
: _apiSearch(history)
, _localSource(std::move(local)) {
	if (_localSource.search) {
		std::move(_localSource.changes) | rpl::on_next([=] {
			if (_outputStarted) {
				refreshLocal();
				_updatedFounds.fire({});
			}
		}, _lifetime);
		return;
	}
	if (const auto migrated = history->migrateFrom()) {
		_migratedSearch.emplace(migrated);
	}

	_apiSearch.messagesFounds(
	) | rpl::on_next([=](const FoundMessages &data) {
		applyApiFound(data);
	}, _lifetime);

	if (_migratedSearch) {
		_migratedSearch->messagesFounds(
		) | rpl::on_next([=](const FoundMessages &data) {
			applyMigratedFound(data);
		}, _lifetime);
	}
}

void MessagesSearchMerged::disableMigrated() {
	_migratedSearch = std::nullopt;
	_migratedStarted = false;
	_migratedFinished = true;
	_migratedFailed = false;
	_migratedTotal = -1;
	_concatedFound.failed = _apiFailed;
	_concatedFound.finished = _apiFinished && !_apiFailed;
	_concatedFound.total = _apiTotal;
}

void MessagesSearchMerged::addFound(const FoundMessages &data) {
	for (const auto &message : data.messages) {
		_concatedFound.messages.push_back(message);
	}
}

void MessagesSearchMerged::applyApiFound(const FoundMessages &data) {
	_apiFinished = data.finished;
	_apiFailed = data.failed;
	_apiTotal = data.total;
	addFound(data);
	updateCombinedState(data);
	fireFound();
	if (_apiFinished && !_apiFailed) {
		startMigrated();
	}
}

void MessagesSearchMerged::applyMigratedFound(const FoundMessages &data) {
	_migratedFinished = data.finished;
	_migratedFailed = data.failed;
	_migratedTotal = data.total;
	addFound(data);
	updateCombinedState(data);
	fireFound();
}

void MessagesSearchMerged::updateCombinedState(const FoundMessages &data) {
	_concatedFound.nextToken = data.nextToken;
	_concatedFound.total = !_migratedStarted
		? _apiTotal
		: (_apiTotal >= 0 && _migratedTotal >= 0)
		? _apiTotal + _migratedTotal
		: -1;
	_concatedFound.failed = _apiFailed || _migratedFailed;
	_concatedFound.finished = _apiFinished
		&& (!_migratedSearch || (_migratedStarted && _migratedFinished))
		&& !_concatedFound.failed;
	_concatedFound.totalOnly = data.totalOnly
		&& data.messages.empty()
		&& _concatedFound.messages.empty()
		&& !data.finished
		&& !data.failed;
}

void MessagesSearchMerged::startMigrated() {
	if (!_migratedSearch || _migratedStarted || _apiFailed) {
		return;
	}
	_migratedStarted = true;
	_migratedSearch->searchMessages(_request);
}

void MessagesSearchMerged::fireFound() {
	if (_outputStarted) {
		_nextFounds.fire({});
	} else {
		_outputStarted = true;
		_newFounds.fire({});
	}
}

const FoundMessages &MessagesSearchMerged::messages() const {
	return _concatedFound;
}

const MessagesSearch::Request &MessagesSearchMerged::request() const {
	return _request;
}

void MessagesSearchMerged::clear() {
	_concatedFound = {};
	_apiFinished = false;
	_apiFailed = false;
	_migratedStarted = false;
	_migratedFinished = false;
	_migratedFailed = false;
	_outputStarted = false;
	_apiTotal = -1;
	_migratedTotal = -1;
}

void MessagesSearchMerged::search(const Request &search) {
	_request = search;
	clear();
	if (_localSource.search) {
		refreshLocal();
		fireFound();
		return;
	}
	if (_migratedSearch) {
		_migratedSearch->cancel();
	}
	_apiSearch.searchMessages(search);
}

void MessagesSearchMerged::searchMore() {
	if (_localSource.search) {
		return;
	}
	if (_apiFailed || _migratedFailed) {
		return;
	} else if (!_apiFinished) {
		_apiSearch.searchMore();
	} else if (_migratedSearch && !_migratedStarted) {
		startMigrated();
	} else if (_migratedSearch && !_migratedFinished) {
		_migratedSearch->searchMore();
	}
}

rpl::producer<> MessagesSearchMerged::newFounds() const {
	return _newFounds.events();
}

rpl::producer<> MessagesSearchMerged::nextFounds() const {
	return _nextFounds.events();
}

void MessagesSearchMerged::refreshLocal() {
	_concatedFound = _localSource.search(_request);
	_concatedFound.total = int(_concatedFound.messages.size());
	_concatedFound.totalOnly = false;
	_concatedFound.finished = !_concatedFound.failed;
	_apiTotal = _concatedFound.total;
	_apiFailed = _concatedFound.failed;
	_apiFinished = _concatedFound.finished;
}

rpl::producer<> MessagesSearchMerged::updatedFounds() const {
	return _updatedFounds.events();
}

} // namespace Api
