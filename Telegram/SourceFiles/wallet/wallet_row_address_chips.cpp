/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_row_address_chips.h"

#include "wallet/wallet_palette.h"

#include "lang/lang_keys.h"
#include "ui/abstract_button.h"
#include "ui/effects/animation_value.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"

#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

namespace Wallet {
namespace {

constexpr auto kChipChars = 4;
constexpr auto kMiddleChars = 3;
constexpr auto kFadedChip = 1;

struct ChipsGeometry {
	style::font font;
	QRect rect;
	float64 scale = 1.;
	std::array<int, 3> widths = {};
	int width = 0;
	int height = 0;
};

class AddressCopyButton final : public Ui::AbstractButton {
public:
	AddressCopyButton(
		QWidget *parent,
		QString address,
		Fn<void()> repaint)
	: AbstractButton(parent)
	, _address(std::move(address))
	, _repaint(std::move(repaint)) {
		setObjectName(u"walletAddressCopy"_q);
		setToolTip(tr::lng_wallet_receive_copy_hint(tr::now));
	}

	QString accessibilityName() override {
		return tr::lng_wallet_receive_copy_hint(tr::now) + ' ' + _address;
	}

protected:
	void onStateChanged(State was, StateChangeSource source) override {
		update();
		if (_repaint) {
			_repaint();
		}
	}

private:
	const QString _address;
	const Fn<void()> _repaint;

};

[[nodiscard]] ChipsGeometry ComputeChipsGeometry(
		const std::array<QString, 3> &texts,
		QRect line) {
	const auto font = st::walletRowAddressFont->monospace();
	auto result = ChipsGeometry{
		.font = font,
		.height = st::walletRowAddressHeight,
	};
	for (auto i = 0; i != int(texts.size()); ++i) {
		result.widths[i] = font->width(texts[i]);
		result.width += result.widths[i];
	}
	result.width += 2 * st::walletRowAddressChipInset
		+ 3 * st::walletRowAddressChipSkip
		+ st::walletRowAddressCopyIcon.width();
	result.scale = std::min({
		1.,
		std::max(line.width(), 0) / float64(result.width),
		std::max(line.height(), 0) / float64(result.height),
	});
	const auto width = int(base::SafeRound(result.width * result.scale));
	const auto height = int(base::SafeRound(result.height * result.scale));
	result.rect = QRect(
		style::RightToLeft() ? (line.x() + line.width() - width) : line.x(),
		line.y() + (line.height() - height) / 2,
		width,
		height);
	return result;
}

} // namespace

AddressChips::AddressChips(std::array<QString, 3> texts)
: _texts(std::move(texts)) {
}

std::optional<AddressChips> AddressChips::FromAddress(
		const QString &address) {
	const auto size = int(address.size());
	if (size < 2 * (kChipChars + kMiddleChars)) {
		return std::nullopt;
	}
	return AddressChips({
		address.left(kChipChars),
		(address.mid(kChipChars, kMiddleChars)
			+ QChar(0x2026)
			+ address.mid(size - kChipChars - kMiddleChars, kMiddleChars)),
		address.right(kChipChars),
	});
}

QRect AddressChips::rect(QRect line) const {
	return ComputeChipsGeometry(_texts, line).rect;
}

void AddressChips::paint(QPainter &p, QRect line, bool over) const {
	const auto geometry = ComputeChipsGeometry(_texts, line);
	if (geometry.rect.isEmpty()) {
		return;
	}
	const auto &font = geometry.font;
	const auto color = over
		? st::windowActiveTextFg->c
		: st::windowSubTextFg->c;
	const auto radius = st::walletRowAddressChipRadius;
	const auto baseline = (geometry.height - font->height) / 2 + font->ascent;
	p.save();
	p.translate(geometry.rect.topLeft());
	p.scale(geometry.scale, geometry.scale);
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(QPen(anim::color(st::windowBgOver, color, 0.15), 1.));
		p.setBrush(anim::color(
			st::windowBgOver,
			over ? st::windowActiveTextFg : st::windowBg,
			over ? 0.1 : 0.65));
		p.drawRoundedRect(
			QRectF(0.5, 0.5, geometry.width - 1., geometry.height - 1.),
			radius,
			radius);
	}
	p.setFont(font);
	p.setLayoutDirection(Qt::LeftToRight);
	auto left = st::walletRowAddressChipInset;
	for (auto i = 0; i != int(_texts.size()); ++i) {
		p.setPen(i == kFadedChip
			? anim::color(st::windowBgOver, color, 0.65)
			: color);
		p.drawText(left, baseline, _texts[i]);
		left += geometry.widths[i] + st::walletRowAddressChipSkip;
	}
	const auto &icon = st::walletRowAddressCopyIcon;
	icon.paintInCenter(
		p,
		QRect(left, 0, icon.width(), geometry.height),
		color);
	p.restore();
}

not_null<Ui::AbstractButton*> CreateAddressCopyButton(
		not_null<Ui::RpWidget*> parent,
		const QString &address,
		Fn<void()> copied,
		Fn<void()> repaint) {
	const auto button = Ui::CreateChild<AddressCopyButton>(
		parent,
		address,
		std::move(repaint));
	button->setClickedCallback(std::move(copied));
	return button;
}

not_null<Ui::RpWidget*> AddHistoryRowAddress(
		not_null<Ui::VerticalLayout*> container,
		const QString &address,
		not_null<Ui::RpWidget*> row,
		Fn<void()> copied) {
	const auto chips = AddressChips::FromAddress(address);
	Expects(chips.has_value());

	const auto slot = container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::walletRowAddressHeight));
	const auto button = CreateAddressCopyButton(row, address, std::move(copied));
	button->paintRequest() | rpl::on_next([=] {
		const auto scope = WindowPaletteScope(button);
		auto p = QPainter(button);
		chips->paint(p, button->rect(), button->isOver() || button->isDown());
	}, button->lifetime());
	rpl::combine(
		slot->geometryValue(),
		row->geometryValue()
	) | rpl::on_next([=] {
		button->setGeometry(Ui::MapFrom(row, slot, chips->rect(slot->rect())));
		button->raise();
	}, button->lifetime());
	return slot;
}

} // namespace Wallet
