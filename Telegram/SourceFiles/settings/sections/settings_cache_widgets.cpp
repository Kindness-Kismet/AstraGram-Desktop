#include "settings/sections/settings_cache_widgets.h"

#include "settings.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "statistics/view/stack_linear_chart_common.h"
#include "ui/text/format_values.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "styles/style_boxes.h"
#include "styles/style_settings_local_storage.h"

#include <QtCore/QStorageInfo>

namespace Settings {

QColor CachePartColor(int index) {
	switch (index) {
	case 0: return st::statisticsChartLineLightblue->c;
	case 1: return st::statisticsChartLineOrange->c;
	case 2: return st::statisticsChartLineLightgreen->c;
	case 3: return st::statisticsChartLinePurple->c;
	case 4: return st::statisticsChartLineGreen->c;
	case 5: return st::statisticsChartLineBlue->c;
	}
	Unexpected("Invalid cache category.");
}

QString CachePartName(int index) {
	switch (Extras::MediaCacheCategory(index)) {
	case Extras::MediaCacheCategory::Images: return tr::extras_CacheImages(tr::now);
	case Extras::MediaCacheCategory::Stickers: return tr::extras_CacheStickers(tr::now);
	case Extras::MediaCacheCategory::Documents: return tr::extras_CacheDocuments(tr::now);
	case Extras::MediaCacheCategory::Avatars: return tr::extras_CacheAvatars(tr::now);
	case Extras::MediaCacheCategory::Other: return tr::extras_CacheOther(tr::now);
	case Extras::MediaCacheCategory::Messages: return tr::extras_CacheMessages(tr::now);
	case Extras::MediaCacheCategory::Count: break;
	}
	Unexpected("Invalid cache category.");
}

QString CacheCategoryId(int index) {
	const auto names = std::array{
		u"images"_q, u"stickers"_q, u"documents"_q,
		u"avatars"_q, u"other"_q, u"messages"_q,
	};
	return names[index];
}

namespace {

[[nodiscard]] QString FormatStoragePercent(int64 part, int64 whole) {
	if (whole <= 0) {
		return u"0%"_q;
	}
	const auto ratio = part / float64(whole);
	if (ratio < 0.001) {
		return u"<0.1%"_q;
	}
	const auto percent = int(base::SafeRound(ratio * 100.));
	return (percent <= 0)
		? u"<1%"_q
		: (QString::number(percent) + '%');
}

[[nodiscard]] QString FormatStorageSize(int64 size) {
	constexpr auto kGigabyte = int64(1024) * 1024 * 1024;
	if (size < kGigabyte) {
		return Ui::FormatSizeText(size);
	}
	const auto tenthGb = size * 10 / kGigabyte;
	return QString::number(tenthGb / 10)
		+ '.'
		+ QString::number(tenthGb % 10)
		+ u" GB"_q;
}

} // namespace

CacheChart::CacheChart(QWidget *parent)
: RpWidget(parent) {
	setMouseTracking(true);
}

void CacheChart::setLoaded() {
	_loaded = true;
	update();
}

void CacheChart::setParts(
		std::array<int64, kChartPartsCount> sizes,
		int64 total) {
	hideTooltip();
	_sizes = sizes;
	_total = total;
	_percent = { { 0 } };
	if (total > 0) {
		const auto values = std::vector<float64>(sizes.begin(), sizes.end());
		const auto percentage = Statistic::PiePartsPercentage(
			values, float64(total), true);
		for (auto i = 0; i != kChartPartsCount; ++i) {
			_percent[i] = int(base::SafeRound(
				percentage.parts[i].roundedPercentage * 100.));
		}
	}
	resizeGetHeight(width());
	setSelected(-1);
	update();
}

QRect CacheChart::barRect() const {
	const auto padding = st::localStorageChartPadding;
	return QRect(padding,
		padding + st::localStorageChartLabelFont->height
			+ st::localStorageChartSizeFont->height
			+ st::localStorageChartBlockSkip * 2,
		std::max(width() - padding * 2, 0),
		st::localStorageChartBarHeight);
}

int CacheChart::resizeGetHeight(int newWidth) {
	const auto padding = st::localStorageChartPadding;
	const auto available = std::max(newWidth - padding * 2, 0);
	const auto columns = std::clamp(
		available / st::localStorageChartLegendMinWidth, 1, 3);
	const auto cellWidth = available / columns;
	const auto bar = barRect();
	const auto legendTop = bar.bottom() + 1 + st::localStorageChartBlockSkip;
	for (auto i = 0; i != kChartPartsCount; ++i) {
		_legends[i] = style::rtlrect(
			padding + (i % columns) * cellWidth,
			legendTop + (i / columns) * st::localStorageChartLegendHeight,
			cellWidth,
			st::localStorageChartLegendHeight,
			newWidth);
	}
	auto offset = 0.;
	for (auto i = 0; i != kChartPartsCount; ++i) {
		const auto next = offset + ((_total > 0)
			? float64(_sizes[i]) * available / _total
			: 0.);
		_segments[i] = QRectF(
			style::RightToLeft() ? newWidth - padding - next : padding + offset,
			bar.y(), next - offset, bar.height());
		offset = next;
	}
	return legendTop
		+ ((kChartPartsCount + columns - 1) / columns)
			* st::localStorageChartLegendHeight
		+ padding;
}

void CacheChart::setSelected(int index) {
	if (_selected == index) {
		return;
	}
	_selected = index;
	setCursor(index >= 0 ? style::cur_pointer : style::cur_default);
	update();
}

int CacheChart::segmentAt(QPointF pos) const {
	for (auto i = 0; i != kChartPartsCount; ++i) {
		if (_sizes[i] > 0
			&& (_segments[i].contains(pos) || _legends[i].contains(pos.toPoint()))) {
			return i;
		}
	}
	return -1;
}

void CacheChart::mouseMoveEvent(QMouseEvent *e) {
	setSelected(segmentAt(e->pos()));
}

void CacheChart::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto index = segmentAt(e->pos());
	if (index < 0 || index == _tooltipPart) {
		hideTooltip();
		return;
	}
	showTooltip(index);
}

void CacheChart::setPartInfo(Fn<TextWithEntities(int)> info) {
	_partInfo = std::move(info);
}

void CacheChart::showTooltip(int index) {
	hideTooltip();
	if (!_partInfo) {
		return;
	}
	const auto parent = window();
	_tooltipPart = index;
	_tooltip = std::make_unique<Ui::ImportantTooltip>(
		parent,
		Ui::MakeNiceTooltipLabel(parent,
			rpl::single(_partInfo(index)),
			st::localStorageUsageTooltipMaxWidth,
			st::defaultImportantTooltipLabel),
		st::defaultImportantTooltip);
	const auto tooltip = _tooltip.get();
	const auto weak = base::make_weak(tooltip);
	tooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
	tooltip->setHiddenCallback([=] { delete weak.get(); });
	const auto local = _legends[index].topLeft()
		+ QPoint(_legends[index].width() / 2, -st::localStorageUsageTooltipSkip);
	tooltip->pointAt(Ui::MapFrom(parent, this, QRect(local, QSize())), RectPart::Top);
	tooltip->toggleAnimated(true);
}

void CacheChart::hideTooltip() {
	_tooltipPart = -1;
	if (const auto tooltip = _tooltip.release()) {
		tooltip->toggleAnimated(false);
	}
}

void CacheChart::leaveEventHook(QEvent *e) {
	setSelected(-1);
}

void CacheChart::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto padding = st::localStorageChartPadding;
	const auto labelFont = st::localStorageChartLabelFont;
	const auto sizeFont = st::localStorageChartSizeFont;
	const auto alignedText = [&](QRect rect, const QString &text) {
		p.drawText(rect, Qt::AlignVCenter
			| (style::RightToLeft() ? Qt::AlignRight : Qt::AlignLeft), text);
	};
	p.setFont(labelFont);
	p.setPen(st::windowSubTextFg);
	alignedText(QRect(padding, padding, width() - 2 * padding, labelFont->height),
		tr::lng_local_storage_device_telegram(tr::now));
	p.setFont(sizeFont);
	p.setPen(st::windowBoldFg);
	alignedText(QRect(padding,
		padding + labelFont->height + st::localStorageChartBlockSkip,
		width() - 2 * padding, sizeFont->height),
		_loaded ? FormatStorageSize(_total) : u"…"_q);

	const auto bar = barRect();
	auto clip = QPainterPath();
	clip.addRoundedRect(bar, bar.height() / 2., bar.height() / 2.);
	p.save();
	p.setClipPath(clip);
	p.fillRect(bar, st::windowBgOver);
	for (auto i = 0; i != kChartPartsCount; ++i) {
		if (_sizes[i] <= 0) {
			continue;
		}
		const auto color = CachePartColor(i);
		p.fillRect(_segments[i], (_selected == i) ? color.lighter(115) : color);
	}
	p.restore();

	const auto dot = st::localStorageChartLegendDot;
	const auto skip = dot + st::localStorageChartBlockSkip;
	p.setFont(labelFont);
	for (auto i = 0; i != kChartPartsCount; ++i) {
		const auto cell = _legends[i];
		p.setPen(Qt::NoPen);
		p.setBrush(CachePartColor(i));
		p.drawEllipse(style::rtlrect(
			style::RightToLeft() ? width() - cell.right() - 1 : cell.x(),
			cell.y() + (labelFont->height - dot) / 2,
			dot, dot, width()));
		const auto textRect = cell.adjusted(
			style::RightToLeft() ? st::localStorageChartBlockSkip : skip, 0,
			style::RightToLeft() ? -skip : -st::localStorageChartBlockSkip, 0);
		p.setPen(st::windowFg);
		alignedText(QRect(textRect.x(), cell.y(), textRect.width(), labelFont->height),
			labelFont->elided(CachePartName(i), textRect.width()));
		p.setPen(st::windowSubTextFg);
		alignedText(QRect(textRect.x(), cell.y() + labelFont->height + 2,
			textRect.width(), labelFont->height),
			FormatStorageSize(_sizes[i]) + u" · "_q
				+ ((_sizes[i] > 0 && !_percent[i])
					? u"<1%"_q
					: (QString::number(_percent[i]) + '%')));
	}
}

CacheDeviceBar::CacheDeviceBar(QWidget *parent)
: RpWidget(parent)
, _subtitle(this, QString(), st::localStorageUsageSubtitle) {
	_subtitle->setAttribute(Qt::WA_TransparentForMouseEvents);
	setMouseTracking(true);
	refresh();
}

void CacheDeviceBar::refresh() {
	const auto weak = base::make_weak(this);
	const auto dir = cWorkingDir();
	crl::async([=] {
		const auto info = QStorageInfo(dir);
		const auto total = info.isValid() ? info.bytesTotal() : int64();
		const auto free = info.isValid() ? info.bytesAvailable() : int64();
		crl::on_main(weak, [=] {
			_total = total;
			_free = (free > total) ? total : free;
			apply();
		});
	});
}

void CacheDeviceBar::apply() {
	const auto used = std::max(_total - _free, int64());
	_cache = std::min(_reported, used);
	_subtitle->setText(tr::lng_local_storage_device_usage(
		tr::now,
		lt_percent,
		FormatStoragePercent(_cache, _total)));
	resizeToWidth(width());
	update();
}

void CacheDeviceBar::setCache(int64 cache) {
	_reported = cache;
	apply();
	refresh();
}

QRect CacheDeviceBar::barRect() const {
	const auto width = std::min(st::localStorageUsageBarWidth, QWidget::width());
	const auto top = st::localStorageUsageTopSkip
		+ _subtitle->height()
		+ st::localStorageUsageBarSkip;
	return QRect(
		(QWidget::width() - width) / 2,
		top,
		width,
		st::localStorageUsageBarHeight);
}

int CacheDeviceBar::resizeGetHeight(int newWidth) {
	_subtitle->resizeToWidth(newWidth);
	_subtitle->moveToLeft(0, st::localStorageUsageTopSkip, newWidth);
	return st::localStorageUsageTopSkip
		+ _subtitle->height()
		+ st::localStorageUsageBarSkip
		+ st::localStorageUsageBarHeight
		+ st::localStorageUsageBottomSkip;
}

void CacheDeviceBar::paintEvent(QPaintEvent *e) {
	if (_total <= 0) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto bar = QRectF(barRect());
	const auto radius = bar.height() / 2.;
	const auto used = _total - _free;
	const auto cacheColor = st::activeButtonBg->c;
	const auto usedColor = st::scrollBg->c;
	const auto freeColor = st::windowBgRipple->c;

	auto path = QPainterPath();
	path.addRoundedRect(bar, radius, radius);
	p.setClipPath(path);
	p.fillRect(bar, freeColor);

	const auto fill = [&](int64 from, int64 size, QColor color) {
		if (size <= 0) {
			return;
		}
		const auto x1 = bar.x() + bar.width() * (from / float64(_total));
		const auto x2 = bar.x()
			+ bar.width() * ((from + size) / float64(_total));
		p.fillRect(QRectF(x1, bar.y(), x2 - x1, bar.height()), color);
	};
	fill(0, used, usedColor);
	fill(0, _cache, cacheColor);
}

void CacheDeviceBar::mouseMoveEvent(QMouseEvent *e) {
	if (_total > 0 && barRect().marginsAdded(st::localStorageUsageBarMargin)
			.contains(e->pos())) {
		showTooltip();
	} else {
		hideTooltip();
	}
}

void CacheDeviceBar::leaveEventHook(QEvent *e) {
	hideTooltip();
}

TextWithEntities CacheDeviceBar::tooltipText() const {
	const auto used = _total - _free;
	const auto other = std::max(used - _cache, int64());
	auto result = TextWithEntities();
	const auto line = [&](const QString &label, int64 size) {
		if (!result.text.isEmpty()) {
			result.append('\n');
		}
		result.append(label).append(u": "_q).append(
			Ui::Text::Bold(FormatStorageSize(size)));
	};
	line(tr::lng_local_storage_device_telegram(tr::now), _cache);
	line(tr::lng_local_storage_device_other(tr::now), other);
	line(tr::lng_local_storage_device_free(tr::now), _free);
	line(tr::lng_local_storage_device_total(tr::now), _total);
	return result;
}

void CacheDeviceBar::showTooltip() {
	if (_tooltip || _total <= 0) {
		return;
	}
	const auto parent = window();
	_tooltip = std::make_unique<Ui::ImportantTooltip>(
		parent,
		Ui::MakeNiceTooltipLabel(
			parent,
			rpl::single(tooltipText()),
			st::localStorageUsageTooltipMaxWidth,
			st::defaultImportantTooltipLabel),
		st::defaultImportantTooltip);
	const auto tooltip = _tooltip.get();
	const auto weak = base::make_weak(tooltip);
	tooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
	tooltip->setHiddenCallback([=] { delete weak.get(); });
	auto area = Ui::MapFrom(parent, this, barRect());
	area.translate(0, -st::localStorageUsageTooltipSkip);
	tooltip->pointAt(area, RectPart::Top);
	tooltip->toggleAnimated(true);
}

void CacheDeviceBar::hideTooltip() {
	if (const auto tooltip = _tooltip.release()) {
		tooltip->toggleAnimated(false);
	}
}

CacheClearButton::CacheClearButton(QWidget *parent)
: RippleButton(parent, st::defaultRippleAnimation)
, _label(st::localStorageClearFont, [=] { update(); }, {
	.splitByWords = true,
	.preserveIndex = true,
	.startFromEnd = true,
	.moveAmplitude = 0.25,
	.duration = crl::time(300),
})
, _amount(st::localStorageClearFont, [=] { update(); }, {
	.splitByWords = true,
	.preserveIndex = true,
	.startFromEnd = true,
	.moveAmplitude = 0.25,
	.duration = crl::time(300),
}) {
}

void CacheClearButton::setText(
		const QString &label,
		const QString &amount) {
	_label.setText(label);
	_amount.setText(amount);
}

int CacheClearButton::resizeGetHeight(int newWidth) {
	return st::localStorageClearHeight;
}

void CacheClearButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto disabled = isDisabled();
	const auto radius = height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(disabled ? st::windowBgRipple : st::activeButtonBg);
	p.drawRoundedRect(rect(), radius, radius);

	if (!disabled) {
		const auto ripple = st::activeButtonBgRipple->c;
		paintRipple(p, 0, 0, &ripple);
	}

	const auto font = st::localStorageClearFont;
	const auto labelWidth = _label.currentWidth();
	const auto amountWidth = _amount.currentWidth();
	const auto skip = (amountWidth > 0.) ? st::localStorageClearSkip : 0;
	const auto left = (width() - labelWidth - skip - amountWidth) / 2.;
	const auto top = (height() - font->height) / 2;
	const auto fg = disabled ? st::windowSubTextFg->c : st::activeButtonFg->c;
	_label.draw(p, int(base::SafeRound(left)), top, fg);
	_amount.draw(p, int(base::SafeRound(left + labelWidth + skip)), top, fg, 0.6);
}

QImage CacheClearButton::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(size(), height() / 2);
}

QPoint CacheClearButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

CacheCategoryRow::CacheCategoryRow(
	QWidget *parent,
	const QString &title,
	int chartIndex,
	bool checked,
	const Extras::MediaCacheUsage &data)
: RippleButton(parent, st::defaultRippleAnimation)
, _checkColor([=] { return CachePartColor(chartIndex); })
, _checkStyle(st::defaultCheck)
, _check(_checkStyle, checked, [=] { Ui::RpWidget::update(); })
, _percent(this, QString(), st::localStorageCategoryPercent)
, _name(this, title, st::localStorageCategoryName)
, _size(this, sizeText(data), st::localStorageCategorySize) {
	_checkStyle.toggledFg = _checkColor.color();
	_percent->setAttribute(Qt::WA_TransparentForMouseEvents);
	_name->setAttribute(Qt::WA_TransparentForMouseEvents);
	_size->setAttribute(Qt::WA_TransparentForMouseEvents);
	setClickedCallback([=] { toggle(); });
}

void CacheCategoryRow::toggle() {
	const auto value = !_check.checked();
	_check.setChecked(value, anim::type::normal);
	_checkedChanges.fire_copy(value);
}

rpl::producer<bool> CacheCategoryRow::checkedChanges() const {
	return _checkedChanges.events();
}

void CacheCategoryRow::setUsage(const Extras::MediaCacheUsage &data) {
	_size->setText(sizeText(data));
	resizeToWidth(width());
}

void CacheCategoryRow::setPercent(int percent) {
	_hasPercent = (percent > 0);
	_percent->setText(_hasPercent
		? (QString::number(percent) + '%')
		: QString());
	resizeToWidth(width());
}

int CacheCategoryRow::resizeGetHeight(int newWidth) {
	const auto height = st::localStorageCategoryHeight;
	const auto pad = st::localStorageCategoryPadding;
	const auto checkSize = _check.getSize();
	const auto left = pad.left()
		+ checkSize.width()
		+ st::localStorageCategoryCheckSkip;

	_size->resizeToWidth(std::min(_size->textMaxWidth(), newWidth));
	_size->moveToRight(pad.right(), (height - _size->height()) / 2, newWidth);

	_percent->resizeToWidth(_percent->textMaxWidth());
	_percent->setVisible(_hasPercent);
	const auto percentWidth = _hasPercent
		? (st::localStorageCategoryPercentSkip + _percent->width())
		: 0;
	const auto nameAvailable = std::max(
		_size->x() - st::localStorageCategoryPercentSkip - percentWidth - left,
		0);
	_name->resizeToWidth(std::min(_name->textMaxWidth(), nameAvailable));
	_name->moveToLeft(left, (height - _name->height()) / 2, newWidth);
	if (_hasPercent) {
		_percent->moveToLeft(
			left + _name->width() + st::localStorageCategoryPercentSkip,
			(height - _percent->height()) / 2,
			newWidth);
	}
	return height;
}

void CacheCategoryRow::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.save();
	ClipCardRow(p, this);
	paintRipple(p, 0, 0);
	p.restore();
	if (isDisabled()) {
		p.setOpacity(st::defaultCheckbox.disabledOpacity);
	}
	const auto pad = st::localStorageCategoryPadding;
	const auto checkSize = _check.getSize();
	_check.paint(
		p,
		pad.left(),
		(height() - checkSize.height()) / 2,
		width());
}

void CacheCategoryRow::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	_check.setOver(
		isOver() && !isDisabled(),
		isDisabled() ? anim::type::instant : anim::type::normal);
}

QImage CacheCategoryRow::prepareRippleMask() const {
	return Ui::RippleAnimation::RectMask(size());
}

QPoint CacheCategoryRow::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QString CacheCategoryRow::sizeText(
		const Extras::MediaCacheUsage &data) const {
	return data.bytes
		? Ui::FormatSizeText(data.bytes)
		: tr::lng_local_storage_empty(tr::now);
}

} // namespace Settings
