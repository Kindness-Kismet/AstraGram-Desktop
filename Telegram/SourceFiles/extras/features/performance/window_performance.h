#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>

#ifdef _DEBUG
#include "extras/libs/json.hpp"
#endif

class QWidget;

namespace Window {
class MainWindow;
} // namespace Window

namespace ExtrasPerformance {

class Monitor final : public QObject {
public:
	explicit Monitor(not_null<Window::MainWindow*> window);
	~Monitor();

	void refreshTitleLabel();

#ifdef _DEBUG
	[[nodiscard]] bool startCapture();
	void stopCapture();
	[[nodiscard]] nlohmann::json snapshot() const;
#endif

private:
	friend class EventSample;
#ifdef _DEBUG
	friend class TaskSample;
#endif
	struct State;
	std::unique_ptr<State> _state;
};

[[nodiscard]] Monitor *findMonitor(QWidget *window);

// 仅统计窗口实际处理的绘制，标签自身刷新不计入帧率。
class EventSample final {
public:
	EventSample(QObject *receiver, QEvent *event);
	~EventSample();
	EventSample(const EventSample &) = delete;
	EventSample &operator=(const EventSample &) = delete;

private:
	QPointer<Monitor> _monitor;
	qint64 _started = 0;
	bool _frame = false;
#ifdef _DEBUG
	QPointer<QObject> _receiver;
	int _type = 0;
	bool _capture = false;
#endif
};

#ifdef _DEBUG
[[nodiscard]] Monitor *capturingMonitor();

class TaskSample final {
public:
	TaskSample(QWidget *window, QString name);
	~TaskSample();

private:
	QPointer<Monitor> _monitor;
	QString _name;
	qint64 _started = 0;
};
#endif

} // namespace ExtrasPerformance
