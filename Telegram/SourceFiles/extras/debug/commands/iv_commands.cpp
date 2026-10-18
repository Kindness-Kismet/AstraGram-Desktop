#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "core/application.h"
#include "api/api_common.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "extras/debug/debug_login.h"
#include "history/history.h"
#include "iv/editor/iv_editor_session.h"
#include "iv/iv_controller.h"
#include "iv/iv_instance.h"
#include "main/main_session.h"
#include "settings.h"
#include "window/window_session_controller.h"

#include <QtGui/QKeyEvent>
#include <QtWidgets/QApplication>

namespace ExtrasDebug::Commands {
namespace {

Result openFile(const QStringList &args) {
	if (args.size() != 1) {
		return Result::Err(u"usage: iv.open-file <path>"_q);
	}
	if (!Core::App().iv().showMarkdown(args.front())) {
		return Result::Err(u"file could not be opened as a Markdown article"_q);
	}
	return Result::Ok();
}

Result openSite(const QStringList &args) {
	if (args.size() != 1 || !Iv::Controller::IsGoodTonSiteUrl(args.front())) {
		return Result::Err(u"usage: iv.open-site <ton-site-url>"_q);
	}
	Core::App().iv().showTonSite(args.front());
	return Result::Ok();
}

Result openEditor(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: iv.open-editor"_q);
	}
	const auto session = ActiveSession();
	if (!cDebugProfile() || !session || !isSimulationSession(session)) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	const auto peer = session->user();
	const auto controller = session->tryResolveWindow(peer);
	if (!controller) {
		return Result::Err(u"no session window"_q);
	}
	Iv::Editor::ShowComposeBox(
		controller,
		peer,
		Api::SendAction(session->data().history(peer)),
		{},
		{},
		nullptr,
		{ .scope = Iv::Editor::ComposeBoxOptions::Scope::Detached });
	return Result::Ok();
}

Result toggleSearch(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: iv.toggle-search"_q);
	}
	const auto window = controlWindow();
	if (!window || (window->objectName() != u"iv.preview"_q
		&& window->objectName() != u"iv.editor"_q)
		|| !window->isActiveWindow()) {
		return Result::Err(u"select and activate an article preview or editor window"_q);
	}
	// 文章窗口在快捷键覆盖事件中处理搜索，沿原有入口切换。
	auto event = QKeyEvent(QEvent::ShortcutOverride, Qt::Key_F, Qt::ControlModifier);
	event.ignore();
	QApplication::sendEvent(window, &event);
	return event.isAccepted()
		? Result::Ok()
		: Result::Err(u"article search shortcut was not handled"_q);
}

} // namespace

const HandlerMap &ivHandlers() {
	static const auto handlers = HandlerMap{
		{ u"iv.open-file"_q, &openFile },
		{ u"iv.open-site"_q, &openSite },
		{ u"iv.open-editor"_q, &openEditor },
		{ u"iv.toggle-search"_q, &toggleSearch },
	};
	return handlers;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
