#include "extras/ui/components/empty_state_icon.h"

#include "ui/painter.h"
#include "styles/style_extras_icons.h"
#include "styles/style_widgets.h"

namespace ExtrasUi {
namespace {

const style::icon &emptyStateGraphic(EmptyStateIcon icon) {
	switch (icon) {
	case EmptyStateIcon::Search: return st::extrasEmptyStateSearch;
	case EmptyStateIcon::NoResults: return st::extrasEmptyStateNoResults;
	case EmptyStateIcon::Chats: return st::extrasEmptyStateChats;
	case EmptyStateIcon::Blocked: return st::extrasEmptyStateBlocked;
	case EmptyStateIcon::Gifts: return st::extrasEmptyStateGifts;
	}
	Unexpected("Icon in emptyStateGraphic.");
}

} // namespace

void paintEmptyStateIcon(
		QPainter &p,
		QRect bounds,
		EmptyStateIcon icon,
		QColor color) {
	emptyStateGraphic(icon).paintInCenter(p, bounds, color);
}

object_ptr<Ui::RpWidget> createEmptyStateIcon(
		not_null<QWidget*> parent,
		EmptyStateIcon icon,
		QSize size,
		QMargins padding) {
	auto result = object_ptr<Ui::RpWidget>(parent);
	const auto raw = result.data();
	raw->setObjectName(u"emptyStateIcon"_q);
	raw->setAttribute(Qt::WA_TransparentForMouseEvents);
	raw->resize(QRect(QPoint(), size).marginsAdded(padding).size());
	raw->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(raw);
		paintEmptyStateIcon(
			p,
			QRect((raw->width() - size.width()) / 2, padding.top(), size.width(), size.height()),
			icon,
			st::windowSubTextFg->c);
	}, raw->lifetime());
	return result;
}

} // namespace ExtrasUi
