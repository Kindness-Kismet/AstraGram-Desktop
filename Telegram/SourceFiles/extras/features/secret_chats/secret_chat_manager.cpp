#include "extras/features/secret_chats/secret_chat_internal.h"

#include "main/main_session.h"
#include "apiwrap.h"
#include "api/api_updates.h"
#include "storage/storage_account.h"
#include "mtproto/mtproto_auth_key.h"
#include "mtproto/mtp_instance.h"
#include "crl/crl_async.h"
#include "crl/crl_on_main.h"

#include <QtCore/QDateTime>

namespace Extras::SecretChats {

Manager::Private::Private(not_null<Manager*> owner, not_null<Main::Session*> session)
: owner(owner)
, session(session)
, state(std::make_shared<Snapshot>())
, timer([=] { tick(); })
, wallStarted(QDateTime::currentSecsSinceEpoch())
, monotonicStarted(crl::now()) {
}

Manager::Manager(not_null<Main::Session*> session)
: MTP::Sender(&session->mtp())
, _private(std::make_unique<Private>(this, session)) {
}

Manager::~Manager() = default;

void Manager::initialize(Fn<void(bool)> done) {
	auto &p = *_private;
	if (p.ready) {
		done(p.enabled);
		return;
	}
	p.initializeCallbacks.push_back(std::move(done));
	if (p.initializing) {
		return;
	}
	p.initializing = true;
	const auto localKey = p.session->local().peekLegacyLocalKey();
	auto authorizationId = quint64();
	for (const auto &key : p.session->mtp().getKeysForWrite()) {
		if (key->dcId() == p.session->mtp().mainDcId()
			&& key->type() != MTP::AuthKey::Type::Temporary) {
			authorizationId = key->keyId();
			break;
		}
	}
	const auto finish = [this](QString error) {
		auto &p = *_private;
		p.failure = std::move(error);
		p.ready = true;
		p.enabled = p.failure.isEmpty() && !p.stopping;
		p.initializing = false;
		const auto callbacks = std::exchange(p.initializeCallbacks, {});
		for (const auto &callback : callbacks) {
			callback(p.enabled);
		}
		if (p.enabled) {
			p.timer.callEach(1000);
			p.publish();
		}
		p.runNext();
	};
	if (!localKey || !authorizationId) {
		finish(u"Secret storage requires a persistent authorization and local key"_q);
		return;
	}
	const auto keyBytes = localKey->data();
	p.store = std::make_unique<Store>(Store::Config{
		.path = p.session->local().cachePath() + u"-secret-chats/state.sqlite"_q,
		.localKey = QByteArray(reinterpret_cast<const char*>(keyBytes.data()), keyBytes.size()),
		.accountId = p.session->uniqueId(),
		.authorizationId = authorizationId,
	});
	p.files = std::make_unique<FileTransfer>(&p.session->mtp());
	const auto selfId = quint64(p.session->userId().bare);
	const auto now = p.now();
	const auto weak = base::make_weak(this);
	p.store->load([=](Store::LoadResult result) mutable {
		if (!weak) {
			return;
		}
		if (!result.ok || _private->stopping) {
			finish(result.error.isEmpty() ? u"Secret storage initialization failed"_q : result.error);
			return;
		}
		crl::async([weak, selfId, now, finish, result = std::move(result)]() mutable {
			auto snapshot = result.exists
				? Details::deserialize(result.state)
				: std::make_optional(Details::Snapshot());
			auto error = QString();
			if (!snapshot || (snapshot->selfId && snapshot->selfId != selfId)) {
				error = u"Secret storage snapshot is invalid"_q;
			} else {
				snapshot->selfId = selfId;
				Details::expire(*snapshot, now);
				Details::drainInbox(*snapshot, now);
			}
			auto bytes = snapshot ? Details::serialize(*snapshot) : QByteArray();
			crl::on_main([weak, finish, snapshot = std::move(snapshot), bytes = std::move(bytes), error]() mutable {
				const auto manager = weak.get();
				if (!manager) {
					return;
				}
				if (!error.isEmpty() || bytes.isEmpty() || manager->_private->stopping) {
					finish(error.isEmpty() ? u"Secret storage initialization cancelled"_q : error);
					return;
				}
				auto next = std::make_shared<Details::Snapshot>(std::move(*snapshot));
				manager->_private->store->save(std::move(bytes), [weak, finish, next](bool ok) {
					if (const auto manager = weak.get()) {
						if (ok && !manager->_private->stopping) {
							manager->_private->state = next;
						}
						finish(ok ? QString() : u"Secret storage recovery could not be committed"_q);
					}
				});
			});
		});
	});
}

bool Manager::available() const {
	return _private->enabled && !_private->stopping;
}
QString Manager::error() const {
	return _private->failure;
}
bool Manager::hasBaseline() const {
	return _private->state->baseline;
}
int Manager::committedQts() const {
	return _private->state->qts;
}
qint64 Manager::currentTime() const {
	return _private->now();
}
std::optional<RecoveryState> Manager::recoveryState() const {
	return _private->state->hasRecovery
		? std::make_optional(_private->state->recovery) : std::nullopt;
}
void Manager::persistRecovery(RecoveryState state, Fn<void(bool)> done) {
	_private->enqueue([=](Private::Snapshot &snapshot) {
		snapshot.recovery = state;
		snapshot.hasRecovery = true;
		return QString();
	}, [done = std::move(done)](QString error) { if (done) done(error.isEmpty()); });
}

void Manager::Private::enqueue(Fn<QString(Snapshot&)> apply, Fn<void(QString)> done) {
	if (stopping || (ready && !enabled)) {
		if (done) {
			done(failure.isEmpty() ? u"Secret chat manager is unavailable"_q : failure);
		}
		return;
	}
	tasks.push_back({ std::move(apply), std::move(done) });
	runNext();
}

void Manager::Private::runNext() {
	if (busy || !ready || tasks.empty()) {
		return;
	}
	auto task = std::move(tasks.front());
	tasks.pop_front();
	if (!enabled || stopping) {
		if (task.done) {
			task.done(failure.isEmpty() ? u"Secret chat manager stopped"_q : failure);
		}
		runNext();
		return;
	}
	busy = true;
	auto next = std::make_shared<Snapshot>(*state);
	const auto timestamp = now();
	const auto weak = base::make_weak(owner.get());
	crl::async([weak, next, timestamp, task = std::move(task)]() mutable {
		Details::expire(*next, timestamp);
		const auto error = task.apply(*next);
		auto bytes = error.isEmpty() ? Details::serialize(*next) : QByteArray();
		crl::on_main([weak, next, bytes = std::move(bytes), task = std::move(task), error]() mutable {
			const auto manager = weak.get();
			if (!manager) {
				return;
			}
			auto &p = *manager->_private;
			if (!error.isEmpty() || p.stopping || bytes.isEmpty()) {
				p.busy = false;
				if (task.done) {
					task.done(error.isEmpty() ? u"Secret operation could not be committed"_q : error);
				}
				p.runNext();
				return;
			}
			p.store->save(std::move(bytes), [weak, next, done = std::move(task.done)](bool ok) {
				const auto manager = weak.get();
				if (!manager) {
					return;
				}
				auto &p = *manager->_private;
				p.busy = false;
				if (ok && !p.stopping) {
					p.state = next;
				} else if (!ok) {
					p.enabled = false;
					p.failure = u"Secret storage commit failed; reception is paused"_q;
				}
				if (done) {
					done(ok && !p.stopping ? QString() : p.failure.isEmpty()
						? u"Secret operation cancelled"_q : p.failure);
				}
				if (ok && !p.stopping) {
					p.publish();
				} else {
					p.changes.fire({});
				}
				p.runNext();
			});
		});
	});
}

void Manager::Private::publish() {
	cancelTransfers();
	changes.fire({});
	pump();
}

void Manager::Private::pump() {
	if (!ready || !enabled || stopping || !state->baseline) {
		return;
	}
	const auto timestamp = now();
	for (const auto &chat : state->chats) {
		for (const auto &message : chat.messages) {
			if (message.view.expiresAt && message.view.expiresAt <= timestamp) {
				enqueue([=](Snapshot &snapshot) { Details::expire(snapshot, timestamp); return QString(); });
				return;
			}
		}
	}
	if (!state->pendingQts.isEmpty() && state->pendingQts.firstKey() > state->qts + 1
		&& !session->api().updates().requestingDifference()) {
		session->api().updates().getDifference();
	}
	pumpHandshakes();
	pumpSending();
	pumpReads();
	acknowledge();
}

qint64 Manager::Private::now() const {
	return std::max(QDateTime::currentSecsSinceEpoch(),
		wallStarted + (crl::now() - monotonicStarted) / 1000);
}

void Manager::Private::tick() {
	if (!enabled || stopping || busy) {
		return;
	}
	const auto timestamp = now();
	auto mutationNeeded = false;
	for (const auto &chat : state->chats) {
		if (chat.view.state == State::Active && !chat.exchangeStage && chat.oldKey.isEmpty()
			&& chat.keyUsedOut > 0 && (chat.keyUsedOut >= 100 || chat.keyUsedIn >= 100
				|| chat.keyCreatedAt + 7 * 86400 <= timestamp)) {
			mutationNeeded = true;
		}
		for (const auto &message : chat.messages) {
			if (message.view.expiresAt && message.view.expiresAt <= timestamp) {
				mutationNeeded = true;
			}
		}
	}
	if (!mutationNeeded) {
		pump();
		return;
	}
	enqueue([=](Snapshot &snapshot) {
		Details::expire(snapshot, timestamp);
		for (auto &chat : snapshot.chats) {
			Details::maybeRekey(snapshot, chat, timestamp);
		}
		return QString();
	});
}

QVector<Chat> Manager::chats() const {
	auto result = QVector<Chat>();
	for (const auto &chat : _private->state->chats) {
		result.push_back(chat.view);
	}
	return result;
}
std::optional<Chat> Manager::chat(int chatId) const {
	const auto i = _private->state->chats.constFind(chatId);
	return i == _private->state->chats.cend() ? std::nullopt : std::make_optional(i->view);
}
QVector<Message> Manager::messages(int chatId) const {
	auto result = QVector<Message>();
	const auto i = _private->state->chats.constFind(chatId);
	if (i == _private->state->chats.cend()) {
		return result;
	}
	for (const auto id : i->messageOrder) {
		const auto message = i->messages.constFind(id);
		if (message != i->messages.cend()
			&& (!message->view.expiresAt || message->view.expiresAt > _private->now())) {
			result.push_back(message->view);
		}
	}
	return result;
}
rpl::producer<> Manager::changed() const {
	return _private->changes.events();
}

void Manager::deleteMessages(int chatId, QVector<qint64> ids, Fn<void(QString)> done) {
	_private->enqueue([=](Private::Snapshot &snapshot) {
		auto i = snapshot.chats.find(chatId);
		if (i == snapshot.chats.end()) {
			return u"Secret chat was not found"_q;
		}
		const auto error = Details::eraseMessages(*i, ids);
		if (!error.isEmpty() || i->view.state != State::Active) {
			return error;
		}
		auto packet = Codec::Packet();
		packet.action = Codec::Action::Delete;
		packet.ids = ids;
		return Details::addService(*i, std::move(packet));
	}, std::move(done));
}

void Manager::clear(int chatId, Fn<void(QString)> done) {
	_private->enqueue([=](Private::Snapshot &snapshot) {
		auto i = snapshot.chats.find(chatId);
		if (i == snapshot.chats.end()) {
			return u"Secret chat was not found"_q;
		}
		const auto error = Details::clearMessages(*i, true);
		if (!error.isEmpty() || i->view.state != State::Active) {
			return error;
		}
		auto packet = Codec::Packet();
		packet.action = Codec::Action::Flush;
		return Details::addService(*i, std::move(packet));
	}, std::move(done));
}

void Manager::discard(int chatId, bool deleteHistory, Fn<void(QString)> done) {
	_private->enqueue([=](Private::Snapshot &snapshot) {
		auto i = snapshot.chats.find(chatId);
		if (i == snapshot.chats.end()) {
			return u"Secret chat was not found"_q;
		}
		if (deleteHistory) {
			const auto error = Details::clearMessages(*i);
			if (!error.isEmpty()) {
				return error;
			}
		}
		Details::discardState(*i, {});
		i->discardHistory = deleteHistory;
		return QString();
	}, std::move(done));
}

void Manager::setTtl(int chatId, int seconds, Fn<void(QString)> done) {
	_private->enqueue([=](Private::Snapshot &snapshot) {
		auto i = snapshot.chats.find(chatId);
		if (i == snapshot.chats.end() || seconds < 0 || seconds == std::numeric_limits<int>::max()) {
			return u"Invalid secret chat timer"_q;
		}
		auto packet = Codec::Packet();
		packet.action = Codec::Action::SetTtl;
		packet.value = seconds;
		const auto error = Details::addService(*i, std::move(packet));
		if (error.isEmpty()) {
			i->view.ttl = seconds;
		}
		return error;
	}, std::move(done));
}

void Manager::read(int chatId, int maxDate, Fn<void(QString)> done) {
	const auto chat = _private->state->chats.constFind(chatId);
	if (chat != _private->state->chats.cend() && chat->readDate >= maxDate) {
		auto unread = false;
		for (const auto &message : chat->messages) {
			if (!message.view.outgoing && message.view.date <= maxDate && message.view.delivery != Delivery::Read) {
				unread = true;
				break;
			}
		}
		if (!unread) {
			if (done) done({});
			return;
		}
	}
	const auto timestamp = _private->now();
	_private->enqueue([=](Private::Snapshot &snapshot) {
		auto i = snapshot.chats.find(chatId);
		if (i == snapshot.chats.end() || i->view.state != State::Active) {
			return u"Secret chat is not active"_q;
		}
		for (auto &message : i->messages) {
			if (!message.view.outgoing && message.view.date <= maxDate) {
				Details::startTtl(message, timestamp);
			}
		}
		i->pendingReadDate = std::max(i->pendingReadDate, maxDate);
		if (maxDate > i->readDate) {
			i->readDate = maxDate;
			i->readAt = timestamp;
		}
		return QString();
	}, std::move(done));
}

void Manager::retry(int chatId, qint64 messageId, Fn<void(QString)> done) {
	_private->enqueue([=](Private::Snapshot &snapshot) {
		auto i = snapshot.chats.find(chatId);
		if (i == snapshot.chats.end() || i->view.state == State::Discarded) {
			return u"Secret chat is unavailable"_q;
		}
		auto found = !messageId;
		for (auto &operation : i->outgoing) {
			if ((!messageId || operation.packet.randomId == messageId) && operation.failed) {
				operation.failed = false;
				found = true;
				const auto m = i->messages.find(operation.packet.randomId);
				if (m != i->messages.end()) {
					m->view.delivery = Delivery::Pending;
				}
			}
		}
		if (found) {
			i->view.error.clear();
		}
		return found ? QString() : u"Failed secret message was not found"_q;
	}, std::move(done));
}

void Manager::logout(Fn<void(bool)> done) {
	auto &p = *_private;
	p.stopping = true;
	p.enabled = false;
	p.timer.cancel();
	for (const auto &map : { p.handshakes, p.sending, p.reading }) {
		for (const auto requestId : map) {
			request(requestId).cancel();
		}
	}
	request(p.dhRequest).cancel();
	request(p.ackRequest).cancel();
	p.files.reset();
	p.transfers.clear();
	p.runNext();
	if (!p.store) {
		if (done) {
			done(true);
		}
		return;
	}
	const auto weak = base::make_weak(this);
	p.store->destroy([weak, done = std::move(done)](bool ok) {
		if (const auto manager = weak.get()) {
			manager->_private->state = std::make_shared<Details::Snapshot>();
			manager->_private->changes.fire({});
			if (done) {
				done(ok);
			}
		}
	});
}

} // namespace Extras::SecretChats
