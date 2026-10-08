/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG
#include "dialogs/dialogs_widget.h"

#include "base/call_delayed.h"
#include "base/unixtime.h"
#include "data/data_authorization.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_top_bar_suggestion.h"
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "history/view/history_view_group_call_bar.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/userpic_button.h"
#include "ui/text/text_utilities.h"
#include "ui/effects/credits_graphics.h"
#include "ui/widgets/elastic_scroll.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_userpic_button.h"

namespace Dialogs {
namespace {

using Preview = ExtrasDebug::DialogsPreview;

void setBirthdayUserpics(
		not_null<TopBarSuggestionContent*> content,
		not_null<Main::Session*> session,
		bool multiple) {
	if (!multiple) {
		const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
			content, session->data().user(UserId(810000001)), st::uploadUserpicButton);
		userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
		content->setLeadingWidget(userpic);
		return;
	}
	struct Views {
		std::vector<HistoryView::UserpicInRow> users;
		QImage image;
	};
	const auto widget = Ui::CreateChild<Ui::RpWidget>(content);
	const auto state = widget->lifetime().make_state<Views>();
	for (const auto id : { 810000001, 810000002, 810000003 }) {
		state->users.push_back({ .peer = session->data().user(UserId(id)) });
	}
	const auto &st = st::dialogsTopBarSuggestionUserpics;
	widget->resize(st.size + 2 * (st.size - st.shift), st.size);
	widget->paintOn([=](QPainter &p) {
		if (HistoryView::NeedRegenerateUserpics(state->image, state->users)) {
			HistoryView::GenerateUserpicsInRow(
				state->image, state->users, st::dialogsTopBarSuggestionUserpics, 3);
		}
		p.drawImage(0, 0, state->image);
	});
	content->setLeadingWidget(widget);
}

void fillSuggestion(
		not_null<TopBarSuggestionContent*> content,
		not_null<Window::SessionController*> controller,
		Preview preview,
		Fn<void()> action,
		Fn<void()> hide) {
	const auto session = &controller->session();
	using RightIcon = TopBarSuggestionContent::RightIcon;
	content->setRightIcon(RightIcon::Close);
	content->setHideCallback(std::move(hide));
	content->setClickedCallback(action);
	switch (preview) {
	case Preview::BirthdaySetup:
		content->setContent(
			tr::lng_dialogs_suggestions_birthday_title(tr::now, tr::bold),
			tr::lng_dialogs_suggestions_birthday_about(tr::now, tr::marked));
		break;
	case Preview::BirthdayContact:
	case Preview::BirthdayContacts: {
		const auto multiple = (preview == Preview::BirthdayContacts);
		content->setContent(
			multiple
				? tr::lng_dialogs_suggestions_birthday_contacts_title(
					tr::now, lt_count, 3., tr::rich)
				: tr::lng_dialogs_suggestions_birthday_contact_title(
					tr::now, lt_text, { session->data().user(UserId(810000001))->shortName() }, tr::rich),
			multiple
				? tr::lng_dialogs_suggestions_birthday_contacts_about(tr::now, tr::marked)
				: tr::lng_dialogs_suggestions_birthday_contact_about(tr::now, tr::marked));
		setBirthdayUserpics(content, session, multiple);
	} break;
	case Preview::Userpic: {
		const auto upload = Ui::CreateChild<Ui::UserpicButton>(
			content, &controller->window(), Ui::UserpicButton::Role::ChoosePhoto,
			st::uploadUserpicButton);
		upload->setAttribute(Qt::WA_TransparentForMouseEvents);
		content->setLeadingWidget(upload);
		content->setContent(
			tr::lng_dialogs_suggestions_userpics_title(tr::now, tr::bold),
			tr::lng_dialogs_suggestions_userpics_about(tr::now, tr::marked));
	} break;
	case Preview::PremiumAnnual:
	case Preview::PremiumUpgrade:
	case Preview::PremiumRestore: {
		const auto title = (preview == Preview::PremiumAnnual)
			? tr::lng_dialogs_suggestions_premium_annual_title
			: (preview == Preview::PremiumRestore)
			? tr::lng_dialogs_suggestions_premium_restore_title
			: tr::lng_dialogs_suggestions_premium_upgrade_title;
		const auto about = (preview == Preview::PremiumAnnual)
			? tr::lng_dialogs_suggestions_premium_annual_about
			: (preview == Preview::PremiumRestore)
			? tr::lng_dialogs_suggestions_premium_restore_about
			: tr::lng_dialogs_suggestions_premium_upgrade_about;
		content->setRightIcon(RightIcon::Arrow);
		content->setContent(
			title(tr::now, lt_text, { u"30%"_q }, tr::bold),
			about(tr::now, tr::marked));
	} break;
	case Preview::PremiumGrace:
		content->setContent(
			tr::lng_dialogs_suggestions_premium_grace_title(tr::now, tr::bold),
			tr::lng_dialogs_suggestions_premium_grace_about(tr::now, tr::marked));
		break;
	case Preview::Credits:
		content->setContent(
			tr::lng_dialogs_suggestions_credits_sub_low_title(
				tr::now, lt_count, 150., lt_emoji, Ui::MakeCreditsIconEntity(),
				lt_channels, { u"本地测试频道"_q }, tr::bold),
			tr::lng_dialogs_suggestions_credits_sub_low_about(tr::now, tr::marked),
			Ui::MakeCreditsIconContext(content->contentTitleSt().font->height, 1));
		break;
	case Preview::Custom:
		content->setContent(
			tr::extras_DebugListPromoTitle(tr::now, tr::bold),
			tr::extras_DebugListPromoAbout(tr::now, tr::marked));
		break;
	case Preview::Auction:
	case Preview::AuctionOutbid: {
		const auto outbid = (preview == Preview::AuctionOutbid);
		content->setRightIcon(RightIcon::None);
		content->setContent(
			tr::lng_auction_bar_active(tr::now, tr::bold),
			outbid
				? tr::lng_auction_bar_outbid(tr::now, tr::marked)
				: tr::lng_auction_bar_winning(tr::now, lt_count, 3., tr::marked),
			std::nullopt,
			outbid ? st::attentionButtonFg->c : std::optional<QColor>());
		content->setRightButton(tr::lng_auction_bar_view(tr::marked), action);
	} break;
	default:
		Unexpected("Invalid suggestion preview.");
	}
}

} // namespace

void Widget::clearListPreviewSuggestion() {
	_topBarSuggestionPlaceholder = nullptr;
	_topBarSuggestion = nullptr;
	_scroll->setBarTopInset(0);
	_topBarSuggestionHeightChanged.fire(0);
}

void Widget::installListPreviewSuggestion(Preview preview) {
	clearListPreviewSuggestion();
	const auto action = [=] {
		controller()->showToast(tr::extras_DebugListPreviewAction(tr::now));
	};
	const auto hide = [=] {
		if (!_topBarSuggestion) {
			return;
		}
		_topBarSuggestion->toggle(false, anim::type::normal);
		base::call_delayed(st::slideWrapDuration * 2, _topBarSuggestion.get(), [=] {
			clearListPreviewSuggestion();
		});
	};
	if (preview == Preview::Auth || preview == Preview::AuthMultiple) {
		auto auths = std::vector<Data::UnreviewedAuth>{
			{ 1, true, base::unixtime::now(), u"Windows Desktop"_q, u"Taipei"_q },
		};
		if (preview == Preview::AuthMultiple) {
			auths.push_back({ 2, true, base::unixtime::now(), u"Android"_q, u"Tokyo"_q });
		}
		const auto wrap = CreateUnconfirmedAuthContent(
			this, rpl::single(std::move(auths)),
			[=](bool) { action(); hide(); }, _childListShown.value());
		_prepareTopBarSnapshot.events() | rpl::on_next([=] {
			wrap->prepareCollapseSnapshot();
		}, wrap->lifetime());
		_topBarSuggestion.reset(wrap.get());
	} else {
		const auto content = Ui::CreateChild<TopBarSuggestionContent>(this);
		fillSuggestion(content, controller(), preview, action, hide);
		content->setNarrowExpandCallback(ExpandChatsListCallback(this));
		content->setCollapseProgress(_childListShown.value());
		_prepareTopBarSnapshot.events() | rpl::on_next([=] {
			content->prepareCollapseSnapshot();
		}, content->lifetime());
		_topBarSuggestion.reset(Ui::CreateChild<Ui::SlideWrap<Ui::RpWidget>>(
			this, object_ptr<Ui::RpWidget>::fromRaw(content)));
	}
	_topBarSuggestion->setObjectName(u"dialogs-preview.suggestion"_q);
	_topBarSuggestion->toggle(false, anim::type::instant);
	MountTopBarSuggestion({
		.scroll = _scroll,
		.innerList = _innerList,
		.wrap = _topBarSuggestion.get(),
		.placeholder = &_topBarSuggestionPlaceholder,
		.heightChanged = [=](int height) {
			_topBarSuggestionHeightChanged.fire_copy(height);
		},
	});
	_topBarSuggestion->toggle(true, anim::type::normal);
}

} // namespace Dialogs
#endif // _DEBUG
