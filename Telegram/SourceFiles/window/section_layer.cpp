#include "window/section_layer.h"

#include "ui/rp_widget.h"

#include <QtGui/QPaintEvent>

namespace Window {
namespace {

class SectionLayer final : public Ui::RpWidget {
public:
	SectionLayer(QWidget *section, Fn<void(QPaintEvent*)> paint)
	: Ui::RpWidget(section)
	, _paint(std::move(paint)) {
		Expects(bool(_paint));
		setAttribute(Qt::WA_NoSystemBackground);
		setVisualTabOrder(true);
		const auto owner = static_cast<Ui::RpWidget*>(section);
		owner->sizeValue() | rpl::on_next([=](QSize size) {
			setGeometry(QRect(QPoint(), size));
		}, lifetime());
	}

private:
	void paintEvent(QPaintEvent *e) override {
		_paint(e);
	}

	Fn<void(QPaintEvent*)> _paint;
};

} // namespace

object_ptr<Ui::RpWidget> CreateSectionLayer(
		not_null<Ui::RpWidget*> section,
		Fn<void(QPaintEvent*)> paint) {
	return object_ptr<SectionLayer>(section.get(), std::move(paint));
}

} // namespace Window
