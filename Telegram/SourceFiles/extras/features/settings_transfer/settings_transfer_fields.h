#pragma once

#include "extras/features/settings_transfer/settings_transfer.h"
#include "extras/libs/json_ext.hpp"

#include <cmath>
#include <functional>
#include <map>

namespace Extras::SettingsTransfer {

struct Field {
	Json value;
	std::function<void(const Json&)> set;
	std::function<bool(const Json&)> valid;
	bool restart = false;
};
using Fields = std::map<std::string, Field>;

template <typename Object, typename Value, typename Getter>
void addField(
		Fields &fields,
		const char *key,
		Object &object,
		Getter getter,
		void (Object::*setter)(Value),
		double minimum = 0,
		double maximum = 0,
		bool restart = false) {
	using Type = std::remove_cvref_t<Value>;
	const auto encode = [](const Type &value) -> Json {
		if constexpr (std::is_enum_v<Type>) {
			return int(value);
		} else {
			return value;
		}
	};
	fields.emplace(key, Field{
		.value = encode(std::invoke(getter, object)),
		.set = [&object, setter](const Json &value) {
			if constexpr (std::is_enum_v<Type>) {
				(object.*setter)(Type(value.get<int>()));
			} else {
				(object.*setter)(value.get<Type>());
			}
		},
		.valid = [=](const Json &value) {
			if constexpr (std::is_same_v<Type, bool>) {
				return value.is_boolean();
			} else if constexpr (std::is_same_v<Type, QString>) {
				return value.is_string();
			} else {
				const auto numeric = std::is_floating_point_v<Type>
					? value.is_number()
					: value.is_number_integer();
				if (!numeric) {
					return false;
				}
				const auto number = value.get<double>();
				return std::isfinite(number)
					&& number >= minimum && number <= maximum;
			}
		},
		.restart = restart,
	});
}

[[nodiscard]] Fields officialFields();
[[nodiscard]] Fields customFields();
[[nodiscard]] Fields accountFields(not_null<Main::Session*> session);

} // namespace Extras::SettingsTransfer
