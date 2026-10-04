/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "settings/settings_type.h"

template <typename Flags>
struct EditFlagsDescriptor;

namespace PowerSaving {
enum Flag : uint32;
using Flags = base::flags<Flag>;
} // namespace PowerSaving

namespace Ui {
class GenericBox;
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

[[nodiscard]] Type PowerSavingId();
void ShowPowerSaving(
	not_null<Window::SessionController*> controller,
	PowerSaving::Flags highlightFlags = PowerSaving::Flags());

// 登录页尚无会话，继续使用可独立打开的设置窗口。
void PowerSavingBox(
	not_null<Ui::GenericBox*> box,
	PowerSaving::Flags highlightFlags = PowerSaving::Flags());

[[nodiscard]] EditFlagsDescriptor<PowerSaving::Flags> PowerSavingLabels();

} // namespace Settings
