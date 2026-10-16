#include "settings/sections/settings_language.h"

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
#include "ui/boxes/dialog_layout.h"
#include "ui/layers/box_content.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

enum class LanguageLayout {
	Page,
	Dialog,
};

[[nodiscard]] not_null<Ui::VerticalLayout*> AddLanguageGroup(
		not_null<Ui::VerticalLayout*> container,
		LanguageLayout layout) {
	if (layout == LanguageLayout::Page) {
		return AddCardGroup(container);
	}
	return container->add(object_ptr<Ui::VerticalLayout>(container));
}

void SetupTranslationControls(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller,
	rpl::producer<> showFinished,
	LanguageLayout layout);

class LanguageContent final : public Ui::RpWidget {
public:
	LanguageContent(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		LanguageLayout layout);

	void showFinished();
	[[nodiscard]] bool handleNavigation(int key);

protected:
	void visibleTopBottomUpdated(int top, int bottom) override;

private:
	void rebuildLanguages();
	void scrollToRequest(Ui::ScrollToRequest request);

	const not_null<Ui::ScrollArea*> _scroll;
	Ui::RpWidget *_body = nullptr;
	Ui::VerticalLayout *_languages = nullptr;
	QPointer<Ui::RpWidget> _list;
	LanguageListContent _listContent;
	rpl::event_stream<> _showFinished;

};

class Language final : public AbstractSection {
public:
	Language(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll);

	[[nodiscard]] Type id() const override;
	[[nodiscard]] rpl::producer<QString> title() override;
	void showFinished() override;

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void visibleTopBottomUpdated(int top, int bottom) override;

private:
	const not_null<LanguageContent*> _content;

};

class LanguageSettingsBox final : public Ui::BoxContent {
public:
	LanguageSettingsBox(
		QWidget*,
		not_null<Window::SessionController*> controller);

	void showFinished() override;
	void setInnerFocus() override;

protected:
	void prepare() override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	const not_null<Window::SessionController*> _controller;
	const object_ptr<Ui::ScrollArea> _scroll;
	QPointer<LanguageContent> _content;

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

LanguageContent::LanguageContent(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Ui::ScrollArea*> scroll,
	LanguageLayout layout)
: RpWidget(parent)
, _scroll(scroll) {
	const auto content = [&]() -> not_null<Ui::VerticalLayout*> {
		if (layout == LanguageLayout::Page) {
			const auto page = Ui::CreateChild<CardPage>(this);
			_body = page;
			return page->content();
		}
		const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
		_body = content;
		Ui::AddSkip(content, st::boxPadding.top());
		return content;
	}();
	SetupTranslationControls(
		content,
		controller,
		_showFinished.events(),
		layout);
	if (layout == LanguageLayout::Page) {
		AddCardTitle(content, tr::lng_languages());
	} else {
		Ui::AddSkip(content, st::boxMediumSkip);
		Ui::AddSubsectionTitle(
			content,
			tr::lng_languages(),
			QMargins(st::boxRowPadding.left()
				- st::defaultSubsectionTitlePadding.left(), 0, 0, 0));
	}
	_languages = AddLanguageGroup(content, layout).get();
	rebuildLanguages();
	if (layout == LanguageLayout::Dialog) {
		Ui::AddSkip(content, st::boxPadding.bottom());
	}
	Ui::ResizeFitChild(this, _body);

	auto &manager = Lang::CurrentCloudManager();
	manager.languageListChanged(
	) | rpl::on_next([=] {
		rebuildLanguages();
	}, lifetime());
	manager.requestLanguageList();
}

void LanguageContent::showFinished() {
	_showFinished.fire({});
}

void LanguageContent::visibleTopBottomUpdated(int top, int bottom) {
	setChildVisibleTopBottom(_body, top, bottom);
}

void LanguageContent::rebuildLanguages() {
	_languages->clear();
	_listContent = CreateLanguageList(_languages, false);
	_list = _languages->add(std::move(_listContent.widget));
	_list->setObjectName(u"settings-language-list"_q);
	std::move(_listContent.scrollRequests
	) | rpl::on_next([=](Ui::ScrollToRequest request) {
		scrollToRequest(request);
	}, _list->lifetime());
}

bool LanguageContent::handleNavigation(int key) {
	const auto rows = std::max(_scroll->height() / _listContent.rowHeight, 1);
	auto skip = 0;
	switch (key) {
	case Qt::Key_Return:
	case Qt::Key_Enter: _listContent.submit(); return true;
	case Qt::Key_Up: skip = -1; break;
	case Qt::Key_Down: skip = 1; break;
	case Qt::Key_PageUp: skip = -rows; break;
	case Qt::Key_PageDown: skip = rows; break;
	default: return false;
	}
	scrollToRequest(_listContent.jump(skip));
	return true;
}

void LanguageContent::scrollToRequest(Ui::ScrollToRequest request) {
	if (request.ymin < 0 || request.ymax < 0) {
		return;
	}
	const auto top = _list->mapTo(_scroll->widget(), QPoint()).y();
	_scroll->scrollToY(top + request.ymin, top + request.ymax);
}

Language::Language(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Ui::ScrollArea*> scroll)
: AbstractSection(parent, controller)
, _content(Ui::CreateChild<LanguageContent>(
	this,
	controller,
	scroll,
	LanguageLayout::Page)) {
	setObjectName(u"settings-language-page"_q);
	Ui::ResizeFitChild(this, _content);
}

Type Language::id() const {
	return LanguageId();
}

rpl::producer<QString> Language::title() {
	return tr::lng_settings_language();
}

void Language::showFinished() {
	AbstractSection::showFinished();
	_content->showFinished();
}

void Language::visibleTopBottomUpdated(int top, int bottom) {
	setChildVisibleTopBottom(_content, top, bottom);
}

void Language::keyPressEvent(QKeyEvent *e) {
	if (_content->handleNavigation(e->key())) {
		e->accept();
		return;
	}
	AbstractSection::keyPressEvent(e);
}

LanguageSettingsBox::LanguageSettingsBox(
	QWidget*,
	not_null<Window::SessionController*> controller)
: _controller(controller)
, _scroll(this, st::boxScroll) {
}

void LanguageSettingsBox::prepare() {
	setObjectName(u"settings-language-dialog"_q);
	setTitle(tr::lng_settings_language());
	_content = _scroll->setOwnedWidget(object_ptr<LanguageContent>(
		_scroll.data(),
		_controller,
		_scroll.data(),
		LanguageLayout::Dialog));
	_content->resizeToWidth(st::boxWideWidth);
	_content->heightValue() | rpl::on_next([=](int height) {
		setDimensions(st::boxWideWidth, std::min(height, st::boxMaxListHeight));
	}, _content->lifetime());
	sizeValue() | rpl::on_next([=](QSize size) {
		_scroll->resize(size);
		_content->resizeToWidth(size.width());
	}, _content->lifetime());
	rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue()
	) | rpl::on_next([=](int top, int height) {
		_content->setVisibleTopBottom(top, top + height);
	}, _content->lifetime());
	_scroll->show();
}

void LanguageSettingsBox::showFinished() {
	_content->showFinished();
}

void LanguageSettingsBox::setInnerFocus() {
	setFocus();
}

void LanguageSettingsBox::keyPressEvent(QKeyEvent *e) {
	if (_content->handleNavigation(e->key())) {
		e->accept();
		return;
	}
	Ui::BoxContent::keyPressEvent(e);
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
	controller->show(Box<LanguageSettingsBox>(controller));
}

namespace {

void SetupTranslationControls(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		rpl::producer<> showFinished,
		LanguageLayout layout) {
	const auto dialog = (layout == LanguageLayout::Dialog);
	const auto group = AddLanguageGroup(container, layout);
	const auto &buttonStyle = dialog
		? Ui::DialogButtonStyle(group, st::settingsButtonNoIcon)
		: st::settingsButtonNoIcon;
	const auto translateEnabled = AddButtonWithIcon(
		group,
		tr::lng_translate_settings_show(),
		buttonStyle)->toggleOn(
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
		buttonStyle)->toggleOn(
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
		buttonStyle);
	translateSkip->setObjectName(u"settings-language-do-not-translate"_q);
	if (!dialog) {
		AddSectionRowDetails(translateSkip);
	}
	translateSkip->setClickedCallback([=] {
		controller->show(Ui::EditSkipTranslationLanguages());
	});
	if (dialog) {
		Ui::AddDialogText(container, tr::lng_translate_settings_about());
	} else {
		AddCardDescription(container, tr::lng_translate_settings_about());
	}
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

} // namespace

void SetupLanguageTranslationControls(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		rpl::producer<> showFinished) {
	SetupTranslationControls(
		container,
		controller,
		std::move(showFinished),
		Ui::IsDialogContent(container)
			? LanguageLayout::Dialog
			: LanguageLayout::Page);
}

} // namespace Settings
