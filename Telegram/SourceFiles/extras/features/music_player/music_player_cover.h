#pragma once

#include "base/basic_types.h"
#include "data/data_audio_msg_id.h"
#include "rpl/lifetime.h"

#include <QtCore/QObject>
#include <QtGui/QImage>

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Extras::MusicPlayer {

class TrackCover final : public QObject {
public:
	TrackCover(QObject *parent, Fn<void()> updated);
	void setTrack(const AudioMsgId &track);
	[[nodiscard]] const QImage &image() const;

private:
	void refresh();

	const Fn<void()> _updated;
	AudioMsgId _track;
	std::shared_ptr<Data::DocumentMedia> _media;
	QImage _image;
	bool _extracting = false;
	bool _embedded = false;
	rpl::lifetime _downloads;
};

} // namespace Extras::MusicPlayer
