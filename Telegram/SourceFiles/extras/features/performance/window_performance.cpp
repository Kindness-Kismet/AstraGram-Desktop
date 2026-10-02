#include "extras/features/performance/window_performance.h"

#include "base/timer.h"
#include "extras/extras_settings.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/platform/ui_platform_window_title.h"
#include "window/main_window.h"
#include "styles/style_widgets.h"

#include <QtGui/QPaintEvent>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <map>

namespace ExtrasPerformance {
namespace {

constexpr auto kNanosecondsPerMs = 1000000.;
constexpr auto kActiveGapMs = 250.;
std::vector<Monitor*> Monitors;
#ifdef _DEBUG
QPointer<Monitor> Capturing;
#endif

qint64 nowNs() {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

#ifdef _DEBUG
QString eventName(int type) {
	switch (type) {
	case QEvent::Paint: return u"paint"_q;
	case QEvent::UpdateRequest: return u"window-update"_q;
	case QEvent::Timer: return u"timer"_q;
	case QEvent::MetaCall: return u"queued-call"_q;
	case QEvent::MouseButtonRelease: return u"mouse-release"_q;
	default: return u"event-%1"_q.arg(type);
	}
}

QString receiverName(QObject *receiver) {
	if (!receiver) {
		return u"destroyed"_q;
	}
	const auto type = QString::fromLatin1(receiver->metaObject()->className());
	for (auto object = receiver; object; object = object->parent()) {
		if (!object->objectName().isEmpty()) {
			return object->objectName() + u"/"_q + type;
		}
	}
	return type;
}
#endif

} // namespace

struct Monitor::State {
	explicit State(not_null<Window::MainWindow*> window)
	: window(window)
	, refreshTimer([=] { refreshFps(); }) {
	}

	bool enabled() const {
		return fpsEnabled
#ifdef _DEBUG
			|| capture
#endif
			;
	}

	void refreshFps() {
		const auto active = !timestamps.empty()
			&& ((nowNs() - timestamps.back()) / kNanosecondsPerMs < 500.);
		const auto value = (active && timestamps.size() > 1)
			? int(std::round((timestamps.size() - 1) * 1.e9
				/ (timestamps.back() - timestamps.front())))
			: 0;
		if (fps != value) {
			fps = value;
			if (label) {
				label->update();
			}
		}
		if (active && fpsEnabled && label && label->isVisible()) {
			refreshTimer.callOnce(250);
		}
	}

	void recordFrame(qint64 started, double duration) {
		if (!timestamps.empty()
			&& (started - timestamps.back()) / kNanosecondsPerMs > kActiveGapMs) {
			timestamps.clear();
		}
		timestamps.push_back(started);
		while (timestamps.size() > 256
			|| (started - timestamps.front()) / kNanosecondsPerMs > 1000.) {
			timestamps.pop_front();
		}
		if (fpsEnabled && label && label->isVisible() && !refreshTimer.isActive()) {
			refreshTimer.callOnce(250);
		}
#ifdef _DEBUG
		if (capture) {
			const auto interval = lastFrame
				? (started - lastFrame) / kNanosecondsPerMs
				: 0.;
			lastFrame = started;
			frames.push_back({
				(started - captureStarted) / kNanosecondsPerMs,
				interval,
				duration,
				paintCount,
			});
			if (frames.size() > 4096) {
				frames.pop_front();
			}
		}
#endif
	}

#ifdef _DEBUG
	struct Frame {
		double time = 0.;
		double interval = 0.;
		double duration = 0.;
		int paints = 0;
	};
	struct Timing {
		int count = 0;
		double total = 0.;
		double maximum = 0.;
	};
	void recordTiming(const QString &name, double duration) {
		if (timings.size() >= 256 && !timings.contains(name)) {
			return;
		}
		auto &timing = timings[name];
		++timing.count;
		timing.total += duration;
		timing.maximum = std::max(timing.maximum, duration);
	}
	bool capture = false;
	qint64 captureStarted = 0;
	qint64 captureStopped = 0;
	qint64 lastFrame = 0;
	std::deque<Frame> frames;
	std::map<QString, Timing> timings;
#endif

	not_null<Window::MainWindow*> window;
	QPointer<Ui::RpWidget> label;
	base::Timer refreshTimer;
	rpl::lifetime lifetime;
	std::deque<qint64> timestamps;
	bool fpsEnabled = true;
	bool inFrame = false;
	bool painted = false;
	int paintCount = 0;
	int fps = 0;
};

Monitor::Monitor(not_null<Window::MainWindow*> window)
: QObject(window)
, _state(std::make_unique<State>(window)) {
	Monitors.push_back(this);
	ExtrasSettings::getInstance().showFpsValue(
	) | rpl::on_next([=](bool enabled) {
		_state->fpsEnabled = enabled;
		_state->refreshTimer.cancel();
		_state->timestamps.clear();
		_state->fps = 0;
		if (_state->label) {
			_state->label->setVisible(enabled);
			_state->label->update();
		}
	}, _state->lifetime);
	window->shownValue() | rpl::on_next([=](bool shown) {
		if (!shown) {
			_state->refreshTimer.cancel();
			_state->timestamps.clear();
			_state->fps = 0;
		}
	}, _state->lifetime);
}

Monitor::~Monitor() {
#ifdef _DEBUG
	stopCapture();
#endif
	Monitors.erase(std::remove(Monitors.begin(), Monitors.end(), this), Monitors.end());
	delete _state->label.data();
}

Monitor *findMonitor(QWidget *window) {
	for (const auto monitor : Monitors) {
		if (monitor->parent() == window) {
			return monitor;
		}
	}
	return nullptr;
}

void Monitor::refreshTitleLabel() {
	const auto title = _state->window->titleWidget();
	if (_state->label && _state->label->parentWidget() == title) {
		return;
	}
	delete _state->label.data();
	if (!title) {
		return;
	}
	const auto label = Ui::CreateChild<Ui::RpWidget>(title);
	_state->label = label;
	label->setObjectName(u"title.fps"_q);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	label->setVisible(_state->fpsEnabled);
	label->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(label);
		p.setFont(st::normalFont->f);
		p.setPen(st::windowSubTextFg->c);
		p.drawText(label->rect(), Qt::AlignCenter, u"%1 FPS"_q.arg(_state->fps));
	}, label->lifetime());

	const auto position = [=] {
		auto controls = QRect();
		for (const auto widget : title->findChildren<QWidget*>()) {
			if (!widget->isHidden() && dynamic_cast<Ui::AbstractButton*>(widget)) {
				controls = controls.united(QRect(widget->mapTo(title, QPoint()), widget->size()));
			}
		}
		const auto width = st::normalFont->width(u"9999 FPS"_q) + 16 * st::lineWidth;
		const auto height = controls.isEmpty() ? title->height() : controls.height();
		const auto left = controls.isEmpty()
			? title->width() - width
			: controls.center().x() > title->width() / 2
			? controls.left() - width
			: controls.right() + 1;
		label->setGeometry(left, controls.y(), width, height);
		label->raise();
	};
	const auto layout = Ui::Platform::TitleControlsLayout::Instance();
	rpl::combine(title->sizeValue(), layout->value()
	) | rpl::on_next([=] {
		crl::on_main(label, position);
	}, label->lifetime());
	label->lifetime().add([layout] {});
	position();
}

EventSample::EventSample(QObject *receiver, QEvent *event) {
	const auto type = event->type();
	if (type == QEvent::UpdateRequest || type == QEvent::Paint) {
		const auto widget = qobject_cast<QWidget*>(receiver);
		const auto monitor = widget ? findMonitor(widget->window()) : nullptr;
		if (monitor && monitor->_state->enabled()) {
			_monitor = monitor;
			auto &state = *monitor->_state;
			if (type == QEvent::UpdateRequest
				&& widget == state.window && !state.inFrame) {
				_frame = true;
				_started = nowNs();
				state.inFrame = true;
				state.painted = false;
				state.paintCount = 0;
			} else if (type == QEvent::Paint && state.inFrame) {
				auto region = static_cast<QPaintEvent*>(event)->region();
				region.translate(widget->mapTo(state.window, QPoint()));
				if (const auto label = state.label.data()) {
					region -= QRect(label->mapTo(state.window, QPoint()), label->size());
				}
				if (!region.isEmpty()) {
					state.painted = true;
					++state.paintCount;
				}
			}
		}
	}
#ifdef _DEBUG
	if (Capturing) {
		_capture = true;
		_receiver = receiver;
		_type = type;
		if (!_monitor) {
			_monitor = Capturing;
		}
		if (!_started) {
			_started = nowNs();
		}
	}
#endif
}

EventSample::~EventSample() {
	if (!_monitor) {
		return;
	}
	auto &state = *_monitor->_state;
	const auto duration = _started ? (nowNs() - _started) / kNanosecondsPerMs : 0.;
	if (_frame) {
		state.inFrame = false;
		if (state.painted) {
			state.recordFrame(_started, duration);
		}
	}
#ifdef _DEBUG
	if (_capture && state.capture && (_type == QEvent::Paint || duration >= 4.)) {
		state.recordTiming(eventName(_type) + u":"_q + receiverName(_receiver), duration);
	}
#endif
}

#ifdef _DEBUG
void Monitor::startCapture() {
	if (Capturing && Capturing != this) {
		Capturing->stopCapture();
	}
	Capturing = this;
	_state->capture = true;
	_state->captureStarted = nowNs();
	_state->captureStopped = 0;
	_state->lastFrame = 0;
	_state->frames.clear();
	_state->timings.clear();
}

void Monitor::stopCapture() {
	if (_state->capture) {
		_state->capture = false;
		_state->captureStopped = nowNs();
	}
	if (Capturing == this) {
		Capturing = nullptr;
	}
}

nlohmann::json Monitor::snapshot() const {
	using json = nlohmann::json;
	auto frames = json::array();
	auto intervals = std::vector<double>();
	auto durations = std::vector<double>();
	for (const auto &frame : _state->frames) {
		frames.push_back({
			{ "timeMs", frame.time },
			{ "intervalMs", frame.interval },
			{ "updateMs", frame.duration },
			{ "paintEvents", frame.paints },
		});
		if (frame.interval > 0. && frame.interval <= kActiveGapMs) {
			intervals.push_back(frame.interval);
		}
		durations.push_back(frame.duration);
	}
	const auto distribution = [](std::vector<double> values) {
		std::sort(values.begin(), values.end());
		const auto percentile = [&](double rank) {
			return values.empty() ? 0. : values[std::min(
				size_t(std::ceil(values.size() * rank)) - 1,
				values.size() - 1)];
		};
		return json{
			{ "count", values.size() },
			{ "p50", percentile(0.5) },
			{ "p95", percentile(0.95) },
			{ "max", values.empty() ? 0. : values.back() },
			{ "over16ms", std::count_if(values.begin(), values.end(), [](double v) { return v > 16.667; }) },
			{ "over33ms", std::count_if(values.begin(), values.end(), [](double v) { return v > 33.333; }) },
		};
	};
	auto timings = json::array();
	for (const auto &[name, timing] : _state->timings) {
		timings.push_back({
			{ "name", name.toStdString() },
			{ "count", timing.count },
			{ "totalMs", timing.total },
			{ "maxMs", timing.maximum },
		});
	}
	const auto handle = _state->window->windowHandle();
	const auto screen = handle ? handle->screen() : nullptr;
	return {
		{ "capturing", _state->capture },
		{ "fpsEnabled", _state->fpsEnabled },
		{ "fps", _state->fps },
		{ "displayRefreshHz", screen ? screen->refreshRate() : 0. },
		{ "durationMs", _state->captureStarted
			? ((_state->captureStopped ? _state->captureStopped : nowNs()) - _state->captureStarted) / kNanosecondsPerMs : 0. },
		{ "activeGapLimitMs", kActiveGapMs },
		{ "frameCount", frames.size() },
		{ "frameIntervalMs", distribution(std::move(intervals)) },
		{ "windowUpdateMs", distribution(std::move(durations)) },
		{ "timings", std::move(timings) },
		{ "frames", std::move(frames) },
	};
}

TaskSample::TaskSample(QWidget *window, QString name)
: _monitor(findMonitor(window))
, _name(std::move(name)) {
	if (_monitor && _monitor->_state->capture) {
		_started = nowNs();
	}
}

TaskSample::~TaskSample() {
	if (_monitor && _started && _monitor->_state->capture) {
		_monitor->_state->recordTiming(_name, (nowNs() - _started) / kNanosecondsPerMs);
	}
}
#endif

} // namespace ExtrasPerformance
