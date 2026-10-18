#include "info/downloads/info_downloads_type_tiles.h"

#include "lang/lang_keys.h"
#include "lang/lang_tag.h"
#include "ui/effects/ripple_animation.h"
#include "ui/widgets/buttons.h"
#include "ui/painter.h"
#include "styles/style_info.h"

namespace Info::Downloads {
namespace {

// 与调试指令 downloads.list 的分类名一致。
[[nodiscard]] QString TypeKey(TypeFilter filter) {
	switch (filter) {
	case TypeFilter::All: return u"all"_q;
	case TypeFilter::Archive: return u"archives"_q;
	case TypeFilter::Music: return u"music"_q;
	case TypeFilter::Video: return u"videos"_q;
	case TypeFilter::Other: return u"other"_q;
	}
	Unexpected("Invalid download type filter.");
}

// 压缩包、其他文件与列表里 zip、txt 的图标同色；音乐、视频取另外两种文件色。
[[nodiscard]] const style::color &TypeColor(TypeFilter filter) {
	switch (filter) {
	case TypeFilter::All: return st::windowBgActive;
	case TypeFilter::Archive: return st::msgFile4Bg;
	case TypeFilter::Music: return st::msgFile2Bg;
	case TypeFilter::Video: return st::msgFile3Bg;
	case TypeFilter::Other: return st::msgFile1Bg;
	}
	Unexpected("Invalid download type filter.");
}

[[nodiscard]] const style::icon &TypeIcon(TypeFilter filter) {
	switch (filter) {
	case TypeFilter::All: return st::infoDownloadsTypeIconAll;
	case TypeFilter::Archive: return st::infoDownloadsTypeIconArchive;
	case TypeFilter::Music: return st::infoDownloadsTypeIconMusic;
	case TypeFilter::Video: return st::infoDownloadsTypeIconVideo;
	case TypeFilter::Other: return st::infoDownloadsTypeIconOther;
	}
	Unexpected("Invalid download type filter.");
}

// 色块空间有限，“其他文件”改用短名，完整名称由页头标题显示。
[[nodiscard]] QString TileLabel(TypeFilter filter) {
	return (filter == TypeFilter::Other)
		? tr::extras_DownloadsTypeOtherShort(tr::now)
		: TypeFilterLabel(filter);
}

} // namespace

class TypeTiles::Tile final : public Ui::RippleButton {
public:
	Tile(QWidget *parent, TypeFilter filter);

	[[nodiscard]] TypeFilter filter() const {
		return _filter;
	}
	[[nodiscard]] const QString &label() const {
		return _label;
	}
	void setLabel(const QString &label);
	void setActive(bool active);
	void setCompact(bool compact);
	void setCount(std::optional<int> count);

	QString accessibilityName() override;
	Ui::AccessibilityState accessibilityState() const override;

private:
	void paintEvent(QPaintEvent *e) override;
	QImage prepareRippleMask() const override;

	void paintFrame(QPainter &p);
	void paintCount(Painter &p, QRect icon);

	const TypeFilter _filter;
	QString _label;
	QString _count;
	bool _active = false;
	bool _compact = false;

};

TypeTiles::Tile::Tile(QWidget *parent, TypeFilter filter)
: RippleButton(parent, st::infoDownloadsTypeTileRipple)
, _filter(filter) {
	setIsPageTab(true);
	setObjectName(u"downloads/type/"_q + TypeKey(filter));
}

void TypeTiles::Tile::setLabel(const QString &label) {
	if (_label == label) {
		return;
	}
	_label = label;
	accessibilityNameChanged();
	update();
}

void TypeTiles::Tile::setActive(bool active) {
	if (_active == active) {
		return;
	}
	_active = active;
	accessibilityStateChanged({ .selected = true });
	update();
}

void TypeTiles::Tile::setCompact(bool compact) {
	if (_compact == compact) {
		return;
	}
	_compact = compact;
	update();
}

void TypeTiles::Tile::setCount(std::optional<int> count) {
	auto text = count ? Lang::FormatCountDecimal(*count) : QString();
	if (_count == text) {
		return;
	}
	_count = std::move(text);
	accessibilityNameChanged();
	update();
}

QString TypeTiles::Tile::accessibilityName() {
	return _count.isEmpty()
		? _label
		: u"%1 (%2)"_q.arg(_label, _count);
}

Ui::AccessibilityState TypeTiles::Tile::accessibilityState() const {
	auto state = RippleButton::accessibilityState();
	state.selectable = true;
	state.selected = _active;
	return state;
}

void TypeTiles::Tile::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	paintFrame(p);
	paintRipple(p, 0, 0);

	const auto size = st::infoDownloadsTypeTileIconSize;
	const auto icon = QRect(
		(width() - size) / 2,
		st::infoDownloadsTypeTileIconTop,
		size,
		size);
	{
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = st::infoDownloadsTypeTileIconRadius;
		p.setPen(Qt::NoPen);
		p.setBrush(TypeColor(_filter));
		p.drawRoundedRect(icon, radius, radius);
	}
	TypeIcon(_filter).paintInCenter(p, icon);

	const auto &font = _compact
		? st::infoDownloadsTypeTileLabelFontCompact
		: st::infoDownloadsTypeTileLabelFont;
	const auto skip = st::infoDownloadsTypeTileLabelSkip;
	const auto available = std::max(width() - 2 * skip, 0);
	p.setFont(font);
	p.setPen(_active ? st::lightButtonFg : st::windowSubTextFg);
	p.drawText(
		QRect(skip, st::infoDownloadsTypeTileLabelTop, available, font->height),
		font->elided(_label, available),
		style::al_top);

	paintCount(p, icon);
}

void TypeTiles::Tile::paintFrame(QPainter &p) {
	auto hq = PainterHighQualityEnabler(p);
	const auto line = float64(st::lineWidth);
	const auto half = line / 2.;
	const auto radius = st::infoDownloadsTypeTileRadius - half;
	p.setPen(QPen(_active ? st::windowBgActive : st::controlBorder, line));
	if (_active) {
		p.setBrush(st::controlActiveBg);
	} else if (isOver() || isDown()) {
		p.setBrush(st::windowBgOver);
	} else {
		p.setBrush(Qt::NoBrush);
	}
	p.drawRoundedRect(
		QRectF(rect()).marginsRemoved({ half, half, half, half }),
		radius,
		radius);
}

void TypeTiles::Tile::paintCount(Painter &p, QRect icon) {
	if (_count.isEmpty()) {
		return;
	}
	const auto &font = st::infoDownloadsTypeTileCountFont;
	const auto position = st::infoDownloadsTypeTileCountPosition;
	const auto countWidth = font->width(_count);
	const auto left = width() - position.x() - countWidth;
	// 色块太窄时数量会压住图标，此时只由页头显示当前分类的数量。
	if (left < icon.x() + icon.width() + position.x()) {
		return;
	}
	p.setFont(font);
	p.setPen(_active ? st::lightButtonFg : st::windowSubTextFg);
	p.drawTextLeft(left, position.y(), width(), _count, countWidth);
}

QImage TypeTiles::Tile::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(
		size(),
		st::infoDownloadsTypeTileRadius);
}

TypeTiles::TypeTiles(
	QWidget *parent,
	rpl::producer<TypeFilter> active,
	rpl::producer<std::optional<TypeCounts>> counts)
: RpWidget(parent) {
	setObjectName(u"downloads/typeTiles"_q);
	for (const auto filter : kTypeFilters) {
		const auto tile = Ui::CreateChild<Tile>(this, filter);
		tile->setClickedCallback([=] {
			_activations.fire_copy(filter);
		});
		tile->show();
		_tiles.push_back(tile);
	}
	rpl::single(rpl::empty) | rpl::then(
		Lang::Updated()
	) | rpl::on_next([=] {
		for (const auto tile : _tiles) {
			tile->setLabel(TileLabel(tile->filter()));
		}
		resizeToWidth(width());
	}, lifetime());
	std::move(active) | rpl::on_next([=](TypeFilter filter) {
		for (const auto tile : _tiles) {
			tile->setActive(tile->filter() == filter);
		}
	}, lifetime());
	std::move(
		counts
	) | rpl::on_next([=](const std::optional<TypeCounts> &counts) {
		for (const auto tile : _tiles) {
			tile->setCount(counts
				? std::make_optional((*counts)[tile->filter()])
				: std::nullopt);
		}
	}, lifetime());
}

rpl::producer<TypeFilter> TypeTiles::activations() const {
	return _activations.events();
}

int TypeTiles::resizeGetHeight(int newWidth) {
	const auto count = int(_tiles.size());
	const auto available = (newWidth
		- (count - 1) * st::infoDownloadsTypeTileSkip) / count
		- 2 * st::infoDownloadsTypeTileLabelSkip;
	const auto &font = st::infoDownloadsTypeTileLabelFont;
	// 任一标签放不下时整行改用紧凑排版，避免只有部分色块截断。
	const auto compact = ranges::any_of(_tiles, [&](not_null<Tile*> tile) {
		return font->width(tile->label()) > available;
	});
	const auto skip = compact
		? st::infoDownloadsTypeTileSkipCompact
		: st::infoDownloadsTypeTileSkip;
	const auto height = st::infoDownloadsTypeTileHeight;
	// 按比例取整分配余数，各块等宽且恰好铺满整行。
	for (auto i = 0; i != count; ++i) {
		const auto left = (newWidth + skip) * i / count;
		const auto right = (newWidth + skip) * (i + 1) / count - skip;
		_tiles[i]->setCompact(compact);
		_tiles[i]->setGeometryToLeft(
			left,
			0,
			std::max(right - left, 0),
			height,
			newWidth);
	}
	return height;
}

} // namespace Info::Downloads
