#include "extras/ui/context_menu/forward_to_saved_menu.h"

#include "extras/features/forward/forward_to_saved.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"
#include "ui/toast/toast.h"
#include "ui/widgets/popup_menu.h"

namespace ExtrasUi {

void addForwardToSavedMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<Main::Session*> session,
		MessageIdsList ids) {
	const auto options = ExtrasForward::savedForwardOptions(session, ids);
	if (options.empty()) {
		return;
	}
	using Options = Data::ForwardOptions;
	auto submenu = std::make_unique<Ui::PopupMenu>(menu, st::popupMenuWithIcons);
	submenu->setObjectName(u"forward.saved.menu"_q);
	for (const auto option : options) {
		const auto title = (option == Options::PreserveInfo)
			? tr::extras_ForwardSavedOriginal(tr::now)
			: (option == Options::NoSenderNames)
			? tr::extras_ForwardSavedNoSource(tr::now)
			: tr::extras_ForwardSavedNoSourceCaption(tr::now);
		const auto icon = (option == Options::PreserveInfo)
			? &st::menuIconForward
			: (option == Options::NoSenderNames)
			? &st::menuIconUserHide
			: &st::menuIconCaptionHide;
		const auto action = submenu->addAction(title, crl::guard(session, [=] {
			using Error = ExtrasForward::SavedForwardError;
			const auto error = ExtrasForward::forwardToSaved(session, ids, option);
			if (error != Error::None) {
				Ui::Toast::Show((error == Error::Unavailable)
					? tr::extras_ForwardSavedUnavailable(tr::now)
					: tr::extras_ForwardSavedUnsupported(tr::now));
			}
		}), icon);
		action->setObjectName((option == Options::PreserveInfo)
			? u"forward.saved.original"_q
			: (option == Options::NoSenderNames)
			? u"forward.saved.noSource"_q
			: u"forward.saved.noSourceCaption"_q);
	}
	menu->addAction(tr::extras_ForwardToSavedMessage(tr::now),
		std::move(submenu), &st::menuIconFave);
}

} // namespace ExtrasUi
