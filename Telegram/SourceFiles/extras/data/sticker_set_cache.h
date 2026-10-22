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
} // namespace Extras
