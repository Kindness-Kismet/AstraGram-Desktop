/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_group_thumbs.h"

#include "base/variant.h"
#include "data/data_shared_media.h"
#include "data/data_user_photos.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_media_types.h"
#include "data/data_poll.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "data/data_file_origin.h"
#include "history/history.h"
#include "history/view/media/history_view_media.h"
#include "ui/image/image.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "main/main_session.h"
#include "core/crash_reports.h"
#include "styles/style_extras_styles.h"
#include "styles/style_media_view.h"

namespace Media {
namespace View {
namespace {

constexpr auto kThumbDuration = crl::time(150);
constexpr auto kAliveOpacity = 0.7;
// 窗口内样式：其余缩略图半透明，底色为 30% 黑，计数略淡。
constexpr auto kCompactAliveOpacity = 0.5;
constexpr auto kCompactBgOpacity = 0.3;
constexpr auto kCompactCounterOpacity = 0.75;

int Round(float64 value) {
	return int(base::SafeRound(value));
}

using Context = GroupThumbs::Context;
using Key = GroupThumbs::Key;

struct Metrics {
	int height = 0;
	int width = 0;
	int widthMax = 0;
	int skip = 0;
	int skipCurrent = 0;
	QMargins padding;
};

[[nodiscard]] Metrics ComputeMetrics(bool compact) {
	if (compact) {
		// 下边距含底色到窗口底边的距离。
		return {
			.height = st::extrasViewerGroupHeight,
			.width = st::extrasViewerGroupWidth,
			.widthMax = st::extrasViewerGroupWidthMax,
			.skip = st::extrasViewerGroupSkip,
			.skipCurrent = st::extrasViewerGroupSkip,
			.padding = (st::extrasViewerGroupPadding
				+ QMargins(0, 0, 0, st::extrasViewerGroupBottom)),
		};
	}
	return {
		.height = st::mediaviewGroupHeight,
		.width = st::mediaviewGroupWidth,
		.widthMax = st::mediaviewGroupWidthMax,
		.skip = st::mediaviewGroupSkip,
		.skipCurrent = st::mediaviewGroupSkipCurrent,
		.padding = st::mediaviewGroupPadding,
	};
}

[[nodiscard]] int CompactCounterWidth() {
	return st::extrasViewerGroupCounterSkip
		+ st::extrasViewerGroupCounterFont->width(u"0000 / 0000"_q);
}

[[nodiscard]] const std::array<QImage, 4> &CompactCorners() {
	static const auto result = Images::CornersMask(
		st::extrasViewerGroupRadius);
	return result;
}

#if 0
[[nodiscard]] QString DebugSerializeMsgId(FullMsgId itemId) {
	return QString("msg%1_%2").arg(itemId.channel.bare).arg(itemId.msg);
}

[[nodiscard]] QString DebugSerializePeer(PeerId peerId) {
	return peerIsUser(peerId)
		? QString("user%1").arg(peerToUser(peerId).bare)
		: peerIsChat(peerId)
		? QString("chat%1").arg(peerToChat(peerId).bare)
		: QString("channel%1").arg(peerToChannel(peerId).bare);
}

[[nodiscard]] QString DebugSerializeKey(const Key &key) {
	return v::match(key, [&](PhotoId photoId) {
		return QString("photo%1").arg(photoId);
	}, [](FullMsgId itemId) {
		return DebugSerializeMsgId(itemId);
	}, [&](GroupThumbs::CollageKey key) {
		return QString("collage%1").arg(key.index);
	}, [&](GroupThumbs::InstantViewKey key) {
		return QString("iv%1").arg(key.index);
	});
}

[[nodiscard]] QString DebugSerializeContext(const Context &context) {
	return v::match(context, [](PeerId peerId) {
		return DebugSerializePeer(peerId);
	}, [](MessageGroupId groupId) {
		return QString("group_%1_%2"
		).arg(DebugSerializePeer(groupId.peer)
		).arg(groupId.value);
	}, [](FullMsgId item) {
		return DebugSerializeMsgId(item);
	}, [](v::null_t) -> QString {
		return "null";
	});
}
#endif

Data::FileOrigin ComputeLocalThumbOrigin(const Context &context) {
	return v::match(context, [](FullMsgId itemId) {
		return Data::FileOrigin(itemId);
	}, [](auto&&) {
		return Data::FileOrigin();
	});
}

Data::FileOrigin ComputeFileOrigin(const Key &key, const Context &context) {
	return v::match(key, [&](PhotoId photoId) {
		return v::match(context, [&](PeerId peerId) {
			return peerIsUser(peerId)
				? Data::FileOriginUserPhoto(peerToUser(peerId), photoId)
				: Data::FileOrigin(Data::FileOriginPeerPhoto(peerId));
		}, [](auto&&) {
			return Data::FileOrigin();
		});
	}, [](FullMsgId itemId) {
		return Data::FileOrigin(itemId);
	}, [&](GroupThumbs::CollageKey) {
		return ComputeLocalThumbOrigin(context);
	}, [&](GroupThumbs::InstantViewKey) {
		return ComputeLocalThumbOrigin(context);
	});
}

Context ComputeContext(
		not_null<Main::Session*> session,
		const SharedMediaWithLastSlice &slice,
		int index) {
	Expects(index >= 0 && index < slice.size());

	const auto value = slice[index];
	if (const auto photo = std::get_if<not_null<PhotoData*>>(&value)) {
		if (const auto peer = (*photo)->peer) {
			return peer->id;
		}
		return v::null;
	} else if (const auto msgId = std::get_if<FullMsgId>(&value)) {
		if (const auto item = session->data().message(*msgId)) {
			if (item->isService()) {
				return item->history()->peer->id;
			} else if (const auto groupId = item->groupId()) {
				return groupId;
			}
		}
		return v::null;
	}
	Unexpected("Variant in ComputeContext(SharedMediaWithLastSlice::Value)");
}

Context ComputeContext(
		not_null<Main::Session*> session,
		const GroupThumbs::ChatMediaSlice &slice,
		int index) {
	Expects(index >= 0 && index < slice.size());

	const auto value = (*slice.data)[index];
	if (const auto photo = std::get_if<not_null<PhotoData*>>(&value)) {
		if (const auto peer = (*photo)->peer) {
			return peer->id;
		}
		return v::null;
	} else if (const auto msgId = std::get_if<FullMsgId>(&value)) {
		if (const auto item = session->data().message(*msgId)) {
			return item->history()->peer->id;
		}
		return v::null;
	}
	Unexpected("Variant in ComputeContext(ChatMediaSlice::Value)");
}

Context ComputeContext(
		not_null<Main::Session*> session,
		const UserPhotosSlice &slice,
		int index) {
	return peerFromUser(slice.key().userId);
}

Context ComputeContext(
		not_null<Main::Session*> session,
		const GroupThumbs::CollageSlice &slice,
		int index) {
	return slice.context;
}

Context ComputeContext(
		not_null<Main::Session*> session,
		const GroupThumbs::InstantViewSlice &slice,
		int index) {
	return slice.context;
}

Key ComputeKey(const SharedMediaWithLastSlice &slice, int index) {
	Expects(index >= 0 && index < slice.size());

	const auto value = slice[index];
	if (const auto photo = std::get_if<not_null<PhotoData*>>(&value)) {
		return (*photo)->id;
	} else if (const auto msgId = std::get_if<FullMsgId>(&value)) {
		return *msgId;
	}
	Unexpected("Variant in ComputeKey(SharedMediaWithLastSlice::Value)");
}

Key ComputeKey(const GroupThumbs::ChatMediaSlice &slice, int index) {
	return ComputeKey(*slice.data, index);
}

Key ComputeKey(const UserPhotosSlice &slice, int index) {
	return slice[index];
}

Key ComputeKey(const GroupThumbs::CollageSlice &slice, int index) {
	return GroupThumbs::CollageKey{ index };
}

Key ComputeKey(const GroupThumbs::InstantViewSlice &slice, int index) {
	return GroupThumbs::InstantViewKey{ index };
}

const GroupThumbs::InstantViewItems *ComputeInstantViewItems(
		const SharedMediaWithLastSlice &) {
	return nullptr;
}

const GroupThumbs::InstantViewItems *ComputeInstantViewItems(
		const GroupThumbs::ChatMediaSlice &) {
	return nullptr;
}

const GroupThumbs::InstantViewItems *ComputeInstantViewItems(
		const UserPhotosSlice &) {
	return nullptr;
}

const GroupThumbs::InstantViewItems *ComputeInstantViewItems(
		const GroupThumbs::CollageSlice &) {
	return nullptr;
}

const GroupThumbs::InstantViewItems *ComputeInstantViewItems(
		const GroupThumbs::InstantViewSlice &slice) {
	return &*slice.data;
}

std::optional<WebPageCollage::Item> PollAnswerMediaItemByIndex(
		not_null<PollData*> poll,
		int index) {
	auto current = 0;
	for (const auto &answer : poll->answers) {
		const auto &media = answer.media;
		auto item = std::optional<WebPageCollage::Item>();
		if (media.photo) {
			item = WebPageCollage::Item(media.photo);
		} else if (media.document && !media.document->sticker()) {
			item = WebPageCollage::Item(media.document);
		}
		if (item) {
			if (current++ == index) {
				return item;
			}
		}
	}
	return std::nullopt;
}

int ComputeThumbsLimit(int availableWidth, bool compact) {
	const auto metrics = ComputeMetrics(compact);
	if (compact) {
		// 当前项居中，两侧都要留出底色内边距和计数的宽度。
		availableWidth -= 2 * (metrics.padding.right() + CompactCounterWidth());
	}
	const auto singleWidth = metrics.width + 2 * metrics.skip;
	const auto currentWidth = metrics.widthMax + 2 * metrics.skipCurrent;
	const auto skipForAnimation = 2 * singleWidth;
	const auto leftWidth = availableWidth
		- currentWidth
		- skipForAnimation;
	return std::max(leftWidth / (2 * singleWidth), 1);
}

} // namespace

class GroupThumbs::Thumb {
public:
	enum class State {
		Unknown,
		Current,
		Alive,
		Dying,
	};

	Thumb(Key key, bool compact, Fn<void()> handler);
	Thumb(
		Key key,
		bool compact,
		not_null<PhotoData*> photo,
		Data::FileOrigin origin,
		Fn<void()> handler);
	Thumb(
		Key key,
		bool compact,
		not_null<DocumentData*> document,
		Data::FileOrigin origin,
		Fn<void()> handler);

	[[nodiscard]] int leftToUpdate() const;
	[[nodiscard]] int rightToUpdate() const;
	[[nodiscard]] int currentLeft() const;
	[[nodiscard]] int currentWidth() const;
	void animate(float64 progress);

	void animateToLeft(not_null<Thumb*> next);
	void animateToRight(not_null<Thumb*> prev);

	void setState(State state);
	[[nodiscard]] State state() const;
	[[nodiscard]] bool inited() const;
	[[nodiscard]] bool removed() const;

	void paint(QPainter &p, int x, int y, int outerWidth, float64 progress);
	[[nodiscard]] ClickHandlerPtr getState(QPoint point) const;

private:
	QSize wantedPixSize() const;
	void validateImage();
	void validateCompactImage();
	void paintCompact(QPainter &p, QRect to, float64 opacity);
	int finalLeft() const;
	int finalWidth() const;
	void animateTo(int left, int width);

	ClickHandlerPtr _link;
	const Key _key;
	const bool _compact = false;
	const Metrics _metrics;
	std::shared_ptr<Data::DocumentMedia> _documentMedia;
	std::shared_ptr<Data::PhotoMedia> _photoMedia;
	Image *_image = nullptr;
	Data::FileOrigin _origin;
	State _state = State::Alive;
	QPixmap _full;
	QImage _rounded;
	int _fullWidth = 0;
	bool _hiding = false;

	anim::value _left = { 0. };
	anim::value _width = { 0. };
	// 新缩略图淡入；窗口内样式直接淡入到非当前项的半透明，避免误画描边。
	anim::value _opacity = { 0., _compact ? kCompactAliveOpacity : 1. };

};

GroupThumbs::Thumb::Thumb(Key key, bool compact, Fn<void()> handler)
: _key(key)
, _compact(compact)
, _metrics(ComputeMetrics(compact)) {
	_link = std::make_shared<LambdaClickHandler>(std::move(handler));
	validateImage();
}

GroupThumbs::Thumb::Thumb(
	Key key,
	bool compact,
	not_null<PhotoData*> photo,
	Data::FileOrigin origin,
	Fn<void()> handler)
: _key(key)
, _compact(compact)
, _metrics(ComputeMetrics(compact))
, _photoMedia(photo->createMediaView())
, _origin(origin) {
	_link = std::make_shared<LambdaClickHandler>(std::move(handler));
	_photoMedia->wanted(Data::PhotoSize::Thumbnail, origin);
	validateImage();
}

GroupThumbs::Thumb::Thumb(
	Key key,
	bool compact,
	not_null<DocumentData*> document,
	Data::FileOrigin origin,
	Fn<void()> handler)
: _key(key)
, _compact(compact)
, _metrics(ComputeMetrics(compact))
, _documentMedia(document->createMediaView())
, _origin(origin) {
	_link = std::make_shared<LambdaClickHandler>(std::move(handler));
	_documentMedia->thumbnailWanted(origin);
	validateImage();
}

QSize GroupThumbs::Thumb::wantedPixSize() const {
	const auto originalWidth = _image ? std::max(_image->width(), 1) : 1;
	const auto originalHeight = _image ? std::max(_image->height(), 1) : 1;
	const auto pixHeight = _metrics.height;
	const auto pixWidth = originalWidth * pixHeight / originalHeight;
	return { pixWidth, pixHeight };
}

void GroupThumbs::Thumb::validateImage() {
	if (!_image) {
		if (_photoMedia) {
			_image = _photoMedia->image(Data::PhotoSize::Thumbnail);
		} else if (_documentMedia) {
			_image = _documentMedia->thumbnail();
		}
	}
	if (_compact) {
		validateCompactImage();
		return;
	} else if (!_full.isNull() || !_image) {
		return;
	}

	const auto pixSize = wantedPixSize();
	if (pixSize.width() > st::mediaviewGroupWidthMax) {
		const auto originalWidth = _image->width();
		const auto originalHeight = _image->height();
		const auto takeWidth = originalWidth * st::mediaviewGroupWidthMax
			/ pixSize.width();
		auto original = _image->original();
		original.setDevicePixelRatio(style::DevicePixelRatio());
		_full = Ui::PixmapFromImage(original.copy(
			(originalWidth - takeWidth) / 2,
			0,
			takeWidth,
			originalHeight
		).scaled(
			st::mediaviewGroupWidthMax * style::DevicePixelRatio(),
			pixSize.height() * style::DevicePixelRatio(),
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation));
	} else {
		_full = _image->pixNoCache(pixSize * style::DevicePixelRatio());
	}
	_fullWidth = std::min(
		wantedPixSize().width(),
		st::mediaviewGroupWidthMax);
}

void GroupThumbs::Thumb::validateCompactImage() {
	// 宽度固定，图片铺满后居中裁剪；加载前也按固定宽度排版。
	_fullWidth = _metrics.widthMax;
	if (!_full.isNull() || !_image) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto size = QSize(_metrics.widthMax, _metrics.height) * ratio;
	const auto scaled = _image->original().scaled(
		size,
		Qt::KeepAspectRatioByExpanding,
		Qt::SmoothTransformation);
	auto cropped = scaled.copy(
		(scaled.width() - size.width()) / 2,
		(scaled.height() - size.height()) / 2,
		size.width(),
		size.height());
	cropped.setDevicePixelRatio(ratio);
	_full = Ui::PixmapFromImage(std::move(cropped));
	_rounded = QImage();
}

int GroupThumbs::Thumb::leftToUpdate() const {
	return Round(std::min(_left.from(), _left.to()));
}

int GroupThumbs::Thumb::rightToUpdate() const {
	return Round(std::max(
		_left.from() + _width.from(),
		_left.to() + _width.to()));
}

int GroupThumbs::Thumb::currentLeft() const {
	return Round(_left.current());
}

int GroupThumbs::Thumb::currentWidth() const {
	return Round(_width.current());
}

int GroupThumbs::Thumb::finalLeft() const {
	return Round(_left.to());
}

int GroupThumbs::Thumb::finalWidth() const {
	return Round(_width.to());
}

void GroupThumbs::Thumb::setState(State state) {
	const auto isNewThumb = (_state == State::Alive);
	_state = state;
	if (_state == State::Current) {
		if (isNewThumb) {
			_opacity = anim::value(1.);
			_left = anim::value(-_fullWidth / 2);
			_width = anim::value(_fullWidth);
		} else {
			_opacity.start(1.);
		}
		_hiding = false;
		animateTo(-_fullWidth / 2, _fullWidth);
	} else if (_state == State::Alive) {
		_opacity.start(_compact ? kCompactAliveOpacity : kAliveOpacity);
		_hiding = false;
	} else if (_state == State::Dying) {
		_opacity.start(0.);
		_hiding = true;
		_left.restart();
		_width.restart();
	}
}

void GroupThumbs::Thumb::animateTo(int left, int width) {
	_left.start(left);
	_width.start(width);
}

void GroupThumbs::Thumb::animateToLeft(not_null<Thumb*> next) {
	const auto width = _metrics.width;
	if (_state == State::Alive) {
		// New item animation, start exactly from the next, move only.
		_left = anim::value(next->currentLeft() - width);
		_width = anim::value(width);
	} else if (_state == State::Unknown) {
		// Existing item animation.
		setState(State::Alive);
	}
	const auto skip1 = _metrics.skip;
	const auto skip2 = (next->state() == State::Current)
		? _metrics.skipCurrent
		: _metrics.skip;
	animateTo(next->finalLeft() - width - skip1 - skip2, width);
}

void GroupThumbs::Thumb::animateToRight(not_null<Thumb*> prev) {
	const auto width = _metrics.width;
	if (_state == State::Alive) {
		// New item animation, start exactly from the next, move only.
		_left = anim::value(prev->currentLeft() + prev->currentWidth());
		_width = anim::value(width);
	} else if (_state == State::Unknown) {
		// Existing item animation.
		setState(State::Alive);
	}
	const auto skip1 = _metrics.skip;
	const auto skip2 = (prev->state() == State::Current)
		? _metrics.skipCurrent
		: _metrics.skip;
	animateTo(prev->finalLeft() + prev->finalWidth() + skip1 + skip2, width);
}

auto GroupThumbs::Thumb::state() const -> State {
	return _state;
}

bool GroupThumbs::Thumb::inited() const {
	return _fullWidth != 0;
}

bool GroupThumbs::Thumb::removed() const {
	return (_state == State::Dying) && _hiding && !_opacity.current();
}

void GroupThumbs::Thumb::paint(
		QPainter &p,
		int x,
		int y,
		int outerWidth,
		float64 progress) {
	validateImage();
	animate(progress);

	const auto left = x + currentLeft();
	const auto width = currentWidth();
	const auto opacity = p.opacity();
	if (_compact) {
		paintCompact(p, QRect(left, y, width, _metrics.height), opacity);
		return;
	}
	p.setOpacity(_opacity.current() * opacity);
	if (width == _fullWidth) {
		p.drawPixmap(left, y, _full);
	} else {
		const auto takeWidth = width * style::DevicePixelRatio();
		const auto from = QRect(
			(_full.width() - takeWidth) / 2,
			0,
			takeWidth,
			_full.height());
		const auto to = QRect(left, y, width, _metrics.height);
		p.drawPixmap(to, _full, from);
	}
	p.setOpacity(opacity);
}

void GroupThumbs::Thumb::animate(float64 progress) {
	_opacity.update(progress, anim::linear);
	_left.update(progress, anim::linear);
	_width.update(progress, anim::linear);
}

void GroupThumbs::Thumb::paintCompact(
		QPainter &p,
		QRect to,
		float64 opacity) {
	if (to.width() <= 0) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	if (!_full.isNull() && _rounded.width() != to.width() * ratio) {
		auto frame = QImage(
			to.size() * ratio,
			QImage::Format_ARGB32_Premultiplied);
		frame.setDevicePixelRatio(ratio);
		frame.fill(Qt::transparent);
		auto q = QPainter(&frame);
		const auto take = to.width() * ratio;
		q.drawPixmap(
			QRect(QPoint(), to.size()),
			_full,
			QRect((_full.width() - take) / 2, 0, take, _full.height()));
		q.end();
		_rounded = Images::Round(std::move(frame), CompactCorners());
	}
	p.setOpacity(_opacity.current() * opacity);
	if (!_rounded.isNull()) {
		p.drawImage(to.topLeft(), _rounded);
	}
	auto hq = PainterHighQualityEnabler(p);
	if (_documentMedia && _documentMedia->owner()->isVideoFile()) {
		// 视频缩略图中间画播放三角，重心略偏左。
		const auto size = st::extrasViewerGroupPlay;
		const auto center = QRectF(to).center();
		const auto left = center.x() - size.width() * 0.35;
		const auto top = center.y() - size.height() / 2.;
		auto path = QPainterPath();
		path.moveTo(left, top);
		path.lineTo(left + size.width(), center.y());
		path.lineTo(left, top + size.height());
		path.closeSubpath();
		p.setPen(Qt::NoPen);
		p.setBrush(st::mediaviewControlFg);
		p.drawPath(path);
	}
	// 当前项带白色描边，切换时随不透明度一起淡入淡出。
	const auto outline = (_opacity.current() - kCompactAliveOpacity)
		/ (1. - kCompactAliveOpacity);
	if (outline > 0.) {
		const auto stroke = float64(st::extrasViewerGroupOutline);
		const auto skip = st::extrasViewerGroupOutlineSkip + stroke / 2.;
		const auto radius = st::extrasViewerGroupRadius + skip;
		p.setOpacity(std::min(outline, 1.) * opacity);
		p.setPen(QPen(st::mediaviewControlFg, stroke));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(
			QRectF(to).marginsAdded({ skip, skip, skip, skip }),
			radius,
			radius);
	}
	p.setOpacity(opacity);
}

ClickHandlerPtr GroupThumbs::Thumb::getState(QPoint point) const {
	if (_state != State::Alive) {
		return nullptr;
	}
	const auto left = finalLeft();
	const auto width = finalWidth();
	return QRect(left, 0, width, _metrics.height).contains(point)
		? _link
		: nullptr;
}

int GroupThumbs::CollageSlice::size() const {
	return data->items.size();
}

int GroupThumbs::InstantViewSlice::size() const {
	return data->size();
}

int GroupThumbs::ChatMediaSlice::size() const {
	return data->size();
}

GroupThumbs::GroupThumbs(
	not_null<Main::Session*> session,
	Context context,
	bool compact)
: _session(session)
, _context(context)
, _compact(compact) {
}

void GroupThumbs::updateContext(Context context) {
	if (_context != context) {
		clear();
		_context = context;
	}
}

template <typename Slice>
void GroupThumbs::RefreshFromSlice(
		not_null<Main::Session*> session,
		std::unique_ptr<GroupThumbs> &instance,
		const Slice &slice,
		int index,
		int availableWidth,
		bool compact) {
	const auto context = ComputeContext(session, slice, index);
	const auto instantViewItems = ComputeInstantViewItems(slice);
	if (instance) {
		instance->updateContext(context);
		instance->updateInstantViewItems(instantViewItems);
	}
	if (v::is_null(context)) {
		if (instance) {
			instance->resizeToWidth(availableWidth);
		}
		return;
	}
	const auto limit = ComputeThumbsLimit(availableWidth, compact);
	const auto from = [&] {
		const auto edge = std::max(index - limit, 0);
		for (auto result = index; result != edge; --result) {
			if (ComputeContext(session, slice, result - 1) != context) {
				return result;
			}
		}
		return edge;
	}();
	const auto till = [&] {
		const auto edge = std::min(index + limit + 1, slice.size());
		for (auto result = index + 1; result != edge; ++result) {
			if (ComputeContext(session, slice, result) != context) {
				return result;
			}
		}
		return edge;
	}();
	if (from + 1 < till) {
		if (!instance) {
			instance = std::make_unique<GroupThumbs>(
				session,
				context,
				compact);
		}
		instance->updateInstantViewItems(instantViewItems);
		instance->fillItems(slice, from, index, till);
		instance->resizeToWidth(availableWidth);
	} else if (instance) {
		instance->updateInstantViewItems(instantViewItems);
		instance->clear();
		instance->resizeToWidth(availableWidth);
	}
}

#if 0
template <typename Slice>
void ValidateSlice(
		const Slice &slice,
		const Context &context,
		int from,
		int index,
		int till) {
	auto keys = base::flat_set<Key>();
	for (auto i = from; i != till; ++i) {
		const auto key = ComputeKey(slice, i);
		if (keys.contains(key)) {
			// All items should be unique!
			auto strings = QStringList();
			strings.reserve(till - from);
			for (auto i = from; i != till; ++i) {
				strings.push_back(DebugSerializeKey(ComputeKey(slice, i)));
			}
			CrashReports::SetAnnotation(
				"keys",
				QString("%1:%2-(%3)-%4:"
				).arg(DebugSerializeContext(context)
				).arg(from
				).arg(index
				).arg(till) + strings.join(","));
			if (Logs::DebugEnabled()) {
				Unexpected("Bad slice in GroupThumbs.");
			}
			break;
		} else {
			keys.emplace(key);
		}
	}
}
#endif

template <typename Slice>
void GroupThumbs::fillItems(
		const Slice &slice,
		int from,
		int index,
		int till) {
	Expects(from <= index);
	Expects(index < till);
	Expects(from + 1 < till);

	// 内存图片在创建缩略图时同步加载完成，会触发查看器重入刷新，这里跳过。
	if (_filling) {
		return;
	}
	const auto current = (index - from);
	const auto old = base::take(_items);

	//ValidateSlice(slice, _context, from, index, till);

	markCacheStale();
	_items.reserve(till - from);
	_filling = true;
	for (auto i = from; i != till; ++i) {
		_items.push_back(validateCacheEntry(ComputeKey(slice, i)));
	}
	_filling = false;
	animateAliveItems(current);
	fillDyingItems(old);
	startDelayedAnimation();
}

void GroupThumbs::updateInstantViewItems(const InstantViewItems *items) {
	_instantViewItems = items;
}

void GroupThumbs::animateAliveItems(int current) {
	Expects(current >= 0 && current < _items.size());

	_items[current]->setState(Thumb::State::Current);
	for (auto i = current; i != 0;) {
		const auto prev = _items[i];
		const auto item = _items[--i];
		item->animateToLeft(prev);
	}
	for (auto i = current + 1; i != _items.size(); ++i) {
		const auto prev = _items[i - 1];
		const auto item = _items[i];
		item->animateToRight(prev);
	}
}

void GroupThumbs::fillDyingItems(const std::vector<not_null<Thumb*>> &old) {
	//Expects(_cache.size() >= _items.size());

	if (_cache.size() >= _items.size()) {
		_dying.reserve(_cache.size() - _items.size());
	}
	animatePreviouslyAlive(old);
	markRestAsDying();
}

void GroupThumbs::markRestAsDying() {
	//Expects(_cache.size() >= _items.size());

	if (_cache.size() >= _items.size()) {
		_dying.reserve(_cache.size() - _items.size());
	}
	for (const auto &cacheItem : _cache) {
		const auto &thumb = cacheItem.second;
		const auto state = thumb->state();
		if (state == Thumb::State::Unknown) {
			markAsDying(thumb.get());
		}
	}
}

void GroupThumbs::markAsDying(not_null<Thumb*> thumb) {
	thumb->setState(Thumb::State::Dying);
	_dying.push_back(thumb.get());
}

void GroupThumbs::animatePreviouslyAlive(
		const std::vector<not_null<Thumb*>> &old) {
	auto toRight = false;
	for (auto i = 0; i != old.size(); ++i) {
		const auto item = old[i];
		if (item->state() == Thumb::State::Unknown) {
			if (toRight) {
				markAsDying(item);
				item->animateToRight(old[i - 1]);
			}
		} else if (!toRight) {
			for (auto j = i; j != 0;) {
				const auto next = old[j];
				const auto prev = old[--j];
				markAsDying(prev);
				prev->animateToLeft(next);
			}
			toRight = true;
		}
	}
}

auto GroupThumbs::createThumb(Key key)
-> std::unique_ptr<Thumb> {
	if (const auto photoId = std::get_if<PhotoId>(&key)) {
		const auto photo = _session->data().photo(*photoId);
		return createThumb(key, photo);
	} else if (const auto msgId = std::get_if<FullMsgId>(&key)) {
		if (const auto item = _session->data().message(*msgId)) {
			if (const auto media = item->media()) {
				if (const auto photo = media->photo()) {
					return createThumb(key, photo);
				} else if (const auto document = media->document()) {
					return createThumb(key, document);
				}
			}
		}
		return createThumb(key, nullptr);
	} else if (const auto collageKey = std::get_if<CollageKey>(&key)) {
		if (const auto itemId = std::get_if<FullMsgId>(&_context)) {
			if (const auto item = _session->data().message(*itemId)) {
				if (const auto media = item->media()) {
					if (const auto page = media->webpage()) {
						return createThumb(
							key,
							page->collage,
							collageKey->index);
					} else if (const auto invoice = media->invoice()) {
						return createThumb(
							key,
							*invoice,
							collageKey->index);
					} else if (const auto poll = media->poll()) {
						if (const auto item = PollAnswerMediaItemByIndex(
								poll,
								collageKey->index)) {
							return v::match(*item, [&](PhotoData *photo) {
								return createThumb(key, photo);
							}, [&](DocumentData *document) {
								return createThumb(key, document);
							});
						}
					}
				}
			}
		}
		return createThumb(key, nullptr);
	} else if (const auto instantViewKey = std::get_if<InstantViewKey>(&key)) {
		return _instantViewItems
			? createThumb(key, *_instantViewItems, instantViewKey->index)
			: createThumb(key, nullptr);
	}
	Unexpected("Value of Key in GroupThumbs::createThumb()");
}

auto GroupThumbs::createThumb(
	Key key,
	const WebPageCollage &collage,
	int index)
-> std::unique_ptr<Thumb> {
	if (index < 0 || index >= collage.items.size()) {
		return createThumb(key, nullptr);
	}
	return v::match(collage.items[index], [&](PhotoData *photo) {
		return createThumb(key, photo);
	}, [&](DocumentData *document) {
		return createThumb(key, document);
	});
}

auto GroupThumbs::createThumb(
	Key key,
	const InstantViewItems &items,
	int index)
-> std::unique_ptr<Thumb> {
	if (index < 0 || index >= items.size()) {
		return createThumb(key, nullptr);
	}
	return v::match(items[index], [&](PhotoData *photo) {
		return photo
			? createThumb(key, not_null{ photo })
			: createThumb(key, nullptr);
	}, [&](DocumentData *document) {
		return document
			? createThumb(key, not_null{ document })
			: createThumb(key, nullptr);
	});
}

auto GroupThumbs::createThumb(
	Key key,
	const Data::Invoice &invoice,
	int index)
-> std::unique_ptr<Thumb> {
	if (index < 0 || index >= invoice.extendedMedia.size()) {
		return createThumb(key, nullptr);
	}
	const auto &media = invoice.extendedMedia[index];
	if (const auto photo = media->photo()) {
		return createThumb(key, photo);
	} else if (const auto document = media->document()) {
		return createThumb(key, document);
	}
	return createThumb(key, nullptr);
}

auto GroupThumbs::createThumb(Key key, std::nullptr_t)
-> std::unique_ptr<Thumb> {
	const auto weak = base::make_weak(this);
	const auto origin = ComputeFileOrigin(key, _context);
	return std::make_unique<Thumb>(key, _compact, [=] {
		if (const auto strong = weak.get()) {
			strong->_activateStream.fire_copy(key);
		}
	});
}

auto GroupThumbs::createThumb(Key key, not_null<PhotoData*> photo)
-> std::unique_ptr<Thumb> {
	const auto weak = base::make_weak(this);
	const auto origin = ComputeFileOrigin(key, _context);
	return std::make_unique<Thumb>(key, _compact, photo, origin, [=] {
		if (const auto strong = weak.get()) {
			strong->_activateStream.fire_copy(key);
		}
	});
}

auto GroupThumbs::createThumb(Key key, not_null<DocumentData*> document)
-> std::unique_ptr<Thumb> {
	const auto weak = base::make_weak(this);
	const auto origin = ComputeFileOrigin(key, _context);
	return std::make_unique<Thumb>(key, _compact, document, origin, [=] {
		if (const auto strong = weak.get()) {
			strong->_activateStream.fire_copy(key);
		}
	});
}

auto GroupThumbs::validateCacheEntry(Key key) -> not_null<Thumb*> {
	const auto i = _cache.find(key);
	return (i != _cache.end())
		? i->second.get()
		: _cache.emplace(key, createThumb(key)).first->second.get();
}

void GroupThumbs::markCacheStale() {
	_dying.clear();
	for (const auto &cacheItem : _cache) {
		const auto &thumb = cacheItem.second;
		thumb->setState(Thumb::State::Unknown);
	}
}

void GroupThumbs::Refresh(
		not_null<Main::Session*> session,
		std::unique_ptr<GroupThumbs> &instance,
		const SharedMediaWithLastSlice &slice,
		int index,
		int availableWidth,
		bool compact) {
	RefreshFromSlice(session, instance, slice, index, availableWidth, compact);
}

void GroupThumbs::Refresh(
		not_null<Main::Session*> session,
		std::unique_ptr<GroupThumbs> &instance,
		const UserPhotosSlice &slice,
		int index,
		int availableWidth,
		bool compact) {
	RefreshFromSlice(session, instance, slice, index, availableWidth, compact);
}

void GroupThumbs::Refresh(
		not_null<Main::Session*> session,
		std::unique_ptr<GroupThumbs> &instance,
		const CollageSlice &slice,
		int index,
		int availableWidth,
		bool compact) {
	RefreshFromSlice(session, instance, slice, index, availableWidth, compact);
}

void GroupThumbs::Refresh(
		not_null<Main::Session*> session,
		std::unique_ptr<GroupThumbs> &instance,
		const InstantViewSlice &slice,
		int index,
		int availableWidth,
		bool compact) {
	RefreshFromSlice(session, instance, slice, index, availableWidth, compact);
}

void GroupThumbs::Refresh(
		not_null<Main::Session*> session,
		std::unique_ptr<GroupThumbs> &instance,
		const ChatMediaSlice &slice,
		int index,
		int availableWidth,
		bool compact) {
	RefreshFromSlice(session, instance, slice, index, availableWidth, compact);
}

void GroupThumbs::setCounter(const QString &text) {
	_counter = text;
}

void GroupThumbs::clear() {
	_instantViewItems = nullptr;
	if (_items.empty()) {
		return;
	}
	base::take(_items);
	markCacheStale();
	markRestAsDying();
	startDelayedAnimation();
}

void GroupThumbs::startDelayedAnimation() {
	_animation.stop();
	_waitingForAnimationStart = true;
	countUpdatedRect();
}

void GroupThumbs::resizeToWidth(int newWidth) {
	_width = newWidth;
}

int GroupThumbs::height() const {
	const auto metrics = ComputeMetrics(_compact);
	return metrics.padding.top() + metrics.height + metrics.padding.bottom();
}

bool GroupThumbs::hiding() const {
	return _items.empty();
}

bool GroupThumbs::hidden() const {
	return hiding() && !_waitingForAnimationStart && !_animation.animating();
}

void GroupThumbs::checkForAnimationStart() {
	if (_waitingForAnimationStart) {
		_waitingForAnimationStart = false;
		_animation.start([=] { update(); }, 0., 1., kThumbDuration);
	}
}

void GroupThumbs::update() {
	if (_cache.empty()) {
		return;
	}
	_updateRequests.fire_copy(_updatedRect);
}

void GroupThumbs::paint(QPainter &p, int x, int y, int outerWidth) {
	const auto progress = _waitingForAnimationStart
		? 0.
		: _animation.value(1.);
	x += (_width / 2);
	if (_compact) {
		paintCompactBackground(p, x, y, progress);
	}
	y += ComputeMetrics(_compact).padding.top();
	auto initedCurrentIndex = int(_items.size());
	for (auto i = _cache.begin(); i != _cache.end();) {
		const auto thumb = not_null{ i->second.get() };
		const auto inited = thumb->inited();
		thumb->paint(p, x, y, outerWidth, progress);
		if (thumb->removed()) {
			_dying.erase(ranges::remove(_dying, thumb), _dying.end());
			i = _cache.erase(i);
		} else {
			if (!inited
				&& thumb->inited()
				&& thumb->state() == Thumb::State::Current) {
				initedCurrentIndex = ranges::find(_items, thumb)
					- begin(_items);
			}
			++i;
		}
	}
	if (initedCurrentIndex < _items.size()) {
		animateAliveItems(initedCurrentIndex);
	}
}

ClickHandlerPtr GroupThumbs::getState(QPoint point) const {
	point -= QPoint((_width / 2), ComputeMetrics(_compact).padding.top());
	for (const auto &cacheItem : _cache) {
		const auto &thumb = cacheItem.second;
		if (auto link = thumb->getState(point)) {
			return link;
		}
	}
	return nullptr;
}

void GroupThumbs::countUpdatedRect() {
	if (_cache.empty()) {
		return;
	}
	auto min = _width;
	auto max = 0;
	const auto left = [](const auto &cacheItem) {
		const auto &[key, thumb] = cacheItem;
		return thumb->leftToUpdate();
	};
	const auto right = [](const auto &cacheItem) {
		const auto &[key, thumb] = cacheItem;
		return thumb->rightToUpdate();
	};
	accumulate_min(min, left(*ranges::max_element(
		_cache,
		std::greater<>(),
		left)));
	accumulate_max(max, right(*ranges::max_element(
		_cache,
		std::less<>(),
		right)));
	if (_compact) {
		// 底色比缩略图宽出内边距，右侧还有计数。
		const auto &padding = st::extrasViewerGroupPadding;
		min -= padding.left();
		max += padding.right() + CompactCounterWidth();
		_updatedRect = QRect(min, 0, max - min, height());
		return;
	}
	_updatedRect = QRect(
		min,
		st::mediaviewGroupPadding.top(),
		max - min,
		st::mediaviewGroupHeight);
}

void GroupThumbs::paintCompactBackground(
		QPainter &p,
		int x,
		int y,
		float64 progress) {
	if (_items.empty()) {
		return;
	}
	auto left = std::numeric_limits<int>::max();
	auto right = std::numeric_limits<int>::min();
	for (const auto &thumb : _items) {
		thumb->animate(progress);
		left = std::min(left, thumb->currentLeft());
		right = std::max(right, thumb->currentLeft() + thumb->currentWidth());
	}
	const auto &padding = st::extrasViewerGroupPadding;
	const auto rect = QRect(
		x + left - padding.left(),
		y,
		right - left + padding.left() + padding.right(),
		padding.top() + st::extrasViewerGroupHeight + padding.bottom());
	const auto opacity = p.opacity();
	{
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = st::extrasViewerGroupBgRadius;
		p.setOpacity(opacity * kCompactBgOpacity);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0, 0, 0));
		p.drawRoundedRect(rect, radius, radius);
	}
	if (!_counter.isEmpty()) {
		const auto &font = st::extrasViewerGroupCounterFont;
		p.setOpacity(opacity * kCompactCounterOpacity);
		p.setFont(font);
		p.setPen(st::mediaviewControlFg);
		p.drawText(
			rect.x() + rect.width() + st::extrasViewerGroupCounterSkip,
			rect.y() + (rect.height() - font->height) / 2 + font->ascent,
			_counter);
	}
	p.setOpacity(opacity);
}

GroupThumbs::~GroupThumbs() = default;

} // namespace View
} // namespace Media
