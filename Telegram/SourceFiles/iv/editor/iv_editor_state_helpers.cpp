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

namespace Iv::Editor::StateDetails {

[[nodiscard]] TextWithEntities JoinText(
		TextWithEntities before,
		TextWithEntities selected,
		TextWithEntities after) {
	before.append(std::move(selected));
	before.append(std::move(after));
	return before;
}

void ExpandInsertContextToActiveLine(State::ActiveTextInsertContext &context) {
	if (!context.selected.text.isEmpty()) {
		return;
	}
	const auto lineStart = context.before.text.lastIndexOf('\n');
	const auto lineEnd = context.after.text.indexOf('\n');
	auto lineHead = Ui::Text::Mid(context.before, lineStart + 1);
	auto lineTail = (lineEnd >= 0)
		? Ui::Text::Mid(context.after, 0, lineEnd)
		: context.after;
	auto newBefore = (lineStart >= 0)
		? Ui::Text::Mid(context.before, 0, lineStart)
		: TextWithEntities();
	auto newAfter = (lineEnd >= 0)
		? Ui::Text::Mid(context.after, lineEnd + 1)
		: TextWithEntities();
	lineHead.append(std::move(lineTail));
	context.before = std::move(newBefore);
	context.selected = std::move(lineHead);
	context.after = std::move(newAfter);
}

[[nodiscard]] bool IsListInsertType(State::InsertBlockType type) {
	return (type == State::InsertBlockType::OrderedList)
		|| (type == State::InsertBlockType::BulletList)
		|| (type == State::InsertBlockType::TaskList);
}

[[nodiscard]] std::vector<TextWithEntities> SplitTextIntoLines(
		const TextWithEntities &text) {
	auto result = std::vector<TextWithEntities>();
	const auto size = int(text.text.size());
	auto from = 0;
	while (from <= size) {
		const auto found = text.text.indexOf('\n', from);
		const auto till = (found < 0) ? size : found;
		auto part = Ui::Text::Mid(text, from, till - from);
		if (!part.text.trimmed().isEmpty()) {
			result.push_back(std::move(part));
		}
		if (found < 0) {
			break;
		}
		from = found + 1;
	}
	return result;
}

[[nodiscard]] const QString *FormattingActionTag(
		TextFormattingAction action) {
	switch (action) {
	case TextFormattingAction::Bold:
		return &Ui::InputField::kTagBold;
	case TextFormattingAction::Italic:
		return &Ui::InputField::kTagItalic;
	case TextFormattingAction::Underline:
		return &Ui::InputField::kTagUnderline;
	case TextFormattingAction::StrikeOut:
		return &Ui::InputField::kTagStrikeOut;
	case TextFormattingAction::Spoiler:
		return &Ui::InputField::kTagSpoiler;
	case TextFormattingAction::Subscript:
		return &Ui::InputField::kTagIvSubscript;
	case TextFormattingAction::Superscript:
		return &Ui::InputField::kTagIvSuperscript;
	case TextFormattingAction::Marked:
		return &Ui::InputField::kTagIvMarked;
	case TextFormattingAction::PlainText:
		return nullptr;
	}
	return nullptr;
}

[[nodiscard]] QString TagWithoutInstantViewMath(QStringView tag) {
	return TextUtilities::TagWithRemoved(
		tag.toString(),
		Ui::InputField::kTagIvMath);
}

[[nodiscard]] QString TagWithoutOppositeScript(
		const QString &tag,
		const QString &added) {
	if (added == Ui::InputField::kTagIvSubscript) {
		return TextUtilities::TagWithRemoved(
			tag,
			Ui::InputField::kTagIvSuperscript);
	} else if (added == Ui::InputField::kTagIvSuperscript) {
		return TextUtilities::TagWithRemoved(
			tag,
			Ui::InputField::kTagIvSubscript);
	}
	return tag;
}

[[nodiscard]] QString TagWithAddedDroppingConflicts(
		const QString &tag,
		const QString &added) {
	if (added == Ui::InputField::kTagIvMath) {
		return Ui::InputField::kTagIvMath;
	}
	return TextUtilities::TagWithAdded(
		TagWithoutOppositeScript(TagWithoutInstantViewMath(tag), added),
		added);
}

void SortTags(TextWithTags::Tags *tags) {
	std::sort(tags->begin(), tags->end(), [](const auto &a, const auto &b) {
		if (a.offset != b.offset) {
			return a.offset < b.offset;
		} else if (a.length != b.length) {
			return a.length < b.length;
		}
		return a.id < b.id;
	});
}

void OverlayTag(
		TextWithTags::Tags *tags,
		const TextWithTags::Tag &overlay,
		const QString &text) {
	if (overlay.id.isEmpty()
		|| overlay.length <= 0
		|| !RangeInsideText(text, overlay.offset, overlay.length)) {
		return;
	}
	const auto from = overlay.offset;
	const auto till = from + overlay.length;
	auto coveredTill = from;
	auto result = TextWithTags::Tags();
	result.reserve(tags->size() + 3);

	for (const auto &tag : *tags) {
		const auto tagFrom = tag.offset;
		const auto tagTill = tag.offset + tag.length;
		if (tagTill <= from) {
			result.push_back(tag);
			continue;
		} else if (tagFrom >= till) {
			if (coveredTill < till) {
				result.push_back({
					.offset = coveredTill,
					.length = till - coveredTill,
					.id = overlay.id,
				});
				coveredTill = till;
			}
			result.push_back(tag);
			continue;
		}
		if (tagFrom > coveredTill) {
			result.push_back({
				.offset = coveredTill,
				.length = tagFrom - coveredTill,
				.id = overlay.id,
			});
			coveredTill = tagFrom;
		}
		if (tagFrom < from) {
			result.push_back({
				.offset = tagFrom,
				.length = from - tagFrom,
				.id = tag.id,
			});
		}
		const auto middleFrom = std::max(tagFrom, from);
		const auto middleTill = std::min(tagTill, till);
		if (middleFrom < middleTill) {
			result.push_back({
				.offset = middleFrom,
				.length = middleTill - middleFrom,
				.id = TagWithAddedDroppingConflicts(tag.id, overlay.id),
			});
			coveredTill = middleTill;
		}
		if (tagTill > till) {
			result.push_back({
				.offset = till,
				.length = tagTill - till,
				.id = tag.id,
			});
		}
	}
	if (coveredTill < till) {
		result.push_back({
			.offset = coveredTill,
			.length = till - coveredTill,
			.id = overlay.id,
		});
	}
	SortTags(&result);
	*tags = TextUtilities::SimplifyTags(std::move(result));
}

void RemoveTagFromSelection(
		TextWithTags::Tags *tags,
		const QString &tag) {
	auto result = TextWithTags::Tags();
	result.reserve(tags->size());
	for (const auto &existing : *tags) {
		const auto updated = TextUtilities::TagWithRemoved(existing.id, tag);
		if (!updated.isEmpty()) {
			result.push_back({
				.offset = existing.offset,
				.length = existing.length,
				.id = updated,
			});
		}
	}
	*tags = std::move(result);
}

bool SetMediaBlockSpoiler(Block *block, bool enabled) {
	if (!block || !MediaBlockSupportsSpoiler(*block)) {
		return false;
	} else if (block->kind == BlockKind::GroupedMedia) {
		auto changed = false;
		for (auto &item : block->mediaItems) {
			if ((item.kind != BlockKind::Photo)
				&& (item.kind != BlockKind::Video)
				&& (item.kind != BlockKind::Audio)
				&& (item.kind != BlockKind::Map)) {
				continue;
			}
			if (item.spoiler != enabled) {
				item.spoiler = enabled;
				changed = true;
			}
		}
		return changed;
	} else if (block->spoiler != enabled) {
		block->spoiler = enabled;
		return true;
	}
	return false;
}

[[nodiscard]] bool IsReplaceableMediaBlockKind(BlockKind kind) {
	return IsPhotoVideoBlockKind(kind)
		|| (kind == BlockKind::Audio)
		|| (kind == BlockKind::File);
}

[[nodiscard]] bool ValidRowButtonIndex(const Block *owner, int index) {
	return owner
		&& (owner->kind == BlockKind::ButtonRow)
		&& (index >= 0)
		&& (index < int(owner->buttons.size()));
}

[[nodiscard]] RichPage::Button NormalizedRowButton(RichPage::Button button) {
	button.text.text = Markdown::NormalizeRichButtonLabel(
		std::move(button.text.text));
	button.button.text = button.text.text.text;
	return button;
}

[[nodiscard]] std::optional<uint64> ReplaceTargetMediaId(const Block &block) {
	switch (block.kind) {
	case BlockKind::Photo:
		return block.photoId ? std::make_optional(block.photoId) : std::nullopt;
	case BlockKind::Video:
	case BlockKind::Audio:
	case BlockKind::File:
		return block.documentId
			? std::make_optional(block.documentId)
			: std::nullopt;
	default:
		return std::nullopt;
	}
}

[[nodiscard]] bool BlockMatchesReplaceTarget(
		const Block &block,
		const ReplaceTarget &target) {
	const auto mediaId = ReplaceTargetMediaId(block);
	return (block.kind == target.kind)
		&& mediaId
		&& (*mediaId == target.mediaId);
}

[[nodiscard]] std::optional<uint64> ReplaceTargetMediaId(
		const RichPage::GroupedMediaItem &item) {
	switch (item.kind) {
	case BlockKind::Photo:
		return item.photoId ? std::make_optional(item.photoId) : std::nullopt;
	case BlockKind::Video:
		return item.documentId
			? std::make_optional(item.documentId)
			: std::nullopt;
	default:
		return std::nullopt;
	}
}

[[nodiscard]] bool GroupedItemMatchesReplaceTarget(
		const RichPage::GroupedMediaItem &item,
		const ReplaceTarget &target) {
	const auto mediaId = ReplaceTargetMediaId(item);
	return (item.kind == target.kind)
		&& mediaId
		&& (*mediaId == target.mediaId);
}

[[nodiscard]] bool BlockHasGroupingCaptionOrAnchor(const Block &block) {
	return !GroupingRichTextIsEmpty(block.caption)
		|| !block.anchorId.isEmpty();
}

[[nodiscard]] bool ContainerStartsWith(
		const BlockContainerPath &container,
		const BlockContainerPath &prefix) {
	if (container.steps.size() < prefix.steps.size()) {
		return false;
	}
	for (auto i = 0, count = int(prefix.steps.size()); i != count; ++i) {
		const auto &a = container.steps[i];
		const auto &b = prefix.steps[i];
		if (a.kind != b.kind
			|| a.blockIndex != b.blockIndex
			|| a.listItemIndex != b.listItemIndex) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool ContainerHasPrefix(
		const BlockContainerPath &path,
		const BlockContainerPath &prefix) {
	if (path.steps.size() < prefix.steps.size()) {
		return false;
	}
	return std::equal(
		prefix.steps.begin(),
		prefix.steps.end(),
		path.steps.begin());
}

[[nodiscard]] bool IndexInRange(int index, int from, int till) {
	return (index >= from) && (index < till);
}

[[nodiscard]] bool ShiftBlockContainerPathAfterRemovedBlock(
		BlockContainerPath &path,
		const BlockPath &removed) {
	if (!ContainerHasPrefix(path, removed.container)) {
		return true;
	}
	const auto size = removed.container.steps.size();
	if (path.steps.size() == size) {
		return true;
	}
	auto &step = path.steps[size];
	if (step.blockIndex == removed.index) {
		return false;
	} else if (step.blockIndex > removed.index) {
		--step.blockIndex;
	}
	return true;
}

[[nodiscard]] bool ShiftBlockPathAfterRemovedBlock(
		BlockPath &path,
		const BlockPath &removed) {
	if (path.container == removed.container) {
		if (path.index == removed.index) {
			return false;
		} else if (path.index > removed.index) {
			--path.index;
		}
		return true;
	}
	return ShiftBlockContainerPathAfterRemovedBlock(path.container, removed);
}

[[nodiscard]] bool ShiftBlockContainerPathAfterRemovedListItem(
		BlockContainerPath &path,
		const BlockPath &list,
		int removedItemIndex) {
	const auto removed = ListItemChildrenContainer(list, removedItemIndex);
	if (ContainerHasPrefix(path, removed)) {
		return false;
	}
	if (!ContainerHasPrefix(path, list.container)) {
		return true;
	}
	const auto size = list.container.steps.size();
	if (path.steps.size() <= size) {
		return true;
	}
	auto &step = path.steps[size];
	if (step.blockIndex != list.index
		|| step.kind != BlockContainerKind::ListItemChildren) {
		return true;
	}
	if (step.listItemIndex == removedItemIndex) {
		return false;
	} else if (step.listItemIndex > removedItemIndex) {
		--step.listItemIndex;
	}
	return true;
}

[[nodiscard]] bool ShiftBlockPathAfterRemovedListItem(
		BlockPath &path,
		const BlockPath &list,
		int removedItemIndex) {
	return ShiftBlockContainerPathAfterRemovedListItem(
		path.container,
		list,
		removedItemIndex);
}

[[nodiscard]] LeafPath ShiftLeafPathAfterRemovedBlocks(
		LeafPath leaf,
		const BlockContainerPath &container,
		int from,
		int till) {
	auto shifted = leaf.block;
	for (auto i = till; i != from;) {
		--i;
		if (!ShiftBlockPathAfterRemovedBlock(shifted, BlockPath{
				.container = container,
				.index = i,
			})) {
			return leaf;
		}
	}
	leaf.block = shifted;
	return leaf;
}

[[nodiscard]] LeafPath ShiftLeafPathAfterRemovedListItems(
		LeafPath leaf,
		const BlockPath &list,
		int from,
		int till) {
	if (leaf.block == list && leaf.kind == LeafKind::ListItemText) {
		if (leaf.listItemIndex >= till) {
			leaf.listItemIndex -= (till - from);
		}
		return leaf;
	}
	auto shifted = leaf.block;
	for (auto i = till; i != from;) {
		--i;
		if (!ShiftBlockPathAfterRemovedListItem(shifted, list, i)) {
			return leaf;
		}
	}
	leaf.block = shifted;
	return leaf;
}

[[nodiscard]] std::optional<int> BlockIndexInContainer(
		const LeafPath &leaf,
		const BlockContainerPath &container) {
	if (leaf.block.container == container) {
		return leaf.block.index;
	}
	if (!ContainerHasPrefix(leaf.block.container, container)
		|| leaf.block.container.steps.size() <= container.steps.size()) {
		return std::nullopt;
	}
	const auto &step = leaf.block.container.steps[container.steps.size()];
	return (step.kind == BlockContainerKind::BlockChildren
			|| step.kind == BlockContainerKind::ListItemChildren)
		? std::make_optional(step.blockIndex)
		: std::nullopt;
}

[[nodiscard]] std::optional<int> ListItemIndexForLeaf(
		const LeafPath &leaf,
		const BlockPath &block) {
	if (leaf.block == block && leaf.kind == LeafKind::ListItemText) {
		return leaf.listItemIndex;
	}
	if (!ContainerHasPrefix(leaf.block.container, block.container)
		|| leaf.block.container.steps.size() <= block.container.steps.size()) {
		return std::nullopt;
	}
	const auto &step = leaf.block.container.steps[block.container.steps.size()];
	return (step.kind == BlockContainerKind::ListItemChildren
			&& step.blockIndex == block.index)
		? std::make_optional(step.listItemIndex)
		: std::nullopt;
}

[[nodiscard]] std::optional<int> TableRowIndexForLeaf(
		const LeafPath &leaf,
		const BlockPath &block) {
	if (!(leaf.block == block)) {
		return std::nullopt;
	}
	if (leaf.kind == LeafKind::BlockText) {
		return -1;
	}
	return (leaf.kind == LeafKind::TableCellText)
		? std::make_optional(leaf.tableRowIndex)
		: std::nullopt;
}

[[nodiscard]] std::optional<int> TableCellIndexForLeaf(
		const LeafPath &leaf,
		const BlockPath &block,
		int rowIndex) {
	return (leaf.block == block
			&& leaf.kind == LeafKind::TableCellText
			&& leaf.tableRowIndex == rowIndex)
		? std::make_optional(leaf.tableCellIndex)
		: std::nullopt;
}

[[nodiscard]] bool TableGridCellMatchesLeaf(
		const TableGridCellReference &cell,
		const LeafPath &leaf,
		const BlockPath &block) {
	const auto index = TableCellIndexForLeaf(leaf, block, cell.rowIndex);
	return index && *index == cell.cellIndex;
}

[[nodiscard]] TableCell MakeDefaultTableCell() {
	return TableCell();
}

[[nodiscard]] TableCell MakeDefaultTableCell(bool header) {
	auto result = MakeDefaultTableCell();
	result.header = header;
	return result;
}

[[nodiscard]] const TableCell *TableGridCellAt(
		const Block &table,
		const TableGrid &grid,
		int row,
		int column) {
	if (row < 0 || row >= grid.rowCount || column < 0) {
		return nullptr;
	}
	for (const auto &reference : grid.cells) {
		if (reference.rowFrom <= row
			&& reference.rowTill > row
			&& reference.columnFrom <= column
			&& reference.columnTill > column) {
			return &table.tableRows[reference.rowIndex].cells[
				reference.cellIndex];
		}
	}
	return nullptr;
}

[[nodiscard]] int IncrementTableSpan(int span) {
	const auto normalized = NormalizeTableSpan(span);
	return (normalized == std::numeric_limits<int>::max())
		? normalized
		: normalized + 1;
}

void InsertTableCellBeforeVisualColumn(
		TableRow *row,
		const TableGrid &grid,
		int rowIndex,
		int column,
		TableCell insertedCell) {
	auto insertAt = int(row->cells.size());
	for (const auto &reference : grid.cells) {
		if (reference.rowIndex == rowIndex
			&& reference.columnFrom >= column) {
			insertAt = std::min(reference.cellIndex, int(row->cells.size()));
			break;
		}
	}
	row->cells.insert(
		row->cells.begin() + insertAt,
		std::move(insertedCell));
}

} // namespace Iv::Editor::StateDetails
