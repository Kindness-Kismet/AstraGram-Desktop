/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_state.h"
#include "iv/editor/iv_editor_page_media.h"
#include "iv/editor/iv_editor_page_table_grid.h"
#include "iv/editor/iv_editor_text_entities.h"
#include "iv/markdown/iv_markdown_prepare_serialize.h"
#include "lang/lang_keys.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"

#include <algorithm>
#include <limits>
#include <utility>
#include "iv/editor/iv_editor_state_internal.h"

namespace Iv::Editor {
using namespace StateDetails;

int State::activeTextLength() const {
	return activeRawText().size();
}

std::optional<int> State::previousEditableOrdinal() const {
	return adjacentEditableOrdinal(false);
}

std::optional<int> State::nextEditableOrdinal() const {
	return adjacentEditableOrdinal(true);
}

std::vector<State::BoundaryTarget> State::boundarySteps(bool forward) const {
	auto steps = std::vector<BoundaryTarget>();
	collectBoundarySteps(
		_richPage->blocks,
		BlockContainerPath(),
		forward,
		&steps);
	return steps;
}

State::BoundaryTarget State::activeBoundaryTarget(bool forward) const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return {};
	}
	return boundaryTargetForLeaf(
		descriptor->leaf,
		descriptor,
		forward,
		true);
}

State::BoundaryTarget State::boundaryTargetForLeaf(
		const LeafPath &leaf,
		const TextNodeDescriptor *descriptor,
		bool forward,
		bool allowRemoveDirectly) const {
	const auto ordinal = textNodeOrdinal(leaf);
	if (ordinal < 0) {
		return {};
	}
	if (!forward) {
		const auto owner = block(leaf.block);
		if (owner && owner->kind == BlockKind::Table) {
			if (leaf.kind == LeafKind::BlockText && CanEditBlock(*owner)) {
				return {
					.action = BoundaryAction::StructuralSelection,
					.structuralSelection = preparedSelectionForBlock(leaf.block),
				};
			} else if (leaf.kind == LeafKind::TableCellText
				&& leaf.tableRowIndex == 0
				&& leaf.tableCellIndex == 0) {
				const auto titleOrdinal = textNodeOrdinal(LeafPath{
					.kind = LeafKind::BlockText,
					.block = leaf.block,
				});
				if (titleOrdinal >= 0) {
					return {
						.action = BoundaryAction::Text,
						.textOrdinal = titleOrdinal,
					};
				}
			}
		}
	}
	const auto removeDirectly = allowRemoveDirectly
		&& descriptor
		&& removalTargetIsEmpty(descriptor->removalTarget)
		&& shouldRemoveActiveOwnerDirectly(*descriptor);
	auto steps = std::vector<BoundaryTarget>();
	collectBoundarySteps(
		_richPage->blocks,
		BlockContainerPath(),
		forward,
		&steps);
	for (auto i = 0, count = int(steps.size()); i != count; ++i) {
		const auto &step = steps[i];
		if (step.action == BoundaryAction::Text
			&& step.textOrdinal == ordinal) {
			const auto next = (i + 1 < count)
				? steps[i + 1]
				: BoundaryTarget();
			if (removeDirectly
				&& next.action != BoundaryAction::StructuralSelection) {
				return {
					.action = BoundaryAction::RemoveActiveOwner,
				};
			}
			return next;
		}
	}
	return removeDirectly
		? BoundaryTarget{
			.action = BoundaryAction::RemoveActiveOwner,
		}
		: BoundaryTarget();
}

bool State::isActiveTopLevelParagraph() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || !descriptor->leaf.block.container.steps.empty()) {
		return false;
	}
	const auto owner = block(descriptor->leaf.block);
	return owner && owner->kind == BlockKind::Paragraph;
}

bool State::isActiveTopLevelParagraphOrHeading() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || !descriptor->leaf.block.container.steps.empty()) {
		return false;
	}
	const auto owner = block(descriptor->leaf.block);
	return owner
		&& ((owner->kind == BlockKind::Paragraph)
			|| (owner->kind == BlockKind::Heading)
			|| (owner->kind == BlockKind::Footer));
}

bool State::hasActiveListItemSurface() const {
	return activeListItemSurface().has_value();
}

bool State::hasActiveListItemExtraLine() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	return descriptor
		&& (descriptor->leaf.kind == LeafKind::BlockText)
		&& (descriptor->leaf.block.index >= 1)
		&& activeListItemSurface().has_value();
}

bool State::activeSurfaceAllowsSeparateLineFormula() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || descriptor->leaf.kind == LeafKind::MathFormula) {
		return false;
	}
	if (isActiveTopLevelParagraph() || activeListItemSurface().has_value()) {
		return true;
	}
	if (descriptor->leaf.kind != LeafKind::BlockText) {
		return false;
	}
	const auto owner = block(descriptor->leaf.block);
	if (!owner) {
		return false;
	}
	if (owner->kind == BlockKind::Quote) {
		return !owner->pullquote;
	}
	if (owner->kind != BlockKind::Paragraph) {
		return false;
	}
	const auto &container = descriptor->leaf.block.container;
	if (container.steps.empty()) {
		return false;
	}
	const auto &step = container.steps.back();
	if (step.kind != BlockContainerKind::BlockChildren) {
		return false;
	}
	auto parent = container;
	parent.steps.pop_back();
	const auto quote = block({
		.container = parent,
		.index = step.blockIndex,
	});
	return quote
		&& (quote->kind == BlockKind::Quote)
		&& !quote->pullquote;
}

bool State::activeLeafUsesQuoteCaptionColor() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || descriptor->leaf.kind != LeafKind::BlockCaption) {
		return false;
	}
	const auto owner = block(descriptor->leaf.block);
	return owner && owner->kind == BlockKind::Quote;
}

bool State::activeLeafUsesQuotePlaceholderColor() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return false;
	}
	const auto &leaf = descriptor->leaf;
	const auto owner = block(leaf.block);
	if (owner
		&& owner->kind == BlockKind::Quote
		&& (leaf.kind == LeafKind::BlockText
			|| leaf.kind == LeafKind::BlockCaption)) {
		return true;
	}
	auto container = leaf.block.container;
	while (!container.steps.empty()) {
		const auto step = container.steps.back();
		container.steps.pop_back();
		if (step.kind != BlockContainerKind::BlockChildren) {
			continue;
		}
		const auto ancestor = block({
			.container = container,
			.index = step.blockIndex,
		});
		if (ancestor
			&& ancestor->kind == BlockKind::Quote) {
			return true;
		}
	}
	return false;
}

bool State::activeBlockBodyCanEscape() const {
	return activeBlockBodyEscapeBlock().has_value();
}

bool State::shouldRemoveActiveOwnerDirectly(
		const TextNodeDescriptor &descriptor) const {
	switch (descriptor.removalTarget.kind) {
	case RemovalKind::Block: {
		const auto owner = block(descriptor.removalTarget.block);
		if (!owner) {
			return false;
		}
		switch (owner->kind) {
		case BlockKind::Heading:
		case BlockKind::Paragraph:
		case BlockKind::Footer:
		case BlockKind::Code:
		case BlockKind::Math:
			return true;
		default:
			return false;
		}
	}
	case RemovalKind::ListItem:
		if (const auto owner = listItem(
				descriptor.removalTarget.block,
				descriptor.removalTarget.listItemIndex)) {
			return owner->blocks.empty();
		}
		return false;
	case RemovalKind::TableCell:
		return false;
	}
	return false;
}

std::optional<int> State::removeActiveOwnerAndSelectAdjacent(bool forward) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || !removalTargetIsEmpty(descriptor->removalTarget)) {
		return std::nullopt;
	}
	const auto target = descriptor->removalTarget;
	auto first = _activeTextOrdinal;
	auto last = _activeTextOrdinal;
	while (first > 0 && _textNodes[first - 1].removalTarget == target) {
		--first;
	}
	while (last + 1 < textNodeCount()
		&& _textNodes[last + 1].removalTarget == target) {
		++last;
	}
	auto adjacent = std::optional<LeafPath>();
	if (forward) {
		for (auto i = last + 1, count = textNodeCount(); i != count; ++i) {
			if (!(_textNodes[i].removalTarget == target)) {
				adjacent = _textNodes[i].leaf;
				break;
			}
		}
	} else {
		for (auto i = first; i != 0; --i) {
			if (!(_textNodes[i - 1].removalTarget == target)) {
				adjacent = _textNodes[i - 1].leaf;
				break;
			}
		}
	}
	if (!removeTarget(target)) {
		return std::nullopt;
	}
	rebuild();
	if (adjacent && (!forward || target.kind == RemovalKind::TableCell)) {
		const auto ordinal = textNodeOrdinal(*adjacent);
		if (setActiveTextByOrdinal(ordinal)) {
			return _activeTextOrdinal;
		}
	}
	if (!textNodeCount()) {
		return std::nullopt;
	}
	const auto fallback = forward
		? std::min(first, textNodeCount() - 1)
		: std::max(std::min(first - 1, textNodeCount() - 1), 0);
	if (!setActiveTextByOrdinal(fallback)) {
		ensureActiveTextOrdinal();
	}
	return _activeTextOrdinal;
}

std::optional<int> State::moveActiveSpecialBlockDown() {
	return applyCheckedMutation(std::optional<int>(), [](State &candidate) {
		const auto result = candidate.moveActiveSpecialBlockDownUnchecked();
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::submitActiveSingleLineField(
		const ActiveEnterContext &context) {
	return applyCheckedMutation(std::optional<int>(), [=](State &candidate) {
		const auto result = candidate.submitActiveSingleLineFieldUnchecked(
			context);
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::escapeActiveBlockBody() {
	return applyCheckedMutation(std::optional<int>(), [](State &candidate) {
		const auto result = candidate.escapeActiveBlockBodyUnchecked();
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

State::BoundaryTarget State::removeTemporaryDownParagraphAndMove() {
	return applyCheckedMutation(BoundaryTarget(), [](State &candidate) {
		const auto result
			= candidate.removeTemporaryDownParagraphAndMoveUnchecked();
		return CheckedMutationResult<BoundaryTarget>{
			.apply = (result.action != BoundaryAction::None),
			.result = result,
		};
	});
}

std::optional<int> State::moveActiveSpecialBlockDownUnchecked() {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	auto target = std::optional<LeafPath>();
	auto trackTemporary = false;
	if (const auto quote = activeQuote(false)) {
		if (descriptor->leaf.kind == LeafKind::BlockCaption
			&& descriptor->leaf.block == quote->path) {
			if (const auto paragraph = reuseOrInsertParagraph(
					quote->path.container,
					quote->path.index + 1)) {
				target = paragraph->leaf;
				trackTemporary = paragraph->inserted;
			}
		} else if (descriptor->leaf.kind == LeafKind::BlockText
			&& descriptor->leaf.block == quote->path) {
			const auto owner = block(quote->path);
			if (owner
				&& owner->kind == BlockKind::Quote
				&& owner->blocks.empty()) {
				target = LeafPath{
					.kind = LeafKind::BlockCaption,
					.block = quote->path,
				};
			}
		} else if (quote->activeLeafIsLastEditableBodyLeaf) {
			target = LeafPath{
				.kind = LeafKind::BlockCaption,
				.block = quote->path,
			};
		}
	} else if (descriptor->leaf.kind == LeafKind::BlockText) {
		const auto owner = block(descriptor->leaf.block);
		if (owner && owner->kind == BlockKind::Code) {
			if (const auto paragraph = reuseOrInsertParagraph(
					descriptor->leaf.block.container,
					descriptor->leaf.block.index + 1)) {
				target = paragraph->leaf;
				trackTemporary = paragraph->inserted;
			}
		}
	}
	if (!target) {
		return std::nullopt;
	}
	_temporaryDownParagraph = trackTemporary
		? std::make_optional(*target)
		: std::nullopt;
	rebuild();
	return activateRebuiltLeaf(*target);
}

std::optional<int> State::submitActiveSingleLineFieldUnchecked(
		const ActiveEnterContext &context) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	const auto leaf = descriptor->leaf;
	const auto owner = block(leaf.block);
	if (!owner) {
		return std::nullopt;
	}
	const auto activate = [&](const LeafPath &target) -> std::optional<int> {
		const auto ordinal = textNodeOrdinal(target);
		return setActiveTextByOrdinal(ordinal)
			? std::make_optional(_activeTextOrdinal)
			: std::nullopt;
	};
	const auto paragraphAfterBlock = [&]() -> std::optional<int> {
		if (const auto paragraph = reuseOrInsertParagraph(
				leaf.block.container,
				leaf.block.index + 1)) {
			rebuild();
			return activateRebuiltLeaf(paragraph->leaf);
		}
		return std::nullopt;
	};
	const auto paragraphBeforeBlock = [&]() -> std::optional<int> {
		const auto blocks = blockContainer(leaf.block.container);
		if (!blocks
			|| leaf.block.index < 0
			|| leaf.block.index >= int(blocks->size())) {
			return std::nullopt;
		}
		clearTemporaryDownParagraph();
		blocks->insert(
			blocks->begin() + leaf.block.index,
			MakeParagraphBlock());
		auto shifted = leaf;
		++shifted.block.index;
		rebuild();
		return activateRebuiltLeaf(shifted);
	};
	if (leaf.kind == LeafKind::BlockCaption) {
		switch (owner->kind) {
		case BlockKind::Quote:
		case BlockKind::Photo:
		case BlockKind::Video:
		case BlockKind::Audio:
		case BlockKind::File:
		case BlockKind::Map:
		case BlockKind::GroupedMedia:
			if (context.position == EnterPosition::Middle) {
				return std::nullopt;
			} else if (context.position == EnterPosition::Beginning) {
				return paragraphBeforeBlock();
			}
			return paragraphAfterBlock();
		default:
			return std::nullopt;
		}
	}
	if (leaf.kind == LeafKind::BlockText) {
		if (owner->kind == BlockKind::Details
			|| owner->kind == BlockKind::Table) {
			if (context.position == EnterPosition::Middle) {
				return std::nullopt;
			} else if (context.position == EnterPosition::Beginning) {
				return paragraphBeforeBlock();
			}
		}
		if (owner->kind == BlockKind::Details) {
			const auto bodyContainer = BlockChildrenContainer(leaf.block);
			auto target = std::optional<LeafPath>();
			for (const auto &candidate : _textNodes) {
				if (ContainerStartsWith(
						candidate.leaf.block.container,
						bodyContainer)) {
					target = candidate.leaf;
					break;
				}
			}
			if (!target) {
				owner->blocks.push_back(MakeParagraphBlock());
				target = LeafPath{
					.kind = LeafKind::BlockText,
					.block = {
						.container = bodyContainer,
						.index = 0,
					},
				};
				rebuild();
				return activateRebuiltLeaf(*target);
			}
			return activate(*target);
		} else if (owner->kind == BlockKind::Table) {
			for (const auto &candidate : _textNodes) {
				if (candidate.leaf.block == leaf.block
					&& candidate.leaf.kind == LeafKind::TableCellText) {
					return activate(candidate.leaf);
				}
			}
			return paragraphAfterBlock();
		}
		return std::nullopt;
	} else if (leaf.kind == LeafKind::TableCellText) {
		if (const auto below = adjacentRowTableCellOrdinal(true)) {
			return setActiveTextByOrdinal(*below)
				? std::make_optional(_activeTextOrdinal)
				: std::nullopt;
		}
		return paragraphAfterBlock();
	}
	return std::nullopt;
}

std::optional<int> State::escapeActiveBlockBodyUnchecked() {
	const auto targetBlock = activeBlockBodyEscapeBlock();
	if (!targetBlock) {
		return std::nullopt;
	}
	if (const auto paragraph = reuseOrInsertParagraph(
			targetBlock->container,
			targetBlock->index + 1)) {
		rebuild();
		return activateRebuiltLeaf(paragraph->leaf);
	}
	return std::nullopt;
}

std::optional<State::BlockPath> State::activeBlockBodyEscapeBlock() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	const auto leaf = descriptor->leaf;
	auto targetBlock = std::optional<BlockPath>();
	if (const auto owner = block(leaf.block);
		owner
		&& leaf.kind == LeafKind::BlockText
		&& (owner->kind == BlockKind::Quote
			|| owner->kind == BlockKind::Code)) {
		targetBlock = leaf.block;
	} else {
		auto container = leaf.block.container;
		while (!container.steps.empty()) {
			const auto step = container.steps.back();
			container.steps.pop_back();
			if (step.kind != BlockContainerKind::BlockChildren) {
				continue;
			}
			const auto candidate = BlockPath{
				.container = container,
				.index = step.blockIndex,
			};
			const auto owner = block(candidate);
			if (owner
				&& (owner->kind == BlockKind::Quote
					|| owner->kind == BlockKind::Details)) {
				targetBlock = candidate;
				break;
			}
		}
	}
	return targetBlock;
}

auto State::captureRebuiltBoundaryTarget(
		const BoundaryTarget &target) const
-> std::optional<RebuiltBoundaryTarget> {
	switch (target.action) {
	case BoundaryAction::Text:
		if (const auto descriptor = textNode(target.textOrdinal)) {
			return RebuiltBoundaryTarget{
				.action = BoundaryAction::Text,
				.leaf = descriptor->leaf,
			};
		}
		break;
	case BoundaryAction::StructuralSelection:
		switch (target.structuralSelection.kind) {
		case PreparedEditSelectionKind::Blocks:
			if (const auto range = validateBlockRange(
					target.structuralSelection.blocks);
				range
				&& (range->till == range->from + 1)) {
				return RebuiltBoundaryTarget{
					.action = BoundaryAction::StructuralSelection,
					.block = {
						.container = range->container,
						.index = range->from,
					},
				};
			}
			break;
		case PreparedEditSelectionKind::ListItems:
			if (const auto range = validateListItemRange(
					target.structuralSelection.listItems);
				range
				&& (range->till == range->from + 1)) {
				return RebuiltBoundaryTarget{
					.action = BoundaryAction::StructuralSelection,
					.block = range->block,
					.listItemIndex = range->from,
				};
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return std::nullopt;
}

void State::shiftRebuiltBoundaryTargetAfterRemovedBlock(
		RebuiltBoundaryTarget &target,
		const BlockPath &removed) const {
	switch (target.action) {
	case BoundaryAction::Text:
		if (!ShiftBlockPathAfterRemovedBlock(target.leaf.block, removed)) {
			target = RebuiltBoundaryTarget();
		}
		break;
	case BoundaryAction::StructuralSelection:
		if (!ShiftBlockPathAfterRemovedBlock(target.block, removed)) {
			target = RebuiltBoundaryTarget();
		}
		break;
	default:
		target = RebuiltBoundaryTarget();
		break;
	}
}

State::BoundaryTarget State::materializeBoundaryTarget(
		const RebuiltBoundaryTarget &target) const {
	switch (target.action) {
	case BoundaryAction::Text:
		if (const auto ordinal = textNodeOrdinal(target.leaf); ordinal >= 0) {
			return {
				.action = BoundaryAction::Text,
				.textOrdinal = ordinal,
			};
		}
		break;
	case BoundaryAction::StructuralSelection:
		if (target.listItemIndex >= 0) {
			const auto owner = block(target.block);
			if (owner
				&& owner->kind == BlockKind::List
				&& target.listItemIndex < int(owner->listItems.size())
				&& CanEditBlocks(owner->listItems[target.listItemIndex].blocks)) {
				return {
					.action = BoundaryAction::StructuralSelection,
					.structuralSelection = preparedSelectionForListItem(
						target.block,
						target.listItemIndex),
				};
			}
		} else if (const auto owner = block(target.block);
			owner && CanEditBlock(*owner)) {
			return {
				.action = BoundaryAction::StructuralSelection,
				.structuralSelection = preparedSelectionForBlock(target.block),
			};
		}
		break;
	default:
		break;
	}
	return {};
}

State::BoundaryTarget State::removeTemporaryDownParagraphAndMoveUnchecked() {
	const auto tracked = _temporaryDownParagraph;
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!tracked
		|| !descriptor
		|| !(descriptor->leaf == *tracked)) {
		return {};
	}
	const auto owner = block(tracked->block);
	if (!owner || owner->kind != BlockKind::Paragraph) {
		clearTemporaryDownParagraph();
		return {};
	}
	if (!BlockIsEmpty(*owner)) {
		clearTemporaryDownParagraph();
		return {};
	}
	const auto next = boundaryTargetForLeaf(
		*tracked,
		descriptor,
		true,
		false);
	if (next.action == BoundaryAction::None) {
		return {};
	}
	auto rebuiltTarget = captureRebuiltBoundaryTarget(next);
	if (!rebuiltTarget) {
		return {};
	}
	const auto removed = tracked->block;
	if (!removeTarget({
			.kind = RemovalKind::Block,
			.block = removed,
		})) {
		return {};
	}
	shiftRebuiltBoundaryTargetAfterRemovedBlock(*rebuiltTarget, removed);
	if (rebuiltTarget->action == BoundaryAction::None) {
		return {};
	}
	rebuild();
	const auto materialized = materializeBoundaryTarget(*rebuiltTarget);
	if (materialized.action == BoundaryAction::Text) {
		if (!setActiveTextByOrdinal(materialized.textOrdinal)) {
			ensureActiveTextOrdinal();
		}
	}
	return materialized;
}

std::optional<int> State::adjacentEditableOrdinal(bool forward) const {
	if (_activeTextOrdinal < 0) {
		return std::nullopt;
	}
	const auto ordinal = _activeTextOrdinal + (forward ? 1 : -1);
	return (ordinal >= 0 && ordinal < textNodeCount())
		? std::make_optional(ordinal)
		: std::nullopt;
}

std::optional<int> State::firstTableCellOrdinalFromActiveTitle() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	const auto leaf = descriptor->leaf;
	if (leaf.kind != LeafKind::BlockText) {
		return std::nullopt;
	}
	const auto owner = block(leaf.block);
	if (!owner || owner->kind != BlockKind::Table) {
		return std::nullopt;
	}
	for (auto i = 0, count = textNodeCount(); i != count; ++i) {
		const auto &candidate = _textNodes[i].leaf;
		if (candidate.block == leaf.block
			&& candidate.kind == LeafKind::TableCellText) {
			return i;
		}
	}
	return std::nullopt;
}

std::optional<int> State::adjacentRowTableCellOrdinal(bool down) const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	const auto leaf = descriptor->leaf;
	if (leaf.kind != LeafKind::TableCellText) {
		return std::nullopt;
	}
	const auto owner = block(leaf.block);
	if (!owner || owner->kind != BlockKind::Table) {
		return std::nullopt;
	}
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	const auto active = [&]() -> const TableGridCellReference* {
		for (const auto &candidate : grid.cells) {
			if (candidate.rowIndex == leaf.tableRowIndex
				&& candidate.cellIndex == leaf.tableCellIndex) {
				return &candidate;
			}
		}
		return nullptr;
	}();
	if (!active) {
		return std::nullopt;
	}
	const auto column = active->columnFrom;
	const auto step = down ? 1 : -1;
	for (auto targetRow = down ? active->rowTill : (active->rowFrom - 1);
			targetRow >= 0 && targetRow < grid.rowCount;
			targetRow += step) {
		auto best = (const TableGridCellReference*)nullptr;
		auto bestDistance = std::numeric_limits<int>::max();
		for (const auto &candidate : grid.cells) {
			if (candidate.rowFrom > targetRow
				|| candidate.rowTill <= targetRow) {
				continue;
			}
			const auto distance = (column < candidate.columnFrom)
				? (candidate.columnFrom - column)
				: (column >= candidate.columnTill)
				? (column - candidate.columnTill + 1)
				: 0;
			if (distance < bestDistance) {
				bestDistance = distance;
				best = &candidate;
			}
		}
		if (best) {
			const auto ordinal = textNodeOrdinal({
				.kind = LeafKind::TableCellText,
				.block = leaf.block,
				.tableRowIndex = best->rowIndex,
				.tableCellIndex = best->cellIndex,
			});
			return (ordinal >= 0)
				? std::make_optional(ordinal)
				: std::nullopt;
		}
	}
	return std::nullopt;
}

std::optional<int> State::tableTitleOrdinalFromActiveCell() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	const auto leaf = descriptor->leaf;
	if (leaf.kind != LeafKind::TableCellText) {
		return std::nullopt;
	}
	const auto owner = block(leaf.block);
	if (!owner || owner->kind != BlockKind::Table) {
		return std::nullopt;
	}
	const auto ordinal = textNodeOrdinal({
		.kind = LeafKind::BlockText,
		.block = leaf.block,
	});
	return (ordinal >= 0) ? std::make_optional(ordinal) : std::nullopt;
}

std::optional<int> State::ordinalAfterActiveTable() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	const auto leaf = descriptor->leaf;
	if (leaf.kind != LeafKind::TableCellText) {
		return std::nullopt;
	}
	const auto owner = block(leaf.block);
	if (!owner || owner->kind != BlockKind::Table) {
		return std::nullopt;
	}
	for (auto i = _activeTextOrdinal + 1, count = textNodeCount();
			i != count;
			++i) {
		const auto &candidate = _textNodes[i].leaf;
		if (candidate.block != leaf.block
			|| candidate.kind != LeafKind::TableCellText) {
			return i;
		}
	}
	return std::nullopt;
}

void State::collectBoundarySteps(
		const std::vector<Block> &blocks,
		const BlockContainerPath &container,
		bool forward,
		std::vector<BoundaryTarget> *steps) const {
	const auto collectBlock = [&](int index) {
		const auto path = BlockPath{
			.container = container,
			.index = index,
		};
		const auto &block = blocks[index];
		switch (block.kind) {
		case BlockKind::Heading:
		case BlockKind::Paragraph:
		case BlockKind::Footer:
		case BlockKind::Code:
			appendBoundaryTextStep({
				.kind = LeafKind::BlockText,
				.block = path,
			}, steps);
			break;
		case BlockKind::Quote:
			if (forward) {
				if (block.blocks.empty()) {
					appendBoundaryTextStep({
						.kind = LeafKind::BlockText,
						.block = path,
					}, steps);
				}
				collectBoundarySteps(
					block.blocks,
					BlockChildrenContainer(path),
					forward,
					steps);
				appendBoundaryTextStep({
					.kind = LeafKind::BlockCaption,
					.block = path,
				}, steps);
			} else {
				appendBoundaryTextStep({
					.kind = LeafKind::BlockCaption,
					.block = path,
				}, steps);
				collectBoundarySteps(
					block.blocks,
					BlockChildrenContainer(path),
					forward,
					steps);
				if (block.blocks.empty()) {
					appendBoundaryTextStep({
						.kind = LeafKind::BlockText,
						.block = path,
					}, steps);
				}
			}
			appendBoundaryBlockStep(path, steps);
			break;
		case BlockKind::List:
			if (forward) {
				for (auto j = 0, itemCount = int(block.listItems.size());
						j != itemCount;
						++j) {
					const auto &item = block.listItems[j];
					if (!RichTextIsEmpty(item.text) || item.blocks.empty()) {
						appendBoundaryTextStep({
							.kind = LeafKind::ListItemText,
							.block = path,
							.listItemIndex = j,
						}, steps);
					}
					if (!item.blocks.empty()) {
						collectBoundarySteps(
							item.blocks,
							ListItemChildrenContainer(path, j),
							forward,
							steps);
						appendBoundaryListItemStep(path, j, steps);
					}
				}
			} else {
				for (auto j = int(block.listItems.size()); j != 0; --j) {
					const auto itemIndex = j - 1;
					const auto &item = block.listItems[itemIndex];
					if (!item.blocks.empty()) {
						collectBoundarySteps(
							item.blocks,
							ListItemChildrenContainer(path, itemIndex),
							forward,
							steps);
					}
					if (!RichTextIsEmpty(item.text) || item.blocks.empty()) {
						appendBoundaryTextStep({
							.kind = LeafKind::ListItemText,
							.block = path,
							.listItemIndex = itemIndex,
						}, steps);
					}
					if (!item.blocks.empty()) {
						appendBoundaryListItemStep(path, itemIndex, steps);
					}
				}
			}
			break;
		case BlockKind::Photo:
		case BlockKind::Video:
		case BlockKind::Audio:
		case BlockKind::File:
		case BlockKind::Map:
		case BlockKind::GroupedMedia:
			appendBoundaryTextStep({
				.kind = LeafKind::BlockCaption,
				.block = path,
			}, steps);
			appendBoundaryBlockStep(path, steps);
			break;
		case BlockKind::Math:
			appendBoundaryTextStep({
				.kind = LeafKind::MathFormula,
				.block = path,
			}, steps);
			break;
		case BlockKind::Table:
			if (forward) {
				if (!block.text.text.text.isEmpty()) {
					appendBoundaryTextStep({
						.kind = LeafKind::BlockText,
						.block = path,
					}, steps);
				}
				for (auto j = 0, rowCount = int(block.tableRows.size());
						j != rowCount;
						++j) {
					const auto &row = block.tableRows[j];
					for (auto k = 0, cellCount = int(row.cells.size());
							k != cellCount;
							++k) {
						appendBoundaryTextStep({
							.kind = LeafKind::TableCellText,
							.block = path,
							.tableRowIndex = j,
							.tableCellIndex = k,
						}, steps);
					}
				}
			} else {
				for (auto j = int(block.tableRows.size()); j != 0; --j) {
					const auto rowIndex = j - 1;
					const auto &row = block.tableRows[rowIndex];
					for (auto k = int(row.cells.size()); k != 0; --k) {
						appendBoundaryTextStep({
							.kind = LeafKind::TableCellText,
							.block = path,
							.tableRowIndex = rowIndex,
							.tableCellIndex = k - 1,
						}, steps);
					}
				}
				if (!block.text.text.text.isEmpty()) {
					appendBoundaryTextStep({
						.kind = LeafKind::BlockText,
						.block = path,
					}, steps);
				}
			}
			appendBoundaryBlockStep(path, steps);
			break;
		case BlockKind::Details:
			if (forward) {
				appendBoundaryTextStep({
					.kind = LeafKind::BlockText,
					.block = path,
				}, steps);
				collectBoundarySteps(
					block.blocks,
					BlockChildrenContainer(path),
					forward,
					steps);
			} else {
				collectBoundarySteps(
					block.blocks,
					BlockChildrenContainer(path),
					forward,
					steps);
				appendBoundaryTextStep({
					.kind = LeafKind::BlockText,
					.block = path,
				}, steps);
			}
			appendBoundaryBlockStep(path, steps);
			break;
		default: {
			const auto before = steps->size();
			if (!block.blocks.empty()) {
				collectBoundarySteps(
					block.blocks,
					BlockChildrenContainer(path),
					forward,
					steps);
			}
			if (steps->size() == before) {
				appendBoundaryBlockStep(path, steps);
			}
		} break;
		}
	};
	if (forward) {
		for (auto i = 0, count = int(blocks.size()); i != count; ++i) {
			collectBlock(i);
		}
	} else {
		for (auto i = int(blocks.size()); i != 0; --i) {
			collectBlock(i - 1);
		}
	}
}

void State::appendBoundaryTextStep(
		LeafPath leaf,
		std::vector<BoundaryTarget> *steps) const {
	const auto ordinal = textNodeOrdinal(leaf);
	if (ordinal >= 0) {
		steps->push_back({
			.action = BoundaryAction::Text,
			.textOrdinal = ordinal,
		});
	}
}

void State::appendBoundaryBlockStep(
		const BlockPath &path,
		std::vector<BoundaryTarget> *steps) const {
	const auto owner = block(path);
	if (owner && CanEditBlock(*owner)) {
		steps->push_back({
			.action = BoundaryAction::StructuralSelection,
			.structuralSelection = preparedSelectionForBlock(path),
		});
	}
}

void State::appendBoundaryListItemStep(
		const BlockPath &path,
		int itemIndex,
		std::vector<BoundaryTarget> *steps) const {
	const auto owner = block(path);
	if (!owner
		|| owner->kind != BlockKind::List
		|| itemIndex < 0
		|| itemIndex >= int(owner->listItems.size())
		|| !CanEditBlocks(owner->listItems[itemIndex].blocks)) {
		return;
	}
	steps->push_back({
		.action = BoundaryAction::StructuralSelection,
		.structuralSelection = preparedSelectionForListItem(path, itemIndex),
	});
}

} // namespace Iv::Editor
