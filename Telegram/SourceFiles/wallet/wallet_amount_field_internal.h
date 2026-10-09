#pragma once

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_amount_field.h"

#include "base/timer.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "ui/controls/ton_common.h"
#include "ui/effects/animations.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "wallet/wallet_amount_painter.h"

#include <QtCore/QLocale>
#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethod>
#include <QtGui/QStyleHints>
#include <QtGui/QTextLayout>
#include <QtWidgets/QStyle>

#include "styles/palette.h"
#include "styles/style_wallet.h"
#include "styles/style_widgets.h"

namespace Wallet {

namespace AmountFieldDetails {}

using namespace AmountFieldDetails;

namespace AmountFieldDetails {

constexpr auto kEditDuration = crl::time(180);

constexpr auto kSwitchFirstDuration = crl::time(300);

constexpr auto kSwitchDuration = crl::time(450);

constexpr auto kSwitchFadeDuration = crl::time(300);

enum class Ease {
	OutCubic,
	OutCirc,
};

enum class EditKind {
	None,
	Replace,
	Append,
	Remove,
	Immediate,
};

enum class GlyphKind {
	Digit,
	Decimal,
	Group,
	Other,
};

enum class GlyphMode {
	Scale,
	Fade,
	Roll,
};

enum class LayerKind {
	Diamond,
	Symbol,
	Ticker,
};

struct Motion {
	[[nodiscard]] float64 value(crl::time now) const;
	[[nodiscard]] bool running(crl::time now) const;
	void retarget(
		float64 target,
		crl::time now,
		crl::time length,
		Ease kind = Ease::OutCubic);
	void jump(float64 target);

	float64 from = 0.;
	float64 to = 0.;
	crl::time start = 0;
	crl::time duration = 0;
	Ease ease = Ease::OutCubic;
};

struct GlyphTarget {
	QChar ch;
	GlyphKind kind = GlyphKind::Digit;
	bool small = false;
	float64 width = 0.;
	int place = 0;

	friend inline bool operator==(
		const GlyphTarget &,
		const GlyphTarget &) = default;
};

struct GlyphLayer {
	QChar ch;
	GlyphMode mode = GlyphMode::Scale;
	int side = 0;
	float64 width = 0.;
	Motion v;
};

struct Slot {
	uint32 id = 0;
	GlyphKind kind = GlyphKind::Digit;
	bool small = false;
	bool dying = false;
	int place = 0;
	Motion width;
	Motion presence;
	GlyphLayer current;
	std::vector<GlyphLayer> leaving;
};

struct Separator {
	QChar ch;
	uint32 gap = 0;
	float64 width = 0.;
	float64 fromX = 0.;
	Motion slide;
	Motion presence;
};

struct GlyphChange {
	GlyphMode mode = GlyphMode::Scale;
	int side = 0;
	Ease ease = Ease::OutCubic;
	bool slide = false;
	std::vector<crl::time> durations;
};

struct InkRange {
	float64 left = 0.;
	float64 right = 0.;
};

struct FlowGap {
	int position = -1;
	float64 width = 0.;
};

struct Layer {
	LayerKind kind = LayerKind::Ticker;
	QString text;
	float64 width = 0.;
	bool fiat = false;
	Motion v;
};

struct LabelPart {
	TextWithEntities source;
	Ui::Text::String text;
	float64 lead = 0.;
	float64 width = 0.;
	Motion v;
};

struct RowGeometry {
	float64 k = 1.;
	float64 left = 0.;
	float64 top = 0.;
	float64 additions = 0.;
	float64 flow = 0.;
	float64 composition = 0.;
	float64 tickerSkip = 0.;
};

struct AmountLayout {
	std::vector<float64> caret;
	std::vector<float64> right;
	int separatorAt = -1;
};

class GlyphFlow final {
public:
	GlyphFlow(const style::font &big, const style::font &small);

	void reset(std::vector<GlyphTarget> targets);
	void edit(
		std::vector<GlyphTarget> targets,
		EditKind kind,
		crl::time now);
	void roll(
		std::vector<GlyphTarget> targets,
		GlyphMode mode,
		bool growing,
		crl::time now);
	void prune(crl::time now);
	void finish();

	[[nodiscard]] bool animating(crl::time now) const;
	[[nodiscard]] float64 width(crl::time now) const;
	[[nodiscard]] float64 caretX(int position, crl::time now) const;

	void paint(
		QPainter &p,
		QPointF origin,
		float64 baseline,
		const QColor &color,
		crl::time now,
		FlowGap gap = FlowGap()) const;

private:
	struct Shape {
		QPainterPath path;
		QRectF ink;
	};

	void apply(
		std::vector<GlyphTarget> targets,
		const std::vector<int> &match,
		const GlyphChange &change,
		crl::time now);
	void applySeparators(
		std::vector<float64> xs,
		std::vector<uint32> killed,
		std::vector<uint32> born,
		const GlyphChange &change,
		crl::time now);
	void changeGlyph(
		Slot &slot,
		const GlyphTarget &target,
		const GlyphChange &change,
		crl::time duration,
		crl::time now) const;
	void paintLayer(
		QPainter &p,
		const Slot &slot,
		const GlyphLayer &layer,
		QPointF centre,
		float64 presence,
		const QColor &color,
		crl::time now) const;

	[[nodiscard]] Slot createSlot(
		const GlyphTarget &target,
		const GlyphChange &change,
		crl::time duration,
		crl::time now);
	[[nodiscard]] Slot settledSlot(const GlyphTarget &target);
	[[nodiscard]] Separator settledSeparator(const Slot &gap) const;
	[[nodiscard]] std::vector<float64> lefts(crl::time now) const;
	[[nodiscard]] std::vector<float64> restLefts() const;
	[[nodiscard]] int slotIndex(uint32 id) const;
	[[nodiscard]] int gapIndex(int position) const;
	[[nodiscard]] float64 separatorX(
		const Separator &separator,
		const std::vector<float64> &lefts,
		crl::time now) const;
	[[nodiscard]] float64 dip(
		const Separator &separator,
		float64 x,
		const std::vector<float64> &lefts,
		crl::time now) const;
	[[nodiscard]] std::vector<InkRange> visibleInks(
		const std::vector<float64> &lefts,
		crl::time now) const;
	[[nodiscard]] std::vector<InkRange> restInks(
		const std::vector<float64> &lefts) const;
	[[nodiscard]] Shape shape(bool small, QChar ch) const;

	const not_null<const style::font*> _big;
	const not_null<const style::font*> _small;
	const QRectF _digitsInk;
	std::vector<Slot> _slots;
	std::vector<Separator> _separators;
	std::vector<GlyphTarget> _targets;
	mutable base::flat_map<std::pair<bool, QChar>, Shape> _shapes;
	uint32 _autoincrement = 0;

};

class AmountRow final : public Ui::RpWidget {
public:
	AmountRow(QWidget *parent, AmountFieldArgs &args);

	[[nodiscard]] not_null<Ui::TonAmountInput*> field() const;
	[[nodiscard]] AmountDiamond takeDiamond();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	void refreshContent();
	void refreshLayers(bool switching, crl::time now);
	void refreshGeometry();
	void applyChange(
		const QString &text,
		const QString &separator,
		const QString &whole,
		const QString &fraction,
		bool switching);
	void startAnimation();
	void finishContent();
	void finishAnimation();
	void playDiamond();
	void restartBlink();
	void paintAddition(QPainter &p, float64 allotted, crl::time now) const;
	void paintTickers(QPainter &p, float64 left, crl::time now) const;
	void paintComposition(QPainter &p, float64 left, float64 baseline) const;
	void paintAnimated(QPainter &p, crl::time now) const;
	void select(int anchor, int position);

	[[nodiscard]] bool animationCallback(crl::time now);
	[[nodiscard]] bool contentAnimating(crl::time now) const;
	[[nodiscard]] bool flowPainted() const;
	[[nodiscard]] RowGeometry animatedGeometry(crl::time now) const;
	[[nodiscard]] QRect additionRect() const;
	[[nodiscard]] std::optional<QRectF> diamondCanvas(crl::time now) const;
	[[nodiscard]] QRect caretRect() const;
	[[nodiscard]] QRect caretRect(
		float64 k,
		float64 left,
		float64 top,
		float64 x) const;
	[[nodiscard]] QRect flowCaretRect(
		const RowGeometry &geometry,
		crl::time now) const;
	[[nodiscard]] int caretPosition() const;
	[[nodiscard]] bool caretInFraction() const;
	[[nodiscard]] const style::font &compositionFont() const;
	[[nodiscard]] std::array<QRectF, 2> selectionRects() const;
	[[nodiscard]] int positionAt(int x) const;
	[[nodiscard]] bool inDigitsZone(int x) const;
	[[nodiscard]] int selectionAnchor() const;

	const not_null<Ui::TonAmountInput*> _field;
	const Fn<QString()> _separator;
	const int _diamondCanvas = 0;
	const int _figureBig = 0;
	const int _figureSmall = 0;
	std::unique_ptr<Lottie::Icon> _diamond;
	GlyphFlow _flow;
	std::vector<Layer> _additions;
	std::vector<Layer> _tickers;
	Ui::Animations::Basic _animation;
	AmountPainter _painter;
	AmountLayout _layout;
	QString _shownText;
	QString _shownSeparator;
	QString _currency;
	QString _symbol;
	QString _ticker;
	base::Timer _blink;
	float64 _left = 0.;
	float64 _top = 0.;
	int _anchor = -1;
	bool _fiat = false;
	bool _caretShown = false;
	bool _selecting = false;
	bool _switchPending = false;
	bool _initialized = false;

};

class EquivalentLabel final : public Ui::RpWidget {
public:
	EquivalentLabel(QWidget *parent, Ui::Text::MarkedContext context);

	void setLabel(AmountLabel label);
	[[nodiscard]] rpl::producer<int> labelWidthValue() const;
	[[nodiscard]] QString plainText() const;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void crossFade(
		std::vector<LabelPart> &parts,
		const TextWithEntities &source,
		crl::time duration,
		crl::time now);
	void finishContent();
	void refreshWidth(crl::time now);
	void paintAnimated(QPainter &p, crl::time now) const;
	float64 paintParts(
		QPainter &p,
		std::span<const LabelPart> parts,
		float64 left,
		crl::time now) const;

	[[nodiscard]] bool animationCallback(crl::time now);
	[[nodiscard]] bool animating(crl::time now) const;
	[[nodiscard]] LabelPart createPart(const TextWithEntities &source) const;
	[[nodiscard]] float64 restWidth() const;
	[[nodiscard]] float64 animatedWidth(crl::time now) const;

	Ui::Text::MarkedContext _context;
	Ui::Text::String _static;
	GlyphFlow _flow;
	std::vector<LabelPart> _prefixes;
	std::vector<LabelPart> _suffixes;
	LabelPart _arrows;
	std::vector<GlyphTarget> _targets;
	AmountLabel _label;
	Motion _extra;
	Ui::Animations::Basic _animation;
	rpl::variable<int> _width = 0;
	bool _initialized = false;

};

class SwapPill final : public Ui::RoundButton {
public:
	using RoundButton::RoundButton;

	void setAccessibleText(QString text);
	QString accessibilityName() override;

private:
	QString _accessibleText;

};

[[nodiscard]] float64 Eased(Ease ease, float64 t);

[[nodiscard]] int AmountBand();

[[nodiscard]] int FigureHeight(const style::font &font);

[[nodiscard]] int DiamondPart(int canvas, float64 part);

[[nodiscard]] int CountDigits(const QString &text);

[[nodiscard]] QString GroupWhole(
		const QString &digits,
		bool fiat,
		const QString &currency);

[[nodiscard]] AmountLayout ComputeAmountLayout(
		const QString &whole,
		const QString &fraction,
		const style::font &big,
		const style::font &small,
		int wholeLeft,
		int fractionLeft);

[[nodiscard]] std::vector<float64> GlyphLefts(
		const style::font &font,
		const QString &text);

[[nodiscard]] std::vector<GlyphTarget> AmountTargets(
		const QString &whole,
		const QString &fraction,
		const style::font &big,
		const style::font &small);

[[nodiscard]] std::vector<GlyphTarget> LabelTargets(
		const AmountLabel &label);

[[nodiscard]] float64 AmountValue(
		const QString &text,
		const QString &decimal);

[[nodiscard]] EditKind ClassifyEdit(
		const QString &was,
		const QString &now,
		int cursor,
		const QString &separator);

[[nodiscard]] QString WholeText(const std::vector<GlyphTarget> &targets);

[[nodiscard]] std::vector<int> IndicesOf(
		const std::vector<GlyphTarget> &targets,
		bool groups);

[[nodiscard]] std::optional<std::vector<int>> EditMatch(
		const std::vector<GlyphTarget> &was,
		const std::vector<GlyphTarget> &now,
		EditKind kind);

[[nodiscard]] std::vector<int> PlaceMatch(
		const std::vector<GlyphTarget> &was,
		const std::vector<GlyphTarget> &now);

[[nodiscard]] float64 Reach(
		InkRange range,
		const std::vector<InkRange> &inks);

[[nodiscard]] const style::color &LayerColor(const Layer &layer);

[[nodiscard]] float64 TargetWidth(const std::vector<Layer> &layers);

void ChangeLayers(
		std::vector<Layer> &layers,
		std::optional<Layer> target,
		bool animated,
		crl::time now);

}

not_null<Ui::TonAmountInput*> AddAmountField(
		not_null<Ui::VerticalLayout*> container,
		int topSkip,
		AmountFieldArgs &&args);

AmountDiamond TakeAmountDiamond(not_null<Ui::TonAmountInput*> field);

}
