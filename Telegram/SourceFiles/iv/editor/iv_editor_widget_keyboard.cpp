/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_widget.h"

#include "base/event_filter.h"
#include "base/qthelp_url.h"
#include "base/qt/qt_common_adapters.h"
#include "base/random.h"
#include "base/weak_qptr.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/rich_paste_toast.h"
#include "core/mime_type.h"
#include "data/data_msg_id.h"
#include "data/data_types.h"
#include "editor/editor_layer_widget.h"
#include "editor/photo_editor.h"
#include "editor/photo_editor_common.h"
#include "iv/editor/iv_editor_article_style.h"
#include "iv/editor/iv_editor_auto_pair.h"
#include "iv/editor/iv_editor_clipboard_import.h"
#include "iv/editor/iv_editor_commands.h"
#include "iv/editor/iv_editor_math_box.h"
#include "iv/editor/iv_editor_page_media.h"
#include "iv/editor/iv_editor_page_path.h"
#include "iv/editor/iv_editor_page_table_grid.h"
#include "iv/editor/iv_editor_prepared_selection.h"
#include "iv/editor/iv_editor_session.h"
#include "iv/editor/iv_editor_structure_menu.h"
#include "iv/editor/iv_editor_text_entities.h"
#include "iv/editor/iv_editor_window.h"
#include "iv/markdown/iv_markdown_article_paint.h"
#include "iv/markdown/iv_markdown_article_selection.h"
#include "iv/markdown/iv_markdown_article_text.h"
#include "iv/markdown/iv_markdown_microtex.h"
#include "iv/markdown/iv_markdown_prepare_links.h"
#include "iv/markdown/iv_markdown_prepare_native_richtext.h"
#include "iv/markdown/iv_markdown_prepare_serialize.h"
#include "iv/markdown/iv_markdown_slideshow_chrome.h"
#include "iv/markdown/iv_markdown_theme.h"
#include "iv/iv_search_bar.h"
#include "iv/iv_search_controller.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "menu/menu_checked_action.h"
#include "platform/platform_file_utilities.h"
#include "spellcheck/spellcheck_highlight_syntax.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/click_handler.h"
#include "ui/delayed_activation.h"
#include "ui/image/image.h"
#include "ui/image/image_location.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_html_tags.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/menu/menu_separator.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"

#include "styles/palette.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_iv.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QEvent>
#include <QtCore/QMimeData>
#include <QtCore/QPointer>
#include <QtGui/QClipboard>
#include <QtGui/qevent.h>
#include <QtGui/QCursor>
#include <QtGui/QTextBlock>
#include <QtGui/QTextLayout>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QTextEdit>
#include <QShortcut>
#include <QAction>

#include <algorithm>
#include <array>
#include <cmath>
#include "iv/editor/iv_editor_widget_internal.h"

namespace Iv::Editor {
using namespace WidgetDetails;

int Widget::textEditableSegmentIndex(int ordinal) const {
	if (!_article) {
		return -1;
	}
	const auto &nodes = _state->textNodes();
	if (ordinal < 0 || ordinal >= int(nodes.size())) {
		return -1;
	}
	const auto segmentIndex = segmentIndexForEditableOrdinal(ordinal);
	return (segmentIndex >= 0)
		&& _article->segmentIsText(segmentIndex)
		&& _article->segmentIsEditable(segmentIndex)
		&& (editableOrdinalForSegment(segmentIndex) == ordinal)
		? segmentIndex
		: -1;
}

std::optional<int> Widget::adjacentTextEditableOrdinal(bool down) const {
	const auto &nodes = _state->textNodes();
	const auto count = int(nodes.size());
	if (_activeOrdinal < 0 || _activeOrdinal >= count) {
		return std::nullopt;
	}
	for (auto ordinal = _activeOrdinal + (down ? 1 : -1);
		ordinal >= 0 && ordinal < count;
		ordinal += down ? 1 : -1) {
		if (textEditableSegmentIndex(ordinal) >= 0) {
			return ordinal;
		}
	}
	return std::nullopt;
}

std::optional<int> Widget::textEditableOrdinalFromSegment(
		int segmentIndex,
		bool down) const {
	if (segmentIndex < 0) {
		return std::nullopt;
	}
	const auto &nodes = _state->textNodes();
	const auto count = int(nodes.size());
	if (down) {
		for (auto ordinal = 0; ordinal != count; ++ordinal) {
			const auto candidateSegmentIndex = textEditableSegmentIndex(ordinal);
			if (candidateSegmentIndex >= segmentIndex) {
				return ordinal;
			}
		}
	} else {
		for (auto ordinal = count - 1; ordinal >= 0; --ordinal) {
			const auto candidateSegmentIndex = textEditableSegmentIndex(ordinal);
			if (candidateSegmentIndex >= 0
				&& candidateSegmentIndex <= segmentIndex) {
				return ordinal;
			}
		}
	}
	return std::nullopt;
}

std::optional<Widget::VerticalNavigationTarget> Widget::adjacentRowTarget(
		int ordinal,
		QPoint articlePoint,
		bool down) {
	if (!_article) {
		return std::nullopt;
	}
	const auto segmentIndex = textEditableSegmentIndex(ordinal);
	if (segmentIndex < 0) {
		return std::nullopt;
	}
	const auto segmentRect = _article->segmentRect(segmentIndex);
	if (!segmentRect.isValid() || segmentRect.isEmpty()) {
		return std::nullopt;
	}
	const auto textRect = _article->textSegmentRect(segmentIndex);
	const auto bounds = (textRect.isValid() && !textRect.isEmpty())
		? textRect
		: segmentRect;
	const auto clampedY = down
		? std::max(articlePoint.y(), bounds.top())
		: std::min(articlePoint.y(), bounds.bottom());
	articlePoint.setY(std::clamp(clampedY, bounds.top(), bounds.bottom()));
	articlePoint.setX(std::clamp(
		articlePoint.x(),
		bounds.left(),
		bounds.right()));
	syncArticleVisibleTopBottom();
	const auto hit = _article->hitTest(
		articlePoint,
		Ui::Text::StateRequest::Flag::LookupSymbol);
	if (!hit.valid()
		|| !_article->segmentIsText(hit.segmentIndex)
		|| !_article->segmentIsEditable(hit.segmentIndex)
		|| (editableOrdinalForSegment(hit.segmentIndex) != ordinal)) {
		return std::nullopt;
	}
	return VerticalNavigationTarget{
		.ordinal = ordinal,
		.offset = _article->selectionOffsetFromHit(
			hit,
			TextSelectType::Letters),
	};
}

std::optional<Widget::VerticalNavigationTarget> Widget::pageNavigationTarget(
		bool down) {
	if (_field->isHidden()
		|| !_article
		|| (_activeOrdinal < 0)
		|| (_activeSegmentIndex < 0)) {
		return std::nullopt;
	}
	const auto activeSegmentIndex = textEditableSegmentIndex(_activeOrdinal);
	if (activeSegmentIndex < 0 || activeSegmentIndex != _activeSegmentIndex) {
		return std::nullopt;
	}
	const auto pageHeight = _visibleRange.bottom - _visibleRange.top;
	if (pageHeight <= 0) {
		return std::nullopt;
	}
	const auto articlePoint = activeFieldCursorArticlePoint();
	if (!articlePoint) {
		return std::nullopt;
	}
	const auto shiftedPoint = *articlePoint
		+ QPoint(0, down ? pageHeight : -pageHeight);
	syncArticleVisibleTopBottom();
	const auto hit = _article->hitTest(
		shiftedPoint,
		Ui::Text::StateRequest::Flag::LookupSymbol);
	if (!hit.valid()) {
		return std::nullopt;
	}
	if (_article->segmentIsText(hit.segmentIndex)
		&& _article->segmentIsEditable(hit.segmentIndex)) {
		const auto ordinal = editableOrdinalForSegment(hit.segmentIndex);
		if (ordinal < 0
			|| ordinal == _activeOrdinal
			|| (segmentIndexForEditableOrdinal(ordinal) != hit.segmentIndex)) {
			return std::nullopt;
		}
		return VerticalNavigationTarget{
			.ordinal = ordinal,
			.offset = _article->selectionOffsetFromHit(
				hit,
				TextSelectType::Letters),
		};
	}
	const auto ordinal = textEditableOrdinalFromSegment(hit.segmentIndex, down);
	return (ordinal && *ordinal != _activeOrdinal)
		? adjacentRowTarget(*ordinal, shiftedPoint, down)
		: std::nullopt;
}

std::optional<Widget::BoundarySelectionOrigin>
Widget::currentBoundarySelectionOrigin(bool forward) const {
	if (_field->isHidden()
		|| !_article
		|| (_activeOrdinal < 0)
		|| (_state->activeFieldMode() != State::FieldMode::Rich)) {
		return std::nullopt;
	}
	const auto viewState = captureHistoryViewState();
	if (!viewState.leafSelection) {
		return std::nullopt;
	}
	const auto activeLeaf = _state->activePreparedLeafSource();
	auto hit = PreparedEditHit();
	if (const auto articlePoint = activeFieldCursorArticlePoint()) {
		const auto current = _article->editHitTest(*articlePoint);
		if (current.valid()
			&& activeLeaf
			&& current.leaf
			&& (*current.leaf == *activeLeaf)
			&& StructuralOwnerFromHit(current).valid()) {
			hit = current;
		}
	}
	if (!hit.valid()) {
		const auto segmentIndex = textEditableSegmentIndex(_activeOrdinal);
		if (segmentIndex >= 0) {
			const auto rect = _article->segmentRect(segmentIndex);
			if (rect.isValid() && !rect.isEmpty()) {
				hit = _article->editHitTest(rect.center());
			}
		}
	}
	if (!StructuralOwnerFromHit(hit).valid()) {
		return std::nullopt;
	}
	return BoundarySelectionOrigin{
		.leafSelection = *viewState.leafSelection,
		.anchorHit = hit,
		.forward = forward,
	};
}

std::optional<PreparedEditHit> Widget::boundaryHitFromTarget(
		const State::BoundaryTarget &target) const {
	switch (target.action) {
	case State::BoundaryTarget::Action::Text: {
		if (!_article || target.textOrdinal < 0) {
			return std::nullopt;
		}
		const auto segmentIndex = segmentIndexForEditableOrdinal(
			target.textOrdinal);
		if (segmentIndex < 0
			|| (editableOrdinalForSegment(segmentIndex)
				!= target.textOrdinal)) {
			return std::nullopt;
		}
		const auto rect = _article->segmentRect(segmentIndex);
		if (!rect.isValid() || rect.isEmpty()) {
			return std::nullopt;
		}
		const auto hit = _article->editHitTest(rect.center());
		return StructuralOwnerFromHit(hit).valid()
			? std::make_optional(hit)
			: std::nullopt;
	}
	case State::BoundaryTarget::Action::StructuralSelection:
		switch (target.structuralSelection.kind) {
		case PreparedEditSelectionKind::Blocks: {
			if (target.structuralSelection.blocks.from + 1
				!= target.structuralSelection.blocks.till) {
				return std::nullopt;
			}
			const auto hit = PreparedEditHitFromBlockSelection(
				target.structuralSelection.blocks,
				false);
			return StructuralOwnerFromHit(hit).valid()
				? std::make_optional(hit)
				: std::nullopt;
		}
		case PreparedEditSelectionKind::ListItems: {
			if (target.structuralSelection.listItems.from + 1
				!= target.structuralSelection.listItems.till) {
				return std::nullopt;
			}
			const auto hit = PreparedEditHitFromListItemSelection(
				target.structuralSelection.listItems,
				false);
			return StructuralOwnerFromHit(hit).valid()
				? std::make_optional(hit)
				: std::nullopt;
		}
		case PreparedEditSelectionKind::None:
		case PreparedEditSelectionKind::TableRows:
		case PreparedEditSelectionKind::TableCells:
			return std::nullopt;
		}
		return std::nullopt;
	case State::BoundaryTarget::Action::None:
	case State::BoundaryTarget::Action::RemoveActiveOwner:
		return std::nullopt;
	}
	return std::nullopt;
}

bool Widget::enterStructuralSelectionFromField(bool forward, bool page) {
	const auto committedSelection = [&]() {
		if (_field->isHidden()
			|| (_state->activeFieldMode() != State::FieldMode::Rich)) {
			return std::optional<CommittedFieldSelectionCapture>();
		}
		const auto leaf = _state->activeLeafPath();
		if (!leaf) {
			return std::optional<CommittedFieldSelectionCapture>();
		}
		const auto text = ConvertEditorTagsToRichText(
			_field->getTextWithAppliedMarkdown());
		const auto cursor = _field->textCursor();
		const auto length = int(text.text.size());
		return std::make_optional(CommittedFieldSelectionCapture{
			.leaf = *leaf,
			.text = text,
			.anchorOffset = std::clamp(
				richOffsetForFieldOffset(text, cursor.anchor()),
				0,
				length),
			.cursorOffset = std::clamp(
				richOffsetForFieldOffset(text, cursor.position()),
				0,
				length),
		});
	}();
	const auto committed = commitInlineFieldForClose();
	if (committed == ApplyResult::Failed) {
		return false;
	}
	if (committed == ApplyResult::Changed) {
		const auto mapped = committedSelection
			? MapCommittedFieldSelectionAfterCommit(*_state, *committedSelection)
			: std::nullopt;
		if (!mapped) {
			return false;
		}
		activateTextOrdinal(
			mapped->ordinal,
			mapped->anchorOffset,
			mapped->cursorOffset,
			ActivateReveal::Skip);
		const auto activeLeaf = _state->activeLeafPath();
		if (_field->isHidden()
			|| !activeLeaf
			|| !_fieldLeaf
			|| (*_fieldLeaf != *activeLeaf)) {
			return false;
		}
	}
	const auto origin = currentBoundarySelectionOrigin(forward);
	if (!origin) {
		return false;
	}
	const auto owner = StructuralOwnerFromHit(origin->anchorHit);
	if (!owner.valid()) {
		return false;
	}
	const auto initialSelection = SelectionFromStructuralOwner(owner);
	if (initialSelection.empty()) {
		return false;
	}
	_selection = {};
	_selectionEndpoints = {};
	finishArticleSelection();
	_pendingOrdinal = -1;
	_pendingCursorOffset = 0;
	hideInlineField();
	clearInlineFieldEditSession();
	relayoutCurrentContent();
	setFocus();
	setStructuralSelection(initialSelection, origin);
	if (page) {
		adjustStructuralSelectionFromKeyboard(forward, true);
	}
	update();
	return true;
}

bool Widget::adjustStructuralSelectionFromKeyboard(bool forward, bool page) {
	if (!_article
		|| !_boundarySelectionOrigin
		|| _structuralSelection.empty()) {
		return false;
	}
	const auto origin = *_boundarySelectionOrigin;
	const auto originSelection = SelectionFromStructuralOwner(
		StructuralOwnerFromHit(origin.anchorHit));
	if (originSelection.empty()) {
		return false;
	}
	const auto edgeY = [&](const PreparedEditSelection &selection) {
		const auto ordinal = StructuralSelectionEdgeTextOrdinal(
			*_state,
			selection,
			origin.forward);
		if (!ordinal) {
			return std::optional<int>();
		}
		const auto segmentIndex = segmentIndexForEditableOrdinal(*ordinal);
		if (segmentIndex < 0
			|| (editableOrdinalForSegment(segmentIndex) != *ordinal)) {
			return std::optional<int>();
		}
		const auto rect = _article->segmentRect(segmentIndex);
		if (!rect.isValid() || rect.isEmpty()) {
			return std::optional<int>();
		}
		return std::make_optional(origin.forward ? rect.bottom() : rect.top());
	};
	const auto advance = [&]() {
		if (!_boundarySelectionOrigin || _structuralSelection.empty()) {
			return false;
		}
		if ((forward != origin.forward)
			&& (_structuralSelection == originSelection)) {
			return restoreFieldFromBoundaryOrigin();
		}
		const auto focusEdge = EdgeSelection(
			_structuralSelection,
			origin.forward);
		if (focusEdge.empty()) {
			return false;
		}
		const auto withinFocusEdge = [&](const PreparedEditHit &hit) {
			const auto owner = StructuralOwnerFromHit(hit);
			const auto selection = SelectionFromStructuralOwner(owner);
			if (selection.empty()) {
				return false;
			} else if (selection == focusEdge) {
				return true;
			}
			const auto block = BlockPathFromOwner(owner);
			if (!block) {
				return false;
			} else if (focusEdge.kind == PreparedEditSelectionKind::Blocks) {
				return PreparedPathInBlockRange(*block, focusEdge.blocks);
			} else if (focusEdge.kind == PreparedEditSelectionKind::ListItems) {
				return PreparedPathInListItemRange(
					*block,
					focusEdge.listItems);
			}
			return false;
		};
		const auto steps = _state->boundarySteps(true);
		auto firstFocus = -1;
		auto lastFocus = -1;
		for (auto i = 0, count = int(steps.size()); i != count; ++i) {
			const auto hit = boundaryHitFromTarget(steps[i]);
			if (!hit || !withinFocusEdge(*hit)) {
				continue;
			}
			if (firstFocus < 0) {
				firstFocus = i;
			}
			lastFocus = i;
		}
		if (firstFocus < 0 || lastFocus < firstFocus) {
			return false;
		}
		for (auto i = forward ? (lastFocus + 1) : (firstFocus - 1);
				i >= 0 && i < int(steps.size());
				i += forward ? 1 : -1) {
			const auto hit = boundaryHitFromTarget(steps[i]);
			if (!hit || withinFocusEdge(*hit)) {
				continue;
			}
			const auto selection = SelectionFromStructuralOwner(
				StructuralOwnerFromHit(*hit));
			if (selection.empty()) {
				continue;
			}
			if ((forward != origin.forward)
				&& (selection == originSelection)) {
				return restoreFieldFromBoundaryOrigin();
			}
			const auto next = structuralSelectionFromHits(
				origin.anchorHit,
				*hit);
			if (next.empty() || next == _structuralSelection) {
				continue;
			}
			setStructuralSelection(next, _boundarySelectionOrigin);
			revealStructuralSelectionEdge(origin.forward);
			update();
			return true;
		}
		return false;
	};
	const auto start = page ? edgeY(_structuralSelection) : std::optional<int>();
	if (!advance()) {
		return false;
	}
	if (!page || !_boundarySelectionOrigin || _structuralSelection.empty()) {
		return true;
	}
	const auto pageHeight = _visibleRange.bottom - _visibleRange.top;
	if (pageHeight <= 0 || !start) {
		return true;
	}
	while (_boundarySelectionOrigin && !_structuralSelection.empty()) {
		const auto current = edgeY(_structuralSelection);
		if (!current) {
			break;
		}
		if (std::abs(*current - *start) >= pageHeight) {
			break;
		}
		if (!advance()) {
			break;
		}
		if (!_boundarySelectionOrigin || _structuralSelection.empty()) {
			break;
		}
	}
	return true;
}

bool Widget::handleInsertSuggestionsKey(QKeyEvent *e) {
	if (_insertSuggestions->handleKeyPress(e)) {
		e->accept();
		return true;
	}
	const auto modifiers = e->modifiers()
		& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier | Qt::ShiftModifier);
	if ((e->text() != u"/"_q)
		|| (modifiers != Qt::NoModifier)
		|| _field->isHidden()
		|| (_fieldMode != State::FieldMode::Rich)
		|| !_state->isActiveTopLevelParagraph()
		|| !_field->getLastText().isEmpty()) {
		return false;
	}
	_insertSuggestions->open();
	return false;
}

void Widget::applyInsertSuggestion(InsertSuggestionCommand command) {
	if (!_insertSuggestions->active() || _field->isHidden()) {
		_insertSuggestions->close();
		return;
	}
	_insertSuggestions->takeQuery();
	_insertSuggestions->close();
	if (const auto action = InsertSuggestionBlock(command)) {
		insertBlock(*action);
		return;
	}
	using Command = InsertSuggestionCommand;
	switch (command) {
	case Command::Button:
		editButtonFromToolbar();
		return;
	case Command::Math:
		editMathFromToolbar();
		return;
	case Command::Media:
		requestMedia(std::nullopt, RequestMediaType::PhotoVideo);
		return;
	case Command::Audio:
		requestMedia(std::nullopt, RequestMediaType::Audio);
		return;
	case Command::Map:
		requestMapInsert();
		return;
	default:
		break;
	}
	Unexpected("Command in Widget::applyInsertSuggestion.");
}

void Widget::requestMapInsert() {
	if (!_requestMap) {
		return;
	}
	const auto outer = static_cast<Ui::RpWidget*>(_outer.get());
	Ui::PreventDelayedActivation();
	_requestMap(
		not_null<Widget*>(this),
		QPointer<QWidget>(_outer.get()),
		outer->death());
}

bool Widget::handleFieldInputRule(QKeyEvent *e) {
	const auto modifiers = e->modifiers()
		& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
	if ((e->key() != Qt::Key_Space)
		|| (modifiers != Qt::NoModifier)
		|| _field->isHidden()
		|| (_fieldMode != State::FieldMode::Rich)
		|| !_state->isActiveTopLevelParagraph()) {
		return false;
	}
	const auto cursor = _field->textCursor();
	const auto typed = _field->getLastText();
	if (cursor.hasSelection() || (cursor.position() != int(typed.size()))) {
		return false;
	}
	const auto rule = MatchInputRule(typed);
	if (!rule) {
		return false;
	}
	const auto leaf = _state->activeLeafPath();
	if (!leaf) {
		return false;
	}
	auto erase = _field->textCursor();
	erase.movePosition(QTextCursor::Start);
	erase.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
	erase.removeSelectedText();
	_field->setTextCursor(erase);
	const auto historyIndexBefore = _historyIndex;
	insertBlock(rule->action);
	if (_historyIndex == historyIndexBefore) {
		if (!_field->isHidden() && _field->getLastText().isEmpty()) {
			auto restore = _field->textCursor();
			restore.insertText(typed);
			_field->setTextCursor(restore);
		}
		return false;
	}
	_inputRuleUndo = InputRuleUndo{
		.historyIndex = _historyIndex,
		.leaf = *leaf,
		.text = typed + QChar(' '),
	};
	e->accept();
	return true;
}

bool Widget::undoLastInputRule() {
	if (!_inputRuleUndo
		|| (_inputRuleUndo->historyIndex != _historyIndex)
		|| _field->isHidden()
		|| !_field->getLastText().isEmpty()) {
		return false;
	}
	const auto undo = *base::take(_inputRuleUndo);
	performUndoRedo(false, false);
	const auto ordinal = _state->textOrdinalForLeafPath(undo.leaf);
	if (ordinal >= 0) {
		activateTextOrdinal(ordinal, 0);
	}
	if (_field->isHidden() || !_field->getLastText().isEmpty()) {
		return true;
	}
	auto cursor = _field->textCursor();
	cursor.insertText(undo.text);
	_field->setTextCursor(cursor);
	return true;
}

bool Widget::handleSubmitShortcut(QKeyEvent *e) {
	const auto type = e->type();
	if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress) {
		return false;
	}
	const auto key = e->key();
	if (key != Qt::Key_Return && key != Qt::Key_Enter) {
		return false;
	}
	const auto modifiers = e->modifiers()
		& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
	if (modifiers != Qt::ControlModifier
		|| !_submit
		|| searchBlockedByLayer()) {
		return false;
	}
	e->accept();
	if (type == QEvent::KeyPress && !e->isAutoRepeat()) {
		_submit();
	}
	return true;
}

bool Widget::handleFieldKey(QKeyEvent *e) {
	if (_field->isHidden()) {
		return false;
	}
	const auto key = e->key();
	if (key != Qt::Key_Backspace) {
		_inputRuleUndo = std::nullopt;
	}
	if (handleInsertSuggestionsKey(e)) {
		return true;
	}
	_insertSuggestions->scheduleRefresh();
	if (handleFieldInputRule(e)) {
		return true;
	}
	if (!hasStructuralSelection() && HandleAutoPairKey(_field.get(), e)) {
		e->accept();
		return true;
	}
	if (key == Qt::Key_Escape) {
		hideInlineFieldAndRefresh();
		e->accept();
		return true;
	}
	const auto modifiers = e->modifiers()
		& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
	const auto cursor = _field->textCursor();
	const auto vertical = (key == Qt::Key_Up)
		|| (key == Qt::Key_Down)
		|| (key == Qt::Key_PageUp)
		|| (key == Qt::Key_PageDown);
	const auto down = (key == Qt::Key_Down) || (key == Qt::Key_PageDown);
	const auto page = (key == Qt::Key_PageUp) || (key == Qt::Key_PageDown);
	const auto atStart = cursor.atStart();
	const auto atEnd = cursor.atEnd();
	auto handled = false;
	const auto applyFieldCursor = [&](QTextCursor next) {
		if ((next.position() == cursor.position())
			&& (next.anchor() == cursor.anchor())) {
			return false;
		}
		_field->setTextCursor(next);
		_field->setFocusFast();
		revealActiveInlineField();
		handled = true;
		return true;
	};
	const auto moveFieldCursor = [&](
			QTextCursor::MoveOperation operation,
			QTextCursor::MoveMode mode) {
		auto next = _field->textCursor();
		next.movePosition(operation, mode);
		return applyFieldCursor(next);
	};
	const auto activateVerticalTarget = [&](
			const VerticalNavigationTarget &target) {
		if (target.ordinal == _activeOrdinal) {
			setActiveFieldCursorOffset(target.offset);
		} else {
			commitAndActivateTextOrdinal(
				target.ordinal,
				target.offset,
				target.offset,
				ActivateReveal::Reveal);
		}
		handled = true;
	};
	const auto refreshPreparedContentAndActivate = [&](
			int ordinal,
			int cursorOffset) {
		beginInlineFieldRevealSuppression();
		{
			const auto revealGuard = gsl::finally([&] {
				endInlineFieldRevealSuppression();
			});
			refreshPreparedContent();
			activateTextOrdinal(ordinal, cursorOffset, ActivateReveal::Skip);
		}
		revealActiveInlineField();
	};
	if (vertical) {
		if (modifiers == Qt::NoModifier) {
			if (page) {
				if (const auto offset = activeFieldPageCursorOffset(down)) {
					auto next = cursor;
					next.setPosition(*offset);
					handled = applyFieldCursor(next);
				}
				if (!handled) {
					if (const auto target = pageNavigationTarget(down)) {
						activateVerticalTarget(*target);
					} else if (down) {
						handled = moveVerticalDownBoundary();
					} else {
						handled = moveBoundary(false, false)
							|| insertLeadingParagraphFromField(true);
					}
				}
			} else if (!fieldCursorLeavesVisibleRow(down)) {
				handled = moveFieldCursor(
					down ? QTextCursor::Down : QTextCursor::Up,
					QTextCursor::MoveAnchor);
			}
			if (!handled) {
				const auto articlePoint = activeFieldCursorArticlePoint();
				const auto activeLeaf = _state->activeLeafPath();
				const auto inTableCell = activeLeaf
					&& (activeLeaf->kind == StateLeafKind::TableCellText);
				const auto activateTableNavigationOrdinal = [&](int ordinal) {
					if (articlePoint) {
						if (const auto target = adjacentRowTarget(
								ordinal,
								*articlePoint,
								down)) {
							activateVerticalTarget(*target);
							return;
						}
					}
					const auto activated = commitAndActivateTextOrdinal(
						ordinal,
						0,
						0,
						ActivateReveal::Reveal);
					if (activated && !down) {
						setActiveFieldCursorOffset(_state->activeTextLength());
					}
					handled = true;
				};
				if (inTableCell) {
					if (const auto ordinal
						= _state->adjacentRowTableCellOrdinal(down)) {
						activateTableNavigationOrdinal(*ordinal);
					} else if (!down) {
						if (const auto ordinal
							= _state->tableTitleOrdinalFromActiveCell()) {
							activateTableNavigationOrdinal(*ordinal);
						}
					} else if (const auto ordinal
						= _state->ordinalAfterActiveTable()) {
						activateTableNavigationOrdinal(*ordinal);
					} else {
						activateTrailingParagraph();
						handled = true;
					}
				} else if (articlePoint) {
					if (const auto ordinal = adjacentTextEditableOrdinal(down)) {
						if (const auto target = adjacentRowTarget(
								*ordinal,
								*articlePoint,
								down)) {
							activateVerticalTarget(*target);
						}
					}
				}
				if (!handled && down) {
					if (const auto ordinal
						= _state->firstTableCellOrdinalFromActiveTitle()) {
						refreshPreparedContentAndActivate(*ordinal, 0);
						handled = true;
					}
				}
				if (!handled) {
					handled = down
						? moveVerticalDownBoundary()
						: (moveBoundary(false, false)
							|| insertLeadingParagraphFromField(true));
				}
			}
		} else if (modifiers == Qt::ShiftModifier) {
			if (page) {
				if (const auto offset = activeFieldPageCursorOffset(down)) {
					auto next = cursor;
					next.setPosition(*offset, QTextCursor::KeepAnchor);
					handled = applyFieldCursor(next);
				}
				if (!handled) {
					handled = enterStructuralSelectionFromField(down, true);
				}
			} else if (!fieldCursorLeavesVisibleRow(down)) {
				handled = moveFieldCursor(
					down ? QTextCursor::Down : QTextCursor::Up,
					QTextCursor::KeepAnchor);
			}
			if (!handled) {
				handled = enterStructuralSelectionFromField(down, false);
			}
		}
		if (!handled && !_field->isHidden() && modifiers == Qt::NoModifier) {
			handled = moveFieldCursor(
				down ? QTextCursor::End : QTextCursor::Start,
				QTextCursor::MoveAnchor);
		}
		if (handled) {
			e->accept();
		}
		return handled;
	}
	if (modifiers != Qt::NoModifier) {
		return false;
	}
	if (cursor.hasSelection()) {
		return false;
	}
	if (atStart
		&& key == Qt::Key_Left) {
		handled = moveBoundary(false, false);
	} else if (atEnd
		&& key == Qt::Key_Right) {
		handled = moveBoundary(true, true);
	} else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
		if (_fieldSuggestions && _fieldSuggestions->consumesEnter()) {
			return false;
		}
		recordMutationTransaction([&] {
			auto context = CommandContext{
				.state = _state.get(),
				.enter = MakeActiveEnterContext(activeTextInsertContext()),
				.caretAtStart = atStart,
			};
			const auto committed = commitInlineField();
			if (committed == ApplyResult::Failed) {
				handled = true;
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			} else if (RunEnterChain(context)) {
				refreshPreparedContentAndActivate(context.targetOrdinal, 0);
				handled = true;
				return MutationTransactionResult{
					.committed = committed,
					.changed = true,
				};
			} else if (_state->lastLimitError()) {
				showLastLimitToast();
				handled = true;
			}
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		});
	} else if (atStart && key == Qt::Key_Backspace) {
		handled = undoLastInputRule()
			|| resetActiveBlockType()
			|| removeBoundaryOwner(false);
	} else if (atEnd && key == Qt::Key_Delete) {
		handled = removeBoundaryOwner(true);
	}
	if (handled) {
		e->accept();
	}
	return handled;
}

bool Widget::handleTabNavigation(QKeyEvent *e) {
	const auto key = e->key();
	if (key != Qt::Key_Tab && key != Qt::Key_Backtab) {
		return false;
	}
	const auto modifiers = e->modifiers()
		& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
	if (modifiers != Qt::NoModifier && modifiers != Qt::ShiftModifier) {
		return false;
	}
	const auto forward = (key != Qt::Key_Backtab)
		&& (modifiers != Qt::ShiftModifier);
	if (!moveListItemDepth(forward) && !moveTabBoundary(forward)) {
		return false;
	}
	e->accept();
	return true;
}

bool Widget::moveBoundary(bool forward, bool allowTrailing) {
	const auto target = forward
		? _state->nextEditableOrdinal()
		: _state->previousEditableOrdinal();
	const auto addTrailing = forward
		&& allowTrailing
		&& !target
		&& !_state->isActiveTopLevelParagraph();
	if (!target && !addTrailing) {
		return false;
	}
	auto handled = false;
	beginArticleRelayoutDeferral();
	const auto relayoutGuard = gsl::finally([&] {
		endArticleRelayoutDeferral();
	});
	recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		}
		if (target) {
			refreshPreparedContent();
			if (forward) {
				activateTextOrdinal(*target, 0);
			} else {
				activateTextOrdinalAtEnd(*target);
			}
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		const auto ordinal = _state->ensureTrailingParagraphActive();
		if (!ordinal) {
			handled = forward
				&& allowTrailing
				&& _state->lastLimitError().has_value();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		refreshPreparedContent();
		activateTextOrdinal(*ordinal, 0);
		handled = true;
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
	return handled;
}

bool Widget::insertLeadingParagraphFromField(bool focusInserted) {
	if (_state->previousEditableOrdinal().has_value()
		|| _state->isActiveTopLevelParagraphOrHeading()) {
		return false;
	}
	auto handled = false;
	beginArticleRelayoutDeferral();
	const auto relayoutGuard = gsl::finally([&] {
		endArticleRelayoutDeferral();
	});
	recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		}
		const auto ordinal = _state->insertLeadingParagraphActive(
			focusInserted);
		if (!ordinal) {
			if (_state->lastLimitError()) {
				showLastLimitToast();
				handled = true;
			}
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		refreshPreparedContent();
		activateTextOrdinal(*ordinal, 0);
		handled = true;
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
	return handled;
}

bool Widget::moveBoundaryAfterCommit(
		ApplyResult committed,
		bool forward,
		bool allowTrailing,
		bool *mutated) {
	if (mutated) {
		*mutated = false;
	}
	const auto target = forward
		? _state->nextEditableOrdinal()
		: _state->previousEditableOrdinal();
	if (target) {
		refreshPreparedContent();
		if (forward) {
			activateTextOrdinal(*target, 0);
		} else {
			activateTextOrdinalAtEnd(*target);
		}
		return true;
	}
	if (forward && allowTrailing && !_state->isActiveTopLevelParagraph()) {
		const auto ordinal = _state->ensureTrailingParagraphActive();
		if (!ordinal) {
			return _state->lastLimitError().has_value();
		}
		if (mutated) {
			*mutated = true;
		}
		refreshPreparedContent();
		activateTextOrdinal(*ordinal, 0);
		return true;
	}
	return false;
}

bool Widget::moveVerticalDownBoundary() {
	auto handled = false;
	const auto refreshPreparedContentAndActivate = [&](
			int ordinal,
			int cursorOffset) {
		beginInlineFieldRevealSuppression();
		{
			const auto revealGuard = gsl::finally([&] {
				endInlineFieldRevealSuppression();
			});
			refreshPreparedContent();
			activateTextOrdinal(ordinal, cursorOffset, ActivateReveal::Skip);
		}
		revealActiveInlineField();
	};
	recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		} else if (const auto target
			= _state->removeTemporaryDownParagraphAndMove();
			target.action != State::BoundaryTarget::Action::None) {
			switch (target.action) {
			case State::BoundaryTarget::Action::Text:
				refreshPreparedContentAndActivate(target.textOrdinal, 0);
				break;
			case State::BoundaryTarget::Action::StructuralSelection:
				beginInlineFieldRevealSuppression();
				{
					const auto revealGuard = gsl::finally([&] {
						endInlineFieldRevealSuppression();
					});
					refreshPreparedContent();
				}
				_boundarySelectionOrigin = std::nullopt;
				_selection = {};
				_selectionEndpoints = {};
				finishArticleSelection();
				setStructuralSelection(target.structuralSelection);
				_pendingOrdinal = -1;
				_pendingCursorOffset = 0;
				hideInlineField();
				clearInlineFieldEditSession();
				relayoutCurrentContent();
				setFocus();
				update();
				break;
			case State::BoundaryTarget::Action::None:
			case State::BoundaryTarget::Action::RemoveActiveOwner:
				break;
			}
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = true,
			};
		} else if (const auto target = _state->moveActiveSpecialBlockDown()) {
			refreshPreparedContentAndActivate(*target, 0);
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = true,
			};
		}
		auto mutated = false;
		if (_state->lastLimitError()) {
			handled = moveBoundaryAfterCommit(
				committed,
				true,
				false,
				&mutated);
			if (!handled) {
				handled = true;
			}
		} else {
			handled = moveBoundaryAfterCommit(
				committed,
				true,
				true,
				&mutated);
		}
		return MutationTransactionResult{
			.committed = committed,
			.changed = mutated || (committed == ApplyResult::Changed),
		};
	});
	return handled;
}

bool Widget::moveTabBoundary(bool forward) {
	auto handled = false;
	const auto target = forward
		? _state->nextEditableOrdinal()
		: _state->previousEditableOrdinal();
	if (!target && (!forward || _state->isActiveTopLevelParagraph())) {
		return false;
	}
	beginArticleRelayoutDeferral();
	const auto relayoutGuard = gsl::finally([&] {
		endArticleRelayoutDeferral();
	});
	recordMutationTransaction([&] {
		auto committed = ApplyResult::Unchanged;
		if (!_field->isHidden()) {
			committed = commitInlineField();
			if (committed == ApplyResult::Failed) {
				handled = true;
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			}
		}
		if (target) {
			clearSelection();
			if (committed == ApplyResult::Changed) {
				refreshAfterInlineFieldCommit(committed);
			}
			activateTextOrdinalAtEnd(*target);
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		clearSelection();
		const auto ordinal = _state->ensureTrailingParagraphActive();
		if (!ordinal) {
			handled = _state->lastLimitError().has_value();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		refreshPreparedContent();
		activateTextOrdinalAtEnd(*ordinal);
		handled = true;
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
	return handled;
}

bool Widget::resetActiveBlockType() {
	if (_field->isHidden()
		|| (_state->activeFieldMode() != State::FieldMode::Rich)) {
		return false;
	}
	const auto info = activeBlockInfo();
	if ((info.kind != RichPage::BlockKind::Heading)
		&& (info.kind != RichPage::BlockKind::Footer)) {
		return false;
	}
	auto handled = false;
	beginArticleRelayoutDeferral();
	const auto relayoutGuard = gsl::finally([&] {
		endArticleRelayoutDeferral();
	});
	recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		}
		const auto target = _state->resetActiveBlockToParagraph();
		if (!target) {
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		refreshPreparedContent();
		activateTextOrdinal(*target, 0);
		handled = true;
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
	return handled;
}

bool Widget::moveListItemDepth(bool deeper) {
	if (_field->isHidden()
		|| (_state->activeFieldMode() != State::FieldMode::Rich)) {
		return false;
	}
	const auto inListItem = _state->hasActiveListItemSurface();
	const auto intoPreviousList = !inListItem
		&& deeper
		&& _field->textCursor().atStart();
	if (!inListItem && !intoPreviousList) {
		return false;
	} else if (deeper && _state->hasActiveListItemExtraLine()) {
		// Own line of an item moves alone, so it can't take the item deeper.
		return true;
	}
	const auto text = ConvertEditorTagsToRichText(
		_field->getTextWithAppliedMarkdown());
	const auto cursorOffset = std::clamp(
		richOffsetForFieldOffset(text, _field->textCursor().position()),
		0,
		int(text.text.size()));
	auto handled = false;
	beginArticleRelayoutDeferral();
	const auto relayoutGuard = gsl::finally([&] {
		endArticleRelayoutDeferral();
	});
	recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		}
		const auto target = intoPreviousList
			? _state->appendActiveParagraphToPreviousList()
			: deeper
			? _state->sinkActiveListItem()
			: _state->liftActiveListLineOrItem();
		if (!target) {
			if (_state->lastLimitError()) {
				showLastLimitToast();
			}
			handled = inListItem;
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		refreshPreparedContent();
		activateTextOrdinal(*target, cursorOffset);
		handled = true;
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
	return handled;
}

bool Widget::removeBoundaryOwner(bool forward) {
	auto handled = false;
	const auto committedSelection = [&]() {
		if (_field->isHidden()
			|| (_state->activeFieldMode() != State::FieldMode::Rich)) {
			return std::optional<CommittedFieldSelectionCapture>();
		}
		const auto leaf = _state->activeLeafPath();
		if (!leaf) {
			return std::optional<CommittedFieldSelectionCapture>();
		}
		const auto text = ConvertEditorTagsToRichText(
			_field->getTextWithAppliedMarkdown());
		const auto cursor = _field->textCursor();
		const auto length = int(text.text.size());
		return std::make_optional(CommittedFieldSelectionCapture{
			.leaf = *leaf,
			.text = text,
			.anchorOffset = std::clamp(
				richOffsetForFieldOffset(text, cursor.anchor()),
				0,
				length),
			.cursorOffset = std::clamp(
				richOffsetForFieldOffset(text, cursor.position()),
				0,
				length),
		});
	}();
	beginArticleRelayoutDeferral();
	const auto relayoutGuard = gsl::finally([&] {
		endArticleRelayoutDeferral();
	});
	recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		}
		if (committed == ApplyResult::Changed) {
			refreshAfterInlineFieldCommit(committed);
			if (committedSelection) {
				const auto mapped = MapCommittedFieldSelectionAfterCommit(
					*_state,
					*committedSelection);
				if (!mapped) {
					handled = true;
					return MutationTransactionResult{
						.committed = committed,
						.changed = true,
					};
				}
				activateTextOrdinal(
					mapped->ordinal,
					mapped->anchorOffset,
					mapped->cursorOffset,
					ActivateReveal::Skip);
			}
		}
		const auto joined = _state->joinActiveParagraphBoundary(forward);
		if (joined.result == ApplyResult::Changed) {
			_boundarySelectionOrigin = std::nullopt;
			hideInlineField();
			clearInlineFieldEditSession();
			refreshPreparedContent();
			auto ordinal = joined.destinationLeaf
				? _state->textOrdinalForLeafPath(*joined.destinationLeaf)
				: -1;
			if (ordinal < 0) {
				ordinal = _state->activeTextOrdinal();
			}
			if (ordinal >= 0 && ordinal < _state->textNodeCount()) {
				activateTextOrdinal(
					ordinal,
					joined.selectionFrom,
					joined.selectionTo);
			} else {
				setFocus();
				notifyToolbarStateChanged();
			}
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = true,
			};
		} else if (joined.result == ApplyResult::Failed) {
			if (_state->lastLimitError()) {
				showLastLimitToast();
			}
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		const auto target = _state->activeBoundaryTarget(forward);
		using BoundaryAction = State::BoundaryTarget::Action;
		switch (target.action) {
		case BoundaryAction::RemoveActiveOwner: {
			_boundarySelectionOrigin = std::nullopt;
			const auto adjacent = _state->removeActiveOwnerAndSelectAdjacent(
				forward);
			hideInlineField();
			clearInlineFieldEditSession();
			refreshPreparedContent();
			if (adjacent) {
				if (forward) {
					activateTextOrdinal(*adjacent, 0);
				} else {
					activateTextOrdinalAtEnd(*adjacent);
				}
			} else {
				activateInitialNode();
			}
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = true,
			};
		}
		case BoundaryAction::Text:
			_boundarySelectionOrigin = std::nullopt;
			if (committed == ApplyResult::Changed) {
				refreshAfterInlineFieldCommit(committed);
			}
			if (forward) {
				activateTextOrdinal(target.textOrdinal, 0);
			} else {
				activateTextOrdinalAtEnd(target.textOrdinal);
			}
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		case BoundaryAction::StructuralSelection:
			setStructuralSelection(
				target.structuralSelection,
				currentBoundarySelectionOrigin(forward));
			_selection = {};
			_selectionEndpoints = {};
			finishArticleSelection();
			_pendingOrdinal = -1;
			_pendingCursorOffset = 0;
			hideInlineField();
			clearInlineFieldEditSession();
			refreshAfterInlineFieldCommit(committed);
			update();
			handled = true;
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		case BoundaryAction::None: {
			auto mutated = false;
			handled = moveBoundaryAfterCommit(
				committed,
				forward,
				forward,
				&mutated);
			return MutationTransactionResult{
				.committed = committed,
				.changed = mutated || (committed == ApplyResult::Changed),
			};
		}
		}
		Unexpected("Boundary action.");
	});
	return handled;
}

void Widget::ensurePendingActivation() {
	if (_pendingOrdinal < 0) {
		_activeSegmentIndex = (_activeOrdinal >= 0)
			? segmentIndexForEditableOrdinal(_activeOrdinal)
			: _article->firstEditableSegmentIndex();
		return;
	}
	const auto ordinal = _pendingOrdinal;
	const auto cursorOffset = _pendingCursorOffset;
	_pendingOrdinal = -1;
	_pendingCursorOffset = 0;
	activateTextOrdinal(ordinal, cursorOffset);
}

} // namespace Iv::Editor
