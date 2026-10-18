/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/media/info_media_widget.h"
#include "ui/rp_widget.h"

namespace Ui {
class FlatLabel;
class InputField;
class CrossButton;
class IconButton;
class SearchFieldController;
class ContentSearchRow;
} // namespace Ui

namespace Info::Media {

class InnerWidget;

// 共享媒体画廊页头：类型名与总数在左，搜索框或缩放按钮在右。
class GalleryHeader final : public Ui::RpWidget {
public:
	GalleryHeader(
		QWidget *parent,
		Type type,
		not_null<InnerWidget*> inner,
		Ui::SearchFieldController *search);
	GalleryHeader(
		QWidget *parent,
		rpl::producer<QString> title,
		rpl::producer<std::optional<int>> count,
		Ui::SearchFieldController *search);

	[[nodiscard]] Ui::InputField *searchField() const;
	void setContentSearch(bool enabled);
	void showSearch();

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void setupSearch(not_null<Ui::SearchFieldController*> search);
	void setupZoom(not_null<InnerWidget*> inner);
	void refreshZoom(not_null<InnerWidget*> inner);
	void layoutTitle(int right);

	const not_null<Ui::FlatLabel*> _title;
	const not_null<Ui::FlatLabel*> _count;
	Ui::InputField *_field = nullptr;
	Ui::CrossButton *_cancel = nullptr;
	Ui::IconButton *_zoomOut = nullptr;
	Ui::IconButton *_zoomIn = nullptr;
	Ui::IconButton *_searchOpen = nullptr;
	Ui::ContentSearchRow *_searchRow = nullptr;
	Ui::SearchFieldController *_search = nullptr;

};

} // namespace Info::Media
