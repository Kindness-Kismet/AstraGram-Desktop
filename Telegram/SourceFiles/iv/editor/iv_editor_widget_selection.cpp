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

void Widget::setStructuralSelection(
		Markdown::PreparedEditSelection selection,
		std::optional<BoundarySelectionOrigin> origin) {
	if (_structuralSelection.empty()
		&& !selection.empty()
		&& !_field->isHidden()) {
		const auto committed = recordMutationTransaction([&] {
			return commitInlineField();
		});
		if (committed != ApplyResult::Failed) {
			refreshAfterInlineFieldCommit(committed);
		}
	}
	_structuralSelection = std::move(selection);
	_boundarySelectionOrigin = std::move(origin);
	const auto keyboardSelection = !_structuralSelection.empty()
		&& _boundarySelectionOrigin.has_value();
	if (keyboardSelection && !_keyboardStructuralSelectionActive) {
		grabKeyboard();
		_keyboardStructuralSelectionActive = true;
	} else if (!keyboardSelection && _keyboardStructuralSelectionActive) {
		releaseKeyboard();
		_keyboardStructuralSelectionActive = false;
	}
	notifyToolbarStateChanged();
}

bool Widget::broaderSelectionHasSelectedText() const {
	const auto &nodes = _state->textNodes();
	const auto &page = _state->richPage();
	const auto hasTextSelection = !_selection.empty()
		&& _selectionEndpoints.from.valid()
		&& _selectionEndpoints.to.valid();
	const auto normalizedSelection = hasTextSelection
		? Markdown::NormalizeSelection(_selection)
		: Markdown::MarkdownArticleSelection();
	for (auto ordinal = 0, count = int(nodes.size()); ordinal != count;
			++ordinal) {
		const auto &descriptor = nodes[ordinal];
		if (descriptor.mode != State::FieldMode::Rich) {
			continue;
		}
		const auto current = RichTextFromPath(page, descriptor.leaf);
		if (!current || current->text.text.isEmpty()) {
			continue;
		}
		const auto segmentIndex = segmentIndexForEditableOrdinal(ordinal);
		if (segmentIndex < 0
			|| (editableOrdinalForSegment(segmentIndex) != ordinal)) {
			continue;
		}
		const auto length = int(current->text.text.size());
		if (!_structuralSelection.empty()
			&& LeafSelectedStructurally(
				page,
				descriptor.leaf,
				_structuralSelection)
			&& (length > 0)) {
			return true;
		}
		if (!hasTextSelection
			|| (normalizedSelection.from.segment > segmentIndex)
			|| (normalizedSelection.to.segment < segmentIndex)) {
			continue;
		}
		auto from = 0;
		auto till = length;
		if (normalizedSelection.from.segment == segmentIndex) {
			from = normalizedSelection.from.offset;
		}
		if (normalizedSelection.to.segment == segmentIndex) {
			till = normalizedSelection.to.offset;
		}
		from = std::clamp(from, 0, length);
		till = std::clamp(till, from, length);
		if (from < till) {
			return true;
		}
	}
	return false;
}

std::vector<State::TextNodeSpan> Widget::broaderSelectionTextSpans() const {
	auto result = std::vector<State::TextNodeSpan>();
	const auto &nodes = _state->textNodes();
	const auto &page = _state->richPage();
	const auto hasTextSelection = !_selection.empty()
		&& _selectionEndpoints.from.valid()
		&& _selectionEndpoints.to.valid();
	const auto normalizedSelection = hasTextSelection
		? Markdown::NormalizeSelection(_selection)
		: Markdown::MarkdownArticleSelection();
	result.reserve(nodes.size());
	for (auto ordinal = 0, count = int(nodes.size()); ordinal != count;
			++ordinal) {
		const auto &descriptor = nodes[ordinal];
		if (descriptor.mode != State::FieldMode::Rich) {
			continue;
		}
		const auto current = RichTextFromPath(page, descriptor.leaf);
		if (!current || current->text.text.isEmpty()) {
			continue;
		}
		const auto segmentIndex = segmentIndexForEditableOrdinal(ordinal);
		if (segmentIndex < 0
			|| (editableOrdinalForSegment(segmentIndex) != ordinal)) {
			continue;
		}
		const auto length = int(current->text.text.size());
		if (!_structuralSelection.empty()
			&& LeafSelectedStructurally(
				page,
				descriptor.leaf,
				_structuralSelection)) {
			result.push_back({
				.leaf = descriptor.leaf,
				.from = 0,
				.till = length,
			});
			continue;
		}
		if (!hasTextSelection
			|| (normalizedSelection.from.segment > segmentIndex)
			|| (normalizedSelection.to.segment < segmentIndex)) {
			continue;
		}
		auto from = 0;
		auto till = length;
		if (normalizedSelection.from.segment == segmentIndex) {
			from = normalizedSelection.from.offset;
		}
		if (normalizedSelection.to.segment == segmentIndex) {
			till = normalizedSelection.to.offset;
		}
		from = std::clamp(from, 0, length);
		till = std::clamp(till, from, length);
		if (from < till) {
			result.push_back({
				.leaf = descriptor.leaf,
				.from = from,
				.till = till,
			});
		}
	}
	return result;
}

std::vector<State::BlockPath> Widget::broaderSelectionMediaBlocks() const {
	auto result = std::vector<State::BlockPath>();
	if (_structuralSelection.empty()) {
		return result;
	}
	const auto &page = _state->richPage();
	EnumerateBlockPaths(
		page,
		StateBlockContainerPath(),
		[&](const StateBlockPath &path, const RichPage::Block &block) {
			if (BlockSelectedStructurally(path, _structuralSelection)
				&& MediaBlockSupportsSpoiler(block)) {
				result.push_back(path);
			}
		});
	return result;
}

PreparedEditSelection Widget::structuralSelectionForTextSelection() const {
	if (_selection.empty()
		|| !_selectionEndpoints.from.valid()
		|| !_selectionEndpoints.to.valid()) {
		return {};
	}
	const auto normalized = Markdown::NormalizeSelection(_selection);
	if (normalized.from.segment == normalized.to.segment) {
		return {};
	}
	const auto fromOrdinal = editableOrdinalForSegment(
		normalized.from.segment);
	const auto toOrdinal = editableOrdinalForSegment(normalized.to.segment);
	const auto count = _state->textNodeCount();
	if (fromOrdinal < 0
		|| toOrdinal < 0
		|| fromOrdinal >= count
		|| toOrdinal >= count) {
		return {};
	}
	const auto &nodes = _state->textNodes();
	const auto &from = nodes[fromOrdinal].leaf;
	const auto &to = nodes[toOrdinal].leaf;
	if ((from.kind == StateLeafKind::ListItemText)
		&& (to.kind == StateLeafKind::ListItemText)
		&& (from.block == to.block)) {
		const auto range = NormalizeIntegerRange(
			from.listItemIndex,
			to.listItemIndex);
		if (range.empty()) {
			return {};
		}
		return {
			.kind = PreparedEditSelectionKind::ListItems,
			.listItems = {
				.block = ToPreparedBlockPath(from.block),
				.from = range.from,
				.till = range.till,
			},
		};
	}
	return LiftedBlockSelection(
		ToPreparedBlockPath(from.block),
		ToPreparedBlockPath(to.block));
}

void Widget::clearSelection() {
	const auto changed = !_selection.empty()
		|| _selectionEndpoints.from.valid()
		|| _selectionEndpoints.to.valid()
		|| hasStructuralSelection()
		|| _articleSelectionDrag.active;
	_selection = {};
	_selectionEndpoints = {};
	setStructuralSelection({});
	finishArticleSelection();
	if (changed) {
		update();
		notifyToolbarStateChanged();
	}
}

void Widget::clearTextSelection() {
	const auto changed = !_selection.empty()
		|| _selectionEndpoints.from.valid()
		|| _selectionEndpoints.to.valid()
		|| (_articleSelectionDrag.active
			&& _articleSelectionDrag.mode == DragSelectionMode::Text);
	_selection = {};
	_selectionEndpoints = {};
	if (_articleSelectionDrag.mode == DragSelectionMode::Text) {
		finishArticleSelection();
	} else {
		_articleSelectionDrag.textSegment = -1;
		_articleSelectionDrag.textOffset = 0;
	}
	if (changed) {
		update();
		notifyToolbarStateChanged();
	}
}

void Widget::clearStructuralSelection() {
	const auto changed = hasStructuralSelection()
		|| (_articleSelectionDrag.active
			&& _articleSelectionDrag.mode == DragSelectionMode::Structural);
	setStructuralSelection({});
	if (_articleSelectionDrag.mode == DragSelectionMode::Structural) {
		finishArticleSelection();
	}
	if (changed) {
		update();
		notifyToolbarStateChanged();
	}
}

bool Widget::hasStructuralSelection() const {
	return !_structuralSelection.empty();
}

void Widget::startArticleSelection(
		QPoint pressPoint,
		QPoint globalPressPoint,
		const Markdown::MarkdownArticleHitTestResult &hit,
		const PreparedEditHit &editHit,
		bool fromField,
		bool startedBelow) {
	const auto isTextHit = hit.valid()
		&& !hit.codeHeaderCopy
		&& hit.direct
		&& _article->segmentIsText(hit.segmentIndex);
	if (isTextHit) {
		clearStructuralSelection();
	} else {
		clearTextSelection();
		clearStructuralSelection();
	}
	_articleSelectionDrag = {
		.active = true,
		.fromField = fromField,
		.startedBelow = startedBelow,
		.codeHeader = hit.codeHeaderCopy,
		.pressPoint = pressPoint,
		.globalPressPoint = globalPressPoint,
		.anchorHit = editHit,
		.textSegment = -1,
		.textOffset = 0,
		.operation = ArticleSelectionOperation::GrowSelection,
		.mode = DragSelectionMode::None,
	};
	if (!isTextHit) {
		const auto mathFormulaHit = editHit.leaf
			&& (editHit.leaf->kind == PreparedEditLeafKind::MathFormula);
		if (editHit.valid()
			&& !mathFormulaHit
			&& !hit.codeHeaderCopy
			&& !startedBelow) {
			_articleSelectionDrag.mode = DragSelectionMode::Structural;
		}
		return;
	}
	const auto offset = _article->selectionOffsetFromHit(
		hit,
		TextSelectType::Letters);
	_articleSelectionDrag.textSegment = hit.segmentIndex;
	_articleSelectionDrag.textOffset = offset;
	_articleSelectionDrag.mode = DragSelectionMode::Text;
	_selection = {
		{ hit.segmentIndex, offset },
		{ hit.segmentIndex, offset },
	};
	_selectionEndpoints = {
		.from = Markdown::MakeSelectionEndpoint(hit),
		.to = Markdown::MakeSelectionEndpoint(hit),
	};
	updateHasSelection();
	update();
}

bool Widget::startSelectionDragFromExistingState(
		QPoint pressPoint,
		QPoint globalPressPoint,
		const PreparedEditHit &editHit,
		bool fromField) {
	auto drag = ArticleSelectionDrag{
		.active = true,
		.fromField = fromField,
		.startedBelow = false,
		.codeHeader = false,
		.pressPoint = pressPoint,
		.globalPressPoint = globalPressPoint,
		.anchorHit = editHit,
		.textSegment = -1,
		.textOffset = 0,
		.operation = ArticleSelectionOperation::DragSelection,
		.mode = DragSelectionMode::None,
	};
	if (fromField) {
		if (_settingField
			|| _field->isHidden()
			|| (_activeSegmentIndex < 0)
			|| (_state->activeFieldMode() != State::FieldMode::Rich)) {
			return false;
		}
		const auto sourceLeaf = _state->activeLeafPath();
		const auto preparedSource = _state->activePreparedLeafSource();
		if (!sourceLeaf || !preparedSource) {
			return false;
		}
		const auto full = ConvertEditorTagsToRichText(
			_field->getTextWithAppliedMarkdown());
		const auto cursor = _field->textCursor();
		if (!cursor.hasSelection()) {
			return false;
		}
		const auto length = int(full.text.size());
		auto from = richOffsetForFieldOffset(full, cursor.selectionStart());
		auto till = richOffsetForFieldOffset(full, cursor.selectionEnd());
		from = std::clamp(from, 0, length);
		till = std::clamp(till, from, length);
		if (from >= till) {
			return false;
		}
		drag.textSegment = _activeSegmentIndex;
		drag.textOffset = std::clamp(
			cursor.position(),
			0,
			int(_field->getLastText().size()));
		drag.mode = DragSelectionMode::Text;
		drag.inlineSource = TextNodeSpan{
			.leaf = *sourceLeaf,
			.from = from,
			.till = till,
		};
		drag.sourceLeaf = *preparedSource;
		drag.sourceSegment = _activeSegmentIndex;
		drag.sourceFrom = from;
		drag.sourceTo = till;
		_articleSelectionDrag = std::move(drag);
		return true;
	}
	if (!_structuralSelection.empty()
		&& ((_structuralSelection.kind == PreparedEditSelectionKind::Blocks)
			|| (_structuralSelection.kind
				== PreparedEditSelectionKind::ListItems))) {
		drag.mode = DragSelectionMode::Structural;
		drag.structuralSource = _structuralSelection;
		_articleSelectionDrag = std::move(drag);
		return true;
	}
	const auto selection = Markdown::NormalizeSelection(_selection);
	if (selection.empty()
		|| !_selectionEndpoints.from.valid()
		|| !_selectionEndpoints.to.valid()
		|| (selection.from.segment != selection.to.segment)
		|| !_article->segmentIsText(selection.from.segment)
		|| !editHit.leaf) {
		return false;
	}
	const auto ordinal = editableOrdinalForSegment(selection.from.segment);
	const auto &nodes = _state->textNodes();
	if ((ordinal < 0) || (ordinal >= int(nodes.size()))) {
		return false;
	}
	drag.textSegment = selection.from.segment;
	drag.textOffset = selection.from.offset;
	drag.mode = DragSelectionMode::Text;
	drag.inlineSource = TextNodeSpan{
		.leaf = nodes[ordinal].leaf,
		.from = selection.from.offset,
		.till = selection.to.offset,
	};
	drag.sourceLeaf = *editHit.leaf;
	drag.sourceSegment = selection.from.segment;
	drag.sourceFrom = selection.from.offset;
	drag.sourceTo = selection.to.offset;
	_articleSelectionDrag = std::move(drag);
	return true;
}

void Widget::updateArticleSelection(
		QPoint articlePoint,
		const Markdown::MarkdownArticleHitTestResult &hit,
		const PreparedEditHit &editHit) {
	if (!_articleSelectionDrag.active
		|| (_articleSelectionDrag.operation
			!= ArticleSelectionOperation::GrowSelection)) {
		return;
	}
	const auto dragSegment = _articleSelectionDrag.textSegment;
	const auto originalMathFormulaHit = [&] {
		return _articleSelectionDrag.anchorHit.leaf
			&& (_articleSelectionDrag.anchorHit.leaf->kind
				== Markdown::PreparedEditLeafKind::MathFormula)
			&& editHit.leaf
			&& (*editHit.leaf == *_articleSelectionDrag.anchorHit.leaf);
	};
	const auto directOriginalTextHit = [&] {
		return (dragSegment >= 0)
			&& hit.valid()
			&& hit.direct
			&& (hit.segmentIndex == dragSegment)
			&& _article->segmentIsText(hit.segmentIndex);
	};
	const auto directOriginalEditableHit = [&] {
		return ((dragSegment >= 0)
			&& hit.valid()
			&& hit.direct
			&& (hit.segmentIndex == dragSegment)
			&& _article->segmentIsEditable(hit.segmentIndex))
			|| originalMathFormulaHit();
	};
	const auto updateTextSelection = [&](bool forceUpdate) {
		const auto offset = _article->selectionOffsetFromHit(
			hit,
			TextSelectType::Letters);
		const auto adjusted = _article->adjustSelection(
			dragSegment,
			TextSelection(
				uint16(std::clamp(
					std::min(_articleSelectionDrag.textOffset, offset),
					0,
					0xFFFF)),
				uint16(std::clamp(
					std::max(_articleSelectionDrag.textOffset, offset),
					0,
					0xFFFF))),
			TextSelectType::Letters);
		const auto selection = Markdown::NormalizeSelection({
			{ dragSegment, adjusted.from },
			{ dragSegment, adjusted.to },
		});
		const auto endpoints = Markdown::MarkdownArticleSelectionEndpoints{
			.from = _selectionEndpoints.from.valid()
				? _selectionEndpoints.from
				: Markdown::MarkdownArticleSelectionEndpoint{
					dragSegment,
					false },
			.to = Markdown::MakeSelectionEndpoint(hit),
		};
		const auto endpointsChanged
			= (_selectionEndpoints.from.segment != endpoints.from.segment)
			|| (_selectionEndpoints.from.direct != endpoints.from.direct)
			|| (_selectionEndpoints.to.segment != endpoints.to.segment)
			|| (_selectionEndpoints.to.direct != endpoints.to.direct);
		if (_selection != selection || endpointsChanged || forceUpdate) {
			_selection = selection;
			_selectionEndpoints = endpoints;
			updateHasSelection();
			update();
		} else {
			_selectionEndpoints = endpoints;
		}
	};
	const auto clearFieldSelection = [&] {
		if (!_articleSelectionDrag.fromField) {
			return;
		}
		auto cursor = _field->textCursor();
		if (!_articleSelectionDrag.interruptedFieldAnchor) {
			_articleSelectionDrag.interruptedFieldAnchor = cursor.anchor();
		}
		if (!cursor.hasSelection()) {
			return;
		}
		cursor.clearSelection();
		_field->setTextCursor(cursor);
	};
	if (_articleSelectionDrag.mode == DragSelectionMode::Structural) {
		if (directOriginalTextHit()) {
			const auto forceUpdate = !_structuralSelection.empty();
			setStructuralSelection({});
			_articleSelectionDrag.mode = DragSelectionMode::Text;
			updateTextSelection(forceUpdate);
			return;
		}
		if (directOriginalEditableHit()) {
			const auto changed = !_structuralSelection.empty();
			setStructuralSelection({});
			_articleSelectionDrag.mode = DragSelectionMode::None;
			if (changed) {
				update();
			}
			return;
		}
		const auto selection = structuralSelectionFromHits(
			_articleSelectionDrag.anchorHit,
			editHit);
		if (_structuralSelection != selection) {
			setStructuralSelection(selection);
			update();
		}
		return;
	} else if (_articleSelectionDrag.mode == DragSelectionMode::None) {
		if (directOriginalEditableHit()) {
			return;
		}
		if (!editHit.valid()
			|| (_articleSelectionDrag.startedBelow
				&& articlePoint.y() >= _articleHeight)) {
			return;
		}
		_articleSelectionDrag.mode = DragSelectionMode::Structural;
		const auto selection = structuralSelectionFromHits(
			_articleSelectionDrag.anchorHit,
			editHit);
		if (_structuralSelection != selection) {
			setStructuralSelection(selection);
			update();
		}
		return;
	}
	if (_articleSelectionDrag.mode != DragSelectionMode::Text) {
		return;
	}
	if (directOriginalTextHit()) {
		updateTextSelection(false);
		return;
	}
	const auto widgetPoint = articlePoint + articleTopLeft();
	if (_articleSelectionDrag.fromField
		&& !_field->isHidden()
		&& !_articleSelectionDrag.anchorHit.tableCell
		&& (widgetPoint.y() >= _field->y())
		&& (widgetPoint.y() < _field->y() + _field->height())) {
		return;
	}
	const auto selection = structuralSelectionFromHits(
		_articleSelectionDrag.anchorHit,
		editHit);
	const auto changed = !_selection.empty()
		|| _selectionEndpoints.from.valid()
		|| _selectionEndpoints.to.valid()
		|| (_structuralSelection != selection);
	_selection = {};
	_selectionEndpoints = {};
	setStructuralSelection(selection);
	_articleSelectionDrag.mode = DragSelectionMode::Structural;
	clearFieldSelection();
	if (changed) {
		update();
	}
}

void Widget::updateArticleDropTarget(QPoint articlePoint) {
	if (!_articleSelectionDrag.active
		|| (_articleSelectionDrag.operation
			!= ArticleSelectionOperation::DragSelection)) {
		clearArticleDropTarget();
		return;
	}
	const auto structuralSource = _articleSelectionDrag.structuralSource;
	auto location = (_articleSelectionDrag.mode == DragSelectionMode::Structural
			&& structuralSource)
		? _article->editStructuralDropTarget(articlePoint, *structuralSource)
		: _articleSelectionDrag.fromField
		? _article->editBlockDropTarget(articlePoint)
		: _article->editDropTarget(articlePoint);
	auto supported = false;
	if (location.valid()) {
		const auto &target = *location.target;
		switch (_articleSelectionDrag.mode) {
		case DragSelectionMode::Structural:
			if (const auto source = structuralSource) {
				if (const auto block = std::get_if<PreparedEditBlockDropTarget>(
						&target)) {
					supported = (source->kind
						== PreparedEditSelectionKind::Blocks);
					if (supported
						&& (block->container == source->blocks.container)
						&& (block->insertIndex >= source->blocks.from)
						&& (block->insertIndex <= source->blocks.till)) {
						supported = false;
					} else if (supported
						&& PreparedContainerNestedInSelection(
							block->container,
							*source)) {
						supported = false;
					}
				} else if (const auto list
					= std::get_if<PreparedEditListItemDropTarget>(&target)) {
					supported = (source->kind
						== PreparedEditSelectionKind::ListItems);
					if (supported
						&& SamePreparedEditBlockPath(
							list->block,
							source->listItems.block)
						&& (list->insertIndex >= source->listItems.from)
						&& (list->insertIndex <= source->listItems.till)) {
						supported = false;
					} else if (supported
						&& PreparedBlockPathInSelection(
							list->block,
							*source)) {
						supported = false;
					}
				}
			}
			break;
		case DragSelectionMode::Text:
			if (const auto text = std::get_if<PreparedEditTextDropTarget>(
					&target)) {
				supported = (text->leaf.kind != PreparedEditLeafKind::MathFormula);
				if (supported
					&& _articleSelectionDrag.sourceLeaf
					&& (*_articleSelectionDrag.sourceLeaf == text->leaf)
					&& (text->offset >= _articleSelectionDrag.sourceFrom)
					&& (text->offset <= _articleSelectionDrag.sourceTo)) {
					supported = false;
				}
			} else {
				supported = std::holds_alternative<PreparedEditBlockDropTarget>(
					target);
			}
			break;
		case DragSelectionMode::None:
			break;
		}
	}
	if (!supported) {
		location = {};
	}
	const auto oldRect = _articleSelectionDrag.indicatorRect;
	_articleSelectionDrag.dropTarget = location.valid()
		? location.target
		: std::nullopt;
	_articleSelectionDrag.indicatorRect = location.valid()
		? location.indicatorRect
		: QRect();
	if (oldRect != _articleSelectionDrag.indicatorRect) {
		update();
	}
}

void Widget::clearArticleDropTarget() {
	const auto oldRect = _articleSelectionDrag.indicatorRect;
	_articleSelectionDrag.dropTarget = std::nullopt;
	_articleSelectionDrag.indicatorRect = QRect();
	if (!oldRect.isEmpty()) {
		update();
	}
}

void Widget::updateExternalDropTarget(QPoint articlePoint) {
	auto target = std::optional<Markdown::PreparedEditBlockDropTarget>();
	auto rect = QRect();
	const auto location = _article->editBlockDropTarget(articlePoint);
	if (location.valid()) {
		if (const auto block
			= std::get_if<Markdown::PreparedEditBlockDropTarget>(
				&*location.target)) {
			target = *block;
			rect = location.indicatorRect;
		}
	}
	const auto oldRect = _externalMediaDrag.indicatorRect;
	_externalMediaDrag.dropTarget = target;
	_externalMediaDrag.indicatorRect = rect;
	if (oldRect != rect) {
		update();
	}
}

void Widget::clearExternalDropTarget() {
	const auto oldRect = _externalMediaDrag.indicatorRect;
	_externalMediaDrag.dropTarget = std::nullopt;
	_externalMediaDrag.indicatorRect = QRect();
	if (!oldRect.isEmpty()) {
		update();
	}
}

void Widget::dragEnterEvent(QDragEnterEvent *e) {
	if (!_article || !IsAcceptableDropMedia(e->mimeData())) {
		clearExternalDropTarget();
		e->ignore();
		return;
	}
	updateExternalDropTarget(e->pos() - articleTopLeft());
	e->setDropAction(Qt::CopyAction);
	e->accept();
}

void Widget::dragMoveEvent(QDragMoveEvent *e) {
	if (!_article || !IsAcceptableDropMedia(e->mimeData())) {
		clearExternalDropTarget();
		e->ignore();
		return;
	}
	updateExternalDropTarget(e->pos() - articleTopLeft());
	e->setDropAction(Qt::CopyAction);
	e->accept();
}

void Widget::dragLeaveEvent(QDragLeaveEvent *e) {
	clearExternalDropTarget();
}

void Widget::dropEvent(QDropEvent *e) {
	const auto clear = gsl::finally([&] { clearExternalDropTarget(); });
	if (!_article
		|| !_applyPreparedMedia
		|| !IsAcceptableDropMedia(e->mimeData())) {
		return;
	}
	updateExternalDropTarget(e->pos() - articleTopLeft());
	const auto target = _externalMediaDrag.dropTarget;
	if (!target) {
		return;
	}
	auto list = PreparedMediaFromClipboard(
		e->mimeData(),
		SessionPremium(_session));
	if (!list) {
		return;
	}
	e->setDropAction(Qt::CopyAction);
	e->accept();
	auto paste = PreparedMediaPasteTarget{ .blockDrop = *target };
	crl::on_main(this, [=, list = std::move(*list)]() mutable {
		_applyPreparedMedia(
			not_null<Widget*>(this),
			std::move(list),
			std::move(paste));
	});
}

Ui::ElasticScroll *Widget::selectionScrollArea() const {
	for (auto parent = parentWidget(); parent; parent = parent->parentWidget()) {
		if (const auto scroll = dynamic_cast<Ui::ElasticScroll*>(parent)) {
			return scroll;
		}
	}
	return nullptr;
}

bool Widget::searchBlockedByLayer() const {
	const auto editorWindow = dynamic_cast<Window*>(window());
	return editorWindow && editorWindow->isLayerShown();
}

bool Widget::articleSelectionAutoScrollActive() const {
	return _articleSelectionDrag.active
		&& _articleSelectionDrag.dragStarted
		&& ((_articleSelectionDrag.operation
				== ArticleSelectionOperation::GrowSelection)
			|| (_articleSelectionDrag.operation
				== ArticleSelectionOperation::DragSelection));
}

void Widget::updateArticleSelectionAutoScroll(QPoint widgetPoint) {
	if (!articleSelectionAutoScrollActive() || !selectionScrollArea()) {
		_selectScroll.cancel();
		return;
	}
	_selectScroll.checkDeltaScroll(
		widgetPoint,
		_visibleRange.top,
		_visibleRange.bottom);
}

bool Widget::restoreFieldFromBoundaryOrigin() {
	if (!_boundarySelectionOrigin) {
		return false;
	}
	const auto expected = _boundarySelectionOrigin->leafSelection;
	const auto ordinal = _state->textOrdinalForLeafPath(expected.leaf);
	if (ordinal < 0) {
		return false;
	}
	activateTextOrdinal(
		ordinal,
		expected.anchorOffset,
		expected.cursorOffset,
		ActivateReveal::Reveal);
	const auto restored = [&]() {
		if (hasStructuralSelection()) {
			return false;
		}
		const auto viewState = captureHistoryViewState();
		return viewState.leafSelection
			&& (*viewState.leafSelection == expected);
	}();
	if (restored) {
		setStructuralSelection({});
		_boundarySelectionOrigin = std::nullopt;
	}
	return restored;
}

void Widget::revealStructuralSelectionEdge(bool forward) {
	if (!_article || _structuralSelection.empty()) {
		return;
	}
	const auto ordinal = StructuralSelectionEdgeTextOrdinal(
		*_state,
		_structuralSelection,
		forward);
	const auto scroll = selectionScrollArea();
	if (!ordinal || !scroll) {
		return;
	}
	const auto segmentIndex = segmentIndexForEditableOrdinal(*ordinal);
	if (segmentIndex < 0
		|| (editableOrdinalForSegment(segmentIndex) != *ordinal)) {
		return;
	}
	auto rect = _article->segmentRect(segmentIndex);
	if (!rect.isValid() || rect.isEmpty()) {
		return;
	}
	if (const auto inner = scroll->widget()) {
		rect.translate(articleTopLeft());
		rect.moveTopLeft(mapTo(inner, rect.topLeft()));
		scrollRangeToMakeVisible(scroll, rect.y(), rect.y() + rect.height());
	}
}

void Widget::updateArticleSelectionDragAtArticlePoint(
		QPoint articlePoint,
		const Markdown::MarkdownArticleHitTestResult &hit,
		const PreparedEditHit &editHit) {
	if (!_articleSelectionDrag.active
		|| !_articleSelectionDrag.dragStarted
		|| !_article) {
		return;
	}
	switch (_articleSelectionDrag.operation) {
	case ArticleSelectionOperation::GrowSelection:
		updateArticleSelection(articlePoint, hit, editHit);
		return;
	case ArticleSelectionOperation::DragSelection:
		updateArticleDropTarget(articlePoint);
		return;
	case ArticleSelectionOperation::None:
		_selectScroll.cancel();
		return;
	}
}

void Widget::updateArticleSelectionDragAtWidgetPoint(QPoint widgetPoint) {
	const auto articlePoint = widgetPoint - articleTopLeft();
	const auto hit = _article->hitTest(
		articlePoint,
		Ui::Text::StateRequest::Flag::LookupSymbol);
	const auto editHit = _article->editHitTest(articlePoint);
	updateArticleSelectionDragAtArticlePoint(articlePoint, hit, editHit);
}

void Widget::updateArticleSelectionDragFromCursor() {
	if (!articleSelectionAutoScrollActive()) {
		_selectScroll.cancel();
		return;
	}
	const auto widgetPoint = mapFromGlobal(QCursor::pos());
	updateArticleSelectionDragAtWidgetPoint(widgetPoint);
	updateArticleSelectionAutoScroll(widgetPoint);
}

void Widget::finishArticleSelection() {
	const auto repaint = !_articleSelectionDrag.indicatorRect.isEmpty();
	_selectScroll.cancel();
	_articleSelectionDrag = {};
	if (repaint) {
		update();
	}
}

void Widget::applyStructuralSelectionDrop() {
	if (!_articleSelectionDrag.structuralSource
		|| !_articleSelectionDrag.dropTarget) {
		return;
	}
	const auto clearOverlay = gsl::finally([&] {
		clearArticleDropTarget();
	});
	const auto selection = *_articleSelectionDrag.structuralSource;
	const auto target = *_articleSelectionDrag.dropTarget;
	recordMutationTransaction([&] {
		const auto hadVisibleField = !_field->isHidden();
		const auto source = hadVisibleField
			? _state->activePreparedLeafSource()
			: std::optional<PreparedEditLeafSource>();
		auto committed = ApplyResult::Unchanged;
		if (hadVisibleField) {
			committed = commitInlineField();
			if (committed == ApplyResult::Failed) {
				return MutationTransactionResult{
					.committed = committed,
					.failed = true,
				};
			}
		}
		_pendingOrdinal = -1;
		_pendingCursorOffset = 0;
		hideInlineField();
		clearInlineFieldEditSession();
		const auto moved = _state->moveStructuralSelectionToDropTarget(
			selection,
			target);
		if (moved.result == ApplyResult::Failed) {
			showLastLimitToast();
			if (hadVisibleField) {
				refreshAfterInlineFieldCommit(committed, source);
			}
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		} else if (moved.result == ApplyResult::Unchanged) {
			if (hadVisibleField) {
				refreshAfterInlineFieldCommit(committed, source);
			}
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		refreshPreparedContent();
		switch (moved.destination.action) {
		case State::BoundaryTarget::Action::StructuralSelection:
			_boundarySelectionOrigin = std::nullopt;
			_selection = {};
			_selectionEndpoints = {};
			setStructuralSelection(moved.destination.structuralSelection);
			update();
			break;
		case State::BoundaryTarget::Action::Text:
			activateTextOrdinal(moved.destination.textOrdinal, 0);
			break;
		case State::BoundaryTarget::Action::None:
		case State::BoundaryTarget::Action::RemoveActiveOwner: {
			const auto ordinal = _state->activeTextOrdinal();
			if (ordinal >= 0 && ordinal < _state->textNodeCount()) {
				activateTextOrdinal(ordinal, 0);
			} else {
				activateInitialNode();
			}
		} break;
		}
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

void Widget::applyInlineSelectionDrop() {
	if (!_articleSelectionDrag.inlineSource
		|| !_articleSelectionDrag.dropTarget) {
		return;
	}
	const auto clearOverlay = gsl::finally([&] {
		clearArticleDropTarget();
	});
	const auto target = *_articleSelectionDrag.dropTarget;
	recordMutationTransaction([&] {
		const auto restoreField = !_field->isHidden();
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
		if (restoreField) {
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
			clearInlineFieldEditSession(true);
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
		const auto sourceSpans = _articleSelectionDrag.fromField
			? (_articleSelectionDrag.sourceLeaf
				? _state->resolveTextSpansForPreparedLeafRange(
					*_articleSelectionDrag.sourceLeaf,
					_articleSelectionDrag.sourceFrom,
					_articleSelectionDrag.sourceTo)
				: std::vector<TextNodeSpan>())
			: std::vector<TextNodeSpan>{ *_articleSelectionDrag.inlineSource };
		if (sourceSpans.empty()) {
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		const auto moved = _state->moveTextSelectionToDropTarget(
			sourceSpans,
			target);
		if (moved.result == ApplyResult::Failed) {
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		} else if (moved.result == ApplyResult::Unchanged) {
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		restore = false;
		refreshPreparedContent();
		const auto ordinal = moved.destinationLeaf
			? _state->textOrdinalForLeafPath(*moved.destinationLeaf)
			: _state->activeTextOrdinal();
		if (ordinal >= 0) {
			activateTextOrdinal(
				ordinal,
				moved.selectionFrom,
				moved.selectionTo);
		} else {
			activateInitialNode();
		}
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

bool Widget::handleStructuralSelectionKey(QKeyEvent *e) {
	if (!hasStructuralSelection()) {
		return false;
	}
	const auto key = e->key();
	if (key == Qt::Key_Escape) {
		clearStructuralSelection();
		e->accept();
		return true;
	}
	const auto modifiers = e->modifiers()
		& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
	if (modifiers == Qt::ShiftModifier) {
		const auto vertical = (key == Qt::Key_Up)
			|| (key == Qt::Key_Down)
			|| (key == Qt::Key_PageUp)
			|| (key == Qt::Key_PageDown);
		if (!vertical) {
			return false;
		}
		if (adjustStructuralSelectionFromKeyboard(
				(key == Qt::Key_Down) || (key == Qt::Key_PageDown),
				(key == Qt::Key_PageUp) || (key == Qt::Key_PageDown))) {
			e->accept();
			return true;
		}
		e->accept();
		return true;
	}
	if (modifiers != Qt::NoModifier) {
		return false;
	}
	const auto forward = (key == Qt::Key_Delete);
	if (!forward && key != Qt::Key_Backspace) {
		return false;
	}
	if (_state->canRemoveStructuralSelection(_structuralSelection)) {
		removeStructuralSelectionAndReposition(forward);
	}
	e->accept();
	return true;
}

void Widget::removeStructuralSelectionAndReposition(bool forward) {
	const auto origin = [&]() -> std::optional<BoundarySelectionOrigin> {
		if (_boundarySelectionOrigin
			&& _boundarySelectionOrigin->forward == forward) {
			return _boundarySelectionOrigin;
		}
		return std::nullopt;
	}();
	const auto target = removeCurrentStructuralSelection(forward);
	if (hasStructuralSelection()) {
		return;
	}
	if (origin) {
		const auto ordinal = _state->textOrdinalForLeafPath(
			origin->leafSelection.leaf);
		if (ordinal >= 0 && ordinal < _state->textNodeCount()) {
			activateTextOrdinal(
				ordinal,
				origin->leafSelection.anchorOffset,
				origin->leafSelection.cursorOffset);
			return;
		}
	}
	if (target) {
		if (forward) {
			activateTextOrdinal(*target, 0);
		} else {
			activateTextOrdinalAtEnd(*target);
		}
	} else {
		activateInitialNode();
	}
}

std::optional<int> Widget::removeCurrentStructuralSelection(bool forward) {
	if (!hasStructuralSelection()) {
		return std::nullopt;
	}
	const auto selection = _structuralSelection;
	auto target = std::optional<int>();
	const auto result = recordMutationTransaction([&] {
		const auto committed = commitInlineField();
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
		target = _state->removeStructuralSelection(selection, forward);
		clearSelection();
		refreshPreparedContent();
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
	if (result.failed) {
		return std::nullopt;
	}
	return target;
}

PreparedEditSelection Widget::structuralSelectionFromHits(
		const PreparedEditHit &anchor,
		const PreparedEditHit &focus) const {
	const auto anchorOwner = StructuralOwnerFromHit(anchor);
	const auto focusOwner = StructuralOwnerFromHit(focus);
	if (!anchorOwner.valid() || !focusOwner.valid()) {
		return {};
	}
	const auto anchorCell = TableCellFromOwner(anchorOwner);
	const auto focusCell = TableCellFromOwner(focusOwner);
	if (anchorCell && focusCell) {
		const auto range = TableRangesUnion(
			TableRangeFromCell(*anchorCell),
			TableRangeFromCell(*focusCell));
		if (!range.empty()
			&& TableRangeContainsCell(range, *anchorCell)
			&& TableRangeContainsCell(range, *focusCell)) {
			return {
				.kind = PreparedEditSelectionKind::TableCells,
				.tableCells = range,
			};
		}
	}
	const auto anchorRow = TableRowFromOwner(anchorOwner);
	const auto focusRow = TableRowFromOwner(focusOwner);
	if (anchorRow
		&& focusRow
		&& SamePreparedEditBlockPath(anchorRow->block, focusRow->block)) {
		const auto range = NormalizeIntegerRange(
			anchorRow->tableRowIndex,
			focusRow->tableRowIndex);
		if (!range.empty()) {
			return {
				.kind = PreparedEditSelectionKind::TableRows,
				.tableRows = {
					.block = anchorRow->block,
					.from = range.from,
					.till = range.till,
				},
			};
		}
	}
	const auto anchorListItem = ListItemFromOwner(anchorOwner);
	const auto focusListItem = ListItemFromOwner(focusOwner);
	if (anchorListItem
		&& focusListItem
		&& SamePreparedEditBlockPath(
			anchorListItem->block,
			focusListItem->block)) {
		const auto range = NormalizeIntegerRange(
			anchorListItem->listItemIndex,
			focusListItem->listItemIndex);
		if (!range.empty()) {
			return {
				.kind = PreparedEditSelectionKind::ListItems,
				.listItems = {
					.block = anchorListItem->block,
					.from = range.from,
					.till = range.till,
				},
			};
		}
	}
	const auto anchorBlock = BlockPathFromOwner(anchorOwner);
	const auto focusBlock = BlockPathFromOwner(focusOwner);
	if (!anchorBlock || !focusBlock) {
		return {};
	}
	if (ComparePreparedEditBlockContainerPaths(
			anchorBlock->container,
			focusBlock->container) == 0) {
		const auto blockSelection = BlockSelectionFromIndexes(
			anchorBlock->container,
			anchorBlock->index,
			focusBlock->index);
		if (!blockSelection.empty()) {
			return blockSelection;
		}
	}
	const auto listItemsFromChildren = ListItemSelectionFromSources(
		ListItemSourcesFromOwner(anchorOwner, anchorBlock),
		ListItemSourcesFromOwner(focusOwner, focusBlock));
	const auto liftedBlockSelection = LiftedBlockSelection(
		*anchorBlock,
		*focusBlock);
	if (IsBlockOwner(anchorOwner)
		&& IsBlockOwner(focusOwner)
		&& !liftedBlockSelection.empty()
		&& !IsMultiListItemSelection(listItemsFromChildren)) {
		return liftedBlockSelection;
	}
	if (!listItemsFromChildren.empty()) {
		return listItemsFromChildren;
	}
	if (!liftedBlockSelection.empty()) {
		return liftedBlockSelection;
	}
	return {};
}

} // namespace Iv::Editor
