/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_type.h"

namespace Data {
namespace AutoDownload {
enum class Source;
} // namespace AutoDownload
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

[[nodiscard]] Type AutoDownloadId(Data::AutoDownload::Source source);
void ShowAutoDownload(
	not_null<Window::SessionController*> controller,
	Data::AutoDownload::Source source,
	Fn<void()> closed = nullptr);

} // namespace Settings
