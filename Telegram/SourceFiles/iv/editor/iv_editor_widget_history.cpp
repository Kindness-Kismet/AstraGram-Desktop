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
#include "iv/editor/iv_editor_widget_internal.h"

namespace Iv::Editor {
using namespace WidgetDetails;

void Widget::truncateHistoryRedo() {
	if ((_historyIndex < 0) || (_historyIndex >= int(_history.size()))) {
		return;
	}
	const auto next = _history.begin() + _historyIndex + 1;
	if (next != _history.end()) {
		_history.erase(next, _history.end());
		removeRetainedLeafFieldsAfter(_historyIndex);
		notifyToolbarStateChanged();
	}
}

void Widget::resetMutationHistory() {
	_history.clear();
	_history.push_back(captureHistoryEntry());
	_historyIndex = 0;
	notifyToolbarStateChanged();
}

bool Widget::canPerformFieldUndoRedo(bool redo) const {
	if (_field->isHidden()) {
		return false;
	}
	const auto document = _field->rawTextEdit()->document();
	const auto steps = redo
		? document->availableRedoSteps()
		: document->availableUndoSteps();
	const auto localRedoAvailable = (document->availableRedoSteps() > 0)
		|| _fieldRedoAvailable
		|| _field->isRedoAvailable();
	if (!redo
		&& localRedoAvailable
		&& activeInlineFieldTextMatchesState()) {
		return false;
	}
	const auto available = (steps > 0)
		|| (redo
			? (_fieldRedoAvailable || _field->isRedoAvailable())
			: (_fieldUndoAvailable || _field->isUndoAvailable()));
	if (!available) {
		return false;
	}
	const auto &noopState = redo
		? _fieldRedoNoopState
		: _fieldUndoNoopState;
	return !noopState || (_field->getTextWithTags() != *noopState);
}

bool Widget::activeInlineFieldTextMatchesState() const {
	if (_field->isHidden() || !_fieldLeaf) {
		return false;
	}
	const auto activeLeaf = _state->activeLeafPath();
	if (!activeLeaf || (*activeLeaf != *_fieldLeaf)) {
		return false;
	}
	const auto trimLeft = !_state->codeBlockLanguage(
		_state->activeTextOrdinal()).has_value();
	if (_state->activeFieldMode() == State::FieldMode::Raw) {
		const auto trimmed = TrimInlineFieldText(
			{ _state->activeRawText(), {} },
			trimLeft);
		return InlineFieldTextsEqual(_field->getTextWithTags(), trimmed.text);
	}
	const auto activeText = ConvertRichTextToEditorTags(_state->activeText());
	const auto trimmed = TrimInlineFieldText(activeText.text, trimLeft);
	return InlineFieldTextsEqual(_field->getTextWithTags(), trimmed.text);
}

bool Widget::canPerformHistoryUndoRedo(bool redo) const {
	if ((_historyIndex < 0) || (_historyIndex >= int(_history.size()))) {
		return false;
	}
	return redo
		? (_historyIndex + 1 < int(_history.size()))
		: (_historyIndex > 0);
}

bool Widget::canPerformUndoRedo(bool redo) const {
	return canPerformFieldUndoRedo(redo) || canPerformHistoryUndoRedo(redo);
}

bool Widget::handleUndoRedoShortcut(QKeyEvent *e) {
	auto redo = std::optional<bool>();
	if (e == QKeySequence::Undo) {
		redo = false;
	} else if (e == QKeySequence::Redo) {
		redo = true;
	}
	if (!redo) {
		return false;
	}
	const auto redoValue = *redo;
	if (canPerformFieldUndoRedo(redoValue)) {
		if (performFieldUndoRedo(redoValue)) {
			e->accept();
			return true;
		}
	}
	if (canPerformHistoryUndoRedo(redoValue)) {
		crl::on_main(this, [=] {
			performUndoRedo(redoValue, false);
		});
	}
	e->accept();
	return true;
}

bool Widget::handleUndoRedoShortcutOverride(QKeyEvent *e) {
	auto redo = std::optional<bool>();
	if (e == QKeySequence::Undo) {
		redo = false;
	} else if (e == QKeySequence::Redo) {
		redo = true;
	}
	if (!redo || searchBlockedByLayer()) {
		return false;
	}
	const auto focused = QApplication::focusWidget();
	if (qobject_cast<QTextEdit*>(focused)
		|| qobject_cast<QLineEdit*>(focused)) {
		return false;
	} else if (hasFocus()) {
		e->accept();
		return false;
	}
	const auto redoValue = *redo;
	if (!canPerformUndoRedo(redoValue)) {
		return false;
	}
	e->accept();
	crl::on_main(this, [=] {
		performToolbarUndoRedo(redoValue);
	});
	return true;
}

bool Widget::handleSelectAllShortcut(QKeyEvent *e) {
	if (e != QKeySequence::SelectAll) {
		return false;
	}
	if (SingleRootPlainTextFieldSelectAllPassthrough(
			_state->richPage(),
			_state->activeLeafPath(),
			_field->isHidden())) {
		return false;
	}
	selectWholeDocument();
	e->accept();
	return true;
}

void Widget::selectWholeDocument() {
	if (!_field->isHidden()) {
		const auto committed = recordMutationTransaction([&] {
			const auto committed = commitInlineField();
			if (committed != ApplyResult::Failed) {
				_pendingOrdinal = -1;
				_pendingCursorOffset = 0;
				hideInlineField();
				clearInlineFieldEditSession();
			}
			return committed;
		});
		if (committed == ApplyResult::Failed) {
			return;
		}
		refreshAfterInlineFieldCommit(committed);
	}
	const auto blockCount = int(_state->richPage().blocks.size());
	_selection = {};
	_selectionEndpoints = {};
	finishArticleSelection();
	setStructuralSelection(blockCount > 0
		? BlockSelectionFromIndexes(
			PreparedEditBlockContainerPath(),
			0,
			blockCount - 1)
		: PreparedEditSelection());
	setFocus();
	update();
}

bool Widget::performFieldUndoRedo(bool redo) {
	if (!canPerformFieldUndoRedo(redo)) {
		return false;
	}
	const auto before = _field->getTextWithTags();
	const auto wasPerformingUndoRedo = _performingUndoRedo;
	_performingUndoRedo = true;
	const auto guard = gsl::finally([&] {
		_performingUndoRedo = wasPerformingUndoRedo;
	});
	if (redo) {
		_field->redo();
	} else {
		_field->undo();
	}
	if (_field->isHidden()) {
		return false;
	}
	const auto document = _field->rawTextEdit()->document();
	_fieldUndoAvailable = (document->availableUndoSteps() > 0)
		|| _field->isUndoAvailable();
	_fieldRedoAvailable = (document->availableRedoSteps() > 0)
		|| _field->isRedoAvailable();
	const auto after = _field->getTextWithTags();
	if (after != before) {
		clearFieldUndoRedoNoopState();
		notifyToolbarStateChanged();
		return true;
	}
	if (redo) {
		_fieldRedoNoopState = after;
	} else {
		_fieldUndoNoopState = after;
	}
	notifyToolbarStateChanged();
	return false;
}

void Widget::performUndoRedo(bool redo, bool allowFieldLocal) {
	if (allowFieldLocal && performFieldUndoRedo(redo)) {
		return;
	}
	if (!canPerformHistoryUndoRedo(redo)) {
		return;
	}
	const auto nextIndex = _historyIndex + (redo ? 1 : -1);
	if ((nextIndex < 0) || (nextIndex >= int(_history.size()))) {
		return;
	}
	const auto previousIndex = _historyIndex;
	const auto wasPerformingUndoRedo = _performingUndoRedo;
	_performingUndoRedo = true;
	const auto guard = gsl::finally([&] {
		_performingUndoRedo = wasPerformingUndoRedo;
	});
	const auto wasRetainingFieldHistoryIndexOverride
		= _retainingFieldHistoryIndexOverride;
	_retainingFieldHistoryIndexOverride = previousIndex;
	const auto retainingFieldHistoryIndexOverride = gsl::finally([&] {
		_retainingFieldHistoryIndexOverride
			= wasRetainingFieldHistoryIndexOverride;
	});
	retainActiveLeafField();
	_historyIndex = nextIndex;
	const auto wasRestoringHistoryRedo = _restoringHistoryRedo;
	_restoringHistoryRedo = redo;
	const auto restoringHistoryRedo = gsl::finally([&] {
		_restoringHistoryRedo = wasRestoringHistoryRedo;
	});
	restoreHistoryEntry(_history[_historyIndex]);
	_fieldUndoAvailable = !_field->isHidden()
		? _field->isUndoAvailable()
		: false;
	_fieldRedoAvailable = !_field->isHidden()
		? _field->isRedoAvailable()
		: false;
	clearFieldUndoRedoNoopState();
	notifyToolbarStateChanged();
	_autosaveEvents.fire({
		.type = AutosaveEventType::StructuralMutation,
	});
}

Widget::HistoryViewState Widget::captureHistoryViewState() const {
	auto result = HistoryViewState();
	if (!_field->isHidden()) {
		const auto leaf = _state->activeLeafPath();
		if (!leaf) {
			return result;
		}
		const auto cursor = _field->textCursor();
		const auto trimLeft = !_state->codeBlockLanguage(
			_state->activeTextOrdinal()).has_value();
		auto anchorOffset = 0;
		auto cursorOffset = 0;
		if (_state->activeFieldMode() == State::FieldMode::Raw) {
			const auto trimmed = TrimInlineFieldText(
				{ _state->activeRawText(), {} },
				trimLeft);
			const auto size = int(_state->activeRawText().size());
			anchorOffset = std::clamp(cursor.anchor() + trimmed.left, 0, size);
			cursorOffset = std::clamp(
				cursor.position() + trimmed.left,
				0,
				size);
		} else {
			const auto activeText = ConvertRichTextToEditorTags(
				_state->activeText());
			const auto trimmed = TrimInlineFieldText(activeText.text, trimLeft);
			const auto size = int(_state->activeText().text.size());
			anchorOffset = std::clamp(
				MapEditorOffsetToRichOffset(
					activeText.replacements,
					cursor.anchor() + trimmed.left),
				0,
				size);
			cursorOffset = std::clamp(
				MapEditorOffsetToRichOffset(
					activeText.replacements,
					cursor.position() + trimmed.left),
				0,
				size);
		}
		result.leafSelection = HistoryLeafSelection{
			.leaf = *leaf,
			.anchorOffset = anchorOffset,
			.cursorOffset = cursorOffset,
		};
	} else if (hasStructuralSelection()) {
		result.structuralSelection = _structuralSelection;
		result.boundarySelectionOrigin = _boundarySelectionOrigin;
	}
	return result;
}

Widget::HistoryEntry Widget::captureHistoryEntry() const {
	return {
		.snapshot = _state->snapshot(),
		.viewState = captureHistoryViewState(),
	};
}

void Widget::restoreHistoryEntry(const HistoryEntry &entry) {
	hideInlineField();
	clearInlineFieldEditSession();
	if (_article && (_horizontalScrollDrag != HorizontalScrollDrag::None)) {
		_article->endHorizontalScroll();
	}
	_selection = {};
	_selectionEndpoints = {};
	setStructuralSelection({});
	finishArticleSelection();
	_pendingOrdinal = -1;
	_pendingCursorOffset = 0;
	_trackingPointerPress = false;
	_horizontalScrollLock = std::nullopt;
	_pressedControl = {};
	_pressedControlPoint = std::nullopt;
	_horizontalScrollDrag = HorizontalScrollDrag::None;
	_pendingTouchHorizontalScrollPoint = std::nullopt;

	const auto wasRestoring = _restoringHistory;
	_restoringHistory = true;
	const auto guard = gsl::finally([&] {
		_restoringHistory = wasRestoring;
	});

	_state->restoreSnapshot(entry.snapshot);
	refreshPreparedContent();

	if (const auto &selection = entry.viewState.structuralSelection) {
		_activeOrdinal = _state->activeTextOrdinal();
		_activeSegmentIndex = -1;
		_fieldLeaf = std::nullopt;
		clearDisplayMathEditSession();
		setStructuralSelection(
			*selection,
			entry.viewState.boundarySelectionOrigin);
		hideInlineField();
		update();
		return;
	}
	if (const auto &leafSelection = entry.viewState.leafSelection) {
		const auto ordinal = _state->textOrdinalForLeafPath(leafSelection->leaf);
		if (ordinal >= 0) {
			activateTextOrdinal(
				ordinal,
				leafSelection->anchorOffset,
				leafSelection->cursorOffset);
			return;
		}
	}
	_activeOrdinal = _state->activeTextOrdinal();
	_activeSegmentIndex = -1;
	_fieldLeaf = std::nullopt;
	clearDisplayMathEditSession();
	hideInlineField();
	update();
}

bool Widget::mutationTransactionChanged(bool changed) {
	return changed;
}

bool Widget::mutationTransactionChanged(ApplyResult result) {
	return (result == ApplyResult::Changed);
}

bool Widget::mutationTransactionChanged(
		const MutationTransactionResult &result) {
	return result.changed;
}

void Widget::finishMutationTransaction(
		const HistoryEntry &before,
		bool changed,
		int beforeHistoryIndex,
		uint64 beforeRetainToken) {
	// Some transactions commit and bail out without refreshing the article.
	if (_preparedContentStaleAfterCommit) {
		refreshPreparedContent();
	}
	if (!changed) {
		return;
	}
	const auto after = captureHistoryEntry();
	const auto snapshotChanged = !SnapshotEquals(before.snapshot, after.snapshot);
	if (!snapshotChanged && (before.viewState == after.viewState)) {
		return;
	}
	if ((beforeHistoryIndex >= 0)
		&& (beforeHistoryIndex < int(_history.size()))
		&& (before.viewState != HistoryViewState())
		&& RichPagesEqual(
			_history[beforeHistoryIndex].snapshot.richPage,
			before.snapshot.richPage)) {
		_history[beforeHistoryIndex].viewState = before.viewState;
	}
	truncateHistoryRedo();
	_history.push_back(after);
	_historyIndex = int(_history.size()) - 1;
	moveRetainedLeafFields(
		beforeHistoryIndex,
		_historyIndex,
		beforeRetainToken);
	notifyToolbarStateChanged();
	if (snapshotChanged
		&& (_state->lastPreparedMutationKind()
			!= PreparedMutationKind::LeafOnly)) {
		_autosaveEvents.fire({
			.type = AutosaveEventType::StructuralMutation,
		});
	}
}

void Widget::retainActiveLeafField(
		bool keepRetainedFieldOnCurrentHistoryEntry) {
	if (!_field) {
		ensureInlineFieldCreated();
		return;
	} else if (!_fieldLeaf
		|| !_activeFieldStyleKey) {
		return;
	}
	const auto leaf = *_fieldLeaf;
	if (_state->textOrdinalForLeafPath(leaf) < 0) {
		return;
	}
	const auto wasSettingField = _settingField;
	_settingField = true;
	_field->hide();
	_settingField = wasSettingField;
	const auto historyIndex = _retainingFieldHistoryIndexOverride.value_or(
		_historyIndex);
	const auto &fieldStyle = inlineFieldStyleFor(
		Markdown::MarkdownArticleTextLeafStyle());
	auto replacement = base::make_unique_q<Ui::InputField>(
		this,
		*fieldStyle.style,
		Ui::InputField::Mode::MultiLine,
		rpl::single(QString()));
	auto retained = RetainedLeafField{
		.historyIndex = historyIndex,
		.retainToken = keepRetainedFieldOnCurrentHistoryEntry
			? _retainedLeafFieldToken
			: ++_retainedLeafFieldToken,
		.leaf = leaf,
		.mode = _fieldMode,
		.styleKey = _activeFieldStyleKey,
	};
	retained.field = std::move(_field);
	retained.suggestions = _fieldSuggestions;
	_field = std::move(replacement);
	_activeFieldStyleKey = std::nullopt;
	_fieldMode = State::FieldMode::Rich;
	_fieldLeaf = std::nullopt;
	setupInlineField();
	clearFieldUndoRedoNoopState();
	for (auto i = _retainedLeafFields.begin(); i != _retainedLeafFields.end();) {
		if ((i->historyIndex == retained.historyIndex)
			&& (i->leaf == retained.leaf)
			&& (i->mode == retained.mode)
			&& (i->styleKey == retained.styleKey)) {
			i = _retainedLeafFields.erase(i);
		} else {
			++i;
		}
	}
	_retainedLeafFields.push_back(std::move(retained));
	pruneRetainedLeafFields();
}

base::unique_qptr<Ui::InputField> Widget::reviveRetainedLeafField(
		int historyIndex,
		const State::LeafPath &leaf,
		State::FieldMode mode,
		const InlineFieldStyleKey &styleKey) {
	for (auto i = int(_retainedLeafFields.size()) - 1; i >= 0; --i) {
		if ((_retainedLeafFields[i].historyIndex == historyIndex)
			&& (_retainedLeafFields[i].leaf == leaf)
			&& (_retainedLeafFields[i].mode == mode)
			&& _retainedLeafFields[i].styleKey
			&& (*_retainedLeafFields[i].styleKey == styleKey)) {
			auto result = std::move(_retainedLeafFields[i].field);
			_fieldSuggestions = _retainedLeafFields[i].suggestions;
			_retainedLeafFields.erase(_retainedLeafFields.begin() + i);
			return result;
		}
	}
	return {};
}

void Widget::restoreInlineFieldAfterMutation(
		const std::optional<State::LeafPath> &restoreLeaf,
		const std::optional<InlineFieldStyleKey> &restoreStyleKey,
		State::FieldMode restoreMode,
		const std::optional<HistoryLeafSelection> &restoreSelection) {
	if (restoreLeaf && restoreStyleKey) {
		if (auto revived = reviveRetainedLeafField(
				_historyIndex,
				*restoreLeaf,
				restoreMode,
				*restoreStyleKey)) {
			_field = std::move(revived);
			_activeFieldStyleKey = restoreStyleKey;
			_fieldMode = restoreMode;
			_fieldLeaf = *restoreLeaf;
			refreshInlineFieldPlaceholder();
			_fieldUndoAvailable = _field->isUndoAvailable();
			_fieldRedoAvailable = _field->isRedoAvailable();
			clearFieldUndoRedoNoopState();
		}
	}
	if (!_fieldLeaf && restoreSelection) {
		const auto ordinal = _state->textOrdinalForLeafPath(
			restoreSelection->leaf);
		if (ordinal >= 0) {
			activateTextOrdinal(
				ordinal,
				restoreSelection->anchorOffset,
				restoreSelection->cursorOffset);
			return;
		}
	}
	_field->show();
	syncInlineFieldGeometry();
	updateInlineFieldHeightOverride();
	syncArticleVisibleTopBottom();
	revealActiveInlineField();
	_field->raise();
	_field->setFocusFast();
	notifyToolbarStateChanged();
}

void Widget::pruneRetainedLeafFields() {
	for (auto i = _retainedLeafFields.begin(); i != _retainedLeafFields.end();) {
		if (!i->field) {
			i = _retainedLeafFields.erase(i);
		} else {
			++i;
		}
	}
	while (int(_retainedLeafFields.size()) > kRetainedLeafFieldLimit) {
		_retainedLeafFields.erase(_retainedLeafFields.begin());
	}
}

void Widget::removeRetainedLeafFieldsAfter(int historyIndex) {
	for (auto i = _retainedLeafFields.begin(); i != _retainedLeafFields.end();) {
		if (i->historyIndex > historyIndex) {
			i = _retainedLeafFields.erase(i);
		} else {
			++i;
		}
	}
}

void Widget::moveRetainedLeafFields(
		int fromHistoryIndex,
		int toHistoryIndex,
		uint64 afterRetainToken) {
	if (fromHistoryIndex == toHistoryIndex) {
		return;
	}
	for (auto &retained : _retainedLeafFields) {
		if ((retained.historyIndex == fromHistoryIndex)
			&& (retained.retainToken > afterRetainToken)) {
			retained.historyIndex = toHistoryIndex;
		}
	}
}

void Widget::refreshAfterInlineFieldCommit(ApplyResult committed) {
	refreshAfterInlineFieldCommit(
		committed,
		_state->activePreparedLeafSource());
}

void Widget::refreshAfterInlineFieldCommit(
		ApplyResult committed,
		std::optional<Markdown::PreparedEditLeafSource> source) {
	_preparedContentStaleAfterCommit = false;
	switch ((committed == ApplyResult::Changed)
		? _state->lastPreparedMutationKind()
		: PreparedMutationKind::None) {
	case PreparedMutationKind::LeafOnly:
		if (source) {
			refreshPreparedLeafAtSource(*source);
		} else {
			refreshPreparedContent();
		}
		break;
	case PreparedMutationKind::FullRebuild:
		refreshPreparedContent();
		break;
	case PreparedMutationKind::None:
		relayoutCurrentContent();
		break;
	}
	notifyToolbarStateChanged();
}

} // namespace Iv::Editor
