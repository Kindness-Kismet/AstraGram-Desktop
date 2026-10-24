#include "extras/ui/components/chat_bars_surface.h"

#include "ui/chat/floating_bar.h"
#include "ui/rp_widget.h"
#include "styles/style_window.h"

#include <QtGui/QPainter>

namespace ExtrasUi {

ChatBarsSurface::ChatBarsSurface(not_null<Ui::RpWidget*> container)
: _container(container)
, _state(container->lifetime().make_state<State>()) {
	// 状态挂在容器生命周期上，表面回调不会比容器活得更久。
	const auto state = _state.get();
	FloatingSurface::attach(container.get(), {
		.radius = st::windowCardRadius,
		.background = chatSurfaceBackground,
		.border = chatSurfaceBorder,
		.borderWidth = st::lineWidth,
		.windowMaterial = true,
		.shapes = [=] { return state->shapes; },
	});
	container->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		if (state->dividers.empty()) {
			return;
		}
		auto p = QPainter(container.get());
		const auto color = chatSurfaceBorder();
		for (const auto &divider : state->dividers) {
			p.fillRect(divider.intersected(clip), color);
		}
	}, container->lifetime());
}

void ChatBarsSurface::apply(const Ui::ChatBarStack &stack) const {
	auto shapes = std::vector<FloatingSurfaceShape>();
	if (const auto card = stack.card(); !card.isEmpty()) {
		shapes.push_back({ card, st::windowCardRadius });
	}
	for (const auto &capsule : stack.capsules()) {
		shapes.push_back({ capsule, st::chatBarCapsuleHeight / 2 });
	}
	_state->shapes = std::move(shapes);
	if (_state->dividers != stack.dividers()) {
		_state->dividers = stack.dividers();
		_container->update();
	}
	const auto surface = FloatingSurface::find(_container.get());
	Expects(surface != nullptr);
	surface->refreshGeometry();
}

} // namespace ExtrasUi
