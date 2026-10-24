#include "extras/ui/components/smooth_scroll.h"
#include "extras/extras_settings.h"

#include "ui/ui_utility.h"
#include "ui/widgets/elastic_scroll.h"

#include <QtGui/QWheelEvent>

namespace ExtrasUi {
namespace {

constexpr auto kWheelDuration = crl::time(180);

struct ScrollAnimation {
	Ui::Animations::Simple animation;
	float64 target = 0.;
	bool applying = false;
	bool enabled = true;
};

} // namespace

void setupSmoothScroll(not_null<Ui::ElasticScroll*> scroll, Fn<void()> wheelStarted) {
	const auto state = scroll->lifetime().make_state<ScrollAnimation>();
	ExtrasSettings::getInstance().smoothScrollingValue() | rpl::on_next([=](bool enabled) {
		state->enabled = enabled;
		if (!enabled) {
			state->animation.stop();
		}
	}, scroll->lifetime());
	scroll->setCustomWheelProcess([=](not_null<QWheelEvent*> event) {
		if (wheelStarted) {
			wheelStarted();
		}
		if (!state->enabled
			|| anim::Disabled()
			|| event->phase() != Qt::NoScrollPhase
			|| !event->pixelDelta().isNull()
			|| event->device()->capabilities().testFlag(QInputDevice::Capability::PixelScroll)
			|| scroll->position().overscroll) {
			state->animation.stop();
			return false;
		}
		const auto pixels = Ui::ScrollDeltaF(event);
		if (std::abs(pixels.x()) >= std::abs(pixels.y())) {
			return false;
		}
		const auto delta = -pixels.y()
			* ((event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))
				? std::max(scroll->height(), 120) / 120. : 1.);
		const auto current = scroll->scrollTop();
		const auto remaining = state->animation.animating()
			? state->target - current : 0.;
		// 换向时舍弃旧方向的余量，连续同向滚动才累加目标。
		const auto from = (delta * remaining > 0.) ? state->target : current;
		state->target = std::clamp(from + delta, 0., float64(scroll->scrollTopMax()));
		if (state->target == current) {
			state->animation.stop();
			return false;
		}
		state->animation.start([=] {
			state->applying = true;
			scroll->scrollToY(int(std::round(state->animation.value(state->target))));
			state->applying = false;
		}, current, state->target, kWheelDuration, anim::easeOutCubic);
		return true;
	});
	scroll->scrolls() | rpl::on_next([=] {
		if (!state->applying) {
			state->animation.stop();
		}
	}, scroll->lifetime());
	const auto interrupt = [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Hide
			|| event->type() == QEvent::MouseButtonPress
			|| event->type() == QEvent::KeyPress
			|| event->type() == QEvent::TouchBegin) {
			state->animation.stop();
		}
	};
	scroll->events() | rpl::on_next(interrupt, scroll->lifetime());
	for (const auto child : scroll->children()) {
		if (const auto bar = dynamic_cast<Ui::ElasticScrollBar*>(child)) {
			bar->events() | rpl::on_next(interrupt, scroll->lifetime());
		}
	}
}

} // namespace ExtrasUi
