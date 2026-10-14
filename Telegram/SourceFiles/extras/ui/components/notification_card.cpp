#include "extras/ui/components/notification_card.h"

#include "extras/extras_settings.h"
#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "window/main_window.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"

namespace ExtrasUi::NotificationCard {
namespace {

// 预览里的一行示例：应用图标、名称、小圆点与示例正文。
void paintSampleRow(
		QPainter &p,
		const QRect &row,
		const RowLayout &layout,
		const QPixmap &userpic,
		int rightSkip) {
	p.drawPixmap(row.topLeft() + layout.photoPos, userpic);
	const auto left = row.x() + layout.textLeft;
	const auto top = row.y() + layout.textTop;
	const auto available = row.width()
		- layout.textLeft
		- st::notifyRightSkip
		- rightSkip;
	const auto title = st::semiboldFont->elided(u"AstraGram"_q, available);
	p.setFont(st::semiboldFont);
	p.setPen(st::dialogsNameFg);
	p.drawText(left, top + st::semiboldFont->ascent, title);
	p.setFont(st::dialogsTextFont);
	p.setPen(st::dialogsTextFgService);
	p.drawText(
		left,
		top + st::semiboldFont->height + st::dialogsTextFont->ascent,
		st::dialogsTextFont->elided(
			tr::lng_notification_sample(tr::now),
			available));

	const auto size = st::notifyDotSize;
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::dialogsUnreadBg);
	p.drawEllipse(QRect(
		left + st::semiboldFont->width(title) + st::notifyDotSkip,
		top + (st::semiboldFont->height - size) / 2,
		size,
		size));
}

} // namespace

int cardWidth(Core::Settings::ScreenCorner corner) {
	return Core::Settings::IsTopCenterCorner(corner)
		? int(st::notifyWidth * 1.5)
		: st::notifyWidth;
}

RowLayout rowLayout() {
	const auto height = st::notifyMinHeight;
	const auto photo = st::notifyPhotoSize;
	const auto left = st::notifyPhotoPos.x();
	const auto lines = st::semiboldFont->height + st::dialogsTextFont->height;
	return {
		.height = height,
		.photoSize = photo,
		.photoPos = QPoint(left, (height - photo) / 2),
		.textLeft = left + photo + st::notifyTextLeft,
		.textTop = (height - lines) / 2,
	};
}

QColor actionColor(int alpha) {
	const auto dark = st::notificationBg->c.lightness() < 128;
	auto result = dark ? QColor(255, 255, 255) : QColor(0x21, 0x21, 0x21);
	result.setAlpha(alpha);
	return result;
}

int chipLeftSkip(int full, int width, Core::Settings::ScreenCorner corner) {
	const auto free = full - width;
	if (Core::Settings::IsTopCenterCorner(corner)) {
		return free / 2;
	}
	return (Core::Settings::IsLeftCorner(corner) == rtl()) ? free : 0;
}

int hideAllChipWidth() {
	return 2 * st::notifyHideAllPadding
		+ st::notifyHideAllFont->width(tr::lng_notification_hide_all(tr::now));
}

void paintCard(QPainter &p, const QRect &card, bool highlighted) {
	const auto radius = std::min(st::notifyRadius, card.height() / 2);
	const auto dark = st::notificationBg->c.lightness() < 128;
	const auto border = QColor(255, 255, 255, dark ? 31 : 178);
	const auto half = st::notifyBorderWidth / 2.;

	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(highlighted ? st::lightButtonBgOver : st::notificationBg);
	p.drawRoundedRect(card, radius, radius);
	p.setPen(QPen(border, st::notifyBorderWidth));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(
		QRectF(card).marginsRemoved({ half, half, half, half }),
		radius - half,
		radius - half);
}

void paintStackBadge(QPainter &p, int right, int titleTop, int more) {
	const auto text = u"+"_q + QString::number(more);
	const auto &font = st::notifyStackCountFont;
	const auto h = st::notifyStackCountHeight;
	const auto w = std::max(
		st::notifyStackCountMinWidth,
		font->width(text) + 2 * st::notifyStackCountPadding);
	const auto badge = QRect(
		right - st::notifyStackCountRight - w,
		titleTop + (st::semiboldFont->height - h) / 2,
		w,
		h);
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(actionColor(23));
		p.drawRoundedRect(badge, h / 2., h / 2.);
	}
	p.setFont(font);
	p.setPen(st::dialogsTextFg);
	p.drawText(badge, text, style::al_center);
}

void paintHideAllChip(QPainter &p, const QRect &chip, bool over) {
	const auto label = tr::lng_notification_hide_all(tr::now);
	const auto &font = st::notifyHideAllFont;
	const auto left = chip.x() + st::notifyHideAllPadding;
	const auto top = chip.y()
		+ (chip.height() - font->height) / 2
		+ font->ascent;
	paintCard(p, chip, over);
	p.setFont(font);
	p.setPen(over ? st::lightButtonFgOver : st::lightButtonFg);
	p.drawText(left, top, label);
}

QImage prepareSample(int count, Core::Settings::ScreenCorner corner) {
	const auto stack = (ExtrasSettings::getInstance().notificationStyle()
		== NotificationStyle::Stack);
	const auto layout = rowLayout();
	const auto fromTop = Core::Settings::IsTopCorner(corner);
	const auto width = cardWidth(corner);
	const auto rows = stack ? 1 : count;
	const auto peeks = stack ? std::min(count - 1, kStackDepth) : 0;
	const auto skip = st::notifySkip;
	const auto withHideAll = !stack && (count > 1);
	const auto height = rows * (layout.height + skip) - skip
		+ peeks * st::notifyStackPeek
		+ (withHideAll ? (skip + st::notifyHideAllHeight) : 0);

	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		QSize(width, height) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);

	auto userpic = Ui::PixmapFromImage(Window::LogoNoMargin().scaled(
		layout.photoSize * ratio,
		layout.photoSize * ratio,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation));
	userpic.setDevicePixelRatio(ratio);

	// 按离屏幕角的距离换算成图片里的位置。
	const auto place = [&](int from, int size) {
		return QRect(0, fromTop ? from : (height - from - size), width, size);
	};
	const auto badge = stack && (count > 1);
	auto p = QPainter(&result);
	for (auto i = peeks; i != 0; --i) {
		const auto inset = int(base::SafeRound(width * kStackShrink / 2. * i));
		paintCard(p, place(i * st::notifyStackPeek, layout.height)
			.marginsRemoved({ inset, 0, inset, 0 }));
	}
	auto from = 0;
	for (auto i = 0; i != rows; ++i) {
		const auto row = place(from, layout.height);
		from += layout.height + skip;
		paintCard(p, row);
		paintSampleRow(
			p,
			row,
			layout,
			userpic,
			badge
				? (st::notifyStackCountMinWidth + st::notifyStackCountPadding)
				: 0);
		if (badge) {
			paintStackBadge(p, width, row.y() + layout.textTop, count - 1);
		}
	}
	if (withHideAll) {
		const auto area = place(from, st::notifyHideAllHeight);
		const auto chip = hideAllChipWidth();
		paintHideAllChip(
			p,
			QRect(
				chipLeftSkip(width, chip, corner),
				area.y(),
				chip,
				area.height()),
			false);
	}
	p.end();
	return result;
}

} // namespace ExtrasUi::NotificationCard
