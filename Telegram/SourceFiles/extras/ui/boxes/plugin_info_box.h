#pragma once

#include <QString>

namespace Window {
class SessionController;
}

namespace Ui {

class GenericBox;

struct PluginMetadata {
	QString id;
	QString name;
	QString description;
	QString author;
	QString version;
	QString icon;
	QString minVersion;
	QStringList requirements;
};

[[nodiscard]] PluginMetadata ParsePluginMetadata(const QByteArray &data);

void ShowPluginInfoBox(
	not_null<Window::SessionController*> controller,
	const QString& pluginPath,
	PluginMetadata metadata);

} // namespace Ui
