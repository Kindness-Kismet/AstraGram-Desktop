#include "extras/features/media_viewer/viewer_layout.h"

#include "ui/text/text.h"
#include "styles/style_basic.h"
#include "styles/style_extras_styles.h"
#include "styles/style_media_view.h"

namespace ExtrasFeatures::MediaViewer {

int topBarHeight() {
	return st::extrasViewerTopBarHeight;
}

QRect topBarButtonRect(int width, int top, int index) {
	const auto size = st::extrasViewerTopBarButton;
	const auto skip = st::extrasViewerTopBarButtonSkip;
	auto right = width
		- st::extrasViewerTopBarPadding.right()
		- (index - 1) * (size + skip);
	if (index > kTopBarGroupSize) {
		// 两组之间的间距换成分隔线及其两侧留白。
		right -= 2 * st::extrasViewerTopBarSeparatorSkip
			+ st::extrasViewerTopBarSeparator.width()
			- skip;
	}
	return QRect(right - size, top + (topBarHeight() - size) / 2, size, size);
}

QRect topBarSeparatorRect(int width, int top) {
	const auto group = topBarButtonRect(width, top, kTopBarGroupSize);
	const auto size = st::extrasViewerTopBarSeparator;
	return QRect(
		group.x() - st::extrasViewerTopBarSeparatorSkip - size.width(),
		top + (topBarHeight() - size.height()) / 2,
		size.width(),
		size.height());
}

TopBarHeader layoutTopBarHeader(const TopBarHeaderArgs &args) {
	const auto &nameFont = st::semiboldFont;
	const auto &dateFont = st::extrasViewerDateFont;
	auto result = TopBarHeader();
	auto left = st::extrasViewerTopBarPadding.left();
	if (args.userpic) {
		const auto size = st::extrasViewerUserpicSize;
		result.userpic = QRect(
			left,
			args.top + (topBarHeight() - size) / 2,
			size,
			size);
		left += size + st::extrasViewerUserpicSkip;
	}
	const auto available = std::max(args.right - left, 0);
	const auto hasName = (args.nameWidth > 0);
	const auto textHeight = (hasName ? nameFont->height : 0)
		+ dateFont->height;
	auto top = args.top + (topBarHeight() - textHeight) / 2;
	if (hasName) {
		result.name = QRect(
			left,
			top,
			std::min(args.nameWidth, available),
			nameFont->height);
		top += nameFont->height;
	}
	const auto dateWidth = std::min(dateFont->width(args.date), available);
	result.date = QRect(left, top, dateWidth, dateFont->height);
	if (args.header.isEmpty()) {
		return result;
	}
	auto x = left + dateWidth;
	if (dateWidth > 0) {
		const auto skip = st::mediaviewTextSkipHalf;
		const auto bullet = dateFont->width(Ui::kQBullet);
		result.separator = QRect(x + skip, top, bullet, dateFont->height);
		x += skip + bullet + skip;
	}
	const auto headerAvailable = left + available - x;
	if (headerAvailable <= 0) {
		result.separator = QRect();
		return result;
	}
	result.headerText = dateFont->elided(
		args.header,
		headerAvailable,
		Qt::ElideMiddle);
	result.header = QRect(
		x,
		top,
		dateFont->width(result.headerText),
		dateFont->height);
	return result;
}

} // namespace ExtrasFeatures::MediaViewer
