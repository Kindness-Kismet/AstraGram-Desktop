#include "extras/data/message_archive_crypto.h"

#include "base/openssl_help.h"
#include "base/random.h"

#include <QtCore/QDataStream>
#include <QtCore/QIODevice>
#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>

namespace Database::ArchiveCrypto {
namespace {

constexpr auto kVersion = qint32(1);
constexpr auto kNonceSize = 12;
constexpr auto kTagSize = 16;
constexpr auto kIdSize = 16;
constexpr auto kMaxPayloadSize = 64 * 1024 * 1024;
constexpr char kMagic[] = { 'A', 'G', 'M', '1' };
constexpr char kKeyPurpose[] = "AstraGram message archive v1";
constexpr auto kHeaderSize = int(sizeof(kMagic)) + kNonceSize;
constexpr auto kEnvelopeSize = kHeaderSize + kTagSize;

using Cipher = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

template <typename Message>
[[nodiscard]] auto messageFields(Message &message) {
	return std::tie(
		message.fakeId,
		message.userId,
		message.dialogId,
		message.groupedId,
		message.peerId,
		message.fromId,
		message.topicId,
		message.messageId,
		message.date,
		message.flags,
		message.editDate,
		message.views,
		message.fwdFlags,
		message.fwdFromId,
		message.fwdName,
		message.fwdDate,
		message.fwdPostAuthor,
		message.postAuthor,
		message.replyFlags,
		message.replyMessageId,
		message.replyPeerId,
		message.replyTopId,
		message.replyForumTopic,
		message.replySerialized,
		message.replyMarkupSerialized,
		message.entityCreateDate,
		message.text,
		message.textEntities,
		message.mediaPath,
		message.hqThumbPath,
		message.documentType,
		message.documentSerialized,
		message.thumbsSerialized,
		message.documentAttributesSerialized,
		message.mimeType);
}

template <typename Value>
[[nodiscard]] bool writeField(QDataStream &stream, const Value &value) {
	if constexpr (std::is_integral_v<Value>) {
		if constexpr (sizeof(Value) == sizeof(qint64)) {
			stream << qint64(value);
		} else {
			stream << qint32(value);
		}
		return stream.status() == QDataStream::Ok;
	} else {
		if (value.size() > kMaxPayloadSize
			|| stream.device()->pos() + sizeof(qint32) + value.size()
				> kMaxPayloadSize) {
			return false;
		}
		const auto size = int(value.size());
		stream << qint32(size);
		return stream.status() == QDataStream::Ok
			&& (!size || stream.writeRawData(value.data(), size) == size);
	}
}

template <typename Value>
[[nodiscard]] bool readField(QDataStream &stream, Value &value) {
	if constexpr (std::is_integral_v<Value>) {
		using Stored = std::conditional_t<
			sizeof(Value) == sizeof(qint64),
			qint64,
			qint32>;
		auto stored = Stored();
		stream >> stored;
		if constexpr (std::is_same_v<Value, bool>) {
			if (stored != 0 && stored != 1) {
				return false;
			}
		}
		value = Value(stored);
		return stream.status() == QDataStream::Ok;
	} else {
		auto size = qint32();
		stream >> size;
		if (stream.status() != QDataStream::Ok
			|| size < 0
			|| size > stream.device()->bytesAvailable()) {
			return false;
		}
		value.resize(size);
		return !size || stream.readRawData(value.data(), size) == size;
	}
}

[[nodiscard]] std::optional<QByteArray> serialize(
		const ExtrasMessageBase &message) {
	auto result = QByteArray();
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	const auto success = std::apply([&](const auto &...field) {
		return (writeField(stream, field) && ...);
	}, messageFields(message));
	if (!success || result.size() > kMaxPayloadSize) {
		OPENSSL_cleanse(result.data(), result.size());
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] std::optional<ExtrasMessageBase> deserialize(
		const QByteArray &data) {
	auto result = ExtrasMessageBase();
	QDataStream stream(data);
	stream.setVersion(QDataStream::Qt_5_1);
	const auto success = std::apply([&](auto &...field) {
		return (readField(stream, field) && ...);
	}, messageFields(result));
	if (!success || !stream.atEnd()) {
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] QByteArray authenticatedData(
		const ExtrasMessageBase &message,
		bool edited) {
	auto result = QByteArray();
	QDataStream stream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream
		<< kVersion
		<< qint32(edited)
		<< qint64(message.fakeId)
		<< qint64(message.userId)
		<< qint64(message.dialogId)
		<< qint64(message.topicId)
		<< qint32(message.messageId);
	return result;
}

} // namespace

Context makeContext(bytes::const_span localKey) {
	Expects(!localKey.empty());
	Expects(localKey.size() <= std::numeric_limits<int>::max());

	auto result = Context();
	auto length = 0U;
	const auto derived = HMAC(
		EVP_sha256(),
		localKey.data(),
		int(localKey.size()),
		reinterpret_cast<const unsigned char*>(kKeyPurpose),
		sizeof(kKeyPurpose) - 1,
		result.key.data(),
		&length);
	Assert(derived != nullptr && length == result.key.size());
	const auto digest = openssl::Sha256(bytes::make_span(result.key));
	result.id = QByteArray(
		reinterpret_cast<const char*>(digest.data()),
		kIdSize);
	return result;
}

std::optional<QByteArray> encrypt(
		const Context &context,
		const ExtrasMessageBase &message,
		bool edited) {
	auto data = serialize(message);
	if (!data) {
		return std::nullopt;
	}
	const auto clear = gsl::finally([&] {
		OPENSSL_cleanse(data->data(), data->size());
	});
	auto cipher = Cipher(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
	if (!cipher) {
		return std::nullopt;
	}
	auto result = QByteArray(data->size() + kEnvelopeSize, Qt::Uninitialized);
	memcpy(result.data(), kMagic, sizeof(kMagic));
	auto nonce = reinterpret_cast<unsigned char*>(
		result.data() + sizeof(kMagic));
	base::RandomFill(nonce, kNonceSize);
	if (EVP_EncryptInit_ex(
			cipher.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1
		|| EVP_CIPHER_CTX_ctrl(
			cipher.get(), EVP_CTRL_GCM_SET_IVLEN, kNonceSize, nullptr) != 1
		|| EVP_EncryptInit_ex(
			cipher.get(), nullptr, nullptr, context.key.data(), nonce) != 1) {
		return std::nullopt;
	}
	const auto aad = authenticatedData(message, edited);
	auto size = 0;
	if (EVP_EncryptUpdate(
			cipher.get(),
			nullptr,
			&size,
			reinterpret_cast<const unsigned char*>(aad.constData()),
			aad.size()) != 1) {
		return std::nullopt;
	}
	auto output = reinterpret_cast<unsigned char*>(
		result.data() + kHeaderSize);
	if (EVP_EncryptUpdate(
			cipher.get(),
			output,
			&size,
			reinterpret_cast<const unsigned char*>(data->constData()),
			data->size()) != 1) {
		return std::nullopt;
	}
	auto finalSize = 0;
	if (EVP_EncryptFinal_ex(cipher.get(), output + size, &finalSize) != 1
		|| size + finalSize != data->size()
		|| EVP_CIPHER_CTX_ctrl(
			cipher.get(),
			EVP_CTRL_GCM_GET_TAG,
			kTagSize,
			output + data->size()) != 1) {
		return std::nullopt;
	}
	return result;
}

std::optional<ExtrasMessageBase> decrypt(
		const Context &context,
		const QByteArray &payload,
		const ExtrasMessageBase &index,
		bool edited) {
	if (payload.size() <= kEnvelopeSize
		|| payload.size() > kMaxPayloadSize + kEnvelopeSize
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
			cipher.get(), nullptr, nullptr, context.key.data(), nonce) != 1) {
		return std::nullopt;
	}
	const auto aad = authenticatedData(index, edited);
	auto size = 0;
	if (EVP_DecryptUpdate(
			cipher.get(),
			nullptr,
			&size,
			reinterpret_cast<const unsigned char*>(aad.constData()),
			aad.size()) != 1) {
		return std::nullopt;
	}
	const auto encryptedSize = payload.size() - kEnvelopeSize;
	auto data = QByteArray(encryptedSize + EVP_MAX_BLOCK_LENGTH, Qt::Uninitialized);
	const auto clear = gsl::finally([&] {
		OPENSSL_cleanse(data.data(), data.size());
	});
	auto output = reinterpret_cast<unsigned char*>(data.data());
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
	data.resize(encryptedSize);
	auto result = deserialize(data);
	if (!result || authenticatedData(*result, edited) != aad) {
		return std::nullopt;
	}
	return result;
}

} // namespace Database::ArchiveCrypto
