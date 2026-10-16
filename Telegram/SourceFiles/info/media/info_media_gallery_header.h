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

	[[nodiscard]] Ui::InputField *searchField() const;

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void setupSearch(not_null<Ui::SearchFieldController*> search);
	void setupZoom();
	void refreshZoom();
	void layoutTitle(int right);

	const not_null<InnerWidget*> _inner;
	const not_null<Ui::FlatLabel*> _title;
	const not_null<Ui::FlatLabel*> _count;
	Ui::InputField *_field = nullptr;
	Ui::CrossButton *_cancel = nullptr;
	Ui::IconButton *_zoomOut = nullptr;
	Ui::IconButton *_zoomIn = nullptr;

};

} // namespace Info::Media
