#include "extras/data/message_archive_store.h"

#include "extras/data/message_archive_crypto.h"
#include "extras/libs/sqlite/sqlite3.h"

#include <openssl/crypto.h>

#include <limits>
#include <mutex>
#include <tuple>
#include <utility>

namespace Database::Archive {
namespace {

constexpr auto kRecordColumns = "fakeId,userId,dialogId,topicId,messageId,payload";
constexpr auto kLegacyColumns =
	"fakeId,userId,dialogId,groupedId,peerId,fromId,topicId,messageId,"
	"date,flags,editDate,views,fwdFlags,fwdFromId,fwdName,fwdDate,"
	"fwdPostAuthor,replyFlags,replyMessageId,replyPeerId,replyTopId,"
	"replyForumTopic,replySerialized,entityCreateDate,text,textEntities,"
	"mediaPath,hqThumbPath,documentType,documentSerialized,thumbsSerialized,"
	"documentAttributesSerialized,mimeType";

auto messageFields(const ExtrasMessageBase &message) {
	return std::tie(
		message.fakeId, message.userId, message.dialogId, message.groupedId,
		message.peerId, message.fromId, message.topicId, message.messageId,
		message.date, message.flags, message.editDate, message.views,
		message.fwdFlags, message.fwdFromId, message.fwdName, message.fwdDate,
		message.fwdPostAuthor, message.postAuthor, message.replyFlags,
		message.replyMessageId, message.replyPeerId, message.replyTopId,
		message.replyForumTopic, message.replySerialized,
		message.replyMarkupSerialized, message.entityCreateDate,
		message.text, message.textEntities, message.mediaPath,
		message.hqThumbPath, message.documentType, message.documentSerialized,
		message.thumbsSerialized, message.documentAttributesSerialized,
		message.mimeType);
}

std::string textColumn(sqlite3_stmt *statement, int column) {
	const auto data = sqlite3_column_text(statement, column);
	const auto size = sqlite3_column_bytes(statement, column);
	return size
		? std::string(reinterpret_cast<const char*>(data), size)
		: std::string();
}

std::vector<char> blobColumn(sqlite3_stmt *statement, int column) {
	const auto data = static_cast<const char*>(
		sqlite3_column_blob(statement, column));
	const auto size = sqlite3_column_bytes(statement, column);
	return size ? std::vector<char>(data, data + size) : std::vector<char>();
}

ExtrasMessageBase legacyMessage(sqlite3_stmt *statement) {
	auto result = ExtrasMessageBase();
	result.fakeId = sqlite3_column_int64(statement, 0);
	result.userId = sqlite3_column_int64(statement, 1);
	result.dialogId = sqlite3_column_int64(statement, 2);
	result.groupedId = sqlite3_column_int64(statement, 3);
	result.peerId = sqlite3_column_int64(statement, 4);
	result.fromId = sqlite3_column_int64(statement, 5);
	result.topicId = sqlite3_column_int64(statement, 6);
	result.messageId = sqlite3_column_int(statement, 7);
	result.date = sqlite3_column_int(statement, 8);
	result.flags = sqlite3_column_int(statement, 9);
	result.editDate = sqlite3_column_int(statement, 10);
	result.views = sqlite3_column_int(statement, 11);
	result.fwdFlags = sqlite3_column_int(statement, 12);
	result.fwdFromId = sqlite3_column_int64(statement, 13);
	result.fwdName = textColumn(statement, 14);
	result.fwdDate = sqlite3_column_int(statement, 15);
	result.fwdPostAuthor = textColumn(statement, 16);
	result.replyFlags = sqlite3_column_int(statement, 17);
	result.replyMessageId = sqlite3_column_int(statement, 18);
	result.replyPeerId = sqlite3_column_int64(statement, 19);
	result.replyTopId = sqlite3_column_int(statement, 20);
	result.replyForumTopic = sqlite3_column_int(statement, 21) != 0;
	result.replySerialized = blobColumn(statement, 22);
	result.entityCreateDate = sqlite3_column_int(statement, 23);
	result.text = textColumn(statement, 24);
	result.textEntities = blobColumn(statement, 25);
	result.mediaPath = textColumn(statement, 26);
	result.hqThumbPath = textColumn(statement, 27);
	result.documentType = sqlite3_column_int(statement, 28);
	result.documentSerialized = blobColumn(statement, 29);
	result.thumbsSerialized = blobColumn(statement, 30);
	result.documentAttributesSerialized = blobColumn(statement, 31);
	result.mimeType = textColumn(statement, 32);
	return result;
}

std::string searchPattern(const std::string &search) {
	auto result = std::string("%");
	result.reserve(search.size() + 2);
	for (const auto ch : search) {
		if (ch == '%' || ch == '_' || ch == '\\') {
			result += '\\';
		}
		result += ch;
	}
	result += '%';
	return result;
}

}

class Store::Private final {
public:
	class Statement final {
	public:
		Statement(Private &owner, const char *sql) : _owner(owner) {
			const auto code = sqlite3_prepare_v2(
				_owner.database, sql, -1, &_statement, nullptr);
			if (code != SQLITE_OK) {
				_owner.fail(u"Could not prepare archive database operation"_q, code);
			}
		}

		~Statement() {
			const auto code = sqlite3_finalize(_statement);
			if (code != SQLITE_OK && _owner.lastError.isEmpty()) {
				_owner.fail(u"Could not finalize archive database operation"_q, code);
			}
		}

		Statement(const Statement &) = delete;
		Statement &operator=(const Statement &) = delete;

		explicit operator bool() const {
			return _statement != nullptr;
		}

		sqlite3_stmt *get() const {
			return _statement;
		}

		bool bind(int index, ID value) {
			return check(sqlite3_bind_int64(_statement, index, value));
		}

		bool bind(int index, const QByteArray &value) {
			return check(sqlite3_bind_blob(
				_statement, index, value.constData(), value.size(), SQLITE_TRANSIENT));
		}

		bool bindText(int index, const char *value) {
			return check(sqlite3_bind_text(
				_statement, index, value, -1, SQLITE_TRANSIENT));
		}

		int step() {
			const auto code = sqlite3_step(_statement);
			if (code != SQLITE_ROW && code != SQLITE_DONE) {
				_owner.fail(u"Could not execute archive database operation"_q, code);
			}
			return code;
		}

	private:
		bool check(int code) {
			return (code == SQLITE_OK)
				|| _owner.fail(u"Could not bind archive database parameters"_q, code);
		}

		Private &_owner;
		sqlite3_stmt *_statement = nullptr;
	};

	class Transaction final {
	public:
		explicit Transaction(Private &owner) : _owner(owner) {
			_active = _owner.execute("BEGIN IMMEDIATE");
		}

		~Transaction() {
			if (!_active || sqlite3_get_autocommit(_owner.database)) {
				return;
			}
			const auto code = sqlite3_exec(
				_owner.database, "ROLLBACK", nullptr, nullptr, nullptr);
			if (code != SQLITE_OK) {
				_owner.fail(u"Could not roll back archive database operation"_q, code);
			}
		}

		explicit operator bool() const {
			return _active;
		}

		bool commit() {
			if (!_owner.execute("COMMIT")) {
				return false;
			}
			_active = false;
			return true;
		}

	private:
		Private &_owner;
		bool _active = false;
	};

	~Private() {
		close();
	}

	bool fail(const QString &message, int code = SQLITE_OK) {
		lastError = code == SQLITE_OK
			? message
			: message + u" (error code %1)"_q.arg(code);
		return false;
	}

	void close() {
		ready = false;
		if (context) {
			OPENSSL_cleanse(context->key.data(), context->key.size());
			context.reset();
		}
		if (database) {
			const auto code = sqlite3_close_v2(database);
			if (code != SQLITE_OK) {
				fail(u"Could not close archive database"_q, code);
			}
			database = nullptr;
		}
	}

	bool checkReady() {
		if (!ready) {
			if (lastError.isEmpty()) {
				fail(u"Message archive is locked"_q);
			}
			return false;
		}
		lastError.clear();
		return true;
	}

	bool execute(const char *sql) {
		const auto code = sqlite3_exec(database, sql, nullptr, nullptr, nullptr);
		return (code == SQLITE_OK)
			|| fail(u"Could not execute archive database operation"_q, code);
	}

	std::optional<bool> tableExists(const char *name) {
		auto statement = Statement(*this,
			"SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1");
		if (!statement || !statement.bindText(1, name)) {
			return std::nullopt;
		}
		const auto code = statement.step();
		if (code != SQLITE_ROW && code != SQLITE_DONE) {
			return std::nullopt;
		}
		return code == SQLITE_ROW;
	}

	bool createSchema() {
		return execute("PRAGMA secure_delete=ON")
			&& execute("PRAGMA temp_store=MEMORY")
			&& execute("PRAGMA journal_size_limit=0")
			&& execute(
				"CREATE TABLE IF NOT EXISTS EncryptedMessages("
				"keyId BLOB NOT NULL,edited INTEGER NOT NULL,fakeId INTEGER NOT NULL,"
				"userId INTEGER NOT NULL,dialogId INTEGER NOT NULL,"
				"topicId INTEGER NOT NULL,messageId INTEGER NOT NULL,"
				"payload BLOB NOT NULL,PRIMARY KEY(keyId,edited,fakeId))")
			&& execute(
				"CREATE INDEX IF NOT EXISTS idx_archive_deleted_page ON "
				"EncryptedMessages(keyId,edited,userId,dialogId,messageId DESC)")
			&& execute(
				"CREATE INDEX IF NOT EXISTS idx_archive_deleted_topic_page ON "
				"EncryptedMessages(keyId,edited,userId,dialogId,topicId,messageId DESC)")
			&& execute(
				"CREATE INDEX IF NOT EXISTS idx_archive_edited_page ON "
				"EncryptedMessages(keyId,edited,userId,dialogId,messageId,fakeId DESC)")
			&& execute(
				"CREATE INDEX IF NOT EXISTS idx_archive_sequence ON "
				"EncryptedMessages(fakeId DESC)")
			&& execute(
				"CREATE TABLE IF NOT EXISTS MessageArchiveState("
				"id INTEGER PRIMARY KEY CHECK(id=1),cleanupPending INTEGER NOT NULL)");
	}

	std::optional<ExtrasMessageBase> readRecord(
			sqlite3_stmt *statement,
			bool edited) {
		for (auto column = 0; column != 5; ++column) {
			if (sqlite3_column_type(statement, column) != SQLITE_INTEGER) {
				ready = false;
				fail(u"Message archive index format is corrupt"_q);
				return std::nullopt;
			}
		}
		const auto messageId = sqlite3_column_int64(statement, 4);
		if (messageId < std::numeric_limits<int>::min()
			|| messageId > std::numeric_limits<int>::max()) {
			ready = false;
			fail(u"Message archive index ID is corrupt"_q);
			return std::nullopt;
		}
		auto index = ExtrasMessageBase();
		index.fakeId = sqlite3_column_int64(statement, 0);
		index.userId = sqlite3_column_int64(statement, 1);
		index.dialogId = sqlite3_column_int64(statement, 2);
		index.topicId = sqlite3_column_int64(statement, 3);
		index.messageId = int(messageId);
		const auto bytes = static_cast<const char*>(
			sqlite3_column_blob(statement, 5));
		const auto size = sqlite3_column_bytes(statement, 5);
		if (sqlite3_errcode(database) == SQLITE_NOMEM) {
			fail(u"Not enough memory to read message archive"_q, SQLITE_NOMEM);
			return std::nullopt;
		}
		const auto payload = QByteArray(bytes, size);
		auto result = ArchiveCrypto::decrypt(*context, payload, index, edited);
		if (!result) {
			ready = false;
			fail(u"Message archive authentication failed; reading stopped"_q);
		}
		return result;
	}

	bool insert(const ExtrasMessageBase &message, bool edited) {
		const auto payload = ArchiveCrypto::encrypt(*context, message, edited);
		if (!payload) {
			return fail(u"Message archive encryption failed"_q);
		}
		auto statement = Statement(*this,
			"INSERT INTO EncryptedMessages("
			"keyId,edited,fakeId,userId,dialogId,topicId,messageId,payload) "
			"VALUES(?1,?2,?3,?4,?5,?6,?7,?8)");
		return statement
			&& statement.bind(1, context->id)
			&& statement.bind(2, ID(edited))
			&& statement.bind(3, message.fakeId)
			&& statement.bind(4, message.userId)
			&& statement.bind(5, message.dialogId)
			&& statement.bind(6, message.topicId)
			&& statement.bind(7, message.messageId)
			&& statement.bind(8, *payload)
			&& statement.step() == SQLITE_DONE;
	}

	bool verifyInserted(const ExtrasMessageBase &message, bool edited) {
		const auto sql = std::string("SELECT ") + kRecordColumns
			+ " FROM EncryptedMessages WHERE keyId=?1 AND edited=?2 AND fakeId=?3";
		auto statement = Statement(*this, sql.c_str());
		if (!statement
			|| !statement.bind(1, context->id)
			|| !statement.bind(2, ID(edited))
			|| !statement.bind(3, message.fakeId)) {
			return false;
		}
		const auto code = statement.step();
		if (code != SQLITE_ROW && code != SQLITE_DONE) {
			return false;
		}
		if (code == SQLITE_DONE) {
			return fail(u"Migrated message archive is missing"_q);
		}
		const auto restored = readRecord(statement.get(), edited);
		if (!restored) {
			return false;
		}
		return messageFields(message) == messageFields(*restored)
			|| fail(u"Migrated message archive validation failed"_q);
	}

	bool migrateTable(const char *table, bool edited, bool &migrated) {
		const auto sql = std::string("SELECT ") + kLegacyColumns + " FROM " + table;
		auto statement = Statement(*this, sql.c_str());
		if (!statement) {
			return false;
		}
		while (true) {
			const auto code = statement.step();
			if (code == SQLITE_DONE) {
				return true;
			}
			if (code != SQLITE_ROW) {
				return false;
			}
			const auto message = legacyMessage(statement.get());
			if (sqlite3_errcode(database) == SQLITE_NOMEM) {
				return fail(u"Not enough memory to read legacy message archive"_q, SQLITE_NOMEM);
			}
			if (!insert(message, edited) || !verifyInserted(message, edited)) {
				return false;
			}
			migrated = true;
		}
	}

	bool setCleanupPending(bool value) {
		auto statement = Statement(*this,
			"INSERT INTO MessageArchiveState(id,cleanupPending) VALUES(1,?1) "
			"ON CONFLICT(id) DO UPDATE SET cleanupPending=excluded.cleanupPending");
		return statement && statement.bind(1, ID(value))
			&& statement.step() == SQLITE_DONE;
	}

	bool checkpoint() {
		const auto code = sqlite3_wal_checkpoint_v2(
			database, nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
		return (code == SQLITE_OK)
			|| fail(u"Could not checkpoint archive database journal"_q, code);
	}

	bool cleanPlaintext() {
		return checkpoint()
			&& execute("VACUUM")
			&& checkpoint()
			&& setCleanupPending(false);
	}

	bool migrate() {
		auto transaction = Transaction(*this);
		if (!transaction) {
			return false;
		}
		const auto deleted = tableExists("DeletedMessage");
		const auto edited = tableExists("EditedMessage");
		if (!deleted || !edited) {
			return false;
		}
		auto cleanupPending = false;
		{
			auto statement = Statement(*this,
				"SELECT cleanupPending FROM MessageArchiveState WHERE id=1");
			if (!statement) {
				return false;
			}
			const auto code = statement.step();
			if (code != SQLITE_ROW && code != SQLITE_DONE) {
				return false;
			}
			cleanupPending = (code == SQLITE_ROW)
				? sqlite3_column_int(statement.get(), 0) != 0
				: (*deleted || *edited);
		}
		auto migrated = false;
		if ((*deleted && !migrateTable("DeletedMessage", false, migrated))
			|| (*edited && !migrateTable("EditedMessage", true, migrated))) {
			return false;
		}
		cleanupPending = cleanupPending || migrated;
		// 旧表和密文必须在同一事务内切换，失败时仍能恢复完整留档。
		if ((*deleted && !execute("DELETE FROM DeletedMessage"))
			|| (*edited && !execute("DELETE FROM EditedMessage"))
			|| !setCleanupPending(cleanupPending)
			|| !transaction.commit()) {
			return false;
		}
		return !cleanupPending || cleanPlaintext();
	}

	std::vector<ExtrasMessageBase> get(
			bool edited,
			ID userId,
			ID dialogId,
			ID topicId,
			ID messageId,
			ID minId,
			ID maxId,
			int limit,
			const std::string &search) {
		if (!checkReady() || limit == 0) {
			return {};
		}
		const auto pageColumn = edited ? "fakeId" : "messageId";
		const auto sql = std::string("SELECT ") + kRecordColumns
			+ " FROM EncryptedMessages WHERE keyId=?1 AND edited=?2"
			" AND userId=?3 AND dialogId=?4"
			+ (edited ? " AND messageId=?5" : " AND (topicId=?5 OR ?5=0)")
			+ " AND (" + pageColumn + ">?6 OR ?6=0)"
			+ " AND (" + pageColumn + "<?7 OR ?7=0)"
			+ " ORDER BY " + pageColumn + " DESC";
		auto statement = Statement(*this, sql.c_str());
		if (!statement
			|| !statement.bind(1, context->id)
			|| !statement.bind(2, ID(edited))
			|| !statement.bind(3, userId)
			|| !statement.bind(4, dialogId)
			|| !statement.bind(5, edited ? messageId : topicId)
			|| !statement.bind(6, minId)
			|| !statement.bind(7, maxId)) {
			return {};
		}
		const auto pattern = search.empty() ? std::string() : searchPattern(search);
		auto result = std::vector<ExtrasMessageBase>();
		while (true) {
			const auto code = statement.step();
			if (code == SQLITE_DONE) {
				return result;
			}
			if (code != SQLITE_ROW) {
				return {};
			}
			auto message = readRecord(statement.get(), edited);
			if (!message) {
				return {};
			}
			if (!pattern.empty()
				&& sqlite3_strlike(pattern.c_str(), message->text.c_str(), '\\') != 0) {
				continue;
			}
			result.push_back(std::move(*message));
			if (limit > 0 && result.size() >= size_t(limit)) {
				return result;
			}
		}
	}

	std::mutex mutex;
	sqlite3 *database = nullptr;
	std::optional<ArchiveCrypto::Context> context;
	QString lastError;
	bool ready = false;
};

Store::Store() : _private(std::make_unique<Private>()) {
}

Store::~Store() = default;

bool Store::unlock(const QString &databasePath, bytes::const_span localKey) {
	const auto lock = std::lock_guard(_private->mutex);
	_private->close();
	_private->lastError.clear();
	if (localKey.empty()) {
		return _private->fail(u"Message archive has no local master key"_q);
	}
	const auto path = databasePath.toUtf8();
	const auto code = sqlite3_open_v2(
		path.constData(),
		&_private->database,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
		nullptr);
	if (code != SQLITE_OK) {
		return _private->fail(u"Could not open archive database"_q, code);
	}
	_private->context.emplace(ArchiveCrypto::makeContext(localKey));
	if (!_private->createSchema() || !_private->migrate()) {
		return false;
	}
	_private->ready = true;
	return true;
}

void Store::close() {
	const auto lock = std::lock_guard(_private->mutex);
	_private->close();
}

bool Store::available() const {
	const auto lock = std::lock_guard(_private->mutex);
	return _private->ready;
}

QString Store::error() const {
	const auto lock = std::lock_guard(_private->mutex);
	return _private->lastError;
}

bool Store::hasMessages() {
	const auto lock = std::lock_guard(_private->mutex);
	if (!_private->checkReady()) {
		return true;
	}
	auto statement = Private::Statement(*_private,
		"SELECT 1 FROM EncryptedMessages WHERE keyId=?1 LIMIT 1");
	if (!statement || !statement.bind(1, _private->context->id)) {
		return true;
	}
	return statement.step() != SQLITE_DONE;
}

bool Store::add(const ExtrasMessageBase &message, bool edited) {
	const auto lock = std::lock_guard(_private->mutex);
	if (!_private->checkReady()) {
		return false;
	}
	auto transaction = Private::Transaction(*_private);
	if (!transaction) {
		return false;
	}
	auto record = message;
	{
		auto statement = Private::Statement(*_private,
			"SELECT COALESCE(MAX(fakeId),0) FROM EncryptedMessages");
		if (!statement || statement.step() != SQLITE_ROW) {
			return false;
		}
		const auto latest = sqlite3_column_int64(statement.get(), 0);
		if (latest == std::numeric_limits<ID>::max()) {
			return _private->fail(u"Message archive IDs are exhausted"_q);
		}
		record.fakeId = latest + 1;
	}
	return _private->insert(record, edited) && transaction.commit();
}

std::vector<ExtrasMessageBase> Store::get(
		bool edited,
		ID userId,
		ID dialogId,
		ID topicId,
		ID messageId,
		ID minId,
		ID maxId,
		int limit,
		const std::string &search,
		QString *queryError) {
	const auto lock = std::lock_guard(_private->mutex);
	auto result = _private->get(
		edited, userId, dialogId, topicId, messageId, minId, maxId, limit, search);
	if (queryError) {
		*queryError = _private->lastError;
	}
	return result;
}

std::vector<ID> Store::deletedIds(ID userId, ID dialogId, ID topicId) {
	const auto lock = std::lock_guard(_private->mutex);
	if (!_private->checkReady()) {
		return {};
	}
	const auto sql = std::string("SELECT ") + kRecordColumns
		+ " FROM EncryptedMessages WHERE keyId=?1 AND edited=0 "
		"AND userId=?2 AND dialogId=?3 AND (topicId=?4 OR ?4=0)";
	auto statement = Private::Statement(*_private, sql.c_str());
	if (!statement
		|| !statement.bind(1, _private->context->id)
		|| !statement.bind(2, userId)
		|| !statement.bind(3, dialogId)
		|| !statement.bind(4, topicId)) {
		return {};
	}
	auto result = std::vector<ID>();
	while (true) {
		const auto code = statement.step();
		if (code == SQLITE_DONE) {
			return result;
		}
		if (code != SQLITE_ROW) {
			return {};
		}
		const auto message = _private->readRecord(statement.get(), false);
		if (!message) {
			return {};
		}
		result.push_back(message->messageId);
	}
}

bool Store::contains(
		bool edited,
		ID userId,
		ID dialogId,
		ID topicId,
		ID messageId) {
	const auto lock = std::lock_guard(_private->mutex);
	return !_private->get(
		edited, userId, dialogId, topicId, messageId, 0, 0, 1, {}).empty();
}

bool Store::remove(ID userId, ID dialogId, ID messageId) {
	const auto lock = std::lock_guard(_private->mutex);
	if (!_private->checkReady()) {
		return false;
	}
	auto statement = Private::Statement(*_private,
		"DELETE FROM EncryptedMessages WHERE keyId=?1 AND edited=0 "
		"AND userId=?2 AND dialogId=?3 AND messageId=?4");
	return statement
		&& statement.bind(1, _private->context->id)
		&& statement.bind(2, userId)
		&& statement.bind(3, dialogId)
		&& statement.bind(4, messageId)
		&& statement.step() == SQLITE_DONE;
}

bool Store::clear(ID userId, ID dialogId, ID topicId) {
	const auto lock = std::lock_guard(_private->mutex);
	if (!_private->checkReady()) {
		return false;
	}
	auto statement = Private::Statement(*_private,
		"DELETE FROM EncryptedMessages WHERE keyId=?1 AND edited=0 "
		"AND userId=?2 AND dialogId=?3 AND (topicId=?4 OR ?4=0)");
	return statement
		&& statement.bind(1, _private->context->id)
		&& statement.bind(2, userId)
		&& statement.bind(3, dialogId)
		&& statement.bind(4, topicId)
		&& statement.step() == SQLITE_DONE;
}

}
