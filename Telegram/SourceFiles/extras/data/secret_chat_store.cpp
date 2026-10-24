#include "extras/data/secret_chat_store.h"

#include "extras/libs/sqlite/sqlite3.h"
#include "base/openssl_help.h"
#include "base/random.h"
#include "crl/crl_object_on_queue.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDataStream>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QIODevice>
#include <QtCore/QThread>
#include <array>
#include <cstring>
#include <limits>
#include <optional>

namespace Extras::SecretChats {
namespace {

constexpr auto kVersion = qint32(1);
constexpr auto kRecordType = qint32(1);
constexpr auto kNonceSize = 12;
constexpr auto kTagSize = 16;
constexpr auto kMaxStateSize = 512 * 1024 * 1024;
constexpr char kMagic[] = { 'A', 'G', 'S', '1' };
constexpr char kKeyPurpose[] = "AstraGram secret chat account state v1";
constexpr auto kHeaderSize = int(sizeof(kMagic)) + kNonceSize;
constexpr auto kEnvelopeSize = kHeaderSize + kTagSize;

using Key = std::array<unsigned char, 32>;
using Cipher = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

void assertMainThread() {
	Expects(QThread::currentThread() == QCoreApplication::instance()->thread());
}

[[nodiscard]] Store::Config checkedConfig(Store::Config config) {
	assertMainThread();
	Expects(!config.path.isEmpty());
	Expects(!config.localKey.isEmpty());
	Expects(config.localKey.size() <= std::numeric_limits<int>::max());
	Expects(config.accountId != 0);
	Expects(config.authorizationId != 0);
	return config;
}

void clearBytes(QByteArray &data) {
	OPENSSL_cleanse(data.data(), data.size());
	data.clear();
}

[[nodiscard]] QByteArray identity(quint64 value) {
	auto result = QByteArray();
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << value;
	return result;
}

[[nodiscard]] QByteArray authenticatedData(
		quint64 accountId,
		quint64 authorizationId) {
	auto result = QByteArray();
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << kVersion << accountId << authorizationId << kRecordType;
	return result;
}

[[nodiscard]] Key deriveKey(
		const QByteArray &localKey,
		const QByteArray &aad) {
	auto purpose = QByteArray(kKeyPurpose, sizeof(kKeyPurpose) - 1);
	purpose += aad;
	auto key = Key();
	auto length = 0U;
	const auto derived = HMAC(
		EVP_sha256(),
		localKey.constData(),
		int(localKey.size()),
		reinterpret_cast<const unsigned char*>(purpose.constData()),
		purpose.size(),
		key.data(),
		&length);
	Assert(derived != nullptr && length == key.size());
	return key;
}

[[nodiscard]] std::optional<QByteArray> encrypt(
		const Key &key,
		const QByteArray &aad,
		const QByteArray &state) {
	if (state.size() > kMaxStateSize) {
		return std::nullopt;
	}
	auto cipher = Cipher(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
	if (!cipher) {
		return std::nullopt;
	}
	auto result = QByteArray(state.size() + kEnvelopeSize, Qt::Uninitialized);
	memcpy(result.data(), kMagic, sizeof(kMagic));
	auto nonce = reinterpret_cast<unsigned char*>(
		result.data() + sizeof(kMagic));
	base::RandomFill(nonce, kNonceSize);
	if (EVP_EncryptInit_ex(
			cipher.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1
		|| EVP_CIPHER_CTX_ctrl(
			cipher.get(), EVP_CTRL_GCM_SET_IVLEN, kNonceSize, nullptr) != 1
		|| EVP_EncryptInit_ex(
			cipher.get(), nullptr, nullptr, key.data(), nonce) != 1) {
		return std::nullopt;
	}
	auto size = 0;
	if (EVP_EncryptUpdate(
			cipher.get(),
			nullptr,
			&size,
			reinterpret_cast<const unsigned char*>(aad.constData()),
			int(aad.size())) != 1) {
		return std::nullopt;
	}
	auto output = reinterpret_cast<unsigned char*>(
		result.data() + kHeaderSize);
	if (EVP_EncryptUpdate(
			cipher.get(),
			output,
			&size,
			reinterpret_cast<const unsigned char*>(state.constData()),
			int(state.size())) != 1) {
		return std::nullopt;
	}
	auto finalSize = 0;
	if (EVP_EncryptFinal_ex(cipher.get(), output + size, &finalSize) != 1
		|| size + finalSize != state.size()
		|| EVP_CIPHER_CTX_ctrl(
			cipher.get(),
			EVP_CTRL_GCM_GET_TAG,
			kTagSize,
			output + state.size()) != 1) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] std::optional<QByteArray> decrypt(
		const Key &key,
		const QByteArray &aad,
		const QByteArray &payload) {
	if (payload.size() < kEnvelopeSize
		|| payload.size() > kMaxStateSize + kEnvelopeSize
		|| memcmp(payload.constData(), kMagic, sizeof(kMagic)) != 0) {
		return std::nullopt;
	}
	auto cipher = Cipher(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
	if (!cipher) {
		return std::nullopt;
	}
	const auto nonce = reinterpret_cast<const unsigned char*>(
		payload.constData() + sizeof(kMagic));
	if (EVP_DecryptInit_ex(
			cipher.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1
		|| EVP_CIPHER_CTX_ctrl(
			cipher.get(), EVP_CTRL_GCM_SET_IVLEN, kNonceSize, nullptr) != 1
		|| EVP_DecryptInit_ex(
			cipher.get(), nullptr, nullptr, key.data(), nonce) != 1) {
		return std::nullopt;
	}
	auto size = 0;
	if (EVP_DecryptUpdate(
			cipher.get(),
			nullptr,
			&size,
			reinterpret_cast<const unsigned char*>(aad.constData()),
			int(aad.size())) != 1) {
		return std::nullopt;
	}
	const auto encryptedSize = int(payload.size()) - kEnvelopeSize;
	auto state = QByteArray(
		encryptedSize + EVP_MAX_BLOCK_LENGTH, Qt::Uninitialized);
	auto authenticated = false;
	const auto clear = gsl::finally([&] {
		if (!authenticated) {
			clearBytes(state);
		}
	});
	auto output = reinterpret_cast<unsigned char*>(state.data());
	if (EVP_DecryptUpdate(
			cipher.get(),
			output,
			&size,
			reinterpret_cast<const unsigned char*>(
				payload.constData() + kHeaderSize),
			encryptedSize) != 1) {
		return std::nullopt;
	}
	auto tag = payload.right(kTagSize);
	auto finalSize = 0;
	if (EVP_CIPHER_CTX_ctrl(
			cipher.get(), EVP_CTRL_GCM_SET_TAG, kTagSize, tag.data()) != 1
		|| EVP_DecryptFinal_ex(cipher.get(), output + size, &finalSize) != 1
		|| size + finalSize != encryptedSize) {
		return std::nullopt;
	}
	state.resize(encryptedSize);
	authenticated = true;
	return state;
}

class Worker final {
public:
	explicit Worker(Store::Config config)
	: _path(std::move(config.path))
	, _account(identity(config.accountId))
	, _authorization(identity(config.authorizationId))
	, _aad(authenticatedData(config.accountId, config.authorizationId)) {
		const auto clear = gsl::finally([&] { clearBytes(config.localKey); });
		_key = deriveKey(config.localKey, _aad);
	}

	~Worker() {
		close();
	}

	[[nodiscard]] Store::LoadResult load() {
		if (!open()) {
			return { .error = _error };
		}
		auto statement = Statement(*this,
			"SELECT version, payload FROM secret_chat_states "
			"WHERE account_id=?1 AND authorization_id=?2 AND record_type=?3");
		if (!statement || !bindIdentity(statement)) {
			return { .error = _error };
		}
		const auto code = statement.step();
		if (code == SQLITE_DONE) {
			return { .ok = true };
		}
		if (code != SQLITE_ROW) {
			return { .error = _error };
		}
		if (sqlite3_column_type(statement.get(), 0) != SQLITE_INTEGER
			|| sqlite3_column_int64(statement.get(), 0) != kVersion
			|| sqlite3_column_type(statement.get(), 1) != SQLITE_BLOB) {
			return {
				.exists = true,
				.error = u"Unsupported secret chat state format"_q,
			};
		}
		const auto data = static_cast<const char*>(
			sqlite3_column_blob(statement.get(), 1));
		const auto size = sqlite3_column_bytes(statement.get(), 1);
		if (sqlite3_errcode(_database) == SQLITE_NOMEM) {
			return {
				.exists = true,
				.error = u"Could not read secret chat state"_q,
			};
		}
		// 解密在语句有效期内完成，避免再复制整份密文。
		const auto payload = QByteArray::fromRawData(data, size);
		auto state = decrypt(_key, _aad, payload);
		if (!state) {
			return {
				.exists = true,
				.error = u"Could not authenticate secret chat state"_q,
			};
		}
		return { .ok = true, .exists = true, .state = std::move(*state) };
	}

	[[nodiscard]] bool save(const QByteArray &state) {
		if (!open()) {
			return false;
		}
		const auto payload = encrypt(_key, _aad, state);
		if (!payload) {
			return fail(u"Could not encrypt secret chat state"_q);
		}
		auto transaction = Transaction(*this);
		if (!transaction) {
			return false;
		}
		auto statement = Statement(*this,
			"INSERT INTO secret_chat_states "
			"(account_id,authorization_id,record_type,version,payload) "
			"VALUES (?1,?2,?3,?4,?5) "
			"ON CONFLICT(account_id,authorization_id,record_type) "
			"DO UPDATE SET version=excluded.version,payload=excluded.payload");
		if (!statement
			|| !bindIdentity(statement)
			|| !statement.bind(4, kVersion)
			|| !statement.bind(5, *payload)
			|| statement.step() != SQLITE_DONE) {
			return false;
		}
		return transaction.commit();
	}

	[[nodiscard]] bool destroy() {
		if (_destroyed) {
			return true;
		}
		if (!open()) {
			return false;
		}
		{
			auto transaction = Transaction(*this);
			if (!transaction) {
				return false;
			}
			auto statement = Statement(*this,
				"DELETE FROM secret_chat_states "
				"WHERE account_id=?1 AND authorization_id=?2 AND record_type=?3");
			if (!statement
				|| !bindIdentity(statement)
				|| statement.step() != SQLITE_DONE
				|| !transaction.commit()) {
				return false;
			}
		}
		_destroyed = true;
		close();
		return true;
	}

	void close() {
		closeDatabase();
		OPENSSL_cleanse(_key.data(), _key.size());
		_closed = true;
	}

private:
	class Statement final {
	public:
		Statement(Worker &owner, const char *sql) : _owner(owner) {
			const auto code = sqlite3_prepare_v2(
				_owner._database, sql, -1, &_statement, nullptr);
			if (code != SQLITE_OK) {
				_owner.fail(u"Could not prepare secret chat database operation"_q, code);
			}
		}

		~Statement() {
			sqlite3_finalize(_statement);
		}

		Statement(const Statement &) = delete;
		Statement &operator=(const Statement &) = delete;

		explicit operator bool() const {
			return _statement != nullptr;
		}

		sqlite3_stmt *get() const {
			return _statement;
		}

		bool bind(int index, int value) {
			return check(sqlite3_bind_int(_statement, index, value));
		}

		bool bind(int index, const QByteArray &value) {
			// 参数在语句销毁前保持有效，避免复制整份密文。
			return check(sqlite3_bind_blob(
				_statement, index, value.constData(), int(value.size()), SQLITE_STATIC));
		}

		int step() {
			const auto code = sqlite3_step(_statement);
			if (code != SQLITE_ROW && code != SQLITE_DONE) {
				_owner.fail(u"Could not execute secret chat database operation"_q, code);
			}
			return code;
		}

	private:
		bool check(int code) {
			return (code == SQLITE_OK)
				|| _owner.fail(u"Could not bind secret chat database parameters"_q, code);
		}

		Worker &_owner;
		sqlite3_stmt *_statement = nullptr;
	};

	class Transaction final {
	public:
		explicit Transaction(Worker &owner) : _owner(owner) {
			_active = _owner.execute("BEGIN IMMEDIATE");
		}

		~Transaction() {
			if (_active && !sqlite3_get_autocommit(_owner._database)) {
				_owner.execute("ROLLBACK");
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
		Worker &_owner;
		bool _active = false;
	};

	bool fail(const QString &message, int code = SQLITE_OK) {
		_error = (code == SQLITE_OK)
			? message
			: message + u" (error code %1)"_q.arg(code);
		return false;
	}

	bool execute(const char *sql) {
		const auto code = sqlite3_exec(_database, sql, nullptr, nullptr, nullptr);
		return (code == SQLITE_OK)
			|| fail(u"Could not execute secret chat database operation"_q, code);
	}

	bool bindIdentity(Statement &statement) const {
		return statement.bind(1, _account)
			&& statement.bind(2, _authorization)
			&& statement.bind(3, kRecordType);
	}

	std::optional<int> schemaVersion() {
		auto statement = Statement(*this, "PRAGMA user_version");
		if (!statement || statement.step() != SQLITE_ROW) {
			return std::nullopt;
		}
		return sqlite3_column_int(statement.get(), 0);
	}

	bool initializeSchema() {
		const auto version = schemaVersion();
		if (!version) {
			return false;
		}
		if (*version == kVersion) {
			return true;
		}
		if (*version != 0) {
			return fail(u"Unsupported secret chat database version"_q);
		}
		auto transaction = Transaction(*this);
		return transaction
			&& execute("CREATE TABLE IF NOT EXISTS secret_chat_states ("
				"account_id BLOB NOT NULL,"
				"authorization_id BLOB NOT NULL,"
				"record_type INTEGER NOT NULL,"
				"version INTEGER NOT NULL,"
				"payload BLOB NOT NULL,"
				"PRIMARY KEY(account_id,authorization_id,record_type)"
				") WITHOUT ROWID")
			&& execute("PRAGMA user_version=1")
			&& transaction.commit();
	}

	bool open() {
		_error.clear();
		if (_closed) {
			return fail(u"Secret chat store is closed"_q);
		}
		if (_database) {
			return true;
		}
		if (!QDir().mkpath(QFileInfo(_path).absolutePath())) {
			return fail(u"Could not create secret chat database directory"_q);
		}
		const auto path = _path.toUtf8();
		const auto code = sqlite3_open_v2(
			path.constData(),
			&_database,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
			nullptr);
		auto initialized = false;
		const auto cleanup = gsl::finally([&] {
			if (!initialized) {
				closeDatabase();
			}
		});
		if (code != SQLITE_OK) {
			return fail(u"Could not open secret chat database"_q, code);
		}
		const auto timeoutCode = sqlite3_busy_timeout(_database, 5000);
		if (timeoutCode != SQLITE_OK) {
			return fail(u"Could not configure secret chat database"_q, timeoutCode);
		}
		if (!execute("PRAGMA journal_mode=DELETE")
			|| !execute("PRAGMA synchronous=FULL")
			|| !execute("PRAGMA secure_delete=ON")
			|| !initializeSchema()) {
			return false;
		}
		initialized = true;
		return true;
	}

	void closeDatabase() {
		if (!_database) {
			return;
		}
		const auto code = sqlite3_close_v2(_database);
		_database = nullptr;
		Assert(code == SQLITE_OK);
	}

	const QString _path;
	const QByteArray _account;
	const QByteArray _authorization;
	const QByteArray _aad;
	Key _key = {};
	sqlite3 *_database = nullptr;
	QString _error;
	bool _closed = false;
	bool _destroyed = false;
};

}

class Store::Private final {
public:
	explicit Private(Config config) : _worker(std::move(config)) {
	}

	std::shared_ptr<int> _lifetime = std::make_shared<int>(0);
	crl::object_on_queue<Worker> _worker;
};

Store::Store(Config config)
: _private(std::make_unique<Private>(checkedConfig(std::move(config)))) {
}

Store::~Store() {
	assertMainThread();
	_private->_lifetime.reset();
	_private->_worker.with_sync([](Worker &worker) { worker.close(); });
}

void Store::load(Fn<void(LoadResult)> done) {
	assertMainThread();
	const auto lifetime = std::weak_ptr(_private->_lifetime);
	_private->_worker.with([
		lifetime,
		done = std::move(done)
	](Worker &worker) mutable {
		auto result = worker.load();
		crl::on_main([
			lifetime,
			done = std::move(done),
			result = std::move(result)
		]() mutable {
			if (lifetime.expired() || !done) {
				clearBytes(result.state);
				return;
			}
			done(std::move(result));
		});
	});
}

void Store::save(QByteArray state, Fn<void(bool)> done) {
	assertMainThread();
	const auto lifetime = std::weak_ptr(_private->_lifetime);
	_private->_worker.with([
		lifetime,
		state = std::move(state),
		done = std::move(done)
	](Worker &worker) mutable {
		const auto clear = gsl::finally([&] { clearBytes(state); });
		const auto success = worker.save(state);
		if (done) {
			crl::on_main(lifetime, [done = std::move(done), success] {
				done(success);
			});
		}
	});
}

void Store::destroy(Fn<void(bool)> done) {
	assertMainThread();
	const auto lifetime = std::weak_ptr(_private->_lifetime);
	_private->_worker.with([
		lifetime,
		done = std::move(done)
	](Worker &worker) mutable {
		const auto success = worker.destroy();
		if (done) {
			crl::on_main(lifetime, [done = std::move(done), success] {
				done(success);
			});
		}
	});
}

}
