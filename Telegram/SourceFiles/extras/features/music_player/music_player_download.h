#pragma once

class AudioMsgId;

namespace Extras::MusicPlayer {

// 只导出完整缓存，沿用下载目录及内容保护设置。
void saveCachedTrack(const AudioMsgId &track);

} // namespace Extras::MusicPlayer
