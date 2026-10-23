#pragma once

#include "base/weak_ptr.h"
#include "data/data_peer_id.h"
#include "mtproto/sender.h"
#include "rpl/event_stream.h"

#include <QtCore/QVector>
#include <optional>

namespace Main {
class Session;
} // namespace Main
class UserData;

namespace Extras::SecretChats {

enum class State { Waiting, Requested, Active, Discarded };
enum class Delivery { Pending, Sent, Failed, Read };

struct Document {
	QString fileName;
	QString mimeType;
	qint64 size = 0;
};

struct Chat {
	int id = 0;
	UserId userId;
	State state = State::Waiting;
	bool creator = false;
	int ttl = 0;
	int layer = 73;
	QByteArray verification;
	QString error;
};

struct Message {
	qint64 id = 0;
	int date = 0;
	bool outgoing = false;
	QString text;
	qint64 replyTo = 0;
	Delivery delivery = Delivery::Pending;
	int ttl = 0;
	qint64 expiresAt = 0;
	std::optional<Document> document;
	bool unsupported = false;
	bool screenshot = false;
};

struct Draft {
	QString text;
	qint64 replyTo = 0;
	friend bool operator==(const Draft &, const Draft &) = default;
};

struct RecoveryState {
	int pts = 0;
	int date = 0;
	int seq = 0;
};

class Manager final : public MTP::Sender, public base::has_weak_ptr {
public:
	explicit Manager(not_null<Main::Session*> session);
	~Manager();

	void initialize(Fn<void(bool)> done);
	[[nodiscard]] bool available() const;
	[[nodiscard]] QString error() const;
	[[nodiscard]] bool hasBaseline() const;
	[[nodiscard]] int committedQts() const;
	[[nodiscard]] qint64 currentTime() const;
	[[nodiscard]] std::optional<RecoveryState> recoveryState() const;
	void persistRecovery(RecoveryState state, Fn<void(bool)> done = {});
	void ensureBaseline(int qts, Fn<void(bool)> done,
		std::optional<RecoveryState> recovery = std::nullopt);
	void persistDifference(
		QVector<MTPEncryptedMessage> messages,
		QVector<MTPUpdate> updates,
		int qts,
		Fn<void(bool)> done,
		std::optional<RecoveryState> recovery = std::nullopt);
	void update(const MTPUpdate &update);

	[[nodiscard]] QVector<Chat> chats() const;
	[[nodiscard]] std::optional<Chat> chat(int chatId) const;
	[[nodiscard]] QVector<Message> messages(int chatId) const;
	[[nodiscard]] Draft draft(int chatId) const;
	void setDraft(int chatId, Draft draft, Fn<void(QString)> done = {});
	[[nodiscard]] rpl::producer<> changed() const;

	void create(not_null<UserData*> user, Fn<void(int, QString)> done);
	void send(int chatId, QString text, Fn<void(qint64, QString)> done = {});
	void sendReply(int chatId, QString text, qint64 replyTo,
		Fn<void(qint64, QString)> done = {});
	void sendDocument(
		int chatId,
		QString fileName,
		QString mimeType,
		QByteArray bytes,
		QString caption = {},
		Fn<void(qint64, QString)> done = {});
	void downloadDocument(
		int chatId,
		qint64 messageId,
		Fn<void(QByteArray, QString)> done);
	void deleteMessages(
		int chatId,
		QVector<qint64> ids,
		Fn<void(QString)> done = {});
	void clear(int chatId, Fn<void(QString)> done = {});
	void discard(int chatId, bool deleteHistory, Fn<void(QString)> done = {});
	void setTtl(int chatId, int seconds, Fn<void(QString)> done = {});
	void read(int chatId, int maxDate, Fn<void(QString)> done = {});
	void retry(int chatId, qint64 messageId, Fn<void(QString)> done = {});
	void logout(Fn<void(bool)> done = {});

private:
	struct Private;
	std::unique_ptr<Private> _private;
};

} // namespace Extras::SecretChats
