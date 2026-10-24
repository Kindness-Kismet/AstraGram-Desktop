#include "extras/features/secret_chats/secret_chat_crypto.h"

#include "extras/features/secret_chats/secret_chat_codec.h"
#include "base/openssl_help.h"
#include "base/random.h"
#include "mtproto/mtproto_auth_key.h"
#include "mtproto/mtproto_dh_utils.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QtEndian>

namespace Extras::SecretChats::Crypto {
namespace {

constexpr auto kKeyBytes = 256;
constexpr auto kBlockBytes = 16;
constexpr auto kHeaderBytes = 24;

QByteArray toArray(bytes::const_span bytes) {
	return QByteArray(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool goodPublic(const QByteArray &value, const QByteArray &prime) {
	if (value.isEmpty() || value.size() > kKeyBytes) {
		return false;
	}
	const auto number = openssl::BigNum(bytes::make_span(value));
	const auto modulus = openssl::BigNum(bytes::make_span(prime));
	const auto difference = openssl::BigNum::Sub(modulus, number);
	auto minimumBytes = QByteArray(249, '\0');
	minimumBytes[0] = 1;
	const auto minimum = openssl::BigNum(bytes::make_span(minimumBytes));
	return !number.failed()
		&& !difference.failed()
		&& !difference.isNegative()
		&& openssl::BigNum::Compare(number, minimum) > 0
		&& openssl::BigNum::Compare(difference, minimum) > 0;
}

struct AesParameters {
	QByteArray key;
	QByteArray iv;
};

AesParameters derive(
		const QByteArray &key,
		bool senderIsCreator,
		const QByteArray &messageKey) {
	const auto offset = senderIsCreator ? 0 : 8;
	const auto a = QCryptographicHash::hash(
		messageKey + key.mid(offset, 36),
		QCryptographicHash::Sha256);
	const auto b = QCryptographicHash::hash(
		key.mid(40 + offset, 36) + messageKey,
		QCryptographicHash::Sha256);
	return {
		a.left(8) + b.mid(8, 16) + a.mid(24, 8),
		b.left(8) + a.mid(8, 16) + b.mid(24, 8),
	};
}

QByteArray messageKey(
		const QByteArray &key,
		bool senderIsCreator,
		const QByteArray &plaintext) {
	return QCryptographicHash::hash(
		key.mid(88 + (senderIsCreator ? 0 : 8), 32) + plaintext,
		QCryptographicHash::Sha256).mid(8, 16);
}

void wipe(QByteArray &bytes) {
	if (!bytes.isEmpty()) {
		OPENSSL_cleanse(bytes.data(), bytes.size());
	}
}

} // namespace

std::optional<DhState> createDh(
		int generator,
		const QByteArray &prime,
		const QByteArray &serverRandom) {
	if (generator < 2
		|| generator > 7
		|| prime.size() != kKeyBytes
		|| serverRandom.size() != kKeyBytes
		|| !MTP::IsPrimeAndGood(bytes::make_span(prime), generator)) {
		return std::nullopt;
	}
	while (true) {
		auto first = MTP::CreateModExp(
			generator,
			bytes::make_span(prime),
			bytes::make_span(serverRandom));
		auto publicValue = toArray(first.modexp);
		if (!goodPublic(publicValue, prime)) {
			OPENSSL_cleanse(first.randomPower.data(), first.randomPower.size());
			continue;
		}
		auto power = toArray(first.randomPower);
		OPENSSL_cleanse(first.randomPower.data(), first.randomPower.size());
		return DhState{ generator, prime, std::move(power), std::move(publicValue) };
	}
}

std::optional<QByteArray> sharedKey(
		const DhState &state,
		const QByteArray &peerPublic) {
	if (state.prime.size() != kKeyBytes
		|| state.power.size() != kKeyBytes
		|| !goodPublic(peerPublic, state.prime)) {
		return std::nullopt;
	}
	auto result = MTP::CreateAuthKey(
		bytes::make_span(peerPublic),
		bytes::make_span(state.power),
		bytes::make_span(state.prime));
	if (result.empty() || result.size() > kKeyBytes) {
		return std::nullopt;
	}
	auto key = QByteArray(kKeyBytes - result.size(), '\0') + toArray(result);
	OPENSSL_cleanse(result.data(), result.size());
	return key;
}

qint64 fingerprint(const QByteArray &key) {
	Expects(key.size() == kKeyBytes);
	const auto digest = QCryptographicHash::hash(key, QCryptographicHash::Sha1);
	return qFromLittleEndian<qint64>(digest.constData() + 12);
}

QByteArray verification(const QByteArray &key) {
	Expects(key.size() == kKeyBytes);
	return QCryptographicHash::hash(key, QCryptographicHash::Sha1).left(16)
		+ QCryptographicHash::hash(key, QCryptographicHash::Sha256).left(20);
}

std::optional<QByteArray> seal(
		const QByteArray &key,
		bool senderIsCreator,
		const QByteArray &body) {
	if (key.size() != kKeyBytes
		|| body.isEmpty()
		|| body.size() > Codec::kMaxPacketBytes) {
		return std::nullopt;
	}
	auto plaintext = QByteArray(4, '\0');
	qToLittleEndian<quint32>(body.size(), plaintext.data());
	plaintext.append(body);
	const auto paddingSize = 12
		+ ((kBlockBytes - ((plaintext.size() + 12) % kBlockBytes)) % kBlockBytes)
		+ (base::RandomValue<quint32>() % 4) * kBlockBytes;
	const auto paddingStart = plaintext.size();
	plaintext.resize(paddingStart + paddingSize);
	base::RandomFill(plaintext.data() + paddingStart, paddingSize);
	const auto digest = messageKey(key, senderIsCreator, plaintext);
	auto aes = derive(key, senderIsCreator, digest);
	auto result = QByteArray(kHeaderBytes + plaintext.size(), '\0');
	qToLittleEndian<qint64>(fingerprint(key), result.data());
	memcpy(result.data() + 8, digest.constData(), 16);
	MTP::aesIgeEncryptRaw(
		plaintext.constData(),
		result.data() + kHeaderBytes,
		plaintext.size(),
		aes.key.constData(),
		aes.iv.constData());
	wipe(plaintext);
	wipe(aes.key);
	wipe(aes.iv);
	return result;
}

std::optional<QByteArray> open(
		const QByteArray &key,
		bool senderIsCreator,
		const QByteArray &ciphertext) {
	if (key.size() != kKeyBytes
		|| ciphertext.size() < kHeaderBytes + kBlockBytes
		|| ciphertext.size() > kHeaderBytes + 4 + Codec::kMaxPacketBytes + 1024
		|| (ciphertext.size() - kHeaderBytes) % kBlockBytes
		|| qFromLittleEndian<qint64>(ciphertext.constData()) != fingerprint(key)) {
		return std::nullopt;
	}
	const auto digest = ciphertext.mid(8, 16);
	auto aes = derive(key, senderIsCreator, digest);
	auto plaintext = QByteArray(ciphertext.size() - kHeaderBytes, '\0');
	MTP::aesIgeDecryptRaw(
		ciphertext.constData() + kHeaderBytes,
		plaintext.data(),
		plaintext.size(),
		aes.key.constData(),
		aes.iv.constData());
	wipe(aes.key);
	wipe(aes.iv);
	const auto size = qFromLittleEndian<quint32>(plaintext.constData());
	const auto paddingSize = plaintext.size() - 4 - qint64(size);
	const auto expected = messageKey(key, senderIsCreator, plaintext);
	if (!size
		|| size > Codec::kMaxPacketBytes
		|| paddingSize < 12
		|| paddingSize > 1024
		|| CRYPTO_memcmp(digest.constData(), expected.constData(), 16)) {
		wipe(plaintext);
		return std::nullopt;
	}
	auto result = plaintext.mid(4, size);
	wipe(plaintext);
	return result;
}

std::optional<QByteArray> sealFile(
		const QByteArray &key,
		const QByteArray &iv,
		const QByteArray &bytes) {
	if (key.size() != 32
		|| iv.size() != 32
		|| bytes.isEmpty()
		|| bytes.size() > Codec::kMaxFileBytes) {
		return std::nullopt;
	}
	auto plaintext = bytes;
	const auto paddingSize = (kBlockBytes - (bytes.size() % kBlockBytes)) % kBlockBytes;
	if (paddingSize) {
		plaintext.resize(bytes.size() + paddingSize);
		base::RandomFill(plaintext.data() + bytes.size(), paddingSize);
	}
	auto result = QByteArray(plaintext.size(), '\0');
	MTP::aesIgeEncryptRaw(
		plaintext.constData(), result.data(), plaintext.size(),
		key.constData(), iv.constData());
	wipe(plaintext);
	return result;
}

std::optional<QByteArray> openFile(
		const QByteArray &key,
		const QByteArray &iv,
		const QByteArray &ciphertext,
		qint64 size) {
	if (key.size() != 32
		|| iv.size() != 32
		|| size <= 0
		|| size > Codec::kMaxFileBytes
		|| ciphertext.size() != ((size + kBlockBytes - 1) / kBlockBytes) * kBlockBytes) {
		return std::nullopt;
	}
	auto plaintext = QByteArray(ciphertext.size(), '\0');
	MTP::aesIgeDecryptRaw(
		ciphertext.constData(), plaintext.data(), plaintext.size(),
		key.constData(), iv.constData());
	auto result = plaintext.left(size);
	wipe(plaintext);
	return result;
}

qint32 fileFingerprint(const QByteArray &key, const QByteArray &iv) {
	Expects(key.size() == 32 && iv.size() == 32);
	const auto digest = QCryptographicHash::hash(key + iv, QCryptographicHash::Md5);
	return qFromLittleEndian<qint32>(digest.constData())
		^ qFromLittleEndian<qint32>(digest.constData() + 4);
}

} // namespace Extras::SecretChats::Crypto
