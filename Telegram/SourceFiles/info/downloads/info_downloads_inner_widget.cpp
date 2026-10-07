/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/downloads/info_downloads_inner_widget.h"

#include "info/downloads/info_downloads_widget.h"
#include "info/media/info_media_list_widget.h"
#include "info/info_controller.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/discrete_sliders.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"
#include "styles/style_dialogs.h"

#include <QtGui/QWheelEvent>
#include <array>

namespace Info::Downloads {
namespace {

constexpr auto kTypeFilters = std::array{
	TypeFilter::All,
	TypeFilter::Archive,
	TypeFilter::Music,
	TypeFilter::Video,
	TypeFilter::Other,
};

[[nodiscard]] QString TypeFilterLabel(TypeFilter filter) {
	switch (filter) {
	case TypeFilter::All: return tr::extras_DownloadsTypeAll(tr::now);
	case TypeFilter::Archive: return tr::extras_DownloadsTypeArchive(tr::now);
	case TypeFilter::Music: return tr::extras_DownloadsTypeMusic(tr::now);
	case TypeFilter::Video: return tr::extras_DownloadsTypeVideo(tr::now);
	case TypeFilter::Other: return tr::extras_DownloadsTypeOther(tr::now);
	}
	Unexpected("Invalid download type filter.");
}

} // namespace

class EmptyWidget : public Ui::RpWidget {
public:
	EmptyWidget(QWidget *parent);

	void setFullHeight(rpl::producer<int> fullHeightValue);
	void setFilter(const QString &query, TypeFilter filter);

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private:
	object_ptr<Ui::FlatLabel> _text;
	int _height = 0;

};

EmptyWidget::EmptyWidget(QWidget *parent)
: RpWidget(parent)
, _text(this, st::infoEmptyLabel) {
}

void EmptyWidget::setFullHeight(rpl::producer<int> fullHeightValue) {
	std::move(
		fullHeightValue
	) | rpl::on_next([this](int fullHeight) {
		// Make icon center be on 1/3 height.
		auto iconCenter = fullHeight / 3;
		auto iconHeight = st::infoEmptyFile.height();
		auto iconTop = iconCenter - iconHeight / 2;
		_height = iconTop + st::infoEmptyIconTop;
		resizeToWidth(width());
	}, lifetime());
}

void EmptyWidget::setFilter(const QString &query, TypeFilter filter) {
	_text->setText(filter != TypeFilter::All
		? tr::extras_DownloadsEmptyFilter(tr::now)
		: query.isEmpty()
		? tr::lng_media_file_empty(tr::now)
		: tr::lng_media_file_empty_search(tr::now));
	resizeToWidth(width());
}

int EmptyWidget::resizeGetHeight(int newWidth) {
	auto labelTop = _height - st::infoEmptyLabelTop;
	auto labelWidth = newWidth - 2 * st::infoEmptyLabelSkip;
	_text->resizeToNaturalWidth(labelWidth);

	auto labelLeft = (newWidth - _text->width()) / 2;
	_text->moveToLeft(labelLeft, labelTop, newWidth);

	update();
	return _height;
}

void EmptyWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto iconLeft = (width() - st::infoEmptyFile.width()) / 2;
	const auto iconTop = height() - st::infoEmptyIconTop;
	st::infoEmptyFile.paint(p, iconLeft, iconTop, width());
}

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller)
: RpWidget(parent)
, _controller(controller)
, _empty(this)
, _typeTabsScroll(this, st::dialogsTabsScroll, true) {
	setupTypeTabs();
	_empty->heightValue(
	) | rpl::on_next(
		[this] { refreshHeight(); },
		_empty->lifetime());
	_list = setupList();
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_list, visibleTop, visibleBottom);
}

bool InnerWidget::showInternal(not_null<Memento*> memento) {
	if (memento->section().type() == Section::Type::Downloads) {
		restoreState(memento);
		return true;
	}
	return false;
}

object_ptr<Media::ListWidget> InnerWidget::setupList() {
	auto result = object_ptr<Media::ListWidget>(this, _controller);
	result->heightValue(
	) | rpl::on_next(
		[this] { refreshHeight(); },
		result->lifetime());
	using namespace rpl::mappers;
	result->scrollToRequests(
	) | rpl::map([widget = result.data()](int to) {
		return Ui::ScrollToRequest {
			widget->y() + to,
			-1
		};
	}) | rpl::start_to_stream(
		_scrollToRequests,
		result->lifetime());
	_selectedLists.fire(result->selectedListValue());
	_listTops.fire(result->topValue());
	_controller->searchQueryValue(
	) | rpl::on_next([this](const QString &query) {
		_searchQuery = query;
		refreshEmptyText();
	}, result->lifetime());
	return result;
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	memento->setTypeFilter(_typeFilter.current());
	_list->saveState(&memento->media());
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	setTypeFilter(memento->typeFilter());
	_list->restoreState(&memento->media());
}

void InnerWidget::setupTypeTabs() {
	_typeTabs = _typeTabsScroll->setOwnedWidget(
		object_ptr<Ui::SettingsSlider>(this, st::chatsFiltersTabs));
	_typeTabs->setObjectName(u"downloads/typeTabs"_q);
	_typeTabsScroll->setObjectName(u"downloads/typeTabsScroll"_q);
	rpl::single(rpl::empty) | rpl::then(
		Lang::Updated()
	) | rpl::on_next([=] {
		auto labels = std::vector<QString>();
		for (const auto filter : kTypeFilters) {
			labels.push_back(TypeFilterLabel(filter));
		}
		_typeTabs->setSections(labels);
		_typeTabs->fitWidthToSections();
		scrollToTypeFilter();
	}, _typeTabs->lifetime());
	_typeTabsScroll->resize(width(), _typeTabs->height());
	_typeTabs->sectionActivated() | rpl::on_next([=](int index) {
		const auto filter = kTypeFilters[index];
		if (_typeFilter.current() == filter) {
			return;
		}
		setTypeFilter(filter);
		_scrollToRequests.fire({ 0, -1 });
	}, _typeTabs->lifetime());
	_typeTabsScroll->setCustomWheelProcess([=](not_null<QWheelEvent*> e) {
		const auto pixels = e->pixelDelta();
		const auto angle = e->angleDelta();
		if (pixels.x() || angle.x()) {
			return false;
		}
		const auto delta = pixels.y() ? pixels.y() : angle.y();
		_typeTabsScroll->scrollToX(_typeTabsScroll->scrollLeft() - delta);
		return true;
	});
}

void InnerWidget::scrollToTypeFilter() {
	const auto index = _typeTabs->activeSection();
	_typeTabsScroll->scrollToX(index
		? _typeTabs->centerOfSection(index) - _typeTabsScroll->width() / 2
		: 0);
}

void InnerWidget::setTypeFilter(TypeFilter filter) {
	if (_typeFilter.current() == filter) {
		return;
	}
	_typeFilter = filter;
	const auto index = int(ranges::find(kTypeFilters, filter) - begin(kTypeFilters));
	if (_typeTabs->activeSection() != index) {
		_typeTabs->setActiveSectionFast(index);
	}
	scrollToTypeFilter();
	refreshEmptyText();
	_list->setDownloadsTypeFilter(filter);
}

void InnerWidget::refreshEmptyText() {
	_empty->setFilter(_searchQuery, _typeFilter.current());
}

rpl::producer<SelectedItems> InnerWidget::selectedListValue() const {
	return _selectedLists.events_starting_with(
		_list->selectedListValue()
	) | rpl::flatten_latest();
}

void InnerWidget::selectionAction(SelectionAction action) {
	_list->selectionAction(action);
}

InnerWidget::~InnerWidget() = default;

int InnerWidget::resizeGetHeight(int newWidth) {
	_inResize = true;
	auto guard = gsl::finally([this] { _inResize = false; });

	_list->resizeToWidth(newWidth);
	_empty->resizeToWidth(newWidth);
	_typeTabsScroll->resize(newWidth, _typeTabs->height());
	scrollToTypeFilter();
	return recountHeight();
}

void InnerWidget::refreshHeight() {
	if (_inResize) {
		return;
	}
	resize(width(), recountHeight());
}

int InnerWidget::recountHeight() {
	auto top = 0;
	_typeTabsScroll->moveToLeft(0, top);
	top += _typeTabsScroll->height();
	auto listHeight = 0;
	if (_list) {
		_list->moveToLeft(0, top);
		listHeight = _list->heightNoMargins();
		top += listHeight;
	}
	if (listHeight > 0) {
		_empty->hide();
	} else {
		_empty->show();
		_empty->moveToLeft(0, top);
		top += _empty->heightNoMargins();
	}
	return top;
}

void InnerWidget::setScrollHeightValue(rpl::producer<int> value) {
	using namespace rpl::mappers;
	_empty->setFullHeight(rpl::combine(
		std::move(value),
		_listTops.events_starting_with(
			_list->topValue()
		) | rpl::flatten_latest(),
		_1 - _2));
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

} // namespace Info::Downloads
