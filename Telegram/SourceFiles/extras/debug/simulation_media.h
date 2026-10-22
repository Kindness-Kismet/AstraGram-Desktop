#pragma once

#ifdef _DEBUG
namespace Main {
class Session;
} // namespace Main

namespace ExtrasDebug::Commands {

enum class SimulationMedia { Photo, File, Contact, Poll, Sticker, Emoji };
[[nodiscard]] MTPMessageMedia simulationMedia(
	not_null<Main::Session*> session, SimulationMedia kind, int id, uint64 setId = 0);
[[nodiscard]] std::optional<MTPMessageMedia> simulationPhotoMedia(
	not_null<Main::Session*> session, const QString &path, int32 messageId);
[[nodiscard]] std::optional<MTPMessageMedia> simulationVideoMedia(
	not_null<Main::Session*> session, const QString &path, int32 messageId);

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
