#include "extras/features/translator/message_translation.h"

#include "boxes/translate_box.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_poll.h"
#include "data/data_session.h"
#include "extras/extras_settings.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_context_menu.h"
#include "iv/iv_rich_page.h"
#include "lang/lang_keys.h"
#include "lang/translate_provider.h"
#include "main/main_session.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"

namespace Extras::Translator {
namespace {

TextWithEntities sourceText(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (const auto poll = media ? media->poll() : nullptr) {
		auto result = poll->question;
		for (const auto &answer : poll->answers) {
			result.append(u"\n• "_q).append(answer.text);
		}
		return result;
	}
	auto result = HistoryView::TransribedText(item);
	if (!result.empty() && !item->originalText().empty()) {
		result.append(u"\n\n"_q);
	}
	return result.append(item->originalText());
}

} // namespace

MessageTranslationManager::MessageTranslationManager(
	not_null<Main::Session*> session)
: _session(session)
, _provider(Ui::CreateTranslateProvider(session))
, _api(&session->mtp()) {
	ExtrasSettings::getInstance().translationProviderChanges(
	) | rpl::on_next([=] {
		resetProvider();
	}, _lifetime);
}

MessageTranslationManager::~MessageTranslationManager() = default;

void MessageTranslationManager::translate(
		not_null<HistoryItem*> item,
		Fn<void(QString)> reportError) {
	const auto id = item->fullId();
	const auto original = sourceText(item);
	const auto page = item->richPage();
	if (const auto i = _pending.find(id); i != _pending.end()) {
		const auto state = item->translation();
		if (state && state->manualRequestToken == i->second.token
			&& original == i->second.original && page == i->second.page) {
			return;
		}
		if (i->second.requestId) {
			_api.request(i->second.requestId).cancel();
		}
		item->translationFinishManual(i->second.token, {}, nullptr);
		_pending.erase(i);
	}
	if (original.empty() && !page) {
		return;
	}
	if (page && !Ui::SupportsRichMessageTranslation()) {
		reportError(tr::extras_TranslationUnsupported(tr::now));
		return;
	}
	const auto history = item->history();
	const auto to = history->translatedTo()
		? history->translatedTo()
		: Ui::ChooseTranslateTo(history);
	const auto token = ++_nextToken;
	_manual.emplace(id);
	if (!item->translationStartManual(to, token, original)) {
		return;
	}
	_pending.emplace(id, Pending{
		.token = token,
		.original = original,
		.page = page,
		.reportError = std::move(reportError),
	});
	const auto weak = base::make_weak(this);
	if (page) {
		using Flag = MTPmessages_TranslateRichMessage::Flag;
		const auto requestId = _api.request(MTPmessages_TranslateRichMessage(
			MTP_flags(Flag::f_peer | Flag::f_id),
			history->peer->input(),
			MTP_vector<MTPint>({ MTP_int(id.msg) }),
			MTPVector<MTPInputRichMessage>(),
			MTP_string(to.twoLetterCode()),
			MTPstring()
		)).done([=](const MTPmessages_TranslatedRichMessage &result) {
			if (!weak) {
				return;
			}
			const auto &list = result.data().vresult().v;
			const auto translated = list.isEmpty()
				? nullptr
				: Iv::ParseRichPage(_session, list.front());
			finish(id, token,
				translated ? Iv::FlattenRichPageSummary(translated) : TextWithEntities(),
				translated,
				translated ? QString() : tr::lng_translate_box_error(tr::now));
		}).fail([=](const MTP::Error &) {
			if (weak) {
				finish(id, token, {}, nullptr, tr::lng_translate_box_error(tr::now));
			}
		}).send();
		if (const auto i = _pending.find(id); i != _pending.end()) {
			i->second.requestId = requestId;
		}
		return;
	}
	// 直接翻译当前文本快照，兼容留档、说明文字和转写内容。
	_provider->request({ .text = original }, to, [=](Ui::TranslateProviderResult result) {
		if (!weak) {
			return;
		}
		const auto missingPack = result.error
			== Ui::TranslateProviderError::LocalLanguagePackMissing;
		const auto error = missingPack
			? tr::lng_translate_box_error_language_pack_not_installed(tr::now)
			: (result.error != Ui::TranslateProviderError::None
				|| !result.text || result.text->empty())
			? tr::lng_translate_box_error(tr::now)
			: QString();
		finish(id, token, error.isEmpty()
			? std::move(*result.text)
			: TextWithEntities(), nullptr, error);
	});
}

void MessageTranslationManager::finish(
		FullMsgId id,
		uint64 token,
		TextWithEntities text,
		std::shared_ptr<const Iv::RichPage> page,
		QString error) {
	const auto i = _pending.find(id);
	if (i == _pending.end() || i->second.token != token) {
		return;
	}
	auto pending = std::move(i->second);
	_pending.erase(i);
	const auto item = _session->data().message(id);
	if (!item) {
		return;
	}
	if (sourceText(item) != pending.original || item->richPage() != pending.page) {
		item->translationFinishManual(token, {}, nullptr);
		return;
	}
	if (!item->translationFinishManual(token, std::move(text), std::move(page))) {
		return;
	}
	if (!error.isEmpty()) {
		pending.reportError(error);
	}
}

void MessageTranslationManager::showOriginal(not_null<HistoryItem*> item) {
	if (const auto i = _pending.find(item->fullId()); i != _pending.end()) {
		if (i->second.requestId) {
			_api.request(i->second.requestId).cancel();
		}
		_pending.erase(i);
	}
	_manual.emplace(item->fullId());
	item->translationShowOriginal();
}

void MessageTranslationManager::resetProvider() {
	for (const auto &[id, pending] : base::take(_pending)) {
		if (pending.requestId) {
			_api.request(pending.requestId).cancel();
		}
	}
	_provider = Ui::CreateTranslateProvider(_session);
	for (const auto id : base::take(_manual)) {
		if (const auto item = _session->data().message(id)) {
			item->removeTranslationBit();
			_session->data().requestItemTextRefresh(item);
		}
	}
}

void addMessageTranslationActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item,
		not_null<Window::SessionController*> controller) {
	const auto session = &item->history()->session();
	const auto id = item->fullId();
	const auto weak = base::make_weak(session);
	const auto original = [=] {
		if (weak) {
			if (const auto item = session->data().message(id)) {
				session->messageTranslations().showOriginal(item);
			}
		}
	};
	const auto translation = item->translation();
	if (translation && translation->manualRequestToken && translation->requested) {
		menu->addAction(tr::extras_TranslationPending(tr::now), [] {
		}, &st::menuIconTranslate)->setEnabled(false);
		menu->addAction(tr::lng_cancel(tr::now), original, &st::menuIconCancel);
		return;
	}
	if (item->translationDisplayed()) {
		menu->addAction(tr::lng_translate_show_original(tr::now), original,
			&st::menuIconTranslate);
		return;
	}
	if (Ui::SkipTranslate(sourceText(item))) {
		return;
	}
	menu->addAction(tr::lng_context_translate(tr::now), crl::guard(controller, [=] {
		if (!weak) {
			return;
		}
		if (const auto item = session->data().message(id)) {
			session->messageTranslations().translate(item,
				crl::guard(controller, [=](QString error) {
					controller->showToast(error);
				}));
		}
	}), &st::menuIconTranslate);
}

} // namespace Extras::Translator
