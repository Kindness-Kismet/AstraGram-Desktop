#include "extras/data/sticker_set_cache.h"

#include "extras/data/conversation_cache_serialization.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "storage/storage_account.h"
#include "storage/cache/storage_cache_database.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QtEndian>

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

Storage::Cache::Key stickerPackKey(const StickerSetIdentifier &set) {
	if (set.id) {
		return { 0x4153475041434b49ULL, set.id };
	}
	const auto hash = QCryptographicHash::hash(
		set.shortName.toLower().toUtf8(), QCryptographicHash::Sha256);
	return { 0x4153475041434b4eULL, qFromBigEndian<quint64>(hash.constData()) };
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

void cachedStickerPack(
		not_null<Main::Session*> session,
		const StickerSetIdentifier &set,
		Fn<void(std::optional<MTPmessages_StickerSet>)> done) {
	session->data().cache().get(stickerPackKey(set),
		CacheSerialization::onMain<QByteArray>(session,
		[=](QByteArray bytes) {
			QDataStream stream(bytes);
			if (!CacheSerialization::readHeader(stream, session->userId())) {
				done(std::nullopt);
				return;
			}
			auto payload = QByteArray();
			stream >> payload;
			auto pack = CacheSerialization::decode<MTPmessages_StickerSet>(payload);
			if (stream.status() != QDataStream::Ok || !stream.atEnd()
				|| !pack || pack->type() != mtpc_messages_stickerSet) {
				LOG(("Sticker pack cache: invalid metadata payload."));
				done(std::nullopt);
				return;
			}
			const auto &info = pack->c_messages_stickerSet().vset().data();
			if (set.id ? info.vid().v != set.id
				: qs(info.vshort_name()).compare(set.shortName, Qt::CaseInsensitive) != 0) {
				LOG(("Sticker pack cache: metadata identifier mismatch."));
				done(std::nullopt);
				return;
			}
			done(std::move(pack));
		}));
}

void cacheStickerPack(
		not_null<Main::Session*> session,
		const MTPDmessages_stickerSet &set) {
	const auto &info = set.vset().data();
	auto bytes = QByteArray();
	QDataStream stream(&bytes, QIODevice::WriteOnly);
	CacheSerialization::writeHeader(stream, session->userId());
	stream << CacheSerialization::encode<MTPmessages_StickerSet>(MTP_messages_stickerSet(
		set.vset(), set.vpacks(), set.vkeywords(), set.vdocuments()));
	const auto save = [&](const StickerSetIdentifier &identifier) {
		session->data().cache().put(stickerPackKey(identifier),
			Storage::Cache::Database::TaggedValue(base::duplicate(bytes), Data::kStickerCacheTag),
			CacheSerialization::onMain<Storage::Cache::Error>(session, [](auto error) {
				if (error.type != Storage::Cache::Error::Type::None) {
					LOG(("Sticker pack cache: could not save metadata."));
				}
			}));
	};
	// 编号和短名称共用受容量限制的贴纸缓存，链接预览也能离线查询。
	save({ info.vid().v, info.vaccess_hash().v });
	if (!info.vshort_name().v.isEmpty()) {
		save({ 0, 0, qs(info.vshort_name()) });
	}
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
	stream << CacheSerialization::encode<MTPDocument>(MTP_document(document.vflags(),
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
