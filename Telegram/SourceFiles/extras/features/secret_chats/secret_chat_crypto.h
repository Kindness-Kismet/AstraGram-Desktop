#pragma once

#include <QtCore/QByteArray>

#include <optional>

namespace Extras::SecretChats::Crypto {

struct DhState {
	int generator = 0;
	QByteArray prime;
	QByteArray power;
	QByteArray publicValue;
};

[[nodiscard]] std::optional<DhState> createDh(
	int generator,
	const QByteArray &prime,
	const QByteArray &serverRandom);
[[nodiscard]] std::optional<QByteArray> sharedKey(
	const DhState &state,
	const QByteArray &peerPublic);
[[nodiscard]] qint64 fingerprint(const QByteArray &key);
[[nodiscard]] QByteArray verification(const QByteArray &key);

// 两端均按实际发送方的角色选择第二版消息方向。
[[nodiscard]] std::optional<QByteArray> seal(
	const QByteArray &key,
	bool senderIsCreator,
	const QByteArray &body);
[[nodiscard]] std::optional<QByteArray> open(
	const QByteArray &key,
	bool senderIsCreator,
	const QByteArray &ciphertext);

[[nodiscard]] std::optional<QByteArray> sealFile(
	const QByteArray &key,
	const QByteArray &iv,
	const QByteArray &bytes);
[[nodiscard]] std::optional<QByteArray> openFile(
	const QByteArray &key,
	const QByteArray &iv,
	const QByteArray &ciphertext,
	qint64 size);
[[nodiscard]] qint32 fileFingerprint(
	const QByteArray &key,
	const QByteArray &iv);

} // namespace Extras::SecretChats::Crypto
