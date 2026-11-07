/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class AbstractButton;
class RpWidget;
class VerticalLayout;
} // namespace Ui

namespace Wallet {

class AddressChips final {
public:
	[[nodiscard]] static std::optional<AddressChips> FromAddress(
		const QString &address);

	[[nodiscard]] QRect rect(QRect line) const;
	void paint(QPainter &p, QRect line, bool over = false) const;

private:
	explicit AddressChips(std::array<QString, 3> texts);

	std::array<QString, 3> _texts;

};

[[nodiscard]] not_null<Ui::AbstractButton*> CreateAddressCopyButton(
	not_null<Ui::RpWidget*> parent,
	const QString &address,
	Fn<void()> copied,
	Fn<void()> repaint = nullptr);

[[nodiscard]] not_null<Ui::RpWidget*> AddHistoryRowAddress(
	not_null<Ui::VerticalLayout*> container,
	const QString &address,
	not_null<Ui::RpWidget*> row,
	Fn<void()> copied);

} // namespace Wallet
