#pragma once

class AudioMsgId;
class DocumentData;

namespace Extras::MusicPlayer {

// 只导出完整缓存，沿用下载目录及内容保护设置。
void saveCachedTrack(const AudioMsgId &track);

[[nodiscard]] float64 trackDownloadProgress(not_null<DocumentData*> document);

} // namespace Extras::MusicPlayer
