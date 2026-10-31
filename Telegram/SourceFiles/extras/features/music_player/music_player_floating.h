#pragma once

#include "base/basic_types.h"
#include "base/object_ptr.h"
#include "data/data_audio_msg_id.h"
#include "rpl/lifetime.h"

#include <QtCore/QObject>
#include <QtCore/QRect>

namespace Ui { class RpWidget; }
namespace Window { class SessionController; }

namespace Extras::MusicPlayer {

class CompactPanel;
class FloatingButton;

class FloatingPlayer final : public QObject {
public:
	FloatingPlayer(not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller);
	~FloatingPlayer();
	// 可用区域由对话列表扣除底部状态区和按钮后提供。
	void setAvailableRect(QRect rect);
	void raise();
	void collapse();

private:
	void refresh();
	void toggle();
	void updatePosition();

	const not_null<Ui::RpWidget*> _parent;
	const not_null<Window::SessionController*> _controller;
	object_ptr<FloatingButton> _button;
	std::unique_ptr<CompactPanel> _panel;
	QRect _available;
	AudioMsgId::Type _type = AudioMsgId::Type::Song;
	AudioMsgId _closedTrack;
	bool _active = false;
	rpl::lifetime _lifetime;
};

} // namespace Extras::MusicPlayer
