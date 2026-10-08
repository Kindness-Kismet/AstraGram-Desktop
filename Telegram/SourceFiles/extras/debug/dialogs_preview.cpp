#ifdef _DEBUG
#include "extras/debug/dialogs_preview.h"

#include "dialogs/dialogs_widget.h"
#include "lang/lang_keys.h"
#include "window/window_session_controller.h"

#include <QApplication>

namespace ExtrasDebug {

std::vector<DialogsPreviewEntry> dialogsPreviewEntries() {
	return {
		{ DialogsPreview::None, "none", tr::extras_DebugListNone(tr::now) },
		{ DialogsPreview::BirthdaySetup, "birthday-setup", tr::extras_DebugListBirthdaySetup(tr::now) },
		{ DialogsPreview::BirthdayContact, "birthday-contact", tr::extras_DebugListBirthdayContact(tr::now) },
		{ DialogsPreview::BirthdayContacts, "birthday-contacts", tr::extras_DebugListBirthdayContacts(tr::now) },
		{ DialogsPreview::Userpic, "userpic", tr::extras_DebugListUserpic(tr::now) },
		{ DialogsPreview::PremiumAnnual, "premium-annual", tr::extras_DebugListPremiumAnnual(tr::now) },
		{ DialogsPreview::PremiumUpgrade, "premium-upgrade", tr::extras_DebugListPremiumUpgrade(tr::now) },
		{ DialogsPreview::PremiumRestore, "premium-restore", tr::extras_DebugListPremiumRestore(tr::now) },
		{ DialogsPreview::PremiumGrace, "premium-grace", tr::extras_DebugListPremiumGrace(tr::now) },
		{ DialogsPreview::Credits, "credits", tr::extras_DebugListCredits(tr::now) },
		{ DialogsPreview::Custom, "custom", tr::extras_DebugListCustom(tr::now) },
		{ DialogsPreview::Auction, "auction", tr::extras_DebugListAuction(tr::now) },
		{ DialogsPreview::AuctionOutbid, "auction-outbid", tr::extras_DebugListAuctionOutbid(tr::now) },
		{ DialogsPreview::Auth, "auth", tr::extras_DebugListAuth(tr::now) },
		{ DialogsPreview::AuthMultiple, "auth-multiple", tr::extras_DebugListAuthMultiple(tr::now) },
		{ DialogsPreview::CommunityRequests, "community-requests", tr::extras_DebugListCommunityRequests(tr::now) },
		{ DialogsPreview::SearchId, "search-id", tr::extras_DebugListSearchId(tr::now) },
		{ DialogsPreview::SearchGlobal, "search-global", tr::extras_DebugListSearchGlobal(tr::now) },
		{ DialogsPreview::SearchLoading, "search-loading", tr::extras_DebugListSearchLoading(tr::now) },
		{ DialogsPreview::SearchPosts, "search-posts", tr::extras_DebugListSearchPosts(tr::now) },
		{ DialogsPreview::SearchMessages, "search-messages", tr::extras_DebugListSearchMessages(tr::now) },
		{ DialogsPreview::Download, "download", tr::extras_DebugListDownload(tr::now) },
		{ DialogsPreview::DownloadDone, "download-done", tr::extras_DebugListDownloadDone(tr::now) },
		{ DialogsPreview::MoreChats, "more-chats", tr::extras_DebugListMoreChats(tr::now) },
		{ DialogsPreview::LoadMore, "load-more", tr::extras_DebugListLoadMore(tr::now) },
		{ DialogsPreview::Loading, "loading", tr::extras_DebugListLoading(tr::now) },
		{ DialogsPreview::UpdateReady, "update", tr::extras_SimulationUpdateReady(tr::now) },
		{ DialogsPreview::Frozen, "frozen", tr::extras_DebugListFrozen(tr::now) },
		{ DialogsPreview::CommunityAdd, "community-add", tr::extras_DebugListCommunityAdd(tr::now) },
		{ DialogsPreview::ForumRequests, "forum-requests", tr::extras_DebugListForumRequests(tr::now) },
		{ DialogsPreview::ForumReport, "forum-report", tr::extras_DebugListForumReport(tr::now) },
		{ DialogsPreview::ForumUnarchive, "forum-unarchive", tr::extras_DebugListForumUnarchive(tr::now) },
		{ DialogsPreview::ForumAll, "forum-all", tr::extras_DebugListForumAll(tr::now) },
		{ DialogsPreview::Stack, "stack", tr::extras_DebugListStack(tr::now) },
	};
}

Dialogs::Widget *dialogsPreviewWidget(
		not_null<Window::SessionController*> controller) {
	for (const auto window : QApplication::topLevelWidgets()) {
		for (const auto child : window->findChildren<QWidget*>()) {
			const auto dialogs = dynamic_cast<Dialogs::Widget*>(child);
			if (dialogs && dialogs->isMainListPreview()
				&& &dialogs->session() == &controller->session()) {
				return dialogs;
			}
		}
	}
	return nullptr;
}

} // namespace ExtrasDebug
#endif // _DEBUG
