#include "extras/ui/context_menu/downloaded_file_menu.h"

#include "extras/features/downloaded_file/downloaded_file.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"

namespace ExtrasUi {

void addDeleteDownloadedFileAction(
		const Ui::Menu::MenuCallback &addAction,
		not_null<Window::SessionController*> controller,
		not_null<DocumentData*> document,
		const QString &path) {
	const auto file = ExtrasDownloadedFile::location(document, path);
	if (file.isEmpty()) {
		return;
	}
	const auto weak = base::make_weak(&document->session());
	const auto id = document->id;
	const auto show = controller->uiShow();
	addAction(tr::extras_DeleteDownloadedFile(tr::now), [=] {
		if (!weak) {
			return;
		}
		ExtrasDownloadedFile::remove(weak->data().document(id), file, [=](bool success) {
			if (!success) {
				show->showToast(tr::extras_DeleteDownloadedFileFailed(tr::now));
			}
		});
	}, &st::menuIconDelete);
}

} // namespace ExtrasUi
