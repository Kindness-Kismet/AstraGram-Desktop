#include "extras/features/secret_chats/secret_chat_internal.h"

#include <QtCore/QtEndian>

namespace Extras::SecretChats::Details {
namespace {

void resend(ChatState &chat, Incoming &message) {
	const auto &packet = message.packet;
	const auto parity = chat.view.creator ? 1 : 0;
	if (packet.value < parity || packet.endSeq < packet.value
		|| packet.value % 2 != parity || packet.endSeq % 2 != parity) {
		discardState(chat, u"Invalid secret resend range"_q);
		return;
	}
	const auto start = (packet.value - parity) / 2;
	const auto end = (packet.endSeq - parity) / 2;
	const auto ack = (packet.inSeq - parity) / 2;
	if (end >= chat.submittedOut || start < ack) {
		discardState(chat, u"Secret resend requested a sequence outside the send queue"_q);
		return;
	}
	for (auto seq = start; seq <= end; ++seq) {
		const auto operation = chat.outgoing.find(seq);
		if (operation == chat.outgoing.end() || operation->ciphertext.isEmpty()) {
			discardState(chat, u"Requested secret message is no longer available"_q);
			return;
		}
		if (!chat.resendQueue.contains(seq)) {
			operation->transportId = randomId();
			operation->failed = false;
			chat.resendQueue.push_back(seq);
		}
	}
	message.resendHandled = true;
}

void applyMessage(Snapshot &state, ChatState &chat, const Incoming &message, qint64 now) {
	const auto &packet = message.packet;
	const auto ack = (packet.inSeq - (chat.view.creator ? 1 : 0)) / 2;
	if (ack < chat.peerAck) {
		discardState(chat, u"Secret peer acknowledgement moved backwards"_q);
		return;
	}
	chat.peerAck = ack;
	clearConfirmed(chat);
	if (Crypto::fingerprint(chat.key) == message.keyFingerprint) {
		++chat.keyUsedIn;
		if (!chat.oldKey.isEmpty()) chat.oldSeenNew = true;
	}
	chat.peerLayer = std::max(chat.peerLayer, packet.layer);
	if (packet.action == Codec::Action::None) {
		if (chat.deleted.contains(packet.randomId) || chat.messages.contains(packet.randomId)) return;
		auto stored = StoredMessage();
		stored.packet = packet;
		stored.file = message.file;
		stored.view.id = packet.randomId;
		stored.view.date = message.date;
		stored.serverDate = message.date;
		stored.view.text = packet.document ? packet.caption : packet.text;
		stored.view.delivery = Delivery::Sent;
		stored.view.ttl = packet.ttl;
		stored.view.unsupported = packet.unsupported;
		if (packet.document && !packet.unsupported) {
			const auto file = deserializeTl<MTPEncryptedFile>(message.file);
			stored.fileFingerprint = Crypto::fileFingerprint(packet.fileKey, packet.fileIv);
			if (!file || file->type() != mtpc_encryptedFile
				|| file->c_encryptedFile().vkey_fingerprint().v != stored.fileFingerprint
				|| file->c_encryptedFile().vsize().v != ((packet.fileSize + 15) / 16) * 16) {
				discardState(chat, u"Secret document metadata does not match its file"_q);
				return;
			}
			stored.view.document = Document{ packet.fileName, packet.mimeType, packet.fileSize };
		}
		insertMessage(chat, std::move(stored));
		return;
	}
	switch (packet.action) {
	case Codec::Action::NotifyLayer:
		if (packet.value < 46) {
			discardState(chat, u"Secret peer announced an unsupported legacy layer"_q);
			return;
		}
		chat.peerLayer = std::max(chat.peerLayer, packet.value);
		chat.view.layer = chat.peerLayer >= 144 ? 144 : chat.peerLayer >= 101 ? 101 : 73;
		break;
	case Codec::Action::SetTtl:
		if (packet.value < 0 || packet.value == std::numeric_limits<int>::max()) {
			discardState(chat, u"Secret peer sent an invalid message timer"_q);
			return;
		}
		chat.view.ttl = packet.value;
		break;
	case Codec::Action::Read:
		for (const auto id : packet.ids) {
			const auto m = chat.messages.find(id);
			if (m != chat.messages.end() && m->view.outgoing) startTtl(*m, message.date);
		}
		break;
	case Codec::Action::Delete: {
		const auto error = eraseMessages(chat, packet.ids);
		if (!error.isEmpty()) discardState(chat, error);
	} break;
	case Codec::Action::Flush: {
		const auto error = clearMessages(chat);
		if (!error.isEmpty()) discardState(chat, error);
	} break;
	case Codec::Action::Screenshot: {
		if (chat.deleted.contains(packet.randomId) || chat.messages.contains(packet.randomId)) break;
		auto stored = StoredMessage();
		stored.packet = packet;
		stored.view.id = packet.randomId;
		stored.view.date = message.date;
		stored.view.delivery = Delivery::Sent;
		stored.view.screenshot = true;
		insertMessage(chat, std::move(stored));
	} break;
	case Codec::Action::RequestKey:
	case Codec::Action::AcceptKey:
	case Codec::Action::CommitKey:
	case Codec::Action::AbortKey:
		applyKeyAction(state, chat, message, now);
		break;
	case Codec::Action::Resend:
		Expects(message.resendHandled);
		break;
	case Codec::Action::Noop:
	case Codec::Action::Typing:
		break;
	case Codec::Action::None:
		Unexpected("Secret message action");
	}
}

void requestGap(ChatState &chat) {
	if (chat.incoming.isEmpty() || chat.incoming.firstKey() <= chat.nextIn
		|| chat.gapEnd >= chat.nextIn) return;
	const auto end = chat.incoming.firstKey() - 1;
	auto packet = Codec::Packet();
	packet.action = Codec::Action::Resend;
	packet.value = chat.nextIn * 2 + (chat.view.creator ? 0 : 1);
	packet.endSeq = end * 2 + (chat.view.creator ? 0 : 1);
	const auto error = addService(chat, std::move(packet));
	if (!error.isEmpty()) {
		discardState(chat, error);
		return;
	}
	chat.gapEnd = end;
}

bool receive(Snapshot &state, const MTPEncryptedMessage &value, qint64 now) {
	auto chatId = 0;
	auto date = 0;
	auto ciphertext = QByteArray();
	auto file = QByteArray();
	value.match([&](const MTPDencryptedMessage &data) {
		chatId = data.vchat_id().v;
		date = data.vdate().v;
		ciphertext = data.vbytes().v;
		file = serializeTl(data.vfile());
	}, [&](const MTPDencryptedMessageService &data) {
		chatId = data.vchat_id().v;
		date = data.vdate().v;
		ciphertext = data.vbytes().v;
	});
	auto i = state.chats.find(chatId);
	if (i == state.chats.end() || i->view.state == State::Waiting || i->view.state == State::Requested) return false;
	auto &chat = *i;
	if (chat.view.state == State::Discarded) return true;
	if (ciphertext.size() < 24) {
		discardState(chat, u"Truncated secret encrypted message"_q);
		return true;
	}
	const auto fingerprint = qFromLittleEndian<qint64>(ciphertext.constData());
	const auto key = keyFor(chat, fingerprint);
	if (key.isEmpty()) {
		chat.view.error = u"Secret message references an unavailable key"_q;
		return true;
	}
	const auto body = Crypto::open(key, !chat.view.creator, ciphertext);
	const auto packet = body ? Codec::decode(*body) : std::nullopt;
	if (!packet) {
		discardState(chat, u"Secret message authentication or decoding failed"_q);
		return true;
	}
	const auto incomingParity = chat.view.creator ? 0 : 1;
	const auto ackParity = chat.view.creator ? 1 : 0;
	if (packet->layer < 46 || packet->layer > 144 || packet->outSeq < incomingParity
		|| packet->inSeq < ackParity || packet->outSeq % 2 != incomingParity
		|| packet->inSeq % 2 != ackParity
		|| (packet->inSeq - ackParity) / 2 > chat.submittedOut) {
		discardState(chat, u"Invalid secret message sequence or layer"_q);
		return true;
	}
	const auto seq = (packet->outSeq - incomingParity) / 2;
	if (seq < chat.nextIn || chat.incoming.contains(seq)) return true;
	if (chat.exchangeStage == 2 && Crypto::fingerprint(chat.candidateKey) == fingerprint) {
		switchKey(chat, now);
		chat.oldSeenNew = true;
		auto response = Codec::Packet();
		response.action = Codec::Action::Noop;
		const auto error = addService(chat, std::move(response));
		if (!error.isEmpty()) {
			discardState(chat, error);
			return true;
		}
	}
	auto message = Incoming{ *packet, date, file, fingerprint };
	if (packet->action == Codec::Action::Resend) {
		resend(chat, message);
		if (chat.view.state == State::Discarded) return true;
	}
	chat.incoming.insert(seq, std::move(message));
	while (chat.view.state == State::Active && chat.incoming.contains(chat.nextIn)) {
		const auto message = chat.incoming.take(chat.nextIn);
		applyMessage(state, chat, message, now);
		++chat.nextIn;
	}
	if (chat.view.state == State::Active) {
		requestGap(chat);
		retireOldKey(chat);
		maybeRekey(state, chat, now);
	}
	return true;
}

} // namespace

void appendEncrypted(Snapshot &state, const MTPEncryptedMessage &message) {
	const auto bytes = serializeTl(message);
	if (!state.inbox.contains(bytes)) state.inbox.push_back(bytes);
}

void drainInbox(Snapshot &state, qint64 now) {
	const auto pending = std::exchange(state.inbox, {});
	for (const auto &bytes : pending) {
		const auto message = deserializeTl<MTPEncryptedMessage>(bytes);
		if (message && !receive(state, *message, now)) state.inbox.push_back(bytes);
	}
}

void drainQts(Snapshot &state) {
	while (state.pendingQts.contains(state.qts + 1)) {
		const auto bytes = state.pendingQts.take(++state.qts);
		const auto message = deserializeTl<MTPEncryptedMessage>(bytes);
		if (message) appendEncrypted(state, *message);
	}
}

void applyUpdate(Snapshot &state, const MTPUpdate &update, qint64 now, int readBoundary) {
	switch (update.type()) {
	case mtpc_updateEncryption:
		applyEncryption(state, update.c_updateEncryption().vchat(), now);
		break;
	case mtpc_updateNewEncryptedMessage: {
		const auto &data = update.c_updateNewEncryptedMessage();
		const auto qts = data.vqts().v;
		if (state.baseline && qts <= state.qts) break;
		if (!state.baseline) {
			appendEncrypted(state, data.vmessage());
			state.pendingQts.insert(qts, serializeTl(data.vmessage()));
			break;
		}
		state.pendingQts.insert(qts, serializeTl(data.vmessage()));
		drainQts(state);
	} break;
	case mtpc_updateEncryptedMessagesRead: {
		const auto &data = update.c_updateEncryptedMessagesRead();
		const auto i = state.chats.find(data.vchat_id().v);
		if (i == state.chats.end()) break;
		const auto receipt = ReadReceipt{ data.vmax_date().v, data.vdate().v,
			readBoundary >= 0 ? readBoundary : i->submittedOut };
		auto known = false;
		for (const auto &previous : i->peerReads) {
			if (previous.maxDate == receipt.maxDate && previous.date == receipt.date) {
				known = true;
				break;
			}
		}
		if (!known) i->peerReads.push_back(receipt);
		for (auto &message : i->messages) {
			applyPeerReads(*i, message);
		}
	} break;
	case mtpc_updateEncryptedChatTyping:
		break;
	default:
		break;
	}
}

} // namespace Extras::SecretChats::Details

namespace Extras::SecretChats {

void Manager::ensureBaseline(int qts, Fn<void(bool)> done, std::optional<RecoveryState> recovery) {
	const auto now = _private->now();
	_private->enqueue([=](Private::Snapshot &snapshot) {
		if (snapshot.baseline) return QString();
		if (qts < 0) return u"Invalid secret receive baseline"_q;
		snapshot.baseline = true;
		snapshot.qts = qts;
		if (recovery) {
			snapshot.hasRecovery = true;
			snapshot.recovery = *recovery;
		}
		for (auto i = snapshot.pendingQts.begin(); i != snapshot.pendingQts.end();) {
			if (i.key() <= qts) {
				i = snapshot.pendingQts.erase(i);
			} else {
				++i;
			}
		}
		Details::drainQts(snapshot);
		Details::drainInbox(snapshot, now);
		return QString();
	}, [done = std::move(done)](QString error) { done(error.isEmpty()); });
}

void Manager::persistDifference(QVector<MTPEncryptedMessage> messages,
		QVector<MTPUpdate> updates, int qts, Fn<void(bool)> done, std::optional<RecoveryState> recovery) {
	const auto now = _private->now();
	auto readBoundaries = QMap<int, int>();
	for (const auto &update : updates) {
		if (update.type() != mtpc_updateEncryptedMessagesRead) continue;
		const auto id = update.c_updateEncryptedMessagesRead().vchat_id().v;
		const auto chat = _private->state->chats.constFind(id);
		readBoundaries[id] = chat == _private->state->chats.cend() ? 0 : chat->submittedOut;
	}
	_private->enqueue([=](Private::Snapshot &snapshot) {
		if (!snapshot.baseline || qts < 0) return u"Secret receive baseline is unavailable"_q;
		for (const auto &update : updates) {
			const auto boundary = update.type() == mtpc_updateEncryptedMessagesRead
				? readBoundaries.value(update.c_updateEncryptedMessagesRead().vchat_id().v) : -1;
			Details::applyUpdate(snapshot, update, now, boundary);
		}
		for (const auto &message : messages) Details::appendEncrypted(snapshot, message);
		snapshot.qts = std::max(snapshot.qts, qts);
		for (auto i = snapshot.pendingQts.begin(); i != snapshot.pendingQts.end();) {
			if (i.key() <= snapshot.qts) {
				const auto message = Details::deserializeTl<MTPEncryptedMessage>(i.value());
				if (message) Details::appendEncrypted(snapshot, *message);
				i = snapshot.pendingQts.erase(i);
			} else {
				++i;
			}
		}
		Details::drainQts(snapshot);
		Details::drainInbox(snapshot, now);
		Details::expire(snapshot, now);
		if (recovery) {
			snapshot.hasRecovery = true;
			snapshot.recovery = *recovery;
		}
		return QString();
	}, [done = std::move(done)](QString error) { done(error.isEmpty()); });
}

void Manager::update(const MTPUpdate &update) {
	const auto now = _private->now();
	auto readBoundary = -1;
	if (update.type() == mtpc_updateEncryptedMessagesRead) {
		const auto id = update.c_updateEncryptedMessagesRead().vchat_id().v;
		const auto chat = _private->state->chats.constFind(id);
		readBoundary = chat == _private->state->chats.cend() ? 0 : chat->submittedOut;
	}
	_private->enqueue([=](Private::Snapshot &snapshot) {
		Details::applyUpdate(snapshot, update, now, readBoundary);
		Details::drainInbox(snapshot, now);
		Details::expire(snapshot, now);
		return QString();
	});
}

void Manager::Private::acknowledge() {
	if (ackRequest || state->qts <= acknowledgedQts) return;
	const auto qts = state->qts;
	const auto weak = base::make_weak(owner.get());
	ackRequest = owner->request(MTPmessages_ReceivedQueue(MTP_int(qts)))
		.done([weak, qts] {
			if (const auto manager = weak.get()) {
				manager->_private->ackRequest = 0;
				manager->_private->acknowledgedQts = qts;
				manager->_private->acknowledge();
			}
		}).fail([weak] { if (weak) weak.get()->_private->ackRequest = 0; }).send();
}

} // namespace Extras::SecretChats
