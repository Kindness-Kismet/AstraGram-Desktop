/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_gif.h"

#include "data/data_document.h"
#include "data/data_file_click_handler.h"
#include "extras/features/message_shot/message_shot.h"
#include "history/history_item.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_element.h"
#include "history/view/media/history_view_media_spoiler.h"
#include "ui/chat/chat_style.h"
#include "ui/painter.h"
#include "styles/style_chat.h"
#include "styles/style_chat_style.h"

#include <QtGui/QPainterPath>
#include <algorithm>

namespace HistoryView {

bool Gif::smallGroupDownloadAvailable() const {
	return _data->isVideoFile()
		&& _realParent->allowsMediaDownloadControls()
		&& !_realParent->isSending()
		&& !_realParent->hasFailed()
		&& !_data->uploading()
		&& !_data->waitingForAlbum()
		&& !_data->forbidsFileSave()
		&& (!_spoiler || _spoiler->revealed)
		&& (_parent->context() != Context::MediaEditor)
		&& !ExtrasFeatures::MessageShot::isTakingShot()
		&& !dataLoaded();
}

QRect Gif::smallGroupDownloadRect(const QRect &geometry) const {
	if (!smallGroupDownloadAvailable()) {
		return {};
	}
	const auto mainSize = st::historyGroupRadialSize;
	const auto size = st::historyGroupDownloadSize;
	const auto offset = mainSize / 2 + st::historyGroupDownloadOffset - size / 2;
	const auto inner = QRect(
		geometry.x() + (geometry.width() - mainSize) / 2 + offset,
		geometry.y() + (geometry.height() - mainSize) / 2 + offset,
		size,
		size);
	const auto padding = st::historyGroupDownloadGap;
	return geometry.contains(inner.marginsAdded(
		QMargins(padding, padding, padding, padding)))
		? inner
		: QRect();
}

void Gif::clipSmallGroupDownload(Painter &p, const QRect &geometry) const {
	const auto inner = smallGroupDownloadRect(geometry);
	if (inner.isEmpty()) {
		return;
	}
	const auto gap = st::historyGroupDownloadGap;
	auto clip = QPainterPath();
	clip.addRect(geometry);
	auto button = QPainterPath();
	button.addEllipse(inner.marginsAdded(QMargins(gap, gap, gap, gap)));
	p.setClipPath(clip.subtracted(button), Qt::IntersectClip);
}

void Gif::drawSmallGroupDownload(
		Painter &p,
		const PaintContext &context,
		const QRect &geometry) const {
	const auto inner = smallGroupDownloadRect(geometry);
	if (inner.isEmpty()) {
		return;
	}
	const auto sti = context.imageStyle();
	const auto hq = PainterHighQualityEnabler(p);
	p.save();
	p.setPen(Qt::NoPen);
	p.setBrush(sti->msgDateImgBg);
	p.drawEllipse(inner);
	const auto &icon = _data->loading()
		? sti->historyVideoCancel
		: sti->historyVideoDownload;
	const auto line = st::historyGroupDownloadRadialLine;
	const auto scale = (inner.width() - 4 * line)
		/ float64(std::max(icon.width(), icon.height()));
	p.save();
	p.translate(inner.x() + inner.width() / 2., inner.y() + inner.height() / 2.);
	p.scale(scale, scale);
	icon.paintInCenter(p, QRect(
		-icon.width() / 2,
		-icon.height() / 2,
		icon.width(),
		icon.height()));
	p.restore();
	if (_animation && _animation->radial.animating()) {
		_animation->radial.draw(
			p,
			inner.marginsRemoved(QMargins(line, line, line, line)),
			line,
			sti->historyFileThumbRadialFg);
	}
	p.restore();
}

TextState Gif::smallGroupDownloadTextState(
		const QRect &geometry,
		QPoint point) const {
	auto result = TextState(_parent);
	if (smallGroupDownloadRect(geometry).contains(point)) {
		result.link = _data->loading() ? _cancell : _savel;
	}
	return result;
}

} // namespace HistoryView
