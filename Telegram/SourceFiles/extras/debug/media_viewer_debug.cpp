#ifdef _DEBUG
#include "extras/libs/json.hpp"
#include "media/view/media_view_overlay_widget.h"

#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "history/history.h"
#include "history/history_item.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

namespace Media::View {
namespace {

[[nodiscard]] nlohmann::json sizeJson(QSize size) {
	return { size.width(), size.height() };
}

[[nodiscard]] nlohmann::json rectJson(QRect rect) {
	return { rect.x(), rect.y(), rect.width(), rect.height() };
}

} // namespace

QString OverlayWidget::debugState() const {
	const auto state = nlohmann::json{
		{ "visible", !isHidden() },
		{ "messageId", _message ? _message->id.bare : int64(0) },
		{ "peerId", _message ? _message->history()->peer->id.value : uint64(0) },
		{ "photoId", _photo ? _photo->id : uint64(0) },
		{ "documentId", _document ? _document->id : uint64(0) },
		{ "videoCoverDocumentId", _videoCoverDocument ? _videoCoverDocument->id : uint64(0) },
		{ "viewingVideoCovers", _viewingVideoCovers },
		{ "streaming", bool(_streamed) },
		{ "loaded", _photoMedia && _photoMedia->loaded() },
		{ "photoSize", sizeJson(_photo ? QSize(_photo->width(), _photo->height()) : QSize()) },
		{ "imageSize", sizeJson(_photoMedia ? _photoMedia->size(Data::PhotoSize::Large) : QSize()) },
		{ "contentRect", rectJson(finalContentRect()) },
		{ "bodySize", sizeJson(_body->size()) },
		{ "zoom", _zoom },
		{ "zoomToDefault", _zoomToDefault },
		{ "rotation", _rotation },
		{ "left", _leftNavVisible },
		{ "right", _rightNavVisible },
		{ "index", _index ? nlohmann::json(*_index) : nlohmann::json(nullptr) },
		{ "fullIndex", _fullIndex ? nlohmann::json(*_fullIndex) : nlohmann::json(nullptr) },
		{ "fullCount", _fullCount ? nlohmann::json(*_fullCount) : nlohmann::json(nullptr) },
		{ "saveRect", rectJson(_saveNav) },
		{ "rotateRect", rectJson(_rotateNav) },
		{ "moreRect", rectJson(_moreNav) },
		{ "savingRestricted", hasCopyMediaRestriction(true) },
	};
	return QString::fromStdString(state.dump());
}

bool OverlayWidget::debugSaveImage(const QString &path) {
	if (isHidden()
		|| hasCopyMediaRestriction(true)
		|| !_photoMedia
		|| !_photoMedia->loaded()) {
		return false;
	}
	return _photoMedia->saveToFile(path);
}

QSize OverlayWidget::debugCopyImage() {
	if (isHidden()
		|| hasCopyMediaRestriction()
		|| !_photoMedia
		|| !_photoMedia->loaded()) {
		return {};
	}
	const auto clipboard = QGuiApplication::clipboard();
	auto previous = std::make_unique<QMimeData>();
	if (const auto current = clipboard->mimeData()) {
		for (const auto &format : current->formats()) {
			previous->setData(format, current->data(format));
		}
		if (current->hasImage()) {
			previous->setImageData(current->imageData());
		}
	}
	copyMedia();
	const auto size = clipboard->image().size();
	clipboard->setMimeData(previous.release());
	return size;
}

} // namespace Media::View
#endif
