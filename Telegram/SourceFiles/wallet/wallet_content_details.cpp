#include "wallet/wallet_content_internal.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

void AddDetailsTable(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Main::SessionShow> show,
		const TransferItem &item,
		DetailsFee fee) {
	const auto session = &show->session();
	const auto table = AddDetailsTableFrame(container);
	if (item.status == TransferItem::Status::Failure) {
		const auto reason = item.failureReason.trimmed();
		Ui::AddTableRow(
			table,
			tr::lng_wallet_details_status(),
			reason.isEmpty()
				? tr::lng_channel_earn_history_failed(tr::marked)
				: tr::lng_wallet_details_failed_reason(
					lt_reason,
					rpl::single(TextWithEntities{ reason }),
					tr::marked));
	}
	const auto peerKind = (item.kind == TransferItem::Kind::PeerTransfer)
		|| (item.kind == TransferItem::Kind::Collectible);
	const auto peer = (peerKind && item.counterpartyPeer)
		? session->data().peerLoaded(PeerId(item.counterpartyPeer))
		: nullptr;
	if (item.kind == TransferItem::Kind::KeyChange) {
		Ui::AddTableRow(
			table,
			tr::lng_wallet_details_operation(),
			tr::lng_wallet_row_key_change(tr::marked));
	} else if (peer) {
		AddPeerCounterpartyRows(box, table, show, peer, item);
	} else if (!item.counterparty.isEmpty()) {
		if (const auto address = DetailsFriendlyAddress(item)) {
			auto label = (item.incoming
				? tr::lng_wallet_details_sender()
				: tr::lng_wallet_details_recipient());
			const auto provider = OnrampProvider(item);
			const auto name = !provider.isEmpty()
				? provider
				: item.counterpartyName.trimmed();
			if (name.isEmpty()) {
				Ui::AddTableRow(
					table,
					std::move(label),
					AddressValueLabel(table, box->uiShow(), *address));
			} else {
				Ui::AddTableRow(
					table,
					std::move(label),
					NameValueLabel(
						table,
						box->uiShow(),
						name,
						*address));
				Ui::AddTableRow(
					table,
					tr::lng_wallet_details_address(),
					AddressValueLabel(table, box->uiShow(), *address));
			}
		}
	}
	const auto pending
		= (item.status == TransferItem::Status::Pending);
	// An incoming transfer was paid for by whoever sent it, and what the
	// wallet spends to receive one is a few nanograms, so the row is left
	// out entirely and nothing is waited for on its behalf.
	if (!item.incoming) {
		if (fee != DetailsFee::Known) {
			AddPendingFeeTableRow(table, fee);
		} else if (!pending
			&& (item.gasless || (item.feeNano && *item.feeNano > 0))) {
			AddFeeTableRow(table, box->uiShow(), session, item);
		}
	}
	if (item.date) {
		Ui::AddTableRow(
			table,
			tr::lng_wallet_details_date(),
			rpl::single(tr::marked(
				langDateTime(base::unixtime::parse(*item.date)))));
	}
}

void AddBoxCloseButton(
		not_null<Ui::GenericBox*> box,
		Fn<void()> close) {
	box->addTopButton(st::boxTitleClose, [=] {
		if (close) {
			close();
		} else {
			box->closeBox();
		}
	});
}

// The wallet's box footers share one "working" appearance: the label goes
// blank and an infinite spinner appears centred on the button. It stays two
// halves because they attach at two different moments - the label is a
// producer handed to addButton, the spinner is a child added once the button
// exists - so every box keeps its own label choice, its own guards and its
// own position for the spinner. The size and color follow the button's own
// style.
[[nodiscard]] rpl::producer<QString> BusyFooterLabel(
		rpl::producer<QString> text,
		rpl::producer<bool> busy) {
	return rpl::combine(
		std::move(text),
		std::move(busy)
	) | rpl::map([](const QString &text, bool busy) {
		return busy ? QString() : text;
	});
}

void AddBusyFooterSpinner(
		not_null<Ui::RoundButton*> button,
		rpl::producer<bool> shown) {
	using Radial = style::InfiniteRadialAnimation;
	const auto &buttonSt = button->st();
	const auto radialSt = button->lifetime().make_state<Radial>(
		st::startGiveawayButtonLoading);
	radialSt->color = buttonSt.textFg;
	const auto loading = Info::Statistics::InfiniteRadialAnimationWidget(
		button,
		buttonSt.height / 2,
		radialSt);
	Info::Statistics::AddChildToWidgetCenter(button, loading);
	loading->showOn(std::move(shown));
}

// The row counterpart of the footer spinner: the same animation in the
// row's subtext colour at its right edge, so a settings row keeps its label
// while the action it started is still in flight.
void AddRowSpinner(
		not_null<Ui::SettingsButton*> button,
		rpl::producer<bool> shown) {
	const auto &st = button->st();
	const auto size = st.style.font->height;
	const auto loading = Info::Statistics::InfiniteRadialAnimationWidget(
		button,
		size,
		&st::walletKeysRowLoading);
	loading->setAttribute(Qt::WA_TransparentForMouseEvents);
	button->sizeValue() | rpl::on_next([=](QSize outer) {
		loading->moveToRight(
			st.padding.right(),
			(outer.height() - size) / 2,
			outer.width());
	}, loading->lifetime());
	loading->showOn(std::move(shown));
}

// An inform box whose one button is Cancel: the label says what is being
// waited for, the spinner under it that the wait is on, and closing it by
// any means reports the same dismissal.
void WalletBusyBox(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> text) {
	Ui::InformBox(box, {
		.text = std::move(text),
		.confirmText = tr::lng_cancel(),
	});
	const auto &loading = st::walletBusyBoxLoading;
	const auto side = loading.size.height() + 2 * loading.thickness;
	const auto content = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(box, side),
		st::walletBusyBoxPadding);
	const auto indicator = Info::Statistics::InfiniteRadialAnimationWidget(
		content,
		side,
		&loading);
	Info::Statistics::AddChildToWidgetCenter(content, indicator);
	indicator->show();
}

[[nodiscard]] QImage ReceiveQrCenter(int side, int markSide) {
	auto result = QImage(side, side, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::white);
	auto p = QPainter(&result);
	auto hq = PainterHighQualityEnabler(p);
	auto svg = QSvgRenderer(
		Ui::Earn::CurrencySvgTwoTone(st::activeButtonBg->c));
	const auto skip = (side - markSide) / 2;
	svg.render(&p, QRectF(skip, skip, markSide, markSide));
	return result;
}

[[nodiscard]] QImage ReceiveQrImage(
		const QString &address,
		int size,
		int ratio,
		int quietZoneModules) {
	const auto data = Qr::Encode(address, Qr::Redundancy::Quartile);
	const auto pixel = std::max(size / std::max(data.size, 1), 1);
	auto image = Qr::Generate(data, pixel * ratio, Qt::black, Qt::white);
	const auto replaceSide = Qr::ReplaceSize(data, pixel * ratio);
	const auto markSide = std::min(
		st::walletReceiveMarkSize * ratio,
		replaceSide - 2 * pixel * ratio);
	image = Qr::ReplaceCenter(
		std::move(image),
		ReceiveQrCenter(replaceSide, markSide));
	if (quietZoneModules > 0) {
		const auto skip = quietZoneModules * pixel * ratio;
		auto padded = QImage(
			image.width() + 2 * skip,
			image.height() + 2 * skip,
			QImage::Format_ARGB32_Premultiplied);
		padded.fill(Qt::white);
		auto p = QPainter(&padded);
		p.drawImage(skip, skip, image);
		p.end();
		image = std::move(padded);
	}
	image.setDevicePixelRatio(ratio);
	return image;
}

[[nodiscard]] WalletBoxTitleBar AddWalletBoxTitleBar(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> title) {
	const auto row = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(
			box,
			st::walletReceiveTitleHeight),
		style::margins(),
		style::al_justify);
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		row,
		std::move(title),
		st::boxTitle);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto close = Ui::CreateChild<Ui::IconButton>(
		row,
		st::boxTitleClose);
	Ui::ToggleChildrenVisibility(row, true);
	row->sizeValue(
	) | rpl::on_next([=](QSize size) {
		label->moveToLeft(
			st::boxTitlePosition.x(),
			st::boxTitlePosition.y(),
			size.width());
		close->moveToRight(st::walletReceiveTitleButtonSkip, 0, size.width());
	}, row->lifetime());
	return { label, close };
}

[[nodiscard]] bool IsValidOnrampUrl(const QString &url) {
	const auto parsed = QUrl(url, QUrl::StrictMode);
	return parsed.isValid()
		&& parsed.scheme() == u"https"_q
		&& !parsed.host().isEmpty();
}

[[nodiscard]] std::optional<OldWalletAppLink> ParseOldWalletAppLink(
		not_null<Main::Session*> session,
		const QString &url) {
	const auto configured = session->appConfig().oldWalletBotUsername();
	if (configured.isEmpty()) {
		return {};
	}
	const auto prefix = u"tg://resolve?"_q;
	const auto local = Core::TryConvertUrlToLocal(url);
	if (!local.startsWith(prefix, Qt::CaseInsensitive)) {
		return {};
	}
	const auto params = qthelp::url_parse_params(
		local.mid(prefix.size()),
		qthelp::UrlParamNameTransform::ToLower);
	if (params.value(u"domain"_q).compare(configured, Qt::CaseInsensitive)) {
		return {};
	}
	const auto mode = params.value(u"mode"_q);
	return OldWalletAppLink{
		.appname = params.value(u"appname"_q),
		.startapp = params.value(u"startapp"_q),
		.startattach = (params.contains(u"startattach"_q)
			? params.value(u"startattach"_q)
			: std::optional<QString>()),
		.compact = (mode == u"compact"_q),
		.fullscreen = (mode == u"fullscreen"_q),
	};
}

void OpenOldWalletApp(
		not_null<UserData*> bot,
		std::shared_ptr<Ui::Show> show,
		const OldWalletAppLink &link) {
	const auto startCommand = link.startattach.value_or(link.startapp);
	auto source = link.startattach
		? InlineBots::WebViewSource(InlineBots::WebViewSourceLinkAttachMenu{
			.token = startCommand,
		})
		: !link.appname.isEmpty()
		? InlineBots::WebViewSource(InlineBots::WebViewSourceLinkApp{
			.appname = link.appname,
			.token = startCommand,
		})
		: InlineBots::WebViewSource(InlineBots::WebViewSourceLinkBotProfile{
			.token = startCommand,
			.compact = link.compact,
		});
	bot->session().attachWebView().open({
		.bot = bot,
		.parentShow = std::move(show),
		.context = {
			.action = ::Api::SendAction(bot->owner().history(bot)),
			.fullscreen = link.fullscreen,
			.maySkipConfirmation = true,
		},
		.button = { .startCommand = startCommand },
		.source = std::move(source),
	});
}

void OpenOldWalletAppLink(
		not_null<Main::Session*> session,
		std::shared_ptr<Ui::Show> show,
		const OldWalletAppLink &link,
		const QString &url) {
	const auto username = session->appConfig().oldWalletBotUsername();
	const auto byUsername = session->data().peerByUsername(username);
	if (const auto bot = byUsername ? byUsername->asUser() : nullptr) {
		if (bot->isOldWalletBot()) {
			OpenOldWalletApp(bot, std::move(show), link);
			return;
		}
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
		const auto bot = peerId
			? session->data().peer(peerId)->asUser()
			: nullptr;
		if (bot && bot->isOldWalletBot()) {
			OpenOldWalletApp(bot, show, link);
		} else {
			UrlClickHandler::Open(url);
		}
	}).fail([=] {
		UrlClickHandler::Open(url);
	}).send();
}

void OpenWalletUrl(
		not_null<Main::Session*> session,
		std::shared_ptr<Ui::Show> show,
		const QString &url) {
	if (const auto link = ParseOldWalletAppLink(session, url)) {
		OpenOldWalletAppLink(session, std::move(show), *link, url);
	} else {
		UrlClickHandler::Open(url);
	}
}

not_null<Ui::IconButton*> AddRowChevron(not_null<Ui::RpWidget*> button) {
	const auto arrow = Ui::CreateChild<Ui::IconButton>(
		button,
		st::backButton);
	arrow->setIconOverride(
		&st::settingsPremiumArrow,
		&st::settingsPremiumArrowOver);
	arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
	arrow->show();
	button->sizeValue(
	) | rpl::on_next([=](QSize size) {
		const auto &shift = st::settingsPremiumArrowShift;
		arrow->moveToRight(
			-shift.x(),
			shift.y() + (size.height() - arrow->height()) / 2);
	}, arrow->lifetime());
	return arrow;
}

void WalletReceiveBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		const QString &address) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::walletReceiveBox);
	box->setNoContentMargin(true);
	box->setCustomCornersFilling(RectPart::FullTop | RectPart::FullBottom);

	struct State {
		rpl::variable<bool> resolving = false;
		QImage image;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto show = box->uiShow();

	box->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(box);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::activeButtonBg);
		p.drawRoundedRect(box->rect(), st::boxRadius, st::boxRadius);
	}, box->lifetime());

	const auto bar = AddWalletBoxTitleBar(box, tr::lng_wallet_add_funds());
	rpl::single(
		rpl::empty
	) | rpl::then(
		style::PaletteChanged()
	) | rpl::on_next([=] {
		const auto scope = WindowPaletteScope(box);
		bar.title->setTextColorOverride(st::activeButtonFg->c);
	}, box->lifetime());
	bar.close->setIconOverride(
		&st::walletReceiveCloseIconActive,
		&st::walletReceiveCloseIconActiveOver);
	bar.close->setRippleColorOverride(&st::activeButtonBgRipple);
	bar.close->setClickedCallback([=] {
		box->closeBox();
	});

	const auto inner = box->verticalLayout();
	{
		const auto scope = WindowPaletteScope(box);
		state->image = ReceiveQrImage(
			address,
			st::walletReceiveQrSize,
			style::DevicePixelRatio());
	}
	const auto qrSide = state->image.width() / style::DevicePixelRatio();
	const auto &padding = st::walletReceivePlatePadding;
	const auto font = st::walletReceiveAddressFont->monospace();
	const auto hintFont = st::walletReceiveHintFont;
	const auto groupWidth = font->width(address.left(kAddressGroup));
	const auto spaceWidth = font->width(QChar(' '));
	const auto lineWidth = kReceiveGroupsPerLine * groupWidth
		+ (kReceiveGroupsPerLine - 1) * spaceWidth;
	const auto lineHeight = font->height + st::walletReceiveAddressLineSkip;
	const auto blockHeight = kReceiveLines * font->height
		+ (kReceiveLines - 1) * st::walletReceiveAddressLineSkip;
	const auto qrLeft = padding.left();
	const auto qrTop = padding.top();
	const auto addressTop = qrTop + qrSide + st::walletReceiveAddressTopSkip;
	const auto hintTop = addressTop
		+ blockHeight
		+ st::walletReceiveHintTopSkip;
	const auto plateWidth = std::max(qrSide, lineWidth)
		+ padding.left()
		+ padding.right();
	const auto plateHeight = hintTop + hintFont->height + padding.bottom();

	auto plateOwned = object_ptr<Ui::FixedHeightWidget>(inner, plateHeight);
	plateOwned->setNaturalWidth(plateWidth);
	const auto plate = inner->add(
		std::move(plateOwned),
		st::walletReceivePlateMargin,
		style::al_top);
	plate->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(plate);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawRoundedRect(
			plate->rect(),
			st::walletReceivePlateRadius,
			st::walletReceivePlateRadius);
		p.drawImage(qrLeft, qrTop, state->image);
		p.setFont(font);
		const auto left = (plate->width() - lineWidth) / 2;
		for (auto i = 0; i != kAddressLength / kAddressGroup; ++i) {
			const auto line = i / kReceiveGroupsPerLine;
			const auto column = i % kReceiveGroupsPerLine;
			p.setPen((i % 2)
				? QColor(0x99, 0x99, 0x99)
				: QColor(0x22, 0x22, 0x22));
			p.drawText(
				left + column * (groupWidth + spaceWidth),
				addressTop + line * lineHeight + font->ascent,
				address.mid(i * kAddressGroup, kAddressGroup));
		}
		const auto hint = hintFont->elided(
			tr::lng_wallet_receive_copy_hint(tr::now),
			plate->width() - padding.left() - padding.right());
		p.setFont(hintFont);
		p.setPen(QColor(0x99, 0x99, 0x99));
		p.drawText(
			(plate->width() - hintFont->width(hint)) / 2,
			hintTop + hintFont->ascent,
			hint);
	}, plate->lifetime());
	style::PaletteChanged(
	) | rpl::on_next([=] {
		const auto scope = WindowPaletteScope(plate);
		state->image = ReceiveQrImage(
			address,
			st::walletReceiveQrSize,
			style::DevicePixelRatio());
		plate->update();
	}, plate->lifetime());

	const auto qrTarget = Ui::CreateChild<Ui::AbstractButton>(plate);
	qrTarget->setClickedCallback([=] {
		const auto scope = WindowPaletteScope(plate);
		QGuiApplication::clipboard()->setImage(ReceiveQrImage(
			address,
			st::walletReceiveQrCopySize,
			1,
			kQrQuietZoneModules));
		show->showToast({
			.text = { tr::lng_group_invite_qr_copied(tr::now) },
			.iconLottie = u"toast/copy"_q,
			.iconLottieSize = st::toastLottieIconSize,
		});
	});
	const auto textTarget = Ui::CreateChild<Ui::AbstractButton>(plate);
	textTarget->setClickedCallback([=] {
		TextUtilities::SetClipboardText(TextForMimeData::Simple(address));
		show->showToast({
			.text = { tr::lng_gift_unique_address_copied(tr::now) },
			.iconLottie = u"toast/copy"_q,
			.iconLottieSize = st::toastLottieIconSize,
		});
	});
	Ui::ToggleChildrenVisibility(plate, true);
	const auto textTop = addressTop - st::walletReceiveAddressTopSkip / 2;
	plate->sizeValue(
	) | rpl::on_next([=](QSize size) {
		qrTarget->setGeometry(qrLeft, qrTop, qrSide, qrSide);
		textTarget->setGeometry(
			0,
			textTop,
			size.width(),
			size.height() - textTop);
	}, plate->lifetime());

	inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			tr::lng_wallet_receive_about(),
			st::walletReceiveAboutLabel),
		st::walletReceiveAboutMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);

	// The margin belongs to the slide wrap's own padding, not to the row:
	// VerticalLayout::moveChildGetSkip() adds a row's top and bottom margin
	// unconditionally, so a row margin would keep a gap above the Buy button
	// while the caveat is collapsed.
	const auto caveat = inner->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			inner,
			object_ptr<Ui::FlatLabel>(
				inner,
				tr::lng_wallet_receive_caveat(),
				st::walletReceiveAboutLabel),
			st::walletReceiveAboutMargin),
		style::margins(),
		style::al_top);
	caveat->entity()->setTryMakeSimilarLines(true);
	caveat->toggleOn(session->wallet().deviceCustodyStateValue(
	) | rpl::map([](const DeviceCustodyState &custody) {
		return (custody.mode == DeviceMode::ReadOnlyNotRestorable);
	}));
	caveat->finishAnimating();

	const auto buy = inner->add(
		object_ptr<Ui::RoundButton>(
			inner,
			BusyFooterLabel(
				tr::lng_wallet_buy_button(),
				state->resolving.value()),
			st::walletReceiveBuyButton),
		st::walletReceiveBuyMargin,
		style::al_justify);
	buy->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	AddBusyFooterSpinner(buy, state->resolving.value());
	buy->setClickedCallback([=] {
		if (state->resolving.current()) {
			return;
		}
		state->resolving = true;
		session->wallet().onramp().requestSessionUrl(
			address,
			session->wallet().rates().current().currency,
			crl::guard(box, [=](QString url) {
				state->resolving = false;
				if (!IsValidOnrampUrl(url)) {
					show->showToast(tr::lng_wallet_buy_empty(tr::now));
					return;
				}
				OpenWalletUrl(session, show, url);
				box->closeBox();
			}));
	});
	buy->widthValue(
	) | rpl::on_next([=](int width) {
		buy->setFullWidth(width);
	}, buy->lifetime());
}

void ShowWalletReceiveBox(
		not_null<Main::Session*> session,
		std::shared_ptr<Ui::Show> show) {
	auto &wallet = session->wallet();
	const auto address = wallet.addressFriendly(false);
	if (address.size() != kAddressLength) {
		return;
	}
	show->showBox(Box(WalletReceiveBox, session, address));
}

void AddWalletFeaturesBody(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> title,
		rpl::producer<QString> subtitle,
		const std::vector<Ui::FeatureListEntry> &features,
		rpl::producer<QString> button) {
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(title),
			st::walletPhraseTitleLabel),
		st::boxRowPadding,
		style::al_top);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(subtitle),
			st::walletHowSubtitleLabel),
		st::walletHowSubtitleMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);

	for (const auto &feature : features) {
		box->addRow(Ui::MakeFeatureListEntry(box, feature));
	}

	AddBoxCloseButton(box);

	box->addButton(std::move(button), [=] { box->closeBox(); });
}

void WalletHowItWorksBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	AddWalletLottie(box, st::walletHowLottieMargin);

	const auto features = std::vector<Ui::FeatureListEntry>{
		{
			.icon = st::walletAboutInstantIcon,
			.title = tr::lng_wallet_about_instant_title(tr::now),
			.about = tr::lng_wallet_about_instant_text(tr::now, tr::marked),
			.similarLines = true,
		},
		{
			.icon = st::walletAboutFeesIcon,
			.title = tr::lng_wallet_about_fees_title(tr::now),
			.about = tr::lng_wallet_about_fees_text(
				tr::now,
				lt_count,
				GaslessDailyTransfers(session),
				tr::marked),
			.similarLines = true,
		},
		{
			.icon = st::walletAboutChainIcon,
			.title = tr::lng_wallet_about_chain_title(tr::now),
			.about = tr::lng_wallet_about_chain_text(tr::now, tr::marked),
			.similarLines = true,
		},
	};
	AddWalletFeaturesBody(
		box,
		tr::lng_wallet_how_title(),
		tr::lng_wallet_how_subtitle(),
		features,
		tr::lng_archive_hint_button());
}

[[nodiscard]] Ui::LayerStackWidget *BoxLayerStack(
		not_null<Ui::GenericBox*> box) {
	auto stack = (Ui::LayerStackWidget*)nullptr;
	for (auto parent = box->parentWidget(); parent && !stack;) {
		parent = parent->parentWidget();
		stack = dynamic_cast<Ui::LayerStackWidget*>(parent);
	}
	return stack;
}

void CloseFirstGramsByOutsideClick(not_null<Ui::GenericBox*> box) {
	const auto layer = box->parentWidget();
	const auto stack = BoxLayerStack(box);
	if (!layer || !stack) {
		return;
	}
	// WHY: a background press clears the whole stack, the Transaction box
	// below included, so it is eaten while this box is the shown layer, and
	// the close is postponed because it destroys this filter synchronously.
	base::install_event_filter(box, stack, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::MouseButtonPress || layer->isHidden()) {
			return base::EventFilterResult::Continue;
		}
		Ui::PostponeCall(box, [=] { box->closeBox(); });
		return base::EventFilterResult::Cancel;
	});
}

void WalletFirstGramsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	AddWalletLottie(box, st::walletHowLottieMargin);

	auto subtitle = FiatRateValue(session) | rpl::map([](const FiatRate &rate) {
		return rate.available()
			? tr::lng_wallet_first_rate(
				lt_amount,
				rpl::single(FormatFiat(Ui::kNanosInOne, rate)))
			: tr::lng_wallet_first_rate_none();
	}) | rpl::flatten_latest();

	const auto features = std::vector<Ui::FeatureListEntry>{
		{
			.icon = st::walletFirstSendIcon,
			.title = tr::lng_wallet_first_send_title(tr::now),
			.about = tr::lng_wallet_first_send_text(
				tr::now,
				lt_attach,
				Ui::Text::IconEmoji(&st::walletFirstAttachEmoji),
				lt_money,
				tr::marked(tr::lng_wallet_menu(tr::now)),
				tr::marked),
			.similarLines = true,
		},
		{
			.icon = st::walletFirstTradeIcon,
			.title = tr::lng_wallet_first_trade_title(tr::now),
			.about = tr::lng_wallet_first_trade_text(tr::now, tr::marked),
			.similarLines = true,
		},
		{
			.icon = st::walletFirstStoreIcon,
			.title = tr::lng_wallet_first_store_title(tr::now),
			.about = tr::lng_wallet_first_store_text(
				tr::now,
				lt_menu,
				Ui::Text::IconEmoji(&st::walletFirstMenuEmoji),
				lt_wallet,
				tr::marked(tr::lng_wallet_menu(tr::now)),
				tr::marked),
			.similarLines = true,
		},
	};
	AddWalletFeaturesBody(
		box,
		tr::lng_wallet_first_title(),
		std::move(subtitle),
		features,
		tr::lng_archive_hint_button());

	box->boxClosing() | rpl::on_next([weak = base::make_weak(session)] {
		if (const auto strong = weak.get()) {
			strong->promoSuggestions().dismiss(
				Data::PromoSuggestions::SugWalletFirstIncomingTransfer());
		}
	}, box->lifetime());

	box->showFinishes() | rpl::take(1) | rpl::on_next([=] {
		CloseFirstGramsByOutsideClick(box);
	}, box->lifetime());
}

void WalletCloudPasswordCreateBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	const auto content = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		st::boxRowPadding);
	const auto fields = Settings::CloudPassword::SetupPasswordFields(
		content,
		Settings::CloudPassword::CreatePasswordDescriptor());

	AddBoxCloseButton(box);

	struct State {
		rpl::lifetime request;
		rpl::variable<bool> loading = false;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto submit = [=] {
		if (state->request) {
			return;
		}
		const auto password = Settings::CloudPassword::ValidatePasswordFields(
			fields);
		if (!password) {
			return;
		}
		state->loading = true;
		state->request = show->session().api().cloudPassword().set(
			QString(),
			*password,
			QString(),
			false,
			QString()
		) | rpl::on_error_done([=](const QString &type) {
			state->request.destroy();
			state->loading = false;
			fields.error->show();
			fields.error->setText(MTP::IsFloodError(type)
				? tr::lng_flood_error(tr::now)
				: Lang::Hard::ServerError());
		}, [=] {
			state->request.destroy();
			box->closeBox();
			show->showToast({
				.text = { tr::lng_cloud_password_was_set(tr::now) },
				.icon = &st::toastCheckIcon,
			});
		});
	};
	const auto button = box->addButton(
		BusyFooterLabel(
			tr::lng_settings_cloud_password_password_subtitle(),
			state->loading.value()),
		submit);
	AddBusyFooterSpinner(button, state->loading.value());

	Settings::CloudPassword::SubmitPasswordFields(fields, submit);
	box->setFocusCallback([=] {
		Settings::CloudPassword::FocusPasswordFields(fields);
	});
}

void WalletCloudPasswordIntroBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	const auto content = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		st::boxRowPadding);
	Settings::CloudPassword::SetupIntroHeader(content, box->showFinishes());
	Ui::AddSkip(content, st::settingLocalPasscodeDescriptionBottomSkip);

	AddBoxCloseButton(box);

	box->addButton(tr::lng_settings_cloud_password_password_subtitle(), [=] {
		box->closeBox();
		show->showBox(Box(WalletCloudPasswordCreateBox, show));
	});
}

void SetupIntroTooltip(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::RpWidget*> card,
		Fn<QRect()> markRect,
		rpl::producer<> moves) {
	struct State {
		Ui::GlareTooltip *tooltip = nullptr;
		base::Timer hide;
		bool started = false;
		bool finished = false;
	};
	const auto state = parent->lifetime().make_state<State>();
	const auto colors = [=] {
		const auto scope = WindowPaletteScope(parent);
		return Ui::GlareTooltipColors{
			.edge = st::windowActiveTextFg->c,
			.center = anim::color(
				st::windowActiveTextFg,
				st::activeButtonFg,
				0.35),
			.rim = st::activeButtonFg->c,
			.text = st::activeButtonFg->c,
		};
	};
	state->tooltip = Ui::CreateChild<Ui::GlareTooltip>(
		parent.get(),
		st::walletIntroTooltip,
		st::walletIntroTooltipFont,
		tr::lng_wallet_intro_text(tr::now),
		colors());
	state->tooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
	state->tooltip->finishAnimating();
	style::PaletteChanged() | rpl::on_next([=] {
		if (!state->finished) {
			state->tooltip->setColors(colors());
		}
	}, parent->lifetime());
	const auto finish = [=] {
		if (state->finished) {
			return;
		}
		state->finished = true;
		state->hide.cancel();
		state->tooltip->stopGlare();
		state->tooltip->fade(false);
	};
	state->hide.setCallback(finish);

	rpl::merge(
		card->geometryValue() | rpl::to_empty,
		parent->widthValue() | rpl::to_empty,
		std::move(moves)
	) | rpl::on_next([=] {
		if (state->finished || !parent->width()) {
			return;
		}
		const auto mark = markRect();
		if (mark.isEmpty()) {
			if (state->started) {
				finish();
			}
			return;
		}
		state->tooltip->pointAt(
			mark,
			Ui::MapFrom(parent, card, card->rect()));
		if (!state->started) {
			state->started = true;
			state->tooltip->fade(true);
			state->hide.callOnce(
				state->tooltip->glaresDuration(kWalletIntroGlares));
		}
	}, parent->lifetime());
}

[[nodiscard]] QString ExplorerTransactionUrl(
		not_null<Main::Session*> session,
		const QByteArray &traceId) {
	if (traceId.isEmpty()) {
		return QString();
	}
	return Core::TonExplorerUrl(
		session,
		u"transaction/"_q + QString::fromLatin1(traceId.toHex()));
}

void WalletTransactionBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		TransferItem item,
		bool partial,
		std::shared_ptr<CollectibleMedia> media,
		Fn<bool()> originCurrent,
		rpl::producer<> originInvalidated,
		Fn<void()> openWallet,
		rpl::producer<TransferItem> updates) {
	if (originCurrent && !originCurrent()) {
		box->closeBox();
		return;
	}
	const auto session = &show->session();
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::walletDetailsBox);
	box->setNoContentMargin(true);
	box->setTitle(tr::lng_wallet_details_title());

	struct State {
		TransferItem item;
		std::shared_ptr<CollectibleMedia> media;
		base::Timer retry;
		int attempts = 0;
		bool looking = false;
	};
	const auto looking = partial && !item.id.isEmpty();
	const auto state = box->lifetime().make_state<State>();
	state->item = std::move(item);
	state->media = std::move(media);
	state->looking = looking;
	const auto art = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins(),
		style::al_justify);
	const auto header = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins(),
		style::al_justify);
	const auto fillArt = [=] {
		art->clear();
		if (!ShowsCollectible(state->item)) {
			// The animation stands where a top skip used to, so the amount and
			// everything under it move up by that much under the box title.
			AddWalletLottie(box, style::margins(), art);
		}
	};
	const auto fillHeader = [=] {
		header->clear();
		// The gap between the header and the table is one skip, whether or not
		// a comment stands in it - see AddDetailsComment for the other half.
		const auto headerBottomSkip = HasDetailsComment(state->item)
			? (st::walletDetailsAmountBottomSkip / 2)
			: st::walletDetailsAmountBottomSkip;
		if (ShowsCollectible(state->item)) {
			if (!state->media) {
				state->media = std::make_shared<CollectibleMedia>(session);
			}
			state->media->resolve(state->item.collectible);
			AddDetailsCollectibleHeader(
				header,
				session,
				state->media,
				state->item,
				headerBottomSkip);
		} else {
			AddDetailsAmountHeader(
				header,
				state->item,
				0,
				headerBottomSkip,
				FiatRateValue(session));
		}
		AddDetailsComment(
			box,
			header,
			Main::MakeSessionShow(box->uiShow(), session),
			state->item,
			originCurrent);
	};
	fillArt();
	fillHeader();

	// The amount and the comment are what the message itself said, while the
	// rows below are the transaction's own record: who it went to under their
	// Telegram name, what it cost and when the chain accepted it. The table
	// is built from the message at once and again from the served record, so
	// the box shows everything it can immediately and nothing of it waits.
	const auto details = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins());
	const auto rebuild = [=](DetailsFee fee) {
		details->clear();
		AddDetailsTable(box, details, show, state->item, fee);
	};
	rebuild(state->looking ? DetailsFee::Loading : DetailsFee::Known);
	if (updates) {
		std::move(updates) | rpl::on_next([=](TransferItem updated) {
			if (state->looking || updated == state->item) {
				return;
			}
			const auto was = std::exchange(state->item, std::move(updated));
			if (ShowsCollectible(was) != ShowsCollectible(state->item)) {
				fillArt();
			}
			if (!SameDetailsHeader(was, state->item)) {
				fillHeader();
			}
			rebuild(DetailsFee::Known);
		}, box->lifetime());
	}
	if (state->looking) {
		// The message can arrive before the transaction it names is served,
		// which is what a transfer just sent looks like, so an answer that
		// names nothing is asked again for a while before it is read as an
		// answer. The timer belongs to the box, so closing it stops asking.
		const auto lookup = [=] {
			session->wallet().resolveTransaction(
				state->item.id,
				crl::guard(box, [=](ResolvedTransaction resolved) {
					if (originCurrent && !originCurrent()) {
						return;
					} else if (resolved.item) {
						if (resolved.item->traceId.isEmpty()) {
							resolved.item->traceId = state->item.traceId;
						}
						state->item = std::move(*resolved.item);
						state->looking = false;
						rebuild(DetailsFee::Known);
						return;
					} else if (resolved.failed
						|| ++state->attempts >= kTransactionLookupAttempts) {
						rebuild(DetailsFee::Failed);
						return;
					}
					state->retry.callOnce(kTransactionLookupInterval);
				}));
		};
		state->retry.setCallback(lookup);
		lookup();
	}

	AddBoxCloseButton(box);
	const auto toggle = box->addTopButton(st::boxTitleMenu);
	const auto menu = box->lifetime().make_state<
		base::unique_qptr<Ui::PopupMenu>>();
	toggle->setClickedCallback([=] {
		if (*menu) {
			return;
		}
		*menu = base::make_unique_q<Ui::PopupMenu>(
			box,
			st::popupMenuWithIcons);
		const auto raw = menu->get();
		raw->setDestroyedCallback(crl::guard(toggle, [=] {
			toggle->setForceRippled(false);
		}));
		toggle->setForceRippled(true);
		// Read when the menu opens, not when the box was built: a served
		// transaction can name the trace a message did not carry.
		const auto url = ExplorerTransactionUrl(session, state->item.traceId);
		if (!url.isEmpty()) {
			raw->addAction(
				Ui::Text::FixAmpersandInAction(
					tr::lng_channel_earn_history_out_button(tr::now)),
				[=] { UrlClickHandler::Open(url); },
				&st::menuIconSearch);
		}
		raw->addAction(
			Ui::Text::FixAmpersandInAction(
				tr::lng_wallet_how_menu(tr::now)),
			[=] { show->showBox(Box(WalletHowItWorksBox, session)); },
			&st::menuIconFaq);
		raw->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
		const auto scope = WindowPaletteScope(box);
		raw->popup(toggle->mapToGlobal(QPoint(
			toggle->width(),
			toggle->height())));
	});

	if (openWallet) {
		box->addButton(tr::lng_wallet_details_open_wallet(), [=] {
			const auto open = openWallet;
			box->closeBox();
			open();
		});
	} else {
		box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
	}
	if (originCurrent) {
		const auto close = [weak = base::make_weak(box)] {
			if (weak && weak->hasDelegate()) {
				weak->closeBox();
			}
		};
		std::move(originInvalidated) | rpl::take(1) | rpl::on_next(
			close,
			box->lifetime());
		if (!originCurrent()) {
			close();
		}
	}
}

void ShowWalletTransactionBox(
		std::shared_ptr<Main::SessionShow> show,
		const TransferItem &item,
		std::shared_ptr<CollectibleMedia> media,
		rpl::producer<TransferItem> updates) {
	const auto current = [show, identity = item.walletIdentity] {
		if (!show || !show->valid()) {
			return false;
		}
		return !identity
			|| show->session().wallet().transferWalletIdentityCurrent(*identity);
	};
	if (!current()) {
		return;
	}
	show->showBox(Box(
		WalletTransactionBox,
		show,
		item,
		false,
		std::move(media),
		current,
		show->session().wallet().transferWalletIdentityChanges()
			| rpl::filter([=] { return !current(); }),
		Fn<void()>(),
		std::move(updates)), Ui::LayerOption::KeepOther);
}

[[nodiscard]] int CommentBytes(const QString &text) {
	return SendCommentBytes(text);
}

[[nodiscard]] bool CommentFits(const QString &text) {
	return SendCommentFits(text);
}

// A comment is limited in UTF-8 bytes, so a Cyrillic letter costs two and an
// emoji four. Saying that in words explains nothing to anyone typing, so the
// field just counts down, and it starts counting late enough that the people
// who never approach the limit never see it. The limit is in bytes while a
// field's own maximum is in UTF-16 units, so the field keeps no maximum of
// its own and the count is allowed to go negative until sending refuses it.
//
// `rightSkip` is what already stands in the field's top right corner, which
// the counter has to stand to the left of.
void ApplyCommentLimit(
		not_null<Ui::InputField*> field,
		int rightSkip) {
	const auto &limitSt = st::defaultInputFieldLimit;
	auto options = Ui::LengthLimitLabelOptions{
		.customThreshold = kSendCommentWarnBytes,
		.customCharactersCount = [=] {
			return CommentBytes(field->getLastText());
		},
	};
	if (rightSkip > 0) {
		options.customUpdatePosition = [=](QSize parent, QSize label) {
			const auto &st = field->st();
			// Baseline alignment, the way the default position does it.
			const auto top = st.textMargins.top()
				+ st.style.font->ascent
				- limitSt.style.font->ascent;
			return QPoint(parent.width() - rightSkip - label.width(), top);
		};
	}
	Ui::AddLengthLimitLabel(field, kSendCommentMaxBytes, std::move(options));

	// A field's own maximum counts UTF-16 units, so it cannot enforce a
	// limit that counts bytes. What it can do is keep a paste from running
	// the counter off into the thousands. Two units per allowed byte leaves
	// every comment that fits typeable - the densest of them, all emoji or
	// all Cyrillic, is half that - and no single unit is worth more than
	// three bytes, so `deepest` is as far below zero as the counter can go.
	field->setMaxLength(kSendCommentMaxLength);
	const auto deepest = 3 * kSendCommentMaxLength - kSendCommentMaxBytes;
	const auto widest = limitSt.style.font->width(
		QChar(0x2212) + QString::number(deepest));
	field->setAdditionalMargins({
		0,
		0,
		std::max(rightSkip + widest - field->st().textMargins.right(), 0),
		0,
	});
}

} // namespace ContentDetails

} // namespace Wallet
