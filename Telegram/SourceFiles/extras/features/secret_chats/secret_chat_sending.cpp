#include "extras/features/secret_chats/secret_chat_internal.h"
#include "extras/features/secret_chats/secret_chat_dialog.h"

namespace Extras::SecretChats {

void Manager::send(int chatId, QString text, Fn<void(qint64, QString)> done) {
	sendReply(chatId, std::move(text), 0, std::move(done));
}

void Manager::sendReply(int chatId, QString text, qint64 replyTo,
		Fn<void(qint64, QString)> done) {
	const auto id = Details::randomId();
	_private->enqueue([=](Private::Snapshot &snapshot) {
		auto i = snapshot.chats.find(chatId);
		if (i == snapshot.chats.end() || text.isEmpty() || text.size() > 32768) {
			return u"Secret message requires a chat and 1 to 32768 text characters"_q;
		}
		const auto target = i->messages.constFind(replyTo);
		if (replyTo && (target == i->messages.cend()
			|| target->packet.action != Codec::Action::None)) {
			return u"Secret reply target was not found"_q;
		}
		auto packet = Codec::Packet();
		packet.randomId = id;
		packet.text = text;
		packet.replyTo = replyTo;
		const auto error = Details::addOutgoing(*i, std::move(packet));
		if (error.isEmpty() && snapshot.drafts.value(chatId) == Draft{ text, replyTo }) {
			snapshot.drafts.remove(chatId);
		}
		return error;
	}, [done = std::move(done), id](QString error) {
		if (done) done(error.isEmpty() ? id : 0, error);
	});
}

void Manager::sendDocument(int chatId, QString fileName, QString mimeType,
		QByteArray bytes, QString caption, Fn<void(qint64, QString)> done) {
	sendDocumentReply(chatId, std::move(fileName), std::move(mimeType),
		std::move(bytes), std::move(caption), 0, std::move(done));
}

void Manager::sendDocumentReply(int chatId, QString fileName, QString mimeType,
		QByteArray bytes, QString caption, qint64 replyTo,
		Fn<void(qint64, QString)> done) {
	const auto id = Details::randomId();
	_private->enqueue([=](Private::Snapshot &snapshot) {
		auto i = snapshot.chats.find(chatId);
		if (i == snapshot.chats.end() || bytes.isEmpty() || bytes.size() > kMaxDocumentBytes
			|| fileName.isEmpty() || mimeType.isEmpty()) {
			return u"Invalid secret document or document exceeds size limit"_q;
		}
		const auto target = i->messages.constFind(replyTo);
		if (replyTo && (target == i->messages.cend()
			|| target->packet.action != Codec::Action::None)) {
			return u"Secret reply target was not found"_q;
		}
		auto packet = Codec::Packet();
		packet.randomId = id;
		packet.replyTo = replyTo;
		packet.document = true;
		packet.fileName = fileName;
		packet.mimeType = mimeType;
		packet.fileSize = bytes.size();
		packet.caption = caption;
		packet.fileKey = Details::randomBytes(32);
		packet.fileIv = Details::randomBytes(32);
		const auto ciphertext = Crypto::sealFile(packet.fileKey, packet.fileIv, bytes);
		if (!ciphertext || ciphertext->size() > kMaxDocumentBytes) {
			return u"Secret document encryption failed"_q;
		}
		return Details::addOutgoing(*i, std::move(packet), *ciphertext);
	}, [done = std::move(done), id](QString error) {
		if (done) done(error.isEmpty() ? id : 0, error);
	});
}

void Manager::Private::cancelTransfers() {
	auto cancelled = QVector<quint64>();
	const auto collect = [&](auto &active) {
		for (auto i = active.begin(); i != active.end();) {
			const auto chat = state->chats.constFind(i.key().first);
			if (chat == state->chats.cend() || chat->view.state == State::Discarded
				|| !chat->messages.contains(i.key().second)) {
				cancelled.push_back(i.value());
				uploadOffsets.remove(i.key());
				i = active.erase(i);
			} else {
				++i;
			}
		}
	};
	collect(transfers);
	collect(uploadTransfers);
	for (auto i = pausedUploads.begin(); i != pausedUploads.end();) {
		const auto chat = state->chats.constFind(i.key().first);
		if (chat == state->chats.cend() || chat->view.state == State::Discarded
			|| !chat->messages.contains(i.key().second)) {
			i = pausedUploads.erase(i);
		} else {
			++i;
		}
	}
	for (const auto id : cancelled) {
		files->cancel(id);
	}
}

void Manager::Private::pumpSending() {
	const auto weak = base::make_weak(owner.get());
	for (auto i = state->chats.cbegin(); i != state->chats.cend(); ++i) {
		const auto chatId = i.key();
		if (i->view.state == State::Discarded) {
			if (sending.contains(chatId)) {
				owner->request(sending.take(chatId)).cancel();
			}
			continue;
		}
		if (i->view.state != State::Active || sending.contains(chatId)) continue;
		if (!i->resendQueue.isEmpty()) {
			const auto seq = i->resendQueue.front();
			const auto operation = i->outgoing.constFind(seq);
			if (operation != i->outgoing.cend() && !operation->failed) transmit(chatId, seq, true);
			continue;
		}
		auto operation = i->outgoing.cbegin();
		for (; operation != i->outgoing.cend(); ++operation) {
			if (!operation->sent && !operation->ciphertext.isEmpty()) break;
		}
		if (operation == i->outgoing.cend() || operation->failed) continue;
		const auto seq = operation.key();
		if (pausedUploads.contains({ chatId, operation->packet.randomId })) { continue; }
		if (!operation->packet.document || !operation->inputFile.isEmpty()) {
			transmit(chatId, seq, false);
			continue;
		}
		sending[chatId] = 0;
		const auto messageId = operation->packet.randomId;
		const auto transferId = files->upload(operation->fileCiphertext,
			operation->fileFingerprint,
			[weak, chatId, seq, messageId](std::optional<MTPInputEncryptedFile> file, QString error) {
				const auto manager = weak.get();
				if (!manager) return;
				auto &p = *manager->_private;
				p.uploadTransfers.remove({ chatId, messageId });
				const auto inputFile = file ? Details::serializeTl(*file) : QByteArray();
				p.enqueue([=](Snapshot &snapshot) {
					auto chat = snapshot.chats.find(chatId);
					if (chat == snapshot.chats.end() || chat->view.state != State::Active) return QString();
					auto operation = chat->outgoing.find(seq);
					if (operation == chat->outgoing.end() || !operation->packet.document) return QString();
					operation->inputFile = inputFile;
					operation->failed = !error.isEmpty() || inputFile.isEmpty();
					if (operation->failed) {
						chat->view.error = error.isEmpty() ? u"Secret document upload failed"_q : error;
						const auto message = chat->messages.find(messageId);
						if (message != chat->messages.end()) message->view.delivery = Delivery::Failed;
					}
					return QString();
				}, [weak, chatId, messageId](QString) {
					if (weak) {
						weak->_private->uploadOffsets.remove({ chatId, messageId });
						weak->_private->sending.remove(chatId);
					}
				});
			}, [weak, chatId, messageId](qint64 offset) {
				if (!weak) { return; }
				auto &p = *weak->_private;
				p.uploadOffsets.insert({ chatId, messageId }, offset);
				if (const auto dialog = weak->dialog(chatId)) {
					dialog->updateDocumentUploadProgress(messageId, offset);
				}
			});
		if (transferId) uploadTransfers.insert({ chatId, messageId }, transferId);
	}
}

void Manager::Private::transmit(int chatId, int seq, bool resend) {
	const auto weak = base::make_weak(owner.get());
	sending[chatId] = 0;
	enqueue([=](Snapshot &snapshot) {
		auto chat = snapshot.chats.find(chatId);
		if (chat == snapshot.chats.end() || chat->view.state != State::Active
			|| !chat->outgoing.contains(seq)) return u"Secret operation is no longer available"_q;
		if (seq > chat->submittedOut) return u"Secret outgoing submission is not continuous"_q;
		if (seq == chat->submittedOut) ++chat->submittedOut;
		return QString();
	}, [weak, chatId, seq, resend](QString error) {
		if (const auto manager = weak.get()) {
			if (!error.isEmpty()) {
				manager->_private->sending.remove(chatId);
				return;
			}
			manager->_private->dispatch(chatId, seq, resend);
		}
	});
}

void Manager::Private::dispatch(int chatId, int seq, bool resend) {
	const auto &chat = state->chats[chatId];
	const auto &operation = chat.outgoing[seq];
	const auto weak = base::make_weak(owner.get());
	const auto transportId = operation.transportId;
	const auto peer = MTP_inputEncryptedChat(MTP_int(chatId), MTP_long(chat.accessHash));
	const auto complete = [weak, chatId, seq, transportId](const MTPmessages_SentEncryptedMessage &result) {
		if (const auto manager = weak.get()) {
			result.match([&](const MTPDmessages_sentEncryptedMessage &data) {
				manager->_private->finishSend(chatId, seq, transportId, data.vdate().v, {}, {});
			}, [&](const MTPDmessages_sentEncryptedFile &data) {
				manager->_private->finishSend(chatId, seq, transportId, data.vdate().v,
					Details::serializeTl(data.vfile()), {});
			});
		}
	};
	const auto fail = [weak, chatId, seq, transportId](const MTP::Error &error) {
		if (const auto manager = weak.get()) {
			manager->_private->finishSend(chatId, seq, transportId, 0, {}, error.type());
		}
	};
	auto requestId = mtpRequestId();
	if (operation.packet.document) {
		auto inputFile = Details::deserializeTl<MTPInputEncryptedFile>(operation.inputFile);
		if (resend && !operation.sentFile.isEmpty()) {
			const auto file = Details::deserializeTl<MTPEncryptedFile>(operation.sentFile);
			if (file && file->type() == mtpc_encryptedFile) {
				const auto &data = file->c_encryptedFile();
				inputFile = MTP_inputEncryptedFile(data.vid(), data.vaccess_hash());
			}
		}
		if (!inputFile) {
			finishSend(chatId, seq, transportId, 0, {}, u"Secret document has no encrypted file reference"_q);
			return;
		}
		requestId = owner->request(MTPmessages_SendEncryptedFile(MTP_flags(0), peer,
			MTP_long(transportId), MTP_bytes(operation.ciphertext), *inputFile))
			.afterRequest(lastRequests.value(chatId)).done(complete).fail(fail).send();
	} else if (operation.packet.action != Codec::Action::None || resend) {
		requestId = owner->request(MTPmessages_SendEncryptedService(peer,
			MTP_long(transportId), MTP_bytes(operation.ciphertext)))
			.afterRequest(lastRequests.value(chatId)).done(complete).fail(fail).send();
	} else {
		requestId = owner->request(MTPmessages_SendEncrypted(MTP_flags(0), peer,
			MTP_long(transportId), MTP_bytes(operation.ciphertext)))
			.afterRequest(lastRequests.value(chatId)).done(complete).fail(fail).send();
	}
	sending[chatId] = requestId;
	lastRequests[chatId] = requestId;
}

void Manager::Private::finishSend(int chatId, int seq, qint64 transportId,
		int date, QByteArray file, QString error) {
	const auto weak = base::make_weak(owner.get());
	enqueue([=](Snapshot &snapshot) {
		Details::applySendResult(snapshot, chatId, seq, transportId, date, file, error);
		return QString();
	}, [weak, chatId](QString) { if (weak) weak.get()->_private->sending.remove(chatId); });
}

void Manager::Private::pumpReads() {
	const auto weak = base::make_weak(owner.get());
	for (auto i = state->chats.cbegin(); i != state->chats.cend(); ++i) {
		const auto id = i.key();
		const auto date = i->pendingReadDate;
		if (!date || i->view.state != State::Active || reading.contains(id)) continue;
		reading[id] = owner->request(MTPmessages_ReadEncryptedHistory(
			MTP_inputEncryptedChat(MTP_int(id), MTP_long(i->accessHash)), MTP_int(date)))
			.done([weak, id, date] {
				if (const auto manager = weak.get()) {
					manager->_private->enqueue([=](Snapshot &snapshot) {
						auto &chat = snapshot.chats[id];
						if (chat.pendingReadDate <= date) chat.pendingReadDate = 0;
						return QString();
					}, [weak, id](QString) { if (weak) weak.get()->_private->reading.remove(id); });
				}
			}).fail([weak, id] { if (weak) weak.get()->_private->reading.remove(id); }).send();
	}
}

void Manager::downloadDocument(int chatId, qint64 messageId, Fn<void(QByteArray, QString)> done,
		Fn<void(qint64)> progress) {
	auto &p = *_private;
	const auto chat = p.state->chats.constFind(chatId);
	if (!available() || chat == p.state->chats.cend() || chat->view.state == State::Discarded
		|| !chat->messages.contains(messageId)) {
		done({}, u"Secret document was not found"_q);
		return;
	}
	const auto &message = chat->messages[messageId];
	if (!message.packet.document || message.view.unsupported
		|| (message.view.expiresAt && message.view.expiresAt <= p.now())) {
		done({}, u"Secret document is unavailable"_q);
		return;
	}
	if (p.transfers.contains({ chatId, messageId })) {
		done({}, u"Secret document transfer is already pending"_q);
		return;
	}
	const auto weak = base::make_weak(this);
	const auto decode = [weak, chatId, messageId, done = std::move(done)](QByteArray ciphertext, QString error) {
		const auto manager = weak.get();
		if (!manager) return;
		auto &p = *manager->_private;
		p.transfers.remove({ chatId, messageId });
		const auto chat = p.state->chats.constFind(chatId);
		if (!error.isEmpty() || chat == p.state->chats.cend() || !chat->messages.contains(messageId)) {
			done({}, error.isEmpty() ? u"Secret document was deleted"_q : error);
			return;
		}
		auto plaintext = std::make_shared<std::optional<QByteArray>>();
		p.enqueue([=](Private::Snapshot &snapshot) {
			const auto chat = snapshot.chats.constFind(chatId);
			if (chat == snapshot.chats.cend() || chat->view.state == State::Discarded
				|| !chat->messages.contains(messageId)) {
				return u"Secret document was deleted"_q;
			}
			const auto &packet = chat->messages[messageId].packet;
			*plaintext = Crypto::openFile(packet.fileKey, packet.fileIv, ciphertext, packet.fileSize);
			return *plaintext ? QString() : u"Secret document decryption failed"_q;
		}, [weak, chatId, messageId, plaintext, done](QString error) mutable {
			const auto manager = weak.get();
			if (!manager) return;
			const auto chat = manager->_private->state->chats.constFind(chatId);
			if (chat == manager->_private->state->chats.cend() || chat->view.state == State::Discarded
				|| !chat->messages.contains(messageId)) {
				done({}, u"Secret document was deleted"_q);
				return;
			}
			const auto expiresAt = chat->messages[messageId].view.expiresAt;
			if (expiresAt && expiresAt <= manager->_private->now()) {
				done({}, u"Secret document has expired"_q);
				return;
			}
			done(error.isEmpty() && *plaintext ? **plaintext : QByteArray(), error);
		});
	};
	if (!message.ciphertext.isEmpty()) {
		decode(message.ciphertext, {});
		return;
	}
	for (const auto &operation : chat->outgoing) {
		if (operation.packet.randomId == messageId && !operation.fileCiphertext.isEmpty()) {
			decode(operation.fileCiphertext, {});
			return;
		}
	}
	const auto file = Details::deserializeTl<MTPEncryptedFile>(message.file);
	if (!file || file->type() != mtpc_encryptedFile
		|| file->c_encryptedFile().vkey_fingerprint().v
			!= message.fileFingerprint) {
		decode({}, u"Secret document file reference is invalid"_q);
		return;
	}
	const auto fileSize = message.packet.fileSize;
	const auto transferId = p.files->download(*file, decode, [=](qint64 bytes) {
		if (progress) { progress(std::min(bytes, fileSize)); }
	});
	if (transferId) p.transfers.insert({ chatId, messageId }, transferId);
}

void Manager::cancelDocumentDownload(int chatId, qint64 messageId) {
	const auto transferId = _private->transfers.value({ chatId, messageId });
	if (transferId) { _private->files->cancel(transferId); }
}

void Manager::setDocumentUploadPaused(int chatId, qint64 messageId, bool paused) {
	if (paused) { _private->pausedUploads.insert({ chatId, messageId }, true); }
	else { _private->pausedUploads.remove({ chatId, messageId }); }
	const auto transferId = _private->uploadTransfers.value({ chatId, messageId });
	if (transferId) { _private->files->setUploadPaused(transferId, paused); }
	if (!paused) { _private->pump(); }
}

} // namespace Extras::SecretChats
