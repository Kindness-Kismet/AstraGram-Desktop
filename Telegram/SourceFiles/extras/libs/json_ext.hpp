#pragma once

#include <QString>
#include "json.hpp"

inline void to_json(nlohmann::json &j, const QString &q) {
	j = nlohmann::json(q.toStdString());
}

inline void from_json(const nlohmann::json &j, QString &q) {
	q = QString::fromStdString(j.get<std::string>());
}
