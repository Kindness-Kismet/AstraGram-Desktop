#pragma once

#include <QString>
#include <QStringList>

namespace ExtrasDebug {

struct Result {
	bool ok = false;
	QString payload;

	[[nodiscard]] static Result Ok(QString payload = QString()) {
		return { true, std::move(payload) };
	}
	[[nodiscard]] static Result Err(QString reason) {
		return { false, std::move(reason) };
	}
};

// 在主线程同步执行一条指令。未知指令返回 Err，不抛异常。
[[nodiscard]] Result Execute(const QString &command, const QStringList &args);

// 所有已注册指令的名字，供 help 使用。
[[nodiscard]] QStringList CommandNames();

} // namespace ExtrasDebug
