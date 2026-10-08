#pragma once

#ifdef _DEBUG

#include <QString>
#include <vector>

namespace Window {
class SessionController;
} // namespace Window

namespace Dialogs {
class Widget;
} // namespace Dialogs

namespace ExtrasDebug {

enum class DialogsPreview {
	None,
	BirthdaySetup,
	BirthdayContact,
	BirthdayContacts,
	Userpic,
	PremiumAnnual,
	PremiumUpgrade,
	PremiumRestore,
	PremiumGrace,
	Credits,
	Custom,
	Auction,
	AuctionOutbid,
	Auth,
	AuthMultiple,
	CommunityRequests,
	SearchId,
	SearchGlobal,
	SearchLoading,
	SearchPosts,
	SearchMessages,
	Download,
	DownloadDone,
	MoreChats,
	LoadMore,
	Loading,
	UpdateReady,
	Frozen,
	CommunityAdd,
	ForumRequests,
	ForumReport,
	ForumUnarchive,
	ForumAll,
	Stack,
};

struct DialogsPreviewEntry {
	DialogsPreview id;
	const char *key;
	QString title;
};

[[nodiscard]] std::vector<DialogsPreviewEntry> dialogsPreviewEntries();
[[nodiscard]] Dialogs::Widget *dialogsPreviewWidget(
	not_null<Window::SessionController*> controller);
} // namespace ExtrasDebug

#endif // _DEBUG
