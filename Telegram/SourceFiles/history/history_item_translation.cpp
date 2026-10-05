#include "history/history_item.h"

#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "iv/iv_rich_page.h"

const HistoryMessageTranslation *HistoryItem::translation() const {
	return Get<HistoryMessageTranslation>();
}

bool HistoryItem::translationDisplayed() const {
	const auto state = translation();
	return state && state->used
		&& state->to == state->manualTo.value_or(history()->translatedTo());
}

bool HistoryItem::translationStartManual(
		LanguageId to,
		uint64 token,
		const TextWithEntities &source) {
	Expects(to && token);

	AddComponents(HistoryMessageTranslation::Bit());
	const auto state = Get<HistoryMessageTranslation>();
	state->manualTo = to;
	if (state->to == to && !state->text.empty()
		&& state->source == source && state->sourcePage == richPage()) {
		state->manualRequestToken = 0;
		translationToggle(state, true);
		return false;
	}
	translationToggle(state, false);
	state->to = to;
	state->text = {};
	state->richPage = nullptr;
	state->source = source;
	state->sourcePage = richPage();
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
	const auto state = translation();
	if (state && state->manualTo.has_value()) {
		return false;
	}
	// 与实际切换方法保持一致，避免重复刷新。
	if (!to) {
		return state && ((!state->failed && state->text.empty()) || state->used);
	}
	return !state || state->to != to || (!state->used && !state->text.empty());
}

bool HistoryItem::translationShowRequiresRequest(LanguageId to) {
	auto state = Get<HistoryMessageTranslation>();
	if (state && state->manualTo.has_value()) {
		return false;
	}
	// 手动选择由单条请求管理，聊天批次只修改跟随聊天的消息。
	if (!to) {
		if (!state) {
			return false;
		}
		if (!state->failed && state->text.empty()) {
			Assert(!state->used);
			RemoveComponents(HistoryMessageTranslation::Bit());
		} else {
			translationToggle(state, false);
		}
		return false;
	}
	if (state && state->to == to) {
		translationToggle(state, true);
		return false;
	}
	if (!state) {
		AddComponents(HistoryMessageTranslation::Bit());
		state = Get<HistoryMessageTranslation>();
	}
	translationToggle(state, false);
	state->to = to;
	state->requested = true;
	state->failed = false;
	state->text = {};
	state->richPage = nullptr;
	state->source = originalText();
	state->sourcePage = richPage();
	return true;
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
		added->source = originalText();
		added->sourcePage = richPage();
		set(added);
	}
}

const TextWithEntities &HistoryItem::translatedText() const {
	if (isService()) {
		static const auto kEmpty = TextWithEntities();
		return kEmpty;
	}
	return translationDisplayed() ? translation()->text : originalText();
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
	}
	const auto state = translation();
	if (translationDisplayed() && state->richPage) {
		return state->richPage;
	}
	return original;
}

void HistoryItem::removeTranslationBit() {
	RemoveComponents(HistoryMessageTranslation::Bit());
}
