/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "overview/overview_layout.h"

#include "overview/overview_layout_delegate.h"
#include "boxes/sticker_set_box.h"
#include "data/data_document.h"
#include "data/data_document_resolver.h"
#include "data/data_photo_media.h"
#include "data/data_document_media.h"
#include "data/data_file_click_handler.h"
#include "history/history_item.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/media/history_view_media_common.h"
#include "media/media_common.h"
#include "lang/lang_keys.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/image/image.h"
#include "ui/text/format_values.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_chat_style.h"
#include "styles/style_overview.h"

namespace Overview::Layout {
namespace {

using TextState = HistoryView::TextState;
using ::Media::ValidFrameSize;

constexpr auto kMaxInlineArea = 1280 * 720;
constexpr auto kStoryRatio = 1.46;

[[nodiscard]] bool CanPlayInline(not_null<DocumentData*> document) {
	return ValidFrameSize(document->dimensions, kMaxInlineArea);
}

[[nodiscard]] QSize FitMediaSize(QSize original, QSize outer, int inset) {
	const auto available = QSize(
		std::max(outer.width() - 2 * inset, 1),
		std::max(outer.height() - 2 * inset, 1));
	if (original.isEmpty()) {
		return available;
	}
	return original.scaled(available, Qt::KeepAspectRatio).expandedTo({ 1, 1 });
}

[[nodiscard]] QImage FitMediaFrame(
		QImage image,
		QSize outer,
		int inset,
		int ratio,
		bool blur = false) {
	const auto size = FitMediaSize(image.size(), outer * ratio, inset * ratio);
	const auto blurFirst = blur
		&& (int64(image.width()) * image.height()
			<= int64(size.width()) * size.height());
	if (blurFirst) {
		image = Images::Blur(std::move(image));
	}
	image = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	if (blur && !blurFirst) {
		image = Images::Blur(std::move(image));
	}
	image.setDevicePixelRatio(1.);
	auto result = QImage(outer * ratio, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	{
		auto painter = QPainter(&result);
		painter.drawImage(
			QPoint((result.width() - size.width()) / 2,
				(result.height() - size.height()) / 2),
			image);
	}
	result.setDevicePixelRatio(ratio);
	return result;
}

[[nodiscard]] QImage CropMediaFrame(QImage image, int width, int height) {
	const auto ratio = style::DevicePixelRatio();
	width *= ratio;
	height *= ratio;
	if (image.width() == width && image.height() == height) {
		image.setDevicePixelRatio(ratio);
		return image;
	}
	// 选择占用更小的缩放与裁剪顺序，限制大图和极端长宽比的中间图尺寸。
	const auto wide = (image.width() * height > image.height() * width);
	const auto cropWidth = wide
		? ((image.height() * width) / height)
		: image.width();
	const auto cropHeight = wide
		? image.height()
		: ((image.width() * height) / width);
	const auto expanded = image.size().scaled(
		width,
		height,
		Qt::KeepAspectRatioByExpanding);
	const auto cropFirst = (int64(cropWidth) * cropHeight)
		< (int64(expanded.width()) * expanded.height());
	auto result = QImage();
	if (cropFirst) {
		result = image.copy(
			(image.width() - cropWidth) / 2,
			(image.height() - cropHeight) / 2,
			cropWidth,
			cropHeight
		).scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	} else {
		result = image.scaled(
			width,
			height,
			Qt::KeepAspectRatioByExpanding,
			Qt::SmoothTransformation);
		if (result.width() != width || result.height() != height) {
			result = result.copy(
				(result.width() - width) / 2,
				(result.height() - height) / 2,
				width,
				height);
		}
	}
	result.setDevicePixelRatio(ratio);
	return result;
}

void PaintSensitiveTag(Painter &p, QRect r) {
	auto text = Ui::Text::String();
	text.setText(
		st::semiboldTextStyle,
		tr::lng_sensitive_tag(tr::now));
	const auto width = text.maxWidth();
	const auto inner = QRect(0, 0, width, text.minHeight());
	const auto outer = style::centerrect(r, inner.marginsAdded(st::paidTagPadding));
	const auto size = outer.size();
	const auto radius = std::min(size.width(), size.height()) / 2;
	auto hq = PainterHighQualityEnabler(p);

	p.setPen(Qt::NoPen);
	p.setBrush(st::radialBg);
	p.drawRoundedRect(outer, radius, radius);
	p.setPen(st::radialFg);
	text.draw(p, {
		.position = outer.marginsRemoved(st::paidTagPadding).topLeft(),
	});
}

} // namespace

Photo::Photo(
	not_null<Delegate*> delegate,
	not_null<HistoryItem*> parent,
	not_null<PhotoData*> photo,
	MediaOptions options)
: ItemBase(delegate, parent)
, _data(photo)
, _spoiler((options.spoiler || parent->isMediaSensitive())
	? std::make_unique<Ui::SpoilerAnimation>([=] {
		delegate->repaintItem(this);
	})
	: nullptr)
, _sensitiveSpoiler(parent->isMediaSensitive() ? 1 : 0)
, _story(options.story)
, _storyPinned(options.storyPinned)
, _storyShowPinned(options.storyShowPinned)
, _storyHidden(options.storyHidden)
, _storyShowHidden(options.storyShowHidden)
, _link(_sensitiveSpoiler
	? HistoryView::MakeSensitiveMediaLink(
		std::make_shared<LambdaClickHandler>(crl::guard(this, [=] {
			maybeClearSensitiveSpoiler();
		})),
		parent)
	: _spoiler
	? std::make_shared<LambdaClickHandler>(crl::guard(this, [=] {
		clearSpoiler();
		_link = makeOpenPhotoHandler();
	}))
	: makeOpenPhotoHandler()) {
	if (_data->inlineThumbnailBytes().isEmpty()
		&& (_data->hasExact(Data::PhotoSize::Small)
			|| _data->hasExact(Data::PhotoSize::Thumbnail))) {
		_data->load(Data::PhotoSize::Small, parent->fullId());
	}
}

Photo::~Photo() = default;

ClickHandlerPtr Photo::makeOpenPhotoHandler() {
	return std::make_shared<PhotoOpenClickHandler>(
		_data,
		crl::guard(this, [=](FullMsgId id) {
			clearSpoiler();
			delegate()->openPhoto(_data, id);
		}),
		parent()->fullId());
}

void Photo::initDimensions() {
	_maxw = 2 * st::overviewPhotoMinSize;
	_minh = _story ? int(base::SafeRound(_maxw * kStoryRatio)) : _maxw;
}

int32 Photo::resizeGetHeight(int32 width) {
	width = std::min(width, _maxw);
	if (_width != width) {
		_width = width;
		_height = _story
			? int(base::SafeRound(_width * kStoryRatio))
			: _width;
	}
	return _height;
}

void Photo::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) {
	const auto selected = (selection == FullSelection);
	const auto inset = _story ? 0 : context->mediaInset;
	const auto size = QSize(_width, _height) * style::DevicePixelRatio();
	if (_cacheSize != size || _mediaInset != inset) {
		_cacheSize = size;
		_mediaInset = inset;
		++_goodRequestId;
		_goodLoaded = false;
		_goodRequested = false;
		_pix = QImage();
	}
	if (!_goodLoaded) {
		ensureDataMediaCreated();
		if (!_spoiler) {
			requestGoodPix();
		}
		if (_pix.isNull()) {
			const auto small = _spoiler
				? nullptr
				: _dataMedia->image(Data::PhotoSize::Small);
			if (const auto image = small ? small : _dataMedia->thumbnailInline()) {
				setPixFrom(image);
			}
		}
	}

	p.fillRect(0, 0, _width, _height, st::overviewPhotoBg);
	if (!_pix.isNull()) {
		p.drawImage(0, 0, _pix);
	}

	if (_spoiler) {
		const auto paused = context->paused || On(PowerSaving::kChatSpoiler);
		Ui::FillSpoilerRect(
			p,
			QRect(0, 0, _width, _height),
			Ui::DefaultImageSpoiler().frame(
				_spoiler->index(context->ms, paused)));

		if (_sensitiveSpoiler) {
			PaintSensitiveTag(p, QRect(0, 0, _width, _height));
		}
	}

	if (_storyHidden) {
		delegate()->hiddenMark()->paint(
			p,
			_pix,
			_hiddenBgCache,
			QPoint(),
			QSize(_width, _height),
			_width);
	}

	if (selected) {
		p.fillRect(0, 0, _width, _height, st::overviewPhotoSelectOverlay);
	}

	if (_storyPinned) {
		const auto &icon = selected
			? st::storyPinnedIconSelected
			: st::storyPinnedIcon;
		icon.paint(p, _width - icon.width(), 0, _width);
	}

	const auto checkDelta = st::overviewCheckSkip + st::overviewCheck.size;
	const auto checkLeft = _width - checkDelta;
	const auto checkTop = _height - checkDelta;
	paintCheckbox(p, { checkLeft, checkTop }, selected, context);
}

bool Photo::elementsAnimating() const {
	return (_spoiler != nullptr);
}

void Photo::requestGoodPix() {
	Expects(_dataMedia != nullptr);

	if (_goodRequested || !_width || !_height) {
		return;
	}
	const auto image = _dataMedia->image(Data::PhotoSize::Large)
		? _dataMedia->image(Data::PhotoSize::Large)
		: _dataMedia->image(Data::PhotoSize::Thumbnail);
	if (!image) {
		return;
	}
	_goodRequested = true;

	const auto width = _width;
	const auto height = _height;
	const auto inset = _mediaInset;
	const auto ratio = style::DevicePixelRatio();
	const auto story = _story;
	const auto id = ++_goodRequestId;
	const auto weak = base::make_weak(this);
	crl::async([=, original = image->original()]() mutable {
		auto result = story
			? CropMediaFrame(std::move(original), width, height)
			: FitMediaFrame(std::move(original), { width, height }, inset, ratio);
		crl::on_main(weak, [=, result = std::move(result)]() mutable {
			weak->goodPixReady(std::move(result), id);
		});
	});
}

void Photo::goodPixReady(QImage image, uint32 id) {
	if (id != _goodRequestId) {
		return;
	}
	_goodRequested = false;
	_pix = std::move(image);
	_goodLoaded = true;
	delegate()->repaintItem(this);
}

void Photo::setPixFrom(not_null<Image*> image) {
	Expects(_width > 0 && _height > 0);

	if (_story) {
		auto imageData = image->original();
		const auto pixels = _cacheSize.width() * int64(_cacheSize.height());
		const auto blurFirst = imageData.width() * int64(imageData.height()) <= pixels;
		if (blurFirst) {
			imageData = Images::Blur(std::move(imageData));
		}
		imageData = CropMediaFrame(std::move(imageData), _width, _height);
		_pix = blurFirst ? std::move(imageData) : Images::Blur(std::move(imageData));
	} else {
		_pix = FitMediaFrame(
			image->original(),
			{ _width, _height },
			_mediaInset,
			style::DevicePixelRatio(),
			true);
	}

	// In case we have inline thumbnail we can unload all images and we still
	// won't get a blank image in the media viewer when the photo is opened.
	if (!_data->inlineThumbnailBytes().isEmpty()
		&& !delegate()->keepPhotoMediaLoaded()) {
		_dataMedia = nullptr;
		delegate()->unregisterHeavyItem(this);
	}
}

void Photo::ensureDataMediaCreated() const {
	if (_dataMedia) {
		return;
	}
	_dataMedia = _data->createMediaView();
	if (_data->inlineThumbnailBytes().isEmpty()) {
		_dataMedia->wanted(Data::PhotoSize::Small, parent()->fullId());
	}
	_dataMedia->wanted(Data::PhotoSize::Thumbnail, parent()->fullId());
	delegate()->registerHeavyItem(this);
}

void Photo::clearSpoiler() {
	if (_spoiler) {
		_spoiler = nullptr;
		_sensitiveSpoiler = false;
		_pix = QImage();
		delegate()->repaintItem(this);
	}
}

void Photo::maybeClearSensitiveSpoiler() {
	if (_sensitiveSpoiler) {
		clearSpoiler();
		_link = makeOpenPhotoHandler();
	}
}

void Photo::itemDataChanged() {
	const auto pinned = _storyShowPinned && parent()->isPinned();
	const auto hidden = _storyShowHidden && !parent()->storyInProfile();
	if (_storyPinned != pinned || _storyHidden != hidden) {
		_storyPinned = pinned;
		_storyHidden = hidden;
		delegate()->repaintItem(this);
	}
}

void Photo::clearHeavyPart() {
	_dataMedia = nullptr;
}

TextState Photo::getState(
		QPoint point,
		StateRequest request) const {
	if (hasPoint(point)) {
		return { parent(), _link };
	}
	return {};
}

Video::Video(
	not_null<Delegate*> delegate,
	not_null<HistoryItem*> parent,
	not_null<DocumentData*> video,
	MediaOptions options)
: RadialProgressItem(delegate, parent)
, _data(video)
, _videoCover(LookupVideoCover(video, parent))
, _duration(Ui::FormatDurationText(_data->duration() / 1000))
, _durationw(st::normalFont->width(_duration))
, _spoiler((options.spoiler || parent->isMediaSensitive())
	? std::make_unique<Ui::SpoilerAnimation>([=] {
		delegate->repaintItem(this);
	})
	: nullptr)
, _sensitiveSpoiler(parent->isMediaSensitive() ? 1 : 0)
, _story(options.story)
, _storyPinned(options.storyPinned)
, _storyShowPinned(options.storyShowPinned)
, _storyHidden(options.storyHidden)
, _storyShowHidden(options.storyShowHidden) {
	setDocumentLinks(_data);
	if (_sensitiveSpoiler) {
		_openl = HistoryView::MakeSensitiveMediaLink(
			std::make_shared<LambdaClickHandler>(crl::guard(this, [=] {
				clearSpoiler();
				setDocumentLinks(_data);
			})),
			parent);
	} else if (_spoiler) {
		_openl = std::make_shared<LambdaClickHandler>(crl::guard(this, [=] {
			clearSpoiler();
			setDocumentLinks(_data);
		}));
	}
	if (!_videoCover) {
		_data->loadThumbnail(parent->fullId());
	} else if (_videoCover->inlineThumbnailBytes().isEmpty()
		&& (_videoCover->hasExact(Data::PhotoSize::Small)
			|| _videoCover->hasExact(Data::PhotoSize::Thumbnail))) {
		_videoCover->load(Data::PhotoSize::Small, parent->fullId());
	}
}

Video::~Video() = default;

void Video::initDimensions() {
	_maxw = 2 * st::overviewPhotoMinSize;
	_minh = _story ? int(base::SafeRound(_maxw * kStoryRatio)) : _maxw;
}

int32 Video::resizeGetHeight(int32 width) {
	width = std::min(width, _maxw);
	if (_width != width) {
		_width = width;
		_height = _story
			? int(base::SafeRound(_width * kStoryRatio))
			: _width;
	}
	return _height;
}

void Video::paint(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		const PaintContext *context) {
	ensureDataMediaCreated();

	const auto selected = (selection == FullSelection);
	const auto blurred = _videoCover
		? _videoCoverMedia->thumbnailInline()
		: _dataMedia->thumbnailInline();
	const auto thumbnail = _spoiler
		? nullptr
		: _videoCover
		? _videoCoverMedia->image(Data::PhotoSize::Small)
		: _dataMedia->thumbnail();
	const auto good = _spoiler
		? nullptr
		: _videoCover
		? _videoCoverMedia->image(Data::PhotoSize::Large)
		: _dataMedia->goodThumbnail();

	bool loaded = dataLoaded(), displayLoading = _data->displayLoading();
	if (displayLoading) {
		ensureRadial();
		if (!_radial->animating()) {
			_radial->start(dataProgress());
		}
	}
	updateStatusText();
	const auto radial = isRadialAnimation();
	const auto radialOpacity = radial ? _radial->opacity() : 0.;

	const auto widthChanged = (_pix.width()
		!= _width * style::DevicePixelRatio());
	if (widthChanged) {
		_pix = QImage();
		_pixBlurred = true;
		_goodRequested = false;
	}
	const auto better = good ? good : thumbnail;
	if (_pix.isNull()) {
		if (const auto source = blurred ? blurred : better) {
			auto img = (source == blurred)
				? Images::Blur(source->original())
				: source->original();
			_pix = CropMediaFrame(std::move(img), _width, _height);
			_pixBlurred = (source == blurred);
		}
	}
	if (better && _pixBlurred) {
		requestGoodPix(better);
	}

	if (_pix.isNull()) {
		p.fillRect(0, 0, _width, _height, st::overviewPhotoBg);
	} else {
		p.drawImage(0, 0, _pix);
	}

	if (_spoiler) {
		const auto paused = context->paused || On(PowerSaving::kChatSpoiler);
		Ui::FillSpoilerRect(
			p,
			QRect(0, 0, _width, _height),
			Ui::DefaultImageSpoiler().frame(
				_spoiler->index(context->ms, paused)));

		if (_sensitiveSpoiler) {
			PaintSensitiveTag(p, QRect(0, 0, _width, _height));
		}
	}

	if (_storyHidden) {
		delegate()->hiddenMark()->paint(
			p,
			_pix,
			_hiddenBgCache,
			QPoint(),
			QSize(_width, _height),
			_width);
	}

	if (selected) {
		p.fillRect(QRect(0, 0, _width, _height), st::overviewPhotoSelectOverlay);
	}

	if (_storyPinned) {
		const auto &icon = selected
			? st::storyPinnedIconSelected
			: st::storyPinnedIcon;
		icon.paint(p, _width - icon.width(), 0, _width);
	}

	if (!selected && !context->selecting && radialOpacity < 1.) {
		if (clip.intersects(QRect(0, _height - st::normalFont->height, _width, st::normalFont->height))) {
			const auto download = !loaded && !_dataMedia->canBePlayed();
			const auto &icon = download
				? (selected ? st::overviewVideoDownloadSelected : st::overviewVideoDownload)
				: (selected ? st::overviewVideoPlaySelected : st::overviewVideoPlay);
			const auto text = download ? _status.text() : _duration;
			const auto textw = download
				? st::normalFont->width(text)
				: _durationw;
			const auto margin = st::overviewVideoStatusMargin;
			const auto padding = st::overviewVideoStatusPadding;
			const auto statusX = margin + padding.x(), statusY = _height - margin - padding.y() - st::normalFont->height;
			const auto fullW = icon.width()
				+ 3 * padding.x()
				+ textw;
			const auto showText = (fullW <= _width - 2 * margin);
			const auto statusW = showText
				? fullW
				: (icon.width() + 2 * padding.x());
			const auto statusH = st::normalFont->height + 2 * padding.y();
			p.setOpacity(1. - radialOpacity);
			Ui::FillRoundRect(p, statusX - padding.x(), statusY - padding.y(), statusW, statusH, selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? Ui::OverviewVideoSelectedCorners : Ui::OverviewVideoCorners);
			p.setFont(st::normalFont);
			p.setPen(st::msgDateImgFg);
			icon.paint(p, statusX, statusY + (st::normalFont->height - icon.height()) / 2, _width);
			if (showText) {
				p.drawTextLeft(statusX + icon.width() + padding.x(), statusY, _width, text, statusW - 2 * padding.x());
			}
		}
	}

	QRect inner((_width - st::overviewVideoRadialSize) / 2, (_height - st::overviewVideoRadialSize) / 2, st::overviewVideoRadialSize, st::overviewVideoRadialSize);
	if (radial && clip.intersects(Ui::MediaButtonPaintRect(inner))) {
		p.setOpacity(radialOpacity);
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(st::msgDateImgBgSelected);
		} else {
			auto over = ClickHandler::showAsActive((_data->loading() || _data->uploading()) ? _cancell : (loaded || _dataMedia->canBePlayed()) ? _openl : _savel);
			p.setBrush(anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, _a_iconOver.value(over ? 1. : 0.)));
		}

		_radial->paint(p, inner, Ui::MediaButtonPalette(
			st::historyFileThumbRadialFg->c, p.brush().color(), true),
			_data->loading() || _data->uploading(),
			ClickHandler::showAsActive(_cancell));
	}
	p.setOpacity(1);

	const auto checkDelta = st::overviewCheckSkip + st::overviewCheck.size;
	const auto checkLeft = _width - checkDelta;
	const auto checkTop = _height - checkDelta;
	paintCheckbox(p, { checkLeft, checkTop }, selected, context);
}

bool Video::elementsAnimating() const {
	return RadialProgressItem::elementsAnimating() || (_spoiler != nullptr);
}

void Video::requestGoodPix(not_null<Image*> image) {
	if (_goodRequested || !_width || !_height) {
		return;
	}
	_goodRequested = true;

	const auto width = _width;
	const auto height = _height;
	const auto id = ++_goodRequestId;
	const auto weak = base::make_weak(this);
	crl::async([=, original = image->original()]() mutable {
		auto result = CropMediaFrame(std::move(original), width, height);
		crl::on_main(weak, [=, result = std::move(result)]() mutable {
			weak->goodPixReady(std::move(result), id);
		});
	});
}

void Video::goodPixReady(QImage image, uint32 id) {
	if (id != _goodRequestId) {
		return;
	}
	_goodRequested = false;
	_pix = std::move(image);
	_pixBlurred = false;
	delegate()->repaintItem(this);
}

void Video::ensureDataMediaCreated() const {
	if (_dataMedia && (!_videoCover || _videoCoverMedia)) {
		return;
	}
	_dataMedia = _data->createMediaView();
	if (_videoCover) {
		_videoCoverMedia = _videoCover->createMediaView();
		_videoCover->load(Data::PhotoSize::Large, parent()->fullId());
	} else {
		_dataMedia->goodThumbnailWanted();
		_dataMedia->thumbnailWanted(parent()->fullId());
	}
	delegate()->registerHeavyItem(this);
}

void Video::clearSpoiler() {
	if (_spoiler) {
		_spoiler = nullptr;
		_sensitiveSpoiler = false;
		_pix = QImage();
		delegate()->repaintItem(this);
	}
}

void Video::maybeClearSensitiveSpoiler() {
	if (_sensitiveSpoiler) {
		clearSpoiler();
		setDocumentLinks(_data);
	}
}

void Video::itemDataChanged() {
	const auto pinned = _storyShowPinned && parent()->isPinned();
	const auto hidden = _storyShowHidden && !parent()->storyInProfile();
	if (_storyPinned != pinned || _storyHidden != hidden) {
		_storyPinned = pinned;
		_storyHidden = hidden;
		delegate()->repaintItem(this);
	}
}

void Video::clearHeavyPart() {
	_dataMedia = nullptr;
	_videoCoverMedia = nullptr;
}

float64 Video::dataProgress() const {
	ensureDataMediaCreated();
	return _dataMedia->progress();
}

bool Video::dataFinished() const {
	return !_data->loading()
		&& (!_data->uploading() || _data->waitingForAlbum());
}

bool Video::dataLoaded() const {
	ensureDataMediaCreated();
	return _dataMedia->loaded();
}

bool Video::iconAnimated() const {
	return true;
}

TextState Video::getState(
		QPoint point,
		StateRequest request) const {
	if (hasPoint(point)) {
		ensureDataMediaCreated();
		auto link = _sensitiveSpoiler
			? _openl
			: (_data->loading() || _data->uploading())
			? _cancell
			: (dataLoaded() || _dataMedia->canBePlayed())
			? _openl
			: _savel;
		const auto size = st::overviewVideoRadialSize;
		const auto button = QRect((_width - size) / 2, (_height - size) / 2, size, size);
		if (link == _cancell && !Ui::MediaCancelBadgeContains(button, point)) {
			link = nullptr;
		}
		return { parent(), link };
	}
	return {};
}

void Video::updateStatusText() {
	auto statusSize = int64();
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = Ui::FileStatusSizeFailed;
	} else if (_data->uploading()) {
		statusSize = _data->uploadingData->offset;
	} else if (dataLoaded()) {
		statusSize = Ui::FileStatusSizeLoaded;
	} else {
		statusSize = Ui::FileStatusSizeReady;
	}
	if (statusSize != _status.size()) {
		auto status = statusSize;
		auto size = _data->size;
		if (statusSize >= 0 && statusSize < 0xFF000000LL) {
			size = status;
			status = Ui::FileStatusSizeReady;
		}
		_status.update(status, size, -1, 0);
		_status.setSize(statusSize);
	}
}

// Copied from inline_bot_layout_internal.
Gif::Gif(
	not_null<Delegate*> delegate,
	not_null<HistoryItem*> parent,
	not_null<DocumentData*> gif)
: RadialProgressItem(delegate, parent)
, _data(gif)
, _spoiler(parent->isMediaSensitive()
	? std::make_unique<Ui::SpoilerAnimation>([=] {
		delegate->repaintItem(this);
	})
	: nullptr)
, _sensitiveSpoiler(parent->isMediaSensitive() ? 1 : 0) {
	setDocumentLinks(_data, true);
	if (_sensitiveSpoiler) {
		_openl = HistoryView::MakeSensitiveMediaLink(
			std::make_shared<LambdaClickHandler>(crl::guard(this, [=] {
				clearSpoiler();
				setDocumentLinks(_data, true);
			})),
			parent);
	}
	_data->loadThumbnail(parent->fullId());
}

Gif::~Gif() = default;

int Gif::contentWidth() const {
	if (_data->dimensions.width() > 0) {
		return _data->dimensions.width();
	}
	return style::ConvertScale(_data->thumbnailLocation().width());
}

int Gif::contentHeight() const {
	if (_data->dimensions.height() > 0) {
		return _data->dimensions.height();
	}
	return style::ConvertScale(_data->thumbnailLocation().height());
}

void Gif::initDimensions() {
	int32 w = contentWidth(), h = contentHeight();
	if (w <= 0 || h <= 0) {
		_maxw = 0;
	} else {
		w = w * st::inlineMediaHeight / h;
		_maxw = std::max(w, int32(st::inlineResultsMinWidth));
	}
	_minh = st::inlineMediaHeight + st::inlineResultsSkip;
}

int32 Gif::resizeGetHeight(int32 width) {
	_width = width;
	_height = _minh;
	return _height;
}

QSize Gif::countFrameSize() const {
	const auto ready = _gif && _gif->ready();
	const auto original = ready
		? QSize(_gif->width(), _gif->height())
		: QSize(contentWidth(), contentHeight());
	return FitMediaSize(original, { _width, st::inlineMediaHeight }, _mediaInset);
}

void Gif::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case Notification::Reinit: {
		if (_gif) {
			if (_gif->state() == State::Error) {
				_gif.setBad();
			} else if (_gif->ready() && !_gif->started()) {
				const auto size = QSize(_gif->width(), _gif->height());
				if (!ValidFrameSize(size, kMaxInlineArea)) {
					_inlineOverCap = true;
					_gif.reset();
				} else {
					_gif->start({
						.frame = countFrameSize(),
						.keepAlpha = true,
					});
				}
			} else if (_gif->autoPausedGif()
					&& !delegate()->itemVisible(this)) {
				clearHeavyPart();
			}
		}

		update();
	} break;

	case Notification::Repaint: {
		if (_gif && !_gif->currentDisplayed()) {
			update();
		}
	} break;
	}
}

void Gif::clearSpoiler() {
	if (_spoiler) {
		_spoiler = nullptr;
		_sensitiveSpoiler = false;
		_thumb = QImage();
		_thumbGood = false;
		delegate()->repaintItem(this);
	}
}

void Gif::maybeClearSensitiveSpoiler() {
	if (_sensitiveSpoiler) {
		clearSpoiler();
		setDocumentLinks(_data);
	}
}

void Gif::validateThumbnail(Image *image, QSize frame, bool good) {
	if (!image) {
		return;
	}
	if (_thumb.size() != frame * style::DevicePixelRatio()) {
		_thumb = QImage();
		_thumbGood = false;
	}
	if ((_thumbGood && !good) || (!_thumb.isNull() && (_thumbGood || !good))) {
		return;
	}
	_thumbGood = good;
	_thumb = image->pixNoCache(
		frame * style::DevicePixelRatio(),
		{ .options = good ? Images::Option() : Images::Option::Blur }).toImage();
}

void Gif::prepareThumbnail(QSize frame) {
	ensureDataMediaCreated();
	if (!_spoiler) {
		validateThumbnail(_dataMedia->thumbnail(), frame, true);
	}
	validateThumbnail(_dataMedia->thumbnailInline(), frame, false);
}

void Gif::paint(
		Painter &p,
		const QRect &clip,
		TextSelection selection,
		const PaintContext *context) {
	_mediaInset = context->mediaInset;
	const auto document = _data;
	ensureDataMediaCreated();
	const auto preview = Data::VideoPreviewState(_dataMedia.get());
	preview.automaticLoad(getItem()->fullId());

	const auto displayLoading = !preview.usingThumbnail()
		&& document->displayLoading();
	const auto loaded = preview.loaded();
	const auto loading = preview.loading();
	if (loaded
		&& !_gif
		&& !_gif.isBad()
		&& !_inlineOverCap
		&& CanPlayInline(document)) {
		auto that = const_cast<Gif*>(this);
		that->_gif = preview.makeAnimation([=](
				Media::Clip::Notification notification) {
			that->clipCallback(notification);
		});
	}

	const auto animating = !_spoiler && (_gif && _gif->started());
	if (displayLoading) {
		ensureRadial();
		if (!_radial->animating()) {
			_radial->start(dataProgress());
		}
	}
	const auto radial = isRadialAnimation();

	const auto frame = countFrameSize();
	const auto r = QRect(0, 0, _width, st::inlineMediaHeight);
	p.fillRect(r, st::overviewPhotoBg);
	const auto position = QPoint((r.width() - frame.width()) / 2,
		(r.height() - frame.height()) / 2);
	if (animating) {
		const auto pixmap = _gif->current({
			.frame = frame,
			.keepAlpha = true,
		}, context->paused ? 0 : context->ms);
		_thumb = pixmap;
		_thumbGood = true;
		p.drawImage(position, pixmap);
	} else {
		prepareThumbnail(frame);
		if (!_thumb.isNull()) {
			p.drawImage(position, _thumb);
		}
	}

	if (_spoiler) {
		const auto paused = context->paused || On(PowerSaving::kChatSpoiler);
		Ui::FillSpoilerRect(
			p,
			r,
			Ui::DefaultImageSpoiler().frame(
				_spoiler->index(context->ms, paused)));

		if (_sensitiveSpoiler) {
			PaintSensitiveTag(p, r);
		}
	}

	const auto selected = (selection == FullSelection);

	if (radial
		|| _gif.isBad()
		|| (!_gif && !loaded && !loading && !preview.usingThumbnail())) {
		const auto radialOpacity = (radial && loaded)
			? _radial->opacity()
			: 1.;
		p.setOpacity(radialOpacity);
		auto icon = [&] {
			if (radial || loading) {
				return &st::historyFileInCancel;
			} else if (loaded) {
				return &st::historyFileInPlay;
			}
			return &st::historyFileInDownload;
		}();
		const auto size = st::overviewVideoRadialSize;
		QRect inner(
			(r.width() - size) / 2,
			(r.height() - size) / 2,
			size,
			size);
		const auto colors = Ui::MediaButtonPalette(st::historyFileThumbRadialFg->c, st::msgDateImgBg->c, true);
		if (radial) {
			_radial->paint(p, inner, colors, _data->loading() || _data->uploading(),
				ClickHandler::showAsActive(_cancell));
		} else {
			Ui::PaintMediaButtonSurface(p, inner, colors);
			icon->paintInCenter(p, inner, colors.foreground);
		}
		p.setOpacity(1.);
	}

	const auto checkDelta = st::overviewCheckSkip + st::overviewCheck.size;
	const auto checkLeft = _width - checkDelta;
	const auto checkTop = st::overviewCheckSkip;
	paintCheckbox(p, { checkLeft, checkTop }, selected, context);
}

bool Gif::elementsAnimating() const {
	return RadialProgressItem::elementsAnimating()
		|| (_spoiler != nullptr)
		|| (_gif && _gif->started());
}

void Gif::update() {
	delegate()->repaintItem(this);
}

void Gif::ensureDataMediaCreated() const {
	if (_dataMedia) {
		return;
	}
	_dataMedia = _data->createMediaView();
	_dataMedia->goodThumbnailWanted();
	_dataMedia->thumbnailWanted(parent()->fullId());
	delegate()->registerHeavyItem(this);
}

void Gif::clearHeavyPart() {
	_gif.reset();
	_dataMedia = nullptr;
}

void Gif::setPosition(int32 position) {
	AbstractLayoutItem::setPosition(position);
	if (position < 0) {
		_gif.reset();
	}
}

float64 Gif::dataProgress() const {
	ensureDataMediaCreated();
	return _dataMedia->progress();
}

bool Gif::dataFinished() const {
	return !_data->loading()
		&& (!_data->uploading() || _data->waitingForAlbum());
}

bool Gif::dataLoaded() const {
	ensureDataMediaCreated();
	const auto preview = Data::VideoPreviewState(_dataMedia.get());
	return preview.loaded();
}

bool Gif::iconAnimated() const {
	return true;
}

TextState Gif::getState(
		QPoint point,
		StateRequest request) const {
	if (hasPoint(point)) {
		auto link = (_data->loading() || _data->uploading())
			? _cancell
			: dataLoaded()
			? _openl
			: _savel;
		const auto size = st::overviewVideoRadialSize;
		const auto button = QRect((_width - size) / 2,
			(st::inlineMediaHeight - size) / 2, size, size);
		if (link == _cancell && !Ui::MediaCancelBadgeContains(button, point)) {
			link = nullptr;
		}
		return { parent(), link };
	}
	return {};
}

void Gif::updateStatusText() {
	auto statusSize = int64();
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = Ui::FileStatusSizeFailed;
	} else if (_data->uploading()) {
		statusSize = _data->uploadingData->offset;
	} else if (dataLoaded()) {
		statusSize = Ui::FileStatusSizeLoaded;
	} else {
		statusSize = Ui::FileStatusSizeReady;
	}
	if (statusSize != _status.size()) {
		auto status = statusSize;
		auto size = _data->size;
		if (statusSize >= 0 && statusSize < 0xFF000000LL) {
			size = status;
			status = Ui::FileStatusSizeReady;
		}
		_status.update(status, size, -1, 0);
		_status.setSize(statusSize);
	}
}

} // namespace Overview::Layout
