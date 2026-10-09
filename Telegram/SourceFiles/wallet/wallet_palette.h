/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/style/style_core_palette.h"

class QWidget;

namespace Wallet {

[[nodiscard]] style::main_palette::Override WindowPaletteScope(
	not_null<const QWidget*> widget);

} // namespace Wallet
