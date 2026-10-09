#include "wallet/wallet_content_internal.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

[[nodiscard]] QString SendUserLoadErrorText(const QString &error) {
	if (error == u"WALLET_UNAVAILABLE"_q) {
		return tr::lng_wallet_unavailable(tr::now);
	} else if (error == u"WALLET_NOT_READY"_q) {
		return tr::lng_wallet_state_error(tr::now);
	} else if (error == u"WALLET_BALANCE_EMPTY"_q) {
		return tr::lng_wallet_send_error_insufficient(tr::now);
	} else if (error == u"WALLET_USER_INVALID"_q
		|| error == u"WALLET_USER_INELIGIBLE"_q) {
		return tr::lng_wallet_send_user_unavailable(tr::now);
	} else if (error == u"WALLET_ADDRESS_INVALID"_q) {
		return tr::lng_wallet_send_user_load_error(tr::now);
	}
	return ErrorWithType(tr::lng_wallet_state_error(tr::now), error);
}

// A button that cannot be pressed yet keeps the background its own style
// gives it and fades its label halfway into that background, which is how the
// rest of the app shows a disabled button. The colors come from the button's
// own style, so an attention or light button fades into its own background
// instead of an active button's.
void ApplyButtonDisabledLook(not_null<Ui::RoundButton*> button) {
	const auto color = [&]() -> std::optional<QColor> {
		if (!button->isDisabled()) {
			return std::nullopt;
		}
		const auto scope = WindowPaletteScope(button);
		const auto &buttonStyle = button->st();
		return anim::color(buttonStyle.textBg, buttonStyle.textFg, 0.5);
	}();
	button->setTextFgOverride(color);
}

void SetButtonDisabledLook(
		not_null<Ui::RoundButton*> button,
		bool disabled) {
	if (disabled) {
		button->clearState();
	}
	button->setDisabled(disabled);
	button->setAttribute(Qt::WA_TransparentForMouseEvents, disabled);
	ApplyButtonDisabledLook(button);
	if (!button->property("walletDisabledLook").toBool()) {
		button->setProperty("walletDisabledLook", true);
		style::PaletteChanged() | rpl::on_next([=] {
			ApplyButtonDisabledLook(button);
		}, button->lifetime());
	}
}

// The send box offers to fund an empty wallet, so the balance never hides one.
[[nodiscard]] bool CanSendToUser(
		not_null<Main::Session*> session,
		UserId id) {
	const auto error = session->wallet().userAddresses().forceResolveError(id);
	return error.isEmpty() || (error == u"WALLET_BALANCE_EMPTY"_q);
}

[[nodiscard]] UserData *SendableUser(
		not_null<Main::Session*> session,
		UserId id) {
	const auto user = id ? session->data().userLoaded(id) : nullptr;
	if (!user || !CanSendToUser(session, id)) {
		return nullptr;
	}
	return user;
}

[[nodiscard]] bool SendsToOwnWallet(
		not_null<Main::Session*> session,
		const QString &destination) {
	const auto identity = session->wallet().transferWalletIdentity();
	return identity
		&& !destination.isEmpty()
		&& (CanonicalAddress(destination) == identity->address);
}

void ChooseMoneyRecipient(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		not_null<UserData*> user) {
	ShowSendToUser(show, user, nullptr, 0, nullptr, box.get());
}

RecentMoneyRecipientsController::RecentMoneyRecipientsController(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Main::SessionShow> show,
	Fn<void(not_null<UserData*>)> choose)
: _box(box)
, _show(std::move(show))
, _session(&_show->session())
, _choose(std::move(choose)) {
}

void RecentMoneyRecipientsController::setContent(
		not_null<PeerListContent*> content) {
	_delegate.setContent(content);
	setDelegate(&_delegate);
}

void RecentMoneyRecipientsController::prepare() {
	_box->boxClosing() | rpl::on_next([=] {
		_closed = true;
		_shown = false;
	}, lifetime());
	_session->recentMoneyRecipients().updates() | rpl::on_next([=] {
		refresh();
	}, lifetime());
	rpl::merge(
		_session->recentPeers().updates(),
		_session->topPeers().updates()
	) | rpl::on_next([=] {
		fillIfEmpty();
	}, lifetime());
	const auto schedule = [=] { scheduleRefresh(); };
	const auto &wallet = _session->wallet();
	wallet.stateKnownValue() | rpl::skip(1) | rpl::on_next(
		schedule,
		lifetime());
	wallet.balanceNanoValue() | rpl::skip(1) | rpl::on_next(
		schedule,
		lifetime());
	_session->wallet().userAddresses().unavailableValue(
	) | rpl::skip(1) | rpl::on_next(schedule, lifetime());
	fillIfEmpty();
	refresh();
}

bool RecentMoneyRecipientsController::active() const {
	return _box
		&& !_closed
		&& _session
		&& _show->valid()
		&& &_show->session() == _session.get();
}

bool RecentMoneyRecipientsController::canOffer(
		not_null<UserData*> user) const {
	return active()
		&& &user->session() == _session.get()
		&& !user->isSelf() // the list a Clear leaves behind
		&& SendableUser(_session.get(), peerToUser(user->id)) == user;
}

void RecentMoneyRecipientsController::fillIfEmpty() {
	if (!active()) {
		return;
	}
	const auto eligible = [=](not_null<UserData*> user) {
		return canOffer(user);
	};
	_session->recentMoneyRecipients().fillIfEmpty(eligible);
}

void RecentMoneyRecipientsController::watchUsers() {
	_userLifetime.destroy();
	for (const auto &user : _users) {
		user->flagsValue() | rpl::skip(1) | rpl::on_next([=] {
			scheduleRefresh();
		}, _userLifetime);
		using Flag = Data::PeerUpdate::Flag;
		_session->changes().peerUpdates(
			user,
			Flag::FullInfo | Flag::SupportInfo | Flag::OnlineStatus
		) | rpl::on_next([=](const Data::PeerUpdate &update) {
			if (!active()) {
				return;
			}
			if (update.flags & Flag::OnlineStatus) {
				if (const auto row = delegate()->peerListFindRow(user->id.value)) {
					row->refreshStatus();
					delegate()->peerListUpdateRow(row);
				}
			}
			if (update.flags & (Flag::FullInfo | Flag::SupportInfo)) {
				scheduleRefresh();
			}
		}, _userLifetime);
	}
}

void RecentMoneyRecipientsController::refresh() {
	if (!active()) {
		_shown = false;
		return;
	}
	const auto &users = _session->recentMoneyRecipients().list();
	if (_users != users) {
		_users = users;
		watchUsers();
	}
	auto rows = std::vector<not_null<UserData*>>();
	rows.reserve(_users.size());
	for (const auto &user : _users) {
		if (canOffer(user)) {
			rows.push_back(user);
		}
	}
	const auto count = delegate()->peerListFullRowsCount();
	auto changed = (count != int(rows.size()));
	for (auto i = 0; !changed && i != count; ++i) {
		changed = (delegate()->peerListRowAt(i)->peer() != rows[i]);
	}
	if (changed) {
		while (delegate()->peerListFullRowsCount()) {
			delegate()->peerListRemoveRow(delegate()->peerListRowAt(0));
		}
		for (const auto &user : rows) {
			delegate()->peerListAppendRow(std::make_unique<PeerListRow>(user));
		}
		delegate()->peerListRefreshRows();
	}
	_shown = !rows.empty();
}

void RecentMoneyRecipientsController::scheduleRefresh() {
	if (!active() || _refreshQueued) {
		return;
	}
	_refreshQueued = true;
	crl::on_main(this, [=] {
		_refreshQueued = false;
		fillIfEmpty();
		refresh();
	});
}

void RecentMoneyRecipientsController::rowClicked(
		not_null<PeerListRow*> row) {
	if (!active()) {
		return;
	}
	const auto user = row->peer()->asUser();
	if (!user || !canOffer(user)) {
		return;
	}
	_choose(user);
}

Main::Session &RecentMoneyRecipientsController::session() const {
	return *_session;
}

void RecentMoneyRecipientsController::clear() {
	if (!active()) {
		return;
	}
	const auto session = _session;
	session->recentMoneyRecipients().clear();
	if (session) {
		session->local().writeSearchSuggestionsIfNeeded();
	}
}

rpl::producer<bool> RecentMoneyRecipientsController::shownValue() const {
	return _shown.value();
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeRecentMoneyRecipientsList(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		rpl::producer<bool> hidden,
		Fn<void(not_null<UserData*>)> choose) {
	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		box,
		object_ptr<Ui::VerticalLayout>(box));
	const auto wrap = result.data();
	const auto container = wrap->entity();
	const auto controller = container->lifetime().make_state<
		RecentMoneyRecipientsController>(
			box,
			std::move(show),
			std::move(choose));

	const auto header = container->add(object_ptr<Ui::RpWidget>(container));
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		header,
		tr::lng_recent_title(),
		st::windowFilterChatsSectionSubtitle);
	const auto clear = Ui::CreateChild<Ui::LinkButton>(
		header,
		QString(),
		st::boxLinkButton);
	tr::lng_recent_clear() | rpl::on_next([=](const QString &text) {
		clear->setText(text);
	}, clear->lifetime());
	clear->setClickedCallback([=] { controller->clear(); });
	rpl::combine(
		header->widthValue(),
		clear->naturalWidthValue(),
		label->heightValue()
	) | rpl::on_next([=](int width, int, int) {
		const auto &padding = st::walletSendRecentHeaderPadding;
		const auto available = std::max(
			width - padding.left() - padding.right(),
			0);
		clear->resizeToNaturalWidth(available);
		label->resizeToWidth(std::max(
			available - clear->width() - st::walletSendFieldMargin.bottom(),
			0));
		const auto height = std::max(
			st::windowFilterChatsSectionSubtitleHeight,
			std::max(label->height(), clear->height())
				+ padding.top()
				+ padding.bottom());
		header->resize(width, height);
		label->moveToLeft(padding.left(), (height - label->height()) / 2);
		clear->moveToRight(padding.right(), (height - clear->height()) / 2);
	}, header->lifetime());
	header->paintRequest() | rpl::on_next([=](QRect clip) {
		QPainter(header).fillRect(clip, st::searchedBarBg);
	}, header->lifetime());

	Ui::AddSkip(container, st::walletSendRecentListTopSkip);
	controller->setStyleOverrides(&st::peerListSingleRow);
	const auto content = container->add(
		object_ptr<PeerListContent>(container, controller));
	controller->setContent(content);
	Ui::AddSkip(container, st::walletSendRecentListSkip);
	const auto wasHidden = wrap->lifetime().make_state<bool>(false);
	rpl::combine(
		controller->shownValue(),
		std::move(hidden)
	) | rpl::on_next([=](bool shown, bool hide) {
		const auto animated = (hide != *wasHidden)
			? anim::type::instant
			: anim::type::normal;
		*wasHidden = hide;
		wrap->toggle(shown && !hide, animated);
	}, wrap->lifetime());
	wrap->finishAnimating();
	return result;
}

MoneyRecipientSearchController::MoneyRecipientSearchController(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Main::SessionShow> show,
	Fn<void(not_null<UserData*>)> choose)
: ChatsListBoxController(&show->session())
, _box(box)
, _show(std::move(show))
, _session(&_show->session())
, _choose(std::move(choose))
, _delegate(_show) {
}

Main::Session &MoneyRecipientSearchController::session() const {
	return *_session;
}

void MoneyRecipientSearchController::setContent(
		not_null<PeerListContent*> content) {
	_delegate.setContent(content);
	setDelegate(&_delegate);
}

void MoneyRecipientSearchController::prepareViewHook() {
	_box->boxClosing() | rpl::on_next([=] {
		_closed = true;
		// WHY: the list outlives boxClosing by the close animation, so drop
		// the pending global search now or its late answer fills the list.
		search(QString());
	}, lifetime());
}

UserData *MoneyRecipientSearchController::offered(
		not_null<PeerData*> peer) const {
	const auto user = peer->asUser();
	return (user
		&& &user->session() == _session
		&& SendableUser(_session, peerToUser(user->id)) == user)
		? user
		: nullptr;
}

auto MoneyRecipientSearchController::createRow(not_null<History*> history)
-> std::unique_ptr<Row> {
	return offered(history->peer)
		? std::make_unique<Row>(history)
		: nullptr;
}

void MoneyRecipientSearchController::rowClicked(
		not_null<PeerListRow*> row) {
	if (!_box
		|| _closed
		|| !_show->valid()
		|| &_show->session() != _session) {
		return;
	}
	const auto user = offered(row->peer());
	if (!user) {
		return;
	}
	_choose(user);
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeMoneyRecipientSearchList(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		rpl::producer<QString> query,
		Fn<void(not_null<UserData*>)> choose) {
	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		box,
		object_ptr<Ui::VerticalLayout>(box));
	const auto wrap = result.data();
	const auto container = wrap->entity();
	const auto controller = container->lifetime().make_state<
		MoneyRecipientSearchController>(
			box,
			std::move(show),
			std::move(choose));

	Ui::AddSkip(container, st::walletSendRecentListTopSkip);
	controller->setStyleOverrides(&st::peerListSingleRow);
	const auto content = container->add(
		object_ptr<PeerListContent>(container, controller));
	controller->setContent(content);
	Ui::AddSkip(container, st::walletSendRecentListSkip);
	std::move(query) | rpl::on_next([=](const QString &text) {
		wrap->toggle(!text.isEmpty(), anim::type::instant);
		content->searchQueryChanged(text);
	}, wrap->lifetime());
	wrap->finishAnimating();
	return result;
}

[[nodiscard]] bool TonNameRowShown(const TonNameState &state) {
	return (state.status == TonNameStatus::Pending)
		|| (state.status == TonNameStatus::Resolved);
}

TonNameResultRow::TonNameResultRow(
	const QString &name,
	const QString &address)
: PeerListRow(PeerListRowId(1))
, _name(name)
, _address(address) {
	if (_address.isEmpty()) {
		setDisabledState(State::Disabled);
	}
}

QString TonNameResultRow::generateName() {
	return _name;
}

QString TonNameResultRow::generateShortName() {
	return _name;
}

PaintRoundImageCallback TonNameResultRow::generatePaintUserpicCallback(
		bool forceRound) {
	return [](Painter &p, int x, int y, int outerWidth, int size) {
		Ui::EmptyUserpic::PaintCurrency(p, x, y, outerWidth, size);
	};
}

void TonNameResultRow::paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) {
	const auto &font = st::contactsStatusFont;
	const auto text = _address.isEmpty()
		? font->elided(tr::lng_contacts_loading(tr::now), availableWidth)
		: font->elided(_address, availableWidth, Qt::ElideMiddle);
	p.setFont(font);
	p.setPen(selected ? st.statusFgOver : st.statusFg);
	p.drawTextLeft(x, y, outerWidth, text);
}

TonNameResultController::TonNameResultController(
	not_null<Main::Session*> session,
	Fn<void()> chosen)
: _session(session)
, _chosen(std::move(chosen)) {
}

void TonNameResultController::prepare() {
}

void TonNameResultController::rowClicked(not_null<PeerListRow*> row) {
	if (_chosen) {
		_chosen();
	}
}

Main::Session &TonNameResultController::session() const {
	return *_session;
}

void TonNameResultController::setContent(
		not_null<PeerListContent*> content) {
	_delegate.setContent(content);
	setDelegate(&_delegate);
}

void TonNameResultController::showState(const TonNameState &state) {
	while (delegate()->peerListFullRowsCount()) {
		delegate()->peerListRemoveRow(delegate()->peerListRowAt(0));
	}
	if (TonNameRowShown(state)) {
		const auto resolved = (state.status == TonNameStatus::Resolved);
		delegate()->peerListAppendRow(std::make_unique<TonNameResultRow>(
			state.name,
			resolved ? state.displayForm : QString()));
	}
	delegate()->peerListRefreshRows();
}

TonNameLookup::TonNameLookup(not_null<Main::Session*> session)
: _session(session)
, _timer([this] { start(); })
, _deadline([this] {
	if (_waiting) {
		finish(TonNameStatus::Failed);
	}
}) {
}

void TonNameLookup::setName(const QString &name) {
	++_revision;
	_waiting = false;
	_timer.cancel();
	_deadline.cancel();
	if (name.isEmpty()) {
		_state = TonNameState();
		return;
	}
	_timer.callOnce(AutoSearchTimeout);
	_state = TonNameState{ .name = name, .status = TonNameStatus::Pending };
}

void TonNameLookup::request() {
	_timer.cancel();
	start();
}

void TonNameLookup::close() {
	_closed = true;
	++_revision;
	_waiting = false;
	_timer.cancel();
	_deadline.cancel();
}

const TonNameState &TonNameLookup::current() const {
	return _state.current();
}

rpl::producer<TonNameState> TonNameLookup::value() const {
	return _state.value();
}

void TonNameLookup::start() {
	const auto name = _state.current().name;
	if (_closed || name.isEmpty()) {
		return;
	}
	if (!_waiting) {
		_waiting = true;
		_deadline.callOnce(kSendUserLoadTimeout);
		_state = TonNameState{
			.name = name,
			.status = TonNameStatus::Pending,
		};
	}
	issue();
}

void TonNameLookup::issue() {
	const auto session = _session.get();
	if (_closed || !_waiting || _inFlight || !session) {
		return;
	}
	_timer.cancel();
	_inFlight = true;
	const auto revision = _revision;
	session->wallet().resolveDnsName(
		_state.current().name,
		crl::guard(this, [=, this](std::optional<QString> address) {
			resolved(revision, std::move(address));
		}),
		crl::guard(this, [=, this](DnsLookupError error) {
			failed(revision, error);
		}));
}

bool TonNameLookup::settle(uint64 revision, bool busy) {
	_inFlight = false;
	if (_closed || !_waiting) {
		return false;
	} else if (busy) {
		_timer.callOnce(kNameBusyRetryDelay);
		return false;
	} else if (revision != _revision) {
		issue();
		return false;
	}
	return true;
}

void TonNameLookup::resolved(
		uint64 revision,
		std::optional<QString> address) {
	if (!settle(revision, false)) {
		return;
	} else if (!address) {
		finish(TonNameStatus::NotFound);
		return;
	}
	const auto flow = ParseRecipientFlow(*address);
	if (!flow) {
		finish(TonNameStatus::Failed);
		return;
	}
	_waiting = false;
	_timer.cancel();
	_deadline.cancel();
	_state = TonNameState{
		.name = _state.current().name,
		.address = *address,
		.displayForm = flow->displayForm,
		.status = TonNameStatus::Resolved,
	};
}

void TonNameLookup::failed(uint64 revision, DnsLookupError error) {
	if (settle(revision, (error == DnsLookupError::Busy))) {
		finish(TonNameStatus::Failed);
	}
}

void TonNameLookup::finish(TonNameStatus status) {
	_waiting = false;
	_timer.cancel();
	_deadline.cancel();
	_state = TonNameState{ .name = _state.current().name, .status = status };
}

[[nodiscard]] object_ptr<Ui::RpWidget> MakeTonNameResultList(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		rpl::producer<TonNameState> state,
		Fn<void()> chosen) {
	auto result = object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		box,
		object_ptr<Ui::VerticalLayout>(box));
	const auto wrap = result.data();
	const auto container = wrap->entity();
	const auto controller = container->lifetime().make_state<
		TonNameResultController>(session, std::move(chosen));

	Ui::AddSkip(container, st::walletSendRecentListTopSkip);
	controller->setStyleOverrides(&st::peerListSingleRow);
	const auto content = container->add(
		object_ptr<PeerListContent>(container, controller));
	controller->setContent(content);
	Ui::AddSkip(container, st::walletSendRecentListSkip);
	std::move(state) | rpl::on_next([=](const TonNameState &value) {
		controller->showState(value);
		wrap->toggle(TonNameRowShown(value), anim::type::instant);
	}, wrap->lifetime());
	wrap->finishAnimating();
	return result;
}

[[nodiscard]] rpl::producer<TextWithEntities> SendRecipientTitle(
		not_null<Ui::GenericBox*> box,
		const QString &recipient,
		Qt::TextElideMode mode) {
	return rpl::combine(
		box->widthValue(),
		rpl::single(rpl::empty) | rpl::then(Lang::Updated())
	) | rpl::map([=](int width, rpl::empty_value) {
		const auto available = width
			- 2 * st::boxTitlePosition.x()
			- st::boxTitleClose.width
			- st::boxTitleMenu.width;
		const auto title = [&](const QString &shown) {
			return tr::lng_wallet_send_user_title(
				tr::now,
				lt_user,
				Ui::Text::Colorized(shown),
				tr::marked);
		};
		auto measure = Ui::Text::String();
		const auto fits = [&](const TextWithEntities &text) {
			measure.setMarkedText(st::giveawayGiftCodeBox.title.style, text);
			return measure.maxWidth() <= available;
		};
		auto full = title(recipient);
		if (fits(full)) {
			return full;
		}
		const auto middle = (mode == Qt::ElideMiddle);
		const auto shortened = [&](int chars) {
			return middle
				? ShortAddressForm(recipient, chars)
				: (recipient.left(chars) + QChar(0x2026));
		};
		const auto most = middle
			? ((int(recipient.size()) - 1) / 2)
			: (int(recipient.size()) - 1);
		for (auto chars = most; chars > 1; --chars) {
			auto elided = title(shortened(chars));
			if (fits(elided)) {
				return elided;
			}
		}
		return title(shortened(1));
	});
}

} // namespace ContentDetails

} // namespace Wallet
