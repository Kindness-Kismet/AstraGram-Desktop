#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"
#include "extras/features/performance/window_performance.h"

#include "core/application.h"
#include "mainwindow.h"
#include "window/window_controller.h"

namespace ExtrasDebug::Commands {
namespace {

ExtrasPerformance::Monitor *activeMonitor() {
	const auto window = Core::App().activeWindow();
	return window ? ExtrasPerformance::findMonitor(window->widget().get()) : nullptr;
}

Result startCapture(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: perf.start"_q);
	}
	const auto monitor = activeMonitor();
	if (!monitor) {
		return Result::Err(u"no window performance monitor"_q);
	}
	if (!monitor->startCapture()) {
		return Result::Err(u"frame rate display is disabled"_q);
	}
	return Result::Ok();
}

Result stopCapture(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: perf.stop"_q);
	}
	const auto monitor = ExtrasPerformance::capturingMonitor();
	if (!monitor) {
		return Result::Err(u"no active performance capture"_q);
	}
	monitor->stopCapture();
	return Result::Ok(Compact(monitor->snapshot()));
}

Result captureStatus(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: perf.status"_q);
	}
	const auto monitor = activeMonitor();
	return monitor
		? Result::Ok(Compact(monitor->snapshot()))
		: Result::Err(u"no window performance monitor"_q);
}

} // namespace

const HandlerMap &performanceHandlers() {
	static const auto result = HandlerMap{
		{ u"perf.start"_q, &startCapture },
		{ u"perf.stop"_q, &stopCapture },
		{ u"perf.status"_q, &captureStatus },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
