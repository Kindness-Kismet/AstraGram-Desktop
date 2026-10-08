#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"
#include "extras/debug/simulation_scenarios.h"

namespace ExtrasDebug::Commands {

const HandlerMap &simulationHandlers() {
	static const auto result = HandlerMap{
		{ u"simulation.list"_q, &listSimulationScenes },
		{ u"simulation.open"_q, &openSimulationScene },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
