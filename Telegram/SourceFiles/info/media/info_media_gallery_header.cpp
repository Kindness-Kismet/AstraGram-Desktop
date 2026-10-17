/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/media/info_media_gallery_header.h"

#include "info/media/info_media_inner_widget.h"
#include "lang/lang_keys.h"
#include "lang/lang_tag.h"
#include "ui/search_field_controller.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "styles/style_info.h"

namespace Info::Media {

GalleryHeader::GalleryHeader(
	QWidget *parent,
	Type type,
	not_null<InnerWidget*> inner,
	Ui::SearchFieldController *search)
: RpWidget(parent)
, _inner(inner)
, _title(Ui::CreateChild<Ui::FlatLabel>(this, st::infoMediaGalleryTitle))
, _count(Ui::CreateChild<Ui::FlatLabel>(this, st::infoMediaGalleryCount)) {
	SharedMediaTitle(type)(
	) | rpl::on_next([=](const QString &title) {
		_title->setText(title);
		resizeToWidth(width());
	}, lifetime());

	_inner->fullCountValue(
	) | rpl::on_next([=](std::optional<int> count) {
		_count->setText((count && *count > 0)
			? Lang::FormatCountDecimal(*count)
			: QString());
		resizeToWidth(width());
	}, lifetime());

	if (search) {
		setupSearch(search);
	} else if (type == Type::Photo
		|| type == Type::Video
		|| type == Type::PhotoVideo
		|| type == Type::RoundFile) {
		setupZoom();
	}
}

Ui::InputField *GalleryHeader::searchField() const {
	return _field;
}

void GalleryHeader::setupSearch(
		not_null<Ui::SearchFieldController*> search) {
	_field = search->createField(this, st::infoMediaGallerySearch).release();
	_field->customUpDown(true);
	_field->show();

	_cancel = Ui::CreateChild<Ui::CrossButton>(
		this,
		st::infoMediaGallerySearchCancel);
	_cancel->setAccessibleName(tr::lng_sr_clear_search(tr::now));
	_cancel->addClickHandler([=] {
		_field->setText(QString());
	});
	search->queryValue(
	) | rpl::map([](const QString &query) {
		return !query.isEmpty();
	}) | rpl::on_next([=](bool shown) {
		_cancel->toggle(shown, anim::type::normal);
	}, _cancel->lifetime());
	_cancel->finishAnimating();
}

void GalleryHeader::setupZoom() {
	_zoomOut = Ui::CreateChild<Ui::IconButton>(
		this,
		st::infoMediaGalleryZoomOut);
	_zoomOut->setAccessibleName(tr::lng_media_zoom_out(tr::now));
	_zoomOut->setClickedCallback([=] {
		_inner->zoomOut();
	});
	_zoomIn = Ui::CreateChild<Ui::IconButton>(
		this,
		st::infoMediaGalleryZoomIn);
	_zoomIn->setAccessibleName(tr::lng_media_zoom_in(tr::now));
	_zoomIn->setClickedCallback([=] {
		_inner->zoomIn();
	});
	_inner->zoomStateChanges(
	) | rpl::on_next([=] {
		refreshZoom();
	}, lifetime());
	refreshZoom();
}

void GalleryHeader::refreshZoom() {
	const auto refresh = [](
			not_null<Ui::IconButton*> button,
			bool enabled,
			const style::icon &disabled) {
		button->setDisabled(!enabled);
		button->setPointerCursor(enabled);
		button->setIconOverride(
			enabled ? nullptr : &disabled,
			enabled ? nullptr : &disabled);
	};
	refresh(
		_zoomOut,
		_inner->canZoomOut(),
		st::infoMediaGalleryZoomOutDisabled);
	refresh(
		_zoomIn,
		_inner->canZoomIn(),
		st::infoMediaGalleryZoomInDisabled);
}

int GalleryHeader::resizeGetHeight(int newWidth) {
	const auto padding = st::infoMediaGalleryPadding;
	const auto titleCenter = st::infoMediaGalleryHeroTop
		+ st::infoMediaGalleryTitle.style.font->height / 2;
	const auto gap = st::infoMediaGalleryTitleGap;
	auto right = newWidth - padding;
	if (_field) {
		const auto titleWidth = _title->textMaxWidth()
			+ st::infoMediaGalleryCountSkip
			+ _count->textMaxWidth();
		const auto fieldWidth = std::clamp(
			newWidth - 2 * padding - titleWidth - gap,
			st::infoMediaGallerySearchMinWidth,
			st::infoMediaGallerySearchWidth);
		const auto fieldLeft = right - fieldWidth;
		const auto fieldTop = titleCenter - _field->height() / 2;
		_field->setGeometryToLeft(
			fieldLeft,
			fieldTop,
			fieldWidth,
			_field->height(),
			newWidth);
		_cancel->moveToLeft(
			right - _cancel->width(),
			fieldTop + (_field->height() - _cancel->height()) / 2,
			newWidth);
		right = fieldLeft - gap;
	} else if (_zoomIn) {
		// 按钮按图标对齐边距，点击区域向外多出一圈。
		const auto inset = (_zoomIn->width()
			- st::infoMediaGalleryZoomIn.icon.width()) / 2;
		const auto top = titleCenter - _zoomIn->height() / 2;
		_zoomIn->moveToLeft(right + inset - _zoomIn->width(), top, newWidth);
		_zoomOut->moveToLeft(
			_zoomIn->x() - st::infoMediaGalleryZoomSkip - _zoomOut->width(),
			top,
			newWidth);
		right = _zoomOut->x() + inset - gap;
	}
	layoutTitle(right);
	return st::infoMediaGalleryHeroHeight;
}

void GalleryHeader::layoutTitle(int right) {
	const auto padding = st::infoMediaGalleryPadding;
	const auto skip = st::infoMediaGalleryCountSkip;
	const auto available = std::max(right - padding, 0);
	const auto countWidth = std::min(
		_count->textMaxWidth(),
		std::max(available - skip, 0));
	const auto titleWidth = std::min(
		_title->textMaxWidth(),
		std::max(available - (countWidth ? (countWidth + skip) : 0), 0));
	_title->resizeToWidth(titleWidth);
	_title->moveToLeft(padding, st::infoMediaGalleryHeroTop);
	_count->resizeToWidth(countWidth);
	// 数量与标题按基线对齐。
	_count->moveToLeft(
		padding + titleWidth + skip,
		st::infoMediaGalleryHeroTop
			+ st::infoMediaGalleryTitle.style.font->ascent
			- st::infoMediaGalleryCount.style.font->ascent);
}

} // namespace Info::Media
