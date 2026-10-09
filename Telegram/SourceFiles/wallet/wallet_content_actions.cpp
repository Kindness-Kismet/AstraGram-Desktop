#include "wallet/wallet_content_internal.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

[[nodiscard]] bool HasDetailsComment(const TransferItem &item) {
	return item.commentEncrypted || !item.comment.trimmed().isEmpty();
}

[[nodiscard]] bool SameDetailsHeader(
		const TransferItem &a,
		const TransferItem &b) {
	return (ShowsCollectible(a) == ShowsCollectible(b))
		&& (a.collectible == b.collectible)
		&& (a.amountNano == b.amountNano)
		&& (a.incoming == b.incoming)
		&& (a.status == b.status)
		&& (a.comment == b.comment)
		&& (a.commentEncrypted == b.commentEncrypted)
		&& (a.encryptedFormat == b.encryptedFormat)
		&& (a.encryptedPayload == b.encryptedPayload);
}

void AddDetailsComment(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::VerticalLayout*> layout,
		std::shared_ptr<Main::SessionShow> show,
		const TransferItem &item,
		Fn<bool()> originCurrent) {
	if (!HasDetailsComment(item)) {
		return;
	}
	const auto comment = item.comment.trimmed();
	auto label = item.commentEncrypted
		? object_ptr<Ui::FlatLabel>(object_ptr<EncryptedCommentLabel>(
			layout,
			box,
			std::move(show),
			item,
			std::move(originCurrent)))
		: object_ptr<Ui::FlatLabel>(layout, comment, st::walletCommentLabel);
	// The bubble stands inside the gap between the amount and the table
	// rather than under the amount: the header leaves half of that gap and
	// the bubble takes the other half, so it reads as its own line.
	layout->add(
		MakeCommentBubble(layout, std::move(label), st::windowBg),
		style::margins(
			st::giveawayGiftCodeTableMargin.left(),
			0,
			st::giveawayGiftCodeTableMargin.right(),
			st::walletDetailsAmountBottomSkip / 2),
		style::al_top);
}

[[nodiscard]] int GaslessDailyTransfers(not_null<Main::Session*> session) {
	return session->appConfig().get<int>(
		u"wallet_gasless_daily_transfers"_q,
		kGaslessDailyTransfersDefault);
}

[[nodiscard]] rpl::producer<int> GaslessDailyTransfersValue(
		not_null<Main::Session*> session) {
	return session->appConfig().value() | rpl::map([=] {
		return GaslessDailyTransfers(session);
	}) | rpl::distinct_until_changed();
}

// The fee itself is shown in the row that opens this box.
void ShowNetworkFeesAbout(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session) {
	show->showBox(Ui::MakeInformBox({
		.text = tr::lng_wallet_fees_text(
			tr::now,
			lt_count,
			GaslessDailyTransfers(session)),
		.title = tr::lng_wallet_fees_title(),
	}));
}

[[nodiscard]] TextWithEntities GramMark(
		Ui::Text::CustomEmojiHelper &helper,
		const style::font &font) {
	auto descriptor = Ui::Text::PaletteDependentEmoji{
		.factory = [=] {
			return Ui::Earn::IconCurrencyTwoTone(
				font,
				st::windowActiveTextFg->c);
		},
	};
	const auto image = descriptor.factory();
	const auto alignedTop = Ui::Earn::AlignedMarkTop(font, image);
	const auto emojiY = (font->height - st::emojiSize) / 2;
	const auto lineShift = Ui::Fixed(font->ascent) - font->fascent;
	const auto naturalTop = (lineShift + emojiY).toInt()
		+ Ui::Emoji::GetCustomSkipNormal();
	const auto marginTop = int(base::SafeRound(alignedTop - naturalTop));
	descriptor.margin = QMargins(0, marginTop, 0, 0);
	return helper.paletteDependent(std::move(descriptor));
}

void AddPendingFeeTableRow(
		not_null<Ui::TableLayout*> table,
		DetailsFee state) {
	Expects(state != DetailsFee::Known);

	if (state == DetailsFee::Failed) {
		Ui::AddTableRow(
			table,
			tr::lng_wallet_details_fee(),
			tr::lng_wallet_details_fee_unknown(tr::marked));
		return;
	}
	const auto &font = table->st().defaultValue.style.font;
	auto helper = Ui::Text::CustomEmojiHelper();
	const auto diamond = GramMark(helper, font);
	Ui::AddTableRow(
		table,
		tr::lng_wallet_details_fee(),
		tr::lng_contacts_loading(
			tr::italic
		) | rpl::map([=](TextWithEntities text) {
			auto result = diamond;
			result.append(QChar(' '));
			result.append(std::move(text));
			return result;
		}),
		helper.context());
}

void AddFeeTableRow(
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session,
		const TransferItem &item) {
	const auto &font = table->st().defaultValue.style.font;
	auto helper = Ui::Text::CustomEmojiHelper();
	const auto diamond = GramMark(helper, font);
	const auto feeNano = item.feeNano.value_or(0);
	auto value = rpl::producer<TextWithEntities>();
	if (item.gasless) {
		value = tr::lng_wallet_details_fee_free(
		) | rpl::map([=](const QString &text) {
			auto result = diamond;
			result.append(QChar(' '));
			result.append(text);
			return result;
		});
	} else {
		value = FiatRateValue(session) | rpl::map([=](const FiatRate &rate) {
			auto result = diamond;
			result.append(QChar(' '));
			result.append(Ui::FormatTonAmount(feeNano).full);
			result.append(QChar(' '));
			result.append(Ui::Text::Colorized(
				FormatFiat(feeNano, rate, kFeeFiatDecimals, true)));
			return result;
		});
	}
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		table,
		std::move(value),
		st::walletDetailsFeeLabel,
		st::defaultPopupMenu,
		helper.context());
	Ui::AddTableRow(
		table,
		tr::lng_wallet_details_fee(),
		Ui::MakeValueWithSmallButton(
			table,
			label,
			rpl::single(u"?"_q),
			[=](not_null<Ui::RpWidget*>) {
				ShowNetworkFeesAbout(show, session);
			}).widget);
}

// Userpic and name leading to the chat, and a Send pill for any user.
[[nodiscard]] object_ptr<Ui::RpWidget> PeerCounterpartyValue(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<Main::SessionShow> show,
		not_null<PeerData*> peer) {
	const auto chatShow = MakeChatShow(show, true);
	const auto user = peer->asUser();
	const auto offer = (user != nullptr);
	const auto weak = base::make_weak(box);
	return Ui::MakePeerTableValue(
		table,
		chatShow,
		peer->id,
		offer ? tr::lng_send_button() : nullptr,
		offer ? Fn<void()>([=] { ShowSendToUser(show, user); }) : nullptr,
		[=] {
			const auto window = chatShow->resolveWindow();
			if (!window) {
				return;
			} else if (weak && weak->hasDelegate()) {
				weak->closeBox();
			}
			window->showPeerHistory(peer);
			window->window().activate();
		});
}

void AddPeerCounterpartyRows(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<Main::SessionShow> show,
		not_null<PeerData*> peer,
		const TransferItem &item) {
	const auto address = !item.counterparty.isEmpty()
		? DetailsFriendlyAddress(item)
		: std::nullopt;
	const auto domain = item.counterpartyName.trimmed();
	auto label = (item.incoming
		? tr::lng_wallet_details_sender()
		: tr::lng_wallet_details_recipient());
	auto value = PeerCounterpartyValue(box, table, show, peer);
	if (address && !domain.isEmpty()) {
		auto wrap = object_ptr<Ui::VerticalLayout>(table);
		wrap->add(std::move(value));
		wrap->add(NameValueLabel(wrap.data(), show, domain, *address));
		value = std::move(wrap);
	}
	Ui::AddTableRow(
		table,
		std::move(label),
		std::move(value),
		st::giveawayGiftCodePeerMargin);
	if (address) {
		Ui::AddTableRow(
			table,
			tr::lng_wallet_details_address(),
			AddressValueLabel(table, show, *address));
	}
}

} // namespace ContentDetails

} // namespace Wallet
