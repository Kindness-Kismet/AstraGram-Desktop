#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"
#include "extras/debug/debug_login.h"
#include "extras/debug/dialogs_preview.h"
#include "extras/debug/simulation_scenarios.h"
#include "dialogs/dialogs_widget.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"

namespace ExtrasDebug::Commands {
using json = nlohmann::json;

const HandlerMap &simulationHandlers() {
	static const auto result = HandlerMap{
		{ u"simulation.trigger"_q, [](const QStringList &args) {
			if (args.size() > 1) {
				return Result::Err(u"usage: simulation.trigger [list|key]"_q);
			}
			if (args.size() == 1 && args.front() == u"delete-countdown"_q) {
				return triggerSimulationCountdown({});
			}
			const auto entries = dialogsPreviewEntries();
			if (args.empty() || args.front() == u"list"_q) {
				auto result = json::array();
				for (const auto &entry : entries) {
					result.push_back({ { "key", entry.key },
						{ "name", entry.title.toStdString() } });
				}
				result.push_back({ { "key", "delete-countdown" },
					{ "name", tr::extras_SimulationCountdown(tr::now).toStdString() } });
				return Result::Ok(Compact(result));
			}
			const auto session = ActiveSession();
			if (!session || !isSimulationSession(session)) {
				return Result::Err(u"simulation mode is required"_q);
			}
			const auto controller = session->tryResolveWindow();
			const auto dialogs = controller ? dialogsPreviewWidget(controller) : nullptr;
			if (!dialogs) {
				return Result::Err(u"no chat list widget"_q);
			}
			for (const auto &entry : entries) {
				if (args.front() == QLatin1String(entry.key)) {
					dialogs->setListPreview(entry.id);
					return Result::Ok(Compact({ { "key", entry.key } }));
				}
			}
			return Result::Err(u"unknown scene, use simulation.trigger list"_q);
		} },
		{ u"simulation.clear"_q, [](const QStringList &args) {
			const auto session = ActiveSession();
			if (!args.empty() || !session || !isSimulationSession(session)) {
				return Result::Err(u"simulation.clear requires simulation mode and no arguments"_q);
			}
			const auto window = session->tryResolveWindow();
			const auto dialogs = window ? dialogsPreviewWidget(window) : nullptr;
			if (!dialogs) {
				return Result::Err(u"no chat list widget"_q);
			}
			dialogs->setListPreview(DialogsPreview::None);
			return Result::Ok();
		} },
		{ u"simulation.list"_q, &listSimulationScenes },
		{ u"simulation.open"_q, &openSimulationScene },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
