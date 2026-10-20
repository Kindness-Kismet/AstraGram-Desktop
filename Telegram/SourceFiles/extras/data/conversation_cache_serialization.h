#pragma once

#include <QtCore/QDataStream>
#include "crl/crl_on_main.h"

namespace Extras::CacheSerialization {

inline constexpr auto kFormat = quint32(1);
inline constexpr auto kMaxDialogs = 1000;
inline constexpr auto kMaxHistoryMessages = 10000;
inline constexpr auto kMaxCacheBytes = 256 * 1024 * 1024;
inline constexpr auto kSnapshotPref = "conversation-cache";

template <typename Type, typename Owner, typename Callback>
[[nodiscard]] auto onMain(not_null<Owner*> owner, Callback callback) {
	const auto guarded = crl::guard(owner, std::move(callback));
	return [guarded](Type value) {
		crl::on_main([guarded, value = std::move(value)]() mutable {
			guarded(std::move(value));
		});
	};
}

template <typename Type>
[[nodiscard]] QByteArray encode(const Type &value) {
	auto buffer = mtpBuffer();
	value.write(buffer);
	return QByteArray(
		reinterpret_cast<const char*>(buffer.data()),
		buffer.size() * sizeof(mtpPrime));
}

template <typename Type>
[[nodiscard]] std::optional<Type> decode(const QByteArray &bytes) {
	if (bytes.isEmpty() || (bytes.size() % sizeof(mtpPrime))) {
		return std::nullopt;
	}
	const auto from = reinterpret_cast<const mtpPrime*>(bytes.constData());
	const auto end = from + bytes.size() / sizeof(mtpPrime);
	auto current = from;
	auto result = Type();
	return (result.read(current, end) && current == end)
		? std::make_optional(std::move(result))
		: std::nullopt;
}

inline void writeHeader(QDataStream &stream, UserId user) {
	stream.setVersion(QDataStream::Qt_5_1);
	stream << kFormat << quint32(MTP::details::kCurrentLayer)
		<< quint64(user.bare);
}

[[nodiscard]] inline bool readHeader(QDataStream &stream, UserId user) {
	stream.setVersion(QDataStream::Qt_5_1);
	auto format = quint32();
	auto layer = quint32();
	auto account = quint64();
	stream >> format >> layer >> account;
	return stream.status() == QDataStream::Ok
		&& format == kFormat
		&& layer == MTP::details::kCurrentLayer
		&& account == user.bare;
}

} // namespace Extras::CacheSerialization
