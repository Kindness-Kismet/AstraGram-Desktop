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

} // namespace Extras
