/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"

namespace Window {
class Controller;
} // namespace Window

namespace Settings {

class IntroSettingsBox final : public Ui::BoxContent {
public:
	IntroSettingsBox(QWidget*, not_null<Window::Controller*> window);

protected:
	void prepare() override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	const not_null<Window::Controller*> _window;

};

} // namespace Settings
