#pragma once

#ifdef _DEBUG

#include "data/data_peer_id.h"
#include "extras/debug/debug_commands.h"

namespace ExtrasDebug::Commands {

enum class SimulationCategory {
	Private,
	Groups,
	Channels,
	Bots,
	Topics,
	Saved,
	Archive,
};

struct SimulationScene {
	QString key;
	QString name;
	SimulationCategory category;
	QStringList features;
	PeerId peerId;
};

[[nodiscard]] QString simulationCategoryName(SimulationCategory category);
[[nodiscard]] std::vector<SimulationScene> simulationScenes();
[[nodiscard]] Result listSimulationScenes(const QStringList &args);
[[nodiscard]] Result openSimulationScene(const QStringList &args);
[[nodiscard]] Result triggerSimulationCountdown(const QStringList &args);

} // namespace ExtrasDebug::Commands

#endif // _DEBUG
