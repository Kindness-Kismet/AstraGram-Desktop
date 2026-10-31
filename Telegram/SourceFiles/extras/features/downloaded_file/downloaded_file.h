#pragma once

#include "core/file_location.h"

class DocumentData;

namespace ExtrasDownloadedFile {

[[nodiscard]] Core::FileLocation location(
	not_null<DocumentData*> document,
	const QString &path = {});

// 只移入回收站；失败时保留文件和下载状态。
void remove(
	not_null<DocumentData*> document,
	Core::FileLocation file,
	Fn<void(bool)> done);

} // namespace ExtrasDownloadedFile
