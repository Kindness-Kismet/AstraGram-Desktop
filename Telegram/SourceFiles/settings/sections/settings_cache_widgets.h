#pragma once

#include "extras/data/media_cache_manager.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/effects/animated_string.h"

namespace Settings {

inline constexpr auto kChartPartsCount = Extras::kMediaCacheCategoryCount;

[[nodiscard]] QColor CachePartColor(int index);
[[nodiscard]] QString CachePartName(int index);
[[nodiscard]] QString CacheCategoryId(int index);

class CacheChart final : public Ui::RpWidget {
public:
	explicit CacheChart(QWidget *parent);

	void setParts(std::array<int64, kChartPartsCount> sizes, int64 total);
	void setPartInfo(Fn<TextWithEntities(int)> info);
	void setLoaded();
	[[nodiscard]] int partPercent(int index) const {
		return _percent[index];
	}

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void setSelected(int index);
	[[nodiscard]] int segmentAt(QPointF pos) const;
	[[nodiscard]] QRect barRect() const;
	void showTooltip(int index);
	void hideTooltip();

	std::array<int64, kChartPartsCount> _sizes = { { 0 } };
	std::array<int, kChartPartsCount> _percent = { { 0 } };
	std::array<QRectF, kChartPartsCount> _segments;
	std::array<QRect, kChartPartsCount> _legends;
	int64 _total = 0;
	bool _loaded = false;
	int _selected = -1;

	Fn<TextWithEntities(int)> _partInfo;
	std::unique_ptr<Ui::ImportantTooltip> _tooltip;
	int _tooltipPart = -1;

};

class CacheDeviceBar final : public Ui::RpWidget {
public:
	explicit CacheDeviceBar(QWidget *parent);

	void setCache(int64 cache);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void refresh();
	void apply();
	[[nodiscard]] QRect barRect() const;
	[[nodiscard]] TextWithEntities tooltipText() const;
	void showTooltip();
	void hideTooltip();

	object_ptr<Ui::FlatLabel> _subtitle;
	std::unique_ptr<Ui::ImportantTooltip> _tooltip;
	int64 _reported = 0;
	int64 _cache = 0;
	int64 _total = 0;
	int64 _free = 0;

};

class CacheClearButton final : public Ui::RippleButton {
public:
	explicit CacheClearButton(QWidget *parent);

	void setText(const QString &label, const QString &amount);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	Ui::AnimatedString _label;
	Ui::AnimatedString _amount;

};

class CacheCategoryRow final : public Ui::RippleButton {
public:
	CacheCategoryRow(
		QWidget *parent,
		const QString &title,
		int chartIndex,
		bool checked,
		const Extras::MediaCacheUsage &data);

	void setUsage(const Extras::MediaCacheUsage &data);
	void setPercent(int percent);

	[[nodiscard]] rpl::producer<bool> checkedChanges() const;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	QString sizeText(const Extras::MediaCacheUsage &data) const;
	void toggle();

	style::complex_color _checkColor;
	style::Check _checkStyle;
	Ui::CheckView _check;
	object_ptr<Ui::FlatLabel> _percent;
	object_ptr<Ui::FlatLabel> _name;
	object_ptr<Ui::FlatLabel> _size;
	rpl::event_stream<bool> _checkedChanges;
	bool _hasPercent = false;

};

} // namespace Settings
