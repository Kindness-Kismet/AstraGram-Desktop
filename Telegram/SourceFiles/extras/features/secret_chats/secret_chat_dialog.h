#pragma once

#include "data/data_thread.h"
#include "data/data_messages.h"
#include "extras/features/secret_chats/secret_chat_manager.h"

class DocumentData;
namespace Data {
class DocumentMedia;
} // namespace Data

namespace Extras::SecretChats {

class Dialog final : public Data::Thread {
public:
	Dialog(not_null<Main::Session*> session, Chat chat);
	~Dialog();

	void refresh(const Chat &chat, const QVector<Message> &messages);
	void updateDocumentUploadProgress(qint64 messageId, std::optional<qint64> offset);
	not_null<History*> owningHistory() override;
	[[nodiscard]] Data::MessagesSlice slice(QString query = {}) const;
	[[nodiscard]] rpl::producer<> historyChanged() const;
	[[nodiscard]] qint64 randomId(MsgId id) const;
	[[nodiscard]] MsgId localId(qint64 randomId) const;
	[[nodiscard]] MsgId ensureLocalId(qint64 randomId);
	[[nodiscard]] std::optional<Message> message(MsgId id) const;
	void hasUnreadMentionChanged(bool) override {}
	void hasUnreadReactionChanged(bool) override {}
	void hasUnreadPollVoteChanged(bool) override {}
	bool isServerSideUnread(not_null<const HistoryItem*> item) const override;
	HistoryView::SendActionPainter *sendActionPainter() override { return nullptr; }
	[[nodiscard]] int chatId() const { return _chat.id; }
	[[nodiscard]] not_null<UserData*> user() const { return _user; }
	[[nodiscard]] const Ui::Text::String &preview() const { return _preview; }

	int fixedOnTopIndex() const override { return 0; }
	bool shouldBeInChatList() const override;
	Dialogs::UnreadState chatListUnreadState() const override;
	Dialogs::BadgesState chatListBadgesState() const override;
	HistoryItem *chatListMessage() const override;
	bool chatListMessageKnown() const override { return true; }
	const QString &chatListName() const override;
	const QString &chatListNameSortKey() const override;
	int chatListNameVersion() const override;
	const base::flat_set<QString> &chatListNameWords() const override;
	const base::flat_set<QChar> &chatListFirstLetters() const override;
	void chatListPreloadData() override;
	void paintUserpic(
		Painter &p,
		Ui::PeerUserpicView &view,
		const Dialogs::Ui::PaintContext &context) const override;

private:
	const not_null<UserData*> _user;
	const not_null<History*> _history;
	Chat _chat;
	Ui::Text::String _preview;
	QString _previewText;
	QString _nameSortKey;
	int _unreadCount = 0;
	bool _hasMessages = false;
	struct Rendered {
		MsgId id;
		Message message;
		DocumentData *document = nullptr;
		std::shared_ptr<Data::DocumentMedia> media;
	};
	base::flat_map<qint64, Rendered> _rendered;
	base::flat_map<MsgId, qint64> _randomIds;
	std::vector<FullMsgId> _messageIds;
	rpl::event_stream<> _historyChanges;
	rpl::lifetime _lifetime;
};

} // namespace Extras::SecretChats
