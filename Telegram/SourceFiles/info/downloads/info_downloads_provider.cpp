/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/downloads/info_downloads_provider.h"

#include "info/media/info_media_widget.h"
#include "info/media/info_media_list_section.h"
#include "info/info_controller.h"
#include "ui/text/format_song_document_name.h"
#include "ui/ui_utility.h"
#include "data/data_download_manager.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/history.h"
#include "core/application.h"
#include "core/mime_type.h"
#include "storage/storage_shared_media.h"
#include "layout/layout_selection.h"
#include "styles/style_overview.h"

namespace Info::Downloads {
using namespace Media;

Provider::Provider(not_null<AbstractController*> controller)
: Provider(&controller->session(), controller->storiesAddToAlbumId()) {
}

Provider::Provider(not_null<Main::Session*> session, int storiesAddToAlbumId)
: _session(session)
, _storiesAddToAlbumId(storiesAddToAlbumId) {
	style::PaletteChanged(
	) | rpl::on_next([=] {
		for (auto &layout : _layouts) {
			layout.second.item->invalidateCache();
		}
	}, _lifetime);
}

std::vector<Provider::Entry> Provider::entries() const {
	auto result = std::vector<Entry>();
	for (const auto &element : ranges::views::reverse(_elements)) {
		if (element.found) {
			result.push_back({ element.item, element.started, element.path });
		}
	}
	return result;
}

Type Provider::type() {
	return Type::File;
}

bool Provider::hasSelectRestriction() {
	return false;
}

rpl::producer<bool> Provider::hasSelectRestrictionChanges() {
	return rpl::never<bool>();
}

bool Provider::sectionHasFloatingHeader() {
	return false;
}

QString Provider::sectionTitle(not_null<const BaseLayout*> item) {
	return QString();
}

bool Provider::sectionItemBelongsHere(
		not_null<const BaseLayout*> item,
		not_null<const BaseLayout*> previous) {
	return true;
}

bool Provider::isPossiblyMyItem(not_null<const HistoryItem*> item) {
	return true;
}

std::optional<int> Provider::fullCount() {
	return (_foundCount || _fullCount.has_value())
		? _foundCount
		: std::optional<int>();
}

void Provider::restart() {
}

void Provider::checkPreload(
	QSize viewport,
	not_null<BaseLayout*> topLayout,
	not_null<BaseLayout*> bottomLayout,
	bool preloadTop,
	bool preloadBottom) {
}

void Provider::setSearchQuery(QString query) {
	if (_query == query) {
		return;
	}
	_query = query;
	auto words = TextUtilities::PrepareSearchWords(_query);
	if (_queryWords == words) {
		return;
	}
	_queryWords = std::move(words);
	refreshFoundElements();
}

void Provider::setTypeFilter(TypeFilter filter) {
	if (_typeFilter == filter) {
		return;
	}
	_typeFilter = filter;
	refreshFoundElements();
}

void Provider::refreshFoundElements() {
	_foundCount = 0;
	for (auto &element : _elements) {
		if ((element.found = computeIsFound(element))) {
			++_foundCount;
		}
	}
	if (_started) {
		_refreshed.fire({});
	}
}

void Provider::jumpToMessage(MsgId messageId, Fn<void(FullMsgId)>) {
}

void Provider::refreshViewer() {
	if (_started) {
		return;
	}
	_started = true;
	auto &manager = Core::App().downloadManager();
	manager.loadingListChanges() | rpl::on_next([=] {
		if (_postponedLoadingRefresh) {
			return;
		}
		_postponedLoadingRefresh = true;
		Ui::PostponeCall(this, [=] {
			_postponedLoadingRefresh = false;
			refreshLoadingList();
		});
	}, _lifetime);
	refreshLoadingList();

	for (const auto id : manager.loadedList()) {
		addPostponed(id);
	}

	manager.loadedAdded(
	) | rpl::on_next([=](not_null<const Data::DownloadedId*> entry) {
		addPostponed(entry);
	}, _lifetime);

	manager.loadedRemoved(
	) | rpl::on_next([=](not_null<const HistoryItem*> item) {
		if (!_downloading.contains(item)) {
			remove(item);
		} else {
			_downloaded.remove(item);
			_addPostponed.erase(
				ranges::remove(_addPostponed, item, &Element::item),
				end(_addPostponed));
		}
	}, _lifetime);

	manager.loadedResolveDone(
	) | rpl::on_next([=] {
		if (!_fullCount.has_value()) {
			_fullCount = 0;
			refreshPostponed(false);
		}
	}, _lifetime);

	performAdd();
	performRefresh();
}

void Provider::refreshLoadingList() {
	// 普通下载与外部下载的完成通知顺序不同，批次结束后统一核对。
	performAdd();
	auto copy = _downloading;
	for (const auto id : Core::App().downloadManager().loadingList()) {
		if (id->done) {
			continue;
		}
		const auto item = id->object.item;
		if (copy.remove(item) || _downloaded.contains(item)) {
			continue;
		}
		_downloading.emplace(item);
		addElementNow({
			.item = item,
			.started = id->started,
			.path = id->path,
		});
		trackItemSession(item);
		refreshPostponed(true);
	}
	for (const auto item : copy) {
		Assert(!_downloaded.contains(item));
		remove(item);
	}
	if (!_fullCount.has_value()) {
		refreshPostponed(false);
	}
}

void Provider::addPostponed(not_null<const Data::DownloadedId*> entry) {
	Expects(entry->object != nullptr);

	const auto item = entry->object->item;
	trackItemSession(item);
	const auto i = ranges::find(_addPostponed, item, &Element::item);
	if (i != end(_addPostponed)) {
		if (i->started > entry->started) {
			return;
		}
		i->path = entry->path;
		i->started = entry->started;
	} else {
		_addPostponed.push_back({
			.item = item,
			.started = entry->started,
			.path = entry->path,
		});
		if (_addPostponed.size() == 1) {
			Ui::PostponeCall(this, [=] {
				performAdd();
			});
		}
	}
}

void Provider::performAdd() {
	if (_addPostponed.empty()) {
		return;
	}
	for (auto &element : base::take(_addPostponed)) {
		const auto added = _downloaded.emplace(element.item).second;
		const auto wasDownloading = _downloading.remove(element.item);
		if (added && !wasDownloading) {
			addElementNow(std::move(element));
			continue;
		}
		const auto i = ranges::find(_elements, element.item, &Element::item);
		Assert(i != end(_elements));
		if (i->started > element.started) {
			continue;
		}
		const auto dateChanged = i->started != element.started;
		_foundCount -= i->found;
		*i = std::move(element);
		fillSearchIndex(*i);
		i->found = computeIsFound(*i);
		_foundCount += i->found;
		if (dateChanged) {
			_layoutsToRecreate.emplace(i->item);
		}
	}
	refreshPostponed(true);
}

void Provider::addElementNow(Element &&element) {
	_elements.push_back(std::move(element));
	auto &added = _elements.back();
	fillSearchIndex(added);
	added.found = computeIsFound(added);
	if (added.found) {
		++_foundCount;
	}
}

void Provider::remove(not_null<const HistoryItem*> item) {
	_addPostponed.erase(
		ranges::remove(_addPostponed, item, &Element::item),
		end(_addPostponed));
	_downloading.remove(item);
	_downloaded.remove(item);
	const auto proj = [&](const Element &element) {
		if (element.item != item) {
			return false;
		}
		if (element.found) {
			--_foundCount;
		}
		return true;
	};
	_elements.erase(ranges::remove_if(_elements, proj), end(_elements));
	_layoutsToRecreate.remove(item);
	removeLayout(item);
	refreshPostponed(false);
}

void Provider::removeLayout(not_null<const HistoryItem*> item) {
	if (const auto i = _layouts.find(item); i != end(_layouts)) {
		auto layout = std::move(i->second.item);
		_layouts.erase(i);
		// 通知可能同步重建布局，旧布局在通知结束后才销毁。
		_layoutRemoved.fire(layout.get());
	}
}

void Provider::refreshPostponed(bool added) {
	if (added) {
		_postponedRefreshSort = true;
	}
	if (!_postponedRefresh) {
		_postponedRefresh = true;
		Ui::PostponeCall(this, [=] {
			performRefresh();
		});
	}
}

void Provider::performRefresh() {
	if (!_postponedRefresh) {
		return;
	}
	_postponedRefresh = false;
	if (!_elements.empty() || _fullCount.has_value()) {
		_fullCount = _elements.size();
	}
	if (base::take(_postponedRefreshSort)) {
		ranges::sort(_elements, ranges::less(), &Element::started);
	}
	for (const auto item : base::take(_layoutsToRecreate)) {
		removeLayout(item);
	}
	_refreshed.fire({});
}

void Provider::trackItemSession(not_null<const HistoryItem*> item) {
	const auto session = &item->history()->session();
	if (_trackedSessions.contains(session)) {
		return;
	}
	auto &lifetime = _trackedSessions.emplace(session).first->second;

	session->data().itemRemoved(
	) | rpl::on_next([this](auto item) {
		itemRemoved(item);
	}, lifetime);

	session->data().itemDataChanges(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		const auto i = ranges::find(_elements, item, &Element::item);
		if (i == end(_elements)) {
			return;
		}
		_foundCount -= i->found;
		fillSearchIndex(*i);
		i->found = computeIsFound(*i);
		_foundCount += i->found;
		refreshPostponed(false);
	}, lifetime);

	session->account().sessionChanges(
	) | rpl::take(1) | rpl::on_next([=] {
		_trackedSessions.remove(session);
	}, lifetime);
}

rpl::producer<> Provider::refreshed() {
	return _refreshed.events();
}

std::vector<ListSection> Provider::fillSections(
		not_null<Overview::Layout::Delegate*> delegate) {
	markLayoutsStale();
	const auto guard = gsl::finally([&] { clearStaleLayouts(); });

	if (!_foundCount) {
		return {};
	}

	auto result = std::vector<ListSection>();
	result.emplace_back(Type::File, sectionDelegate());
	auto &section = result.back();
	for (const auto &element : ranges::views::reverse(_elements)) {
		if (!element.found) {
			continue;
		}
		if (auto layout = getLayout(element, delegate)) {
			section.addItem(layout);
		}
	}
	section.finishSection();
	return result;
}

void Provider::markLayoutsStale() {
	for (auto &layout : _layouts) {
		layout.second.stale = true;
	}
}

void Provider::clearStaleLayouts() {
	for (auto i = _layouts.begin(); i != _layouts.end();) {
		if (i->second.stale) {
			_layoutRemoved.fire(i->second.item.get());
			i = _layouts.erase(i);
		} else {
			++i;
		}
	}
}

rpl::producer<not_null<BaseLayout*>> Provider::layoutRemoved() {
	return _layoutRemoved.events();
}

BaseLayout *Provider::lookupLayout(const HistoryItem *item) {
	return nullptr;
}

bool Provider::isMyItem(not_null<const HistoryItem*> item) {
	const auto i = ranges::find(_elements, item, &Element::item);
	return (i != end(_elements)) && i->found;
}

bool Provider::isAfter(
		not_null<const HistoryItem*> a,
		not_null<const HistoryItem*> b) {
	if (a != b) {
		for (const auto &element : _elements) {
			if (element.item == a) {
				return false;
			} else if (element.item == b) {
				return true;
			}
		}
	}
	return false;
}

void Provider::fillSearchIndex(Element &element) {
	auto strings = QStringList(QFileInfo(element.path).fileName());
	if (const auto media = element.item->media()) {
		if (const auto document = media->document()) {
			strings.append(document->filename());
			strings.append(Ui::Text::FormatDownloadsName(document).text);
		}
	}
	element.words = TextUtilities::PrepareSearchWords(strings.join(' '));
	element.letters.clear();
	for (const auto &word : element.words) {
		element.letters.emplace(word.front());
	}
}

bool Provider::computeIsFound(const Element &element) const {
	if (_typeFilter != TypeFilter::All
		&& _typeFilter != ClassifyFile(element.item, element.path)) {
		return false;
	}

	const auto has = [&](const QString &queryWord) {
		if (!element.letters.contains(queryWord.front())) {
			return false;
		}
		for (const auto &word : element.words) {
			if (word.startsWith(queryWord)) {
				return true;
			}
		}
		return false;
	};
	for (const auto &queryWord : _queryWords) {
		if (!has(queryWord)) {
			return false;
		}
	}
	return true;
}

void Provider::itemRemoved(not_null<const HistoryItem*> item) {
	remove(item);
}

BaseLayout *Provider::getLayout(
		Element element,
		not_null<Overview::Layout::Delegate*> delegate) {
	auto it = _layouts.find(element.item);
	if (it == _layouts.end()) {
		if (auto layout = createLayout(element, delegate)) {
			layout->initDimensions();
			it = _layouts.emplace(element.item, std::move(layout)).first;
		} else {
			return nullptr;
		}
	}
	it->second.stale = false;
	return it->second.item.get();
}

std::unique_ptr<BaseLayout> Provider::createLayout(
		Element element,
		not_null<Overview::Layout::Delegate*> delegate) {
	const auto getFile = [&]() -> DocumentData* {
		if (auto media = element.item->media()) {
			return media->document();
		}
		return nullptr;
	};

	using namespace Overview::Layout;
	const auto &songSt = st::overviewFileLayout;
	if (const auto file = getFile()) {
		auto fields = DocumentFields{
			.document = file,
			.dateOverride = Data::DateFromDownloadDate(element.started),
			.forceFileLayout = true,
		};
		const auto item = element.item;
		auto &manager = Core::App().downloadManager();
		if (manager.loadingExternalState(item).has_value()) {
			fields.externalLoading = [item]()
			-> std::optional<DocumentExternalLoading> {
				auto &manager = Core::App().downloadManager();
				const auto state = manager.loadingExternalState(item);
				if (!state || state->done) {
					return std::nullopt;
				}
				return DocumentExternalLoading{
					.ready = state->ready,
					.total = state->total,
				};
			};
			fields.externalCancel = [item] {
				Core::App().downloadManager().cancelLoadingExternal(item);
			};
		}
		return std::make_unique<Document>(
			delegate,
			element.item,
			std::move(fields),
			songSt);
	}
	return nullptr;
}

ListItemSelectionData Provider::computeSelectionData(
		not_null<const HistoryItem*> item,
		TextSelection selection) {
	auto result = ListItemSelectionData(selection);
	result.canDelete = true;
	result.canForward = item->allowsForward()
		&& (&item->history()->session() == _session.get());
	return result;
}

void Provider::applyDragSelection(
		ListSelectedMap &selected,
		not_null<const HistoryItem*> fromItem,
		bool skipFrom,
		not_null<const HistoryItem*> tillItem,
		bool skipTill) {
	auto from = ranges::find(_elements, fromItem, &Element::item);
	auto till = ranges::find(_elements, tillItem, &Element::item);
	if (from == end(_elements) || till == end(_elements)) {
		return;
	}
	if (skipFrom) {
		++from;
	}
	if (!skipTill) {
		++till;
	}
	if (from >= till) {
		selected.clear();
		return;
	}
	const auto selectLimit = _storiesAddToAlbumId
		? _session->appConfig().storiesAlbumLimit()
		: MaxSelectedItems;
	auto chosen = base::flat_set<not_null<const HistoryItem*>>();
	chosen.reserve(till - from);
	for (auto i = from; i != till; ++i) {
		if (!i->found) {
			continue;
		}
		const auto item = i->item;
		chosen.emplace(item);
		ChangeItemSelection(
			selected,
			item,
			computeSelectionData(item, FullSelection),
			selectLimit);
	}
	if (selected.size() != chosen.size()) {
		for (auto i = begin(selected); i != end(selected);) {
			if (chosen.contains(i->first)) {
				++i;
			} else {
				i = selected.erase(i);
			}
		}
	}
}

bool Provider::allowSaveFileAs(
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document) {
	return false;
}

QString Provider::showInFolderPath(
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document) {
	const auto i = ranges::find(_elements, item, &Element::item);
	return (i != end(_elements)) ? i->path : QString();
}

int64 Provider::scrollTopStatePosition(not_null<HistoryItem*> item) {
	const auto i = ranges::find(_elements, item, &Element::item);
	return (i != end(_elements)) ? i->started : 0;
}

HistoryItem *Provider::scrollTopStateItem(ListScrollTopState state) {
	auto i = ranges::lower_bound(
		_elements,
		state.position,
		ranges::less(),
		&Element::started);
	if (state.position) {
		for (; i != end(_elements); ++i) {
			if (i->found) {
				return i->item.get();
			}
		}
	}
	for (const auto &element : ranges::views::reverse(_elements)) {
		if (element.found) {
			return element.item.get();
		}
	}
	return nullptr;
}

void Provider::saveState(
		not_null<Media::Memento*> memento,
		ListScrollTopState scrollState) {
	if (!_elements.empty() && scrollState.item) {
		memento->setAroundId({ PeerId(), 1 });
		memento->setScrollTopItem(scrollState.item->globalId());
		memento->setScrollTopItemPosition(scrollState.position);
		memento->setScrollTopShift(scrollState.shift);
	}
}

void Provider::restoreState(
		not_null<Media::Memento*> memento,
		Fn<void(ListScrollTopState)> restoreScrollState) {
	if (memento->aroundId() == FullMsgId(PeerId(), 1)) {
		restoreScrollState({
			.position = memento->scrollTopItemPosition(),
			.item = MessageByGlobalId(memento->scrollTopItem()),
			.shift = memento->scrollTopShift(),
		});
		refreshViewer();
	}
}

} // namespace Info::Downloads
