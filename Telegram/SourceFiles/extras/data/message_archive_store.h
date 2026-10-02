#pragma once

#include "base/bytes.h"

#include <QtCore/QJsonObject>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "extras/data/entities.h"

namespace Database::Archive {

class Store final {
public:
	Store();
	~Store();

	Store(const Store &) = delete;
	Store &operator=(const Store &) = delete;

	[[nodiscard]] bool unlock(
		const QString &databasePath,
		bytes::const_span localKey);
	void close();
	[[nodiscard]] bool available() const;
	[[nodiscard]] QString error() const;

	// 无法确认留档为空时返回 true，避免误取消本地密码。
	[[nodiscard]] bool hasMessages();
	[[nodiscard]] bool add(const ExtrasMessageBase &message, bool edited);
	[[nodiscard]] std::vector<ExtrasMessageBase> get(
		bool edited,
		ID userId,
		ID dialogId,
		ID topicId,
		ID messageId,
		ID minId,
		ID maxId,
		int limit,
		const std::string &search = {},
		QString *queryError = nullptr);
	[[nodiscard]] std::vector<ID> deletedIds(
		ID userId,
		ID dialogId,
		ID topicId);
	[[nodiscard]] bool contains(
		bool edited,
		ID userId,
		ID dialogId,
		ID topicId = 0,
		ID messageId = 0);
	[[nodiscard]] bool remove(ID userId, ID dialogId, ID messageId);
	[[nodiscard]] bool clear(ID userId, ID dialogId, ID topicId);

private:
	class Private;
	const std::unique_ptr<Private> _private;
};

}
