/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>

namespace Core::Updates {

struct VerifiedUpdate;

[[nodiscard]] std::optional<QByteArray> DecompressUpdatePayload(
	const VerifiedUpdate &verified,
	QString *error = nullptr);

// 解压目标必须是调用方控制的新目录。
[[nodiscard]] bool ExtractUpdateFiles(
	const QByteArray &uncompressed,
	quint64 expectedVersion,
	const QString &directory,
	QString *error = nullptr);

} // namespace Core::Updates
