#ifdef _DEBUG
#include "extras/debug/simulation_widget.h"

#include "extras/debug/commands/commands_internal.h"
#include "extras/debug/debug_login.h"
#include "extras/debug/simulation_scenarios.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_list_widget.h"
#include "history/view/media/history_view_html_widget.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "settings.h"
#include "tgw/tgw_media.h"
#include "tgw/tgw_renderer.h"
#include "tgw/tgw_runtime.h"
#include "window/window_session_controller.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>

namespace ExtrasDebug::Commands {
namespace {

using Json = nlohmann::json;

base::weak_ptr<Main::Session> WidgetSession;
FullMsgId WidgetMessage;

[[nodiscard]] QByteArray widgetHtml() {
	return uR"widget(<!doctype html>
<html><head><meta charset="utf-8"><style>
* { box-sizing: border-box; }
body { margin: 0; padding: 0; font: 14px sans-serif; color: #183b38; background: #f2f8f5; }
.card { padding: 22px; }
.tag { display: inline-block; padding: 5px 9px; border-radius: 12px; background: #dceee5; color: #39785f; font-size: 11px; }
h1 { margin: 13px 0 7px; font-size: 22px; font-weight: 700; }
.intro { margin: 0 0 20px; color: #6b8179; font-size: 13px; }
.reading { padding: 17px; border-radius: 16px; background: #fff; border: 1px solid #deebe4; }
.number { font-size: 48px; font-weight: 700; line-height: 1.1; }
.unit { color: #80928a; font-size: 15px; }
.summary { margin-top: 9px; color: #5c756a; font-size: 13px; }
.track { margin-top: 17px; height: 9px; overflow: hidden; border-radius: 8px; background: #e4eee8; }
#fill { width: 37.5%; height: 9px; border-radius: 8px; background: #4b9a75; }
.goal { margin-top: 9px; color: #80928a; font-size: 12px; }
.actions { display: flex; gap: 10px; margin-top: 16px; }
button { border: 0; border-radius: 12px; padding: 13px 16px; cursor: pointer; font: 14px sans-serif; font-weight: 600; }
#add { flex: 1; color: #fff; background: #327956; }
#undo { color: #42745b; background: #e1eee6; }
button:active { opacity: .75; }
#tips-toggle { display: block; width: 100%; margin-top: 8px; padding: 10px; font-size: 12px; font-weight: 400; color: #5f7d6d; background: transparent; }
#tips { display: none; margin-top: 4px; padding: 12px 14px; border-radius: 10px; color: #657b6e; background: #e5f0e8; font-size: 12px; line-height: 1.7; }
</style></head><body><div class="card">
<span class="tag">生活小习惯</span>
<h1>今天，喝够水了吗？</h1>
<p class="intro">忙碌也别忘了，给自己留一点关照。</p>
<div class="reading">
<span id="count" class="number">3</span><span class="unit"> / 8 杯</span>
<div id="summary" class="summary">已喝 750 ml · 还差 5 杯</div>
<div class="track"><div id="fill"></div></div>
<div class="goal">每日目标 2,000 ml · 每杯按 250 ml 计算</div>
</div>
<div class="actions"><button id="add">再喝一杯 +</button><button id="undo">撤回</button></div>
<button id="tips-toggle">展开小贴士 ↓</button>
<div id="tips">少量多次，按自己的需要补充水分。<br>喝完这一杯，也记得起身活动一下。</div>
</div><script>
var stored = localStorage.getItem('water-cups');
var count = stored === null ? 3 : Number(stored);
var expanded = localStorage.getItem('water-tips') === 'true';
function render() {
  document.getElementById('count').textContent = String(count);
  document.getElementById('summary').textContent = count === 8
    ? '已喝 2,000 ml · 今日目标完成！'
    : '已喝 ' + String(count * 250) + ' ml · 还差 ' + String(8 - count) + ' 杯';
  document.getElementById('fill').style.width = String(count * 12.5) + '%';
  document.getElementById('add').textContent = count === 8 ? '目标完成 ✓' : '再喝一杯 +';
  document.getElementById('tips').style.display = expanded ? 'block' : 'none';
  document.getElementById('tips-toggle').textContent = expanded ? '收起小贴士 ↑' : '展开小贴士 ↓';
  localStorage.setItem('water-cups', String(count));
  localStorage.setItem('water-tips', String(expanded));
}
document.getElementById('add').addEventListener('click', function () {
  if (count < 8) { count += 1; render(); }
});
document.getElementById('undo').addEventListener('click', function () {
  if (count > 0) { count -= 1; render(); }
});
document.getElementById('tips-toggle').addEventListener('click', function () {
  expanded = !expanded;
  render();
});
render();
</script></body></html>)widget"_q.toUtf8();
}

[[nodiscard]] HistoryItem *sampleItem(not_null<Main::Session*> session) {
	return (WidgetSession.get() == session.get())
		? session->data().message(WidgetMessage)
		: nullptr;
}

[[nodiscard]] Tgw::Runtime *sampleRuntime(not_null<HistoryItem*> item) {
	auto view = static_cast<HistoryView::Element*>(nullptr);
	const auto controller = item->history()->session().tryResolveWindow();
	if (!controller) {
		return nullptr;
	}
	// 新列表独立持有消息视图，mainView 只属于旧列表的消息块。
	for (const auto widget : controller->content()->findChildren<QWidget*>()) {
		const auto list = dynamic_cast<HistoryView::ListWidget*>(widget);
		if (!list || !list->isVisible()) {
			continue;
		}
		const auto &items = list->debugItems();
		const auto found = ranges::find_if(items, [&](const auto candidate) {
			return candidate->data() == item;
		});
		view = (found != end(items)) ? found->get() : nullptr;
		if (view) {
			break;
		}
	}
	if (!view) {
		const auto legacy = item->mainView();
		if (legacy && item->history()->owner().queryItemVisibility(item, legacy)) {
			view = legacy;
		}
	}
	const auto media = view
		? dynamic_cast<HistoryView::HtmlWidget*>(view->media())
		: nullptr;
	const auto layout = media ? media->heightLayout() : std::nullopt;
	return layout ? layout->runtime.get() : nullptr;
}

[[nodiscard]] const char *stateName(Tgw::Runtime::State state) {
	switch (state) {
	case Tgw::Runtime::State::Idle: return "idle";
	case Tgw::Runtime::State::Waiting: return "waiting";
	case Tgw::Runtime::State::Constructing: return "constructing";
	case Tgw::Runtime::State::Live: return "live";
	case Tgw::Runtime::State::Released: return "released";
	case Tgw::Runtime::State::Stopped: return "stopped";
	case Tgw::Runtime::State::Failed: return "failed";
	}
	Unexpected("Invalid widget runtime state.");
}

[[nodiscard]] Json sampleState(not_null<HistoryItem*> item) {
	const auto runtime = sampleRuntime(item);
	const auto core = runtime ? runtime->core() : nullptr;
	const auto height = runtime ? runtime->height() : std::nullopt;
	return Json{
		{ "peerId", item->history()->peer->id.value },
		{ "messageId", item->id.bare },
		{ "local", item->isLocal() },
		{ "widgetMedia", dynamic_cast<Tgw::WidgetMedia*>(item->media()) != nullptr },
		{ "runtime", runtime ? stateName(runtime->state()) : "no-view" },
		{ "height", height ? Json(*height) : Json(nullptr) },
		{ "loaded", core && core->loaded() },
		{ "hasContent", core && core->hasContent() },
		{ "interactive", runtime && runtime->interactive()
			&& runtime->interactive()->acceptsInput() },
		{ "stopReason", core ? core->stopReason().toStdString() : std::string() },
	};
}

[[nodiscard]] Result showSample(not_null<Main::Session*> session, bool reset) {
	const auto peer = simulationPeer(session, u"inline-bot"_q);
	if (!peer || !session->tryResolveWindow()) {
		return Result::Err(u"simulation peer or window not found"_q);
	}
	auto item = sampleItem(session);
	if (item && reset) {
		item->destroy();
		item = nullptr;
	}
	if (!item) {
		const auto html = widgetHtml();
		// qCompress 的长度头不属于协议的 zlib 数据。
		const auto payload = qCompress(html, 9).mid(4);
		if (Tgw::UnpackPayload(payload) != html) {
			return Result::Err(u"could not prepare the widget payload"_q);
		}
		const auto history = session->data().history(peer);
		item = history->addNewLocalMessage({
			.id = session->data().nextLocalMessageId(),
			.flags = MessageFlag::Local | MessageFlag::Outgoing | MessageFlag::HasFromId,
			.from = session->userPeerId(),
			.date = base::unixtime::now(),
		}, TextWithEntities{}, MTP_messageMediaWidget(
			MTP_flags(0), MTP_int(390), MTP_int(0), MTP_bytes(payload),
			MTPstring(), MTPlong(), MTPstring(), MTPlong())).get();
		WidgetSession = base::make_weak(session.get());
		WidgetMessage = item->fullId();
	}
	if (const auto opened = openSimulationScene({ u"inline-bot"_q }); !opened.ok) {
		return opened;
	}
	return Result::Ok(Compact(sampleState(item)));
}

} // namespace

Result simulateWidget(const QStringList &args) {
	const auto mode = args.empty() ? u"show"_q : args.front();
	if (args.size() > 1 || (mode != u"show"_q && mode != u"state"_q
		&& mode != u"log"_q && mode != u"reset"_q)) {
		return Result::Err(u"usage: simulation.widget [show|state|log|reset]"_q);
	}
	const auto session = ActiveSession();
	if (!cDebugProfile() || !session || !isSimulationSession(session)) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	if (mode == u"show"_q || mode == u"reset"_q) {
		return showSample(session, mode == u"reset"_q);
	}
	const auto item = sampleItem(session);
	if (!item) {
		return Result::Err(u"open the sample with simulation.widget show first"_q);
	}
	if (mode == u"state"_q) {
		return Result::Ok(Compact(sampleState(item)));
	}
	const auto runtime = sampleRuntime(item);
	if (!runtime) {
		return Result::Err(u"the widget view is not available"_q);
	}
	if (runtime->state() != Tgw::Runtime::State::Live) {
		return Result::Ok(Compact(Json{ { "log", runtime->logText().toStdString() } }));
	}
	const auto job = beginJob("widget-log");
	session->lifetime().add([job] { finishJob(job, false, "session closed"); });
	runtime->requestLog([job](const QString &text) {
		finishJob(job, true, Json{ { "log", text.toStdString() } });
	});
	// 视图切换或退出会取消诊断回调，任务不能一直停留在运行中。
	QTimer::singleShot(10000, QCoreApplication::instance(), [job] {
		finishJob(job, false, "widget diagnostics did not finish before the deadline");
	});
	return jobStarted(job);
}

} // namespace ExtrasDebug::Commands
#endif
