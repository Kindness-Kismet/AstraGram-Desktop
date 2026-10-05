#include "history/history_item.h"

#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "iv/iv_rich_page.h"
#include "main/main_session.h"

const HistoryMessageTranslation *HistoryItem::translation() const {
	return Get<HistoryMessageTranslation>();
}

bool HistoryItem::translationDisplayed() const {
	const auto state = translation();
	return state && state->used
		&& state->to == state->manualTo.value_or(history()->translatedTo());
}

bool HistoryItem::translationStartManual(LanguageId to, uint64 token) {
	Expects(to && token);

	AddComponents(HistoryMessageTranslation::Bit());
	const auto state = Get<HistoryMessageTranslation>();
	state->manualTo = to;
	if (state->to == to && !state->text.empty()) {
		state->manualRequestToken = 0;
		translationToggle(state, true);
		return false;
	}
	translationToggle(state, false);
	state->to = to;
	state->text = {};
	state->richPage = nullptr;
	state->requested = true;
	state->failed = false;
	state->manualRequestToken = token;
	return true;
}

bool HistoryItem::translationFinishManual(
		uint64 token,
		TextWithEntities text,
		std::shared_ptr<const Iv::RichPage> page) {
	const auto state = Get<HistoryMessageTranslation>();
	if (!state || state->manualRequestToken != token
		|| state->manualTo != state->to) {
		return false;
	}
	state->manualRequestToken = 0;
	state->requested = false;
	state->failed = text.empty();
	if (state->failed) {
		return true;
	}
	state->text = std::move(text);
	state->richPage = std::move(page);
	translationToggle(state, true);
	return true;
}

void HistoryItem::translationShowOriginal() {
	AddComponents(HistoryMessageTranslation::Bit());
	const auto state = Get<HistoryMessageTranslation>();
	state->manualTo = LanguageId();
	state->manualRequestToken = 0;
	state->requested = false;
	translationToggle(state, false);
}

bool HistoryItem::translationShowRequiresCheck(LanguageId to) const {
	if (const auto state = translation(); state && state->manualTo.has_value()) {
		return false;
	}
	// 与实际切换方法保持一致，避免重复刷新。
	if (!to) {
		if (const auto translation = Get<HistoryMessageTranslation>()) {
			return (!translation->failed && translation->text.empty())
				|| translation->used;
		}
		return false;
	} else if (const auto translation = Get<HistoryMessageTranslation>()) {
		if (translation->to == to) {
			return !translation->used && !translation->text.empty();
		}
		return true;
	} else {
		return true;
	}
}

bool HistoryItem::translationShowRequiresRequest(LanguageId to) {
	if (const auto state = translation(); state && state->manualTo.has_value()) {
		return false;
	}
	// 手动选择由单条请求管理，聊天批次只修改跟随聊天的消息。
	if (!to) {
		if (const auto translation = Get<HistoryMessageTranslation>()) {
			if (!translation->failed && translation->text.empty()) {
				Assert(!translation->used);
				RemoveComponents(HistoryMessageTranslation::Bit());
			} else {
				translationToggle(translation, false);
			}
		}
		return false;
	} else if (const auto translation = Get<HistoryMessageTranslation>()) {
		if (translation->to == to) {
			translationToggle(translation, true);
			return false;
		}
		translationToggle(translation, false);
		translation->to = to;
		translation->requested = true;
		translation->failed = false;
		translation->text = {};
		translation->richPage = nullptr;
		return true;
	} else {
		AddComponents(HistoryMessageTranslation::Bit());
		const auto added = Get<HistoryMessageTranslation>();
		added->to = to;
		added->requested = true;
		return true;
	}
}

void HistoryItem::translationToggle(
		not_null<HistoryMessageTranslation*> translation,
		bool used) {
	if (translation->used != used && !translation->text.empty()) {
		translation->used = used;
		_history->owner().requestItemTextRefresh(this);
		if (!translation->manualTo.has_value()) {
			_history->owner().updateDependentMessages(this);
		}
	}
}

void HistoryItem::translationDone(LanguageId to, TextWithEntities result) {
	translationDone(to, std::move(result), nullptr);
}

void HistoryItem::translationDone(
		LanguageId to,
		std::shared_ptr<const Iv::RichPage> result) {
	auto summary = result
		? Iv::FlattenRichPageSummary(result)
		: TextWithEntities();
	translationDone(to, std::move(summary), std::move(result));
}

void HistoryItem::translationDone(
		LanguageId to,
		TextWithEntities result,
		std::shared_ptr<const Iv::RichPage> page) {
	if (const auto state = translation(); state && state->manualTo.has_value()) {
		return;
	}
	const auto set = [&](not_null<HistoryMessageTranslation*> translation) {
		if (result.empty()) {
			translation->failed = true;
		} else {
			translation->text = std::move(result);
			translation->richPage = std::move(page);
			if (_history->translatedTo() == to) {
				translationToggle(translation, true);
			}
		}
	};
	if (const auto translation = Get<HistoryMessageTranslation>()) {
		if (translation->to == to && translation->text.empty()) {
			translation->requested = false;
			set(translation);
		}
	} else {
		AddComponents(HistoryMessageTranslation::Bit());
		const auto added = Get<HistoryMessageTranslation>();
		added->to = to;
		set(added);
	}
}

const TextWithEntities &HistoryItem::translatedText() const {
	if (isService()) {
		static const auto kEmpty = TextWithEntities();
		return kEmpty;
	} else if (const auto translation = this->translation()
		; translation
		&& translation->used
		&& (translation->to == translation->manualTo.value_or(history()->translatedTo()))) {
		return translation->text;
	} else {
		return originalText();
	}
}

TextWithEntities HistoryItem::translatedTextWithLocalEntities() const {
	if (isService()) {
		return {};
	}
	auto result = withLocalEntities(translatedText());

	if (hideLinks()) {
		const auto isUrl = [](const EntityInText &entity) {
			const auto type = entity.type();
			return (type == EntityType::Mention)
				|| (type == EntityType::Hashtag)
				|| (type == EntityType::Cashtag)
				|| (type == EntityType::Url)
				|| (type == EntityType::CustomUrl);
		};
		const auto from = ranges::remove_if(result.entities, isUrl);
		if (from != result.entities.end()) {
			result.entities.erase(from, result.entities.end());
			setHasHiddenLinks(true);
		}
	}

	return result;
}

auto HistoryItem::translatedRichPage() const
-> std::shared_ptr<const Iv::RichPage> {
	const auto original = richPage();
	if (!original) {
		return nullptr;
	} else if (const auto translation = this->translation()
		; translation
		&& translation->used
		&& translation->richPage
		&& (translation->to == translation->manualTo.value_or(history()->translatedTo()))) {
		return translation->richPage;
	}
	return original;
}

void HistoryItem::removeTranslationBit() {
	RemoveComponents(HistoryMessageTranslation::Bit());
}
