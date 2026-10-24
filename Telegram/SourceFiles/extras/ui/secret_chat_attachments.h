#pragma once

class QMimeData;
namespace Ui { class RpWidget; struct PreparedList; }
namespace Window { class SessionController; }
namespace HistoryView { class ComposeControls; }

namespace Extras::SecretChats {

void chooseAttachment(not_null<Window::SessionController*> controller,
	not_null<Ui::RpWidget*> parent, int chatId,
	not_null<HistoryView::ComposeControls*> compose);
bool insertAttachments(not_null<Window::SessionController*> controller,
	not_null<Ui::RpWidget*> parent, int chatId,
	not_null<HistoryView::ComposeControls*> compose,
	not_null<const QMimeData*> data);

} // namespace Extras::SecretChats
