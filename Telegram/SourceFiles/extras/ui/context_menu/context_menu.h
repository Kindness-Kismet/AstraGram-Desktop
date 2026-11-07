#pragma once

#include "extras/extras_settings.h"
#include "data/data_document.h"
#include "history/view/history_view_element.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"

namespace HistoryView {
enum class Context : char;
}

namespace ExtrasUi {

[[nodiscard]] Ui::PopupMenu *contextMenuTarget(
	ContextMenuVisibility state,
	not_null<Ui::PopupMenu*> menu);

void AddExtrasActions(PeerData *peerData,
							   Data::Thread *thread,
							   not_null<Window::SessionController*> sessionController,
							   const Window::PeerMenuCallback &addCallback);

void AddJumpToBeginningAction(PeerData *peerData,
							  Data::Thread *thread,
							  not_null<Window::SessionController*> sessionController,
							  const Window::PeerMenuCallback &addCallback);

void AddShadowBanAction(PeerData *peerData,
						const Window::PeerMenuCallback &addCallback);
void AddOpenChannelAction(PeerData *peerData,
						  not_null<Window::SessionController*> sessionController,
						  const Window::PeerMenuCallback &addCallback);
void AddDeleteOwnMessagesAction(PeerData *peerData,
								Data::ForumTopic *topic,
								not_null<Window::SessionController*> sessionController,
								const Window::PeerMenuCallback &addCallback);

void AddHistoryAction(not_null<Ui::PopupMenu*> menu, HistoryItem *item);
void AddUserMessagesAction(not_null<Ui::PopupMenu*> menu, HistoryItem *item);
void AddMessageDetailsAction(not_null<Ui::PopupMenu*> menu, HistoryItem *item);
void AddRepeatMessageAction(
	not_null<Ui::PopupMenu*> menu,
	HistoryItem *item,
	HistoryView::Context context = HistoryView::Context::History,
	bool asGroup = false);
void AddReadUntilAction(not_null<Ui::PopupMenu*> menu, HistoryItem *item);
void AddBurnAction(not_null<Ui::PopupMenu*> menu, HistoryItem *item);
void AddVideoCoverAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller,
	HistoryItem *item,
	not_null<DocumentData*> document);
void AddCreateFilterAction(not_null<Ui::PopupMenu*> menu,
						   not_null<Window::SessionController*> controller,
						   HistoryItem *item,
						   const QString &selectedText);

}
