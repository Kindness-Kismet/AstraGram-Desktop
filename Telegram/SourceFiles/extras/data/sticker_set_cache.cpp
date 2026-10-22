#include "extras/data/sticker_set_cache.h"

#include "extras/data/conversation_cache_serialization.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "storage/storage_account.h"
#include "storage/cache/storage_cache_database.h"

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

Storage::Cache::Key customEmojiKey(DocumentId id) {
	return { 0x415347454d4f4a49ULL, id };
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

void cachedCustomEmoji(
		not_null<Main::Session*> session,
		DocumentId id,
		Fn<void(std::optional<MTPDocument>)> done) {
	session->data().cache().get(customEmojiKey(id),
		CacheSerialization::onMain<QByteArray>(session,
		[=](QByteArray bytes) {
			QDataStream stream(bytes);
			if (!CacheSerialization::readHeader(stream, session->userId())) {
				done(std::nullopt);
				return;
			}
			auto payload = QByteArray();
			stream >> payload;
			auto document = CacheSerialization::decode<MTPDocument>(payload);
			if (stream.status() != QDataStream::Ok || !stream.atEnd()
				|| !document || document->type() != mtpc_document
				|| document->c_document().vid().v != id) {
				done(std::nullopt);
				return;
			}
			done(std::move(document));
		}));
}

void cacheCustomEmoji(
		not_null<Main::Session*> session,
		const MTPDdocument &document) {
	if (ranges::none_of(document.vattributes().v, [](const MTPDocumentAttribute &attribute) {
		return attribute.type() == mtpc_documentAttributeCustomEmoji;
	})) {
		return;
	}
	auto bytes = QByteArray();
	QDataStream stream(&bytes, QIODevice::WriteOnly);
	CacheSerialization::writeHeader(stream, session->userId());
	stream << CacheSerialization::encode(MTP_document(document.vflags(),
		document.vid(), document.vaccess_hash(), document.vfile_reference(),
		document.vdate(), document.vmime_type(), document.vsize(),
		MTP_vector<MTPPhotoSize>(document.vthumbs().value_or_empty()),
		MTP_vector<MTPVideoSize>(document.vvideo_thumbs().value_or_empty()),
		document.vdc_id(), document.vattributes()));
	// 资料和帧同属可清理的贴纸缓存，容量由媒体库限制。
	session->data().cache().put(customEmojiKey(document.vid().v),
		Storage::Cache::Database::TaggedValue(std::move(bytes), Data::kStickerCacheTag),
		CacheSerialization::onMain<Storage::Cache::Error>(session, [](auto error) {
			if (error.type != Storage::Cache::Error::Type::None) {
				LOG(("Custom emoji cache: could not save document metadata."));
			}
		}));
}

} // namespace Extras
