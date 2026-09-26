#include "ayu/ui/components/chat_frosted_background.h"

#include "ui/image/image_prepare.h"
#include "ui/painter.h"

#include <QtWidgets/QWidget>
#include <algorithm>
#include <utility>

namespace AyuUi {
namespace {

constexpr auto kDownsample = 8;
constexpr auto kBlurRadius = 3;
constexpr auto kBlurPadding = kDownsample * kBlurRadius;
constexpr auto kFrameInterval = 16;
constexpr auto kTintOpacity = 0.76;

} // namespace

ChatFrostedBackground::ChatFrostedBackground(
		QWidget *root,
		Capture capture,
		Fn<void()> changed)
: QObject(root)
, _root(root)
, _capture(std::move(capture))
, _changed(std::move(changed)) {
	_timer.setSingleShot(true);
	connect(&_timer, &QTimer::timeout, this, [=] { refresh(); });
}

void ChatFrostedBackground::setAreas(std::vector<QRect> areas) {
	auto expanded = std::vector<QRect>();
	for (const auto area : areas) {
		if (area.isEmpty()) {
			continue;
		}
		const auto padded = area.marginsAdded({
			kBlurPadding, kBlurPadding, kBlurPadding, kBlurPadding
		}).intersected(_root->rect());
		if (!padded.isEmpty()
			&& std::find(expanded.begin(), expanded.end(), padded) == expanded.end()) {
			expanded.push_back(padded);
		}
	}
	if (expanded.size() == _tiles.size()
		&& std::equal(expanded.begin(), expanded.end(), _tiles.begin(),
			[](QRect area, const Tile &tile) { return area == tile.area; })) {
		return;
	}
	auto tiles = std::vector<Tile>();
	for (const auto area : expanded) {
		const auto i = std::find_if(_tiles.begin(), _tiles.end(),
			[=](const Tile &tile) { return tile.area == area; });
		tiles.push_back(i != _tiles.end() ? std::move(*i) : Tile{ area });
	}
	_tiles = std::move(tiles);
	invalidate();
}

void ChatFrostedBackground::invalidate(QRect area) {
	auto dirty = false;
	for (auto &tile : _tiles) {
		tile.dirty |= area.isEmpty() || tile.area.intersects(area);
		dirty |= tile.dirty;
	}
	if (dirty && !_timer.isActive() && _root->isVisible()) {
		_timer.start(kFrameInterval);
	}
}

void ChatFrostedBackground::clear() {
	_timer.stop();
	for (auto &tile : _tiles) {
		tile.sample = QImage();
		tile.blurred = QImage();
	}
	_changed();
	invalidate();
}

void ChatFrostedBackground::refresh() {
	if (!_root->isVisible()) {
		return;
	}
	auto changed = false;
	for (auto &tile : _tiles) {
		if (!std::exchange(tile.dirty, false)) {
			continue;
		}
		const auto size = QSize(
			(tile.area.width() + kDownsample - 1) / kDownsample,
			(tile.area.height() + kDownsample - 1) / kDownsample);
		auto sample = QImage(size, QImage::Format_RGB32);
		sample.fill(Qt::black);
		{
			auto p = Painter(&sample);
			p.scale(double(size.width()) / tile.area.width(),
				double(size.height()) / tile.area.height());
			p.translate(-tile.area.topLeft());
			if (!_capture(p, tile.area)) {
				changed |= !tile.blurred.isNull();
				tile.sample = QImage();
				tile.blurred = QImage();
				continue;
			}
		}
		// 表面重绘可能连带重绘下层；相同采样不再刷新，避免形成绘制循环。
		if (sample == tile.sample) {
			continue;
		}
		tile.sample = sample;
		tile.blurred = Images::BlurLargeImage(std::move(sample), kBlurRadius);
		changed = true;
	}
	if (changed) {
		_changed();
	}
}

void ChatFrostedBackground::paint(QPainter &p, QRect area, QColor tint) const {
	const auto i = std::find_if(_tiles.begin(), _tiles.end(),
		[=](const Tile &tile) {
			return tile.area.contains(area) && !tile.blurred.isNull();
		});
	if (i == _tiles.end()) {
		p.fillRect(area, tint);
		return;
	}
	p.save();
	p.setClipRect(area, Qt::IntersectClip);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawImage(QRectF(i->area), i->blurred);
	tint.setAlphaF(tint.alphaF() * kTintOpacity);
	p.fillRect(area, tint);
	p.restore();
}

} // namespace AyuUi
