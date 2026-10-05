#include "settings/sections/settings_language.h"

#include "base/event_filter.h"
#include "boxes/language_box.h"
#include "boxes/translate_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_cloud_manager.h"
#include "lang/lang_keys.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_card_layout.h"
#include "settings/settings_common_session.h"
#include "spellcheck/spellcheck_types.h"
#include "ui/boxes/choose_language_box.h"
#include "ui/search_field_controller.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

class Language final : public AbstractSection {
public:
	Language(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll);

	[[nodiscard]] Type id() const override;
	[[nodiscard]] rpl::producer<QString> title() override;
	[[nodiscard]] base::weak_qptr<Ui::RpWidget> createPinnedToTop(
		not_null<QWidget*> parent) override;
	void setInnerFocus() override;
	void sectionSaveState(std::any &state) override;
	void sectionRestoreState(const std::any &state) override;

protected:
	void keyPressEvent(QKeyEvent *e) override;

private:
	void rebuildLanguages();
	void scrollToRequest(Ui::ScrollToRequest request);
	[[nodiscard]] bool handleNavigation(int key);

	const not_null<Ui::ScrollArea*> _scroll;
	const not_null<CardPage*> _page;
	Ui::VerticalLayout *_languages = nullptr;
	QPointer<Ui::RpWidget> _list;
	LanguageListContent _listContent;
	std::unique_ptr<Ui::SearchFieldController> _searchController;
	QPointer<Ui::InputField> _searchField;
	QString _query;
};

class LanguageFactory final : public AbstractSectionFactory {
public:
	object_ptr<AbstractSection> create(
			not_null<QWidget*> parent,
			not_null<Window::SessionController*> controller,
			not_null<Ui::ScrollArea*> scroll,
			rpl::producer<Container> containerValue) const override {
		return object_ptr<Language>(parent, controller, scroll);
	}
};

Language::Language(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Ui::ScrollArea*> scroll)
: AbstractSection(parent, controller)
, _scroll(scroll)
, _page(Ui::CreateChild<CardPage>(this)) {
	setObjectName(u"settings-language-page"_q);
	const auto content = _page->content();
	SetupLanguageTranslationControls(content, controller, showFinishes());
	AddCardTitle(content, tr::lng_languages());
	_languages = AddCardGroup(content).get();
	rebuildLanguages();
	Ui::ResizeFitChild(this, _page);

	auto &manager = Lang::CurrentCloudManager();
	manager.languageListChanged(
	) | rpl::on_next([=] {
		rebuildLanguages();
	}, lifetime());
	manager.requestLanguageList();
}

Type Language::id() const {
	return LanguageId();
}

rpl::producer<QString> Language::title() {
	return tr::lng_settings_language();
}

void Language::rebuildLanguages() {
	_languages->clear();
	_listContent = CreateLanguageList(_languages, false);
	_list = _languages->add(std::move(_listContent.widget));
	_list->setObjectName(u"settings-language-list"_q);
	_listContent.filter(_query);
	std::move(_listContent.scrollRequests
	) | rpl::on_next([=](Ui::ScrollToRequest request) {
		scrollToRequest(request);
	}, _list->lifetime());
}

base::weak_qptr<Ui::RpWidget> Language::createPinnedToTop(
		not_null<QWidget*> parent) {
	auto search = CreateSectionSearchRow(parent, _query);
	_searchController = std::move(search.controller);
	_searchField = search.field;
	_searchField->setObjectName(u"settings-language-search"_q);
	_searchField->setPlaceholder(tr::lng_participant_filter());
	_searchField->customUpDown(true);

	_searchController->queryChanges(
	) | rpl::on_next([=](const QString &query) {
		_query = query;
		_listContent.filter(query);
	}, search.row->lifetime());
	_searchField->submits(
	) | rpl::on_next([=] {
		_listContent.submit();
	}, search.row->lifetime());
	base::install_event_filter(_searchField, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::KeyPress) {
			return base::EventFilterResult::Continue;
		}
		const auto key = static_cast<QKeyEvent*>(e.get())->key();
		if (key == Qt::Key_Escape && !_query.isEmpty()) {
			_searchField->setText(QString());
			return base::EventFilterResult::Cancel;
		}
		return handleNavigation(key)
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	}, search.row->lifetime());
	return base::make_weak(search.row);
}

bool Language::handleNavigation(int key) {
	const auto rows = std::max(_scroll->height() / _listContent.rowHeight, 1);
	auto skip = 0;
	switch (key) {
	case Qt::Key_Up: skip = -1; break;
	case Qt::Key_Down: skip = 1; break;
	case Qt::Key_PageUp: skip = -rows; break;
	case Qt::Key_PageDown: skip = rows; break;
	default: return false;
	}
	scrollToRequest(_listContent.jump(skip));
	return true;
}

void Language::scrollToRequest(Ui::ScrollToRequest request) {
	if (request.ymin < 0 || request.ymax < 0) {
		return;
	}
	const auto top = _list->mapTo(_scroll->widget(), QPoint()).y();
	_scroll->scrollToY(top + request.ymin, top + request.ymax);
}

void Language::keyPressEvent(QKeyEvent *e) {
	if (handleNavigation(e->key())) {
		e->accept();
		return;
	}
	AbstractSection::keyPressEvent(e);
}

void Language::setInnerFocus() {
	if (_searchField) {
		_searchField->setFocus();
	}
}

void Language::sectionSaveState(std::any &state) {
	state = _query;
}

void Language::sectionRestoreState(const std::any &state) {
	const auto saved = std::any_cast<QString>(&state);
	if (!saved) {
		return;
	}
	_query = *saved;
	_listContent.filter(_query);
	if (_searchField) {
		_searchField->setText(_query);
	}
}

const auto kMeta = Builder::BuildHelper({
	.id = LanguageId(),
	.parentId = MainId(),
	.title = &tr::lng_settings_language,
	.icon = &st::menuIconLanguage,
}, [](Builder::SectionBuilder &builder) {
	builder.add(nullptr, [] {
		return Builder::SearchEntry{
			.id = u"language/show-button"_q,
			.title = tr::lng_translate_settings_show(tr::now),
			.keywords = { u"translation"_q, u"button"_q },
		};
	});
	builder.add(nullptr, [] {
		return Builder::SearchEntry{
			.id = u"language/translate-chats"_q,
			.title = tr::lng_translate_settings_chat(tr::now),
			.keywords = { u"translation"_q, u"chats"_q },
		};
	});
	builder.add(nullptr, [] {
		return Builder::SearchEntry{
			.id = u"language/do-not-translate"_q,
			.title = tr::lng_translate_settings_choose(tr::now),
			.keywords = { u"translation"_q, u"skip"_q, u"languages"_q },
		};
	});
});

} // namespace

Type LanguageId() {
	static const auto result = std::make_shared<LanguageFactory>();
	return result;
}

void ShowLanguageSettings(
		not_null<Window::SessionController*> controller,
		const QString &highlightId) {
	if (!highlightId.isEmpty()) {
		controller->setHighlightControlId(highlightId);
	}
	ShowSettingsLayer(controller, LanguageId());
}

void SetupLanguageTranslationControls(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		rpl::producer<> showFinished) {
	const auto group = AddCardGroup(container);
	const auto translateEnabled = AddButtonWithIcon(
		group,
		tr::lng_translate_settings_show(),
		st::settingsButtonNoIcon)->toggleOn(
				rpl::single(Core::App().settings().translateButtonEnabled()));
	translateEnabled->setObjectName(u"settings-language-show-button"_q);
	translateEnabled->toggledValue(
	) | rpl::filter([](bool checked) {
		return checked != Core::App().settings().translateButtonEnabled();
	}) | rpl::on_next([=](bool checked) {
		Core::App().settings().setTranslateButtonEnabled(checked);
		Core::App().saveSettingsDelayed();
	}, translateEnabled->lifetime());

	const auto translateChat = AddButtonWithIcon(
		group,
		tr::lng_translate_settings_chat(),
		st::settingsButtonNoIcon)->toggleOn(
				Core::App().settings().translateChatEnabledValue());
	translateChat->setObjectName(u"settings-language-translate-chats"_q);
	translateChat->toggledValue(
	) | rpl::filter([](bool checked) {
		return checked != Core::App().settings().translateChatEnabled();
	}) | rpl::on_next([=](bool checked) {
		Core::App().settings().setTranslateChatEnabled(checked);
		Core::App().saveSettingsDelayed();
	}, translateChat->lifetime());

	const auto translateSkipWrap = group->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			group,
			object_ptr<Ui::VerticalLayout>(group)));
	translateSkipWrap->toggleOn(rpl::combine(
		translateEnabled->toggledValue(),
		translateChat->toggledValue(),
		rpl::mappers::_1 || rpl::mappers::_2));
	translateSkipWrap->finishAnimating();
	const auto translateSkip = AddButtonWithLabel(
		translateSkipWrap->entity(),
		tr::lng_translate_settings_choose(),
		Core::App().settings().skipTranslationLanguagesValue(
		) | rpl::map([](const std::vector<::LanguageId> &list) {
			return (list.size() > 1)
				? tr::lng_languages_count(tr::now, lt_count, list.size())
				: Ui::LanguageName(list.front());
		}),
		st::settingsButtonNoIcon);
	translateSkip->setObjectName(u"settings-language-do-not-translate"_q);
	AddSectionRowDetails(translateSkip);
	translateSkip->setClickedCallback([=] {
		controller->show(Ui::EditSkipTranslationLanguages());
	});
	AddCardDescription(container, tr::lng_translate_settings_about());
	std::move(showFinished) | rpl::on_next([=] {
		controller->checkHighlightControl(
			u"language/show-button"_q,
			translateEnabled);
		controller->checkHighlightControl(
			u"language/translate-chats"_q,
			translateChat);
		controller->checkHighlightControl(
			u"language/do-not-translate"_q,
			translateSkip.get());
	}, container->lifetime());
}

} // namespace Settings
