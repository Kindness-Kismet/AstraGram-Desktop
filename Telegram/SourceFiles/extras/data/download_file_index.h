#pragma once

#include "base/timer.h"
#include "core/file_location.h"
#include "data/data_types.h"

namespace MTP {
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;
} // namespace MTP

namespace Extras {

// 按远端媒体标识共享已完成文件，账号缓存和临时下载仍由各账号管理。
class DownloadFileIndex final {
public:
	DownloadFileIndex(QString name, QString basePath, MTP::AuthKeyPtr key);
	~DownloadFileIndex();

	void remember(MediaKey key, const Core::FileLocation &location);
	[[nodiscard]] Core::FileLocation lookup(MediaKey key);
	void flush();

private:
	void restore();
	void scheduleSave();
	[[nodiscard]] bool accepts(MediaKey key, const Core::FileLocation &location) const;

	const QString _name;
	const QString _basePath;
	const MTP::AuthKeyPtr _key;
	std::map<MediaKey, std::vector<Core::FileLocation>> _files;
	base::Timer _saveTimer;
	bool _dirty = false;

};

} // namespace Extras
