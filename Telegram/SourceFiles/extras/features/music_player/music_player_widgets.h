#pragma once

#include "base/timer.h"
#include "ui/abstract_button.h"
#include "ui/widgets/continuous_sliders.h"

namespace Extras::MusicPlayer {

[[nodiscard]] QColor accentColor();
[[nodiscard]] QColor softColor();

enum class PlayerIcon {
	None, Play, Pause, Cancel, Previous, Next, Expand, Collapse, Close,
	Download, Repeat, RepeatOne, Order, Reverse, Shuffle, Volume, Muted, Queue,
};

class PlayerButton final : public Ui::AbstractButton {
public:
	PlayerButton(QWidget *parent, PlayerIcon icon, const QString &name,
		const QString &label);
	void setIcon(PlayerIcon icon);
	void setText(QString text);
	void setActive(bool active);
	void setPrimary(bool primary);
	void setLabel(const QString &label);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	PlayerIcon _icon;
	QString _text;
	bool _active = false;
	bool _primary = false;
};

class Waveform final : public Ui::RpWidget {
public:
	explicit Waveform(QWidget *parent);
	void setPlaying(bool playing);

protected:
	void paintEvent(QPaintEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;

private:
	void refreshAnimation();
	base::Timer _timer;
	bool _playing = false;
};

class PlayerProgress final : public Ui::MediaSlider {
public:
	explicit PlayerProgress(QWidget *parent);
	void setMini(bool mini);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	bool _mini = false;
};

void paintCover(QPainter &p, const QRect &rect, const QImage &image,
	bool video = false, bool voice = false);

} // namespace Extras::MusicPlayer
