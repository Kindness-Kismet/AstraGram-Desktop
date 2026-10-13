#include "extras/features/video_preview/video_preview.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "history/view/media/history_view_media.h"
#include "lang_auto.h"
#include "main/main_session.h"
#include "ui/image/image_location_factory.h"
#include "ui/image/image_prepare.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "window/window_session_controller.h"

namespace ExtrasVideoPreview {
namespace {

struct Frame {
	TimeId time = 0;
	QRect source;
};

struct Storyboard {
	QImage sprite;
	QSize frameSize;
	std::vector<Frame> frames;
};

// 索引开头是 key=value 元数据，之后每行为“秒,x,y”，指向拼图里的一帧。
[[nodiscard]] std::optional<Storyboard> parseStoryboard(
		const QByteArray &map,
		QImage sprite) {
	if (sprite.isNull()) {
		return std::nullopt;
	}
	auto result = Storyboard{ .sprite = std::move(sprite) };
	auto positions = std::vector<std::pair<TimeId, QPoint>>();
	for (const auto &raw : map.split('\n')) {
		const auto line = raw.trimmed();
		if (line.startsWith("frame_width=")) {
			result.frameSize.setWidth(line.mid(12).toInt());
			continue;
		} else if (line.startsWith("frame_height=")) {
			result.frameSize.setHeight(line.mid(13).toInt());
			continue;
		}
		const auto parts = line.split(',');
		if (parts.size() != 3) {
			continue;
		}
		positions.emplace_back(
			TimeId(parts[0].toDouble()),
			QPoint(parts[1].toInt(), parts[2].toInt()));
	}
	if (result.frameSize.isEmpty()) {
		return std::nullopt;
	}
	const auto bounds = result.sprite.rect();
	for (const auto &[time, position] : positions) {
		const auto source = QRect(position, result.frameSize);
		if (bounds.contains(source)) {
			result.frames.push_back({ .time = time, .source = source });
		}
	}
	if (result.frames.empty()) {
		return std::nullopt;
	}
	return result;
}

// 帧图只有一百多像素，按视频尺寸声明大小，查看器像播放时一样放大铺满。
[[nodiscard]] QSize displaySize(
		not_null<DocumentData*> document,
		QSize frame) {
	const auto video = document->dimensions.isEmpty()
		? frame
		: document->dimensions;
	const auto limit = PhotoData::SideLimit();
	const auto result = frame.scaled(video, Qt::KeepAspectRatio);
	return result.scaled(
		result.boundedTo(QSize(limit, limit)),
		Qt::KeepAspectRatio);
}

// 帧编号由拼图文件编号推出，再次打开同一视频时复用已经建好的图片。
[[nodiscard]] not_null<PhotoData*> framePhoto(
		not_null<DocumentData*> storyboard,
		int index,
		TimeId date,
		const QImage &image,
		QSize display) {
	const auto owner = &storyboard->owner();
	const auto id = PhotoId(storyboard->id + index + 1);
	const auto existing = owner->photo(id);
	if (!existing->isNull()) {
		return existing;
	}
	// 图片数据只放在内存里，释放后从内存重新解码，不写缓存也不联网。
	auto large = Images::FromImageInMemory(image, "JPG");
	large.location.setSize(display.width(), display.height());
	return owner->photo(
		id,
		uint64(),
		QByteArray(),
		date,
		storyboard->getDC(),
		false,
		QByteArray(),
		ImageWithLocation(),
		ImageWithLocation(),
		large,
		ImageWithLocation(),
		ImageWithLocation(),
		crl::time(0));
}

// 点击说明文字里的链接，从这一帧的时间开始播放视频。
[[nodiscard]] TextWithEntities frameCaption(
		not_null<DocumentData*> document,
		FullMsgId itemId,
		TimeId time) {
	return Ui::Text::Link(
		tr::extras_VideoPreviewPlayFrom(
			tr::now,
			lt_time,
			Ui::FormatDurationText(time)),
		HistoryView::TimestampLinkBase(document, itemId)
			+ QString::number(time));
}

// 各帧挂到消息的即时预览图集上，查看器据此左右切换并显示底部缩略图。
void openFrames(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document,
		const Storyboard &storyboard) {
	const auto storyboardFile = document->video()->storyboard;
	const auto display = displaySize(document, storyboard.frameSize);
	auto first = std::shared_ptr<Data::PhotoMedia>();
	for (auto i = 0; i != int(storyboard.frames.size()); ++i) {
		const auto &frame = storyboard.frames[i];
		const auto image = storyboard.sprite.copy(frame.source);
		const auto photo = framePhoto(
			storyboardFile,
			i,
			item->date(),
			image,
			display);
		item->addPhotoForInstantView(
			photo,
			frameCaption(document, item->fullId(), frame.time));
		if (first) {
			continue;
		}
		// 首帧直接放入解码好的图片，打开时不必等内存解码。
		first = photo->createMediaView();
		if (!first->loaded()) {
			using Data::PhotoSize;
			first->set(PhotoSize::Large, PhotoSize::Large, image, {});
		}
	}
	controller->openPhoto(first->owner(), {
		.id = item->fullId(),
		.topicRootId = item->topicRootId(),
		.monoforumPeerId = item->sublistPeerId(),
	});
}

} // namespace

bool hasPreview(not_null<DocumentData*> document) {
	const auto data = document->video();
	return data && data->storyboard && data->storyboardMap;
}

void showFrames(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document) {
	Expects(hasPreview(document));

	const auto itemId = item->fullId();
	const auto data = document->video();
	const auto origin = Data::FileOrigin(itemId);
	const auto sprite = data->storyboard->createMediaView();
	const auto map = data->storyboardMap->createMediaView();
	for (const auto &media : { sprite, map }) {
		const auto file = media->owner();
		// 拼图与索引只有几十 KB，存进本地缓存，再次打开不必重新下载。
		file->forceToCache(true);
		file->save(origin, QString());
	}
	const auto ready = [=] {
		return sprite->loaded() && map->loaded();
	};
	if (!ready()) {
		controller->showToast(tr::extras_VideoPreviewLoading(tr::now));
	}
	rpl::single() | rpl::then(
		document->session().downloaderTaskFinished()
	) | rpl::filter(ready) | rpl::take(1) | rpl::on_next([=] {
		// 下载期间消息可能被删除，完成时重新查找。
		const auto item = document->owner().message(itemId);
		if (!item) {
			return;
		}
		const auto storyboard = parseStoryboard(
			map->bytes(),
			Images::Read({ .content = sprite->bytes() }).image);
		if (!storyboard) {
			controller->showToast(
				tr::extras_VideoPreviewUnavailable(tr::now));
			return;
		}
		openFrames(controller, item, document, *storyboard);
	}, controller->lifetime());
}

} // namespace ExtrasVideoPreview
