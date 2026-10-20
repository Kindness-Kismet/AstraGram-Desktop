#pragma once

#include "extras/data/media_cache_manager.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/effects/animated_string.h"
#include "ui/effects/drifting_particles.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

inline constexpr auto kChartPartsCount = Extras::kMediaCacheCategoryCount;

[[nodiscard]] QColor CachePartColor(int index);
[[nodiscard]] QString CachePartName(int index);
[[nodiscard]] QString CacheCategoryId(int index);

class CompleteParticles final {
public:
	CompleteParticles();

	void paint(
		QPainter &p,
		QRect rect,
		QPointF center,
		float64 alpha,
		bool paused);

private:
	Ui::DriftingParticles _particles;
	QImage _layer;

};

class CacheChart final : public Ui::RpWidget {
public:
	CacheChart(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

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
	struct Geometry {
		float64 center = 0.;
		float64 half = 0.;
	};

	void computeTargets();
	[[nodiscard]] bool paused() const;
	[[nodiscard]] std::array<Geometry, kChartPartsCount> shown() const;
	void setSelected(int index);
	[[nodiscard]] int segmentAt(QPointF pos) const;
	[[nodiscard]] QPointF partCenter(int index) const;
	void showTooltip(int index);
	void hideTooltip();
	[[nodiscard]] QPointF radialPoint(
		QPointF center,
		float64 radius,
		float64 angle) const;
	void drawParticles(
		QPainter &p,
		QPointF center,
		int index,
		float64 from,
		float64 to,
		float64 innerRadius,
		float64 outerRadius,
		float64 alpha);

	std::array<int64, kChartPartsCount> _sizes = { { 0 } };
	std::array<Geometry, kChartPartsCount> _from;
	std::array<Geometry, kChartPartsCount> _to;
	std::array<int, kChartPartsCount> _percent = { { 0 } };
	std::array<bool, kChartPartsCount> _showPercent = { { false } };
	std::array<float64, kChartPartsCount> _percentTarget = { { 0. } };
	std::array<Ui::Animations::Simple, kChartPartsCount> _percentAlpha;
	int64 _total = 0;

	Ui::Animations::Simple _morph;
	Ui::Animations::Simple _appear;
	bool _appeared = false;
	bool _loaded = false;
	bool _complete = false;
	Ui::Animations::Simple _completeAnimation;
	CompleteParticles _completeParticles;

	int _selected = -1;
	std::array<Ui::Animations::Simple, kChartPartsCount> _selectAnimations;

	const not_null<Window::SessionController*> _controller;
	Ui::Animations::Basic _particles;
	crl::time _particlesStart = 0;
	crl::time _particlesPausedAt = 0;
	bool _shown = false;

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
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	QString sizeText(const Extras::MediaCacheUsage &data) const;
	void toggle();

	style::complex_color _checkColor;
	style::Check _checkStyle;
	Ui::RoundCheckView _check;
	object_ptr<Ui::FlatLabel> _percent;
	object_ptr<Ui::FlatLabel> _name;
	object_ptr<Ui::FlatLabel> _size;
	rpl::event_stream<bool> _checkedChanges;
	bool _hasPercent = false;

};

} // namespace Settings
