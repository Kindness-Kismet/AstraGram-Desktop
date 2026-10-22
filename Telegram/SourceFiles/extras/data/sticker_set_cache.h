#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Extras {

enum class EmojiSetType {
	Animated,
	Interactions,
	Standard,
};

[[nodiscard]] std::optional<MTPmessages_StickerSet> cachedEmojiSet(
	not_null<Main::Session*> session,
	EmojiSetType type);
void cacheEmojiSet(
	not_null<Main::Session*> session,
	EmojiSetType type,
	const MTPmessages_StickerSet &set);
[[nodiscard]] int cachedEmojiSetBytes(
	not_null<Main::Session*> session,
	EmojiSetType type);

[[nodiscard]] std::optional<MTPmessages_StickerSet> cachedStickerSet(
	not_null<Main::Session*> session,
	const QString &name);
void cacheStickerSet(
	not_null<Main::Session*> session,
	const QString &name,
	const MTPmessages_StickerSet &set);
void cachedStickerPack(
	not_null<Main::Session*> session,
	const StickerSetIdentifier &set,
	Fn<void(std::optional<MTPmessages_StickerSet>)> done);
void cacheStickerPack(
	not_null<Main::Session*> session,
	const MTPDmessages_stickerSet &set);
void cachedCustomEmoji(
	not_null<Main::Session*> session,
	DocumentId id,
	Fn<void(std::optional<MTPDocument>)> done);
void cacheCustomEmoji(
	not_null<Main::Session*> session,
	const MTPDdocument &document);

} // namespace Extras
