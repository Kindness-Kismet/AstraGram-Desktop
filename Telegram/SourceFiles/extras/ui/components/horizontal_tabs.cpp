#include "extras/ui/components/horizontal_tabs.h"

#include "extras/extras_settings.h"
#include "ui/color_contrast.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/pill_tabs.h"
#include "styles/style_widgets.h"

namespace ExtrasUi::HorizontalTabs {
namespace {

QColor selectedBackground() {
	const auto accent = st::windowActiveTextFg->c;
	static auto previous = QColor();
	static auto result = QColor();
	if (previous != accent) {
		previous = result = accent;
		// 保持强调色色相，让白字在浅色强调色上也能看清。
		while (Ui::CountContrast(result, QColor(Qt::white)) < 4.5) {
			result = result.darker(105);
		}
	}
	return result;
}

} // namespace

bool enabled() {
	return ExtrasSettings::getInstance().horizontalTabStyle()
		!= HorizontalTabStyle::Default;
}

bool enabled(const style::SettingsSlider &st) {
	// 底部指示线和整段背景是标签，顶部刻度用于数值选择。
	return enabled() && (st.barSnapToLabel || st.barStroke == st.height);
}

bool solid() {
	return ExtrasSettings::getInstance().horizontalTabStyle()
		== HorizontalTabStyle::Solid;
}

void watch(not_null<Ui::RpWidget*> widget, Fn<void()> invalidate) {
	const auto refresh = [=] {
		if (invalidate) {
			invalidate();
		}
		widget->update();
	};
	ExtrasSettings::getInstance().horizontalTabStyleChanges(
	) | rpl::on_next(refresh, widget->lifetime());
	style::PaletteChanged() | rpl::on_next(refresh, widget->lifetime());
}

QRect backgroundRect(QRect bounds, bool compact) {
	const auto inset = style::ConvertScale(4);
	auto result = bounds.marginsRemoved(QMargins(inset, inset, inset, inset));
	if (compact) {
		const auto height = std::min(result.height(), style::ConvertScale(solid() ? 38 : 34));
		result.setTop(bounds.y() + (bounds.height() - height) / 2);
		result.setHeight(height);
	}
	return result;
}

void apply(not_null<Ui::PillTabs*> tabs, const style::PillTabs &st) {
	const auto refresh = [=, &st] {
		if (!enabled()) {
			tabs->setTabPainter(nullptr);
			return;
		}
		tabs->setTabPainter([&st](QPainter &p, QRect rect, const QString &text, bool active) {
			paint(p, rect, active);
			p.setFont(st.textStyle.font);
			p.setPen(foreground(st.fg->c, active));
			p.drawText(rect, st.textStyle.font->elided(text, rect.width() - st.height),
				style::al_center);
		});
	};
	watch(tabs, refresh);
	refresh();
}

QImage rippleMask(QSize size, bool compact) {
	return Ui::RippleAnimation::MaskByDrawer(size, false, [&](QPainter &p) {
		const auto rect = backgroundRect(QRect(QPoint(), size), compact);
		const auto radius = solid() ? rect.height() / 2 : style::ConvertScale(8);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawRoundedRect(rect, radius, radius);
	});
}

QColor foreground(QColor normal, float64 active) {
	return anim::color(normal,
		solid() ? QColor(Qt::white) : st::windowActiveTextFg->c,
		std::clamp(active, 0., 1.));
}

QColor ripple(float64 active) {
	auto color = (solid() && active > .5)
		? QColor(Qt::white)
		: st::windowActiveTextFg->c;
	color.setAlphaF(.16);
	return color;
}

void paint(QPainter &p, QRect bounds, float64 active, bool hovered, bool compact) {
	const auto rect = QRectF(backgroundRect(bounds, compact));
	if (rect.isEmpty()) {
		return;
	}
	active = std::clamp(active, 0., 1.);
	const auto filled = solid();
	const auto radius = filled ? rect.height() / 2. : style::ConvertScale(8);
	auto fill = filled ? selectedBackground() : st::windowActiveTextFg->c;
	fill.setAlphaF(filled ? active : active * .04);
	if (hovered) {
		fill.setAlphaF(std::max(float64(fill.alphaF()), .07));
	}
	p.save();
	const auto hq = PainterHighQualityEnabler(p);
	p.setBrush(fill);
	if (filled) {
		p.setPen(Qt::NoPen);
	} else {
		auto border = st::windowSubTextFg->c;
		border.setAlphaF(.25);
		p.setPen(QPen(anim::color(border, st::windowActiveTextFg->c, active),
			st::lineWidth * (1. + active * .5)));
	}
	const auto inset = st::lineWidth * .75;
	p.drawRoundedRect(rect.adjusted(inset, inset, -inset, -inset), radius, radius);
	p.restore();
}

} // namespace ExtrasUi::HorizontalTabs
