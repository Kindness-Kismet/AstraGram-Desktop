#ifdef _DEBUG
#include "extras/debug/simulation_widget.h"

#include "extras/debug/commands/commands_internal.h"
#include "base/weak_ptr.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "tgw/tgw_backend.h"
#include "tgw/tgw_painter.h"
#include "tgw/tgw_render_thread.h"
#include "tgw/tgw_renderer_work.h"
#include "tgw/tgw_storage.h"
#include "twidget/sdk_policy.h"

#include <QtCore/QBuffer>
#include <zlib.h>

namespace ExtrasDebug::Commands {
namespace {

using Json = nlohmann::json;

void appendUInt32(QByteArray &bytes, uint32 value) {
	for (auto shift = 24; shift >= 0; shift -= 8) {
		bytes.append(char(uchar(value >> shift)));
	}
}

[[nodiscard]] QByteArray damagedPng(uchar depth, uchar type) {
	// 只有头部的微型 PNG 进入损坏图像路径，避免为异常位深分配大图。
	auto header = QByteArray();
	appendUInt32(header, 1);
	appendUInt32(header, 2);
	header.append(char(depth)).append(char(type)).append(3, char(0));
	auto result = QByteArray("\x89PNG\r\n\x1a\n", 8);
	appendUInt32(result, uint32(header.size()));
	const auto from = result.size();
	result.append("IHDR").append(header);
	appendUInt32(result, uint32(crc32(0,
		reinterpret_cast<const Bytef*>(result.constData() + from),
		uInt(result.size() - from))));
	return result;
}

[[nodiscard]] bool loadsImage(const QByteArray &png) {
	auto backend = Tgw::QtBackend({}, {}, {}, {}, {});
	auto width = float();
	auto height = float();
	return backend.load_image(1,
		"data:image/png;base64," + png.toBase64().toStdString(),
		width, height);
}

[[nodiscard]] int paintedPixels(const QImage &image, QRect area = {}) {
	if (area.isEmpty()) {
		area = image.rect();
	}
	area &= image.rect();
	auto count = 0;
	for (auto y = area.top(); y <= area.bottom(); ++y) {
		const auto row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		for (auto x = area.left(); x <= area.right(); ++x) {
			count += (qAlpha(row[x]) != 0);
		}
	}
	return count;
}

[[nodiscard]] Tgw::PaintedFrame groupFrame(float scale) {
	auto group = twlite::Command{ .kind = twlite::Cmd::GroupPush };
	group.matrix[0] = group.matrix[3] = scale;
	const auto commands = twlite::DisplayList{
		group,
		{ .kind = twlite::Cmd::FillRect,
			.rect = { 0, 0, .00001f, .00001f }, .color = { 255, 0, 0, 255 } },
		{ .kind = twlite::Cmd::GroupPop },
		{ .kind = twlite::Cmd::FillRect,
			.rect = { 48, 48, 8, 8 }, .color = { 0, 255, 0, 255 } },
	};
	auto backend = Tgw::QtBackend({}, {}, {}, {}, {});
	return Tgw::PaintFrame(commands, backend, QSize(64, 64), 1.);
}

[[nodiscard]] Tgw::PaintedFrame glyphFrame(
		float pixels,
		int count,
		QSize size,
		float step = 0.) {
	// 固定彩色字体，确保预算检查覆盖实际彩色字形栅格。
	auto backend = Tgw::QtBackend({}, {}, u"Segoe UI Emoji"_q, {}, {});
	auto metrics = twlite::FontMetrics();
	const auto font = backend.create_font(
		"Segoe UI Emoji", pixels, 400, false, {}, metrics);
	auto commands = twlite::DisplayList();
	for (auto i = 0; i != count; ++i) {
		// 单个区域指示符走 Qt 字体路径，不被 Telegram 的旗帜图像替换。
		commands.push_back({
			.kind = twlite::Cmd::Text,
			.color = { 255, 255, 255, 255 },
			.font = font,
			.text = "\xf0\x9f\x87\xa6",
			.x = i * step,
			.y = pixels + 8,
		});
	}
	return Tgw::PaintFrame(commands, backend, size, 1.);
}

class WidgetVerification final
	: public std::enable_shared_from_this<WidgetVerification> {
public:
	explicit WidgetVerification(not_null<HistoryItem*> item)
	: _session(base::make_weak(&item->history()->session()))
	, _message(item->fullId())
	, _original(Tgw::StoredAreas(item))
	, _host(Tgw::OpenStorageHost(item))
	, _job(beginJob("widget-verification")) {
	}

	[[nodiscard]] Result start() {
		Tgw::PostToRenderThread([self = shared_from_this()] {
			self->checkImages();
			self->checkPainting();
			crl::on_main([self] { self->checkStorage(); });
		});
		return jobStarted(_job);
	}

private:
	void checkStorage() {
		if (!_host.id) {
			record("storage-host", false);
			finish();
			return;
		}
		commit(twidget::WidgetStorage{ .local = _accepted, .session = _accepted });
		crl::on_main([self = shared_from_this()] { self->checkAccepted(); });
	}

	void record(const char *name, bool passed, Json details = Json::object()) {
		_passed &= passed;
		_checks.push_back({ { "name", name }, { "passed", passed },
			{ "details", std::move(details) } });
	}

	void checkImages() {
		auto image = QImage(QSize(2, 2), QImage::Format_ARGB32);
		image.fill(Qt::red);
		auto buffer = QBuffer();
		const auto saved = buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG");
		record("png-valid", saved && loadsImage(buffer.data()));
		record("png-damaged-valid-depth", loadsImage(damagedPng(8, 6)));
		for (const auto [depth, type] : { std::pair(255, 6), std::pair(1, 6),
				std::pair(16, 3), std::pair(3, 0) }) {
			record("png-invalid-depth", !loadsImage(damagedPng(uchar(depth), uchar(type))),
				{ { "depth", depth }, { "type", type } });
		}
	}

	void checkPainting() {
		const auto inside = groupFrame(float(1 << 20));
		const auto outside = groupFrame(float(1 << 21));
		record("group-stretch-boundary", !inside.image.isNull()
			&& qRed(inside.image.pixel(4, 4)) == 255);
		record("group-stretch-rejected", !outside.image.isNull()
			&& qAlpha(outside.image.pixel(4, 4)) == 0
			&& qGreen(outside.image.pixel(50, 50)) == 255);
		const auto normal = glyphFrame(32, 1, QSize(64, 64));
		record("color-glyph-normal", !normal.image.isNull()
			&& paintedPixels(normal.image) > 0);
		const auto large = glyphFrame(1536, 1, QSize(2048, 2048));
		record("color-glyph-over-budget", !large.image.isNull()
			&& paintedPixels(large.image) == 0);
		const auto shared = glyphFrame(800, 3, QSize(2600, 1000), 850);
		const auto first = paintedPixels(shared.image, QRect(0, 0, 850, 1000));
		const auto second = paintedPixels(shared.image, QRect(850, 0, 850, 1000));
		const auto third = paintedPixels(shared.image, QRect(1700, 0, 850, 1000));
		record("color-glyph-shared-budget", !shared.image.isNull()
			&& first > 0 && second > 0 && third == 0,
			{ { "firstPixels", first }, { "secondPixels", second }, { "thirdPixels", third } });
	}

	[[nodiscard]] HistoryItem *item() const {
		const auto session = _session.get();
		return session ? session->data().message(_message) : nullptr;
	}

	void commit(const twidget::WidgetStorage &areas) {
		_host.commit("local", areas.local);
		_host.commit("session", areas.session);
	}

	void checkAccepted() {
		// 提交经主线程队列应用，下一轮再核对实际存储内容。
		const auto current = item();
		if (!current) {
			record("storage-message-available", false);
			finish();
			return;
		}
		const auto areas = Tgw::StoredAreas(current);
		record("storage-4096-accepted", areas.local == _accepted && areas.session == _accepted);
		const auto oversized = twidget::StorageArea{ { "k", std::string(4096, 'x') } };
		commit(twidget::WidgetStorage{ .local = oversized, .session = oversized });
		crl::on_main([self = shared_from_this()] { self->checkRejected(); });
	}

	void checkRejected() {
		const auto current = item();
		if (!current) {
			record("storage-message-available", false);
			finish();
			return;
		}
		const auto areas = Tgw::StoredAreas(current);
		record("storage-4097-rejected", areas.local == _accepted && areas.session == _accepted);
		commit(_original);
		crl::on_main([self = shared_from_this()] { self->checkRestored(); });
	}

	void checkRestored() {
		const auto current = item();
		const auto areas = current ? Tgw::StoredAreas(current) : twidget::WidgetStorage();
		record("storage-restored", current && areas.local == _original.local
			&& areas.session == _original.session);
		finish();
	}

	void finish() {
		Tgw::StorageHostReleased(_host.id);
		finishJob(_job, _passed, Json{ { "passed", _passed }, { "checks", std::move(_checks) } });
	}

	const base::weak_ptr<Main::Session> _session;
	const FullMsgId _message;
	const twidget::WidgetStorage _original;
	const Tgw::StorageHost _host;
	const uint64 _job;
	const twidget::StorageArea _accepted = { { "k", std::string(4095, 'x') } };
	Json _checks = Json::array();
	bool _passed = true;
};

} // namespace

Result verifyWidget(not_null<HistoryItem*> item) {
	static auto running = std::weak_ptr<WidgetVerification>();
	if (!running.expired()) {
		return Result::Err(u"widget verification is already running"_q);
	}
	const auto check = std::make_shared<WidgetVerification>(item);
	running = check;
	return check->start();
}

} // namespace ExtrasDebug::Commands
#endif
