#pragma once

#include "data/data_audio_msg_id.h"
#include "ui/rp_widget.h"

namespace Extras::MusicPlayer {

class QueueRow;

class PlayerQueue final : public Ui::RpWidget {
public:
	explicit PlayerQueue(QWidget *parent);
	void refresh();
	[[nodiscard]] int count() const;
	[[nodiscard]] int currentTop() const;

protected:
	void resizeEvent(QResizeEvent *event) override;

private:
	std::vector<AudioMsgId> _tracks;
	std::vector<QueueRow*> _rows;
};

} // namespace Extras::MusicPlayer
