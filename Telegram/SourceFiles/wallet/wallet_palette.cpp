/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_palette.h"

#include "ui/window_palette.h"

namespace Wallet {

style::main_palette::Override WindowPaletteScope(
		not_null<const QWidget*> widget) {
	return style::main_palette::Override(Ui::WindowPaletteFor(widget));
}

} // namespace Wallet
