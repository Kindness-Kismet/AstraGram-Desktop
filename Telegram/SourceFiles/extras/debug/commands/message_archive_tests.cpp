#ifdef _DEBUG
#include "extras/debug/commands/message_archive_tests.h"

#include "extras/debug/commands/commands_internal.h"
#include "extras/debug/debug_login.h"
#include "extras/data/message_archive_crypto.h"
#include "extras/data/message_archive_store.h"
#include "extras/libs/sqlite/sqlite3.h"
#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <algorithm>
#include <array>
#include <memory>
#include <set>
#include <tuple>
#include <type_traits>

namespace ExtrasDebug::Commands {
namespace {

using Store = Database::Archive::Store;
namespace Crypto = Database::ArchiveCrypto;
using Json = nlohmann::json;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
constexpr auto kUser = ID(1001);
constexpr auto kDialog = ID(-2002);
constexpr auto kTopic = ID(11);

class TestDatabase final {
public:
	explicit TestDatabase(const QString &path) {
		const auto utf8 = path.toUtf8();
		_valid = sqlite3_open_v2(utf8.constData(), &_database,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK;
	}

	~TestDatabase() {
		close();
	}

	void close() {
		if (_database) {
			sqlite3_close_v2(_database);
			_database = nullptr;
		}
		_valid = false;
	}

	[[nodiscard]] bool execute(const QByteArray &sql) {
		return _valid && sqlite3_exec(_database, sql.constData(),
			nullptr, nullptr, nullptr) == SQLITE_OK;
	}

	[[nodiscard]] Statement prepare(const QByteArray &sql) {
		auto result = static_cast<sqlite3_stmt*>(nullptr);
		if (!_valid || sqlite3_prepare_v2(_database, sql.constData(),
				-1, &result, nullptr) != SQLITE_OK) {
			sqlite3_finalize(result);
			return Statement(nullptr, sqlite3_finalize);
		}
		return Statement(result, sqlite3_finalize);
	}

	[[nodiscard]] std::optional<qint64> scalar(const QByteArray &sql) {
		const auto statement = prepare(sql);
		if (!statement || sqlite3_step(statement.get()) != SQLITE_ROW) {
			return std::nullopt;
		}
		return sqlite3_column_int64(statement.get(), 0);
	}

	[[nodiscard]] std::optional<QByteArray> bytes(const QByteArray &sql) {
		const auto statement = prepare(sql);
		if (!statement || sqlite3_step(statement.get()) != SQLITE_ROW) {
			return std::nullopt;
		}
		return QByteArray(static_cast<const char*>(
			sqlite3_column_blob(statement.get(), 0)),
			sqlite3_column_bytes(statement.get(), 0));
	}

	[[nodiscard]] bool bindBlob(const QByteArray &sql, const QByteArray &data) {
		const auto statement = prepare(sql);
		return statement && sqlite3_bind_blob(statement.get(), 1,
			data.constData(), data.size(), SQLITE_TRANSIENT) == SQLITE_OK
			&& sqlite3_step(statement.get()) == SQLITE_DONE;
	}

private:
	sqlite3 *_database = nullptr;
	bool _valid = false;
};

auto fields(const ExtrasMessageBase &m) {
	return std::tie(m.fakeId, m.userId, m.dialogId, m.groupedId, m.peerId,
		m.fromId, m.topicId, m.messageId, m.date, m.flags, m.editDate, m.views,
		m.fwdFlags, m.fwdFromId, m.fwdName, m.fwdDate, m.fwdPostAuthor,
		m.postAuthor, m.replyFlags, m.replyMessageId, m.replyPeerId, m.replyTopId,
		m.replyForumTopic, m.replySerialized, m.replyMarkupSerialized,
		m.entityCreateDate, m.text, m.textEntities, m.mediaPath, m.hqThumbPath,
		m.documentType, m.documentSerialized, m.thumbsSerialized,
		m.documentAttributesSerialized, m.mimeType);
}

ExtrasMessageBase sample(int messageId) {
	auto m = ExtrasMessageBase();
	m.fakeId = 777;
	m.userId = kUser;
	m.dialogId = kDialog;
	m.groupedId = 0x123456789LL;
	m.peerId = 0x123456788LL;
	m.fromId = 0x123456787LL;
	m.topicId = kTopic;
	m.messageId = messageId;
	m.date = 1711111111;
	m.flags = 312;
	m.editDate = 1711111222;
	m.views = 48;
	m.fwdFlags = 17;
	m.fwdFromId = 0x123456786LL;
	m.fwdName = u"转发来源🔐"_q.toStdString();
	m.fwdDate = 1711111000;
	m.fwdPostAuthor = u"原作者"_q.toStdString();
	m.postAuthor = u"当前作者"_q.toStdString();
	m.replyFlags = 43;
	m.replyMessageId = 20;
	m.replyPeerId = 0x123456785LL;
	m.replyTopId = 10;
	m.replyForumTopic = true;
	m.replySerialized = { 'r', '\0', char(0xFF), 'q' };
	m.replyMarkupSerialized = { '\0', 'b', '\0' };
	m.entityCreateDate = 1711111333;
	m.text = u"留档正文🔐，包含中文和表情。"_q.toStdString();
	m.textEntities = { 'e', '\0', char(0xFE) };
	m.mediaPath = u"synthetic/附件.txt"_q.toStdString();
	m.hqThumbPath = u"synthetic/缩略图.png"_q.toStdString();
	m.documentType = 4;
	m.documentSerialized = { 'd', '\0', 'o', 'c' };
	m.thumbsSerialized = { 't', '\0', 'h' };
	m.documentAttributesSerialized = { 'a', '\0', 't' };
	m.mimeType = "application/octet-stream";
	return m;
}

QString cryptoChecks(bytes::const_span key) {
	const auto context = Crypto::makeContext(key);
	const auto message = sample(80);
	if (context.id.size() != 16 || Crypto::makeContext(key).id != context.id) {
		return u"archive key identity is not stable"_q;
	}
	for (const auto edited : { false, true }) {
		const auto encrypted = Crypto::encrypt(context, message, edited);
		const auto another = Crypto::encrypt(context, message, edited);
		if (!encrypted || !another || *encrypted == *another
			|| encrypted->contains(QByteArray::fromStdString(message.text))) {
			return u"archive encryption failed or reused plaintext/nonce"_q;
		}
		const auto restored = Crypto::decrypt(context, *encrypted, message, edited);
		if (!restored || fields(*restored) != fields(message)) {
			return u"archive full-field roundtrip failed"_q;
		}
		auto wrongKey = context;
		wrongKey.key[0] ^= 0x80;
		if (Crypto::decrypt(wrongKey, *encrypted, message, edited)
			|| Crypto::decrypt(context, *encrypted, message, !edited)) {
			return u"archive accepted the wrong key or message kind"_q;
		}
		for (const auto position : { 0, 4, 16, encrypted->size() - 1 }) {
			auto damaged = *encrypted;
			damaged[position] = char(damaged.at(position) ^ 1);
			if (Crypto::decrypt(context, damaged, message, edited)) {
				return u"archive accepted a modified ciphertext"_q;
			}
		}
		for (const auto length : { 0, 3, 16, encrypted->size() - 16,
				encrypted->size() - 1 }) {
			if (Crypto::decrypt(context, encrypted->left(length), message, edited)) {
				return u"archive accepted a truncated ciphertext"_q;
			}
		}
		auto altered = std::array<ExtrasMessageBase, 5>{
			message, message, message, message, message };
		++altered[0].fakeId;
		++altered[1].userId;
		++altered[2].dialogId;
		++altered[3].topicId;
		++altered[4].messageId;
		for (const auto &index : altered) {
			if (Crypto::decrypt(context, *encrypted, index, edited)) {
				return u"archive accepted modified record ownership"_q;
			}
		}
	}
	auto empty = sample(81);
	empty.text.clear();
	empty.fwdName.clear();
	empty.postAuthor.clear();
	empty.replySerialized.clear();
	empty.replyMarkupSerialized.clear();
	empty.textEntities.clear();
	empty.mediaPath.clear();
	empty.hqThumbPath.clear();
	empty.documentSerialized.clear();
	empty.thumbsSerialized.clear();
	empty.documentAttributesSerialized.clear();
	empty.mimeType.clear();
	empty.replyForumTopic = false;
	const auto payload = Crypto::encrypt(context, empty, false);
	const auto restored = payload
		? Crypto::decrypt(context, *payload, empty, false)
		: std::nullopt;
	if (!restored || fields(*restored) != fields(empty)) {
		return u"archive empty-field roundtrip failed"_q;
	}
	return {};
}

bool messageIds(const std::vector<ExtrasMessageBase> &rows,
		std::initializer_list<int> expected) {
	return rows.size() == expected.size()
		&& std::equal(rows.begin(), rows.end(), expected.begin(),
			[](const auto &row, int id) { return row.messageId == id; });
}

QString storeChecks(const QString &path, bytes::const_span key) {
	auto store = Store();
	if (!store.unlock(path, key) || !store.available() || store.hasMessages()) {
		return u"fresh archive store initialization failed"_q;
	}
	for (auto id = 1; id <= 80; ++id) {
		auto message = sample(id);
		message.text = (id <= 10) ? u"目标内容"_q.toStdString() : "filler";
		if (!store.add(message, false)) {
			return u"fresh archive insert failed"_q;
		}
	}
	auto otherUser = sample(901);
	++otherUser.userId;
	auto otherTopic = sample(902);
	++otherTopic.topicId;
	auto otherDialog = sample(903);
	++otherDialog.dialogId;
	auto edit = sample(45);
	if (!store.add(otherUser, false) || !store.add(otherTopic, false)
		|| !store.add(otherDialog, false) || !store.add(edit, true)) {
		return u"archive isolation samples could not be inserted"_q;
	}
	edit.text = "second revision";
	if (!store.add(edit, true) || !store.hasMessages()
		|| !store.contains(false, kUser, kDialog, kTopic)
		|| !store.contains(true, kUser, kDialog, 0, 45)
		|| store.contains(false, kUser + 2, kDialog, kTopic)) {
		return u"archive existence or kind isolation failed"_q;
	}
	const auto ids = store.deletedIds(kUser, kDialog, kTopic);
	const auto unique = std::set<ID>(ids.begin(), ids.end());
	if (ids.size() != 80 || unique.size() != 80
		|| *unique.begin() != 1 || *unique.rbegin() != 80) {
		return u"archive account/topic isolation failed"_q;
	}
	const auto first = store.get(false, kUser, kDialog, kTopic, 0, 0, 0, 1);
	auto expected = sample(80);
	expected.fakeId = 80;
	expected.text = "filler";
	if (first.size() != 1 || fields(first[0]) != fields(expected)) {
		return u"archive stored full-field roundtrip failed"_q;
	}
	if (!messageIds(store.get(false, kUser, kDialog, kTopic, 0, 10, 15, 2), {14, 13})
		|| !messageIds(store.get(false, kUser, kDialog, kTopic, 0, 10, 13, 2), {12, 11})
		|| !messageIds(store.get(false, kUser, kDialog, kTopic, 0, 0, 0, 3,
			u"目标"_q.toStdString()), {10, 9, 8})
		|| !messageIds(store.get(false, kUser, kDialog, kTopic, 0, 0, 8, 3,
			u"目标"_q.toStdString()), {7, 6, 5})) {
		return u"archive ordering, bounds, or search pagination failed"_q;
	}
	const auto edits = store.get(true, kUser, kDialog, 0, 45, 0, 0, 10);
	if (edits.size() != 2 || edits[0].fakeId <= edits[1].fakeId
		|| edits[0].text != "second revision") {
		return u"archive revision order failed"_q;
	}
	const auto older = store.get(true, kUser, kDialog, 0, 45, 0, edits[0].fakeId, 10);
	if (older.size() != 1 || older[0].fakeId != edits[1].fakeId
		|| !store.get(true, kUser, kDialog, 0, 45,
			edits[1].fakeId, edits[0].fakeId, 10).empty()) {
		return u"archive revision bounds failed"_q;
	}
	const auto literals = std::array<std::string, 4>{
		"literal 100% rate", "literal under_score", "literal C:\\archive", "plain text" };
	for (auto i = 0; i != int(literals.size()); ++i) {
		auto message = sample(81 + i);
		message.text = literals[i];
		if (!store.add(message, false)) {
			return u"archive search literal insert failed"_q;
		}
	}
	if (!messageIds(store.get(false, kUser, kDialog, kTopic, 0, 0, 0, 10, "%"), {81})
		|| !messageIds(store.get(false, kUser, kDialog, kTopic, 0, 0, 0, 10, "_"), {82})
		|| !messageIds(store.get(false, kUser, kDialog, kTopic, 0, 0, 0, 10, "\\"), {83})
		|| !messageIds(store.get(false, kUser, kDialog, kTopic, 0, 0, 0, 10,
			"PLAIN TEXT"), {84})) {
		return u"archive search escaping or ASCII case matching failed"_q;
	}
	store.close();
	if (store.available() || !store.unlock(path, key)
		|| store.deletedIds(kUser, kDialog, kTopic).size() != 84) {
		return u"archive close/reopen durability failed"_q;
	}
	store.close();
	auto sql = TestDatabase(path);
	const auto payload = sql.bytes("SELECT payload FROM EncryptedMessages "
		"WHERE edited=0 AND messageId=80");
	const auto count = sql.scalar("SELECT COUNT(*) FROM EncryptedMessages");
	auto changedKey = bytes::make_vector(key);
	changedKey[0] = gsl::byte(gsl::to_integer<unsigned char>(changedKey[0]) ^ 1);
	if (!payload || !count || !store.unlock(path, changedKey)
		|| !store.get(false, kUser, kDialog, kTopic, 0, 0, 0, 100).empty()
		|| store.hasMessages()
		|| sql.scalar("SELECT COUNT(*) FROM EncryptedMessages") != count
		|| sql.bytes("SELECT payload FROM EncryptedMessages "
			"WHERE edited=0 AND messageId=80") != payload) {
		return u"archive key replacement exposed or destroyed old rows"_q;
	}
	store.close();
	if (!store.unlock(path, key) || store.deletedIds(kUser, kDialog, kTopic).size() != 84
		|| !store.remove(kUser, kDialog, 80)
		|| !store.get(false, kUser, kDialog, kTopic, 0, 79, 81, 100).empty()
		|| !store.clear(kUser, kDialog, kTopic)
		|| !store.deletedIds(kUser, kDialog, kTopic).empty()
		|| !store.contains(false, kUser, kDialog, kTopic + 1)
		|| !store.contains(false, kUser + 1, kDialog, kTopic)
		|| !store.contains(true, kUser, kDialog, 0, 45)) {
		return u"archive remove/clear isolation failed"_q;
	}
	return {};
}

auto legacyFields(const ExtrasMessageBase &m) {
	return std::tie(m.fakeId, m.userId, m.dialogId, m.groupedId, m.peerId,
		m.fromId, m.topicId, m.messageId, m.date, m.flags, m.editDate, m.views,
		m.fwdFlags, m.fwdFromId, m.fwdName, m.fwdDate, m.fwdPostAuthor,
		m.replyFlags, m.replyMessageId, m.replyPeerId, m.replyTopId,
		m.replyForumTopic, m.replySerialized, m.entityCreateDate, m.text,
		m.textEntities, m.mediaPath, m.hqThumbPath, m.documentType,
		m.documentSerialized, m.thumbsSerialized, m.documentAttributesSerialized,
		m.mimeType);
}

bool createLegacyTable(TestDatabase &database, const char *name) {
	return database.execute(QByteArray("CREATE TABLE ") + name + "("
		"fakeId INTEGER PRIMARY KEY AUTOINCREMENT,userId INTEGER,dialogId INTEGER,"
		"groupedId INTEGER,peerId INTEGER,fromId INTEGER,topicId INTEGER,"
		"messageId INTEGER,date INTEGER,flags INTEGER,editDate INTEGER,views INTEGER,"
		"fwdFlags INTEGER,fwdFromId INTEGER,fwdName TEXT,fwdDate INTEGER,"
		"fwdPostAuthor TEXT,replyFlags INTEGER,replyMessageId INTEGER,"
		"replyPeerId INTEGER,replyTopId INTEGER,replyForumTopic INTEGER,"
		"replySerialized BLOB,entityCreateDate INTEGER,text TEXT,textEntities BLOB,"
		"mediaPath TEXT,hqThumbPath TEXT,documentType INTEGER,documentSerialized BLOB,"
		"thumbsSerialized BLOB,documentAttributesSerialized BLOB,mimeType TEXT)");
}

template <typename Value>
bool bindLegacy(sqlite3_stmt *statement, int index, const Value &value) {
	if constexpr (std::is_integral_v<Value>) {
		return sqlite3_bind_int64(statement, index, value) == SQLITE_OK;
	} else if constexpr (std::is_same_v<Value, std::string>) {
		return sqlite3_bind_text(statement, index, value.data(), int(value.size()),
			SQLITE_TRANSIENT) == SQLITE_OK;
	} else {
		return sqlite3_bind_blob(statement, index,
			value.empty() ? "" : value.data(), int(value.size()),
			SQLITE_TRANSIENT) == SQLITE_OK;
	}
}

bool insertLegacy(TestDatabase &database, const char *table,
		const ExtrasMessageBase &message) {
	auto sql = QByteArray("INSERT INTO ") + table + " VALUES(";
	constexpr auto count = std::tuple_size_v<decltype(legacyFields(message))>;
	for (auto i = size_t(0); i != count; ++i) {
		sql += i ? ",?" : "?";
	}
	sql += ')';
	const auto statement = database.prepare(sql);
	if (!statement) {
		return false;
	}
	auto index = 0;
	const auto bound = std::apply([&](const auto &...field) {
		return (bindLegacy(statement.get(), ++index, field) && ...);
	}, legacyFields(message));
	return bound && sqlite3_step(statement.get()) == SQLITE_DONE;
}

bool matchesRecords(const std::vector<ExtrasMessageBase> &rows,
		const std::vector<ExtrasMessageBase> &expected) {
	if (rows.size() != expected.size()) {
		return false;
	}
	for (const auto &message : expected) {
		const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto &row) {
			return row.fakeId == message.fakeId;
		});
		if (found == rows.end() || fields(*found) != fields(message)) {
			return false;
		}
	}
	return true;
}

QString scanDatabaseFiles(const QString &path, const std::vector<QByteArray> &markers) {
	for (const auto &suffix : { QString(), u"-wal"_q, u"-shm"_q, u"-journal"_q }) {
		QFile file(path + suffix);
		if (!file.exists()) {
			continue;
		}
		if (!file.open(QIODevice::ReadOnly)) {
			return u"could not inspect a synthetic database artifact"_q;
		}
		const auto data = file.readAll();
		if (file.error() != QFileDevice::NoError) {
			return u"could not read a synthetic database artifact"_q;
		}
		for (const auto &marker : markers) {
			if (data.contains(marker)) {
				return u"plaintext remains in the migrated database or journal"_q;
			}
		}
	}
	return {};
}

QString migrationChecks(const QString &path, bytes::const_span key) {
	const auto marker = QByteArray("SYNTHETIC_ARCHIVE_PLAINTEXT_735102_");
	const auto removedMarker = QByteArray("SYNTHETIC_ARCHIVE_FREED_PAGE_951204");
	auto database = TestDatabase(path);
	if (!database.execute("PRAGMA journal_mode=WAL")
		|| !database.execute("PRAGMA secure_delete=OFF")
		|| !createLegacyTable(database, "DeletedMessage")
		|| !createLegacyTable(database, "EditedMessage")
		|| !database.execute("CREATE TABLE RegexFilter(id BLOB PRIMARY KEY,text TEXT,"
			"enabled INTEGER,reversed INTEGER,caseInsensitive INTEGER,dialogId INTEGER)")
		|| !database.execute("CREATE TABLE RegexFilterGlobalExclusion("
			"fakeId INTEGER PRIMARY KEY,dialogId INTEGER,filterId BLOB)")
		|| !database.execute("INSERT INTO RegexFilter VALUES("
			"X'00010200','preserved[0-9]+',1,0,1,-2002)")
		|| !database.execute("INSERT INTO RegexFilterGlobalExclusion VALUES("
			"1,-2003,X'00010200')")) {
		return u"could not create the synthetic legacy database"_q;
	}
	auto deleted = std::vector<ExtrasMessageBase>();
	auto edited = std::vector<ExtrasMessageBase>();
	for (auto i = 0; i != 4; ++i) {
		auto message = sample((i < 2) ? 501 + i : 601);
		message.fakeId = (i % 2) + 101;
		message.text = marker.toStdString() + std::to_string(i)
			+ u"迁移正文🔐"_q.toStdString();
		// 旧表没有保存这两个字段，迁移不得凭空生成内容。
		message.postAuthor.clear();
		message.replyMarkupSerialized.clear();
		if (!insertLegacy(database, (i < 2) ? "DeletedMessage" : "EditedMessage", message)) {
			return u"could not insert synthetic legacy messages"_q;
		}
		((i < 2) ? deleted : edited).push_back(std::move(message));
	}
	auto removed = sample(900);
	removed.fakeId = 999;
	removed.text = removedMarker.toStdString() + std::string(12000, 'x');
	if (!insertLegacy(database, "DeletedMessage", removed)
		|| !database.execute("DELETE FROM DeletedMessage WHERE fakeId=999")) {
		return u"could not prepare legacy free-page contents"_q;
	}
	const auto regexSql = QByteArray("SELECT hex(id)||'|'||text||'|'||enabled||'|'||"
		"reversed||'|'||caseInsensitive||'|'||dialogId FROM RegexFilter");
	const auto exclusionSql = QByteArray("SELECT fakeId||'|'||dialogId||'|'||"
		"hex(filterId) FROM RegexFilterGlobalExclusion");
	const auto regexBefore = database.bytes(regexSql);
	const auto exclusionsBefore = database.bytes(exclusionSql);
	if (!regexBefore || !exclusionsBefore
		|| !database.execute("CREATE TRIGGER fail_archive_migration "
			"BEFORE DELETE ON DeletedMessage BEGIN "
			"SELECT RAISE(ABORT,'synthetic migration failure'); END")) {
		return u"could not prepare migration rollback verification"_q;
	}
	auto store = Store();
	if (store.unlock(path, key) || store.available() || store.error().isEmpty()
		|| !store.hasMessages()
		|| database.scalar("SELECT COUNT(*) FROM DeletedMessage") != qint64(2)
		|| database.scalar("SELECT COUNT(*) FROM EditedMessage") != qint64(2)
		|| database.scalar("SELECT COUNT(*) FROM EncryptedMessages") != qint64(0)) {
		return u"failed migration did not preserve all legacy records"_q;
	}
	if (!database.execute("DROP TRIGGER fail_archive_migration")
		|| !store.unlock(path, key)
		|| !matchesRecords(store.get(false, kUser, kDialog, kTopic,
			0, 0, 0, 100), deleted)
		|| !matchesRecords(store.get(true, kUser, kDialog, 0,
			601, 0, 0, 100), edited)
		|| database.scalar("SELECT COUNT(*) FROM DeletedMessage") != qint64(0)
		|| database.scalar("SELECT COUNT(*) FROM EditedMessage") != qint64(0)
		|| database.scalar("SELECT COUNT(*) FROM EncryptedMessages") != qint64(4)
		|| database.bytes(regexSql) != regexBefore
		|| database.bytes(exclusionSql) != exclusionsBefore) {
		return u"legacy migration changed messages, identifiers, or regex settings"_q;
	}
	store.close();
	database.close();
	if (const auto error = scanDatabaseFiles(path,
			{ marker, removedMarker, QByteArray::fromStdString(deleted[0].fwdName) });
			!error.isEmpty()) {
		return error;
	}
	if (!store.unlock(path, key)
		|| !matchesRecords(store.get(false, kUser, kDialog, kTopic, 0, 0, 0, 100), deleted)
		|| !matchesRecords(store.get(true, kUser, kDialog, 0, 601, 0, 0, 100), edited)) {
		return u"migrated archive did not survive reopening"_q;
	}
	return {};
}

QString corruptionChecks(const QString &path, bytes::const_span key) {
	auto store = Store();
	if (!store.unlock(path, key) || !store.add(sample(20), false)
		|| !store.add(sample(10), false)) {
		return u"could not initialize corruption samples"_q;
	}
	auto database = TestDatabase(path);
	const auto payload = database.bytes("SELECT payload FROM EncryptedMessages "
		"WHERE messageId=10");
	if (!payload || !database.execute("UPDATE EncryptedMessages "
			"SET payload=zeroblob(32) WHERE messageId=10")) {
		return u"could not corrupt the synthetic archive record"_q;
	}
	if (!store.get(false, kUser, kDialog, kTopic, 0, 0, 0, 100).empty()
		|| store.available() || store.error().isEmpty()) {
		return u"corrupt archive query returned partial records or stayed available"_q;
	}
	store.close();
	if (!database.bindBlob("UPDATE EncryptedMessages SET payload=?1 WHERE messageId=10", *payload)
		|| !store.unlock(path, key)
		|| !messageIds(store.get(false, kUser, kDialog, kTopic, 0, 0, 0, 100), {20, 10})
		|| !database.execute("UPDATE EncryptedMessages SET userId=1002 WHERE messageId=10")) {
		return u"could not prepare archive ownership corruption"_q;
	}
	if (!store.get(false, kUser + 1, kDialog, kTopic, 0, 0, 0, 100).empty()
		|| store.available() || store.error().isEmpty()) {
		return u"modified database ownership bypassed archive authentication"_q;
	}
	store.close();
	if (!database.execute("UPDATE EncryptedMessages SET userId=1001 WHERE messageId=10")
		|| !store.unlock(path, key)
		|| !database.execute("DROP TABLE EncryptedMessages")) {
		return u"could not prepare a SQLite query failure"_q;
	}
	if (!store.get(false, kUser, kDialog, kTopic, 0, 0, 0, 100).empty()
		|| store.error().isEmpty() || !store.hasMessages()) {
		return u"SQLite query failure was reported as an empty healthy archive"_q;
	}
	return {};
}

QString readOnlyChecks(const QString &path, bytes::const_span key) {
	auto store = Store();
	if (!store.unlock(path, key) || !store.add(sample(1), false)) {
		return u"could not initialize the read-only archive sample"_q;
	}
	store.close();
	QFile source(path);
	if (!source.open(QIODevice::ReadOnly)) {
		return u"could not read the synthetic read-only sample"_q;
	}
	const auto before = source.readAll();
	source.close();
	const auto permissions = QFile::permissions(path);
	const auto restorePermissions = gsl::finally([&] {
		store.close();
		QFile::setPermissions(path, permissions);
	});
	if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::ReadUser
			| QFileDevice::ReadGroup | QFileDevice::ReadOther)) {
		return u"could not mark the synthetic database read-only"_q;
	}
	if (store.unlock(path, key) || store.available() || store.error().isEmpty()) {
		return u"read-only archive initialization did not report a write failure"_q;
	}
	store.close();
	if (!source.open(QIODevice::ReadOnly) || source.readAll() != before) {
		return u"failed read-only initialization changed the archive"_q;
	}
	return {};
}

}

Result verifyMessageArchive(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: storage.verify-archive"_q);
	}
	const auto session = ActiveSession();
	if ((!cDebugProfile() && !cTestAgent())
		|| !session
		|| !isFakeSession(session)) {
		return Result::Err(u"an isolated debug profile and fake session are required"_q);
	}
	QTemporaryDir directory(QDir::tempPath() + u"/astragram-archive-tests-XXXXXX"_q);
	if (!directory.isValid()) {
		return Result::Err(u"could not create the temporary archive test directory"_q);
	}
	auto key = std::array<unsigned char, 256>();
	for (auto i = 0; i != int(key.size()); ++i) {
		key[i] = static_cast<unsigned char>(i);
	}
	auto checks = Json::array();
	if (const auto error = cryptoChecks(bytes::make_span(key)); !error.isEmpty()) {
		return Result::Err(error);
	}
	checks.push_back("完整字段、二进制、随机性与密文认证");
	if (const auto error = storeChecks(directory.filePath(u"fresh.db"_q),
			bytes::make_span(key)); !error.isEmpty()) {
		return Result::Err(error);
	}
	checks.push_back("账号隔离、分页搜索、重启恢复与换钥");
	if (const auto error = migrationChecks(directory.filePath(u"migration.db"_q),
			bytes::make_span(key)); !error.isEmpty()) {
		return Result::Err(error);
	}
	checks.push_back("旧库完整迁移、失败回滚、正则保留与明文清理");
	if (const auto error = corruptionChecks(directory.filePath(u"corruption.db"_q),
			bytes::make_span(key)); !error.isEmpty()) {
		return Result::Err(error);
	}
	checks.push_back("损坏记录、归属篡改与数据库查询故障");
	if (const auto error = readOnlyChecks(directory.filePath(u"readonly.db"_q),
			bytes::make_span(key)); !error.isEmpty()) {
		return Result::Err(error);
	}
	checks.push_back("只读数据库拒绝写入并保留原文件");
	return Result::Ok(Compact(Json{
		{ "checks", checks },
		{ "count", checks.size() },
	}));
}

}
#endif
