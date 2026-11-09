#include "extras/features/delete_messages/local_message_deletion.h"

#include "extras/data/conversation_cache.h"
#include "extras/data/extras_database.h"
#include "extras/data/messages_storage.h"
#include "extras/utils/telegram_helpers.h"
#include "data/components/ephemeral_messages.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "tgw/tgw_storage.h"

namespace Extras::DeleteMessages {

void forgetLocalMessages(
		not_null<Main::Session*> session,
		PeerId peer,
		QVector<MTPint> ids,
		Fn<void()> done) {
	Expects(peer != PeerId());

	session->conversationCache().messagesDeleted(peer, ids, crl::guard(
		session,
		[=, done = std::move(done)] {
			// 缓存停用可能发生在会话析构中，延后清理以验证会话仍然存在。
			crl::on_main(session, [=] {
				auto &owner = session->data();
				const auto dialog = owner.peer(peer);
				const auto userId = session->userId().bare & PeerId::kChatTypeMask;
				const auto dialogId = getDialogIdFromPeer(dialog);
				auto items = std::vector<not_null<HistoryItem*>>();
				items.reserve(ids.size());
				for (const auto id : ids) {
					Database::removeDeletedMessage(userId, dialogId, id.v);
					if (const auto item = owner.message(peer, MsgId(id.v))) {
						if (item->isEphemeral() || session->ephemeralMessages().anchored(item)) {
							ExtrasMessages::removeDeletedMessage(item);
						}
						items.push_back(item);
					}
				}
				Tgw::ClearMessagesStorage(session, peer, ids);
				owner.destroyMessagesWithCacheCleanup(items);
				owner.sendHistoryChangeNotifications();
				session->conversationCache().flush();
				if (done) {
					done();
				}
			});
		}));
}

} // namespace Extras::DeleteMessages
