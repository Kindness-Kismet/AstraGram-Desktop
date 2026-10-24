#include "extras/features/music_player/music_player_download.h"

#include "core/application.h"
#include "data/data_audio_msg_id.h"
#include "data/data_document.h"
#include "data/data_file_click_handler.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "main/main_session.h"

namespace Extras::MusicPlayer {

void saveCachedTrack(const AudioMsgId &track) {
	const auto document = track.audio();
	if (!document || !document->isAudioFile() || document->hasLocalSource()) {
		return;
	}
	crl::on_main(&document->session(), [=] {
		const auto item = document->owner().message(track.contextId());
		if (!item || !item->allowsMediaDownloadControls() || document->forbidsFileSave()
			|| !document->loadedInMediaCache() || document->loading()
			|| document->cancelled() || !document->filepath(true).isEmpty()
			|| !Core::App().canSaveFileWithoutAskingForPath()) {
			return;
		}
		DocumentSaveClickHandler::SaveAndTrack(
			track.contextId(), document, DocumentSaveClickHandler::Mode::ToFile);
	});
}

} // namespace Extras::MusicPlayer
