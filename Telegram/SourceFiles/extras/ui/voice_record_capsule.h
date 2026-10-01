#pragma once

#include "base/timer.h"
#include "ui/rp_widget.h"
#include "media/audio/media_audio_capture_common.h"

namespace Ui {
class AbstractButton;
class PopupMenu;
}

namespace ExtrasUi {

struct VoiceRecordPreview {
	VoiceWaveform waveform;
	crl::time duration = 0;
	float64 progress = 0.;
	float64 trimLeft = 0.;
	float64 trimRight = 1.;
	bool playing = false;
};

struct VoiceRecordActions {
	Fn<void()> cancel;
	Fn<void()> pause;
	Fn<void()> confirm;
	Fn<void()> play;
	Fn<void()> beginSeek;
	Fn<void(float64)> seek;
	Fn<void(float64, float64)> trim;
	Fn<VoiceRecordPreview()> preview;
};

class VoiceRecordCapsule final : public Ui::RpWidget {
public:
	enum class State { Recording, Paused, Ready };
	VoiceRecordCapsule(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::RpWidget*> sendAnchor,
		VoiceRecordActions actions);
	~VoiceRecordCapsule();
	void setState(State state);
	void updateLevel(ushort level, crl::time duration);
	void setOnceAllowed(bool allowed);
	[[nodiscard]] bool once() const;
	bool takeOnce();

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

private:
	enum class Icon { Cancel, Pause, Microphone, Confirm, Play, Send };
	enum class Drag { None, Left, Right, Seek };
	void setupButton(Ui::AbstractButton *button, Icon icon, bool primary);
	void paintIcon(QPainter &p, Icon icon, QPointF center, QColor color);
	void updateButtons();
	void updateGeometry();
	void refreshPreview();
	void updateDrag(QPoint position);
	void showMenu(QPoint position);
	[[nodiscard]] QRectF waveformRect() const;
	[[nodiscard]] qreal progressAt(int x) const;

	const not_null<Ui::RpWidget*> _sendAnchor;
	const VoiceRecordActions _actions;
	const std::unique_ptr<Ui::AbstractButton> _cancel;
	const std::unique_ptr<Ui::AbstractButton> _pause;
	const std::unique_ptr<Ui::AbstractButton> _confirm;
	const std::unique_ptr<Ui::AbstractButton> _play;
	std::unique_ptr<Ui::PopupMenu> _menu;
	State _state = State::Recording;
	Drag _drag = Drag::None;
	VoiceRecordPreview _preview;
	QVector<float64> _levels;
	crl::time _duration = 0;
	bool _onceAllowed = false;
	bool _once = false;
	base::Timer _refresh;
};

} // namespace ExtrasUi
