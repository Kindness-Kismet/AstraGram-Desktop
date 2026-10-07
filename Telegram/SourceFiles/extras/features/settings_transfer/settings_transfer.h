#pragma once

#include "extras/libs/json_ext.hpp"

#include <QtCore/QStringList>

namespace Main {
class Session;
} // namespace Main

namespace Extras::SettingsTransfer {

using Json = nlohmann::json;

struct Selection {
	bool official = true;
	bool custom = true;
	bool account = true;
};

struct Inspection {
	Json document;
	QString error;
	QStringList skipped;
	QStringList missingPaths;
	QStringList unavailable;
	QStringList restart;
	int count = 0;
};

[[nodiscard]] Json snapshot(not_null<Main::Session*> session, Selection selection);
[[nodiscard]] Inspection inspect(
	not_null<Main::Session*> session,
	const Json &document,
	Selection selection);
void prepare(
	not_null<Main::Session*> session,
	const Json &document,
	Selection selection,
	Fn<void(Inspection)> done);
void apply(
	not_null<Main::Session*> session,
	const Json &document,
	Selection selection,
	Fn<void(Inspection, bool)> done);
void readFile(QString path, Fn<void(Json, bool)> done);
void writeFile(QString path, Json document, Fn<void(bool)> done);

} // namespace Extras::SettingsTransfer
