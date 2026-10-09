#include "wallet/wallet_content_internal.h"

#include "extras/features/window_material/window_material.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

[[nodiscard]] TextWithEntities IslandAmount(
		const TextWithEntities &mark,
		CreditsAmount amount) {
	auto result = mark;
	result.append(QChar(' '));
	result.append(Ui::Text::Colorized(
		Lang::FormatCreditsAmountToShort(amount).string));
	return result;
}

void AddIslandRowLabel(
		not_null<InfoIslandEntry*> button,
		rpl::producer<TextWithEntities> text,
		Ui::Text::MarkedContext context) {
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button.get(),
		std::move(text),
		st::walletIslandRowLabel,
		st::defaultPopupMenu,
		std::move(context));
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	label->setTryMakeSimilarLines(true);
	const auto updateLabelGeometry = [=] {
		const auto &padding = st::walletIslandRow.padding;
		const auto available = button->width()
			- padding.left()
			- st::walletIslandLabelRightSkip;
		if (available <= 0) {
			return;
		}
		label->resizeToWidth(available);
		button->setMinimalHeight(label->height()
			+ padding.top()
			+ padding.bottom());
		label->moveToLeft(
			padding.left(),
			(button->height() - label->height()) / 2,
			button->width());
	};
	button->widthValue(
	) | rpl::on_next(updateLabelGeometry, button->lifetime());
	label->heightValue(
	) | rpl::on_next(updateLabelGeometry, label->lifetime());
}

void Content::setupInfoIsland() {
	auto owned = object_ptr<InfoIsland>(_pinnedInner);
	const auto island = owned.data();
	const auto wrap = _pinnedInner->add(
		object_ptr<Ui::SlideWrap<InfoIsland>>(
			_pinnedInner,
			std::move(owned),
			style::margins(0, st::walletCardTopSkip, 0, 0)));
	setupCustodyEntry(island);
	setupWaltEntry(island);
	setupEarningsEntry(island);
	setupOldWalletEntry(island);
	wrap->toggleOn(island->anyShownValue(), anim::type::normal);
}

void Content::setupWaltEntry(not_null<InfoIsland*> island) {
	const auto session = &_show->session();
	const auto wrap = island->add(
		object_ptr<InfoIslandEntry>(
			island,
			tr::lng_wallet_walt_existing(),
			st::walletIslandRow));
	const auto button = wrap->entity();
	AddRowChevron(button);
	button->setClickedCallback([=] {
		const auto url = session->wallet().existingWaltBalanceUrl();
		if (!url.isEmpty()) {
			OpenWalletUrl(session, _show, url);
		}
	});
	wrap->toggleOn(session->wallet().existingWaltBalanceUrlValue(
	) | rpl::map([](const QString &url) {
		return !url.isEmpty();
	}), anim::type::normal);
}

void Content::setupEarningsEntry(not_null<InfoIsland*> island) {
	const auto session = &_show->session();
	const auto wrap = island->add(
		object_ptr<InfoIslandEntry>(island, nullptr, st::walletIslandRow));
	const auto button = wrap->entity();
	AddRowChevron(button);

	auto helper = Ui::Text::CustomEmojiHelper();
	const auto mark = GramMark(helper, st::walletIslandRowLabel.style.font);
	AddIslandRowLabel(
		button,
		tr::lng_wallet_earnings_existing(
			lt_amount,
			session->credits().tonBalanceValue(
			) | rpl::map([=](CreditsAmount value) {
				return IslandAmount(mark, value);
			}),
			tr::marked),
		helper.context());

	button->setClickedCallback([=] {
		if (const auto window = session->tryResolveWindow()) {
			window->showSettings(Settings::CurrencyId());
			window->window().activate();
		}
	});

	session->credits().tonLoad();
	wrap->toggleOn(session->credits().tonBalanceValue(
	) | rpl::map([](CreditsAmount value) {
		return !value.empty();
	}), anim::type::normal);
}

void Content::setupProtectRow() {
	const auto session = &_show->session();
	const auto column = _column->entity();
	const auto wrap = column->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			column,
			object_ptr<Ui::VerticalLayout>(column)));
	const auto inner = wrap->entity();
	Ui::AddSkip(inner, st::walletProtectRowSkip);
	const auto button = Settings::AddButtonWithIcon(
		inner,
		tr::lng_wallet_protect_account(),
		st::walletProtectRow,
		{ .icon = &st::walletProtectRowIcon });
	ExtrasFeatures::WindowMaterial::watchSurface(button);
	AddRowChevron(button);
	button->setClickedCallback([=] {
		_show->showBox(Box(WalletCloudPasswordIntroBox, _show));
	});

	auto &cloud = session->api().cloudPassword();
	cloud.reload();
	auto off = rpl::single(false) | rpl::then(cloud.state(
	) | rpl::map([](const Core::CloudPasswordState &state) {
		return !state.hasPassword && state.unconfirmedPattern.isEmpty();
	}));
	auto nonEmpty = rpl::combine(
		session->wallet().balanceNanoValue(),
		HistoryShownValue(session)
	) | rpl::map([](int64 balance, bool history) {
		return (balance != 0) || history;
	});
	wrap->toggleOn(rpl::combine(
		std::move(off),
		std::move(nonEmpty)
	) | rpl::map([](bool off, bool nonEmpty) {
		return off && nonEmpty;
	}) | rpl::distinct_until_changed(), anim::type::instant);
}

void Content::setupBalance() {
	_ink = std::make_unique<BalanceInk>();

	_pinnedBalance = Ui::CreateChild<Ui::RpWidget>(_pinned);
	_pinnedBalance->setAttribute(Qt::WA_TransparentForMouseEvents);
	_pinnedBalance->show();
	_pinnedBalance->raise();
	_pinnedBalance->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		const auto fold = cardFold();
		const auto ink = _ink->boundingRect(fold);
		if (!clip.intersects(ink)) {
			return;
		}
		auto p = QPainter(_pinnedBalance);
		auto hq = PainterHighQualityEnabler(p);
		_ink->paint(p, fold, cardOutline(), clip);
	}, _pinnedBalance->lifetime());

	_titleBalance.reset(Ui::CreateChild<Ui::RpWidget>(window()));
	_titleBalance->setAttribute(Qt::WA_TransparentForMouseEvents);
	_titleBalance->show();
	_titleBalance->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_titleBalance.get());
		paintTitle(p, cardFold().fold);
		auto hq = PainterHighQualityEnabler(p);
		p.translate(0, st::separatePanelTitleHeight);
		_ink->paint(
			p,
			cardFold(),
			cardOutline(),
			_titleBalance->rect().translated(
				0,
				-st::separatePanelTitleHeight));
	}, _titleBalance->lifetime());

	const auto repaintBalance = [=] {
		_pinnedBalance->update();
		_titleBalance->update();
		_paintedInk = _ink->boundingRect(cardFold());
	};

	widthValue(
	) | rpl::on_next([=](int width) {
		const auto scope = WindowPaletteScope(this);
		_ink->setOuterWidth(width);
		repaintBalance();
	}, lifetime());

	SetupCardBalance(
		_ink.get(),
		&_show->session(),
		repaintBalance,
		this);

	style::PaletteChanged(
	) | rpl::on_next([=] {
		const auto scope = WindowPaletteScope(this);
		_ink->refresh();
		_card->invalidateCache();
		_card->update();
		_pinnedBackground->update();
		repaintBalance();
	}, lifetime());

	tr::lng_wallet_menu(
	) | rpl::on_next([=](const QString &title) {
		_title.setText(
			st::separatePanelTitle.style,
			title,
			kPlainTextOptions);
		const auto scope = WindowPaletteScope(this);
		_ink->refresh();
		repaintBalance();
	}, lifetime());

	SetupCardMark(_ink.get(), this, std::make_shared<bool>(), [=] {
		const auto mark = _ink->markPaintRect(cardFold());
		_pinnedBalance->update(mark);
		_titleBalance->update(
			mark.translated(0, st::separatePanelTitleHeight));
	});
}

const CardFold &Content::cardFold() const {
	return _card->fold();
}

QRegion Content::cardOutline() const {
	if (_card->isHidden()) {
		return {};
	}
	return QRegion(_card->paintedQuad().toPolygon());
}

void Content::paintTitle(QPainter &p, float64 fold) {
	const auto &st = st::separatePanelTitle;
	const auto left = st::separatePanelTitleLeft;
	const auto top = st::separatePanelTitleTop;
	const auto textWidth = std::min(
		(width()
			- left
			- st::separatePanelClose.width
			- st::separatePanelMenu.width),
		_title.maxWidth());
	const auto fullHeight = _title.countHeight(textWidth);
	const auto titleHeight = std::min(fullHeight, st.maxHeight);
	const auto elided = (st.maxHeight < fullHeight)
		|| (textWidth < _title.maxWidth());
	const auto lineHeight = std::max(
		st.style.lineHeight,
		st.style.font->height);
	const auto box = QRect(left, top, textWidth, titleHeight);
	p.save();
	p.setClipRect(box);
	if (fold > 0.) {
		const auto scale = 1. - (1. - st::walletTitleFoldScale) * fold;
		const auto center = QPointF(left, top + titleHeight / 2.);
		p.setOpacity(1. - fold);
		p.translate(center);
		p.scale(scale, scale);
		p.translate(-center);
	}
	p.setPen(_panel->titleOverridePalette()->windowFg()->c);
	_title.draw(p, {
		.position = { left, top },
		.availableWidth = textWidth,
		.align = st.align,
		.clip = box,
		.palette = &st.palette,
		.elisionHeight = (elided ? std::max(st.maxHeight, lineHeight) : 0),
		.elisionLines = 0,
	});
	p.restore();
}

void Content::setupTabs(rpl::producer<bool> collectiblesShown) {
	_tabsWrap = _pinnedInner->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsSlider>>(
			_pinnedInner,
			object_ptr<Ui::SettingsSlider>(
				_pinnedInner,
				st::walletTabsSlider)));
	const auto tabs = _tabsWrap->entity();
	tabs->setSections({
		tr::lng_wallet_rows_title(tr::now),
		tr::lng_wallet_rows_collectibles(tr::now),
	});
	tabs->fitWidthToSections();
	tabs->setNaturalWidth(tabs->width());
	_tabsShadow = Ui::CreateChild<Ui::PlainShadow>(this);

	// Without collectibles the first tab stands alone, scrolling with the list.
	const auto column = _column->entity();
	const auto title = column->insert(
		0,
		object_ptr<Ui::SlideWrap<Ui::SettingsSlider>>(
			column,
			object_ptr<Ui::SettingsSlider>(column, st::walletRowsTitle)));
	const auto label = title->entity();
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	tr::lng_wallet_rows_title(
	) | rpl::on_next([=](const QString &text) {
		label->setSections({ text });
		label->fitWidthToSections();
		label->setNaturalWidth(label->width());
	}, label->lifetime());

	const auto wallet = &_show->session().wallet();
	tabs->setActiveSectionFast(wallet->collectiblesTab() ? 1 : 0);
	tabs->sectionActivated(
	) | rpl::on_next([=](int index) {
		wallet->setCollectiblesTab(index == 1);
		_scroll->scrollToY(0);
	}, tabs->lifetime());

	wallet->collectiblesTabValue(
	) | rpl::on_next([=](bool collectibles) {
		const auto index = collectibles ? 1 : 0;
		if (tabs->activeSection() != index) {
			tabs->setActiveSectionFast(index);
		}
	}, tabs->lifetime());

	rpl::combine(
		std::move(collectiblesShown),
		TransactionsShownValue(&_show->session())
	) | rpl::map([](bool shown, bool transactions) {
		return std::make_pair(shown, transactions && !shown);
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](std::pair<bool, bool> sections) {
		// WHY: the section that shows goes in before the one that hides, so
		// no height in between lets the scroll area clamp a scrolled reader.
		if (sections.second) {
			title->toggle(true, anim::type::instant);
		}
		_tabsShown = sections.first;
		_tabsWrap->toggle(sections.first, anim::type::instant);
		_tabsShadow->setVisible(sections.first);
		if (!sections.second) {
			title->toggle(false, anim::type::instant);
		}
		updateRegions();
	}, lifetime());
}

void Content::setupStrip() {
	_stripShadow = Ui::CreateChild<Ui::PlainShadow>(this);
	_strip = Ui::CreateChild<Ui::RpWidget>(this);
	_strip->paintRequest(
	) | rpl::on_next([=] {
		if (ExtrasFeatures::WindowMaterial::isActive(_strip)) {
			return;
		}
		auto p = QPainter(_strip);
		PaintBottomRoundedPlate(p, _strip->rect(), st::windowBgOver);
	}, _strip->lifetime());

	const auto wallet = &_show->session().wallet();
	auto minAmount = rpl::single(rpl::empty) | rpl::then(
		wallet->historyUpdates()
	) | rpl::map([=] {
		return wallet->transferMinNanos();
	}) | rpl::distinct_until_changed(
	) | rpl::map([](int64 nanos) {
		return Ui::FormatTonAmount(nanos).full;
	});
	const auto hint = Ui::CreateChild<Ui::FlatLabel>(
		_strip,
		tr::lng_wallet_rows_hidden_below(lt_amount, std::move(minAmount)),
		st::defaultSubTextLabel);
	hint->setAttribute(Qt::WA_TransparentForMouseEvents);
	hint->show();
	rpl::combine(
		_strip->sizeValue(),
		hint->sizeValue()
	) | rpl::on_next([=](QSize size, QSize) {
		hint->moveToLeft(
			st::boxRowPadding.left(),
			(size.height() - hint->height()) / 2,
			size.width());
	}, hint->lifetime());

	TransactionsShownValue(
		&_show->session()
	) | rpl::on_next([=](bool shown) {
		_stripShown = shown;
		_strip->setVisible(shown);
		_stripShadow->setVisible(shown);
		updateRegions();
	}, lifetime());
}

void Content::setupListsLoading() {
	_listsLoading = Ui::CreateChild<Ui::RpWidget>(this);
	_listsLoading->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto &loading = st::walletListsLoading;
	const auto side = loading.size.height() + 2 * loading.thickness;
	const auto indicator = Info::Statistics::InfiniteRadialAnimationWidget(
		_listsLoading,
		side,
		&loading);
	indicator->setAttribute(Qt::WA_TransparentForMouseEvents);
	Info::Statistics::AddChildToWidgetCenter(_listsLoading, indicator);

	const auto addCaption = [=](rpl::producer<QString> text) {
		const auto caption = Ui::CreateChild<Ui::FlatLabel>(
			_listsLoading,
			std::move(text),
			st::walletAboutTextLabel);
		caption->setAttribute(Qt::WA_TransparentForMouseEvents);
		_listsLoading->sizeValue(
		) | rpl::on_next([=](QSize size) {
			caption->resizeToNaturalWidth(size.width());
			caption->moveToLeft(
				(size.width() - caption->width()) / 2,
				((size.height() + side) / 2) + st::walletAboutTitleSkip,
				size.width());
		}, caption->lifetime());
		return caption;
	};
	const auto caption = addCaption(tr::lng_wallet_provisioning());
	_show->session().wallet().presenceValue(
	) | rpl::map(
		rpl::mappers::_1 == Presence::Provisioning
	) | rpl::on_next([=](bool provisioning) {
		caption->setVisible(provisioning);
	}, caption->lifetime());
	const auto walkingCaption = addCaption(
		tr::lng_wallet_history_searching());

	// The gate is not the only state with nothing to paint. A feed whose
	// loaded pages are all hidden while the server still offers a cursor is
	// walking towards a row it can show, and this indicator - the one an
	// unsettled feed already renders, with its caption bound to
	// Provisioning and so hidden here - is the only face that says so. It
	// resolves into rows or into the About face when the cursor exhausts.
	// A visible row or a pending send makes it a lie, and so does the
	// Collectibles tab, whose own list this region does not describe.
	rpl::combine(
		_show->session().wallet().listsGatedValue(),
		_show->session().wallet().historyLoadingMoreValue(),
		_show->session().wallet().collectiblesTabValue(),
		HistoryShownValue(&_show->session())
	) | rpl::map([](
			bool gated,
			bool loadingMore,
			bool collectiblesTab,
			bool historyShown) {
		// The gate and the walk are the region's two reasons to show, and
		// only the walk is the one the second caption speaks for, so both
		// bits leave here together: written by one handler, the caption
		// can neither outlive the region nor appear without it.
		// updateListsGate() makes Provisioning - the state the first
		// caption is bound to - one of the gate's own disjuncts, so the
		// two captions are mutually exclusive by the same expression.
		return std::make_pair(
			gated,
			!gated && loadingMore && !collectiblesTab && !historyShown);
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](std::pair<bool, bool> face) {
		const auto shown = face.first || face.second;
		walkingCaption->setVisible(face.second);
		indicator->setVisible(shown);
		_listsLoading->setVisible(shown);
		updateRegions();
	}, lifetime());
}

void Content::setupCustodyEntry(not_null<InfoIsland*> island) {
	const auto wrap = island->add(
		object_ptr<InfoIslandEntry>(island, nullptr, st::walletIslandRow));
	wrap->toggle(false, anim::type::instant);

	const auto button = wrap->entity();
	_custodyBarLabel = Ui::CreateChild<Ui::FlatLabel>(
		button,
		st::walletInfoBarLabel);
	_custodyBarLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	_custodyBarLabel->setTryMakeSimilarLines(true);
	const auto updateLabelGeometry = [=] {
		const auto available = button->width()
			- 2 * st::walletInfoBarLabelSkip;
		if (available <= 0) {
			return;
		}
		_custodyBarLabel->resizeToWidth(
			std::min(_custodyBarLabel->textMaxWidth(), available));
		const auto &padding = st::walletIslandRow.padding;
		button->setMinimalHeight(_custodyBarLabel->height()
			+ padding.top()
			+ padding.bottom());
		_custodyBarLabel->moveToLeft(
			(button->width() - _custodyBarLabel->width()) / 2,
			(button->height() - _custodyBarLabel->height()) / 2,
			button->width());
	};
	button->widthValue(
	) | rpl::on_next(updateLabelGeometry, button->lifetime());

	button->setClickedCallback([=] {
		_show->showBox(Box(
			WalletImportBox,
			_show,
			WalletImportMode::Restore,
			nullptr,
			nullptr,
			nullptr));
	});

	// The old wallet entry takes a conflict, and resolving it comes first.
	auto &wallet = _show->session().wallet();
	rpl::combine(
		wallet.deviceCustodyStateValue(),
		wallet.presenceValue()
	) | rpl::map([](DeviceCustodyState state, Presence presence) {
		return (presence == Presence::Ready)
			&& !state.conflict
			&& (state.mode == DeviceMode::ReadOnlyNotRestorable);
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](bool shown) {
		if (shown) {
			_custodyBarLabel->setText(tr::lng_wallet_readonly_bar(tr::now));
			updateLabelGeometry();
		}
		wrap->toggle(shown, anim::type::normal);
	}, lifetime());
}

void Content::setupOldWalletEntry(not_null<InfoIsland*> island) {
	const auto wallet = &_show->session().wallet();
	const auto wrap = island->add(
		object_ptr<InfoIslandEntry>(island, nullptr, st::walletIslandRow));
	wrap->toggle(false, anim::type::instant);
	const auto button = wrap->entity();
	AddRowChevron(button);

	auto helper = Ui::Text::CustomEmojiHelper();
	const auto mark = GramMark(helper, st::walletIslandRowLabel.style.font);
	AddIslandRowLabel(
		button,
		wallet->parkedBalanceNanoValue(
		) | rpl::map([=](std::optional<int64> nano) {
			return (nano && *nano > 0)
				? tr::lng_wallet_conflict_existing(
					lt_amount,
					rpl::single(IslandAmount(mark, CreditsAmount(
						*nano / Ui::kNanosInOne,
						*nano % Ui::kNanosInOne,
						CreditsType::Ton))),
					tr::marked)
				: tr::lng_wallet_conflict_bar(tr::marked);
		}) | rpl::flatten_latest(),
		helper.context());

	button->setClickedCallback([=] {
		_show->showBox(Box(WalletConflictBox, _show, nullptr));
	});

	wrap->toggleOn(rpl::combine(
		wallet->deviceCustodyStateValue(),
		wallet->presenceValue()
	) | rpl::map([](DeviceCustodyState state, Presence presence) {
		return (presence == Presence::Ready) && state.conflict;
	}), anim::type::normal);
}

int Content::pinnedMax() const {
	return _pinnedInner->height();
}

int Content::pinnedMin() const {
	return _tabsShown ? st::walletTabsSlider.height : 0;
}

QRect Content::cardRest() const {
	return QRect(
		_cardPlaceholder->x(),
		_cardPlaceholder->y(),
		_cardPlaceholder->width(),
		st::walletCardHeight);
}

Content::Regions Content::countRegions(int columnHeight) const {
	const auto max = pinnedMax();
	const auto min = pinnedMin();
	const auto stripHeight = _stripShown
		? (st::walletRowsHintHeight + st::lineWidth)
		: 0;
	const auto open = height() - max - stripHeight;
	const auto reserve = (columnHeight > open) ? (max - min) : 0;
	const auto scrollTop = max - reserve;
	return {
		.reserve = reserve,
		.scrollTop = scrollTop,
		.scrollHeight = std::max(0, height() - scrollTop - stripHeight),
	};
}

float64 Content::foldProgress() const {
	// The card's rest bottom, measured down from the pinned top, is the
	// placeholder's own bottom inside _pinnedInner, because that layout's
	// top sits at the pinned top with nothing scrolled away. So this is
	// the scroll distance that carries the card's bottom edge up to the
	// title bar, and no Content-space rest rect has to be re-derived.
	const auto travel = _cardPlaceholder->y() + _cardPlaceholder->height();
	const auto scrolled = std::clamp(_scroll->scrollTop(), 0, _reserve);
	return std::clamp(scrolled / float64(travel), 0., 1.);
}

void Content::updateRegions() {
	if (!width() || !height()) {
		return;
	}
	_container->resize(width(), _container->height());
	if (_pinnedInner->widthNoMargins() != width()) {
		_pinnedInner->resizeToWidth(width());
	}
	const auto max = pinnedMax();
	const auto regions = countRegions(_column->entity()->height());
	_reserve = regions.reserve;
	_column->setPadding({ 0, _reserve, 0, 0 });
	_scroll->setGeometry(
		0,
		regions.scrollTop,
		width(),
		regions.scrollHeight);
	if (_listsLoading && !_listsLoading->isHidden()) {
		_listsLoading->setGeometry(0, max, width(), height() - max);
	}

	const auto body = Ui::MapFrom(window(), this, rect());
	_titleBalance->setGeometry(
		body.x(),
		body.y() - st::separatePanelTitleHeight,
		(width()
			- st::separatePanelClose.width
			- st::separatePanelMenu.width),
		st::separatePanelTitleHeight);

	updatePinned();
	if (_stripShown) {
		const auto stripTop = std::max(
			regions.scrollTop,
			height() - st::walletRowsHintHeight);
		_stripShadow->setGeometry(
			0,
			stripTop - st::lineWidth,
			width(),
			st::lineWidth);
		_strip->setGeometry(0, stripTop, width(), st::walletRowsHintHeight);
	}
	_headerShadow->setGeometry(0, 0, width(), st::lineWidth);
	updateVisibleArea();
}

bool Content::revealSendingRow() {
	if (!_sendingRow || !base::take(_sendingRow->revealPending)) {
		return false;
	}
	auto &wallet = _show->session().wallet();
	if (wallet.collectiblesTab()) {
		wallet.setCollectiblesTab(false);
		_scroll->scrollToY(0);
		return false;
	}
	const auto slot = _sendingRow->slot;
	const auto top = Ui::MapFrom(_container, slot, QPoint()).y();
	const auto bottom = top + slot->height();
	const auto scrollTop = _scroll->scrollTop();
	if (top < std::max(scrollTop, _reserve)) {
		_scroll->scrollToY(top);
	} else if (bottom > scrollTop + _scroll->height()) {
		_scroll->scrollToY(bottom - _scroll->height());
	}
	return true;
}

void Content::flySendDiamond(
		const std::string &operationId,
		not_null<Ui::TonAmountInput*> amount) {
	const auto current = [&]() -> SendingHistoryRow* {
		return (_sendingRow && _sendingRow->operationId == operationId)
			? _sendingRow->look
			: nullptr;
	};
	if (!current() || anim::Disabled()) {
		return;
	}
	const auto body = dynamic_cast<Ui::RpWidget*>(parentWidget());
	const auto now = crl::now();
	const auto revealed = body && revealSendingRow();
	const auto look = current();
	if (!look) {
		return;
	}
	auto diamond = AmountDiamond();
	if (revealed && look->surfaceShown()) {
		const auto slot = look->diamondTarget(now).translated(
			QPointF(Ui::MapFrom(_scroll.data(), look, QPoint())));
		const auto occluded = _reserve
			- std::clamp(_scroll->scrollTop(), 0, _reserve);
		if (slot.top() >= occluded && slot.bottom() <= _scroll->height()) {
			diamond = TakeAmountDiamond(amount);
		}
	}
	if (!diamond.icon) {
		look->scheduleBump(now + kSendingRowFlightDuration);
		return;
	}
	look->awaitDiamond();
	struct State {
		DiamondFlight *flight = nullptr;
		bool landed = false;
	};
	const auto state = std::make_shared<State>();
	const auto weak = base::make_weak(look);
	const auto loopStarted = SendingDiamondLoopStart(diamond.icon.get(), now);
	_diamondFlight = std::make_unique<DiamondFlight>(DiamondFlightArgs{
		.body = body,
		.icon = std::move(diamond.icon),
		.from = diamond.global,
		.loopStarted = loopStarted,
		.duration = kSendingRowFlightDuration,
		.target = [=](crl::time at) -> std::optional<QRectF> {
			const auto row = weak.get();
			if (!row || !row->surfaceShown()) {
				return std::nullopt;
			}
			return row->diamondTarget(at).translated(
				QPointF(Ui::MapFrom(body, row, QPoint())));
		},
		.landed = [=](std::unique_ptr<Lottie::Icon> icon, crl::time loop) {
			if (const auto row = weak.get()) {
				state->landed = true;
				row->landDiamond(std::move(icon), loop);
			}
		},
		.finished = [=] {
			if (const auto row = state->landed ? nullptr : weak.get()) {
				row->cancelDiamondAwait();
			}
			crl::on_main(this, [=] {
				if (_diamondFlight.get() == state->flight) {
					_diamondFlight = nullptr;
				}
			});
		},
	});
	state->flight = _diamondFlight.get();
}

void Content::updateVisibleArea() {
	const auto top = _scroll->scrollTop();
	_column->setVisibleTopBottom(top, top + _scroll->height());
}

void Content::updatePinned() {
	if (!width() || !height()) {
		return;
	}
	const auto max = pinnedMax();
	const auto min = pinnedMin();
	const auto top = std::clamp(_scroll->scrollTop(), 0, _reserve);
	const auto height = max - top;
	_pinnedInner->moveToLeft(0, height - max, width());
	_pinned->setGeometry(0, 0, width(), height);
	_pinnedBackground->setGeometry(0, 0, width(), height);
	const auto rest = cardRest();
	const auto fold = ComputeCardFold(rest, foldProgress());
	const auto cardWidget = QRect(
		QPoint(rest.left(), 0),
		rest.bottomRight());
	_card->setGeometry(cardWidget);
	_card->setFold(fold);
	const auto shown = fold.valid && fold.opacity > 0.;
	if (shown) {
		const auto outline = _card->paintedOutline();
		const auto bounds = outline.boundingRect().toAlignedRect();
		_cardButton->setGeometry(bounds);
		_cardButton->setMask(QRegion(
			outline.translated(-bounds.topLeft()).toPolygon()));
	}
	_cardButton->setVisible(shown);
	_scroll->setVerticalBarTopSkip(height - min);
	_tabsShadow->setGeometry(0, height, width(), st::lineWidth);
	_headerShadow->setVisible(height == min);
	_pinnedBalance->setGeometry(_pinned->rect());

	// The card's dirty area is its whole widget rect, which contains
	// every folded quad, and the ink's is the union of the rects it was
	// and is painted into.
	const auto inkPainted = _ink->boundingRect(fold);
	const auto inkDirty = _paintedInk.united(inkPainted);
	_paintedInk = inkPainted;
	update(cardWidget);
	update(inkDirty);
	_pinnedBackground->update(cardWidget);
	_pinnedBalance->update(cardWidget);
	_pinnedBalance->update(inkDirty);
	if (_paintedHeight == height && _paintedMin == min) {
		return;
	}
	_paintedHeight = height;
	_paintedMin = min;
	_pinnedBackground->update();
	// Reaching here means the pinned height changed, and with it the
	// scrolled distance the fold progress is computed from. The band
	// overlay carries the fading title and the end of the balance's
	// travel, and it is at most the title bar's height tall, so it is
	// repainted whole rather than tracked rect by rect.
	_titleBalance->update();
}

void Content::checkLoadMore() {
	auto &wallet = _show->session().wallet();
	if (wallet.listsGated()) {
		return;
	}
	const auto collectibles = wallet.collectiblesTab();
	const auto hasNext = collectibles
		? wallet.collectiblesHasNext()
		: wallet.historyHasNext();
	if (!hasNext) {
		return;
	}
	if (_scroll->scrollTop() + _scroll->height() >= _scroll->scrollTopMax()) {
		if (collectibles) {
			wallet.loadMoreCollectibles();
		} else {
			wallet.loadMoreHistory();
		}
	}
}

void Content::focusInEvent(QFocusEvent *e) {
	_scroll->setFocus();
}

void Content::resizeEvent(QResizeEvent *e) {
	updateRegions();
	_loadMoreCheck.call();
}

void Content::paintEvent(QPaintEvent *e) {
	// 窗口材质生效时主体透出材质，与主窗口一致。
	if (ExtrasFeatures::WindowMaterial::isActive(this)) {
		return;
	}
	auto p = QPainter(this);
	if (_stripShown) {
		p.fillRect(0, 0, width(), _strip->y(), st::windowBg);
		return;
	}
	PaintBottomRoundedPlate(p, rect(), st::windowBg);
}

} // namespace ContentDetails

} // namespace Wallet
