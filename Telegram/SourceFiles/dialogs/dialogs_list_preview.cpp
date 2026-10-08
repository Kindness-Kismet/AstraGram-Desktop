#ifdef _DEBUG
#include "dialogs/dialogs_widget.h"

#include "api/api_peer_search.h"
#include "base/unixtime.h"
#include "chat_helpers/message_field.h"
#include "data/data_channel.h"
#include "data/data_forum.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_inner_widget.h"
#include "dialogs/ui/chat_search_in.h"
#include "extras/debug/commands/commands_internal.h"
#include "extras/debug/debug_login.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_contact_status.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/chat/more_chats_bar.h"
#include "ui/controls/download_bar.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/elastic_scroll.h"
#include "window/window_session_controller.h"

namespace Dialogs {
namespace {

using Preview = ExtrasDebug::DialogsPreview;

void prepareForum(not_null<ChannelData*> channel, Preview preview) {
	const auto combined = (preview == Preview::ForumAll);
	const auto requests = combined || preview == Preview::ForumRequests;
	const auto report = combined || preview == Preview::ForumReport;
	channel->setFlags(ChannelDataFlag::Megagroup | ChannelDataFlag::Forum);
	channel->setAdminRights(requests
		? ChatAdminRight::InviteByLinkOrAdd | ChatAdminRight::ProcessJoinRequests
		: ChatAdminRights());
	channel->setPendingRequestsCount(requests ? 1 : 0,
		requests ? std::vector<UserId>{ UserId(810000002) } : std::vector<UserId>());
	channel->setBarSettings(preview == Preview::ForumUnarchive
		? PeerBarSetting::AutoArchived | PeerBarSetting::ReportSpam
		: report ? PeerBarSetting::ReportSpam : PeerBarSettings());
	channel->clearGroupCall();
}

} // namespace

bool Widget::isMainListPreview() const {
	return _layout == Layout::Main;
}

bool Widget::handleListPreviewAction() {
	if (_listPreview == Preview::None) {
		return false;
	}
	controller()->showToast(tr::extras_DebugListPreviewAction(tr::now));
	return true;
}

bool Widget::listPreviewIs(Preview preview) const {
	if (_listPreview == preview) {
		return true;
	}
	return _listPreview == Preview::Stack
		&& (preview == Preview::BirthdayContact
			|| preview == Preview::Frozen
			|| preview == Preview::MoreChats
			|| preview == Preview::Download);
}

void Widget::setListPreview(Preview preview) {
	if (!isMainListPreview() || !ExtrasDebug::isSimulationSession(&session())) {
		return;
	}
	const auto old = _listPreview;
	_listPreview = Preview::None;
	controller()->closeForum();
	controller()->closeFolder();
	controller()->closeCommunity();
	cancelSearch({ .forceFullCancel = true });
	clearListPreviewSuggestion();
	_downloadBar = nullptr;
	_moreChatsBar = nullptr;
	refreshLoadMoreButton(false, false);
	const auto forumPeer = ExtrasDebug::Commands::simulationPeer(&session(), u"topic"_q);
	if (old >= Preview::ForumRequests && old <= Preview::ForumAll) {
		prepareForum(forumPeer->asChannel(), Preview::None);
	}

	_listPreview = preview;
	checkUpdateStatus();
	const auto action = [=] {
		controller()->showToast(tr::extras_DebugListPreviewAction(tr::now));
	};
	updateFrozenAccountBar();
	updateCommunityRequestsBubble();
	updateCommunityAddChatButton();
	if (_frozenAccountBar) {
		_frozenAccountBar->setClickedCallback(action);
	}
	if (preview >= Preview::BirthdaySetup && preview <= Preview::AuthMultiple) {
		installListPreviewSuggestion(preview);
	} else if (preview == Preview::Stack) {
		installListPreviewSuggestion(Preview::BirthdayContact);
	}
	if (listPreviewIs(Preview::MoreChats)) {
		_moreChatsBar = std::make_unique<Ui::MoreChatsBar>(
			this, rpl::single(Ui::MoreChatsBarContent{ .count = 3 }));
		_moreChatsBar->barClicks() | rpl::on_next(action, _moreChatsBar->lifetime());
		_moreChatsBar->closeClicks() | rpl::on_next([=] {
			_moreChatsBar->hide();
		}, _moreChatsBar->lifetime());
		_moreChatsBar->heightValue() | rpl::on_next([=] {
			updateControlsGeometry();
		}, _moreChatsBar->lifetime());
		_moreChatsBar->show();
		_moreChatsBar->finishAnimating();
	}
	if (listPreviewIs(Preview::Download) || preview == Preview::DownloadDone) {
		const auto done = (preview == Preview::DownloadDone);
		_downloadBar = std::make_unique<Ui::DownloadBar>(
			this, rpl::single(Ui::DownloadBarProgress{
				.ready = done ? 10000000 : 4500000, .total = 10000000,
			}));
		_downloadBar->show({
			.singleName = { u"本地预览.zip"_q }, .count = 1, .done = done ? 1 : 0,
		});
		_downloadBar->clicks() | rpl::on_next(action, _downloadBar->lifetime());
		_downloadBar->heightValue() | rpl::on_next([=] {
			updateControlsGeometry();
		}, _downloadBar->lifetime());
	}
	if (preview == Preview::LoadMore || preview == Preview::Loading) {
		refreshLoadMoreButton(true, preview == Preview::LoadMore);
	}
	if (preview >= Preview::SearchId && preview <= Preview::SearchMessages) {
		const auto posts = (preview == Preview::SearchPosts);
		applySearchState({
			.tab = ChatSearchTab::MyMessages,
			.query = posts ? u"#preview"_q : u"preview"_q,
		});
		_inner->searchRequested(true);
		const auto peer = ExtrasDebug::Commands::simulationPeer(&session(), u"private"_q);
		if (preview == Preview::SearchId) {
			_inner->searchReceived({}, nullptr, { .start = true }, 0);
			_inner->idSearchReceived({ peer });
		} else if (preview == Preview::SearchGlobal) {
			const auto user = session().data().user(UserId(819999001));
			user->setName(u"全局搜索样本"_q, {}, {}, {});
			user->setLoadedStatus(PeerData::LoadedStatus::Full);
			_inner->searchReceived({}, nullptr, { .start = true }, 0);
			_inner->peerSearchReceived({ .query = u"preview"_q, .peers = { user } });
		} else if (preview != Preview::SearchLoading) {
			const auto item = session().data().history(peer)->lastMessage();
			Expects(item != nullptr);
			_inner->searchReceived({ item }, nullptr, { .posts = posts, .start = true }, 1);
		}
	}
	if (preview >= Preview::ForumRequests && preview <= Preview::ForumAll) {
		prepareForum(forumPeer->asChannel(), preview);
		auto params = Window::SectionShow();
		params.animated = anim::type::instant;
		// 在原话题列表中预览，回到其他场景时完整退出话题导航。
		controller()->showForum(forumPeer->forum(), params);
	}
	updateControlsGeometry();
	_scroll->scrollToY(0);
}

} // namespace Dialogs
#endif // _DEBUG
