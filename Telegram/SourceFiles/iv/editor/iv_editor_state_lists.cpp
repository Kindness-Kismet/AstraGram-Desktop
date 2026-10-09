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

State::ListSelectionInfo State::listSelectionInfo(
		const PreparedEditListItemRange &range) const {
	const auto validated = validateListItemRange(range);
	const auto owner = validated ? block(validated->block) : nullptr;
	if (!validated || !owner || owner->kind != BlockKind::List) {
		return {};
	}
	auto result = ListSelectionInfo{
		.valid = true,
		.taskList = IsTaskList(owner->listItems),
		.wholeList = (validated->from == 0)
			&& (validated->till == int(owner->listItems.size())),
		.singleItem = (validated->till == validated->from + 1),
		.reversed = (owner->listKind == ListKind::Ordered)
			&& owner->orderedList.reversed,
		.selectedItems = validated->till - validated->from,
		.listKind = owner->listKind,
	};
	if (owner->listKind != ListKind::Ordered) {
		return result;
	}
	result.listOrderedType = ResolvePreparedOrderedListType(
		owner->orderedList.type);
	result.listOrderedUniform = ranges::all_of(
		owner->listItems,
		[&](const ListItem &item) {
			return EffectiveOrderedListType(*owner, item)
				== result.listOrderedType;
		});
	result.listRomanSupported = OrderedItemsSupportRoman(
		*owner,
		0,
		int(owner->listItems.size()));
	result.selectedRomanSupported = OrderedItemsSupportRoman(
		*owner,
		validated->from,
		validated->till);
	result.allOrderedDecimal = true;
	result.allOrderedLowerAlpha = true;
	result.allOrderedUpperAlpha = true;
	result.allOrderedLowerRoman = true;
	result.allOrderedUpperRoman = true;
	for (auto i = validated->from; i != validated->till; ++i) {
		const auto type = EffectiveOrderedListType(*owner, owner->listItems[i]);
		result.allOrderedDecimal = result.allOrderedDecimal
			&& (type == PreparedOrderedListType::Decimal);
		result.allOrderedLowerAlpha = result.allOrderedLowerAlpha
			&& (type == PreparedOrderedListType::LowerAlpha);
		result.allOrderedUpperAlpha = result.allOrderedUpperAlpha
			&& (type == PreparedOrderedListType::UpperAlpha);
		result.allOrderedLowerRoman = result.allOrderedLowerRoman
			&& (type == PreparedOrderedListType::LowerRoman);
		result.allOrderedUpperRoman = result.allOrderedUpperRoman
			&& (type == PreparedOrderedListType::UpperRoman);
	}
	return result;
}

std::optional<PreparedEditListItemRange> State::listContextRangeForSelection(
		const PreparedEditSelection &selection,
		const PreparedEditListItemSource &source) const {
	if (source.listItemIndex < 0) {
		return std::nullopt;
	}
	const auto sourceBlock = convertBlockPath(source.block);
	const auto owner = sourceBlock ? block(*sourceBlock) : nullptr;
	if (!sourceBlock
		|| !owner
		|| owner->kind != BlockKind::List
		|| source.listItemIndex >= int(owner->listItems.size())) {
		return std::nullopt;
	}
	switch (selection.kind) {
	case PreparedEditSelectionKind::ListItems: {
		const auto range = validateListItemRange(selection.listItems);
		if (!range
			|| range->block != *sourceBlock
			|| source.listItemIndex < range->from
			|| source.listItemIndex >= range->till) {
			return std::nullopt;
		}
		return selection.listItems;
	}
	case PreparedEditSelectionKind::Blocks: {
		const auto range = validateBlockRange(selection.blocks);
		if (!range
			|| sourceBlock->container != range->container
			|| sourceBlock->index < range->from
			|| sourceBlock->index >= range->till) {
			return std::nullopt;
		}
		return PreparedEditListItemRange{
			.block = source.block,
			.from = 0,
			.till = int(owner->listItems.size()),
		};
	}
	case PreparedEditSelectionKind::TableRows:
	case PreparedEditSelectionKind::TableCells:
	case PreparedEditSelectionKind::None:
		return std::nullopt;
	}
	return std::nullopt;
}

bool State::setListStyle(
		const PreparedEditListItemRange &range,
		ListStyle style) {
	const auto validated = validateListItemRange(range);
	auto owner = validated ? block(validated->block) : nullptr;
	if (!validated || !owner || owner->kind != BlockKind::List) {
		return false;
	}
	auto changed = false;
	const auto current = CurrentListStyle(*owner);
	if (current == style) {
		if (style == ListStyle::Ordered) {
			changed = ClearOrderedTaskStates(owner);
		}
		if (changed) {
			rebuild();
		}
		return true;
	}
	switch (style) {
	case ListStyle::Ordered:
		owner->listKind = ListKind::Ordered;
		owner->orderedList = {};
		changed = true;
		for (auto &item : owner->listItems) {
			if (item.taskState != TaskState::None) {
				item.taskState = TaskState::None;
			}
			item.number = {};
		}
		break;
	case ListStyle::Bullet:
		owner->listKind = ListKind::Bullet;
		changed = true;
		ResetNonOrderedListMetadata(owner);
		for (auto &item : owner->listItems) {
			if (item.taskState != TaskState::None) {
				item.taskState = TaskState::None;
				changed = true;
			}
		}
		break;
	case ListStyle::Task:
		owner->listKind = ListKind::Bullet;
		changed = ResetNonOrderedListMetadata(owner) || changed;
		for (auto &item : owner->listItems) {
			if (item.taskState == TaskState::None) {
				item.taskState = TaskState::Unchecked;
				changed = true;
			}
		}
		break;
	}
	if (changed) {
		rebuild();
	}
	return true;
}

bool State::setListOrderedType(
		const PreparedEditListItemRange &range,
		PreparedOrderedListType type) {
	const auto validated = validateListItemRange(range);
	auto owner = validated ? block(validated->block) : nullptr;
	if (!validated
		|| !owner
		|| owner->kind != BlockKind::List
		|| owner->listKind != ListKind::Ordered) {
		return false;
	}
	auto changed = false;
	const auto stored = StoredOrderedListType(type);
	if (owner->orderedList.type != stored) {
		owner->orderedList.type = stored;
		changed = true;
	}
	// Item overrides would shadow the new list type.
	changed = ClearOrderedListItemTypes(owner) || changed;
	if (changed) {
		ClearOrderedListRawMarkers(owner);
		rebuild();
	}
	return true;
}

bool State::setListOrderedReversed(
		const PreparedEditListItemRange &range,
		bool reversed) {
	const auto validated = validateListItemRange(range);
	auto owner = validated ? block(validated->block) : nullptr;
	if (!validated
		|| !owner
		|| owner->kind != BlockKind::List
		|| owner->listKind != ListKind::Ordered) {
		return false;
	}
	auto changed = false;
	if (owner->orderedList.reversed != reversed) {
		owner->orderedList.reversed = reversed;
		ClearOrderedListRawMarkers(owner);
		changed = true;
	}
	if (changed) {
		rebuild();
	}
	return true;
}

bool State::setListItemOrderedType(
		const PreparedEditListItemRange &range,
		std::optional<PreparedOrderedListType> type) {
	const auto validated = validateListItemRange(range);
	auto owner = validated ? block(validated->block) : nullptr;
	if (!validated
		|| !owner
		|| owner->kind != BlockKind::List
		|| owner->listKind != ListKind::Ordered) {
		return false;
	}
	auto changed = false;
	const auto parentType = ResolvePreparedOrderedListType(owner->orderedList.type);
	const auto stored = (type && (*type != parentType))
		? StoredOrderedListType(*type, (*type == PreparedOrderedListType::Decimal))
		: std::optional<QString>();
	for (auto i = validated->from; i != validated->till; ++i) {
		auto &item = owner->listItems[i];
		if (item.number.type != stored) {
			item.number.type = stored;
			if (item.number.num.has_value()) {
				item.number.num = std::nullopt;
			}
			changed = true;
		}
	}
	if (changed) {
		rebuild();
	}
	return true;
}

std::optional<int> State::sinkActiveListItem() {
	return applyCheckedMutation(std::optional<int>(), [](State &candidate) {
		const auto result = candidate.sinkActiveListItemUnchecked();
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::sinkActiveListItemUnchecked() {
	const auto surface = activeListItemSurface();
	if (!surface) {
		return std::nullopt;
	}
	const auto itemIndex = surface->itemIndex;
	if (itemIndex < 1) {
		return std::nullopt;
	}
	auto *owner = block(surface->path);
	if (!owner
		|| owner->kind != BlockKind::List
		|| itemIndex >= int(owner->listItems.size())) {
		return std::nullopt;
	}
	const auto listKind = owner->listKind;
	clearTemporaryDownParagraph();
	normalizeTextOnlyListItemForInsertion(
		ListItemChildrenContainer(surface->path, itemIndex - 1));
	owner = block(surface->path);
	if (!owner || itemIndex >= int(owner->listItems.size())) {
		return std::nullopt;
	}
	auto moved = std::move(owner->listItems[itemIndex]);
	DropOrderedItemNumber(&moved);
	owner->listItems.erase(owner->listItems.begin() + itemIndex);
	auto &previous = owner->listItems[itemIndex - 1];
	if (previous.blocks.empty()) {
		previous.blocks.push_back(MakeParagraphBlock());
	}
	auto nestedIndex = int(previous.blocks.size()) - 1;
	if (nestedIndex < 0
		|| previous.blocks[nestedIndex].kind != BlockKind::List) {
		auto nested = Block();
		nested.kind = BlockKind::List;
		nested.listKind = listKind;
		previous.blocks.push_back(std::move(nested));
		nestedIndex = int(previous.blocks.size()) - 1;
	} else {
		AdoptListItemMarkers(previous.blocks[nestedIndex], &moved);
	}
	auto &nested = previous.blocks[nestedIndex];
	nested.listItems.push_back(std::move(moved));
	const auto target = rebasedActiveListItemLeaf(
		BlockPath{
			.container = ListItemChildrenContainer(
				surface->path,
				itemIndex - 1),
			.index = nestedIndex,
		},
		int(nested.listItems.size()) - 1);
	if (!target) {
		return std::nullopt;
	}
	rebuild();
	return activateRebuiltLeaf(*target);
}

std::optional<int> State::liftActiveListItem() {
	return applyCheckedMutation(std::optional<int>(), [](State &candidate) {
		const auto result = candidate.liftActiveListItemUnchecked();
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::liftActiveListLineOrItem() {
	if (const auto line = splitActiveLineIntoListItem()) {
		return line;
	}
	return liftActiveListItem();
}

std::optional<int> State::splitActiveLineIntoListItem() {
	return applyCheckedMutation(std::optional<int>(), [](State &candidate) {
		const auto result = candidate.splitActiveLineIntoListItemUnchecked();
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::splitActiveLineIntoListItemUnchecked() {
	const auto descriptor = textNode(_activeTextOrdinal);
	const auto surface = activeListItemSurface();
	if (!descriptor
		|| !surface
		|| descriptor->leaf.kind != LeafKind::BlockText
		|| descriptor->leaf.block.index < 1) {
		return std::nullopt;
	}
	const auto index = descriptor->leaf.block.index;
	auto *blocks = blockContainer(descriptor->leaf.block.container);
	auto *owner = block(surface->path);
	const auto item = listItem(surface->path, surface->itemIndex);
	if (!blocks
		|| !owner
		|| !item
		|| owner->kind != BlockKind::List
		|| index >= int(blocks->size())) {
		return std::nullopt;
	}
	clearTemporaryDownParagraph();
	auto moved = ListItem();
	moved.blocks.assign(
		std::make_move_iterator(blocks->begin() + index),
		std::make_move_iterator(blocks->end()));
	blocks->erase(blocks->begin() + index, blocks->end());
	adoptLeadingParagraphListItemText(item);
	adoptLeadingParagraphListItemText(&moved);
	AdoptListItemMarkers(*owner, &moved);
	const auto movedIndex = surface->itemIndex + 1;
	const auto inlineText = moved.blocks.empty();
	owner->listItems.insert(
		owner->listItems.begin() + movedIndex,
		std::move(moved));
	const auto target = inlineText
		? LeafPath{
			.kind = LeafKind::ListItemText,
			.block = surface->path,
			.listItemIndex = movedIndex,
		}
		: LeafPath{
			.kind = LeafKind::BlockText,
			.block = {
				.container = ListItemChildrenContainer(
					surface->path,
					movedIndex),
				.index = 0,
			},
		};
	rebuild();
	return activateRebuiltLeaf(target);
}

std::optional<int> State::liftActiveListItemUnchecked() {
	const auto surface = activeListItemSurface();
	if (!surface) {
		return std::nullopt;
	}
	auto *owner = block(surface->path);
	const auto itemIndex = surface->itemIndex;
	if (!owner
		|| owner->kind != BlockKind::List
		|| itemIndex < 0
		|| itemIndex >= int(owner->listItems.size())) {
		return std::nullopt;
	}
	const auto &steps = surface->path.container.steps;
	if (steps.empty()
		|| steps.back().kind != BlockContainerKind::ListItemChildren) {
		// Only last item may leave its list by depth change.
		if (itemIndex + 1 != int(owner->listItems.size())) {
			return std::nullopt;
		}
		clearTemporaryDownParagraph();
		if (!unwrapListItemIntoParent(surface->path, itemIndex, true)) {
			return std::nullopt;
		}
		return (_activeTextOrdinal >= 0)
			? std::make_optional(_activeTextOrdinal)
			: std::nullopt;
	}
	auto parentPath = BlockPath{ .index = steps.back().blockIndex };
	parentPath.container.steps.assign(steps.begin(), steps.end() - 1);
	const auto parentItemIndex = steps.back().listItemIndex;
	const auto parentList = block(parentPath);
	if (!parentList
		|| parentList->kind != BlockKind::List
		|| parentItemIndex < 0
		|| parentItemIndex >= int(parentList->listItems.size())) {
		return std::nullopt;
	}
	clearTemporaryDownParagraph();
	auto sourceLeaf = LeafPath();
	if (const auto descriptor = textNode(_activeTextOrdinal)) {
		sourceLeaf = descriptor->leaf;
	}
	if (itemIndex + 1 < int(owner->listItems.size())
		&& (sourceLeaf.kind == LeafKind::ListItemText)) {
		normalizeTextOnlyListItemForInsertion(
			ListItemChildrenContainer(surface->path, itemIndex));
		owner = block(surface->path);
		if (!owner || itemIndex >= int(owner->listItems.size())) {
			return std::nullopt;
		}
		auto &item = owner->listItems[itemIndex];
		if (item.blocks.empty()) {
			item.blocks.push_back(MakeParagraphBlock());
		}
	}
	if (!owner->listItems[itemIndex].blocks.empty()
		&& (sourceLeaf.kind == LeafKind::ListItemText)) {
		sourceLeaf = LeafPath{ .kind = LeafKind::BlockText };
		sourceLeaf.block.index = 0;
	}
	auto moved = std::move(owner->listItems[itemIndex]);
	DropOrderedItemNumber(&moved);
	if (itemIndex + 1 < int(owner->listItems.size())) {
		auto rest = Block();
		rest.kind = BlockKind::List;
		rest.listKind = owner->listKind;
		rest.listItems = std::vector<ListItem>(
			std::make_move_iterator(
				owner->listItems.begin() + itemIndex + 1),
			std::make_move_iterator(owner->listItems.end()));
		owner->listItems.erase(
			owner->listItems.begin() + itemIndex + 1,
			owner->listItems.end());
		DropOrderedItemNumbers(rest.listItems);
		moved.blocks.push_back(std::move(rest));
	}
	owner->listItems.erase(owner->listItems.begin() + itemIndex);
	if (owner->listItems.empty()) {
		const auto blocks = blockContainer(surface->path.container);
		if (!blocks
			|| surface->path.index < 0
			|| surface->path.index >= int(blocks->size())) {
			return std::nullopt;
		}
		blocks->erase(blocks->begin() + surface->path.index);
	}
	AdoptListItemMarkers(*parentList, &moved);
	parentList->listItems.insert(
		parentList->listItems.begin() + parentItemIndex + 1,
		std::move(moved));
	const auto target = rebasedListItemLeaf(
		sourceLeaf,
		parentPath,
		parentItemIndex + 1);
	if (!target) {
		return std::nullopt;
	}
	rebuild();
	return activateRebuiltLeaf(*target);
}

std::optional<State::LeafPath> State::rebasedActiveListItemLeaf(
		const BlockPath &list,
		int itemIndex) const {
	const auto descriptor = textNode(_activeTextOrdinal);
	return descriptor
		? rebasedListItemLeaf(descriptor->leaf, list, itemIndex)
		: std::nullopt;
}

std::optional<State::LeafPath> State::rebasedListItemLeaf(
		const LeafPath &leaf,
		const BlockPath &list,
		int itemIndex) const {
	if (leaf.kind == LeafKind::ListItemText) {
		return LeafPath{
			.kind = LeafKind::ListItemText,
			.block = list,
			.listItemIndex = itemIndex,
		};
	} else if (leaf.kind == LeafKind::BlockText) {
		return LeafPath{
			.kind = LeafKind::BlockText,
			.block = {
				.container = ListItemChildrenContainer(list, itemIndex),
				.index = leaf.block.index,
			},
		};
	}
	return std::nullopt;
}

std::optional<int> State::handleActiveQuoteEnter(
		const ActiveEnterContext &context) {
	return applyCheckedMutation(std::optional<int>(), [=](State &candidate) {
		const auto result = candidate.handleActiveQuoteEnterUnchecked(
			context);
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::handleActiveQuoteEnterUnchecked(
		const ActiveEnterContext &context) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || descriptor->leaf.kind != LeafKind::BlockText) {
		return std::nullopt;
	}
	const auto owner = block(descriptor->leaf.block);
	if (!owner
		|| owner->kind != BlockKind::Quote
		|| owner->pullquote
		|| !owner->blocks.empty()) {
		return std::nullopt;
	}
	const auto container = BlockChildrenContainer(descriptor->leaf.block);
	if (!normalizeTextOnlyQuoteSurface(container, true)) {
		return std::nullopt;
	}
	return handleEnterAtBlockUnchecked(container, 0, context);
}

bool State::pasteClipboardListItemsAfterActive(
		const ClipboardListItemsData &data,
		std::optional<ActiveTextInsertContext> context) {
	if (data.items.empty()) {
		return false;
	}
	return applyCheckedMutation(false, [
			data,
			context = std::move(context)](State &candidate) mutable {
		auto block = Block();
		block.kind = BlockKind::List;
		block.listKind = data.listKind;
		block.orderedList = data.orderedList;
		block.listItems = data.items;
		auto blocks = std::vector<Block>();
		blocks.push_back(std::move(block));
		NormalizeInsertedOrderedListMetadata(&blocks);
		candidate.normalizeInsertedBlockAnchors(blocks);

		const auto sameList = [&] {
			const auto descriptor = candidate.textNode(
				candidate._activeTextOrdinal);
			const auto surface = candidate.activeListItemSurface();
			auto owner = surface ? candidate.block(surface->path) : nullptr;
			if (!descriptor
				|| !surface
				|| !owner
				|| !ListBlockMatchesClipboardData(*owner, data)) {
				return false;
			}
			auto insertContext = context
				? *context
				: ActiveTextInsertContext{
					.before = candidate.activeText(),
				};
			const auto itemContainer = ListItemChildrenContainer(
				surface->path,
				surface->itemIndex);
			auto activeBlockIndex = -1;
			switch (descriptor->leaf.kind) {
			case LeafKind::ListItemText:
				break;
			case LeafKind::BlockText:
				if (descriptor->leaf.block.container != itemContainer) {
					return false;
				}
				activeBlockIndex = descriptor->leaf.block.index;
				break;
			default:
				return false;
			}
			if (const auto normalized
				= candidate.normalizeTextOnlyListItemForInsertion(
					itemContainer)) {
				if (descriptor->leaf.kind == LeafKind::ListItemText) {
					activeBlockIndex = *normalized;
				} else if (*normalized >= 0) {
					++activeBlockIndex;
				}
			} else if (descriptor->leaf.kind == LeafKind::ListItemText) {
				activeBlockIndex = -1;
			}
			owner = candidate.block(surface->path);
			if (!owner
				|| owner->kind != BlockKind::List
				|| surface->itemIndex < 0
				|| surface->itemIndex >= int(owner->listItems.size())) {
				return false;
			}
			const auto item = &owner->listItems[surface->itemIndex];
			if (activeBlockIndex >= 0) {
				if (activeBlockIndex >= int(item->blocks.size())
					|| item->blocks[activeBlockIndex].kind
						!= BlockKind::Paragraph) {
					return false;
				}
			} else if (!item->blocks.empty()) {
				return false;
			}

			enum class OriginalParagraphSide {
				None,
				Leading,
				Trailing,
			};

			enum class OriginalItemSide {
				None,
				Leading,
				Trailing,
			};

			const auto makeParagraph = [](TextWithEntities text) {
				auto paragraph = MakeParagraphBlock();
				paragraph.text.text = std::move(text);
				return paragraph;
			};

			candidate.clearTemporaryDownParagraph();
			auto current = std::move(owner->listItems[surface->itemIndex]);
			auto insertedItems = std::move(blocks.front().listItems);
			const auto insertedCount = int(insertedItems.size());
			auto leading = ListItem();
			leading.taskState = current.taskState;
			auto trailing = ListItem();
			trailing.taskState = current.taskState;
			auto activeParagraph = Block();
			if (activeBlockIndex >= 0) {
				activeParagraph = std::move(current.blocks[activeBlockIndex]);
			}
			for (auto i = 0; i < std::max(activeBlockIndex, 0); ++i) {
				leading.blocks.push_back(std::move(current.blocks[i]));
			}
			auto originalParagraphSide = OriginalParagraphSide::None;
			if (activeBlockIndex >= 0 && !insertContext.before.text.isEmpty()) {
				activeParagraph.text.text = std::move(insertContext.before);
				leading.blocks.push_back(std::move(activeParagraph));
				originalParagraphSide = OriginalParagraphSide::Leading;
			}
			if (activeBlockIndex >= 0 && !insertContext.after.text.isEmpty()) {
				if (originalParagraphSide == OriginalParagraphSide::None) {
					activeParagraph.text.text = std::move(insertContext.after);
					trailing.blocks.push_back(std::move(activeParagraph));
					originalParagraphSide = OriginalParagraphSide::Trailing;
				} else {
					trailing.blocks.push_back(makeParagraph(
						std::move(insertContext.after)));
				}
			}
			if (activeBlockIndex >= 0) {
				for (auto i = activeBlockIndex + 1;
					i < int(current.blocks.size());
					++i) {
					trailing.blocks.push_back(std::move(current.blocks[i]));
				}
			}
			auto originalItemSide = OriginalItemSide::None;
			switch (originalParagraphSide) {
			case OriginalParagraphSide::Leading:
				originalItemSide = OriginalItemSide::Leading;
				break;
			case OriginalParagraphSide::Trailing:
				originalItemSide = OriginalItemSide::Trailing;
				break;
			case OriginalParagraphSide::None:
				break;
			}
			const auto keepLeading = !ListItemIsEmpty(leading);
			const auto keepTrailing = !ListItemIsEmpty(trailing);
			if (originalItemSide == OriginalItemSide::None) {
				if (keepLeading) {
					originalItemSide = OriginalItemSide::Leading;
				} else if (keepTrailing) {
					originalItemSide = OriginalItemSide::Trailing;
				}
			}
			if (originalItemSide == OriginalItemSide::Leading) {
				leading.number = std::move(current.number);
				leading.anchorId = std::move(current.anchorId);
				leading.text = std::move(current.text);
			} else if (originalItemSide == OriginalItemSide::Trailing) {
				trailing.number = std::move(current.number);
				trailing.anchorId = std::move(current.anchorId);
				trailing.text = std::move(current.text);
			}

			auto replacement = std::vector<ListItem>();
			replacement.reserve(
				insertedCount
				+ (keepLeading ? 1 : 0)
				+ (keepTrailing ? 1 : 0));
			if (keepLeading) {
				replacement.push_back(std::move(leading));
			}
			const auto insertedFrom = surface->itemIndex + int(keepLeading);
			replacement.insert(
				replacement.end(),
				std::make_move_iterator(insertedItems.begin()),
				std::make_move_iterator(insertedItems.end()));
			if (keepTrailing) {
				replacement.push_back(std::move(trailing));
			}
			owner->listItems.erase(owner->listItems.begin() + surface->itemIndex);
			owner->listItems.insert(
				owner->listItems.begin() + surface->itemIndex,
				std::make_move_iterator(replacement.begin()),
				std::make_move_iterator(replacement.end()));
			candidate.rebuild();
			for (auto i = 0, count = candidate.textNodeCount(); i != count; ++i) {
				const auto itemIndex = ListItemIndexForLeaf(
					candidate._textNodes[i].leaf,
					surface->path);
				if (itemIndex
					&& (*itemIndex >= insertedFrom)
					&& (*itemIndex < insertedFrom + insertedCount)
					&& candidate.setActiveTextByOrdinal(i)) {
					return true;
				}
			}
			candidate.ensureActiveTextOrdinal();
			return true;
		}();
		auto applied = sameList;
		if (!sameList) {
			applied = candidate.insertBlocksAfterActiveUnchecked(
				std::move(blocks),
				std::move(context));
			if (applied) {
				candidate.joinInsertedListWithSiblings(false);
			}
		}
		return CheckedMutationResult<bool>{
			.apply = applied,
			.result = applied,
		};
	});
}

} // namespace Iv::Editor
