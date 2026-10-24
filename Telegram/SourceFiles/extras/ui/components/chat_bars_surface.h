#pragma once

#include "extras/ui/components/floating_surface.h"

#include <QtCore/QRect>
#include <vector>

namespace Ui {
class RpWidget;
class ChatBarStack;
} // namespace Ui

namespace ExtrasUi {

// 聊天顶部横条容器：一个表面画出内容卡片和动作胶囊，横条自身不再挂表面。
class ChatBarsSurface final {
public:
	explicit ChatBarsSurface(not_null<Ui::RpWidget*> container);

	// 每次排版后调用，同步表面形状与卡片内的分隔线。
	void apply(const Ui::ChatBarStack &stack) const;

private:
	struct State {
		std::vector<FloatingSurfaceShape> shapes;
		std::vector<QRect> dividers;
	};

	const not_null<Ui::RpWidget*> _container;
	const not_null<State*> _state;

};

} // namespace ExtrasUi
