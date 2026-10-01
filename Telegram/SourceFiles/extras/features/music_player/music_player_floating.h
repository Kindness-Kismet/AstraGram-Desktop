#pragma once

#include "base/basic_types.h"
#include "base/object_ptr.h"
#include "data/data_audio_msg_id.h"
#include "rpl/lifetime.h"
#include "rpl/event_stream.h"

#include <QtCore/QObject>
#include <QtCore/QRect>

namespace Ui {
class RpWidget;
class InnerDropdown;
} // namespace Ui

namespace Media::Player {
class Panel;
} // namespace Media::Player

namespace Window {
class SessionController;
} // namespace Window

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
	[[nodiscard]] bool expanded() const { return _shown; }
	[[nodiscard]] rpl::producer<> expandedChanges() const {
		return _expandedChanges.events();
	}

protected:
	bool eventFilter(QObject *object, QEvent *event) override;

private:
	void refresh();
	void toggle();
	void ensurePanel();
	void togglePlaylist();
	void updatePosition();
	[[nodiscard]] bool contains(QWidget *widget) const;

	const not_null<Ui::RpWidget*> _parent;
	const not_null<Window::SessionController*> _controller;
	object_ptr<FloatingButton> _button;
	object_ptr<Ui::InnerDropdown> _dropdown;
	object_ptr<Media::Player::Panel> _playlist;
	CompactPanel *_panel = nullptr;
	QRect _available;
	AudioMsgId::Type _type = AudioMsgId::Type::Song;
	bool _active = false;
	bool _shown = false;
	bool _playlistShown = false;
	bool _playlistOnly = false;
	bool _positioning = false;
	rpl::event_stream<> _expandedChanges;
	rpl::lifetime _lifetime;
};

} // namespace Extras::MusicPlayer
