/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace style {
struct FlatLabel;
} // namespace style

namespace Ui {
class AbstractButton;
class RpWidget;
class SettingsButton;
class VerticalLayout;
} // namespace Ui

namespace Window {

class SessionController;

// 页脚一行显示应用名和版本号，整体可点击，悬停提示构建信息。
[[nodiscard]] not_null<Ui::AbstractButton*> AddVersionButton(
	not_null<Ui::RpWidget*> parent,
	const style::FlatLabel &st);

[[nodiscard]] not_null<Ui::SettingsButton*> AddMyChannelsBox(
	not_null<Ui::SettingsButton*> button,
	not_null<SessionController*> controller,
	bool chats);

void SetupMenuBots(
	not_null<Ui::VerticalLayout*> container,
	not_null<SessionController*> controller,
	const style::color &iconFg);

} // namespace Window
