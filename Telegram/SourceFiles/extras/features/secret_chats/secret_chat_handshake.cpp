#include "extras/features/secret_chats/secret_chat_internal.h"

#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_user.h"

namespace Extras::SecretChats::Details {

void applyEncryption(Snapshot &state, const MTPEncryptedChat &value, qint64 now, int creationId) {
	const auto prepare = [&](int id, qint64 hash, quint64 admin, quint64 participant) -> ChatState& {
		auto &chat = state.chats[id];
		chat.view.id = id;
		if (chat.view.state == State::Discarded) return chat;
		chat.accessHash = hash;
		chat.view.creator = admin == state.selfId;
		chat.view.userId = UserId(chat.view.creator ? participant : admin);
		if (creationId && chat.view.creator) {
			const auto creation = state.creations.find(creationId);
			if (creation != state.creations.end()) {
				if (creation->userId != chat.view.userId.bare) {
					discardState(chat, u"Secret creation response has an unexpected participant"_q);
					return chat;
				}
				chat.dh = creation->dh;
				state.creations.erase(creation);
			}
		}
		return chat;
	};
	const auto activate = [&](ChatState &chat, const QByteArray &peerPublic, qint64 fingerprint) {
		if (chat.view.state == State::Discarded) {
			return;
		}
		if (chat.view.state == State::Active && chat.initialKeyFingerprint == fingerprint) {
			return;
		}
		if (chat.key.isEmpty()) {
			if (chat.dh.power.isEmpty()) {
				chat.peerPublic = peerPublic;
				chat.pendingFingerprint = fingerprint;
				chat.activationPending = true;
				chat.view.state = State::Waiting;
				return;
			}
			const auto key = Crypto::sharedKey(chat.dh, peerPublic);
			if (!key) {
				discardState(chat, u"Secret handshake has invalid public value"_q);
				return;
			}
			chat.key = *key;
		}
		if (Crypto::fingerprint(chat.key) != fingerprint) {
			discardState(chat, u"Secret handshake fingerprint mismatch"_q);
			return;
		}
		chat.keyFingerprint = fingerprint;
		chat.initialKeyFingerprint = chat.keyFingerprint;
		chat.view.state = State::Active;
		chat.view.error.clear();
		chat.acceptPending = false;
		chat.activationPending = false;
		chat.pendingFingerprint = 0;
		chat.peerPublic.clear();
		chat.initialGenerator = chat.dh.generator;
		chat.initialPrime = chat.dh.prime;
		chat.dh = {};
		if (chat.view.verification.isEmpty()) {
			chat.view.verification = Crypto::verification(chat.key);
			chat.keyCreatedAt = now;
		}
		if (!chat.notified) {
			auto packet = Codec::Packet();
			packet.action = Codec::Action::NotifyLayer;
			packet.value = 144;
			const auto error = addService(chat, std::move(packet));
			if (!error.isEmpty()) {
				discardState(chat, error);
				return;
			}
			chat.notified = true;
		}
	};
	value.match([&](const MTPDencryptedChatEmpty &) {
	}, [&](const MTPDencryptedChatWaiting &data) {
		auto &chat = prepare(data.vid().v, data.vaccess_hash().v,
			data.vadmin_id().v, data.vparticipant_id().v);
		if (chat.view.state != State::Active && chat.view.state != State::Discarded) chat.view.state = State::Waiting;
		if (chat.activationPending && !chat.dh.power.isEmpty()) activate(chat, chat.peerPublic, chat.pendingFingerprint);
	}, [&](const MTPDencryptedChatRequested &data) {
		auto &chat = prepare(data.vid().v, data.vaccess_hash().v,
			data.vadmin_id().v, data.vparticipant_id().v);
		if (chat.view.state == State::Active || chat.view.state == State::Discarded) return;
		chat.view.state = State::Requested;
		chat.peerPublic = data.vg_a().v;
	}, [&](const MTPDencryptedChat &data) {
		auto &chat = prepare(data.vid().v, data.vaccess_hash().v,
			data.vadmin_id().v, data.vparticipant_id().v);
		activate(chat, data.vg_a_or_b().v, data.vkey_fingerprint().v);
	}, [&](const MTPDencryptedChatDiscarded &data) {
		auto &chat = state.chats[data.vid().v];
		chat.view.id = data.vid().v;
		discardState(chat, {});
		chat.discardPending = false;
		if (data.is_history_deleted()) {
			chat.messages.clear();
			chat.messageOrder.clear();
			chat.deleted.clear();
		}
	});
}

void maybeRekey(Snapshot &, ChatState &chat, qint64 now) {
	if (chat.view.state != State::Active || chat.exchangeStage || !chat.oldKey.isEmpty()
		|| !chat.keyUsedOut || (chat.keyUsedOut < 100 && chat.keyUsedIn < 100
			&& now < chat.keyCreatedAt + 7 * 86400)) {
		return;
	}
	const auto dh = Crypto::createDh(chat.initialGenerator, chat.initialPrime, randomBytes(256));
	if (!dh) {
		discardState(chat, u"Secret rekey parameter validation failed"_q);
		return;
	}
	chat.exchangeId = randomId();
	chat.exchangeDh = *dh;
	chat.exchangeStage = 1;
	auto packet = Codec::Packet();
	packet.action = Codec::Action::RequestKey;
	packet.exchangeId = chat.exchangeId;
	packet.publicValue = dh->publicValue;
	const auto error = addService(chat, std::move(packet));
	if (!error.isEmpty()) {
		discardState(chat, error);
	}
}

void switchKey(ChatState &chat, qint64 now) {
	Expects(chat.candidateKey.size() == 256);
	chat.oldKey = chat.key;
	chat.key = std::exchange(chat.candidateKey, {});
	chat.keyFingerprint = Crypto::fingerprint(chat.key);
	chat.oldUntilOut = chat.nextOut;
	chat.oldSeenNew = false;
	chat.keyCreatedAt = now;
	chat.keyUsedOut = chat.keyUsedIn = 0;
	chat.completedExchangeId = chat.exchangeId;
	chat.completedExchangeFingerprint = Crypto::fingerprint(chat.key);
	chat.exchangeDh = {};
	chat.exchangeStage = 0;
	chat.exchangeId = 0;
}

void retireOldKey(ChatState &chat) {
	if (chat.oldKey.isEmpty() || !chat.oldSeenNew || !chat.incoming.isEmpty()
		|| chat.peerAck < chat.oldUntilOut) {
		return;
	}
	const auto oldFingerprint = Crypto::fingerprint(chat.oldKey);
	for (const auto &operation : chat.outgoing) {
		if (!operation.ciphertext.isEmpty() && operation.keyFingerprint == oldFingerprint) return;
	}
	chat.oldKey.clear();
}

void applyKeyAction(Snapshot &, ChatState &chat, const Incoming &message, qint64 now) {
	const auto &packet = message.packet;
	const auto reset = [&] {
		chat.exchangeId = 0;
		chat.exchangeStage = 0;
		chat.exchangeDh = {};
		chat.candidateKey.clear();
	};
	const auto service = [&](Codec::Packet response) {
		const auto error = addService(chat, std::move(response));
		if (!error.isEmpty()) {
			discardState(chat, error);
		}
	};
	const auto abort = [&](qint64 exchangeId) {
		auto response = Codec::Packet();
		response.action = Codec::Action::AbortKey;
		response.exchangeId = exchangeId;
		service(std::move(response));
	};
	if (packet.action == Codec::Action::RequestKey) {
		if (!packet.exchangeId || !chat.oldKey.isEmpty()) {
			abort(packet.exchangeId);
			return;
		}
		if (chat.exchangeStage == 1) {
			if (chat.exchangeId == packet.exchangeId) {
				reset();
				return;
			}
			if (chat.exchangeId > packet.exchangeId) {
				return;
			}
			reset();
		} else if (chat.exchangeStage) {
			if (chat.exchangeId != packet.exchangeId) abort(packet.exchangeId);
			return;
		}
		const auto dh = Crypto::createDh(chat.initialGenerator, chat.initialPrime, randomBytes(256));
		const auto key = dh ? Crypto::sharedKey(*dh, packet.publicValue) : std::nullopt;
		if (!key) {
			abort(packet.exchangeId);
			return;
		}
		chat.exchangeId = packet.exchangeId;
		chat.exchangeStage = 2;
		chat.exchangeDh = *dh;
		chat.candidateKey = *key;
		auto response = Codec::Packet();
		response.action = Codec::Action::AcceptKey;
		response.exchangeId = packet.exchangeId;
		response.publicValue = dh->publicValue;
		response.keyFingerprint = Crypto::fingerprint(*key);
		service(std::move(response));
		return;
	}
	if (packet.action == Codec::Action::AcceptKey) {
		if (chat.exchangeStage != 1 || chat.exchangeId != packet.exchangeId) {
			abort(packet.exchangeId);
			return;
		}
		const auto key = Crypto::sharedKey(chat.exchangeDh, packet.publicValue);
		if (!key || Crypto::fingerprint(*key) != packet.keyFingerprint) {
			reset();
			abort(packet.exchangeId);
			return;
		}
		chat.candidateKey = *key;
		auto response = Codec::Packet();
		response.action = Codec::Action::CommitKey;
		response.exchangeId = packet.exchangeId;
		response.keyFingerprint = packet.keyFingerprint;
		service(std::move(response));
		if (chat.view.state == State::Active) {
			switchKey(chat, now);
		}
		return;
	}
	if (packet.action == Codec::Action::CommitKey) {
		if (chat.completedExchangeId == packet.exchangeId
			&& chat.completedExchangeFingerprint == packet.keyFingerprint) {
			return;
		}
		if (chat.exchangeStage != 2 || chat.exchangeId != packet.exchangeId
			|| Crypto::fingerprint(chat.candidateKey) != packet.keyFingerprint) {
			if (chat.exchangeStage == 2 && chat.exchangeId == packet.exchangeId) {
				discardState(chat, u"Secret rekey commit fingerprint mismatch"_q);
			} else {
				abort(packet.exchangeId);
			}
			return;
		}
		switchKey(chat, now);
		chat.oldSeenNew = true;
		auto response = Codec::Packet();
		response.action = Codec::Action::Noop;
		service(std::move(response));
		return;
	}
	if (packet.action == Codec::Action::AbortKey && chat.exchangeId == packet.exchangeId) {
		reset();
	}
}

} // namespace Extras::SecretChats::Details

namespace Extras::SecretChats {

void Manager::Private::getDh(Fn<void(int, QByteArray, QByteArray, int, QString)> done) {
	dhCallbacks.push_back(std::move(done));
	if (dhRequest) {
		return;
	}
	const auto weak = base::make_weak(owner.get());
	const auto complete = [weak](int generator, QByteArray prime, QByteArray random, int version, QString error) {
		const auto manager = weak.get();
		if (!manager) {
			return;
		}
		auto &p = *manager->_private;
		p.dhRequest = 0;
		const auto callbacks = std::exchange(p.dhCallbacks, {});
		for (const auto &callback : callbacks) {
			callback(generator, prime, random, version, error);
		}
	};
	dhRequest = owner->request(MTPmessages_GetDhConfig(MTP_int(0), MTP_int(256)))
		.done([=](const MTPmessages_DhConfig &result) {
			if (result.type() != mtpc_messages_dhConfig) {
				complete(0, {}, {}, 0, u"Unexpected unchanged secret DH configuration"_q);
				return;
			}
			const auto &data = result.c_messages_dhConfig();
			complete(data.vg().v, data.vp().v, data.vrandom().v, data.vversion().v, {});
		}).fail([=](const MTP::Error &error) {
			complete(0, {}, {}, 0, error.type());
		}).send();
}

void Manager::create(not_null<UserData*> user, Fn<void(int, QString)> done) {
	auto &p = *_private;
	if (!available() || !hasBaseline() || user->id == peerFromUser(p.session->userId()) || user->isBot()) {
		done(0, u"Secret chat cannot be created for this user"_q);
		return;
	}
	auto creationId = 0;
	while (!creationId) creationId = int(quint64(Details::randomId()) & 0x7fffffffULL);
	const auto userId = quint64(peerToUser(user->id).bare);
	const auto inputUser = Details::serializeTl(user->inputUser());
	p.createCallbacks.insert(creationId, std::move(done));
	const auto weak = base::make_weak(this);
	p.enqueue([=](Private::Snapshot &snapshot) {
		for (const auto &creation : snapshot.creations) {
			if (creation.userId == userId) {
				return u"Secret chat creation is already pending"_q;
			}
		}
		auto creation = Details::Creation();
		creation.userId = userId;
		creation.inputUser = inputUser;
		snapshot.creations.insert(creationId, std::move(creation));
		return QString();
	}, [weak, creationId](QString error) {
		if (const auto manager = weak.get(); manager && !error.isEmpty()) {
			const auto callback = manager->_private->createCallbacks.take(creationId);
			if (callback) {
				callback(0, error);
			}
		}
	});
}

void Manager::Private::pumpHandshakes() {
	const auto weak = base::make_weak(owner.get());
	for (auto i = state->creations.cbegin(); i != state->creations.cend(); ++i) {
		const auto id = i.key();
		if (handshakes.contains(-id)) {
			continue;
		}
		handshakes.insert(-id, 0);
		if (i->dh.power.isEmpty()) {
			getDh([weak, id](int generator, QByteArray prime, QByteArray random, int version, QString error) {
				const auto manager = weak.get();
				if (!manager) {
					return;
				}
				auto &p = *manager->_private;
				auto validationError = std::make_shared<QString>();
				p.enqueue([=](Snapshot &snapshot) {
					const auto i = snapshot.creations.find(id);
					if (i == snapshot.creations.end()) {
						return QString();
					}
					if (!error.isEmpty()) {
						snapshot.creations.erase(i);
						return QString();
					}
					const auto dh = Crypto::createDh(generator, prime, random);
					if (!dh) {
						*validationError = u"Secret DH configuration failed validation"_q;
						snapshot.creations.erase(i);
						return QString();
					}
					i->dh = *dh;
					snapshot.dhGenerator = generator;
					snapshot.dhPrime = prime;
					snapshot.dhVersion = version;
					return QString();
				}, [weak, id, error, validationError](QString storageError) {
					if (const auto manager = weak.get()) {
						auto &p = *manager->_private;
						p.handshakes.remove(-id);
						const auto failure = !storageError.isEmpty() ? storageError
							: !error.isEmpty() ? error : *validationError;
						if (!failure.isEmpty()) {
							const auto callback = p.createCallbacks.take(id);
							if (callback) callback(0, failure);
						}
					}
				});
			});
			continue;
		}
		const auto creation = *i;
		const auto inputUser = Details::deserializeTl<MTPInputUser>(creation.inputUser);
		Expects(inputUser.has_value());
		handshakes[-id] = owner->request(MTPmessages_RequestEncryption(
			*inputUser, MTP_int(id), MTP_bytes(creation.dh.publicValue)))
			.done([weak, id](const MTPEncryptedChat &result) {
				const auto manager = weak.get();
				if (!manager) return;
				const auto timestamp = manager->_private->now();
				const auto chatId = result.match([](const auto &data) { return data.vid().v; });
				const auto rejected = result.type() == mtpc_encryptedChatEmpty || result.type() == mtpc_encryptedChatDiscarded;
				manager->_private->enqueue([=](Snapshot &snapshot) {
					const auto creation = snapshot.creations.constFind(id);
					if (creation != snapshot.creations.cend()) {
						snapshot.chats[chatId].view.userId = UserId(creation->userId);
						snapshot.chats[chatId].view.creator = true;
					}
					Details::applyEncryption(snapshot, result, timestamp, id);
					snapshot.creations.remove(id);
					Details::drainInbox(snapshot, timestamp);
					return QString();
				}, [weak, id, chatId, rejected](QString error) {
					if (const auto manager = weak.get()) {
						auto &p = *manager->_private;
						p.handshakes.remove(-id);
						const auto callback = p.createCallbacks.take(id);
						if (error.isEmpty() && rejected) error = u"Secret chat creation was declined"_q;
						if (callback) callback(error.isEmpty() ? chatId : 0, error);
					}
				});
			}).fail([weak, id](const MTP::Error &error) {
				if (const auto manager = weak.get()) {
					manager->_private->enqueue([=](Snapshot &snapshot) {
						snapshot.creations.remove(id);
						return QString();
					}, [weak, id, failure = error.type()](QString storageError) {
						if (const auto manager = weak.get()) {
							auto &p = *manager->_private;
							p.handshakes.remove(-id);
							const auto callback = p.createCallbacks.take(id);
							if (callback) callback(0, storageError.isEmpty() ? failure : storageError);
						}
					});
				}
			}).send();
	}
	for (auto i = state->chats.cbegin(); i != state->chats.cend(); ++i) {
		const auto id = i.key();
		if (handshakes.contains(id)) continue;
		if (i->discardPending) {
			handshakes[id] = owner->request(MTPmessages_DiscardEncryption(
				MTP_flags(i->discardHistory ? MTPmessages_DiscardEncryption::Flag::f_delete_history
					: MTPmessages_DiscardEncryption::Flags()), MTP_int(id)))
				.done([weak, id] {
					if (const auto manager = weak.get()) {
						manager->_private->enqueue([=](Snapshot &snapshot) {
							snapshot.chats[id].discardPending = false;
							return QString();
						}, [weak, id](QString) { if (weak) weak.get()->_private->handshakes.remove(id); });
					}
				}).fail([weak, id] { if (weak) weak.get()->_private->handshakes.remove(id); }).send();
			continue;
		}
		if (i->view.state != State::Requested || !i->view.error.isEmpty()) continue;
		handshakes[id] = 0;
		if (i->dh.power.isEmpty()) {
			getDh([weak, id](int generator, QByteArray prime, QByteArray random, int version, QString error) {
				const auto manager = weak.get();
				if (!manager) return;
				const auto timestamp = manager->_private->now();
				manager->_private->enqueue([=](Snapshot &snapshot) {
					auto &chat = snapshot.chats[id];
					if (chat.view.state != State::Requested) return QString();
					if (!error.isEmpty()) {
						chat.view.error = error;
						return QString();
					}
					const auto dh = Crypto::createDh(generator, prime, random);
					const auto key = dh ? Crypto::sharedKey(*dh, chat.peerPublic) : std::nullopt;
					if (!key) {
						Details::discardState(chat, u"Incoming secret handshake failed validation"_q);
						return QString();
					}
					chat.dh = *dh;
					chat.key = *key;
					chat.keyFingerprint = Crypto::fingerprint(*key);
					chat.keyCreatedAt = timestamp;
					chat.acceptPending = true;
					snapshot.dhGenerator = generator;
					snapshot.dhPrime = prime;
					snapshot.dhVersion = version;
					return QString();
				}, [weak, id](QString) { if (weak) weak.get()->_private->handshakes.remove(id); });
			});
			continue;
		}
		handshakes[id] = owner->request(MTPmessages_AcceptEncryption(
			MTP_inputEncryptedChat(MTP_int(id), MTP_long(i->accessHash)),
			MTP_bytes(i->dh.publicValue), MTP_long(i->keyFingerprint)))
			.done([weak, id](const MTPEncryptedChat &result) {
				const auto manager = weak.get();
				if (!manager) return;
				const auto timestamp = manager->_private->now();
				manager->_private->enqueue([=](Snapshot &snapshot) {
					Details::applyEncryption(snapshot, result, timestamp);
					Details::drainInbox(snapshot, timestamp);
					return QString();
				}, [weak, id](QString) { if (weak) weak.get()->_private->handshakes.remove(id); });
			}).fail([weak, id](const MTP::Error &error) {
				if (const auto manager = weak.get()) {
					manager->_private->enqueue([=](Snapshot &snapshot) {
						snapshot.chats[id].view.error = error.type();
						return QString();
					}, [weak, id](QString) { if (weak) weak.get()->_private->handshakes.remove(id); });
				}
			}).send();
	}
}

} // namespace Extras::SecretChats
