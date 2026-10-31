#pragma once

class DocumentData;

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Window {
class SessionController;
} // namespace Window

namespace ExtrasUi {

void addDeleteDownloadedFileAction(
	const Ui::Menu::MenuCallback &addAction,
	not_null<Window::SessionController*> controller,
	not_null<DocumentData*> document,
	const QString &path = {});

} // namespace ExtrasUi
