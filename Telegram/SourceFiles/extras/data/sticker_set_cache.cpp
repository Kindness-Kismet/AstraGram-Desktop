#include "extras/data/sticker_set_cache.h"

#include "extras/data/conversation_cache_serialization.h"
#include "main/main_session.h"
#include "storage/storage_account.h"

namespace Extras {
namespace {

std::string_view preferenceKey(EmojiSetType type) {
	switch (type) {
	case EmojiSetType::Animated: return "emoji-set-animated";
	case EmojiSetType::Interactions: return "emoji-set-interactions";
	case EmojiSetType::Standard: return "emoji-set-standard";
	}
	Unexpected("EmojiSetType");
}

std::optional<MTPmessages_StickerSet> readSet(
		not_null<Main::Session*> session,
		std::string_view key) {
	const auto bytes = session->local().readPref<QByteArray>(key);
	if (bytes.isEmpty()) {
		return std::nullopt;
	}
	QDataStream stream(bytes);
	if (!CacheSerialization::readHeader(stream, session->userId())) {
		return std::nullopt;
	}
	auto payload = QByteArray();
	stream >> payload;
	if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
		return std::nullopt;
	}
	auto result = CacheSerialization::decode<MTPmessages_StickerSet>(payload);
	return result && result->type() == mtpc_messages_stickerSet
		? std::move(result)
		: std::nullopt;
}

void writeSet(
		not_null<Main::Session*> session,
		std::string_view key,
		const MTPmessages_StickerSet &set) {
	Expects(set.type() == mtpc_messages_stickerSet);
	auto bytes = QByteArray();
	QDataStream stream(&bytes, QIODevice::WriteOnly);
	CacheSerialization::writeHeader(stream, session->userId());
	stream << CacheSerialization::encode(set);
	// 名单保存在账号加密配置，动画文件继续由媒体缓存保存。
	session->local().writePref<QByteArray>(key, std::move(bytes));
}

} // namespace

std::optional<MTPmessages_StickerSet> cachedEmojiSet(
		not_null<Main::Session*> session,
		EmojiSetType type) {
	return readSet(session, preferenceKey(type));
}

void cacheEmojiSet(
		not_null<Main::Session*> session,
		EmojiSetType type,
		const MTPmessages_StickerSet &set) {
	writeSet(session, preferenceKey(type), set);
}

int cachedEmojiSetBytes(
		not_null<Main::Session*> session,
		EmojiSetType type) {
	return session->local().readPref<QByteArray>(preferenceKey(type)).size();
}

std::optional<MTPmessages_StickerSet> cachedStickerSet(
		not_null<Main::Session*> session,
		const QString &name) {
	return readSet(session, (u"special-sticker-set-"_q + name).toStdString());
}

void cacheStickerSet(
		not_null<Main::Session*> session,
		const QString &name,
		const MTPmessages_StickerSet &set) {
	writeSet(session, (u"special-sticker-set-"_q + name).toStdString(), set);
}

} // namespace Extras
