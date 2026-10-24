#include "extras/features/secret_chats/secret_chat_dialog.h"

#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "extras/features/secret_chats/secret_chat_peer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/history_unread_things.h"
#include "base/random.h"
#include "core/application.h"
#include "dialogs/ui/dialogs_layout.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "styles/style_dialogs.h"

namespace Extras::SecretChats {

Dialog::Dialog(not_null<Main::Session*> session, Chat chat)
: Thread(&session->data(), Type::SecretChat)
, _user(session->data().user(chat.userId))
, _history(session->data().history(SecretChatId(chat.id)))
, _chat(std::move(chat)) {
	static_cast<Peer*>(_history->peer.get())->setUser(_user);
	_history->clearFolder();
	_nameSortKey = TextUtilities::NameSortKey(_user->name());
	session->changes().peerUpdates(Data::PeerUpdate::Flag::Name
		| Data::PeerUpdate::Flag::Photo) | rpl::filter([=](const Data::PeerUpdate &update) {
		return update.peer == _user;
	}) | rpl::on_next([=] {
		static_cast<Peer*>(_history->peer.get())->setUser(_user);
		_nameSortKey = TextUtilities::NameSortKey(_user->name());
		updateChatListEntry();
	}, _lifetime);
}

Dialog::~Dialog() {
	Core::App().closeChatFromWindows(_history->peer);
	owner().removeChatListEntry(this);
	session().changes().entryUpdated(this, Data::EntryUpdate::Flag::Destroyed);
	for (auto &[randomId, rendered] : _rendered) {
		if (rendered.document) {
			rendered.document->clearLocalSource();
		}
		if (const auto item = owner().message(_history->peer->id, rendered.id)) {
			item->destroy();
		}
	}
}

void Dialog::refresh(const Chat &chat, const QVector<Message> &messages) {
	const auto wasUnread = chatListUnreadState();
	_chat = chat;
	_hasMessages = !messages.isEmpty();
	for (const auto &message : messages) {
		(void)ensureLocalId(message.id);
	}
	_messageIds.clear();
	for (const auto &message : messages) {
		auto &rendered = _rendered[message.id];
		auto item = owner().message(_history->peer->id, rendered.id);
		const auto text = message.unsupported ? tr::extras_SecretChatUnsupported(tr::now)
			: message.screenshot ? tr::extras_SecretChatScreenshotNotice(tr::now) : message.text;
		if (!item) {
			auto fields = HistoryItemCommonFields{
				.id = rendered.id,
				.flags = MessageFlag::Local | MessageFlag::HistoryEntry | MessageFlag::HasFromId
					| (message.outgoing ? MessageFlag::Outgoing : MessageFlag()),
				.from = message.outgoing ? session().userId() : _user->id,
				.date = message.date,
			};
			if (message.replyTo && localId(message.replyTo)) {
				fields.flags |= MessageFlag::HasReplyInfo;
				fields.replyTo.messageId = FullMsgId(_history->peer->id, localId(message.replyTo));
			}
			if (message.screenshot) {
				item = _history->makeMessage(std::move(fields), PreparedServiceText{ TextWithEntities{ text } });
			} else if (message.document) {
				const auto document = owner().document(base::RandomValue<DocumentId>());
				document->size = message.document->size;
				document->date = message.date;
				document->setMimeString(message.document->mimeType);
				if (message.ttl) { document->setForbidsFileSave(); }
				document->setattributes({ MTP_documentAttributeFilename(MTP_string(message.document->fileName)) });
				const auto weak = base::make_weak(&session().secretChats());
				const auto chatId = _chat.id;
				const auto randomId = message.id;
				document->setLocalSource({
					.download = [=](Fn<void(QByteArray, QString)> done, Fn<void(qint64)> progress) {
						if (const auto manager = weak.get()) {
							manager->downloadDocument(chatId, randomId, [=](QByteArray bytes, QString error) {
								if (error.isEmpty()) { progress(bytes.size()); }
								done(std::move(bytes), std::move(error));
							}, progress);
						}
					},
					.cancel = [=] {
						if (const auto manager = weak.get()) { manager->cancelDocumentDownload(chatId, randomId); }
					},
					.valid = [=] {
						if (!weak || !weak->available()) { return false; }
						const auto messages = weak->messages(chatId);
						return ranges::any_of(messages, [=](const Message &message) {
							return message.id == randomId && (!message.expiresAt || message.expiresAt > weak->currentTime());
						});
					},
				});
				rendered.document = document;
				rendered.media = document->createMediaView();
				item = _history->makeMessage(std::move(fields), document, TextWithEntities{ text });
			} else {
				item = _history->makeMessage(std::move(fields), TextWithEntities{ text }, MTP_messageMediaEmpty());
			}
		}
		rendered.message = message;
		if (rendered.document && message.outgoing) {
			if (message.delivery == Delivery::Failed) {
				rendered.document->status = FileUploadFailed;
			} else if (rendered.document->status == FileUploadFailed) {
				rendered.document->status = FileReady;
			}
		}
		updateDocumentUploadProgress(message.id, message.uploadOffset);
		item->updateLocalState(message.date,
			message.delivery == Delivery::Pending,
			message.delivery == Delivery::Failed,
			message.delivery != Delivery::Read, message.ttl);
		_messageIds.push_back(item->fullId());
	}
	auto removed = std::vector<not_null<HistoryItem*>>();
	for (const auto &[randomId, rendered] : _rendered) {
		if (!ranges::any_of(messages, [=](const Message &message) { return message.id == randomId; })) {
			if (const auto item = owner().message(_history->peer->id, rendered.id)) {
				removed.push_back(item);
			}
		}
	}
	if (!removed.empty()) {
		owner().notifyItemsAboutToBeDestroyed(removed);
	}
	for (auto i = _rendered.begin(); i != _rendered.end();) {
		if (ranges::any_of(messages, [&](const Message &message) { return message.id == i->first; })) {
			++i;
			continue;
		}
		if (i->second.document) { i->second.document->clearLocalSource(); }
		if (const auto item = owner().message(_history->peer->id, i->second.id)) { item->destroy(); }
		_randomIds.erase(i->second.id);
		i = _rendered.erase(i);
	}
	_unreadCount = ranges::count_if(messages, [](const Message &message) {
		return !message.outgoing && message.delivery != Delivery::Read;
	});
	const auto text = [&] {
		if (!messages.isEmpty()) {
			const auto &last = messages.back();
			if (last.unsupported) {
				return tr::extras_SecretChatUnsupported(tr::now);
			}
			if (last.screenshot) {
				return tr::extras_SecretChatScreenshotNotice(tr::now);
			}
			if (!last.text.isEmpty()) {
				return last.text;
			}
			if (last.document) {
				return last.document->fileName;
			}
		}
		return (_chat.state == State::Active)
			? tr::extras_SecretChatReady(tr::now)
			: (_chat.state == State::Discarded)
			? tr::extras_SecretChatEnded(tr::now)
			: tr::extras_SecretChatPending(tr::now);
	}();
	if (text != _previewText) {
		_previewText = text;
		_preview.setText(st::dialogsTextStyle, text, kPlainTextOptions);
	}
	if (inChatList()) {
		notifyUnreadStateChange(wasUnread);
	}
	setChatListTimeId(messages.isEmpty()
		? (chatListTimeId() ? chatListTimeId() : session().secretChats().currentTime())
		: messages.back().date);
	updateChatListExistence();
	updateChatListEntry();
	_historyChanges.fire({});
}

not_null<History*> Dialog::owningHistory() { return _history; }

void Dialog::updateDocumentUploadProgress(qint64 messageId, std::optional<qint64> offset) {
	const auto i = _rendered.find(messageId);
	if (i == _rendered.end() || !i->second.document) {
		return;
	}
	const auto document = i->second.document;
	if (!offset) {
		if (!document->uploadingData) { return; }
		document->uploadingData = nullptr;
	} else {
		if (!document->uploadingData) {
			document->uploadingData = std::make_unique<Data::UploadState>(document->size);
		}
		document->uploadingData->offset = std::min(*offset, qint64(document->size));
	}
	owner().notifyDocumentLayoutChanged(document);
}

Data::MessagesSlice Dialog::slice(QString query) const {
	auto result = Data::MessagesSlice{ .skippedBefore = 0, .skippedAfter = 0 };
	for (const auto &id : _messageIds) {
		const auto message = this->message(id.msg);
		if (!query.isEmpty() && message
			&& !message->text.contains(query, Qt::CaseInsensitive)
			&& (!message->document || !message->document->fileName.contains(query, Qt::CaseInsensitive))) {
			continue;
		}
		result.ids.push_back(id);
	}
	result.fullCount = result.ids.size();
	return result;
}

rpl::producer<> Dialog::historyChanged() const { return _historyChanges.events(); }
qint64 Dialog::randomId(MsgId id) const {
	const auto i = _randomIds.find(id);
	return i != _randomIds.end() ? i->second : 0;
}
MsgId Dialog::localId(qint64 randomId) const {
	const auto i = _rendered.find(randomId);
	return i != _rendered.end() ? i->second.id : MsgId();
}
MsgId Dialog::ensureLocalId(qint64 randomId) {
	Expects(randomId != 0);
	if (const auto id = localId(randomId)) {
		return id;
	}
	const auto id = owner().nextLocalMessageId();
	_rendered.emplace(randomId, Rendered{ .id = id });
	_randomIds.emplace(id, randomId);
	return id;
}
std::optional<Message> Dialog::message(MsgId id) const {
	const auto i = _rendered.find(randomId(id));
	return i != _rendered.end() ? std::make_optional(i->second.message) : std::nullopt;
}
bool Dialog::isServerSideUnread(not_null<const HistoryItem*> item) const {
	const auto message = this->message(item->id);
	return message && message->delivery != Delivery::Read;
}

bool Dialog::shouldBeInChatList() const {
	return session().secretChats().available()
		&& (_chat.state != State::Discarded || _hasMessages);
}

HistoryItem *Dialog::chatListMessage() const {
	return _messageIds.empty() ? nullptr : owner().message(_messageIds.back());
}

Dialogs::UnreadState Dialog::chatListUnreadState() const {
	return { .messages = _unreadCount, .chats = int(_unreadCount > 0), .known = true };
}

Dialogs::BadgesState Dialog::chatListBadgesState() const {
	return Dialogs::BadgesForUnread(chatListUnreadState());
}

const QString &Dialog::chatListName() const { return _user->name(); }
const QString &Dialog::chatListNameSortKey() const { return _nameSortKey; }
int Dialog::chatListNameVersion() const { return _user->nameVersion(); }
const base::flat_set<QString> &Dialog::chatListNameWords() const { return _user->nameWords(); }
const base::flat_set<QChar> &Dialog::chatListFirstLetters() const { return _user->nameFirstLetters(); }

void Dialog::chatListPreloadData() {
	_user->loadUserpic();
}

void Dialog::paintUserpic(
		Painter &p,
		Ui::PeerUserpicView &view,
		const Dialogs::Ui::PaintContext &context) const {
	_user->paintUserpicLeft(p, view, context.st->padding.left(),
		context.st->padding.top(), context.width, context.st->photoSize);
}

} // namespace Extras::SecretChats
