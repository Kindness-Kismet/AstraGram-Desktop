/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/field_autocomplete.h"
#include "base/qt/qt_key_modifiers.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/tabbed_selector.h"
#include "data/business/data_shortcut_messages.h"
#include "data/components/recent_inline_bots.h"
#include "data/components/top_peers.h"
#include "data/stickers/data_stickers.h"
#include "data/data_changes.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "settings.h"
#include "storage/storage_account.h"
#include "ui/emoji_config.h"
#include "ui/widgets/fields/input_field.h"

namespace ChatHelpers {

void InitFieldAutocomplete(
		std::unique_ptr<FieldAutocomplete> &autocomplete,
		FieldAutocompleteDescriptor &&descriptor) {
	Expects(!autocomplete);

	autocomplete = std::make_unique<FieldAutocomplete>(
		descriptor.parent,
		descriptor.show,
		descriptor.stOverride);
	const auto raw = autocomplete.get();
	const auto field = descriptor.field;

	field->rawTextEdit()->installEventFilter(raw);

	raw->mentionChosen(
	) | rpl::on_next([=](FieldAutocomplete::MentionChosen data) {
		const auto user = data.user;
		const auto ctrlClick = base::IsCtrlPressed()
			&& data.method == FieldAutocomplete::ChooseMethod::ByClick;
		if (data.mention.isEmpty() || ctrlClick) {
			field->insertTag(
				user->firstName.isEmpty() ? user->name() : user->firstName,
				PrepareMentionTag(user));
		} else {
			field->insertTag('@' + data.mention);
		}
	}, raw->lifetime());

	const auto sendCommand = descriptor.sendBotCommand;
	const auto setText = descriptor.setText;

	raw->hashtagChosen(
	) | rpl::on_next([=](FieldAutocomplete::HashtagChosen data) {
		field->insertTag(data.hashtag);
	}, raw->lifetime());

	const auto peer = descriptor.peer;
	const auto features = descriptor.features;
	const auto processShortcut = descriptor.processShortcut;
	const auto shortcutMessages = (processShortcut != nullptr)
		? &peer->owner().shortcutMessages()
		: nullptr;
	raw->botCommandChosen(
	) | rpl::on_next([=](FieldAutocomplete::BotCommandChosen data) {
		if (!features().autocompleteCommands) {
			return;
		}
		using Method = FieldAutocompleteChooseMethod;
		const auto byTab = (data.method == Method::ByTab);
		const auto shortcut = data.user->isSelf();

		// 按 Tab 只补全文本，其他选择沿用原来的发送流程。
		if (byTab && data.command.size() > 1) {
			field->insertTag(data.command);
		} else if (!shortcut) {
			sendCommand(data.command);
			setText(
				field->getTextWithTagsPart(field->textCursor().position()));
		} else if (processShortcut) {
			processShortcut(data.command.mid(1));
		}
	}, raw->lifetime());

	raw->setModerateKeyActivateCallback(
		std::move(descriptor.moderateKeyActivateCallback));

	if (const auto stickerChoosing = descriptor.stickerChoosing) {
		raw->choosingProcesses(
		) | rpl::on_next([=](FieldAutocomplete::Type type) {
			if (type == FieldAutocomplete::Type::Stickers) {
				stickerChoosing();
			}
		}, raw->lifetime());
	}
	if (const auto chosen = descriptor.stickerChosen) {
		raw->stickerChosen(
		) | rpl::on_next(chosen, raw->lifetime());
	}

	field->tabbed(
	) | rpl::on_next([=](not_null<Ui::InputField::TabbedRequest*> request) {
		if (!raw->isHidden()) {
			raw->chooseSelected(FieldAutocomplete::ChooseMethod::ByTab);
			request->handled = true;
		}
	}, raw->lifetime());

	const auto check = [=] {
		auto parsed = ParseMentionHashtagBotCommandQuery(field, features());
		if (parsed.query.isEmpty()) {
		} else if (parsed.query[0] == '#'
			&& cRecentWriteHashtags().isEmpty()
			&& cRecentSearchHashtags().isEmpty()) {
			peer->session().local().readRecentHashtagsAndBots();
		} else if (parsed.query[0] == '@'
			&& peer->session().recentInlineBots().list().empty()) {
			peer->session().local().readRecentHashtagsAndBots();
		} else if (parsed.query[0] == '/'
			&& peer->isUser()
			&& !peer->asUser()->isBot()
			&& (!shortcutMessages
				|| shortcutMessages->shortcuts().list.empty()
				|| peer->starsPerMessageChecked() != 0)) {
			parsed = {};
		}
		if (!parsed.query.isEmpty() && parsed.query[0] == '@') {
			peer->session().topGuestChatBots().reload();
		}
		raw->showFiltered(peer, parsed.query, parsed.fromStart);
	};

	const auto updateStickersByEmoji = [=] {
		const auto errorForStickers = Data::RestrictionError(
			peer,
			ChatRestriction::SendStickers);
		if (features().suggestStickersByEmoji && !errorForStickers) {
			const auto &text = field->getTextWithTags().text;
			auto length = 0;
			if (const auto emoji = Ui::Emoji::Find(text, &length)) {
				if (text.size() <= length) {
					raw->showStickers(emoji);
					return;
				}
			}
		}
		raw->showStickers(nullptr);
	};

	raw->refreshRequests(
	) | rpl::on_next(check, raw->lifetime());

	raw->stickersUpdateRequests(
	) | rpl::on_next(updateStickersByEmoji, raw->lifetime());

	peer->owner().botCommandsChanges(
	) | rpl::filter([=](not_null<PeerData*> changed) {
		return (peer == changed);
	}) | rpl::on_next([=] {
		if (raw->clearFilteredBotCommands()) {
			check();
		}
	}, raw->lifetime());

	peer->owner().stickers().updated(
		Data::StickersType::Stickers
	) | rpl::on_next(updateStickersByEmoji, raw->lifetime());

	QObject::connect(
		field->rawTextEdit(),
		&QTextEdit::cursorPositionChanged,
		raw,
		check,
		Qt::QueuedConnection);

	field->changes(
	) | rpl::on_next(updateStickersByEmoji, raw->lifetime());

	peer->session().changes().peerUpdates(
		Data::PeerUpdate::Flag::Rights
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		return (update.peer == peer);
	}) | rpl::on_next(updateStickersByEmoji, raw->lifetime());

	if (shortcutMessages) {
		shortcutMessages->shortcutsChanged(
		) | rpl::on_next(check, raw->lifetime());
	}

	raw->setSendMenuDetails(std::move(descriptor.sendMenuDetails));
	raw->hideFast();
}

} // namespace ChatHelpers
