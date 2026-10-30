#include "extras/features/video_cover/video_cover.h"

#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "window/window_session_controller.h"

namespace ExtrasVideoCover {
namespace {

// 服务器缩略图包装成图片，编号沿用视频编号，下载与缓存沿用缩略图的文件位置。
[[nodiscard]] not_null<PhotoData*> thumbnailPhoto(
		not_null<DocumentData*> document,
		TimeId date) {
	const auto owner = &document->owner();
	const auto id = PhotoId(document->id);
	const auto existing = owner->photo(id);
	if (!existing->isNull()) {
		return existing;
	}
	const auto large = ImageWithLocation{
		.location = document->thumbnailLocation(),
		.bytesCount = document->thumbnailByteSize(),
	};
	return owner->photo(
		id,
		uint64(),
		QByteArray(),
		date,
		document->getDC(),
		false,
		document->inlineThumbnailBytes(),
		ImageWithLocation(),
		ImageWithLocation(),
		large,
		ImageWithLocation(),
		ImageWithLocation(),
		crl::time(0));
}

} // namespace

bool hasCover(
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document) {
	return document->isVideoFile()
		&& (LookupVideoCover(document, item) || document->hasThumbnail());
}

not_null<PhotoData*> photoForCover(
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document) {
	Expects(hasCover(item, document));

	const auto cover = LookupVideoCover(document, item);
	return cover
		? not_null<PhotoData*>(cover)
		: thumbnailPhoto(document, item->date());
}

void showCover(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document) {
	controller->openPhoto(photoForCover(item, document), {
		.id = item->fullId(),
		.topicRootId = item->topicRootId(),
		.monoforumPeerId = item->sublistPeerId(),
		.videoCoverDocument = document.get(),
	});
}

} // namespace ExtrasVideoCover
