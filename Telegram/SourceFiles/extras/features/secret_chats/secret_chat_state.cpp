#include "extras/features/secret_chats/secret_chat_internal.h"

#include "base/random.h"
#include <QtCore/QDateTime>
#include <limits>
#include <tuple>

namespace Extras::SecretChats::Details {

QDataStream &operator<<(QDataStream &s, const Crypto::DhState &v) {
	return s << v.generator << v.prime << v.power << v.publicValue;
}
QDataStream &operator>>(QDataStream &s, Crypto::DhState &v) {
	return s >> v.generator >> v.prime >> v.power >> v.publicValue;
}
QDataStream &operator<<(QDataStream &s, const Codec::Packet &v) {
	return s << v.layer << v.inSeq << v.outSeq << v.randomId << v.ttl
		<< v.text << qint32(v.action) << v.ids << v.value << v.endSeq
		<< v.exchangeId << v.publicValue << v.keyFingerprint << v.document
		<< v.mimeType << v.fileName << v.fileSize << v.fileKey << v.fileIv
		<< v.thumb << v.thumbWidth << v.thumbHeight << v.caption << v.unsupported;
}
QDataStream &operator>>(QDataStream &s, Codec::Packet &v) {
	auto action = qint32();
	s >> v.layer >> v.inSeq >> v.outSeq >> v.randomId >> v.ttl
		>> v.text >> action >> v.ids >> v.value >> v.endSeq
		>> v.exchangeId >> v.publicValue >> v.keyFingerprint >> v.document
		>> v.mimeType >> v.fileName >> v.fileSize >> v.fileKey >> v.fileIv
		>> v.thumb >> v.thumbWidth >> v.thumbHeight >> v.caption >> v.unsupported;
	if (action < 0 || action > qint32(Codec::Action::Typing)) {
		s.setStatus(QDataStream::ReadCorruptData);
	}
	v.action = Codec::Action(action);
	return s;
}
QDataStream &operator<<(QDataStream &s, const StoredMessage &v) {
	return s << v.view.id << v.view.date << v.view.outgoing << v.view.text
		<< qint32(v.view.delivery) << v.view.ttl << v.view.expiresAt
		<< v.view.unsupported << v.view.screenshot << v.packet << v.file << v.ciphertext
		<< v.startedAt << v.fileFingerprint << v.serverDate;
}
QDataStream &operator>>(QDataStream &s, StoredMessage &v) {
	auto delivery = qint32();
	s >> v.view.id >> v.view.date >> v.view.outgoing >> v.view.text
		>> delivery >> v.view.ttl >> v.view.expiresAt
		>> v.view.unsupported >> v.view.screenshot >> v.packet >> v.file >> v.ciphertext
		>> v.startedAt >> v.fileFingerprint >> v.serverDate;
	if (delivery < 0 || delivery > qint32(Delivery::Read)) {
		s.setStatus(QDataStream::ReadCorruptData);
	}
	v.view.delivery = Delivery(delivery);
	if (v.packet.document && !v.view.unsupported) {
		v.view.document = Document{ v.packet.fileName, v.packet.mimeType, v.packet.fileSize };
	}
	return s;
}
QDataStream &operator<<(QDataStream &s, const Outgoing &v) {
	return s << v.packet << v.ciphertext << v.transportId << v.keyFingerprint
		<< v.fileCiphertext << v.inputFile << v.sentFile << v.sent << v.failed << v.fileFingerprint;
}
QDataStream &operator>>(QDataStream &s, Outgoing &v) {
	return s >> v.packet >> v.ciphertext >> v.transportId >> v.keyFingerprint
		>> v.fileCiphertext >> v.inputFile >> v.sentFile >> v.sent >> v.failed >> v.fileFingerprint;
}
QDataStream &operator<<(QDataStream &s, const Incoming &v) {
	return s << v.packet << v.date << v.file << v.keyFingerprint << v.resendHandled;
}
QDataStream &operator>>(QDataStream &s, Incoming &v) {
	return s >> v.packet >> v.date >> v.file >> v.keyFingerprint >> v.resendHandled;
}
QDataStream &operator<<(QDataStream &s, const ReadReceipt &v) {
	return s << v.maxDate << v.date << v.submittedOut;
}
QDataStream &operator>>(QDataStream &s, ReadReceipt &v) {
	return s >> v.maxDate >> v.date >> v.submittedOut;
}
QDataStream &operator<<(QDataStream &s, const ChatState &v) {
	return s << v.view.id << quint64(v.view.userId.bare) << qint32(v.view.state)
		<< v.view.creator << v.view.ttl << v.view.layer << v.view.verification << v.view.error
		<< v.accessHash << v.dh << v.initialGenerator << v.initialPrime << v.peerPublic
		<< v.key << v.keyFingerprint << v.initialKeyFingerprint << v.oldKey << v.candidateKey
		<< v.exchangeDh << v.exchangeId << v.exchangeStage << v.completedExchangeId
		<< v.completedExchangeFingerprint << v.oldUntilOut << v.oldSeenNew
		<< v.keyCreatedAt << v.keyUsedOut << v.keyUsedIn << v.nextOut << v.submittedOut << v.nextIn
		<< v.peerAck << v.gapEnd << v.peerLayer << v.notified << v.acceptPending
		<< v.activationPending << v.pendingFingerprint
		<< v.discardPending << v.discardHistory << v.pendingReadDate << v.readDate << v.readAt
		<< v.peerReads
		<< v.outgoing << v.incoming << v.messages << v.messageOrder << v.deleted << v.resendQueue;
}
QDataStream &operator>>(QDataStream &s, ChatState &v) {
	auto userId = quint64();
	auto state = qint32();
	s >> v.view.id >> userId >> state
		>> v.view.creator >> v.view.ttl >> v.view.layer >> v.view.verification >> v.view.error
		>> v.accessHash >> v.dh >> v.initialGenerator >> v.initialPrime >> v.peerPublic
		>> v.key >> v.keyFingerprint >> v.initialKeyFingerprint >> v.oldKey >> v.candidateKey
		>> v.exchangeDh >> v.exchangeId >> v.exchangeStage >> v.completedExchangeId
		>> v.completedExchangeFingerprint >> v.oldUntilOut >> v.oldSeenNew
		>> v.keyCreatedAt >> v.keyUsedOut >> v.keyUsedIn >> v.nextOut >> v.submittedOut >> v.nextIn
		>> v.peerAck >> v.gapEnd >> v.peerLayer >> v.notified >> v.acceptPending
		>> v.activationPending >> v.pendingFingerprint
		>> v.discardPending >> v.discardHistory >> v.pendingReadDate >> v.readDate >> v.readAt
		>> v.peerReads
		>> v.outgoing >> v.incoming >> v.messages >> v.messageOrder >> v.deleted >> v.resendQueue;
	if (state < 0 || state > qint32(State::Discarded)) {
		s.setStatus(QDataStream::ReadCorruptData);
	}
	v.view.state = State(state);
	v.view.userId = UserId(userId);
	return s;
}
QDataStream &operator<<(QDataStream &s, const Creation &v) {
	return s << v.userId << v.dh << v.inputUser;
}
QDataStream &operator>>(QDataStream &s, Creation &v) {
	return s >> v.userId >> v.dh >> v.inputUser;
}

QByteArray serialize(const Snapshot &state) {
	auto bytes = QByteArray();
	QDataStream s(&bytes, QIODevice::WriteOnly);
	s.setVersion(QDataStream::Qt_5_1);
	s << quint32(12) << state.selfId << state.baseline << state.qts
		<< state.hasRecovery << state.recovery.pts << state.recovery.date << state.recovery.seq
		<< state.dhGenerator << state.dhVersion << state.dhPrime
		<< state.chats << state.creations << state.pendingQts << state.inbox;
	return s.status() == QDataStream::Ok ? bytes : QByteArray();
}

std::optional<Snapshot> deserialize(const QByteArray &bytes) {
	auto result = Snapshot();
	auto version = quint32();
	QDataStream s(bytes);
	s.setVersion(QDataStream::Qt_5_1);
	s >> version;
	if (version != 12) {
		return std::nullopt;
	}
	s >> result.selfId >> result.baseline >> result.qts
		>> result.hasRecovery >> result.recovery.pts >> result.recovery.date >> result.recovery.seq
		>> result.dhGenerator >> result.dhVersion >> result.dhPrime
		>> result.chats >> result.creations >> result.pendingQts >> result.inbox;
	return s.status() == QDataStream::Ok && s.atEnd()
		? std::make_optional(std::move(result)) : std::nullopt;
}

qint64 randomId() {
	auto result = qint64();
	while (!result) {
		result = base::RandomValue<qint64>();
	}
	return result;
}

QByteArray randomBytes(int size) {
	auto result = QByteArray(size, '\0');
	base::RandomFill(result.data(), result.size());
	return result;
}

QByteArray keyFor(const ChatState &chat, qint64 fingerprint) {
	for (const auto &key : { chat.key, chat.oldKey, chat.candidateKey }) {
		if (key.size() == 256 && Crypto::fingerprint(key) == fingerprint) {
			return key;
		}
	}
	return {};
}

QString addOutgoing(ChatState &chat, Codec::Packet packet, QByteArray fileCiphertext) {
	if (chat.view.state != State::Active || chat.key.size() != 256) {
		return u"Secret chat is not active"_q;
	}
	if (chat.nextOut >= std::numeric_limits<int>::max() / 2
		|| chat.nextIn >= std::numeric_limits<int>::max() / 2) {
		return u"Secret chat sequence limit reached"_q;
	}
	packet.randomId = packet.randomId ? packet.randomId : randomId();
	packet.layer = chat.view.layer;
	packet.inSeq = chat.nextIn * 2 + (chat.view.creator ? 0 : 1);
	packet.outSeq = chat.nextOut * 2 + (chat.view.creator ? 1 : 0);
	if (packet.action == Codec::Action::None) {
		packet.ttl = chat.view.ttl;
	}
	const auto body = Codec::encode(packet);
	const auto ciphertext = body ? Crypto::seal(chat.key, chat.view.creator, *body) : std::nullopt;
	if (!ciphertext) {
		return u"Secret message encryption failed"_q;
	}
	auto operation = Outgoing();
	operation.packet = packet;
	operation.ciphertext = *ciphertext;
	operation.transportId = packet.randomId;
	operation.keyFingerprint = Crypto::fingerprint(chat.key);
	operation.fileCiphertext = std::move(fileCiphertext);
	if (packet.document) operation.fileFingerprint = Crypto::fileFingerprint(packet.fileKey, packet.fileIv);
	chat.outgoing.insert(chat.nextOut++, std::move(operation));
	++chat.keyUsedOut;
	if (packet.action != Codec::Action::None) {
		return {};
	}
	auto message = StoredMessage();
	message.packet = packet;
	message.view.id = packet.randomId;
	message.view.date = int(QDateTime::currentSecsSinceEpoch());
	message.view.outgoing = true;
	message.view.text = packet.document ? packet.caption : packet.text;
	message.view.ttl = packet.ttl;
	if (packet.document) {
		message.view.document = Document{ packet.fileName, packet.mimeType, packet.fileSize };
		message.fileFingerprint = Crypto::fileFingerprint(packet.fileKey, packet.fileIv);
	}
	insertMessage(chat, std::move(message));
	return {};
}

void insertMessage(ChatState &chat, StoredMessage message) {
	const auto id = message.view.id;
	chat.messages.insert(id, std::move(message));
	chat.messageOrder.push_back(id);
	const auto localParity = chat.view.creator ? 1 : 0;
	const auto peerParity = chat.view.creator ? 0 : 1;
	const auto order = [&](qint64 id) {
		const auto &message = chat.messages[id];
		const auto &packet = message.packet;
		return message.view.outgoing
			? std::tuple((packet.outSeq - localParity) / 2, 1, 0)
			: std::tuple((packet.inSeq - localParity) / 2, 0, (packet.outSeq - peerParity) / 2);
	};
	// 对端确认位置决定插入点，服务动作占用的序号不能从可见历史推算。
	std::sort(chat.messageOrder.begin(), chat.messageOrder.end(),
		[&](qint64 a, qint64 b) { return order(a) < order(b); });
}

QString addService(ChatState &chat, Codec::Packet packet) {
	Expects(packet.action != Codec::Action::None);
	return addOutgoing(chat, std::move(packet));
}

QString eraseMessages(ChatState &chat, const QVector<qint64> &ids) {
	for (const auto id : ids) {
		chat.deleted.insert(id);
		chat.messages.remove(id);
		chat.messageOrder.removeAll(id);
		for (auto &operation : chat.outgoing) {
			if (operation.packet.randomId != id || operation.ciphertext.isEmpty()) {
				continue;
			}
			const auto key = keyFor(chat, operation.keyFingerprint);
			auto packet = Codec::Packet();
			packet.layer = operation.packet.layer;
			packet.inSeq = operation.packet.inSeq;
			packet.outSeq = operation.packet.outSeq;
			packet.randomId = id;
			packet.action = Codec::Action::Delete;
			packet.ids = { id };
			const auto body = Codec::encode(packet);
			const auto ciphertext = body ? Crypto::seal(key, chat.view.creator, *body) : std::nullopt;
			if (!ciphertext) {
				return u"Could not replace deleted secret operation"_q;
			}
			operation.packet = std::move(packet);
			operation.ciphertext = *ciphertext;
			operation.transportId = randomId();
			operation.fileCiphertext.clear();
			operation.inputFile.clear();
			operation.sentFile.clear();
			operation.failed = false;
			// 已确认的包仅在对方请求补缺时发送无正文占位。
		}
		for (auto &operation : chat.incoming) {
			if (operation.packet.randomId == id && operation.packet.action == Codec::Action::None) {
				const auto inSeq = operation.packet.inSeq;
				const auto outSeq = operation.packet.outSeq;
				const auto layer = operation.packet.layer;
				operation.packet = {};
				operation.packet.layer = layer;
				operation.packet.randomId = id;
				operation.packet.inSeq = inSeq;
				operation.packet.outSeq = outSeq;
				operation.packet.action = Codec::Action::Noop;
				operation.file.clear();
			}
		}
	}
	return {};
}

QString clearMessages(ChatState &chat, bool includeWaiting) {
	auto ids = QVector<qint64>();
	ids.reserve(chat.messages.size());
	for (auto i = chat.messages.cbegin(); i != chat.messages.cend(); ++i) {
		ids.push_back(i.key());
	}
	if (includeWaiting) {
		for (const auto &message : chat.incoming) {
			if (message.packet.action == Codec::Action::None) ids.push_back(message.packet.randomId);
		}
	}
	return eraseMessages(chat, ids);
}

void startTtl(StoredMessage &message, qint64 now) {
	message.view.delivery = Delivery::Read;
	if (message.view.ttl > 0 && !message.startedAt) {
		message.startedAt = now;
		message.view.expiresAt = now + message.view.ttl;
	}
}

void applyPeerReads(ChatState &chat, StoredMessage &message) {
	if (!message.view.outgoing || !message.serverDate) return;
	const auto seq = (message.packet.outSeq - (chat.view.creator ? 1 : 0)) / 2;
	auto readAt = std::optional<qint64>();
	for (const auto &receipt : chat.peerReads) {
		if (seq < receipt.submittedOut && message.serverDate <= receipt.maxDate) {
			readAt = readAt ? std::min(*readAt, receipt.date) : receipt.date;
		}
	}
	if (!readAt) return;
	startTtl(message, *readAt);
	// 恢复更早投递事实后，首次已读只能把已经启动的计时提前。
	if (message.view.ttl > 0 && *readAt < message.startedAt) {
		message.startedAt = *readAt;
		message.view.expiresAt = *readAt + message.view.ttl;
	}
}

void applySendResult(Snapshot &state, int chatId, int seq, qint64 transportId,
		int date, const QByteArray &file, const QString &error) {
	auto chat = state.chats.find(chatId);
	if (chat == state.chats.end()) return;
	auto operation = chat->outgoing.find(seq);
	if (operation == chat->outgoing.end()) return;
	const auto message = chat->messages.find(operation->packet.randomId);
	const auto sameMessage = message != chat->messages.end() && message->view.outgoing
		&& message->packet.outSeq == seq * 2 + (chat->view.creator ? 1 : 0);
	// 补发和过时成功回调都不能推迟首次投递日期，已读仍按首次边界重放。
	if (sameMessage && error.isEmpty() && date > 0) {
		if (!message->serverDate || date < message->serverDate) {
			message->serverDate = date;
			message->view.date = date;
		}
		applyPeerReads(*chat, *message);
	}
	if (sameMessage && error.isEmpty() && message->packet.document
		&& message->file.isEmpty() && !file.isEmpty()) {
		const auto reference = deserializeTl<MTPEncryptedFile>(file);
		if (reference && reference->type() == mtpc_encryptedFile
			&& reference->c_encryptedFile().vkey_fingerprint().v == message->fileFingerprint
			&& reference->c_encryptedFile().vsize().v == ((message->packet.fileSize + 15) / 16) * 16) {
			message->file = file;
		}
	}
	if (operation->transportId != transportId) return;
	operation->failed = !error.isEmpty();
	if (seq < chat->peerAck) operation->failed = false;
	if (error.isEmpty()) {
		operation->sent = true;
		if (!file.isEmpty()) operation->sentFile = file;
		chat->resendQueue.removeAll(seq);
	} else if (seq >= chat->peerAck) {
		chat->view.error = error;
	}
	if (sameMessage) {
		if (!date) {
			if (!operation->sent) message->view.delivery = Delivery::Failed;
		} else {
			if (message->view.delivery != Delivery::Read) message->view.delivery = Delivery::Sent;
		}
	}
	if (error == u"ENCRYPTION_DECLINED"_q && seq >= chat->peerAck) {
		discardState(*chat, error);
		// 服务器已经确认终止，无需再提交终止请求。
		chat->discardPending = false;
		return;
	}
	clearConfirmed(*chat);
	retireOldKey(*chat);
}

void clearConfirmed(ChatState &chat) {
	for (auto i = chat.outgoing.begin(); i != chat.outgoing.end() && i.key() < chat.peerAck; ++i) {
		chat.resendQueue.removeAll(i.key());
		const auto id = i->packet.randomId;
		i->packet = {};
		i->packet.randomId = id;
		i->ciphertext.clear();
		i->fileCiphertext.clear();
		i->inputFile.clear();
		i->sentFile.clear();
		i->sent = true;
		i->failed = false;
	}
}

void expire(Snapshot &state, qint64 now) {
	for (auto &chat : state.chats) {
		auto ids = QVector<qint64>();
		for (const auto &message : chat.messages) {
			if (message.view.expiresAt && message.view.expiresAt <= now) {
				ids.push_back(message.view.id);
			}
		}
		const auto error = eraseMessages(chat, ids);
		if (!error.isEmpty()) {
			discardState(chat, error);
		}
	}
}

void discardState(ChatState &chat, QString error) {
	chat.view.state = State::Discarded;
	chat.view.error = std::move(error);
	chat.discardPending = true;
	chat.acceptPending = false;
	chat.activationPending = false;
	chat.pendingFingerprint = 0;
	chat.pendingReadDate = 0;
	chat.peerReads.clear();
	chat.outgoing.clear();
	chat.incoming.clear();
	chat.resendQueue.clear();
	chat.dh = {};
	chat.initialGenerator = 0;
	chat.initialPrime.clear();
	chat.exchangeDh = {};
	chat.key.clear();
	chat.oldKey.clear();
	chat.candidateKey.clear();
	chat.peerPublic.clear();
}

} // namespace Extras::SecretChats::Details
