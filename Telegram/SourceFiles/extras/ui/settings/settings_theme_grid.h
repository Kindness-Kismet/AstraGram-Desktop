#pragma once

namespace Ui {
class RpWidget;
class Radiobutton;
} // namespace Ui

namespace Window {
class Controller;
} // namespace Window

namespace Extras::ThemeGrid {

void setupThemeGrid(
	not_null<Window::Controller*> window,
	not_null<Ui::RpWidget*> container,
	std::vector<not_null<Ui::Radiobutton*>> embedded,
	bool includeLocal);

} // namespace Extras::ThemeGrid
