#pragma once

#ifdef _DEBUG
#include "ayu/debug/commands/commands_internal.h"
#include "ayu/libs/json_ext.hpp"

#include <cmath>
#include <functional>
#include <limits>
#include <type_traits>

namespace AyuDebug::Commands {

using Json = nlohmann::json;

struct SettingEntry {
	std::function<Json()> get;
	std::function<Result(const Json&)> set;
};
using SettingsMap = std::map<QString, SettingEntry>;

template <typename T>
struct OptionalValue { static constexpr bool optional = false; };
template <typename T>
struct OptionalValue<std::optional<T>> {
	static constexpr bool optional = true;
	using Type = T;
};

template <typename T>
Json settingJson(const T &value) {
	if constexpr (std::is_same_v<T, QByteArray>) {
		return value.toBase64().toStdString();
	} else if constexpr (OptionalValue<T>::optional) {
		return value ? settingJson(*value) : Json(nullptr);
	} else {
		return Json(value);
	}
}

template <typename T>
QString readSetting(const Json &value, T &result) {
	if constexpr (OptionalValue<T>::optional) {
		if (value.is_null()) {
			result.reset();
			return {};
		}
		typename OptionalValue<T>::Type inner{};
		if (const auto error = readSetting(value, inner); !error.isEmpty()) {
			return error;
		}
		result = std::move(inner);
	} else if constexpr (std::is_same_v<T, bool>) {
		if (!value.is_boolean()) return u"expected a boolean"_q;
		result = value.get<bool>();
	} else if constexpr (std::is_integral_v<T>) {
		if (!value.is_number_integer()) return u"expected an integer"_q;
		if (value.is_number_unsigned()) {
			if (value.get<uint64>() > uint64(std::numeric_limits<T>::max())) {
				return u"integer out of range"_q;
			}
		} else {
			const auto number = value.get<int64>();
			if constexpr (std::is_unsigned_v<T>) {
				if (number < 0 || uint64(number) > std::numeric_limits<T>::max()) {
					return u"integer out of range"_q;
				}
			} else if (number < std::numeric_limits<T>::lowest()
				|| number > std::numeric_limits<T>::max()) {
				return u"integer out of range"_q;
			}
		}
		result = value.get<T>();
	} else if constexpr (std::is_floating_point_v<T>) {
		if (!value.is_number()) return u"expected a number"_q;
		const auto number = value.get<double>();
		if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<T>::max()) {
			return u"number out of range"_q;
		}
		result = T(number);
	} else if constexpr (std::is_same_v<T, QString>) {
		if (!value.is_string()) return u"expected a string"_q;
		result = QString::fromStdString(value.get<std::string>());
	} else if constexpr (std::is_same_v<T, QByteArray>) {
		if (!value.is_string()) return u"expected a Base64 string"_q;
		const auto encoded = QByteArray::fromStdString(value.get<std::string>());
		result = QByteArray::fromBase64(encoded);
		if (result.toBase64() != encoded) return u"invalid Base64 encoding"_q;
	} else if constexpr (requires(T values) { typename T::value_type; values.begin(); values.end(); }) {
		if (!value.is_array()) return u"expected an array"_q;
		for (const auto &element : value) {
			typename T::value_type parsed{};
			if (const auto error = readSetting(element, parsed); !error.isEmpty()) return error;
			if constexpr (requires { result.push_back(std::move(parsed)); }) {
				result.push_back(std::move(parsed));
			} else {
				result.insert(std::move(parsed));
			}
		}
	} else {
		result = value.get<T>();
		if (settingJson(result) != value) return u"invalid or out-of-range setting value"_q;
	}
	return {};
}

// 每次执行重新绑定当前对象，避免账号切换后持有已销毁的设置对象。
template <typename Object, typename Getter, typename Value>
void addSetting(
		SettingsMap &entries,
		QString key,
		Getter getter,
		Object &object,
		void (Object::*setter)(Value)) {
	entries.emplace(std::move(key), SettingEntry{
		[getter] { return settingJson(getter()); },
		[&object, setter](const Json &value) {
			std::remove_cvref_t<Value> parsed{};
			if (const auto error = readSetting(value, parsed); !error.isEmpty()) {
				return Result::Err(error);
			}
			(object.*setter)(std::move(parsed));
			return Result::Ok();
		},
	});
}

void addAyuSettings(SettingsMap &entries);
void addCoreSettings(SettingsMap &entries);
void addSessionSettings(SettingsMap &entries);
void addProxySettings(SettingsMap &entries);
[[nodiscard]] SettingsMap settingsEntries();
[[nodiscard]] Result setSetting(const QString &key, const QString &raw);
[[nodiscard]] Result getSetting(const QString &key);

} // namespace AyuDebug::Commands
#endif
