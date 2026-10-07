#include "info/downloads/info_downloads_common.h"

#include "core/mime_type.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "history/history_item.h"

namespace Info::Downloads {

[[nodiscard]] TypeFilter ClassifyFile(
		not_null<HistoryItem*> item,
		const QString &path) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	if (!document || document->isVoiceMessage() || document->sticker()) {
		return TypeFilter::Other;
	}
	if (document->isSong()) {
		return TypeFilter::Music;
	}
	if (document->isVideoFile() || document->isVideoMessage()) {
		return TypeFilter::Video;
	}
	const auto filename = document->filename();
	const auto name = filename.isEmpty() ? path : filename;
	const auto nameType = filename.isEmpty()
		? Core::DetectNameType(path)
		: document->nameType();
	if (nameType == Core::NameType::Archive) {
		return TypeFilter::Archive;
	}
	const auto mime = document->mimeString();
	if (mime.startsWith(u"video/"_q, Qt::CaseInsensitive)) {
		return TypeFilter::Video;
	}
	if (document->isAudioFile()
		|| mime.startsWith(u"audio/"_q, Qt::CaseInsensitive)) {
		return TypeFilter::Music;
	}
	if (nameType == Core::NameType::Video) {
		const auto extension = Core::FileExtension(name).toLower();
		// 字幕、工程文件与动画贴纸不是视频文件。
		return (extension == u"srt" || extension == u"aep"
			|| extension == u"tgs" || extension == u"tgv")
			? TypeFilter::Other
			: TypeFilter::Video;
	}
	return (nameType == Core::NameType::Audio)
		? TypeFilter::Music
		: TypeFilter::Other;
}


} // namespace Info::Downloads
