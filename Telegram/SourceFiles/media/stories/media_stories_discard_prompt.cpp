/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_discard_prompt.h"

#include "lang/lang_keys.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace Media::Stories {

void ShowDiscardStoryPrompt(
		not_null<Ui::RpWidget*> parent,
		DiscardStoryArgs args) {
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		parent,
		st::popupMenuWithIcons);
	menu->setObjectName(u"stories.discardMenu"_q);
	menu->setAccessibleName(tr::lng_story_discard_title(tr::now));
	menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopLeft);
	menu->addAction(
		tr::lng_story_discard_title(tr::now),
		Fn<void()>())->setEnabled(false);
	menu->addSeparator();
	// 延后关闭编辑器，避免在菜单动作栈中销毁宿主；宿主销毁后回调失效。
	const auto deferred = [=](Fn<void()> callback) {
		return [guarded = crl::guard(parent, std::move(callback))] {
			crl::on_main(guarded);
		};
	};
	const auto add = Ui::Menu::CreateAddActionCallback(menu);
	if (args.allowDraft) {
		add({
			.text = args.draft
				? tr::lng_story_keep_draft(tr::now)
				: tr::lng_story_save_draft(tr::now),
			.handler = deferred(std::move(args.saveDraft)),
			.icon = &st::menuIconStoriesSave,
		});
	}
	add({
		.text = tr::lng_cancel(tr::now),
		.handler = [] {},
		.icon = &st::menuIconCancel,
	});
	add({
		.text = args.draft
			? tr::lng_story_delete_draft(tr::now)
			: tr::lng_story_discard_button(tr::now),
		.handler = deferred(std::move(args.discard)),
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
	const auto center = parent->mapToGlobal(parent->rect().center());
	menu->popup(center - QPoint(menu->width() / 2, menu->height() / 2));
}

} // namespace Media::Stories
