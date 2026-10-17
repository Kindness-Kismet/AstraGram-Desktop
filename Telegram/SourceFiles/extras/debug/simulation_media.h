#pragma once

#ifdef _DEBUG
namespace Main {
class Session;
} // namespace Main

namespace ExtrasDebug::Commands {

enum class SimulationMedia { Photo, File, Contact, Poll, Sticker, Emoji };
[[nodiscard]] MTPMessageMedia simulationMedia(
	not_null<Main::Session*> session, SimulationMedia kind, int id, uint64 setId = 0);

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
