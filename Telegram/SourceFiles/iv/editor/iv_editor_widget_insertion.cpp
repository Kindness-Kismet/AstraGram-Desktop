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

bool Widget::canInsertListAtCaret() const {
	// Nesting is Tab, restyling is the change menu, so never insert in an item.
	if (hasStructuralSelection()) {
		return true;
	}
	const auto converted = structuralSelectionForTextSelection();
	if ((converted.kind == PreparedEditSelectionKind::Blocks)
		|| (converted.kind == PreparedEditSelectionKind::ListItems)) {
		return true;
	}
	return !_state->hasActiveListItemSurface();
}

void Widget::insertBlock(State::InsertAction action) {
	using InsertType = State::InsertBlockType;
	const auto listAction = (action.type == InsertType::OrderedList)
		|| (action.type == InsertType::BulletList)
		|| (action.type == InsertType::TaskList);
	if (listAction && !canInsertListAtCaret()) {
		return;
	} else if (BlockConversionExpandsToActiveLine(action.type)
		&& activeLeafIsTableCell()) {
		return;
	}
	recordMutationTransaction([&] {
		auto committed = ApplyResult::Unchanged;
		const auto wrapAction = listAction
			|| (action.type == InsertType::Blockquote)
			|| (action.type == InsertType::Pullquote)
			|| (action.type == InsertType::Details);
		if (wrapAction && !hasStructuralSelection()) {
			const auto converted = structuralSelectionForTextSelection();
			const auto usable = (converted.kind
				== PreparedEditSelectionKind::Blocks)
				|| (listAction
					&& (converted.kind
						== PreparedEditSelectionKind::ListItems));
			auto stale = false;
			if (usable) {
				if (!_field->isHidden()) {
					const auto was = CountRichPageBlocks(_state->richPage());
					committed = commitInlineField();
					if (committed == ApplyResult::Failed) {
						return MutationTransactionResult{
							.committed = committed,
							.failed = true,
						};
					}
					_pendingOrdinal = -1;
					_pendingCursorOffset = 0;
					hideInlineField();
					clearInlineFieldEditSession();
					stale = (CountRichPageBlocks(_state->richPage()) != was);
				}
				if (!stale) {
					_boundarySelectionOrigin = std::nullopt;
					_selection = {};
					_selectionEndpoints = {};
					finishArticleSelection();
					setStructuralSelection(converted);
				}
			}
		}
		const auto context = activeTextInsertContext();
		const auto reversedFieldSelection = [&] {
			if (!context) {
				return false;
			}
			const auto cursor = _field->textCursor();
			return cursor.hasSelection()
				&& (cursor.anchor() > cursor.position());
		}();
		const auto restoreField = context.has_value();
		const auto restoreLeaf = restoreField
			? _fieldLeaf
			: std::optional<State::LeafPath>();
		const auto restoreStyleKey = restoreField
			? _activeFieldStyleKey
			: std::optional<InlineFieldStyleKey>();
		const auto restoreMode = _fieldMode;
		const auto restoreSelection = restoreField
			? captureHistoryViewState().leafSelection
			: std::optional<HistoryLeafSelection>();
		if (!context && !_field->isHidden()) {
			committed = commitInlineField();
			if (committed == ApplyResult::Failed) {
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			}
		}
		const auto hadStructuralSelection = hasStructuralSelection();
		auto destination = State::BoundaryTarget();
		if (hadStructuralSelection || restoreField) {
			_pendingOrdinal = -1;
			_pendingCursorOffset = 0;
			hideInlineField();
			clearInlineFieldEditSession(restoreField);
		}
		auto restore = restoreField;
		const auto restoreInlineField = gsl::finally([&] {
			if (restore) {
				restoreInlineFieldAfterMutation(
					restoreLeaf,
					restoreStyleKey,
					restoreMode,
					restoreSelection);
			}
		});
		auto activeBlockResult
			= std::optional<State::ActiveTextBlockActionResult>();
		auto applied = false;
		if (hadStructuralSelection) {
			applied = _state->replaceStructuralSelectionWithBlock(
				_structuralSelection,
				action,
				context,
				&destination);
		} else if (restoreField
			&& context
			&& _state->blockActionExpandsToActiveLine(action.type)) {
			activeBlockResult = _state->applyActiveTextBlockAction(
				action,
				*context);
			applied = (activeBlockResult->result == ApplyResult::Changed);
		} else {
			applied = _state->insertBlockAfterActive(action, context);
		}
		if (!applied) {
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		restore = false;
		if (hadStructuralSelection) {
			refreshPreparedContent();
			switch (destination.action) {
			case State::BoundaryTarget::Action::StructuralSelection:
				_boundarySelectionOrigin = std::nullopt;
				_selection = {};
				_selectionEndpoints = {};
				finishArticleSelection();
				setStructuralSelection(destination.structuralSelection);
				update();
				break;
			case State::BoundaryTarget::Action::Text:
				clearSelection();
				activateTextOrdinal(destination.textOrdinal, 0);
				break;
			case State::BoundaryTarget::Action::None:
			case State::BoundaryTarget::Action::RemoveActiveOwner: {
				clearSelection();
				const auto ordinal = _state->activeTextOrdinal();
				if (ordinal >= 0 && ordinal < _state->textNodeCount()) {
					activateTextOrdinal(ordinal, 0);
				} else {
					activateInitialNode();
				}
			} break;
			}
		} else {
			refreshPreparedContent();
			auto restoredActiveBlock = false;
			if (activeBlockResult && activeBlockResult->destinationLeaf) {
				const auto ordinal = _state->textOrdinalForLeafPath(
					*activeBlockResult->destinationLeaf);
				if (ordinal >= 0) {
					const auto from = activeBlockResult->selectionFrom;
					const auto to = activeBlockResult->selectionTo;
					activateTextOrdinal(
						ordinal,
						reversedFieldSelection ? to : from,
						reversedFieldSelection ? from : to);
					restoredActiveBlock = true;
				}
			}
			if (!restoredActiveBlock) {
				const auto ordinal = _state->activeTextOrdinal();
				if (ordinal >= 0 && ordinal < _state->textNodeCount()) {
					activateTextOrdinal(ordinal, 0);
				} else {
					activateInitialNode();
				}
			}
		}
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

void Widget::insertPreparedBlock(RichPage::Block block) {
	auto blocks = std::vector<RichPage::Block>();
	blocks.push_back(std::move(block));
	insertPreparedBlocks(std::move(blocks));
}

void Widget::requestMedia(
		std::optional<State::ReplaceTarget> replaceTarget,
		RequestMediaType type) {
	if (_requestMedia) {
		_requestMedia(
			not_null<Widget*>(this),
			QPointer<QWidget>(_outer.get()),
			std::move(replaceTarget),
			type);
	}
}

void Widget::replacePreparedBlock(
		State::ReplaceTarget target,
		RichPage::Block block) {
	const auto savedActiveIndex = (target.itemIndex >= 0)
		? groupedActiveIndexForPath(target.path)
		: -1;
	recordMutationTransaction([&] {
		auto committed = ApplyResult::Unchanged;
		if (!_field->isHidden()) {
			committed = commitInlineField();
			if (committed == ApplyResult::Failed) {
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			}
		}
		if (!_state->replaceBlockWithPreparedBlock(target, std::move(block))) {
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		_pendingOrdinal = -1;
		_pendingCursorOffset = 0;
		hideInlineField();
		clearInlineFieldEditSession();
		clearTextSelection();
		clearStructuralSelection();
		refreshPreparedContent();
		restoreGroupedActiveIndexForPath(target.path, savedActiveIndex);
		const auto ordinal = _state->activeTextOrdinal();
		if (ordinal >= 0 && ordinal < _state->textNodeCount()) {
			activateTextOrdinal(ordinal, 0);
		}
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

void Widget::insertPreparedBlocks(std::vector<RichPage::Block> blocks) {
	insertPreparedBlocks(std::move(blocks), activeTextInsertContext());
}

void Widget::pastePreparedBlocks(
		std::vector<RichPage::Block> blocks,
		PreparedMediaPasteTarget target) {
	if (target.blockDrop) {
		pasteBlocksAtDropTarget(std::move(blocks), *target.blockDrop);
		return;
	}
	auto activation = activatePreparedMediaPasteTarget(std::move(target));
	if (activation.resolved) {
		insertPreparedBlocks(
			std::move(blocks),
			std::move(activation.context),
			false);
	} else {
		insertPreparedBlocks(
			std::move(blocks),
			activeTextInsertContext(),
			false);
	}
}

void Widget::pasteBlocksAtDropTarget(
		std::vector<RichPage::Block> blocks,
		const Markdown::PreparedEditBlockDropTarget &target) {
	recordMutationTransaction([&] {
		auto committed = ApplyResult::Unchanged;
		if (!_field->isHidden()) {
			committed = commitInlineField();
			if (committed == ApplyResult::Failed) {
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			}
		}
		if (!_state->insertPreparedBlocksAtDropTarget(
				std::move(blocks),
				target)) {
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		_pendingOrdinal = -1;
		_pendingCursorOffset = 0;
		hideInlineField();
		clearInlineFieldEditSession();
		clearTextSelection();
		clearStructuralSelection();
		refreshPreparedContent();
		const auto ordinal = _state->activeTextOrdinal();
		if (ordinal >= 0 && ordinal < _state->textNodeCount()) {
			activateTextOrdinal(ordinal, 0);
		}
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

void Widget::insertPreparedBlocks(
		std::vector<RichPage::Block> blocks,
		std::optional<State::ActiveTextInsertContext> context,
		bool useStructuralSelection) {
	if (blocks.empty()) {
		return;
	}
	recordMutationTransaction([&] {
		const auto restoreField = context.has_value();
		const auto restoreLeaf = restoreField
			? _fieldLeaf
			: std::optional<State::LeafPath>();
		const auto restoreStyleKey = restoreField
			? _activeFieldStyleKey
			: std::optional<InlineFieldStyleKey>();
		const auto restoreMode = _fieldMode;
		const auto restoreSelection = restoreField
			? captureHistoryViewState().leafSelection
			: std::optional<HistoryLeafSelection>();
		auto committed = ApplyResult::Unchanged;
		if (!context && !_field->isHidden()) {
			committed = commitInlineField();
			if (committed == ApplyResult::Failed) {
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			}
		}
		const auto ignoredStructuralSelection = !useStructuralSelection
			&& hasStructuralSelection();
		const auto hadStructuralSelection = useStructuralSelection
			&& hasStructuralSelection();
		if (hadStructuralSelection || restoreField) {
			_pendingOrdinal = -1;
			_pendingCursorOffset = 0;
			hideInlineField();
			clearInlineFieldEditSession(restoreField);
		}
		auto restore = restoreField;
		const auto restoreInlineField = gsl::finally([&] {
			if (restore) {
				restoreInlineFieldAfterMutation(
					restoreLeaf,
					restoreStyleKey,
					restoreMode,
					restoreSelection);
			}
		});
		const auto applied = hadStructuralSelection
			? _state->replaceStructuralSelectionWithPreparedBlocks(
				_structuralSelection,
				std::move(blocks),
				context)
			: _state->insertPreparedBlocksAfterActive(
				std::move(blocks),
				context);
		if (!applied) {
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		restore = false;
		if (hadStructuralSelection || ignoredStructuralSelection) {
			clearSelection();
		}
		refreshPreparedContent();
		activateTextOrdinal(_state->activeTextOrdinal(), 0);
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

} // namespace Iv::Editor
