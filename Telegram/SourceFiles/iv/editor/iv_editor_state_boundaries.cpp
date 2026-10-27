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

const TextNodeDescriptor *State::adjacentTextNode(
		int ordinal,
		bool forward) const {
	return textNode(ordinal + (forward ? 1 : -1));
}

bool State::canJoinActiveTextBlockBoundary(bool forward) const {
	const auto descriptor = textNode(_activeTextOrdinal);
	const auto adjacent = descriptor
		? adjacentTextNode(_activeTextOrdinal, forward)
		: nullptr;
	if (!descriptor
		|| !adjacent
		|| descriptor->leaf.kind != LeafKind::BlockText
		|| adjacent->leaf.kind != LeafKind::BlockText
		|| !(descriptor->leaf.block.container
			== adjacent->leaf.block.container)
		|| (adjacent->leaf.block.index
			!= descriptor->leaf.block.index + (forward ? 1 : -1))) {
		return false;
	}
	const auto activeOwner = block(descriptor->leaf.block);
	const auto adjacentOwner = block(adjacent->leaf.block);
	return activeOwner
		&& adjacentOwner
		&& JoinableTextBlockKind(activeOwner->kind)
		&& JoinableTextBlockKind(adjacentOwner->kind);
}

bool State::canJoinActiveListItemBoundary() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor
		|| (descriptor->leaf.kind == LeafKind::BlockText
			&& descriptor->leaf.block.index != 0)) {
		return false;
	}
	return activeListItemSurface().has_value();
}

bool State::joinActiveParagraphBoundaryUnchecked(
		bool forward,
		ActiveTextSelectionTarget *target) {
	if (!target || !canJoinActiveTextBlockBoundary(forward)) {
		return false;
	}
	const auto descriptor = textNode(_activeTextOrdinal);
	const auto adjacent = adjacentTextNode(_activeTextOrdinal, forward);
	const auto activeIndex = descriptor->leaf.block.index;
	const auto adjacentIndex = adjacent->leaf.block.index;
	auto *blocks = blockContainer(descriptor->leaf.block.container);
	if (!blocks
		|| activeIndex < 0
		|| adjacentIndex < 0
		|| activeIndex >= int(blocks->size())
		|| adjacentIndex >= int(blocks->size())) {
		return false;
	}
	auto &activeOwner = (*blocks)[activeIndex];
	auto &adjacentOwner = (*blocks)[adjacentIndex];
	clearTemporaryDownParagraph();
	auto destinationLeaf = forward ? descriptor->leaf : adjacent->leaf;
	const auto seamOffset = forward
		? AppendParagraphSeam(&activeOwner, std::move(adjacentOwner))
		: AppendParagraphSeam(&adjacentOwner, std::move(activeOwner));
	blocks->erase(blocks->begin() + (forward ? adjacentIndex : activeIndex));
	rebuild();
	if (!activateRebuiltLeaf(destinationLeaf)) {
		return false;
	}
	*target = {
		.leaf = destinationLeaf,
		.selectionFrom = seamOffset,
		.selectionTo = seamOffset,
	};
	return true;
}

bool State::removeBlankListItemBeforeBoundary(
		Block &owner,
		const BlockPath &list,
		int itemIndex,
		ActiveTextSelectionTarget *target) {
	const auto previousIndex = itemIndex - 1;
	const auto children = ListItemChildrenContainer(list, previousIndex);
	auto landing = std::optional<LeafPath>();
	for (const auto &node : _textNodes) {
		const auto &leaf = node.leaf;
		if (((leaf.kind == LeafKind::ListItemText)
			&& (leaf.block == list)
			&& (leaf.listItemIndex == previousIndex))
			|| ContainerHasPrefix(leaf.block.container, children)) {
			landing = leaf;
		}
	}
	if (!landing) {
		return false;
	}
	owner.listItems.erase(owner.listItems.begin() + itemIndex);
	rebuild();
	if (!activateRebuiltLeaf(*landing)) {
		return false;
	}
	const auto text = richText(*landing);
	const auto offset = text ? int(text->text.text.size()) : 0;
	*target = {
		.leaf = *landing,
		.selectionFrom = offset,
		.selectionTo = offset,
	};
	return true;
}

bool State::joinActiveListItemBoundaryUnchecked(
		ActiveTextSelectionTarget *target) {
	if (!target || !canJoinActiveListItemBoundary()) {
		return false;
	}
	const auto surface = activeListItemSurface();
	if (!surface) {
		return false;
	}
	const auto owner = block(surface->path);
	const auto item = listItem(surface->path, surface->itemIndex);
	if (!owner || owner->kind != BlockKind::List || !item) {
		return false;
	}
	const auto &steps = surface->path.container.steps;
	if (!steps.empty()
		&& steps.back().kind == BlockContainerKind::ListItemChildren) {
		if (!liftActiveListItemUnchecked()) {
			return false;
		}
		const auto lifted = textNode(_activeTextOrdinal);
		if (!lifted) {
			return false;
		}
		*target = {
			.leaf = lifted->leaf,
			.selectionFrom = 0,
			.selectionTo = 0,
		};
		return true;
	}
	clearTemporaryDownParagraph();
	if (surface->itemIndex > 0) {
		const auto previousIndex = surface->itemIndex - 1;
		const auto previous = listItem(surface->path, previousIndex);
		if (!previous) {
			return false;
		}
		if (ListItemIsBlankLine(*item)) {
			return removeBlankListItemBeforeBoundary(
				*owner,
				surface->path,
				surface->itemIndex,
				target);
		}
		auto merged = takeListItemBlocksForUnwrap(previous);
		previous->anchorId = QString();
		previous->text = RichText();
		auto taken = takeListItemBlocksForUnwrap(item);
		if (taken.empty()) {
			taken.push_back(MakeParagraphBlock());
		}
		auto seamOffset = 0;
		auto destinationIndex = int(merged.size());
		if (!merged.empty()
			&& merged.back().kind == BlockKind::Paragraph
			&& taken.front().kind == BlockKind::Paragraph) {
			seamOffset = AppendParagraphSeam(
				&merged.back(),
				std::move(taken.front()));
			taken.erase(taken.begin());
			destinationIndex = int(merged.size()) - 1;
		}
		merged.insert(
			merged.end(),
			std::make_move_iterator(taken.begin()),
			std::make_move_iterator(taken.end()));
		const auto count = int(merged.size()) - destinationIndex;
		previous->blocks = std::move(merged);
		owner->listItems.erase(
			owner->listItems.begin() + surface->itemIndex);
		rebuild();
		focusInsertedBlocks(
			ListItemChildrenContainer(surface->path, previousIndex),
			destinationIndex,
			count);
		const auto destination = textNode(_activeTextOrdinal);
		if (!destination) {
			return false;
		}
		*target = {
			.leaf = destination->leaf,
			.selectionFrom = seamOffset,
			.selectionTo = seamOffset,
		};
		return true;
	}
	if (!unwrapListItemIntoParent(surface->path, 0, true)) {
		return false;
	}
	const auto destination = textNode(_activeTextOrdinal);
	if (!destination) {
		return false;
	}
	*target = {
		.leaf = destination->leaf,
		.selectionFrom = 0,
		.selectionTo = 0,
	};
	return true;
}

auto State::nextListItemAfterActive(
		const ActiveListItemSurface &surface) const
-> std::optional<NextListItem> {
	const auto item = listItem(surface.path, surface.itemIndex);
	if (!item) {
		return std::nullopt;
	} else if (!item->blocks.empty()
		&& item->blocks.back().kind == BlockKind::List
		&& !item->blocks.back().listItems.empty()) {
		return NextListItem{
			.list = {
				.container = ListItemChildrenContainer(
					surface.path,
					surface.itemIndex),
				.index = int(item->blocks.size()) - 1,
			},
			.index = 0,
		};
	}
	auto path = surface.path;
	auto index = surface.itemIndex;
	while (true) {
		const auto owner = block(path);
		if (!owner || owner->kind != BlockKind::List) {
			return std::nullopt;
		} else if (index + 1 < int(owner->listItems.size())) {
			return NextListItem{ .list = path, .index = index + 1 };
		}
		const auto &steps = path.container.steps;
		if (steps.empty()
			|| steps.back().kind != BlockContainerKind::ListItemChildren) {
			return std::nullopt;
		}
		auto parent = BlockPath{ .index = steps.back().blockIndex };
		parent.container.steps.assign(steps.begin(), steps.end() - 1);
		const auto parentIndex = steps.back().listItemIndex;
		const auto parentItem = listItem(parent, parentIndex);
		if (!parentItem
			|| path.index + 1 != int(parentItem->blocks.size())) {
			return std::nullopt;
		}
		path = parent;
		index = parentIndex;
	}
}

bool State::canJoinActiveListItemForward() const {
	const auto surface = activeListItemSurface();
	if (!surface) {
		return false;
	}
	const auto descriptor = textNode(_activeTextOrdinal);
	const auto item = listItem(surface->path, surface->itemIndex);
	if (!descriptor || !item) {
		return false;
	} else if (descriptor->leaf.kind == LeafKind::BlockText) {
		const auto index = descriptor->leaf.block.index;
		if (index < 0 || index >= int(item->blocks.size())) {
			return false;
		}
		const auto trailing = int(item->blocks.size()) - 1 - index;
		const auto nested = (trailing == 1)
			&& (item->blocks.back().kind == BlockKind::List);
		if (trailing != 0 && !nested) {
			return false;
		}
	} else if (descriptor->leaf.kind != LeafKind::ListItemText) {
		return false;
	}
	return nextListItemAfterActive(*surface).has_value();
}

bool State::joinActiveListItemForwardUnchecked(
		ActiveTextSelectionTarget *target) {
	if (!target || !canJoinActiveListItemForward()) {
		return false;
	}
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return false;
	}
	auto destination = descriptor->leaf;
	const auto surface = normalizeActiveListItemSurface();
	if (!surface) {
		return false;
	}
	if (destination.kind == LeafKind::ListItemText) {
		destination = LeafPath{
			.kind = LeafKind::BlockText,
			.block = {
				.container = ListItemChildrenContainer(
					surface->path,
					surface->itemIndex),
				.index = 0,
			},
		};
	} else if (destination.kind != LeafKind::BlockText) {
		return false;
	}
	const auto index = destination.block.index;
	const auto next = nextListItemAfterActive(*surface);
	if (!next) {
		return false;
	}
	const auto nextList = block(next->list);
	if (!nextList
		|| nextList->kind != BlockKind::List
		|| next->index < 0
		|| next->index >= int(nextList->listItems.size())) {
		return false;
	}
	clearTemporaryDownParagraph();
	auto taken = takeListItemBlocksForUnwrap(
		&nextList->listItems[next->index]);
	nextList->listItems.erase(nextList->listItems.begin() + next->index);
	if (nextList->listItems.empty()) {
		const auto blocks = blockContainer(next->list.container);
		if (!blocks
			|| next->list.index < 0
			|| next->list.index >= int(blocks->size())) {
			return false;
		}
		blocks->erase(blocks->begin() + next->list.index);
	}
	const auto item = listItem(surface->path, surface->itemIndex);
	if (!item || index < 0 || index >= int(item->blocks.size())) {
		return false;
	}
	auto &paragraph = item->blocks[index];
	auto seamOffset = int(paragraph.text.text.text.size());
	if (!taken.empty() && taken.front().kind == BlockKind::Paragraph) {
		seamOffset = AppendParagraphSeam(
			&paragraph,
			std::move(taken.front()));
		taken.erase(taken.begin());
	}
	if (!taken.empty()) {
		item->blocks.insert(
			item->blocks.begin() + index + 1,
			std::make_move_iterator(taken.begin()),
			std::make_move_iterator(taken.end()));
	}
	rebuild();
	if (!activateRebuiltLeaf(destination)) {
		return false;
	}
	*target = {
		.leaf = destination,
		.selectionFrom = seamOffset,
		.selectionTo = seamOffset,
	};
	return true;
}

std::optional<int> State::escapeEmptyActiveBlockLine() {
	return applyCheckedMutation(std::optional<int>(), [](State &candidate) {
		const auto result = candidate.escapeEmptyActiveBlockLineUnchecked();
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::escapeEmptyActiveBlockLineUnchecked() {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || descriptor->leaf.kind != LeafKind::BlockText) {
		return std::nullopt;
	}
	const auto path = descriptor->leaf.block;
	const auto &steps = path.container.steps;
	if (steps.empty()
		|| steps.back().kind != BlockContainerKind::BlockChildren) {
		return std::nullopt;
	}
	const auto owner = block(path);
	if (!owner
		|| owner->kind != BlockKind::Paragraph
		|| !owner->blocks.empty()
		|| !owner->anchorId.isEmpty()
		|| !RichTextIsEmpty(owner->text)) {
		return std::nullopt;
	}
	const auto blocks = blockContainer(path.container);
	if (!blocks
		|| path.index < 0
		|| path.index + 1 != int(blocks->size())) {
		return std::nullopt;
	}
	auto parentPath = BlockPath{ .index = steps.back().blockIndex };
	parentPath.container.steps.assign(steps.begin(), steps.end() - 1);
	clearTemporaryDownParagraph();
	blocks->erase(blocks->begin() + path.index);
	auto removed = false;
	if (blocks->empty()) {
		const auto parent = blockContainer(parentPath.container);
		if (!parent
			|| parentPath.index < 0
			|| parentPath.index >= int(parent->size())) {
			return std::nullopt;
		}
		if (BlockIsEmpty((*parent)[parentPath.index])) {
			parent->erase(parent->begin() + parentPath.index);
			removed = true;
		}
	}
	const auto paragraph = reuseOrInsertParagraph(
		parentPath.container,
		parentPath.index + (removed ? 0 : 1));
	if (!paragraph) {
		return std::nullopt;
	}
	rebuild();
	return activateRebuiltLeaf(paragraph->leaf);
}

std::optional<int> State::resetActiveBlockToParagraph() {
	return applyCheckedMutation(std::optional<int>(), [](State &candidate) {
		const auto result = candidate.resetActiveBlockToParagraphUnchecked();
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::resetActiveBlockToParagraphUnchecked() {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || descriptor->leaf.kind != LeafKind::BlockText) {
		return std::nullopt;
	}
	const auto leaf = descriptor->leaf;
	const auto owner = block(leaf.block);
	if (!owner
		|| ((owner->kind != BlockKind::Heading)
			&& (owner->kind != BlockKind::Footer))) {
		return std::nullopt;
	}
	clearTemporaryDownParagraph();
	owner->kind = BlockKind::Paragraph;
	owner->headingLevel = 0;
	rebuild();
	return activateRebuiltLeaf(leaf);
}

std::optional<State::BlockPath> State::activeLineContainerBlock() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || descriptor->leaf.kind != LeafKind::BlockText) {
		return std::nullopt;
	}
	const auto &steps = descriptor->leaf.block.container.steps;
	if (steps.empty()
		|| steps.back().kind != BlockContainerKind::BlockChildren) {
		return std::nullopt;
	}
	auto result = BlockPath{ .index = steps.back().blockIndex };
	result.container.steps.assign(steps.begin(), steps.end() - 1);
	return result;
}

bool State::canLiftActiveLineOutOfContainer() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	const auto container = activeLineContainerBlock();
	if (!descriptor || !container) {
		return false;
	}
	const auto owner = block(descriptor->leaf.block);
	const auto parent = block(*container);
	return owner
		&& parent
		&& (parent->kind == BlockKind::Quote)
		&& JoinableTextBlockKind(owner->kind)
		&& (descriptor->leaf.block.index == 0);
}

bool State::liftActiveLineOutOfContainerUnchecked(
		ActiveTextSelectionTarget *target) {
	if (!target || !canLiftActiveLineOutOfContainer()) {
		return false;
	}
	const auto descriptor = textNode(_activeTextOrdinal);
	const auto containerPath = activeLineContainerBlock();
	if (!descriptor || !containerPath) {
		return false;
	}
	const auto path = descriptor->leaf.block;
	const auto blocks = blockContainer(path.container);
	const auto parent = blockContainer(containerPath->container);
	if (!blocks
		|| !parent
		|| blocks->empty()
		|| containerPath->index < 0
		|| containerPath->index >= int(parent->size())) {
		return false;
	}
	clearTemporaryDownParagraph();
	auto lifted = std::move((*blocks)[0]);
	blocks->erase(blocks->begin());
	if (blocks->empty()
		&& BlockIsEmpty((*parent)[containerPath->index])) {
		parent->erase(parent->begin() + containerPath->index);
	}
	parent->insert(parent->begin() + containerPath->index, std::move(lifted));
	const auto destination = LeafPath{
		.kind = LeafKind::BlockText,
		.block = {
			.container = containerPath->container,
			.index = containerPath->index,
		},
	};
	rebuild();
	if (!activateRebuiltLeaf(destination)) {
		return false;
	}
	*target = {
		.leaf = destination,
		.selectionFrom = 0,
		.selectionTo = 0,
	};
	return true;
}

bool State::canJoinBlockAfterActiveContainer() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	const auto containerPath = activeLineContainerBlock();
	if (!descriptor || !containerPath) {
		return false;
	}
	const auto owner = block(descriptor->leaf.block);
	const auto blocks = blockContainer(descriptor->leaf.block.container);
	if (!owner
		|| !blocks
		|| (owner->kind != BlockKind::Paragraph)
		|| (descriptor->leaf.block.index + 1 != int(blocks->size()))) {
		return false;
	}
	auto nextPath = *containerPath;
	++nextPath.index;
	const auto next = block(nextPath);
	return next
		&& JoinableTextBlockKind(next->kind)
		&& next->blocks.empty();
}

bool State::joinBlockAfterActiveContainerUnchecked(
		ActiveTextSelectionTarget *target) {
	if (!target || !canJoinBlockAfterActiveContainer()) {
		return false;
	}
	const auto descriptor = textNode(_activeTextOrdinal);
	const auto containerPath = activeLineContainerBlock();
	if (!descriptor || !containerPath) {
		return false;
	}
	const auto destination = descriptor->leaf;
	const auto parent = blockContainer(containerPath->container);
	if (!parent || containerPath->index + 1 >= int(parent->size())) {
		return false;
	}
	clearTemporaryDownParagraph();
	auto taken = std::move((*parent)[containerPath->index + 1]);
	parent->erase(parent->begin() + containerPath->index + 1);
	const auto owner = block(destination.block);
	if (!owner) {
		return false;
	}
	const auto seamOffset = AppendParagraphSeam(owner, std::move(taken));
	rebuild();
	if (!activateRebuiltLeaf(destination)) {
		return false;
	}
	*target = {
		.leaf = destination,
		.selectionFrom = seamOffset,
		.selectionTo = seamOffset,
	};
	return true;
}

bool State::canRemoveEmptyBlockBeforeActive() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor
		|| descriptor->leaf.kind != LeafKind::BlockText
		|| descriptor->leaf.block.index < 1) {
		return false;
	}
	auto path = descriptor->leaf.block;
	--path.index;
	const auto previous = block(path);
	return previous
		&& JoinableTextBlockKind(previous->kind)
		&& previous->blocks.empty()
		&& previous->anchorId.isEmpty()
		&& RichTextIsEmpty(previous->text);
}

bool State::removeEmptyBlockBeforeActiveUnchecked(
		ActiveTextSelectionTarget *target) {
	if (!target || !canRemoveEmptyBlockBeforeActive()) {
		return false;
	}
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return false;
	}
	auto destination = descriptor->leaf;
	const auto blocks = blockContainer(destination.block.container);
	if (!blocks || destination.block.index >= int(blocks->size())) {
		return false;
	}
	clearTemporaryDownParagraph();
	blocks->erase(blocks->begin() + destination.block.index - 1);
	--destination.block.index;
	rebuild();
	if (!activateRebuiltLeaf(destination)) {
		return false;
	}
	*target = {
		.leaf = destination,
		.selectionFrom = 0,
		.selectionTo = 0,
	};
	return true;
}

std::optional<State::BlockPath> State::listBeforeActiveParagraph() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || descriptor->leaf.kind != LeafKind::BlockText) {
		return std::nullopt;
	}
	const auto &path = descriptor->leaf.block;
	const auto owner = block(path);
	if (!owner || owner->kind != BlockKind::Paragraph || path.index < 1) {
		return std::nullopt;
	}
	auto result = path;
	--result.index;
	const auto previous = block(result);
	return (previous
		&& previous->kind == BlockKind::List
		&& !previous->listItems.empty())
		? std::make_optional(result)
		: std::nullopt;
}

auto State::deepestLastItem(BlockPath list) const
-> std::optional<NextListItem> {
	while (true) {
		const auto owner = block(list);
		if (!owner
			|| owner->kind != BlockKind::List
			|| owner->listItems.empty()) {
			return std::nullopt;
		}
		const auto index = int(owner->listItems.size()) - 1;
		const auto &item = owner->listItems[index];
		if (!item.blocks.empty()
			&& item.blocks.back().kind == BlockKind::List
			&& !item.blocks.back().listItems.empty()) {
			list = BlockPath{
				.container = ListItemChildrenContainer(list, index),
				.index = int(item.blocks.size()) - 1,
			};
			continue;
		}
		return NextListItem{ .list = list, .index = index };
	}
}

std::optional<int> State::appendActiveParagraphToPreviousList() {
	return applyCheckedMutation(std::optional<int>(), [](State &candidate) {
		const auto result
			= candidate.appendActiveParagraphToPreviousListUnchecked();
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::appendActiveParagraphToPreviousListUnchecked() {
	const auto listPath = listBeforeActiveParagraph();
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!listPath || !descriptor) {
		return std::nullopt;
	}
	const auto paragraphPath = descriptor->leaf.block;
	const auto blocks = blockContainer(paragraphPath.container);
	if (!blocks
		|| paragraphPath.index < 0
		|| paragraphPath.index >= int(blocks->size())) {
		return std::nullopt;
	}
	clearTemporaryDownParagraph();
	auto paragraph = std::move((*blocks)[paragraphPath.index]);
	blocks->erase(blocks->begin() + paragraphPath.index);
	const auto list = block(*listPath);
	if (!list || list->kind != BlockKind::List) {
		return std::nullopt;
	}
	auto item = ListItem();
	item.blocks.push_back(std::move(paragraph));
	adoptLeadingParagraphListItemText(&item);
	AdoptListItemMarkers(*list, &item);
	const auto itemIndex = int(list->listItems.size());
	const auto inlineText = item.blocks.empty();
	list->listItems.push_back(std::move(item));
	const auto target = inlineText
		? LeafPath{
			.kind = LeafKind::ListItemText,
			.block = *listPath,
			.listItemIndex = itemIndex,
		}
		: LeafPath{
			.kind = LeafKind::BlockText,
			.block = {
				.container = ListItemChildrenContainer(*listPath, itemIndex),
				.index = 0,
			},
		};
	rebuild();
	return activateRebuiltLeaf(target);
}

void State::mergeListWithNextSibling(const BlockPath &list) {
	const auto blocks = blockContainer(list.container);
	if (!blocks
		|| list.index < 0
		|| list.index + 1 >= int(blocks->size())) {
		return;
	}
	auto &first = (*blocks)[list.index];
	auto &second = (*blocks)[list.index + 1];
	if (!ListsJoinable(first, second)) {
		return;
	}
	DropOrderedItemNumbers(second.listItems);
	first.listItems.insert(
		first.listItems.end(),
		std::make_move_iterator(second.listItems.begin()),
		std::make_move_iterator(second.listItems.end()));
	blocks->erase(blocks->begin() + list.index + 1);
}

auto State::joinListWithSiblings(const BlockPath &list, bool startExplicit)
-> std::optional<ListJoin> {
	const auto blocks = blockContainer(list.container);
	if (!blocks
		|| list.index < 0
		|| list.index >= int(blocks->size())
		|| (*blocks)[list.index].kind != BlockKind::List) {
		return std::nullopt;
	}
	const auto joinNext = (list.index + 1 < int(blocks->size()))
		&& ListsJoinSeamlessly(
			(*blocks)[list.index],
			(*blocks)[list.index + 1],
			false);
	const auto joinPrevious = (list.index > 0)
		&& ListsJoinSeamlessly(
			(*blocks)[list.index - 1],
			(*blocks)[list.index],
			startExplicit);
	if (!joinNext && !joinPrevious) {
		return std::nullopt;
	}
	const auto join = [&](int index) {
		auto &first = (*blocks)[index];
		auto &second = (*blocks)[index + 1];
		first.listItems.insert(
			first.listItems.end(),
			std::make_move_iterator(second.listItems.begin()),
			std::make_move_iterator(second.listItems.end()));
		blocks->erase(blocks->begin() + index + 1);
	};
	auto result = ListJoin{
		.list = list,
		.itemsCount = int((*blocks)[list.index].listItems.size()),
	};
	if (joinNext) {
		join(list.index);
	}
	if (joinPrevious) {
		result.itemsFrom = int((*blocks)[list.index - 1].listItems.size());
		--result.list.index;
		join(list.index - 1);
	}
	return result;
}

bool State::blockActionExpandsToActiveLine(InsertBlockType type) const {
	if (!BlockConversionExpandsToActiveLine(type)) {
		return false;
	} else if (!IsListInsertType(type)) {
		return true;
	}
	const auto leaf = activeLeafPath();
	return leaf && (leaf->kind != LeafKind::ListItemText);
}

void State::joinInsertedListWithSiblings(bool startExplicit) {
	const auto leaf = activeLeafPath();
	if (!leaf || leaf->kind != LeafKind::ListItemText) {
		return;
	}
	const auto joined = joinListWithSiblings(leaf->block, startExplicit);
	if (!joined) {
		return;
	}
	const auto target = LeafPath{
		.kind = LeafKind::ListItemText,
		.block = joined->list,
		.listItemIndex = joined->itemsFrom + leaf->listItemIndex,
	};
	rebuild();
	activateRebuiltLeaf(target);
}

bool State::canJoinActiveParagraphIntoPreviousList() const {
	const auto listPath = listBeforeActiveParagraph();
	return listPath && deepestLastItem(*listPath).has_value();
}

bool State::joinActiveParagraphIntoPreviousListUnchecked(
		ActiveTextSelectionTarget *target) {
	if (!target || !canJoinActiveParagraphIntoPreviousList()) {
		return false;
	}
	const auto listPath = listBeforeActiveParagraph();
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!listPath || !descriptor) {
		return false;
	}
	const auto deepest = deepestLastItem(*listPath);
	const auto paragraphPath = descriptor->leaf.block;
	if (!deepest) {
		return false;
	}
	clearTemporaryDownParagraph();
	const auto blocks = blockContainer(paragraphPath.container);
	if (!blocks
		|| paragraphPath.index < 0
		|| paragraphPath.index >= int(blocks->size())) {
		return false;
	}
	auto paragraph = std::move((*blocks)[paragraphPath.index]);
	blocks->erase(blocks->begin() + paragraphPath.index);
	const auto item = listItem(deepest->list, deepest->index);
	if (!item) {
		return false;
	}
	auto destination = LeafPath();
	auto seamOffset = 0;
	if (item->blocks.empty()) {
		seamOffset = AppendRichTextSeam(&item->text, std::move(paragraph));
		destination = LeafPath{
			.kind = LeafKind::ListItemText,
			.block = deepest->list,
			.listItemIndex = deepest->index,
		};
	} else {
		const auto last = int(item->blocks.size()) - 1;
		if (item->blocks[last].kind == BlockKind::Paragraph) {
			seamOffset = AppendParagraphSeam(
				&item->blocks[last],
				std::move(paragraph));
			destination = LeafPath{
				.kind = LeafKind::BlockText,
				.block = {
					.container = ListItemChildrenContainer(
						deepest->list,
						deepest->index),
					.index = last,
				},
			};
		} else {
			item->blocks.push_back(std::move(paragraph));
			destination = LeafPath{
				.kind = LeafKind::BlockText,
				.block = {
					.container = ListItemChildrenContainer(
						deepest->list,
						deepest->index),
					.index = last + 1,
				},
			};
		}
	}
	mergeListWithNextSibling(*listPath);
	rebuild();
	if (!activateRebuiltLeaf(destination)) {
		return false;
	}
	*target = {
		.leaf = destination,
		.selectionFrom = seamOffset,
		.selectionTo = seamOffset,
	};
	return true;
}

State::ParagraphBoundaryJoinResult State::joinActiveParagraphBoundary(
		bool forward) {
	auto failure = ParagraphBoundaryJoinResult{
		.result = ApplyResult::Failed,
	};
	return applyCheckedMutation(failure, [forward](State &candidate) {
		const auto dropEmpty = !forward
			&& candidate.canRemoveEmptyBlockBeforeActive();
		const auto textJoin = !dropEmpty
			&& candidate.canJoinActiveTextBlockBoundary(forward);
		const auto listJoin = !dropEmpty
			&& !textJoin
			&& (forward
				? candidate.canJoinActiveListItemForward()
				: candidate.canJoinActiveListItemBoundary());
		const auto listAppend = !dropEmpty
			&& !textJoin
			&& !listJoin
			&& !forward
			&& candidate.canJoinActiveParagraphIntoPreviousList();
		const auto containerStep = !dropEmpty
			&& !textJoin
			&& !listJoin
			&& !listAppend
			&& (forward
				? candidate.canJoinBlockAfterActiveContainer()
				: candidate.canLiftActiveLineOutOfContainer());
		if (!dropEmpty
			&& !textJoin
			&& !listJoin
			&& !listAppend
			&& !containerStep) {
			return CheckedMutationResult<ParagraphBoundaryJoinResult>{
				.result = { .result = ApplyResult::Unchanged },
			};
		}
		auto target = ActiveTextSelectionTarget();
		const auto joined = dropEmpty
			? candidate.removeEmptyBlockBeforeActiveUnchecked(&target)
			: containerStep
			? (forward
				? candidate.joinBlockAfterActiveContainerUnchecked(&target)
				: candidate.liftActiveLineOutOfContainerUnchecked(&target))
			: textJoin
			? candidate.joinActiveParagraphBoundaryUnchecked(
				forward,
				&target)
			: listAppend
			? candidate.joinActiveParagraphIntoPreviousListUnchecked(&target)
			: forward
			? candidate.joinActiveListItemForwardUnchecked(&target)
			: candidate.joinActiveListItemBoundaryUnchecked(&target);
		if (!joined) {
			return CheckedMutationResult<ParagraphBoundaryJoinResult>{
				.result = { .result = ApplyResult::Failed },
			};
		}
		return CheckedMutationResult<ParagraphBoundaryJoinResult>{
			.apply = true,
			.result = {
				.result = ApplyResult::Changed,
				.destinationLeaf = target.leaf,
				.selectionFrom = target.selectionFrom,
				.selectionTo = target.selectionTo,
			},
		};
	});
}

std::optional<int> State::handleActiveHeadingEnter(
		const ActiveEnterContext &context) {
	return applyCheckedMutation(std::optional<int>(), [=](State &candidate) {
		const auto result = candidate.handleActiveHeadingEnterUnchecked(
			context);
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::handleActiveHeadingEnterUnchecked(
		const ActiveEnterContext &context) {
	return handleActiveBlockEnterUnchecked(BlockKind::Heading, context);
}

std::optional<int> State::handleActiveFooterEnter(
		const ActiveEnterContext &context) {
	return applyCheckedMutation(std::optional<int>(), [=](State &candidate) {
		const auto result = candidate.handleActiveFooterEnterUnchecked(
			context);
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::handleActiveFooterEnterUnchecked(
		const ActiveEnterContext &context) {
	return handleActiveBlockEnterUnchecked(BlockKind::Footer, context);
}

std::optional<int> State::handleActiveParagraphEnter(
		const ActiveEnterContext &context) {
	return applyCheckedMutation(std::optional<int>(), [=](State &candidate) {
		const auto result = candidate.handleActiveParagraphEnterUnchecked(
			context);
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::handleActiveParagraphEnterUnchecked(
		const ActiveEnterContext &context) {
	return handleActiveBlockEnterUnchecked(BlockKind::Paragraph, context);
}

std::optional<int> State::handleActiveBlockEnterUnchecked(
		BlockKind kind,
		const ActiveEnterContext &context) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || descriptor->leaf.kind != LeafKind::BlockText) {
		return std::nullopt;
	}
	const auto path = descriptor->leaf.block;
	const auto blocks = blockContainer(path.container);
	if (!blocks
		|| path.index < 0
		|| path.index >= int(blocks->size())
		|| (*blocks)[path.index].kind != kind) {
		return std::nullopt;
	}
	return handleEnterAtBlockUnchecked(path.container, path.index, context);
}

std::optional<int> State::handleEnterAtBlockUnchecked(
		const BlockContainerPath &container,
		int index,
		const ActiveEnterContext &context) {
	const auto blocks = blockContainer(container);
	if (!blocks || index < 0 || index >= int(blocks->size())) {
		return std::nullopt;
	}
	if (context.position == EnterPosition::Beginning) {
		clearTemporaryDownParagraph();
		blocks->insert(blocks->begin() + index, MakeParagraphBlock());
		const auto target = LeafPath{
			.kind = LeafKind::BlockText,
			.block = {
				.container = container,
				.index = index + 1,
			},
		};
		rebuild();
		return activateRebuiltLeaf(target);
	}
	const auto insertAt = index + 1;
	if (insertAt < 0 || insertAt > int(blocks->size())) {
		return std::nullopt;
	}
	auto &owner = (*blocks)[index];
	const auto split = (context.position == EnterPosition::Middle)
		&& (context.head.text.size() + context.tail.text.size()
			== owner.text.text.text.size());
	clearTemporaryDownParagraph();
	auto paragraph = MakeParagraphBlock();
	if (split) {
		owner.text.text = context.head;
		paragraph.text.text = context.tail;
	}
	blocks->insert(blocks->begin() + insertAt, std::move(paragraph));
	const auto target = LeafPath{
		.kind = LeafKind::BlockText,
		.block = {
			.container = container,
			.index = insertAt,
		},
	};
	rebuild();
	return activateRebuiltLeaf(target);
}

std::optional<int> State::handleActiveListEnter(
		const ActiveEnterContext &context) {
	return applyCheckedMutation(std::optional<int>(), [=](State &candidate) {
		const auto result = candidate.handleActiveListEnterUnchecked(
			context);
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::handleActiveListEnterUnchecked(
		const ActiveEnterContext &context) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	const auto blocksForm = (descriptor->leaf.kind == LeafKind::BlockText);
	const auto paragraphIndex = blocksForm
		? descriptor->leaf.block.index
		: 0;
	const auto surface = normalizeActiveListItemSurface();
	if (!surface) {
		return std::nullopt;
	}
	const auto owner = block(surface->path);
	const auto item = listItem(surface->path, surface->itemIndex);
	if (!owner || owner->kind != BlockKind::List || !item) {
		return std::nullopt;
	}
	if (paragraphIndex < 0 || paragraphIndex >= int(item->blocks.size())) {
		return std::nullopt;
	}
	const auto itemStart = (context.position == EnterPosition::Beginning)
		&& (paragraphIndex == 0);
	const auto itemEnd = (context.position == EnterPosition::End)
		&& (paragraphIndex + 1 == int(item->blocks.size()));
	if (!itemStart && !itemEnd) {
		auto &blocks = item->blocks;
		const auto moveFrom = (context.position == EnterPosition::Beginning)
			? paragraphIndex
			: (paragraphIndex + 1);
		const auto split = (context.position == EnterPosition::Middle)
			&& (context.head.text.size() + context.tail.text.size()
				== blocks[paragraphIndex].text.text.text.size());
		clearTemporaryDownParagraph();
		auto next = ListItem();
		next.taskState = SplitTaskState(item->taskState);
		if (split) {
			blocks[paragraphIndex].text.text = context.head;
			auto paragraph = MakeParagraphBlock();
			paragraph.text.text = context.tail;
			next.blocks.push_back(std::move(paragraph));
		}
		next.blocks.insert(
			next.blocks.end(),
			std::make_move_iterator(blocks.begin() + moveFrom),
			std::make_move_iterator(blocks.end()));
		blocks.erase(blocks.begin() + moveFrom, blocks.end());
		if (next.blocks.empty()
			|| next.blocks.front().kind != BlockKind::Paragraph) {
			// Item paints its first block on marker line.
			next.blocks.insert(next.blocks.begin(), MakeParagraphBlock());
		}
		const auto insertedCount = int(next.blocks.size());
		owner->listItems.insert(
			owner->listItems.begin() + surface->itemIndex + 1,
			std::move(next));
		rebuild();
		focusInsertedBlocks(
			ListItemChildrenContainer(surface->path, surface->itemIndex + 1),
			0,
			insertedCount);
		return (_activeTextOrdinal >= 0)
			? std::make_optional(_activeTextOrdinal)
			: std::nullopt;
	}
	auto target = std::optional<LeafPath>();
	if (itemStart) {
		// Enter may not touch items the caret does not stand in.
		clearTemporaryDownParagraph();
		owner->listItems.insert(
			owner->listItems.begin() + surface->itemIndex,
			MakeParagraphListItem(SplitTaskState(item->taskState)));
		target = LeafPath{
			.kind = LeafKind::BlockText,
			.block = {
				.container = ListItemChildrenContainer(
					surface->path,
					surface->itemIndex + 1),
				.index = 0,
			},
		};
	} else {
		const auto itemEmpty = (item->blocks.size() == 1)
			&& (item->blocks.front().kind == BlockKind::Paragraph)
			&& ListItemIsEmpty(*item);
		const auto trailingEmpty = itemEmpty
			&& (surface->itemIndex + 1 == int(owner->listItems.size()));
		const auto &steps = surface->path.container.steps;
		const auto nested = !steps.empty()
			&& (steps.back().kind == BlockContainerKind::ListItemChildren);
		if (itemEmpty && nested) {
			return liftActiveListItemUnchecked();
		} else if (itemEmpty && !trailingEmpty) {
			clearTemporaryDownParagraph();
			if (!unwrapListItemIntoParent(
					surface->path,
					surface->itemIndex,
					true)) {
				return std::nullopt;
			}
			return (_activeTextOrdinal >= 0)
				? std::make_optional(_activeTextOrdinal)
				: std::nullopt;
		} else if (trailingEmpty) {
			clearTemporaryDownParagraph();
			owner->listItems.erase(
				owner->listItems.begin() + surface->itemIndex);
			if (owner->listItems.empty()) {
				const auto blocks = blockContainer(surface->path.container);
				if (!blocks
					|| surface->path.index < 0
						|| surface->path.index >= int(blocks->size())) {
					return std::nullopt;
				}
				clearTemporaryDownParagraph();
				blocks->erase(blocks->begin() + surface->path.index);
				if (const auto paragraph = reuseOrInsertParagraph(
						surface->path.container,
						surface->path.index)) {
					target = paragraph->leaf;
				}
			} else {
				if (const auto paragraph = reuseOrInsertParagraph(
						surface->path.container,
						surface->path.index + 1)) {
					target = paragraph->leaf;
				}
			}
		} else {
			clearTemporaryDownParagraph();
			owner->listItems.insert(
				owner->listItems.begin() + surface->itemIndex + 1,
				MakeParagraphListItem(SplitTaskState(item->taskState)));
			target = LeafPath{
				.kind = LeafKind::BlockText,
				.block = {
					.container = ListItemChildrenContainer(
						surface->path,
						surface->itemIndex + 1),
					.index = 0,
				},
			};
		}
	}
	if (!target) {
		return std::nullopt;
	}
	rebuild();
	return activateRebuiltLeaf(*target);
}

} // namespace Iv::Editor
