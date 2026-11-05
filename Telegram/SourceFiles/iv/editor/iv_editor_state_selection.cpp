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
#include <utility>
#include "iv/editor/iv_editor_state_internal.h"

namespace Iv::Editor {
using namespace StateDetails;

State::LeafPath State::shiftLeafAfterRemovedSelection(
		const Markdown::PreparedEditSelection &selection,
		LeafPath leaf) const {
	if (selection.kind != PreparedEditSelectionKind::Blocks) {
		return leaf;
	}
	const auto range = validateBlockRange(selection.blocks);
	if (!range || leafWillBeRemoved(leaf, *range)) {
		return leaf;
	}
	auto shifted = leaf.block;
	for (auto i = range->till; i != range->from;) {
		--i;
		if (!ShiftBlockPathAfterRemovedBlock(shifted, BlockPath{
				.container = range->container,
				.index = i,
			})) {
			return leaf;
		}
	}
	leaf.block = shifted;
	return leaf;
}

std::optional<int> State::removeStructuralSelection(
		const Markdown::PreparedEditSelection &selection,
		bool forward) {
	clearTemporaryDownParagraph();
	const auto activate = [&](const LeafPath &leaf) -> std::optional<int> {
		const auto ordinal = textNodeOrdinal(leaf);
		if (setActiveTextByOrdinal(ordinal)) {
			return _activeTextOrdinal;
		}
		return std::nullopt;
	};
	const auto finish = [&](
			auto postMutationFocus,
			std::optional<LeafPath> plannedFocus) -> std::optional<int> {
		rebuild();
		if (const auto focus = postMutationFocus()) {
			if (const auto ordinal = activate(*focus)) {
				return ordinal;
			}
		}
		if (plannedFocus) {
			if (const auto ordinal = activate(*plannedFocus)) {
				return ordinal;
			}
		}
		ensureActiveTextOrdinal();
		return (_activeTextOrdinal >= 0)
			? std::make_optional(_activeTextOrdinal)
			: std::nullopt;
	};
	const auto leafForContainerOwner = [&](
			const BlockContainerPath &container) -> std::optional<LeafPath> {
		if (container.steps.empty()) {
			return std::nullopt;
		}
		auto parent = container;
		const auto step = parent.steps.back();
		parent.steps.pop_back();
		const auto owner = BlockPath{
			.container = parent,
			.index = step.blockIndex,
		};
		if (step.kind == BlockContainerKind::BlockChildren) {
			for (const auto &descriptor : _textNodes) {
				if (leafBelongsToBlock(descriptor.leaf, owner)) {
					return descriptor.leaf;
				}
			}
		} else if (step.kind == BlockContainerKind::ListItemChildren) {
			for (const auto &descriptor : _textNodes) {
				const auto index = ListItemIndexForLeaf(
					descriptor.leaf,
					owner);
				if (index && *index == step.listItemIndex) {
					return descriptor.leaf;
				}
			}
		}
		return std::nullopt;
	};
	const auto leafNearBlockRange = [&](
			const StructuralBlockRange &range,
			bool forward) -> std::optional<LeafPath> {
		if (forward) {
			for (const auto &descriptor : _textNodes) {
				const auto index = BlockIndexInContainer(
					descriptor.leaf,
					range.container);
				if (index && *index >= range.from) {
					return descriptor.leaf;
				}
			}
		} else {
			for (auto i = textNodeCount(); i != 0; --i) {
				const auto &leaf = _textNodes[i - 1].leaf;
				const auto index = BlockIndexInContainer(leaf, range.container);
				if (index && *index < range.from) {
					return leaf;
				}
			}
		}
		return leafForContainerOwner(range.container);
	};
	const auto leafOutsideBlock = [&](
			const BlockPath &path,
			bool forward) -> std::optional<LeafPath> {
		if (forward) {
			for (const auto &descriptor : _textNodes) {
				const auto index = BlockIndexInContainer(
					descriptor.leaf,
					path.container);
				if (index && *index > path.index) {
					return descriptor.leaf;
				}
			}
		} else {
			for (auto i = textNodeCount(); i != 0; --i) {
				const auto &leaf = _textNodes[i - 1].leaf;
				const auto index = BlockIndexInContainer(leaf, path.container);
				if (index && *index < path.index) {
					return leaf;
				}
			}
		}
		return std::nullopt;
	};
	const auto leafNearListItemRange = [&](
			const StructuralListItemRange &range,
			bool forward) -> std::optional<LeafPath> {
		if (forward) {
			for (const auto &descriptor : _textNodes) {
				const auto index = ListItemIndexForLeaf(
					descriptor.leaf,
					range.block);
				if (index && *index >= range.from) {
					return descriptor.leaf;
				}
			}
		} else {
			for (auto i = textNodeCount(); i != 0; --i) {
				const auto &leaf = _textNodes[i - 1].leaf;
				const auto index = ListItemIndexForLeaf(leaf, range.block);
				if (index && *index < range.from) {
					return leaf;
				}
			}
		}
		return leafOutsideBlock(range.block, forward);
	};
	const auto tableTitleLeaf = [&](
			const BlockPath &path) -> std::optional<LeafPath> {
		auto leaf = LeafPath{
			.kind = LeafKind::BlockText,
			.block = path,
		};
		return (textNodeOrdinal(leaf) >= 0)
			? std::make_optional(leaf)
			: std::nullopt;
	};
	const auto leafNearTableRows = [&](
			const StructuralTableRowRange &range,
			bool forward) -> std::optional<LeafPath> {
		const auto cellInDirection = [&](
				bool direction) -> std::optional<LeafPath> {
			if (direction) {
				for (const auto &descriptor : _textNodes) {
					const auto &leaf = descriptor.leaf;
					if (leaf.block == range.block
						&& leaf.kind == LeafKind::TableCellText
						&& leaf.tableRowIndex >= range.from) {
						return leaf;
					}
				}
			} else {
				for (auto i = textNodeCount(); i != 0; --i) {
					const auto &leaf = _textNodes[i - 1].leaf;
					if (leaf.block == range.block
						&& leaf.kind == LeafKind::TableCellText
						&& leaf.tableRowIndex < range.from) {
						return leaf;
					}
				}
			}
			return std::nullopt;
		};
		if (const auto leaf = cellInDirection(forward)) {
			return leaf;
		}
		if (const auto leaf = cellInDirection(!forward)) {
			return leaf;
		}
		if (const auto leaf = tableTitleLeaf(range.block)) {
			return leaf;
		}
		return leafOutsideBlock(range.block, forward);
	};
	const auto leafNearTableCells = [&](
			const StructuralTableCellRange &range,
			bool forward) -> std::optional<LeafPath> {
		if (const auto leaf = firstSelectedLeaf(range)) {
			return leaf;
		}
		return adjacentLeafOutsideRange(range, forward);
	};
	const auto focusForRemovedBlock = [&](
			const BlockPath &path,
			bool forward) {
		return leafNearBlockRange(StructuralBlockRange{
			.container = path.container,
			.from = path.index,
			.till = path.index + 1,
		}, forward);
	};
	switch (selection.kind) {
	case PreparedEditSelectionKind::Blocks: {
		const auto range = validateBlockRange(selection.blocks);
		if (!range) {
			return std::nullopt;
		}
		const auto plannedFocus = plannedFocusForRange(*range, forward);
		const auto blocks = blockContainer(range->container);
		if (!blocks) {
			return std::nullopt;
		}
		blocks->erase(
			blocks->begin() + range->from,
			blocks->begin() + range->till);
		return finish([&] {
			return leafNearBlockRange(*range, forward);
		}, plannedFocus);
	}
	case PreparedEditSelectionKind::ListItems: {
		const auto range = validateListItemRange(selection.listItems);
		if (!range) {
			return std::nullopt;
		}
		const auto plannedFocus = plannedFocusForRange(*range, forward);
		const auto owner = block(range->block);
		if (!owner || owner->kind != BlockKind::List) {
			return std::nullopt;
		}
		owner->listItems.erase(
			owner->listItems.begin() + range->from,
			owner->listItems.begin() + range->till);
		if (owner->listItems.empty()) {
			const auto removed = range->block;
			if (!removeTarget({
					.kind = RemovalKind::Block,
					.block = removed,
				})) {
				return std::nullopt;
			}
			return finish([&] {
				return focusForRemovedBlock(removed, forward);
			}, plannedFocus);
		}
		return finish([&] {
			return leafNearListItemRange(*range, forward);
		}, plannedFocus);
	}
	case PreparedEditSelectionKind::TableRows: {
		const auto range = validateTableRowRange(selection.tableRows);
		if (!range) {
			return std::nullopt;
		}
		const auto plannedFocus = plannedFocusForRange(*range, forward);
		const auto owner = block(range->block);
		if (!owner || owner->kind != BlockKind::Table) {
			return std::nullopt;
		}
		owner->tableRows.erase(
			owner->tableRows.begin() + range->from,
			owner->tableRows.begin() + range->till);
		if (owner->tableRows.empty() && RichTextIsEmpty(owner->text)) {
			const auto removed = range->block;
			if (!removeTarget({
					.kind = RemovalKind::Block,
					.block = removed,
				})) {
				return std::nullopt;
			}
			return finish([&] {
				return focusForRemovedBlock(removed, forward);
			}, plannedFocus);
		}
		return finish([&] {
			return leafNearTableRows(*range, forward);
		}, plannedFocus);
	}
	case PreparedEditSelectionKind::TableCells: {
		const auto range = validateTableCellRange(selection.tableCells);
		if (!range) {
			return std::nullopt;
		}
		const auto owner = block(range->block);
		if (!owner || owner->kind != BlockKind::Table) {
			return std::nullopt;
		}
		const auto grid = BuildTableGrid(*owner, tableRenderLimits());
		if (!TableGridRangeSpansAllRows(grid, *range)) {
			return std::nullopt;
		}
		const auto plannedFocus = plannedFocusForRange(*range, forward);
		const auto selected = SelectedTableGridCells(grid, *range);
		if (selected.empty()) {
			return std::nullopt;
		}
		auto toErase = std::vector<TableGridCellReference>();
		for (const auto &reference : selected) {
			if (TableGridCellContainedInRange(reference, *range)) {
				toErase.push_back(reference);
				continue;
			}
			const auto intersection = TableGridCellColumnIntersection(
				reference,
				*range);
			if (intersection <= 0) {
				continue;
			}
			auto &cell = owner->tableRows[reference.rowIndex].cells[
				reference.cellIndex];
			cell.colspan = std::max(
				(reference.columnTill - reference.columnFrom) - intersection,
				1);
		}
		std::sort(
			toErase.begin(),
			toErase.end(),
			[](const auto &a, const auto &b) {
				if (a.rowIndex != b.rowIndex) {
					return a.rowIndex > b.rowIndex;
				}
				return a.cellIndex > b.cellIndex;
			});
		for (const auto &reference : toErase) {
			auto &row = owner->tableRows[reference.rowIndex];
			row.cells.erase(row.cells.begin() + reference.cellIndex);
		}
		if (std::all_of(
				owner->tableRows.begin(),
				owner->tableRows.end(),
				[](const TableRow &row) {
					return row.cells.empty();
				})) {
			if (RichTextIsEmpty(owner->text)) {
				const auto removed = range->block;
				if (!removeTarget({
						.kind = RemovalKind::Block,
						.block = removed,
					})) {
					return std::nullopt;
				}
				return finish([&] {
					return focusForRemovedBlock(removed, forward);
				}, plannedFocus);
			}
			auto row = TableRow();
			row.cells.push_back(MakeDefaultTableCell());
			owner->tableRows.clear();
			owner->tableRows.push_back(std::move(row));
		}
		return finish([&] {
			return leafNearTableCells(*range, forward);
		}, plannedFocus);
	}
	case PreparedEditSelectionKind::None:
		return std::nullopt;
	}
	return std::nullopt;
}

std::optional<State::BlockContainerPath> State::convertBlockContainerPath(
		const Markdown::PreparedEditBlockContainerPath &path) const {
	auto result = BlockContainerPath();
	result.steps.reserve(path.steps.size());
	const auto *blocks = &_richPage->blocks;
	for (const auto &step : path.steps) {
		if (step.blockIndex < 0 || step.blockIndex >= int(blocks->size())) {
			return std::nullopt;
		}
		const auto &parent = (*blocks)[step.blockIndex];
		auto converted = BlockContainerStep();
		converted.blockIndex = step.blockIndex;
		converted.listItemIndex = step.listItemIndex;
		switch (step.kind) {
		case PreparedBlockContainerKind::Root:
			return std::nullopt;
		case PreparedBlockContainerKind::BlockChildren:
			if (!BlockCanOwnChildContainer(parent)) {
				return std::nullopt;
			}
			converted.kind = BlockContainerKind::BlockChildren;
			blocks = &parent.blocks;
			break;
		case PreparedBlockContainerKind::ListItemChildren:
			if (parent.kind != BlockKind::List
				|| step.listItemIndex < 0
				|| step.listItemIndex >= int(parent.listItems.size())) {
				return std::nullopt;
			}
			converted.kind = BlockContainerKind::ListItemChildren;
			blocks = &parent.listItems[step.listItemIndex].blocks;
			break;
		}
		result.steps.push_back(converted);
	}
	return result;
}

std::optional<State::BlockPath> State::convertBlockPath(
		const Markdown::PreparedEditBlockPath &path) const {
	if (path.index < 0) {
		return std::nullopt;
	}
	const auto container = convertBlockContainerPath(path.container);
	if (!container) {
		return std::nullopt;
	}
	const auto blocks = blockContainer(*container);
	if (!blocks || path.index >= int(blocks->size())) {
		return std::nullopt;
	}
	return BlockPath{
		.container = *container,
		.index = path.index,
	};
}

std::optional<State::BlockPath> State::convertBlockPath(
		const Markdown::PreparedEditBlockSource &source) const {
	return convertBlockPath(source.path);
}

std::optional<State::LeafPath> State::convertLeafPath(
		const Markdown::PreparedEditLeafSource &source) const {
	const auto blockPath = convertBlockPath(source.block);
	if (!blockPath) {
		return std::nullopt;
	}
	auto result = LeafPath();
	result.block = *blockPath;
	switch (source.kind) {
	case PreparedEditLeafKind::BlockText:
		result.kind = LeafKind::BlockText;
		break;
	case PreparedEditLeafKind::BlockCaption:
		result.kind = LeafKind::BlockCaption;
		break;
	case PreparedEditLeafKind::ListItemText:
		if (source.listItemIndex < 0) {
			return std::nullopt;
		}
		result.kind = LeafKind::ListItemText;
		result.listItemIndex = source.listItemIndex;
		break;
	case PreparedEditLeafKind::TableCellText:
		if (source.tableRowIndex < 0 || source.tableCellIndex < 0) {
			return std::nullopt;
		}
		result.kind = LeafKind::TableCellText;
		result.tableRowIndex = source.tableRowIndex;
		result.tableCellIndex = source.tableCellIndex;
		break;
	case PreparedEditLeafKind::MathFormula:
		result.kind = LeafKind::MathFormula;
		break;
	}
	const auto owner = block(result.block);
	if (!owner) {
		return std::nullopt;
	}
	switch (result.kind) {
	case LeafKind::BlockText:
		if (!BlockSupportsBlockText(*owner)) {
			return std::nullopt;
		}
		break;
	case LeafKind::BlockCaption:
		if (!BlockSupportsBlockCaption(*owner)) {
			return std::nullopt;
		}
		break;
	case LeafKind::ListItemText:
		if (owner->kind != BlockKind::List
			|| !listItem(result.block, result.listItemIndex)) {
			return std::nullopt;
		}
		break;
	case LeafKind::TableCellText:
		if (owner->kind != BlockKind::Table
			|| !tableCell(
				result.block,
				result.tableRowIndex,
				result.tableCellIndex)) {
			return std::nullopt;
		}
		break;
	case LeafKind::MathFormula:
		if (owner->kind != BlockKind::Math) {
			return std::nullopt;
		}
		break;
	}
	return result;
}

std::optional<PreparedEditLeafSource> State::convertPreparedLeafSource(
		const LeafPath &path) const {
	const auto owner = block(path.block);
	if (!owner) {
		return std::nullopt;
	}
	auto result = PreparedEditLeafSource();
	result.block = {
		.container = ToPreparedBlockContainerPath(path.block.container),
		.index = path.block.index,
	};
	switch (path.kind) {
	case LeafKind::BlockText:
		if (!BlockSupportsBlockText(*owner)) {
			return std::nullopt;
		}
		result.kind = PreparedEditLeafKind::BlockText;
		break;
	case LeafKind::BlockCaption:
		if (!BlockSupportsBlockCaption(*owner)) {
			return std::nullopt;
		}
		result.kind = PreparedEditLeafKind::BlockCaption;
		break;
	case LeafKind::ListItemText:
		if (owner->kind != BlockKind::List
			|| !listItem(path.block, path.listItemIndex)) {
			return std::nullopt;
		}
		result.kind = PreparedEditLeafKind::ListItemText;
		result.listItemIndex = path.listItemIndex;
		break;
	case LeafKind::TableCellText:
		if (owner->kind != BlockKind::Table
			|| !tableCell(
				path.block,
				path.tableRowIndex,
				path.tableCellIndex)) {
			return std::nullopt;
		}
		result.kind = PreparedEditLeafKind::TableCellText;
		result.tableRowIndex = path.tableRowIndex;
		result.tableCellIndex = path.tableCellIndex;
		break;
	case LeafKind::MathFormula:
		if (owner->kind != BlockKind::Math) {
			return std::nullopt;
		}
		result.kind = PreparedEditLeafKind::MathFormula;
		break;
	}
	return result;
}

std::optional<PreparedEditLeafSource> State::convertPreparedLeafSource(
		const TextNodeDescriptor &descriptor) const {
	return convertPreparedLeafSource(descriptor.leaf);
}

std::optional<State::StructuralBlockRange> State::validateBlockRange(
		const Markdown::PreparedEditBlockRange &range) const {
	if (range.empty()) {
		return std::nullopt;
	}
	const auto container = convertBlockContainerPath(range.container);
	if (!container) {
		return std::nullopt;
	}
	const auto blocks = blockContainer(*container);
	if (!blocks
		|| range.from < 0
		|| range.till > int(blocks->size())) {
		return std::nullopt;
	}
	for (auto i = range.from; i != range.till; ++i) {
		if (!CanEditBlock((*blocks)[i])) {
			return std::nullopt;
		}
	}
	return StructuralBlockRange{
		.container = *container,
		.from = range.from,
		.till = range.till,
	};
}

std::optional<State::StructuralListItemRange> State::validateListItemRange(
		const Markdown::PreparedEditListItemRange &range) const {
	if (range.empty()) {
		return std::nullopt;
	}
	const auto path = convertBlockPath(range.block);
	if (!path) {
		return std::nullopt;
	}
	const auto owner = block(*path);
	if (!owner
		|| owner->kind != BlockKind::List
		|| range.from < 0
		|| range.till > int(owner->listItems.size())) {
		return std::nullopt;
	}
	for (auto i = range.from; i != range.till; ++i) {
		if (!CanEditBlocks(owner->listItems[i].blocks)) {
			return std::nullopt;
		}
	}
	return StructuralListItemRange{
		.block = *path,
		.from = range.from,
		.till = range.till,
	};
}

std::optional<State::StructuralTableRowRange> State::validateTableRowRange(
		const Markdown::PreparedEditTableRowRange &range) const {
	if (range.empty()) {
		return std::nullopt;
	}
	const auto path = convertBlockPath(range.block);
	if (!path) {
		return std::nullopt;
	}
	const auto owner = block(*path);
	if (!owner
		|| owner->kind != BlockKind::Table
		|| range.from < 0
		|| range.till > int(owner->tableRows.size())) {
		return std::nullopt;
	}
	return StructuralTableRowRange{
		.block = *path,
		.from = range.from,
		.till = range.till,
	};
}

std::optional<State::StructuralTableCellRange> State::validateTableCellRange(
		const Markdown::PreparedEditTableCellRange &range) const {
	if (range.empty()) {
		return std::nullopt;
	}
	const auto path = convertBlockPath(range.block);
	if (!path) {
		return std::nullopt;
	}
	const auto owner = block(*path);
	if (!owner || owner->kind != BlockKind::Table) {
		return std::nullopt;
	}
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	if (range.rowFrom < 0
		|| range.rowTill > grid.rowCount
		|| range.columnFrom < 0
		|| range.columnTill > grid.columnCount) {
		return std::nullopt;
	}
	auto result = StructuralTableCellRange{
		.block = *path,
		.rowFrom = range.rowFrom,
		.rowTill = range.rowTill,
		.columnFrom = range.columnFrom,
		.columnTill = range.columnTill,
	};
	if (SelectedTableGridCells(grid, result).empty()) {
		return std::nullopt;
	}
	return result;
}

bool State::leafWillBeRemoved(
		const LeafPath &path,
		const StructuralBlockRange &range) const {
	const auto index = BlockIndexInContainer(path, range.container);
	return index && IndexInRange(*index, range.from, range.till);
}

bool State::leafWillBeRemoved(
		const LeafPath &path,
		const StructuralListItemRange &range) const {
	const auto index = ListItemIndexForLeaf(path, range.block);
	return index && IndexInRange(*index, range.from, range.till);
}

bool State::leafWillBeRemoved(
		const LeafPath &path,
		const StructuralTableRowRange &range) const {
	const auto index = TableRowIndexForLeaf(path, range.block);
	return index && IndexInRange(*index, range.from, range.till);
}

bool State::leafWillBeRemoved(
		const LeafPath &,
		const StructuralTableCellRange &) const {
	return false;
}

bool State::leafBelongsToBlock(
		const LeafPath &leaf,
		const BlockPath &path) const {
	const auto &owner = leaf.block;
	if (owner == path) {
		return true;
	}
	if (!ContainerHasPrefix(owner.container, path.container)) {
		return false;
	}
	if (owner.container.steps.size() <= path.container.steps.size()) {
		return false;
	}
	const auto &step = owner.container.steps[path.container.steps.size()];
	return step.blockIndex == path.index
		&& (step.kind == BlockContainerKind::BlockChildren
			|| step.kind == BlockContainerKind::ListItemChildren);
}

std::optional<State::LeafPath> State::firstSelectedLeaf(
		const StructuralTableCellRange &range) const {
	const auto owner = block(range.block);
	if (!owner || owner->kind != BlockKind::Table) {
		return std::nullopt;
	}
	const auto selected = SelectedTableGridCells(
		BuildTableGrid(*owner, tableRenderLimits()),
		range);
	for (const auto &descriptor : _textNodes) {
		const auto &leaf = descriptor.leaf;
		if (leaf.block != range.block
			|| leaf.kind != LeafKind::TableCellText) {
			continue;
		}
		for (const auto &reference : selected) {
			if (TableGridCellMatchesLeaf(reference, leaf, range.block)) {
				return leaf;
			}
		}
	}
	return std::nullopt;
}

std::optional<State::LeafPath> State::adjacentLeafOutsideRange(
		const StructuralBlockRange &range,
		bool forward) const {
	if (forward) {
		for (const auto &descriptor : _textNodes) {
			const auto index = BlockIndexInContainer(
				descriptor.leaf,
				range.container);
			if (index && *index >= range.till) {
				return descriptor.leaf;
			}
		}
	} else {
		for (auto i = textNodeCount(); i != 0; --i) {
			const auto &leaf = _textNodes[i - 1].leaf;
			const auto index = BlockIndexInContainer(leaf, range.container);
			if (index && *index < range.from) {
				return leaf;
			}
		}
	}
	return std::nullopt;
}

std::optional<State::LeafPath> State::adjacentLeafOutsideRange(
		const StructuralListItemRange &range,
		bool forward) const {
	if (forward) {
		for (const auto &descriptor : _textNodes) {
			const auto index = ListItemIndexForLeaf(
				descriptor.leaf,
				range.block);
			if (index && *index >= range.till) {
				return descriptor.leaf;
			}
		}
	} else {
		for (auto i = textNodeCount(); i != 0; --i) {
			const auto &leaf = _textNodes[i - 1].leaf;
			const auto index = ListItemIndexForLeaf(leaf, range.block);
			if (index && *index < range.from) {
				return leaf;
			}
		}
	}
	return std::nullopt;
}

std::optional<State::LeafPath> State::adjacentLeafOutsideRange(
		const StructuralTableRowRange &range,
		bool forward) const {
	if (forward) {
		for (const auto &descriptor : _textNodes) {
			const auto index = TableRowIndexForLeaf(
				descriptor.leaf,
				range.block);
			if (index && *index >= range.till) {
				return descriptor.leaf;
			}
		}
	} else {
		for (auto i = textNodeCount(); i != 0; --i) {
			const auto &leaf = _textNodes[i - 1].leaf;
			const auto index = TableRowIndexForLeaf(leaf, range.block);
			if (index && *index < range.from) {
				return leaf;
			}
		}
	}
	return std::nullopt;
}

std::optional<State::LeafPath> State::adjacentLeafOutsideRange(
		const StructuralTableCellRange &range,
		bool forward) const {
	const auto owner = block(range.block);
	if (!owner || owner->kind != BlockKind::Table) {
		return std::nullopt;
	}
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	const auto leafIntersectsRange = [&](const LeafPath &leaf) {
		if (leaf.block != range.block
			|| leaf.kind != LeafKind::TableCellText) {
			return false;
		}
		for (const auto &reference : grid.cells) {
			if (TableGridCellMatchesLeaf(reference, leaf, range.block)) {
				return TableGridCellIntersectsRange(reference, range);
			}
		}
		return false;
	};
	auto fallback = std::optional<LeafPath>();
	auto foundIntersecting = false;
	if (forward) {
		for (const auto &descriptor : _textNodes) {
			if (descriptor.leaf.block == range.block
				&& descriptor.leaf.kind == LeafKind::TableCellText) {
				if (leafIntersectsRange(descriptor.leaf)) {
					foundIntersecting = true;
				} else if (foundIntersecting) {
					return descriptor.leaf;
				} else if (!fallback) {
					fallback = descriptor.leaf;
				}
			}
		}
	} else {
		for (auto i = textNodeCount(); i != 0; --i) {
			const auto &leaf = _textNodes[i - 1].leaf;
			if (leaf.block == range.block
				&& leaf.kind == LeafKind::TableCellText) {
				if (leafIntersectsRange(leaf)) {
					foundIntersecting = true;
				} else if (foundIntersecting) {
					return leaf;
				} else if (!fallback) {
					fallback = leaf;
				}
			}
		}
	}
	return foundIntersecting ? std::nullopt : fallback;
}

std::optional<State::LeafPath> State::fallbackFocusLeaf() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (descriptor) {
		return descriptor->leaf;
	}
	return !_textNodes.empty()
		? std::make_optional(_textNodes.front().leaf)
		: std::nullopt;
}

std::optional<State::LeafPath> State::plannedFocusForRange(
		const StructuralBlockRange &range,
		bool forward) const {
	if (const auto adjacent = adjacentLeafOutsideRange(range, forward)) {
		return adjacent;
	}
	const auto descriptor = textNode(_activeTextOrdinal);
	if (descriptor && !leafWillBeRemoved(descriptor->leaf, range)) {
		return descriptor->leaf;
	}
	for (const auto &fallback : _textNodes) {
		if (!leafWillBeRemoved(fallback.leaf, range)) {
			return fallback.leaf;
		}
	}
	return fallbackFocusLeaf();
}

std::optional<State::LeafPath> State::plannedFocusForRange(
		const StructuralListItemRange &range,
		bool forward) const {
	if (const auto adjacent = adjacentLeafOutsideRange(range, forward)) {
		return adjacent;
	}
	const auto ownerRange = StructuralBlockRange{
		.container = range.block.container,
		.from = range.block.index,
		.till = range.block.index + 1,
	};
	if (const auto adjacent = adjacentLeafOutsideRange(ownerRange, forward)) {
		return adjacent;
	}
	const auto descriptor = textNode(_activeTextOrdinal);
	if (descriptor && !leafWillBeRemoved(descriptor->leaf, range)) {
		return descriptor->leaf;
	}
	for (const auto &fallback : _textNodes) {
		if (!leafWillBeRemoved(fallback.leaf, range)) {
			return fallback.leaf;
		}
	}
	return fallbackFocusLeaf();
}

std::optional<State::LeafPath> State::plannedFocusForRange(
		const StructuralTableRowRange &range,
		bool forward) const {
	if (const auto adjacent = adjacentLeafOutsideRange(range, forward)) {
		return adjacent;
	}
	const auto ownerRange = StructuralBlockRange{
		.container = range.block.container,
		.from = range.block.index,
		.till = range.block.index + 1,
	};
	if (const auto adjacent = adjacentLeafOutsideRange(ownerRange, forward)) {
		return adjacent;
	}
	const auto descriptor = textNode(_activeTextOrdinal);
	if (descriptor && !leafWillBeRemoved(descriptor->leaf, range)) {
		return descriptor->leaf;
	}
	for (const auto &fallback : _textNodes) {
		if (!leafWillBeRemoved(fallback.leaf, range)) {
			return fallback.leaf;
		}
	}
	return fallbackFocusLeaf();
}

std::optional<State::LeafPath> State::plannedFocusForRange(
		const StructuralTableCellRange &range,
		bool forward) const {
	if (const auto selected = firstSelectedLeaf(range)) {
		return selected;
	}
	if (const auto adjacent = adjacentLeafOutsideRange(range, forward)) {
		return adjacent;
	}
	return fallbackFocusLeaf();
}

PreparedEditSelection State::preparedSelectionForBlock(
		const BlockPath &path) const {
	return {
		.kind = PreparedEditSelectionKind::Blocks,
		.blocks = {
			.container = ToPreparedBlockContainerPath(path.container),
			.from = path.index,
			.till = path.index + 1,
		},
	};
}

PreparedEditSelection State::preparedSelectionForBlockRange(
		const StructuralBlockRange &range) const {
	return {
		.kind = PreparedEditSelectionKind::Blocks,
		.blocks = {
			.container = ToPreparedBlockContainerPath(range.container),
			.from = range.from,
			.till = range.till,
		},
	};
}

PreparedEditSelection State::preparedSelectionForListItem(
		const BlockPath &path,
		int itemIndex) const {
	return preparedSelectionForListItems(path, itemIndex, itemIndex + 1);
}

PreparedEditSelection State::preparedSelectionForListItems(
		const BlockPath &path,
		int from,
		int till) const {
	return {
		.kind = PreparedEditSelectionKind::ListItems,
		.listItems = {
			.block = PreparedBlockPath{
				.container = ToPreparedBlockContainerPath(path.container),
				.index = path.index,
			},
			.from = from,
			.till = till,
		},
	};
}

bool State::descriptorBelongsToBlock(
		const TextNodeDescriptor &descriptor,
		const BlockPath &path) const {
	return leafBelongsToBlock(descriptor.leaf, path);
}

bool State::removalTargetIsEmpty(const RemovalTarget &target) const {
	switch (target.kind) {
	case RemovalKind::Block:
		if (const auto owner = block(target.block)) {
			return BlockIsEmpty(*owner);
		}
		return false;
	case RemovalKind::ListItem:
		if (const auto item = listItem(target.block, target.listItemIndex)) {
			return ListItemIsEmpty(*item);
		}
		return false;
	case RemovalKind::TableCell:
		if (const auto cell = tableCell(
				target.block,
				target.tableRowIndex,
				target.tableCellIndex)) {
			return RichTextIsEmpty(cell->text);
		}
		return false;
	}
	return false;
}

bool State::removeTarget(const RemovalTarget &target) {
	clearTemporaryDownParagraph();
	switch (target.kind) {
	case RemovalKind::Block: {
		const auto blocks = blockContainer(target.block.container);
		if (!blocks
			|| target.block.index < 0
			|| target.block.index >= int(blocks->size())) {
			return false;
		}
		blocks->erase(blocks->begin() + target.block.index);
		return true;
	}
	case RemovalKind::ListItem: {
		const auto owner = block(target.block);
		if (!owner
			|| target.listItemIndex < 0
			|| target.listItemIndex >= int(owner->listItems.size())) {
			return false;
		}
		owner->listItems.erase(owner->listItems.begin() + target.listItemIndex);
		return true;
	}
	case RemovalKind::TableCell: {
		const auto cell = tableCell(
			target.block,
			target.tableRowIndex,
			target.tableCellIndex);
		if (!cell) {
			return false;
		}
		cell->text = RichText();
		return true;
	}
	}
	return false;
}

} // namespace Iv::Editor
