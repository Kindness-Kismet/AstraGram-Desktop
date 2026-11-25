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
	_minimalHeight = std::max(minimalHeight, minimal);
	resize(width(), fullHeight());
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
	_label = label;
	_graphic = graphic;

	rpl::combine(
		sizeValue(),
		label->sizeValue()
	) | rpl::on_next([=](QSize size, QSize labelSize) {
		const auto x = (size.width() - graphic->width()) / 2;
		const auto y = (size.height() - graphic->height()) / 3;
		const auto top = y + graphic->height() + st::recentPeersEmptySkip;
		const auto bottom = top + labelSize.height();
		const auto sub = std::max(bottom - size.height(), 0);
		graphic->move(x, y - sub);
		label->move((size.width() - labelSize.width()) / 2, top - sub);
	}, lifetime());

	label->heightValue() | rpl::skip(1) | rpl::filter([=] {
		return !_measuring;
	}) | rpl::on_next([=] {
		resize(width(), fullHeight());
	}, lifetime());

	label->setClickHandlerFilter([=](
			const ClickHandlerPtr &handler,
			Qt::MouseButton) {
		_handlerActivated.fire_copy(handler);
		return false;
	});
}

int SearchEmpty::resizeGetHeight(int newWidth) {
	const auto padding = st::recentPeersEmptyMargin;
	_measuring = true;
	_label->resizeToWidth(newWidth - padding.left() - padding.right());
	_measuring = false;
	return fullHeight();
}

int SearchEmpty::fullHeight() const {
	const auto content = _graphic->height()
		+ st::recentPeersEmptySkip
		+ _label->height();
	return std::max(_minimalHeight, content);
}

} // namespace Dialogs
