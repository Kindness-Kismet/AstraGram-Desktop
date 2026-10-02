/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/ui/chat_search_empty.h"

#include "base/object_ptr.h"
#include "extras/ui/components/empty_state_icon.h"
#include "ui/widgets/labels.h"
#include "styles/style_dialogs.h"

namespace Dialogs {

SearchEmpty::SearchEmpty(
	QWidget *parent,
	Icon icon,
	rpl::producer<TextWithEntities> text)
: RpWidget(parent) {
	setup(icon, std::move(text));
}

void SearchEmpty::setMinimalHeight(int minimalHeight) {
	const auto minimal = st::recentPeersEmptyHeightMin;
	resize(width(), std::max(minimalHeight, minimal));
}

void SearchEmpty::setup(Icon icon, rpl::producer<TextWithEntities> text) {
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		this,
		std::move(text),
		st::defaultPeerListAbout);
	const auto size = st::recentPeersEmptySize;
	const auto widget = ExtrasUi::createEmptyStateIcon(
		this,
		icon == Icon::Search
			? ExtrasUi::EmptyStateIcon::Search
			: ExtrasUi::EmptyStateIcon::NoResults,
		{ size, size },
		st::recentPeersEmptyMargin);
	const auto graphic = widget.data();

	sizeValue() | rpl::on_next([=](QSize size) {
		const auto padding = st::recentPeersEmptyMargin;
		const auto paddings = padding.left() + padding.right();
		label->resizeToWidth(size.width() - paddings);
		const auto x = (size.width() - graphic->width()) / 2;
		const auto y = (size.height() - graphic->height()) / 3;
		const auto top = y + graphic->height() + st::recentPeersEmptySkip;
		const auto sub = std::max(top + label->height() - size.height(), 0);
		graphic->move(x, y - sub);
		label->move((size.width() - label->width()) / 2, top - sub);
	}, lifetime());

	label->setClickHandlerFilter([=](
			const ClickHandlerPtr &handler,
			Qt::MouseButton) {
		_handlerActivated.fire_copy(handler);
		return false;
	});
}

} // namespace Dialogs
