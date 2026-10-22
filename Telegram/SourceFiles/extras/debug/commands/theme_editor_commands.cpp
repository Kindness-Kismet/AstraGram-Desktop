#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "core/application.h"
#include "extras/debug/debug_login.h"
#include "main/main_session.h"
#include "settings.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor.h"
#include "window/themes/window_theme_editor_box.h"
#include "window/window_controller.h"

namespace ExtrasDebug::Commands {
namespace {

using Json = nlohmann::json;
using ThemeEditor = Window::Theme::Editor;

[[nodiscard]] ThemeEditor *activeEditor() {
	const auto window = controlWindow();
	if (!window) {
		return nullptr;
	}
	for (const auto widget : window->findChildren<QWidget*>()) {
		if (const auto editor = dynamic_cast<ThemeEditor*>(widget)) {
			return editor;
		}
	}
	return nullptr;
}

[[nodiscard]] Json sizeState(not_null<QWidget*> widget) {
	return Json{
		{ "width", widget->width() },
		{ "height", widget->height() },
	};
}

[[nodiscard]] Result describeEditor() {
	const auto editor = activeEditor();
	if (!editor) {
		return Result::Err(u"no theme editor in the selected window"_q);
	}
	const auto box = editor->parentWidget();
	Expects(box != nullptr);
	return Result::Ok(Compact(Json{
		{ "window", sizeState(editor->window()) },
		{ "box", sizeState(box) },
		{ "content", sizeState(editor) },
		{ "visible", editor->isVisible() },
		{ "narrow", editor->width() < style::ConvertScale(660) },
		{ "detailsPage", editor->showingDetails() },
		{ "selected", editor->selectedName().toStdString() },
		{ "value", editor->selectedValue().toStdString() },
		{ "color", Window::Theme::ColorHexString(editor->selectedColor()).toStdString() },
		{ "visibleColors", editor->visibleColorCount() },
		{ "modifiedColors", editor->modifiedColorCount() },
		{ "paletteBytes", editor->paletteContent().size() },
		{ "editing", Window::Theme::Background()->editingTheme().has_value() },
	}));
}

[[nodiscard]] bool isolatedSimulation() {
	const auto session = ActiveSession();
	return cDebugProfile() && session && isSimulationSession(session);
}

Result openThemeEditor(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: theme.editor.open"_q);
	}
	if (!isolatedSimulation()) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	if (activeEditor()) {
		return describeEditor();
	}
	const auto controller = Core::App().activePrimaryWindow();
	if (!controller) {
		return Result::Err(u"no primary window"_q);
	}
	using namespace Window::Theme;
	if (const auto editing = Background()->editingTheme()) {
		controller->show(Box<ThemeEditor>(controller, *editing));
	} else {
		StartEditor(controller, Data::CloudTheme());
	}
	return describeEditor();
}

Result themeEditorState(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: theme.editor.state"_q);
	}
	return describeEditor();
}

Result selectThemeColor(const QStringList &args) {
	if (args.size() != 1) {
		return Result::Err(u"usage: theme.editor.select <colorName>"_q);
	}
	const auto editor = activeEditor();
	if (!editor) {
		return Result::Err(u"no theme editor in the selected window"_q);
	}
	editor->selectColor(args.front());
	if (editor->selectedName() != args.front()) {
		return Result::Err(u"color selection was not applied"_q);
	}
	return describeEditor();
}

Result setThemeColor(const QStringList &args) {
	if (args.size() != 2) {
		return Result::Err(u"usage: theme.editor.set <colorName> <#rrggbb[aa]|sourceName>"_q);
	}
	if (!isolatedSimulation()) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	const auto editor = activeEditor();
	if (!editor) {
		return Result::Err(u"no theme editor in the selected window"_q);
	}
	if (!editor->applyEditing(args[0], args[1])) {
		return Result::Err(u"color edit was not applied; check the value and reference order"_q);
	}
	editor->selectColor(args[0]);
	return describeEditor();
}

Result importThemeFile(const QStringList &args) {
	if (args.size() != 1) {
		return Result::Err(u"usage: theme.editor.import <filePath>"_q);
	}
	if (!isolatedSimulation()) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	const auto editor = activeEditor();
	if (!editor) {
		return Result::Err(u"no theme editor in the selected window"_q);
	}
	if (!editor->importThemeFile(args.front())) {
		return Result::Err(u"theme import was not applied"_q);
	}
	return describeEditor();
}

} // namespace

const HandlerMap &themeEditorHandlers() {
	static const auto handlers = HandlerMap{
		{ u"theme.editor.open"_q, &openThemeEditor },
		{ u"theme.editor.state"_q, &themeEditorState },
		{ u"theme.editor.select"_q, &selectThemeColor },
		{ u"theme.editor.set"_q, &setThemeColor },
		{ u"theme.editor.import"_q, &importThemeFile },
	};
	return handlers;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
