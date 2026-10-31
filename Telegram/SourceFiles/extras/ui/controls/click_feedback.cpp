#include "extras/ui/controls/click_feedback.h"

#include "settings/settings_common.h"
#include "settings/settings_workspace.h"
#include "settings/settings_workspace_search.h"
#include "ui/effects/animations.h"
#include "ui/layers/box_layer_widget.h"
#include "ui/widgets/buttons.h"
#include "window/window_main_menu.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QVariant>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtWidgets/QApplication>
#include <QtWidgets/QGraphicsEffect>

namespace ExtrasUi {
namespace {

enum class Feedback { None, Spring, Luminance };

Feedback feedbackFor(not_null<Ui::AbstractButton*> button) {
	const auto round = dynamic_cast<Ui::RoundButton*>(button.get());
	const auto flat = dynamic_cast<Ui::FlatButton*>(button.get());
	for (auto parent = button->parentWidget(); parent; parent = parent->parentWidget()) {
		if (dynamic_cast<Ui::BoxLayerWidget*>(parent)) {
			return (round || flat) ? Feedback::Spring : Feedback::None;
		}
		if (dynamic_cast<Window::MainMenu*>(parent)
			|| dynamic_cast<Settings::AbstractSection*>(parent)
			|| dynamic_cast<Settings::Workspace*>(parent)
			|| dynamic_cast<Settings::WorkspaceSearch*>(parent)) {
			return Feedback::Luminance;
		}
	}
	return Feedback::None;
}

double springCurve(double delta, double progress) {
	const auto sample = [](double t, double first, double second) {
		const auto remaining = 1. - t;
		return 3. * remaining * remaining * t * first
			+ 3. * remaining * t * t * second + t * t * t;
	};
	auto left = 0.;
	auto right = 1.;
	for (auto i = 0; i != 16; ++i) {
		const auto middle = (left + right) / 2.;
		if (sample(middle, 0.34, 0.64) < progress) { left = middle; }
		else { right = middle; }
	}
	return delta * sample((left + right) / 2., 1.56, 1.);
}

class ClickEffect final : public QGraphicsEffect {
public:
	ClickEffect(not_null<Ui::AbstractButton*> button, Feedback feedback)
	: QGraphicsEffect(button.get()), _button(button), _feedback(feedback) {
		button->installEventFilter(this);
	}

	void setFeedback(Feedback feedback) {
		if (_feedback == feedback) { return; }
		_feedback = feedback;
		reset();
		updateBoundingRect();
	}

#ifdef _DEBUG
	ClickFeedbackState state() const {
		const auto level = _animation.value(_level);
		return {
			.spring = _feedback == Feedback::Spring,
			.scale = _feedback == Feedback::Spring ? 1. - 0.06 * level : 1.,
			.brightness = _feedback == Feedback::Luminance ? 1. - 0.18 * level : 1.,
			.contrast = _feedback == Feedback::Luminance ? 1. + 0.12 * std::max(0., level) : 1.,
		};
	}
#endif

protected:
	bool eventFilter(QObject *object, QEvent *event) override {
		if (object != _button.get()) { return false; }
		switch (event->type()) {
		case QEvent::Enter:
			_over = true;
			animate();
			break;
		case QEvent::Leave:
			_over = false;
			animate();
			break;
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease: {
			const auto mouse = static_cast<QMouseEvent*>(event);
			if (mouse->button() != Qt::LeftButton) { break; }
			_over = _button->rect().contains(mouse->position().toPoint());
			_down = event->type() == QEvent::MouseButtonPress && _over;
			animate();
		} break;
		case QEvent::MouseMove:
			_over = _button->rect().contains(static_cast<QMouseEvent*>(event)->position().toPoint());
			animate();
			break;
		case QEvent::KeyPress:
		case QEvent::KeyRelease: {
			const auto key = static_cast<QKeyEvent*>(event);
			if (key->isAutoRepeat() || (key->key() != Qt::Key_Space
				&& key->key() != Qt::Key_Return && key->key() != Qt::Key_Enter)) { break; }
			_down = event->type() == QEvent::KeyPress;
			_keyboard = _down;
			animate();
		} break;
		case QEvent::Hide:
		case QEvent::WindowDeactivate:
		case QEvent::FocusOut:
			reset();
			break;
		default: break;
		}
		return false;
	}

	QRectF boundingRectFor(const QRectF &rect) const override {
		return _feedback == Feedback::Spring
			? rect.adjusted(-2., -2., 2., 2.) : rect;
	}

	void draw(QPainter *p) override {
		// 先清理原按钮的波纹，再取得本体缓存；业务事件仍走原控件。
		if (const auto ripple = dynamic_cast<Ui::RippleButton*>(_button.get())) {
			ripple->finishAnimating();
		}
		if (_button->isDisabled() || !_button->isEnabled()) { reset(); }
		const auto level = _animation.value(_level);
		auto offset = QPoint();
		// 缓存合成保持嵌套控件的坐标一致，静止状态也走同一绘制路径。
		const auto source = sourcePixmap(Qt::LogicalCoordinates, &offset, NoPad);
		if (level == 0.) {
			p->drawPixmap(offset, source);
			return;
		}
		if (_feedback == Feedback::Spring) {
			const auto center = QRectF(_button->rectNoMargins()).center();
			p->save();
			p->setRenderHint(QPainter::SmoothPixmapTransform);
			p->translate(center);
			p->scale(1. - 0.06 * level, 1. - 0.06 * level);
			p->translate(-center);
			p->drawPixmap(offset, source);
			p->restore();
			return;
		}
		if (_cacheKey != source.cacheKey() || _cacheLevel != level) {
			_cacheKey = source.cacheKey();
			_cacheLevel = level;
			_cache = source.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
			const auto brightness = 1. - 0.18 * level;
			const auto contrast = 1. + 0.12 * std::max(0., level);
			for (auto y = 0; y != _cache.height(); ++y) {
				auto pixels = reinterpret_cast<QRgb*>(_cache.scanLine(y));
				for (auto x = 0; x != _cache.width(); ++x) {
					const auto pixel = pixels[x];
					const auto alpha = qAlpha(pixel);
					const auto channel = [&](int value) {
						return std::clamp(qRound((value * brightness - alpha / 2.)
							* contrast + alpha / 2.), 0, alpha);
					};
					pixels[x] = qRgba(channel(qRed(pixel)), channel(qGreen(pixel)),
						channel(qBlue(pixel)), alpha);
				}
			}
		}
		p->drawImage(offset, _cache);
	}

private:
	void reset() {
		const auto changed = _level != 0. || _animation.animating();
		_animation.stop();
		_level = 0.;
		_down = _over = _keyboard = false;
		if (changed) { update(); }
	}

	void animate() {
		if (_button->isDisabled() || !_button->isEnabled()) { return; }
		const auto down = _down && (_over || _keyboard);
		const auto to = down ? 1. : _feedback == Feedback::Luminance && _over ? -0.08 / 0.18 : 0.;
		if (to == _level) { return; }
		const auto from = _animation.value(_level);
		_level = to;
		_animation.start([=] { update(); }, from, to,
			anim::Disabled() ? 0 : _feedback == Feedback::Spring ? 180 : down ? 30 : 72,
			_feedback == Feedback::Spring ? anim::transition(springCurve) : anim::easeOutCubic);
	}

	const not_null<Ui::AbstractButton*> _button;
	Feedback _feedback;
	Ui::Animations::Simple _animation;
	double _level = 0.;
	bool _down = false;
	bool _over = false;
	bool _keyboard = false;
	qint64 _cacheKey = 0;
	double _cacheLevel = 0.;
	QImage _cache;
};

class FeedbackController final : public QObject {
public:
	FeedbackController() : QObject(QCoreApplication::instance()) {
		QCoreApplication::instance()->installEventFilter(this);
		QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
			this, [=] { delete this; });
	}

protected:
	bool eventFilter(QObject *object, QEvent *event) override {
		if (event->type() != QEvent::Polish && event->type() != QEvent::Show
			&& event->type() != QEvent::ParentChange) { return false; }
		const auto button = dynamic_cast<Ui::AbstractButton*>(object);
		if (!button) { return false; }
		const auto feedback = feedbackFor(button);
		const auto effect = dynamic_cast<ClickEffect*>(button->graphicsEffect());
		if (feedback == Feedback::None) {
			if (effect) { button->setGraphicsEffect(nullptr); }
		} else if (effect) {
			effect->setFeedback(feedback);
		} else if (!button->graphicsEffect()) {
			button->setGraphicsEffect(new ClickEffect(button, feedback));
		}
		return false;
	}
};

} // namespace

void initClickFeedback() { new FeedbackController(); }

#ifdef _DEBUG
std::optional<ClickFeedbackState> clickFeedbackState(QWidget *widget) {
	const auto effect = dynamic_cast<ClickEffect*>(widget->graphicsEffect());
	return effect ? std::make_optional(effect->state()) : std::nullopt;
}
#endif

} // namespace ExtrasUi
