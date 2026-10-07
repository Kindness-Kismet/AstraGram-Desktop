#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"
#include "extras/features/settings_transfer/settings_transfer.h"
#include "main/main_session.h"

#include <QDir>
#include <QFileInfo>

namespace ExtrasDebug::Commands {
namespace {

namespace Transfer = Extras::SettingsTransfer;
using Json = nlohmann::json;

std::optional<Transfer::Selection> selection(const QString &scope) {
	if (scope == u"all"_q) return Transfer::Selection{};
	if (scope == u"official"_q) return Transfer::Selection{ true, false, false };
	if (scope == u"custom"_q) return Transfer::Selection{ false, true, false };
	if (scope == u"account"_q) return Transfer::Selection{ false, false, true };
	return std::nullopt;
}

Json inspection(const Transfer::Inspection &value) {
	const auto strings = [](const QStringList &list) {
		auto result = Json::array();
		for (const auto &text : list) result.push_back(text.toStdString());
		return result;
	};
	return { { "count", value.count }, { "error", value.error.toStdString() },
		{ "skipped", strings(value.skipped) }, { "missingPaths", strings(value.missingPaths) },
		{ "unavailable", strings(value.unavailable) }, { "restart", strings(value.restart) } };
}

Result exportSettings(const QStringList &args) {
	if (args.size() != 2) return Result::Err(u"usage: settings.export <path> <all|official|custom|account>"_q);
	const auto session = ActiveSession();
	const auto scope = selection(args[1]);
	if (!session || !scope) return Result::Err(u"active session and valid scope required"_q);
	const auto path = QFileInfo(args[0]);
	if (!path.isAbsolute() || path.exists() || !path.dir().exists()) {
		return Result::Err(u"expected a new absolute file path in an existing directory"_q);
	}
	const auto id = beginJob("settings-export");
	Transfer::writeFile(path.absoluteFilePath(), Transfer::snapshot(session, *scope), [=](bool ok) {
		finishJob(id, ok, ok ? Json{ { "path", path.absoluteFilePath().toStdString() } } : Json("could not save settings file"));
	});
	return jobStarted(id);
}

Result importSettings(const QStringList &args, bool apply) {
	if (args.size() != 2) return Result::Err(u"usage: settings.inspect-import|settings.import <path> <all|official|custom|account>"_q);
	const auto session = ActiveSession();
	const auto scope = selection(args[1]);
	if (!session || !scope) return Result::Err(u"active session and valid scope required"_q);
	const auto weak = base::make_weak(session);
	const auto id = beginJob(apply ? "settings-import" : "settings-inspect");
	session->lifetime().add([id] { finishJob(id, false, "session closed"); });
	Transfer::readFile(args[0], [=](Json document, bool ok) {
		if (!weak) return;
		if (!ok) {
			finishJob(id, false, "could not read settings file");
			return;
		}
		if (!apply) {
			Transfer::prepare(weak.get(), document, *scope, [id](Transfer::Inspection result) {
				finishJob(id, result.error.isEmpty(), inspection(result));
			});
			return;
		}
		Transfer::apply(weak.get(), document, *scope, [id](Transfer::Inspection result, bool saved) {
			auto details = inspection(result);
			details["saved"] = saved;
			finishJob(id, saved && result.error.isEmpty(), std::move(details));
		});
	});
	return jobStarted(id);
}

Result inspectImport(const QStringList &args) { return importSettings(args, false); }
Result applyImport(const QStringList &args) { return importSettings(args, true); }

} // namespace

const HandlerMap &transferHandlers() {
	static const auto handlers = HandlerMap{
		{ u"settings.export"_q, &exportSettings },
		{ u"settings.inspect-import"_q, &inspectImport },
		{ u"settings.import"_q, &applyImport },
	};
	return handlers;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
