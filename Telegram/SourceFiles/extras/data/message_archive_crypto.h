#pragma once

#include "base/bytes.h"

#include <QtCore/QJsonObject>
#include <array>
#include <optional>

#include "extras/data/entities.h"

namespace Database::ArchiveCrypto {

struct Context {
	std::array<unsigned char, 32> key = {};
	QByteArray id;
};

[[nodiscard]] Context makeContext(bytes::const_span localKey);

// fakeId 必须先分配；索引字段和消息类型共同参与认证。
[[nodiscard]] std::optional<QByteArray> encrypt(
	const Context &context,
	const ExtrasMessageBase &message,
	bool edited);

[[nodiscard]] std::optional<ExtrasMessageBase> decrypt(
	const Context &context,
	const QByteArray &payload,
	const ExtrasMessageBase &index,
	bool edited);

} // namespace Database::ArchiveCrypto
