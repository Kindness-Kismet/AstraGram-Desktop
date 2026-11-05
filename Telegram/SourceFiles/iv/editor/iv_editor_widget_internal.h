/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/editor/iv_editor_widget.h"
#include "iv/editor/iv_editor_page_table_grid.h"

namespace Iv::Editor::WidgetDetails {

[[nodiscard]] bool IsFieldLineBreak(QChar ch);

[[nodiscard]] bool MatchesKeySequence(
		QKeyEvent *e,
		const QKeySequence &sequence);

constexpr auto kRetainedLeafFieldLimit = 50;

constexpr auto kTooltipDelay = 1000;

extern thread_local Widget *PreservingExternalFieldRestore;

using ToolbarFormatAction = Widget::ToolbarFormatAction;
using ToolbarLinkMode = Widget::ToolbarLinkMode;
using TextFormattingAction = State::TextFormattingAction;
using TextNodeSpan = State::TextNodeSpan;
using StateBlockContainerKind = State::BlockContainerKind;
using StateBlockContainerPath = State::BlockContainerPath;
using StateBlockPath = State::BlockPath;
using StateLeafKind = State::LeafKind;
using StateLeafPath = State::LeafPath;
using PreparedListItemRange = Markdown::PreparedEditListItemRange;
using PreparedOrderedListType = Markdown::PreparedOrderedListType;
using PreparedSelection = Markdown::PreparedEditSelection;
using PreparedSelectionKind = Markdown::PreparedEditSelectionKind;

[[nodiscard]] const std::vector<RichPage::Block> *BlockContainer(
	const RichPage &page,
	const StateBlockContainerPath &path);

[[nodiscard]] const RichPage::Block *BlockFromPath(
	const RichPage &page,
	const StateBlockPath &path);

[[nodiscard]] const RichPage::RichText *RichTextFromPath(
	const RichPage &page,
	const StateLeafPath &path);

struct CommittedFieldSelectionCapture {
	StateLeafPath leaf;
	TextWithEntities text;
	int anchorOffset = 0;
	int cursorOffset = 0;
};

struct CommittedFieldSelectionRestore {
	int ordinal = -1;
	int anchorOffset = 0;
	int cursorOffset = 0;
};

void RemoveBlockLevelEntities(TextWithEntities *text);

struct SplitCommittedFieldOffset {
	int chunkIndex = -1;
	int localOffset = 0;
};

[[nodiscard]] SplitCommittedFieldOffset SplitCommittedFieldOffsetAt(
		const std::vector<TextWithEntities> &chunks,
		int offset);

[[nodiscard]] std::optional<StateLeafPath> SplitCommittedFieldLeafAt(
		const RichPage &page,
		const CommittedFieldSelectionCapture &capture,
		int chunkIndex);

[[nodiscard]] auto CaptureCommittedFieldSelection(
		const Ui::InputField &field,
		const State &state,
		const Widget &widget,
		int (Widget::*richOffset)(const TextWithEntities &, int) const)
-> std::optional<CommittedFieldSelectionCapture>;

[[nodiscard]] std::optional<CommittedFieldSelectionRestore>
MapCommittedFieldSelectionAfterCommit(
		const State &state,
		const CommittedFieldSelectionCapture &capture);

[[nodiscard]] const QString *ToolbarActionTag(ToolbarFormatAction action);

[[nodiscard]] std::optional<TextFormattingAction> BroaderFormattingAction(
		ToolbarFormatAction action);

[[nodiscard]] bool TableGridCellMatchesLeaf(
		const TableGridCellReference &cell,
		const StateLeafPath &leaf,
		const StateBlockPath &block);

[[nodiscard]] bool LeafSelectedStructurally(
		const RichPage &page,
		const StateLeafPath &leaf,
		const PreparedSelection &selection);

[[nodiscard]] bool BlockSelectedStructurally(
		const StateBlockPath &path,
		const PreparedSelection &selection);

[[nodiscard]] bool IsSimpleMediaBlockKind(RichPage::BlockKind kind);

[[nodiscard]] uint64 MediaIdForBlock(const RichPage::Block &block);

[[nodiscard]] uint64 MediaIdForGroupedItem(
		const RichPage::GroupedMediaItem &item);

[[nodiscard]] bool GroupedMediaHasPhotoVideoItems(
		const RichPage::Block &block);

[[nodiscard]] bool GroupedPhotoVideoItemsHaveSpoiler(
		const RichPage::Block &block);

template <typename Callback>
void EnumerateBlockPaths(
		const RichPage &page,
		const StateBlockContainerPath &container,
		Callback &&callback) {
	const auto *blocks = BlockContainer(page, container);
	if (!blocks) {
		return;
	}
	for (auto index = 0, count = int(blocks->size()); index != count; ++index) {
		const auto path = StateBlockPath{
			.container = container,
			.index = index,
		};
		const auto &block = (*blocks)[index];
		callback(path, block);
		EnumerateBlockPaths(page, BlockChildrenContainer(path), callback);
		for (auto itemIndex = 0, itemCount = int(block.listItems.size());
			itemIndex != itemCount;
			++itemIndex) {
			EnumerateBlockPaths(
				page,
				ListItemChildrenContainer(path, itemIndex),
				callback);
		}
	}
}

[[nodiscard]] bool RedirectTextToField(const QString &text);

struct InlineFieldTrimResult {
	TextWithTags text;
	int left = 0;
};

[[nodiscard]] TextWithTags RestoreInlineFieldEdges(
		TextWithTags text,
		const QString &left,
		const QString &right);

[[nodiscard]] InlineFieldTrimResult TrimInlineFieldText(
		TextWithTags text,
		bool trimLeft);

[[nodiscard]] QString TagWithoutCustomEmojiCounters(QStringView id);

[[nodiscard]] TextWithTags::Tags TagsWithoutCustomEmojiCounters(
		const TextWithTags::Tags &tags);

[[nodiscard]] bool InlineFieldTextsEqual(
		const TextWithTags &a,
		const TextWithTags &b);

[[nodiscard]] int MapEditorOffsetToRichOffset(
		const std::vector<RichTextEditorOffsetReplacement> &replacements,
		int offset);

[[nodiscard]] bool HasRealEnterContent(const QString &text);

struct InputRule {
	State::InsertAction action;
};

[[nodiscard]] std::optional<InputRule> MatchInputRule(QString typed);

[[nodiscard]] State::ActiveEnterContext MakeActiveEnterContext(
		std::optional<State::ActiveTextInsertContext> context);

[[nodiscard]] auto ClipboardPasteInsertContext(
		std::optional<State::ActiveTextInsertContext> context)
-> std::optional<State::ActiveTextInsertContext>;

[[nodiscard]] QString ValidateInstantViewEditorLink(QString link);

[[nodiscard]] bool ImeEventProducesInput(
		const QInputMethodEvent &e,
		const QTextCursor &cursor);

using PreparedEditBlockContainerPath
	= Markdown::PreparedEditBlockContainerPath;
using PreparedEditBlockContainerStep
	= Markdown::PreparedEditBlockContainerStep;
using PreparedEditBlockContainerKind
	= Markdown::PreparedEditBlockContainerKind;
using PreparedEditBlockPath = Markdown::PreparedEditBlockPath;
using PreparedEditBlockSource = Markdown::PreparedEditBlockSource;
using PreparedEditHit = Markdown::PreparedEditHit;
using PreparedEditHitKind = Markdown::PreparedEditHitKind;
using PreparedEditLeafKind = Markdown::PreparedEditLeafKind;
using PreparedEditDropTarget = Markdown::PreparedEditDropTarget;
using PreparedEditBlockDropTarget = Markdown::PreparedEditBlockDropTarget;
using PreparedEditLeafSource = Markdown::PreparedEditLeafSource;
using PreparedEditListItemDropTarget = Markdown::PreparedEditListItemDropTarget;
using PreparedEditListItemSource = Markdown::PreparedEditListItemSource;
using PreparedEditSelection = Markdown::PreparedEditSelection;
using PreparedEditSelectionKind = Markdown::PreparedEditSelectionKind;
using PreparedEditTableCellRange = Markdown::PreparedEditTableCellRange;
using PreparedEditTableCellSource = Markdown::PreparedEditTableCellSource;
using PreparedEditTableRowSource = Markdown::PreparedEditTableRowSource;
using PreparedEditTextDropTarget = Markdown::PreparedEditTextDropTarget;
using ApplyResult = State::ApplyResult;
using PreparedMutationKind = State::PreparedMutationKind;

[[nodiscard]] bool SnapshotEquals(
		const State::Snapshot &a,
		const State::Snapshot &b);

[[nodiscard]] bool SingleRootPlainTextFieldSelectAllPassthrough(
		const RichPage &page,
		const std::optional<StateLeafPath> &leaf,
		bool fieldHidden);

[[nodiscard]] std::optional<int> StructuralSelectionEdgeTextOrdinal(
		const State &state,
		const PreparedEditSelection &selection,
		bool forward);

[[nodiscard]] int FieldNaturalHeight(not_null<Ui::InputField*> field);

[[nodiscard]] QPoint LocalPosition(QWheelEvent *e);

[[nodiscard]] QPoint GlobalPosition(QWheelEvent *e);

[[nodiscard]] QString RowButtonTooltip(
		const Markdown::MarkdownArticleHitTestResult &hit);

} // namespace Iv::Editor::WidgetDetails
