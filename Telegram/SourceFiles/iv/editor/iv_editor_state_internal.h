/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/editor/iv_editor_state.h"
#include "iv/editor/iv_editor_page_table_grid.h"
#include "iv/editor/iv_editor_page_media.h"

#include <algorithm>

namespace Iv::Editor::StateDetails {

using Block = RichPage::Block;
using BlockContainerKind = State::BlockContainerKind;
using BlockContainerPath = State::BlockContainerPath;
using ApplyResult = State::ApplyResult;
using BlockKind = RichPage::BlockKind;
using BoundaryAction = State::BoundaryTarget::Action;
using BlockPath = State::BlockPath;
using FieldMode = State::FieldMode;
using InsertBlockType = State::InsertBlockType;
using InsertionAnchor = State::InsertionAnchor;
using LeafKind = State::LeafKind;
using LeafPath = State::LeafPath;
using ListStyle = State::ListStyle;
using ListItem = RichPage::ListItem;
using ListKind = RichPage::ListKind;
using NativeInstantViewLeafUpdateResult
	= Markdown::NativeInstantViewLeafUpdateResult;
using PreparedBlockContainerKind = Markdown::PreparedEditBlockContainerKind;
using PreparedEditLeafKind = Markdown::PreparedEditLeafKind;
using PreparedEditLeafSource = Markdown::PreparedEditLeafSource;
using PreparedEditListItemRange = Markdown::PreparedEditListItemRange;
using PreparedEditListItemSource = Markdown::PreparedEditListItemSource;
using PreparedEditSelectionKind = Markdown::PreparedEditSelectionKind;
using PreparedBlockPath = Markdown::PreparedEditBlockPath;
using PreparedEditSelection = Markdown::PreparedEditSelection;
using PreparedMutationKind = State::PreparedMutationKind;
using PreparedOrderedListType = Markdown::PreparedOrderedListType;
using ReplaceTarget = State::ReplaceTarget;
using RemovalKind = State::RemovalKind;
using RemovalTarget = State::RemovalTarget;
using RichText = RichPage::RichText;
using OrderedListData = RichPage::OrderedListData;
using TableCell = RichPage::TableCell;
using TableRow = RichPage::TableRow;
using TaskState = RichPage::TaskState;
using TextNodeDescriptor = State::TextNodeDescriptor;
using TextFormattingAction = State::TextFormattingAction;
using TextSelectionDropResult = State::TextSelectionDropResult;
using TextNodeSpan = State::TextNodeSpan;

[[nodiscard]] TextWithEntities JoinText(
		TextWithEntities before,
		TextWithEntities selected,
		TextWithEntities after);

void ExpandInsertContextToActiveLine(State::ActiveTextInsertContext &context);

[[nodiscard]] bool IsListInsertType(State::InsertBlockType type);

[[nodiscard]] std::vector<TextWithEntities> SplitTextIntoLines(
		const TextWithEntities &text);

[[nodiscard]] const QString *FormattingActionTag(
		TextFormattingAction action);

[[nodiscard]] QString TagWithoutInstantViewMath(QStringView tag);

[[nodiscard]] QString TagWithoutOppositeScript(
		const QString &tag,
		const QString &added);

[[nodiscard]] QString TagWithAddedDroppingConflicts(
		const QString &tag,
		const QString &added);

void SortTags(TextWithTags::Tags *tags);

void OverlayTag(
		TextWithTags::Tags *tags,
		const TextWithTags::Tag &overlay,
		const QString &text);

void RemoveTagFromSelection(
		TextWithTags::Tags *tags,
		const QString &tag);

bool SetMediaBlockSpoiler(Block *block, bool enabled);

[[nodiscard]] bool IsReplaceableMediaBlockKind(BlockKind kind);

[[nodiscard]] bool ValidRowButtonIndex(const Block *owner, int index);

[[nodiscard]] RichPage::Button NormalizedRowButton(RichPage::Button button);

[[nodiscard]] std::optional<uint64> ReplaceTargetMediaId(const Block &block);

[[nodiscard]] bool BlockMatchesReplaceTarget(
		const Block &block,
		const ReplaceTarget &target);

[[nodiscard]] std::optional<uint64> ReplaceTargetMediaId(
		const RichPage::GroupedMediaItem &item);

[[nodiscard]] bool GroupedItemMatchesReplaceTarget(
		const RichPage::GroupedMediaItem &item,
		const ReplaceTarget &target);

[[nodiscard]] bool BlockHasGroupingCaptionOrAnchor(const Block &block);

[[nodiscard]] bool ContainerStartsWith(
		const BlockContainerPath &container,
		const BlockContainerPath &prefix);

[[nodiscard]] bool ContainerHasPrefix(
		const BlockContainerPath &path,
		const BlockContainerPath &prefix);

[[nodiscard]] bool IndexInRange(int index, int from, int till);

[[nodiscard]] bool ShiftBlockContainerPathAfterRemovedBlock(
		BlockContainerPath &path,
		const BlockPath &removed);

[[nodiscard]] bool ShiftBlockPathAfterRemovedBlock(
		BlockPath &path,
		const BlockPath &removed);

[[nodiscard]] bool ShiftBlockContainerPathAfterRemovedListItem(
		BlockContainerPath &path,
		const BlockPath &list,
		int removedItemIndex);

[[nodiscard]] bool ShiftBlockPathAfterRemovedListItem(
		BlockPath &path,
		const BlockPath &list,
		int removedItemIndex);

[[nodiscard]] LeafPath ShiftLeafPathAfterRemovedBlocks(
		LeafPath leaf,
		const BlockContainerPath &container,
		int from,
		int till);

[[nodiscard]] LeafPath ShiftLeafPathAfterRemovedListItems(
		LeafPath leaf,
		const BlockPath &list,
		int from,
		int till);

[[nodiscard]] std::optional<int> BlockIndexInContainer(
		const LeafPath &leaf,
		const BlockContainerPath &container);

[[nodiscard]] std::optional<int> ListItemIndexForLeaf(
		const LeafPath &leaf,
		const BlockPath &block);

[[nodiscard]] std::optional<int> TableRowIndexForLeaf(
		const LeafPath &leaf,
		const BlockPath &block);

[[nodiscard]] std::optional<int> TableCellIndexForLeaf(
		const LeafPath &leaf,
		const BlockPath &block,
		int rowIndex);

template <typename Range>
[[nodiscard]] bool TableGridCellContainedInRange(
		const TableGridCellReference &cell,
		const Range &range) {
	return (cell.rowFrom >= range.rowFrom)
		&& (cell.rowTill <= range.rowTill)
		&& (cell.columnFrom >= range.columnFrom)
		&& (cell.columnTill <= range.columnTill);
}

template <typename Range>
[[nodiscard]] bool TableGridRangeCovered(
		const TableGrid &grid,
		const Range &range) {
	if ((range.rowFrom < 0)
		|| (range.rowTill <= range.rowFrom)
		|| (range.columnFrom < 0)
		|| (range.columnTill <= range.columnFrom)
		|| (range.rowTill > grid.rowCount)
		|| (range.columnTill > grid.columnCount)) {
		return false;
	}
	for (auto row = range.rowFrom; row != range.rowTill; ++row) {
		if (row >= int(grid.occupancy.size())) {
			return false;
		}
		const auto &occupied = grid.occupancy[row];
		for (auto column = range.columnFrom;
				column != range.columnTill;
				++column) {
			if (column >= int(occupied.size()) || !occupied[column]) {
				return false;
			}
		}
	}
	return true;
}

template <typename Range>
[[maybe_unused]] [[nodiscard]] bool CleanTableGridUniteRange(
		const TableGrid &grid,
		const Range &range) {
	const auto selected = SelectedTableGridCells(grid, range);
	if (selected.empty()) {
		return false;
	}
	for (const auto &cell : selected) {
		if (!TableGridCellContainedInRange(cell, range)) {
			return false;
		}
	}
	return TableGridRangeCovered(grid, range);
}

template <typename Range>
[[nodiscard]] bool TableGridRangeSpansAllRows(
		const TableGrid &grid,
		const Range &range) {
	return (range.rowFrom == 0)
		&& (range.rowTill == grid.rowCount)
		&& (range.columnFrom >= 0)
		&& (range.columnTill > range.columnFrom)
		&& (range.columnTill <= grid.columnCount);
}

template <typename Range>
[[nodiscard]] bool TableGridRangeSpansAllColumns(
		const TableGrid &grid,
		const Range &range) {
	return (range.rowFrom >= 0)
		&& (range.rowTill > range.rowFrom)
		&& (range.rowTill <= grid.rowCount)
		&& (range.columnFrom == 0)
		&& (range.columnTill == grid.columnCount);
}

template <typename Range>
[[nodiscard]] bool TableGridRangeCoversFullTable(
		const TableGrid &grid,
		const Range &range) {
	return TableGridRangeSpansAllRows(grid, range)
		&& TableGridRangeSpansAllColumns(grid, range);
}

template <typename Range>
[[nodiscard]] int TableGridCellColumnIntersection(
		const TableGridCellReference &cell,
		const Range &range) {
	const auto from = std::max(cell.columnFrom, range.columnFrom);
	const auto till = std::min(cell.columnTill, range.columnTill);
	return std::max(till - from, 0);
}

[[nodiscard]] bool TableGridCellMatchesLeaf(
		const TableGridCellReference &cell,
		const LeafPath &leaf,
		const BlockPath &block);

[[nodiscard]] TableCell MakeDefaultTableCell();

[[nodiscard]] TableCell MakeDefaultTableCell(bool header);

[[nodiscard]] const TableCell *TableGridCellAt(
		const Block &table,
		const TableGrid &grid,
		int row,
		int column);

[[nodiscard]] int IncrementTableSpan(int span);

void InsertTableCellBeforeVisualColumn(
		TableRow *row,
		const TableGrid &grid,
		int rowIndex,
		int column,
		TableCell insertedCell);

} // namespace Iv::Editor::StateDetails

namespace Iv::Editor {

template <typename Result, typename Callback>
Result State::applyCheckedMutation(Result failure, Callback &&callback) {
	_lastLimitError = std::nullopt;
	auto candidate = State(
		std::make_shared<RichPage>(*_richPage),
		_mediaRuntime,
		_limits);
	candidate._activeTextOrdinal = _activeTextOrdinal;
	candidate._lastLimitError = std::nullopt;
	candidate._temporaryDownParagraph = _temporaryDownParagraph;
	const auto outcome = callback(candidate);
	if (!outcome.apply) {
		return outcome.result;
	}
	if (DegradeEditModeButtons(candidate._richPage->blocks)) {
		candidate.rebuildPrepared();
	}
	if (const auto error = ValidateRichMessage(*candidate._richPage, _limits)) {
		_lastLimitError = error;
		return failure;
	}
	commitCheckedMutation(std::move(candidate));
	return outcome.result;
}

} // namespace Iv::Editor
