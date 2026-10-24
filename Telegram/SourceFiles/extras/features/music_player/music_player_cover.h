#pragma once

#include "base/basic_types.h"
#include "data/data_audio_msg_id.h"
#include "rpl/lifetime.h"

#include <QtCore/QObject>
#include <QtGui/QImage>

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Media::Streaming {
class Document;
} // namespace Media::Streaming

namespace Extras::MusicPlayer {

class TrackCover final : public QObject {
public:
	TrackCover(QObject *parent, Fn<void()> updated);
	void setTrack(const AudioMsgId &track);
	[[nodiscard]] const QImage &image() const;
	[[nodiscard]] QImage originalImage() const;

private:
	void refresh();

	const Fn<void()> _updated;
	AudioMsgId _track;
	std::shared_ptr<Data::DocumentMedia> _media;
	std::shared_ptr<Media::Streaming::Document> _streamed;
	QImage _image;
	bool _embedded = false;
	rpl::lifetime _downloads;
};

} // namespace Extras::MusicPlayer
