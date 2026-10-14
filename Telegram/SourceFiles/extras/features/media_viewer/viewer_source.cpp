#include "extras/features/media_viewer/viewer_source.h"

#include "data/data_groups.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_inner_widget.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/media/history_view_media.h"
#include "styles/style_chat.h"

namespace ExtrasFeatures::MediaViewer {
namespace {

[[nodiscard]] std::optional<MediaSource> findIn(
		not_null<HistoryInner*> inner,
		not_null<HistoryItem*> item) {
	// 相册只有最后一条消息带视图，各条消息对应其中一个格子。
	const auto group = item->history()->owner().groups().find(item);
	const auto leader = group ? group->items.back().get() : item.get();
	const auto view = leader->mainView();
	const auto media = view ? view->media() : nullptr;
	if (!media) {
		return std::nullopt;
	}
	const auto top = inner->itemTop(view);
	if (top < 0) {
		return std::nullopt;
	}
	auto local = QRect(view->mediaTopLeft(), media->currentSize());
	auto radius = st::bubbleRadiusLarge;
	if (group) {
		const auto i = ranges::find(group->items, item);
		const auto cell = media->groupItemRect(int(i - begin(group->items)));
		if (cell.isEmpty()) {
			return std::nullopt;
		}
		local = cell.translated(view->mediaTopLeft());
		radius = st::bubbleRadiusSmall;
	}
	const auto geometry = local.translated(
		inner->mapToGlobal(QPoint(0, top)));
	const auto viewport = inner->parentWidget();
	const auto visible = QRect(
		viewport->mapToGlobal(QPoint()),
		viewport->size());
	if (!visible.intersects(geometry)) {
		return std::nullopt;
	}
	return MediaSource{ .geometry = geometry, .radius = radius };
}

} // namespace

std::optional<MediaSource> findMediaSource(
		not_null<QWidget*> host,
		not_null<HistoryItem*> item) {
	for (const auto widget : host->findChildren<QWidget*>()) {
		const auto inner = dynamic_cast<HistoryInner*>(widget);
		if (!inner || !inner->isVisible()) {
			continue;
		}
		if (const auto result = findIn(inner, item)) {
			return result;
		}
	}
	return std::nullopt;
}

} // namespace ExtrasFeatures::MediaViewer
