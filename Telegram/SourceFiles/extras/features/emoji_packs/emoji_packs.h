#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

#include <vector>

namespace Extras::EmojiPacks {

struct Pack {
	int id = 0;
	QString name;
	QString previewPath;
	QString hash;
};

struct Preset {
	QString id;
	QString name;
	QString url;
	QString hash;
	int64 size = 0;
};

enum class ImportError {
	None,
	Busy,
	Read,
	Font,
	Full,
	Write,
	Download,
	Cancelled,
};

struct ImportResult {
	Pack pack;
	ImportError error = ImportError::None;
};

// 下载进度；total 为 0 表示服务端未给出长度。
struct DownloadProgress {
	int64 already = 0;
	int64 total = 0;
};

[[nodiscard]] bool isCustom(int id);
[[nodiscard]] std::vector<Pack> installed();
[[nodiscard]] std::vector<Preset> presets();

// 主线程发起；转换在后台执行，回调回到主线程。
void importFont(const QString &path, Fn<void(ImportResult)> done,
	const QString &presetName = {});

// 下载预设字体后转换，字体文件用完即删。progress 与 done 都回到主线程。
void installPreset(const Preset &preset,
	Fn<void(DownloadProgress)> progress,
	Fn<void(ImportResult)> done);

// 取消 installPreset 发起的下载，已在转换阶段则不受影响。
void cancelPreset(const QString &id);

} // namespace Extras::EmojiPacks
