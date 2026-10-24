#pragma once

#include "base/basic_types.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <memory>

namespace Extras::SecretChats {

class Store final {
public:
	struct Config {
		QString path;
		QByteArray localKey;
		quint64 accountId = 0;
		quint64 authorizationId = 0;
	};

	struct LoadResult {
		bool ok = false;
		bool exists = false;
		QByteArray state;
		QString error;
	};

	explicit Store(Config config);
	~Store();

	Store(const Store &) = delete;
	Store &operator=(const Store &) = delete;

	// 接口和回调都在主线程；销毁时等待已排队保存完成。
	void load(Fn<void(LoadResult)> done);
	void save(QByteArray state, Fn<void(bool)> done);
	void destroy(Fn<void(bool)> done);

private:
	class Private;
	const std::unique_ptr<Private> _private;
};

}
