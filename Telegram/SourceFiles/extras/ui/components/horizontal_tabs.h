#pragma once

#include "base/basic_types.h"
#include <QtCore/QRect>
#include <QtGui/QColor>

class QPainter;
class QImage;
namespace Ui { class RpWidget; class PillTabs; }
namespace style { struct SettingsSlider; struct PillTabs; }

namespace ExtrasUi::HorizontalTabs {

[[nodiscard]] bool enabled();
[[nodiscard]] bool enabled(const style::SettingsSlider &st);
[[nodiscard]] bool solid();
void watch(not_null<Ui::RpWidget*> widget, Fn<void()> invalidate = nullptr);
void apply(not_null<Ui::PillTabs*> tabs, const style::PillTabs &st);
[[nodiscard]] QRect backgroundRect(QRect bounds, bool compact = true);
[[nodiscard]] QImage rippleMask(QSize size, bool compact = true);
[[nodiscard]] QColor foreground(QColor normal, float64 active);
[[nodiscard]] QColor ripple(float64 active);
void paint(QPainter &p, QRect bounds, float64 active, bool hovered = false, bool compact = true);

} // namespace ExtrasUi::HorizontalTabs
