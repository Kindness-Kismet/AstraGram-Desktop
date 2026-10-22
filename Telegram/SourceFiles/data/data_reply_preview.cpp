/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_reply_preview.h"

#include "data/data_file_origin.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "main/main_session.h"
#include "ui/image/image.h"
#include "styles/style_chat.h"

namespace Data {

ReplyPreview::ReplyPreview(not_null<DocumentData*> document)
: _document(document) {
}

ReplyPreview::ReplyPreview(not_null<PhotoData*> photo)
: _photo(photo) {
}

ReplyPreview::~ReplyPreview() = default;

bool ReplyPreview::prepare(
		not_null<Image*> image,
		Images::Options options,
		bool spoiler) {
	using namespace Images;
	if (image->isNull()) {
		return false;
	}
	int w = image->width(), h = image->height();
	if (w <= 0) w = 1;
	if (h <= 0) h = 1;
	auto thumbSize = (w > h)
		? QSize(
			w * st::historyReplyPreview / h,
			st::historyReplyPreview)
		: QSize(
			st::historyReplyPreview,
			h * st::historyReplyPreview / w);
	thumbSize *= style::DevicePixelRatio();
	options |= Option::TransparentBackground;
	auto outerSize = st::historyReplyPreview;
	auto original = spoiler
		? image->original().scaled(
			{ 40, 40 },
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation)
		: image->original();
	auto prepared = Prepare(std::move(original), thumbSize, {
		.options = options | (spoiler ? Option::Blur : Option()),
		.outer = { outerSize, outerSize },
	});
	(spoiler ? _spoilered : _regular) = std::make_unique<Image>(
		std::move(prepared));
	_good = spoiler || ((options & Option::Blur) == 0);
	return _good;
}

bool ReplyPreview::preparePhoto(bool spoiler) {
	using Size = PhotoSize;
	if (spoiler) {
		if (const auto blurred = _photoMedia->thumbnailInline()) {
			return prepare(blurred, {}, true);
		}
	}
	if (const auto small = _photoMedia->image(Size::Small)) {
		return prepare(small, {}, spoiler);
	}
	if (const auto large = _photoMedia->image(Size::Large)) {
		return prepare(large, {}, spoiler);
	}
	if (!spoiler && !_regular) {
		if (const auto blurred = _photoMedia->thumbnailInline()) {
			prepare(blurred, Images::Option::Blur);
		}
	}
	return false;
}

void ReplyPreview::refreshPhoto() {
	// wanted() 可能同步完成加载，并在回调中释放媒体。
	if (!_photoMedia) {
		return;
	}
	if (_requestedRegular && !_checkedRegular) {
		_checkedRegular = preparePhoto(false);
	}
	if (_requestedSpoilered && !_checkedSpoilered) {
		_checkedSpoilered = preparePhoto(true);
	}
	if ((!_requestedRegular || _checkedRegular)
		&& (!_requestedSpoilered || _checkedSpoilered)) {
		_photoLifetime.destroy();
		_photoMedia = nullptr;
	} else if (!_photoLifetime) {
		// 离开聊天后也要生成小预览，避免继续持有整张照片。
		_photo->session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			refreshPhoto();
		}, _photoLifetime);
	}
}

Image *ReplyPreview::image(
		Data::FileOrigin origin,
		not_null<PeerData*> context,
		bool spoiler) {
	auto &image = spoiler ? _spoilered : _regular;
	auto &checked = spoiler ? _checkedSpoilered : _checkedRegular;
	if (checked) {
		return image.get();
	} else if (_document) {
		if (!image || (!_good && _document->hasThumbnail())) {
			if (!_documentMedia) {
				_documentMedia = _document->createMediaView();
				_documentMedia->thumbnailWanted(origin);
			}
			const auto thumbnail = _documentMedia->thumbnail();
			const auto option = _document->isVideoMessage()
				? Images::Option::RoundCircle
				: Images::Option::None;
			if (spoiler) {
				if (const auto image = _documentMedia->thumbnailInline()) {
					prepare(image, option, true);
				} else if (thumbnail) {
					prepare(thumbnail, option, true);
				}
			} else if (thumbnail) {
				prepare(thumbnail, option);
			} else if (!image) {
				if (const auto image = _documentMedia->thumbnailInline()) {
					prepare(image, option | Images::Option::Blur);
				}
			}
			if (_good || !_document->hasThumbnail()) {
				checked = true;
				_documentMedia = nullptr;
			}
		}
	} else {
		Assert(_photo != nullptr);
		(spoiler ? _requestedSpoilered : _requestedRegular) = true;
		const auto inlineThumbnailBytes = _photo->inlineThumbnailBytes();
		if (!_photoMedia) {
			_photoMedia = _photo->createMediaView();
		}
		const auto media = _photoMedia;
		const auto loadThumbnail = inlineThumbnailBytes.isEmpty()
			|| (!spoiler && media->autoLoadThumbnailAllowed(context));
		if (loadThumbnail) {
			media->wanted(PhotoSize::Small, origin);
		}
		refreshPhoto();
	}
	return image.get();
}

bool ReplyPreview::loaded(bool spoiler) const {
	return spoiler ? _checkedSpoilered : _checkedRegular;
}

} // namespace Data
