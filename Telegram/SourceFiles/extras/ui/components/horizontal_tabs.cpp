#include "extras/ui/components/horizontal_tabs.h"

#include "extras/extras_settings.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/pill_tabs.h"
#include "styles/style_widgets.h"

namespace ExtrasUi::HorizontalTabs {
namespace {

bool darkBackground() {
	return st::windowBg->c.lightnessF() < .5;
}

} // namespace

bool appliesTo(const style::SettingsSlider &st) {
	// 底部指示线和整段背景是标签，顶部刻度用于数值选择。
	return st.barSnapToLabel || st.barStroke == st.height;
}

bool underline() {
	return ExtrasSettings::getInstance().horizontalTabStyle()
		== HorizontalTabStyle::Underline;
}

bool softPill() {
	return ExtrasSettings::getInstance().horizontalTabStyle()
		== HorizontalTabStyle::SoftPill;
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
	const auto verticalInset = compact
		? std::clamp((bounds.height() - style::ConvertScale(20)) / 2, 0, inset)
		: inset;
	auto result = bounds.marginsRemoved(
		QMargins(inset, verticalInset, inset, verticalInset));
	if (compact) {
		const auto height = std::min(result.height(), style::ConvertScale(32));
		result.setTop(bounds.y() + (bounds.height() - height) / 2);
		result.setHeight(height);
	}
	return result;
}

void apply(not_null<Ui::PillTabs*> tabs, const style::PillTabs &st) {
	const auto refresh = [=, &st] {
		tabs->setTabPainter([&st](QPainter &p, QRect rect, const QString &text, bool active) {
			paint(p, rect, active, false, true, st.textStyle.font->width(text));
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
		const auto radius = softPill() ? rect.height() / 2 : style::ConvertScale(7);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawRoundedRect(rect, radius, radius);
	});
}

QColor foreground(QColor normal, float64 active) {
	return anim::color(normal,
		st::windowActiveTextFg->c,
		std::clamp(active, 0., 1.));
}

QColor ripple(float64) {
	auto color = st::windowActiveTextFg->c;
	color.setAlphaF(.12);
	return color;
}

void paint(
		QPainter &p,
		QRect bounds,
		float64 active,
		bool hovered,
		bool compact,
		int contentWidth) {
	const auto rect = QRectF(backgroundRect(bounds, compact));
	if (rect.isEmpty()) {
		return;
	}
	active = std::clamp(active, 0., 1.);
	p.save();
	const auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	if (underline()) {
		if (hovered) {
			auto hover = st::windowActiveTextFg->c;
			hover.setAlphaF(.045);
			p.setBrush(hover);
			p.drawRoundedRect(rect, style::ConvertScale(7), style::ConvertScale(7));
		}
		const auto lineWidth = (contentWidth < 0)
			? rect.width()
			: std::min(float64(contentWidth), rect.width());
		const auto thickness = style::ConvertScale(2);
		auto line = st::windowActiveTextFg->c;
		line.setAlphaF(active);
		p.setBrush(line);
		p.drawRoundedRect(QRectF(
			bounds.x() + (bounds.width() - lineWidth) / 2.,
			bounds.y() + bounds.height() - thickness,
			lineWidth,
			thickness), thickness / 2., thickness / 2.);
		p.restore();
		return;
	}
	const auto radius = rect.height() / 2.;
	auto fill = st::windowActiveTextFg->c;
	fill.setAlphaF(active * (darkBackground() ? .16 : .10));
	if (hovered) {
		fill.setAlphaF(std::max(float64(fill.alphaF()), .055));
	}
	p.setBrush(fill);
	p.drawRoundedRect(rect, radius, radius);
	p.restore();
}

} // namespace ExtrasUi::HorizontalTabs
