#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QVector>

#include <optional>

namespace Extras::SecretChats::Codec {

inline constexpr auto kMaxPacketBytes = 1024 * 1024;
inline constexpr auto kMaxFileBytes = qint64(32) * 1024 * 1024;

enum class Action {
	None,
	NotifyLayer,
	SetTtl,
	Read,
	Delete,
	Screenshot,
	Flush,
	Resend,
	RequestKey,
	AcceptKey,
	CommitKey,
	AbortKey,
	Noop,
	Typing,
};

struct Packet {
	int layer = 144;
	int inSeq = 0;
	int outSeq = 0;
	qint64 randomId = 0;
	int ttl = 0;
	QString text;
	Action action = Action::None;
	QVector<qint64> ids;
	int value = 0;
	int endSeq = 0;
	qint64 exchangeId = 0;
	QByteArray publicValue;
	qint64 keyFingerprint = 0;
	bool document = false;
	QString mimeType;
	QString fileName;
	qint64 fileSize = 0;
	QByteArray fileKey;
	QByteArray fileIv;
	QByteArray thumb;
	int thumbWidth = 0;
	int thumbHeight = 0;
	QString caption;
	bool unsupported = false;
};

[[nodiscard]] std::optional<QByteArray> encode(const Packet &packet);
[[nodiscard]] std::optional<Packet> decode(const QByteArray &bytes);

} // namespace Extras::SecretChats::Codec
