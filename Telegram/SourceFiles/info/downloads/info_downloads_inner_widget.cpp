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
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/search_field_controller.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"
#include "styles/style_media_player.h"

namespace Info::Downloads {
namespace {

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
, _empty(this) {
	_typeButton.create(
		this,
		_typeFilter.value() | rpl::map([](TypeFilter filter) {
			return tr::extras_DownloadsTypeFilter(
				lt_type,
				rpl::single(TypeFilterLabel(filter)));
		}) | rpl::flatten_latest(),
		st::infoProfileButton);
	_typeButton->setObjectName(u"downloads/typeFilter"_q);
	_typeButton->setClickedCallback([=] { showTypeMenu(); });
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

void InnerWidget::showTypeMenu() {
	_typeMenu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	for (const auto filter : {
		TypeFilter::All,
		TypeFilter::Archive,
		TypeFilter::Music,
		TypeFilter::Video,
		TypeFilter::Other,
	}) {
		_typeMenu->addAction(TypeFilterLabel(filter), [=] {
			if (_typeFilter.current() == filter) {
				return;
			}
			setTypeFilter(filter);
			_scrollToRequests.fire({ 0, -1 });
		}, (filter == _typeFilter.current())
			? &st::mediaPlayerMenuCheck
			: nullptr);
	}
	_typeMenu->popup(_typeButton->mapToGlobal(QPoint(0, _typeButton->height())));
}

void InnerWidget::setTypeFilter(TypeFilter filter) {
	if (_typeFilter.current() == filter) {
		return;
	}
	_typeFilter = filter;
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
	_typeButton->resizeToWidth(newWidth);
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
	if (_typeButton) {
		_typeButton->moveToLeft(0, top);
		top += _typeButton->heightNoMargins();
	}
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
