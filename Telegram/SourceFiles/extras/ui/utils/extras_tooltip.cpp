#include "extras/ui/utils/extras_tooltip.h"

#include "ui/rp_widget.h"
#include "ui/widgets/tooltip.h"

namespace ExtrasUi {
namespace {

class TooltipShower final : public Ui::AbstractTooltipShower {
public:
	TooltipShower(
		not_null<Ui::RpWidget*> widget,
		Fn<QString()> text,
		const style::Tooltip &st)
	: _widget(widget)
	, _text(std::move(text))
	, _st(st) {
	}

	QString tooltipText() const override {
		return _text();
	}
	QPoint tooltipPos() const override {
		return QCursor::pos();
	}
	bool tooltipWindowActive() const override {
		return _widget->window()->isActiveWindow();
	}
	const style::Tooltip *tooltipSt() const override {
		return &_st;
	}

private:
	const not_null<Ui::RpWidget*> _widget;
	const Fn<QString()> _text;
	const style::Tooltip &_st;

};

} // namespace

void installTooltip(
		not_null<Ui::RpWidget*> widget,
		Fn<QString()> text,
		const style::Tooltip &st) {
	const auto shower = widget->lifetime().make_state<TooltipShower>(
		widget,
		std::move(text),
		st);
	widget->events() | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Enter) {
			Ui::Tooltip::Show(1000, shower);
		} else if (e->type() == QEvent::Leave) {
			Ui::Tooltip::Hide();
		}
	}, widget->lifetime());
}

} // namespace ExtrasUi
