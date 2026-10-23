#pragma once

#include "extras/features/secret_chats/secret_chat_manager.h"
#include "extras/features/secret_chats/secret_chat_codec.h"
#include "extras/features/secret_chats/secret_chat_crypto.h"
#include "extras/features/secret_chats/secret_chat_files.h"
#include "extras/data/secret_chat_store.h"
#include "base/timer.h"

#include <QtCore/QDataStream>
#include <QtCore/QMap>
#include <QtCore/QSet>
#include <deque>
#include <limits>

namespace Extras::SecretChats::Details {

struct StoredMessage {
	Message view;
	Codec::Packet packet;
	QByteArray file;
	QByteArray ciphertext;
	qint64 startedAt = 0;
	qint32 fileFingerprint = 0;
	int serverDate = 0;
};

struct Outgoing {
	Codec::Packet packet;
	QByteArray ciphertext;
	qint64 transportId = 0;
	qint64 keyFingerprint = 0;
	qint32 fileFingerprint = 0;
	QByteArray fileCiphertext;
	QByteArray inputFile;
	QByteArray sentFile;
	bool sent = false;
	bool failed = false;
};

struct Incoming {
	Codec::Packet packet;
	int date = 0;
	QByteArray file;
	qint64 keyFingerprint = 0;
	bool resendHandled = false;
};

struct ReadReceipt {
	int maxDate = 0;
	qint64 date = 0;
	int submittedOut = 0;
	friend bool operator==(const ReadReceipt &, const ReadReceipt &) = default;
};

struct ChatState {
	Chat view;
	qint64 accessHash = 0;
	Crypto::DhState dh;
	int initialGenerator = 0;
	QByteArray initialPrime;
	QByteArray peerPublic;
	QByteArray key;
	qint64 keyFingerprint = 0;
	qint64 initialKeyFingerprint = 0;
	QByteArray oldKey;
	QByteArray candidateKey;
	Crypto::DhState exchangeDh;
	qint64 exchangeId = 0;
	int exchangeStage = 0;
	qint64 completedExchangeId = 0;
	qint64 completedExchangeFingerprint = 0;
	int oldUntilOut = 0;
	bool oldSeenNew = false;
	qint64 keyCreatedAt = 0;
	int keyUsedOut = 0;
	int keyUsedIn = 0;
	int nextOut = 0;
	int submittedOut = 0;
	int nextIn = 0;
	int peerAck = 0;
	int gapEnd = -1;
	int peerLayer = 0;
	bool notified = false;
	bool acceptPending = false;
	bool activationPending = false;
	qint64 pendingFingerprint = 0;
	bool discardPending = false;
	bool discardHistory = false;
	int pendingReadDate = 0;
	int readDate = 0;
	qint64 readAt = 0;
	QVector<ReadReceipt> peerReads;
	QMap<int, Outgoing> outgoing;
	QMap<int, Incoming> incoming;
	QMap<qint64, StoredMessage> messages;
	QVector<qint64> messageOrder;
	QSet<qint64> deleted;
	QVector<int> resendQueue;
};

struct Creation {
	quint64 userId = 0;
	Crypto::DhState dh;
	QByteArray inputUser;
};

struct Snapshot {
	quint64 selfId = 0;
	bool baseline = false;
	int qts = 0;
	bool hasRecovery = false;
	RecoveryState recovery;
	int dhGenerator = 0;
	int dhVersion = 0;
	QByteArray dhPrime;
	QMap<int, ChatState> chats;
	QMap<int, Creation> creations;
	QMap<int, QByteArray> pendingQts;
	QVector<QByteArray> inbox;
};

template <typename Type>
[[nodiscard]] QByteArray serializeTl(const Type &value) {
	auto buffer = mtpBuffer();
	value.write(buffer);
	return QByteArray(reinterpret_cast<const char*>(buffer.data()),
		buffer.size() * sizeof(mtpPrime));
}

template <typename Type>
[[nodiscard]] std::optional<Type> deserializeTl(const QByteArray &bytes) {
	if (bytes.isEmpty() || bytes.size() % sizeof(mtpPrime)) {
		return std::nullopt;
	}
	const auto from = reinterpret_cast<const mtpPrime*>(bytes.constData());
	const auto end = from + bytes.size() / sizeof(mtpPrime);
	auto current = from;
	auto result = Type();
	return result.read(current, end) && current == end
		? std::make_optional(std::move(result))
		: std::nullopt;
}

[[nodiscard]] QByteArray serialize(const Snapshot &state);
[[nodiscard]] std::optional<Snapshot> deserialize(const QByteArray &bytes);
[[nodiscard]] qint64 randomId();
[[nodiscard]] QByteArray randomBytes(int size);
[[nodiscard]] QString addOutgoing(
	ChatState &chat,
	Codec::Packet packet,
	QByteArray fileCiphertext = {});
[[nodiscard]] QString addService(ChatState &chat, Codec::Packet packet);
void insertMessage(ChatState &chat, StoredMessage message);
[[nodiscard]] QByteArray keyFor(const ChatState &chat, qint64 fingerprint);
[[nodiscard]] QString eraseMessages(ChatState &chat, const QVector<qint64> &ids);
[[nodiscard]] QString clearMessages(ChatState &chat, bool includeWaiting = false);
void startTtl(StoredMessage &message, qint64 now);
void applyPeerReads(ChatState &chat, StoredMessage &message);
void applySendResult(Snapshot &state, int chatId, int seq, qint64 transportId,
	int date, const QByteArray &file, const QString &error);
void clearConfirmed(ChatState &chat);
void expire(Snapshot &state, qint64 now);
void discardState(ChatState &chat, QString error);
void applyEncryption(Snapshot &state, const MTPEncryptedChat &value, qint64 now, int creationId = 0);
void applyUpdate(Snapshot &state, const MTPUpdate &update, qint64 now, int readBoundary = -1);
void appendEncrypted(Snapshot &state, const MTPEncryptedMessage &message);
void drainInbox(Snapshot &state, qint64 now);
void drainQts(Snapshot &state);
void maybeRekey(Snapshot &state, ChatState &chat, qint64 now);
void applyKeyAction(Snapshot &state, ChatState &chat, const Incoming &message, qint64 now);
void switchKey(ChatState &chat, qint64 now);
void retireOldKey(ChatState &chat);

} // namespace Extras::SecretChats::Details

namespace Extras::SecretChats {

struct Manager::Private {
	using Snapshot = Details::Snapshot;
	struct Task {
		Fn<QString(Snapshot&)> apply;
		Fn<void(QString)> done;
	};

	Private(not_null<Manager*> owner, not_null<Main::Session*> session);
	void enqueue(Fn<QString(Snapshot&)> apply, Fn<void(QString)> done = {});
	void runNext();
	void publish();
	void pump();
	void pumpHandshakes();
	void pumpSending();
	void pumpReads();
	void acknowledge();
	void tick();
	[[nodiscard]] qint64 now() const;
	void getDh(Fn<void(int, QByteArray, QByteArray, int, QString)> done);
	void finishSend(int chatId, int seq, qint64 transportId,
		int date, QByteArray file, QString error);
	void transmit(int chatId, int seq, bool resend);
	void dispatch(int chatId, int seq, bool resend);
	void cancelTransfers();

	not_null<Manager*> owner;
	not_null<Main::Session*> session;
	std::unique_ptr<Store> store;
	std::unique_ptr<FileTransfer> files;
	std::shared_ptr<const Snapshot> state;
	std::deque<Task> tasks;
	rpl::event_stream<> changes;
	base::Timer timer;
	bool initializing = false;
	bool ready = false;
	bool enabled = false;
	bool busy = false;
	bool stopping = false;
	QString failure;
	QVector<Fn<void(bool)>> initializeCallbacks;
	QMap<int, mtpRequestId> handshakes;
	QMap<int, mtpRequestId> sending;
	QMap<int, mtpRequestId> lastRequests;
	QMap<int, mtpRequestId> reading;
	QMap<int, Fn<void(int, QString)>> createCallbacks;
	QMap<std::pair<int, qint64>, quint64> transfers;
	mtpRequestId dhRequest = 0;
	QVector<Fn<void(int, QByteArray, QByteArray, int, QString)>> dhCallbacks;
	mtpRequestId ackRequest = 0;
	int acknowledgedQts = -1;
	qint64 wallStarted = 0;
	crl::time monotonicStarted = 0;
};

} // namespace Extras::SecretChats
