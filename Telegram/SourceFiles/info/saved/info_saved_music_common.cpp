/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/saved/info_saved_music_common.h"

#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_saved_music.h"
#include "extras/extras_settings.h"
#include "history/history_item.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/profile/info_profile_music_button.h"
#include "info/saved/info_saved_music_widget.h"
#include "lang/lang_keys.h"
#include "ui/text/format_song_document_name.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_menu_icons.h"

namespace Info::Saved {

rpl::producer<bool> SetupSavedMusic(
		not_null<Ui::VerticalLayout*> container,
		not_null<Info::Controller*> controller,
		not_null<PeerData*> peer,
		Fn<void(std::optional<QColor>)> coverColorChanged) {
	struct State {
		rpl::variable<bool> shown = false;
		Profile::MusicButton *button = nullptr;
		FullMsgId id;
		base::unique_qptr<Ui::PopupMenu> menu;
	};
	const auto state = container->lifetime().make_state<State>();
	if (!Data::SavedMusic::Supported(peer->id)) {
		return state->shown.value();
	}
	Data::SavedMusicList(peer, nullptr, 1
	) | rpl::on_next([=](const Data::SavedMusicSlice &slice) {
		const auto item = slice.size() ? slice[0].get() : nullptr;
		const auto document = (item && item->media())
			? item->media()->document() : nullptr;
		state->shown = (document != nullptr);
		if (!document) {
			container->clear();
			state->button = nullptr;
			state->id = {};
			coverColorChanged(std::nullopt);
			return;
		}
		if (state->button && state->id == item->fullId()) {
			return;
		}
		const auto name = Ui::Text::FormatSongNameFor(document);
		auto data = Profile::MusicButtonData{
			.name = name,
			.title = name.composedName().title,
			.performer = name.composedName().performer,
			.msgId = item->fullId(),
			.mediaView = document->createMediaView(),
		};
		state->id = data.msgId;
		if (state->button) {
			state->button->updateData(std::move(data));
			return;
		}
		const auto button = state->button = container->add(
			object_ptr<Profile::MusicButton>(container, std::move(data), [=] {
				controller->showSection(MakeMusic(peer));
			}));
		button->coverColorValue(
		) | rpl::on_next(coverColorChanged, button->lifetime());
		button->setAcceptBoth(true);
		button->clicks() | rpl::filter([](Qt::MouseButton mouse) {
			return mouse == Qt::RightButton;
		}) | rpl::on_next([=] {
			state->menu = base::make_unique_q<Ui::PopupMenu>(
				button, st::popupMenuWithIcons);
			const auto enabled = ExtrasSettings::getInstance().adaptiveCoverColor();
			state->menu->addAction(enabled
				? tr::extras_DisableColorfulCover(tr::now)
				: tr::extras_EnableColorfulCover(tr::now), [=] {
					auto &settings = ExtrasSettings::getInstance();
					settings.setAdaptiveCoverColor(!settings.adaptiveCoverColor());
				}, &st::menuIconPalette);
			state->menu->popup(QCursor::pos());
		}, button->lifetime());
	}, container->lifetime());
	return state->shown.value();
}

} // namespace Info::Saved
