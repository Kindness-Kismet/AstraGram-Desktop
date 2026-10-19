/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_view_highlight_manager.h"

#include "data/data_session.h"
#include "extras/extras_settings.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "ui/chat/chat_style.h"

namespace HistoryView {

ElementHighlighter::ElementHighlighter(
	not_null<Data::Session*> data,
	ViewForItem viewForItem,
	RepaintView repaintView)
: _data(data)
, _viewForItem(std::move(viewForItem))
, _repaintView(std::move(repaintView))
, _animation(*this) {
}

void ElementHighlighter::enqueue(const SelectedQuote &quote) {
	const auto data = computeHighlight(quote);
	if (_queue.empty() && !_animation.animating()) {
		highlight(data);
	} else if (_highlighted != data && !base::contains(_queue, data)) {
		_queue.push_back(data);
		checkNextHighlight();
	}
}

void ElementHighlighter::highlight(const SelectedQuote &quote) {
	highlight(computeHighlight(quote));
}

void ElementHighlighter::checkNextHighlight() {
	if (_animation.animating()) {
		return;
	}
	const auto next = [&] {
		while (!_queue.empty()) {
			const auto highlight = _queue.front();
			_queue.pop_front();
			if (const auto item = _data->message(highlight.itemId)) {
				if (_viewForItem(item)) {
					return highlight;
				}
			}
		}
		return Highlight();
	}();
	if (next) {
		highlight(next);
	}
}

Ui::ChatPaintHighlight ElementHighlighter::state(
		not_null<const HistoryItem*> item) const {
	if (item->fullId() == _highlighted.itemId) {
		auto result = _animation.state();
		result.range = _highlighted.part;
		result.todoItemId = _highlighted.todoListId;
		result.pollOption = _highlighted.pollOption;
		return result;
	}
	return {};
}

ElementHighlighter::Highlight ElementHighlighter::computeHighlight(
		const SelectedQuote &quote) {
	Assert(quote.item != nullptr);

	const auto item = not_null(quote.item);
	const auto owner = &item->history()->owner();
	if (const auto group = owner->groups().find(item)) {
		const auto leader = group->items.front();
		const auto leaderId = leader->fullId();
		const auto i = ranges::find(group->items, item);
		if (i != end(group->items)) {
			const auto index = int(i - begin(group->items));
			if (quote.highlight.empty()) {
				return { leaderId, AddGroupItemSelection({}, index) };
			} else if (const auto leaderView = _viewForItem(leader)) {
				return {
					leaderId,
					leaderView->selectionFromQuote(quote),
					quote.highlight.todoItemId,
					quote.highlight.pollOption,
				};
			}
		}
		return {
			leaderId,
			{},
			quote.highlight.todoItemId,
			quote.highlight.pollOption,
		};
	} else if (quote.highlight.quote.empty()) {
		return {
			item->fullId(),
			{},
			quote.highlight.todoItemId,
			quote.highlight.pollOption,
		};
	} else if (const auto view = _viewForItem(item)) {
		return {
			item->fullId(),
			view->selectionFromQuote(quote),
			quote.highlight.todoItemId,
			quote.highlight.pollOption,
		};
	}
	return {
		item->fullId(),
		{},
		quote.highlight.todoItemId,
		quote.highlight.pollOption,
	};
}

void ElementHighlighter::highlight(Highlight data) {
	if (const auto item = _data->message(data.itemId)) {
		if (const auto view = _viewForItem(item)) {
			if (_highlighted && _highlighted.itemId != data.itemId) {
				if (const auto was = _data->message(_highlighted.itemId)) {
					if (const auto view = _viewForItem(was)) {
						repaintHighlightedItem(view);
					}
				}
			}
			_highlighted = data;
			_animation.start((!data.part.empty()
					|| data.todoListId
					|| !data.pollOption.isEmpty())
				&& !IsSubGroupSelection(data.part));

			repaintHighlightedItem(view);
		}
	}
}

void ElementHighlighter::repaintHighlightedItem(
		not_null<const Element*> view) {
	if (view->isHiddenByGroup()) {
		if (const auto group = _data->groups().find(view->data())) {
			if (const auto leader = _viewForItem(group->items.front())) {
				if (!leader->isHiddenByGroup()) {
					_repaintView(leader);
					return;
				}
			}
		}
	}
	_repaintView(view);
}

void ElementHighlighter::updateMessage() {
	if (const auto item = _data->message(_highlighted.itemId)) {
		if (const auto view = _viewForItem(item)) {
			repaintHighlightedItem(view);
		}
	}
}

void ElementHighlighter::clear() {
	_animation.cancel();
	_highlighted = {};
	_lastHighlightedMessageId = FullMsgId();
	_queue.clear();
}

ElementHighlighter::AnimationManager::AnimationManager(
	ElementHighlighter &parent)
: _parent(parent) {
}

bool ElementHighlighter::AnimationManager::animating() const {
	return (_timer && _timer->isActive()) || _simple.animating();
}

Ui::ChatPaintHighlight ElementHighlighter::AnimationManager::state() const {
	const auto progress = _timer ? .5 : _simple.value(1.);
	return {
		.opacity = _timer ? 1. : std::clamp(
			std::min(progress / .12, (1. - progress) / .3), 0., 1.),
		.collapsion = _withTextPart
			? std::clamp((progress - .18) / .22, 0., 1.)
			: 0.,
		.progress = progress,
	};
}

MsgId ElementHighlighter::latestSingleHighlightedMsgId() const {
	return _highlighted.itemId
		? _highlighted.itemId.msg
		: _lastHighlightedMessageId.msg;
}

void ElementHighlighter::AnimationManager::start(bool withTextPart) {
	cancel();
	_withTextPart = withTextPart;
	const auto finish = [=] {
		_parent.updateMessage();
		cancel();
		_parent._lastHighlightedMessageId = base::take(
			_parent._highlighted.itemId);
		_parent.checkNextHighlight();
	};
	const auto duration = ExtrasSettings::getInstance().messageLocateDuration();
	if (anim::Disabled()) {
		_timer.emplace(finish);
		_timer->callOnce(duration);
		_parent.updateMessage();
		return;
	}
	_simple.start([=](float64 progress) {
		if (progress == 1.) {
			finish();
			return;
		}
		_parent.updateMessage();
	}, 0., 1., duration);
}

void ElementHighlighter::AnimationManager::cancel() {
	_simple.stop();
	_timer.reset();
}

} // namespace HistoryView
