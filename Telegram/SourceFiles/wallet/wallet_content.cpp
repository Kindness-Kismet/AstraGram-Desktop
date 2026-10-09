#include "wallet/wallet_content_internal.h"

namespace Wallet {
using namespace ContentDetails;

Fn<void()> CopyTextCallback(
		std::shared_ptr<Ui::Show> show,
		QString text,
		QString toast) {
	return [=] {
		TextUtilities::SetClipboardText(TextForMimeData::Simple(text));
		show->showToast({
			.text = { toast },
			.iconLottie = u"toast/copy"_q,
			.iconLottieSize = st::toastLottieIconSize,
		});
	};
}

object_ptr<Ui::FlatLabel> AddressValueLabel(
		not_null<QWidget*> parent,
		std::shared_ptr<Ui::Show> show,
		const QString &address) {
	auto result = object_ptr<Ui::FlatLabel>(
		parent,
		rpl::single(DetailsAddressValue(address)),
		st::walletDetailsAddressLabel);
	result->setTryMakeSimilarLines(true);
	const auto copy = CopyAddressCallback(std::move(show), address);
	result->setClickHandlerFilter([=](const auto &...) {
		copy();
		return false;
	});
	return result;
}

object_ptr<Ui::RpWidget> MakeActionRow(
		not_null<QWidget*> parent,
		ActionRowArgs args) {
	return object_ptr<ActionRow>(parent, std::move(args));
}

object_ptr<Ui::RpWidget> MakeCommentBubble(
		not_null<QWidget*> parent,
		object_ptr<Ui::RpWidget> content,
		const style::color &bg) {
	auto result = object_ptr<Ui::PaddingWrap<Ui::RpWidget>>(
		parent,
		std::move(content),
		st::giveawayGiftCodeValueMargin);
	const auto raw = result.data();
	const auto background = raw->lifetime().make_state<Ui::RoundRect>(
		st::boxRadius,
		bg);
	raw->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(raw);
		background->paint(p, raw->rect());
	}, raw->lifetime());
	return result;
}

not_null<Ui::TableLayout*> AddDetailsTableFrame(
		not_null<Ui::VerticalLayout*> container) {
	const auto wrap = container->add(
		object_ptr<Ui::PaddingWrap<Ui::TableLayout>>(
			container,
			object_ptr<Ui::TableLayout>(container, st::walletDetailsTable),
			style::margins()),
		st::giveawayGiftCodeTableMargin);
	const auto bg = wrap->lifetime().make_state<Ui::RoundRect>(
		st::walletDetailsTable.radius,
		st::windowBg);
	wrap->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(wrap);
		bg->paint(p, wrap->rect());
	}, wrap->lifetime());
	return wrap->entity();
}

QString SendErrorText(SendError error, int64 minTransferNano) {
	switch (error) {
	case SendError::None:
	case SendError::Silent:
	case SendError::SubmissionUnknown:
		return QString();
	case SendError::AmountTooSmall:
		return tr::lng_wallet_send_error_too_small(
			tr::now,
			lt_amount,
			Ui::FormatTonAmount(minTransferNano).full);
	case SendError::CommentTooLong:
		return tr::lng_wallet_comment_too_long(tr::now);
	case SendError::CommentEncryptionUnavailable:
		return tr::lng_wallet_comment_encryption_failed(tr::now);
	// A balance that covers the amount but not the fee is the same problem
	// to the sender as one that covers neither, and one sentence says it.
	case SendError::InsufficientBalance:
	case SendError::InsufficientFees:
		return tr::lng_wallet_send_error_insufficient(tr::now);
	case SendError::PreviousUnresolved:
	case SendError::AlreadySending:
		return tr::lng_wallet_send_error_in_progress(tr::now);
	case SendError::SigningUnavailable:
		return tr::lng_wallet_readonly_bar(tr::now);
	case SendError::Locked:
		return tr::lng_wallet_vault_locked(tr::now);
	case SendError::InvalidRequest:
	case SendError::Failed:
	case SendError::KeyMismatch:
	case SendError::Rejected:
	case SendError::DataInvalid:
	case SendError::CollectibleRejected:
		return tr::lng_wallet_send_error_failed(tr::now);
	case SendError::CollectibleUnavailable:
		return tr::lng_wallet_collectible_not_owned(tr::now);
	case SendError::KeyChanged:
		return tr::lng_wallet_send_key_changed_text(tr::now);
	case SendError::QuoteExpired:
		return tr::lng_wallet_send_error_quote_expired(tr::now);
	case SendError::LinkExpired:
		return tr::lng_wallet_send_link_expired(tr::now);
	}
	Unexpected("Error value in SendErrorText.");
}

void ShowWalletKeyChanged(std::shared_ptr<Main::SessionShow> show) {
	ShowKeyChangedBox(show, tr::lng_wallet_send_key_changed_text());
}

QString ErrorWithType(const QString &message, const QString &error) {
	return error.isEmpty()
		? message
		: tr::lng_wallet_error_with_type(
			tr::now,
			lt_message,
			message,
			lt_error,
			error);
}

void AcquireTransferCommentKey(
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CommentScope> scope,
		Fn<bool()> current,
		rpl::lifetime &lifetime,
		Fn<void(KeyAuthorization)> done) {
	AcquireKeyThroughLadder(
		std::move(show),
		std::move(scope),
		std::move(current),
		lifetime,
		std::move(done),
		nullptr);
}

void AcquireWalletKey(
		std::shared_ptr<Main::SessionShow> show,
		Fn<bool()> current,
		rpl::lifetime &lifetime,
		Fn<void(KeyAuthorization)> done,
		rpl::producer<QString> importAbout) {
	AcquireKeyThroughLadder(
		std::move(show),
		nullptr,
		std::move(current),
		lifetime,
		std::move(done),
		std::move(importAbout));
}

void ShowTransactionDetails(
		std::shared_ptr<Main::SessionShow> show,
		TransferItem item,
		bool partial,
		std::shared_ptr<CollectibleMedia> media,
		Fn<bool()> originCurrent,
		rpl::producer<> originInvalidated,
		Fn<void()> openWallet,
		Ui::LayerOptions options) {
	if (!show || !show->valid()
		|| (originCurrent && !originCurrent())) {
		return;
	}
	show->showBox(Box(
		WalletTransactionBox,
		show,
		std::move(item),
		partial,
		std::move(media),
		std::move(originCurrent),
		std::move(originInvalidated),
		std::move(openWallet),
		rpl::producer<TransferItem>()), options);
}

void ShowSubmittedTransfer(
		std::shared_ptr<Main::SessionShow> show,
		const std::string &operationId) {
	if (!show || !show->valid()) {
		return;
	}
	const auto wallet = &show->session().wallet();
	const auto find = [=]() -> std::optional<TransferItem> {
		if (auto item = wallet->trackedTransaction(operationId)) {
			return item;
		}
		const auto pending = wallet->pendingSend();
		return (pending && pending->operationId == operationId)
			? std::make_optional(ItemFromPending(*pending))
			: std::nullopt;
	};
	if (const auto item = find()) {
		ShowWalletTransactionBox(
			show,
			*item,
			nullptr,
			wallet->historyUpdates() | rpl::map(find) | rpl::filter_optional());
	}
}

bool ShowFirstGramsIfPending(std::shared_ptr<Main::SessionShow> show) {
	if (!show || !show->valid()) {
		return false;
	}
	const auto session = &show->session();
	if (!session->promoSuggestions().current(
			Data::PromoSuggestions::SugWalletFirstIncomingTransfer())) {
		return false;
	}
	show->showBox(Box(WalletFirstGramsBox, session));
	return true;
}

rpl::producer<bool> TransactionsShownValue(
		not_null<Main::Session*> session) {
	return rpl::combine(
		HistoryShownValue(session),
		session->wallet().collectiblesTabValue()
	) | rpl::map([](bool history, bool collectibles) {
		return history && !collectibles;
	}) | rpl::distinct_until_changed();
}

base::unique_qptr<Ui::RpWidget> CreateContent(
		not_null<Ui::SeparatePanel*> panel,
		std::shared_ptr<Main::SessionShow> show) {
	return base::make_unique_q<Content>(panel, std::move(show));
}

object_ptr<Ui::RpWidget> MakeWalletCard(
		QWidget *parent,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<bool> markPlayed) {
	auto result = object_ptr<Ui::FixedHeightWidget>(
		parent,
		st::walletCardHeight);
	const auto raw = result.data();
	raw->setNaturalWidth(PanelCardWidth());

	const auto card = Ui::CreateChild<Card>(
		raw,
		show,
		CardNameValue(&show->session()));
	card->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto overlay = Ui::CreateChild<Ui::RpWidget>(raw);
	overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
	overlay->show();
	overlay->raise();

	const auto ink = raw->lifetime().make_state<BalanceInk>();
	{
		const auto scope = WindowPaletteScope(raw);
		ink->setOuterWidth(st::walletPanelSize.width());
	}

	raw->sizeValue(
	) | rpl::on_next([=](QSize size) {
		const auto rest = QRect(QPoint(), size);
		card->setGeometry(rest);
		card->setFold(ComputeCardFold(rest, 0.));
		overlay->setGeometry(rest);
	}, raw->lifetime());

	overlay->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(overlay);
		auto hq = PainterHighQualityEnabler(p);
		ink->paint(
			p,
			ComputeCardFold(raw->rect(), 0.),
			QRegion(raw->rect()),
			clip);
	}, overlay->lifetime());

	SetupCardBalance(
		ink,
		&show->session(),
		[=] { overlay->update(); },
		raw);
	SetupCardMark(ink, raw, std::move(markPlayed), [=] {
		overlay->update(
			ink->markPaintRect(ComputeCardFold(raw->rect(), 0.)));
	});

	rpl::single(rpl::empty) | rpl::then(
		style::PaletteChanged()
	) | rpl::on_next([=] {
		const auto scope = WindowPaletteScope(raw);
		ink->refresh();
		card->invalidateCache();
		card->update();
		overlay->update();
	}, raw->lifetime());

	return result;
}

object_ptr<Ui::RpWidget> MakeTransferCard(
		QWidget *parent,
		not_null<Main::Session*> session,
		TransferCardArgs args) {
	auto result = object_ptr<Ui::FixedHeightWidget>(
		parent,
		st::walletCardHeight);
	const auto raw = result.data();
	raw->setNaturalWidth(PanelCardWidth());

	struct State {
		BalanceInk ink;
		CardBackground background;
		QStringList lines;
	};
	const auto state = raw->lifetime().make_state<State>();
	{
		const auto scope = WindowPaletteScope(raw);
		state->ink.setOuterWidth(st::walletPanelSize.width());
	}
	state->lines = TransferCardLines(args.destination, args.recipients);

	const auto info = Ui::CreateChild<Ui::AbstractButton>(raw);
	info->setClickedCallback(std::move(args.info));
	info->show();
	raw->sizeValue(
	) | rpl::on_next([=](QSize size) {
		info->setGeometry(TransferCardInfoRect(size.width()));
	}, info->lifetime());

	raw->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = QPainter(raw);
		auto hq = PainterHighQualityEnabler(p);
		const auto rect = raw->rect();
		state->background.paint(p, rect, 0.);

		const auto plate = TransferCardInfoRect(rect.width());
		PaintCardQrPlate(p, plate);
		st::walletCardInfoIcon.paintInCenter(p, plate, CardQrIconFg());

		const auto font
			= st::walletDetailsCollectionLabel.style.font->monospace();
		const auto &lines = state->lines;
		auto baseline = rect.height()
			- st::walletCardNameBottom
			- (int(lines.size()) - 1) * font->height;
		p.setPen(st::activeButtonFg);
		p.setFont(font);
		for (const auto &line : lines) {
			p.drawText(st::walletCardContentLeft, baseline, line);
			baseline += font->height;
		}

		state->ink.paint(p, ComputeCardFold(rect, 0.), QRegion(rect), clip);
	}, raw->lifetime());

	const auto amount = args.netNano.value_or(-args.totalNano);
	const auto magnitude = std::abs(amount);
	const auto style = (amount > 0)
		? BalanceStyle::Plus
		: ((amount < 0) || !args.netNano)
		? BalanceStyle::Minus
		: BalanceStyle::Exact;
	FiatRateValue(
		session
	) | rpl::on_next([=](FiatRate rate) {
		const auto scope = WindowPaletteScope(raw);
		state->ink.setContent(
			CreditsAmount(
				magnitude / Ui::kNanosInOne,
				magnitude % Ui::kNanosInOne,
				CreditsType::Ton),
			FormatFiat(magnitude, rate),
			style);
		raw->update();
	}, raw->lifetime());

	rpl::single(rpl::empty) | rpl::then(
		style::PaletteChanged()
	) | rpl::on_next([=] {
		const auto scope = WindowPaletteScope(raw);
		state->ink.refresh();
		raw->update();
	}, raw->lifetime());

	SetupCardMark(&state->ink, raw, std::move(args.markPlayed), [=] {
		raw->update(
			state->ink.markPaintRect(ComputeCardFold(raw->rect(), 0.)));
	});

	return result;
}

void FillMenu(
		std::shared_ptr<Main::SessionShow> show,
		const Ui::Menu::MenuCallback &addAction) {
	const auto currency = show->session().wallet().rates().current().currency;
	addAction(
		(Ui::Text::FixAmpersandInAction(
			tr::lng_wallet_menu_currency(tr::now))
			+ u"\t"_q
			+ currency),
		[=] {
			show->showBox(Box(WalletChooseCurrencyBox, show));
		},
		&st::walletMenuCurrencyIcon);
	// The menu is rebuilt on every open, so this reading is live: the entry
	// is absent in both read-only modes and while the mode is still Unknown.
	const auto custody = show->session().wallet().deviceCustodyState();
	if (custody.mode == DeviceMode::Full) {
		addAction(
			Ui::Text::FixAmpersandInAction(
				tr::lng_wallet_protection_title(tr::now)),
			[=] {
				ShowKeyProtectionBox(
					show,
					{ .mode = KeyProtectionMode::Change });
			},
			&st::menuIconLock);
	}
	if (show->session().wallet().presence() == Presence::Ready) {
		addAction(
			Ui::Text::FixAmpersandInAction(
				tr::lng_wallet_keys_title(tr::now)),
			[=] { show->showBox(Box(WalletKeysBackupBox, show)); },
			&st::menuIconPermissions);
		const auto &store = show->session().wallet().tonConnect();
		const auto connected = ranges::any_of(
			ranges::views::values(store.sessions()),
			TonConnectSessionConnected);
		if (connected) {
			addAction(
				Ui::Text::FixAmpersandInAction(
					tr::lng_wallet_apps_title(tr::now)),
				[=] { show->showBox(Box(TonConnectAppsBox, show)); },
				&st::menuIconLink);
		}
	}
	addAction({ .isSeparator = true });
	addAction(
		Ui::Text::FixAmpersandInAction(tr::lng_wallet_how_menu(tr::now)),
		[=] { show->showBox(Box(WalletHowItWorksBox, &show->session())); },
		&st::menuIconFaq);
}

bool TransferLinkValid(const QString &url) {
	return ParseRecipientFlow(url).has_value();
}

void ShowTransferLink(
		std::shared_ptr<Main::SessionShow> show,
		const QString &url) {
	const auto flow = ParseRecipientFlow(url);
	if (!flow) {
		return;
	} else if (TransferLinkExpired(flow->expiresAt)) {
		show->showToast(tr::lng_wallet_send_link_expired(tr::now));
		return;
	}
	ResolveOwnerAndOpenSendFlow(show, *flow);
}

void ShowWalletConflict(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> switched) {
	show->showBox(Box(WalletConflictBox, show, std::move(switched)));
}

Fn<void()> ShowWalletBusyBox(
		std::shared_ptr<Main::SessionShow> show,
		rpl::producer<QString> text,
		Fn<void()> dismissed) {
	auto box = Box(WalletBusyBox, std::move(text));
	const auto weak = base::make_weak(box.data());
	const auto closing = std::make_shared<bool>(false);
	box->boxClosing() | rpl::on_next([=] {
		if (!*closing && dismissed) {
			dismissed();
		}
	}, box->lifetime());
	show->showBox(std::move(box));
	return [=] {
		*closing = true;
		if (const auto strong = weak.get()) {
			if (strong->hasDelegate()) {
				strong->closeBox();
			}
		}
	};
}

void ShowSendToUser(
		std::shared_ptr<Main::SessionShow> show,
		not_null<UserData*> user,
		Fn<void()> sent,
		int64 amountNano,
		Fn<void()> notReady,
		base::weak_qptr<Ui::BoxContent> origin) {
	if (!show || !show->valid() || &show->session() != &user->session()) {
		return;
	}
	const auto session = &show->session();
	const auto userId = peerToUser(user->id);
	if (session->data().userLoaded(userId) != user) {
		return;
	}
	WhenWalletReady(show, [=] {
		const auto error = session->wallet().userAddresses(
		).forceResolveError(userId);
		if (!error.isEmpty() && error != u"WALLET_BALANCE_EMPTY"_q) {
			show->showToast(SendUserLoadErrorText(error));
			return;
		} else if (user->isSelf()) {
			const auto identity = session->wallet().transferWalletIdentity();
			auto flow = identity
				? ParseRecipientFlow(FormatFriendly(identity->address, false))
				: std::nullopt;
			if (!flow) {
				show->showToast(SendUserLoadErrorText(u"WALLET_NOT_READY"_q));
				return;
			}
			flow->amountNano = amountNano;
			show->showBox(Box(
				WalletSendBox,
				show,
				std::move(flow),
				static_cast<UserData*>(nullptr),
				sent,
				notReady,
				amountNano,
				origin));
			return;
		}
		show->showBox(Box(
			WalletSendBox,
			show,
			std::nullopt,
			user.get(),
			sent,
			notReady,
			amountNano,
			origin));
	}, notReady);
}

void ShowCollectibleTransfer(
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CollectibleMedia> media,
		const QString &collectible) {
	if (!show || !show->valid() || !media || collectible.isEmpty()) {
		return;
	}
	media->resolve(collectible);
	const auto weak = std::weak_ptr<CollectibleMedia>(media);
	WhenWalletReady(show, [=] {
		const auto strong = weak.lock();
		if (!strong || !show->valid()) {
			return;
		}
		show->showBox(Box(
			WalletSendRecipientBox,
			show,
			QString(),
			std::make_shared<const CollectibleTransfer>(CollectibleTransfer{
				.address = collectible,
				.media = strong,
			})));
	});
}

void ShowSendToLinkRecipient(
		std::shared_ptr<Main::SessionShow> show,
		const QString &recipient,
		int64 amountNano) {
	if (!show || !show->valid()) {
		return;
	}
	const auto session = &show->session();
	const auto invalid = [=] {
		show->showToast(tr::lng_wallet_send_link_invalid(tr::now));
	};
	if (auto flow = ParseRecipientFlow(recipient)) {
		if (!flow->amountNano) {
			flow->amountNano = amountNano;
		}
		ResolveOwnerAndOpenSendFlow(show, *flow);
		return;
	}
	const auto username = recipient.startsWith('@')
		? recipient.mid(1)
		: recipient;
	if (!qthelp::regex_match(u"^[a-zA-Z0-9\\_]+$"_q, username, {})) {
		invalid();
		return;
	}
	const auto open = [=](PeerData *peer) {
		if (!show->valid() || &show->session() != session) {
			return;
		}
		const auto user = peer ? peer->asUser() : nullptr;
		if (!user) {
			show->showToast(tr::lng_wallet_send_user_unavailable(tr::now));
			return;
		}
		ShowSendToUser(show, user, nullptr, amountNano);
	};
	if (const auto peer = session->data().peerByUsername(username)) {
		open(peer);
		return;
	}
	session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(username),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		const auto &data = result.data();
		session->data().processUsers(data.vusers());
		session->data().processChats(data.vchats());
		const auto peerId = peerFromMTP(data.vpeer());
		open(peerId ? session->data().peer(peerId).get() : nullptr);
	}).fail([=] {
		if (show->valid() && &show->session() == session) {
			show->showToast(
				tr::lng_username_not_found(tr::now, lt_user, username));
		}
	}).send();
}

} // namespace Wallet
