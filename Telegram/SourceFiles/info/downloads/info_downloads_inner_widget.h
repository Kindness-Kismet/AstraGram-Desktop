/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "info/downloads/info_downloads_common.h"
#include "ui/widgets/scroll_area.h"
#include "base/unique_qptr.h"

namespace Ui {
class VerticalLayout;
class SearchFieldController;
class SettingsButton;
class PopupMenu;
} // namespace Ui

namespace Info {

class Controller;
struct SelectedItems;
enum class SelectionAction;

namespace Media {
class ListWidget;
} // namespace Media

namespace Downloads {

class Memento;
class EmptyWidget;

class InnerWidget final : public Ui::RpWidget {
public:
	InnerWidget(
		QWidget *parent,
		not_null<Controller*> controller);

	bool showInternal(not_null<Memento*> memento);

	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	void setScrollHeightValue(rpl::producer<int> value);

	rpl::producer<Ui::ScrollToRequest> scrollToRequests() const;
	rpl::producer<SelectedItems> selectedListValue() const;
	void selectionAction(SelectionAction action);

	~InnerWidget();

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

private:
	int recountHeight();
	void refreshHeight();
	void showTypeMenu();
	void setTypeFilter(TypeFilter filter);
	void refreshEmptyText();

	object_ptr<Media::ListWidget> setupList();

	const not_null<Controller*> _controller;

	object_ptr<Media::ListWidget> _list = { nullptr };
	object_ptr<EmptyWidget> _empty;
	object_ptr<Ui::SettingsButton> _typeButton = { nullptr };
	base::unique_qptr<Ui::PopupMenu> _typeMenu;
	rpl::variable<TypeFilter> _typeFilter = TypeFilter::All;
	QString _searchQuery;

	bool _inResize = false;

	rpl::event_stream<Ui::ScrollToRequest> _scrollToRequests;
	rpl::event_stream<rpl::producer<SelectedItems>> _selectedLists;
	rpl::event_stream<rpl::producer<int>> _listTops;

};

} // namespace Downloads
} // namespace Info
