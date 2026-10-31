#pragma once

#include "base/timer.h"
#include "ui/abstract_button.h"
#include "ui/widgets/continuous_sliders.h"

#include <array>

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
	std::array<QImage, 4> _miniCorners;
};

class CoverPainter final {
public:
	void paint(QPainter &p, const QRect &rect, const QImage &image,
		bool video = false, bool voice = false, int radius = 10,
		double zoom = 1., bool dimmed = false, bool gradient = false);

private:
	QImage _cache;
	qint64 _imageKey = 0;
	QColor _background;
	int _radius = 0;
	double _zoom = 0.;
	bool _video = false;
	bool _voice = false;
	bool _dimmed = false;
	bool _gradient = false;
};

class CoverButton final : public Ui::AbstractButton {
public:
	explicit CoverButton(QWidget *parent);
	void setImage(QImage image, bool video, bool voice);
	void setPlayback(bool expanded, bool playing);
	void setLabels(QString mode, QString format);

protected:
	void paintEvent(QPaintEvent *event) override;
	void enterEventHook(QEnterEvent *event) override;
	void leaveEventHook(QEvent *event) override;

private:
	void animate();
	CoverPainter _painter;
	QImage _image;
	QString _mode;
	QString _format;
	Ui::Animations::Simple _animation;
	double _zoom = 1.;
	bool _video = false;
	bool _voice = false;
	bool _expanded = false;
	bool _playing = false;
};

} // namespace Extras::MusicPlayer
