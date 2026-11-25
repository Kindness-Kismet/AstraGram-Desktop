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

struct LegacyUpdate {
	quint32 version = 0;
	QByteArray uncompressed;
};

// 普通更新与提权安装共用验签，调用方只传应用内置公钥。
[[nodiscard]] std::optional<LegacyUpdate> VerifyLegacyUpdate(
	const QByteArray &bytes,
	const QByteArray &publicKey,
	quint32 currentVersion,
	QString *error = nullptr);

[[nodiscard]] std::optional<QByteArray> DecompressUpdatePayload(
	const VerifiedUpdate &verified,
	QString *error = nullptr);

// 调用方必须先验证整个数据体的签名。
[[nodiscard]] std::optional<QByteArray> DecompressAuthenticatedPayload(
	const QByteArray &payload,
	QString *error = nullptr);

// Extraction requires a fresh caller-controlled directory.
[[nodiscard]] bool ExtractUpdateFiles(
	const QByteArray &uncompressed,
	quint64 expectedVersion,
	const QString &directory,
	QString *error = nullptr);

} // namespace Core::Updates
