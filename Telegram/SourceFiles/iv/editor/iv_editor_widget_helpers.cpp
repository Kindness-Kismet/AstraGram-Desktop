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
#include <cmath>
#include "iv/editor/iv_editor_widget_internal.h"

namespace Iv::Editor::WidgetDetails {

[[nodiscard]] bool IsFieldLineBreak(QChar ch) {
	return (ch == QChar::LineFeed)
		|| (ch == QChar::LineSeparator)
		|| (ch == QChar::ParagraphSeparator);
}

[[nodiscard]] bool MatchesKeySequence(
		QKeyEvent *e,
		const QKeySequence &sequence) {
	const auto matches = [&](Qt::KeyboardModifiers modifiers, int key) {
		const auto searchKey = (int(modifiers) | key)
			& ~(int(Qt::KeypadModifier) | int(Qt::GroupSwitchModifier));
		return sequence.matches(QKeySequence(searchKey))
			== QKeySequence::ExactMatch;
	};
	const auto modifiers = e->modifiers();
	if (matches(modifiers, e->key())) {
		return true;
	}
	const auto cleanedModifiers = int(modifiers)
		& ~(int(Qt::KeypadModifier) | int(Qt::GroupSwitchModifier));
	return (sequence == Ui::kBlockquoteSequence)
		&& (cleanedModifiers == int(Qt::ControlModifier | Qt::ShiftModifier))
		&& (e->key() == Qt::Key_Period || e->key() == Qt::Key_Greater);
}

thread_local auto PreservingExternalFieldRestore = (Widget*)nullptr;

void RemoveBlockLevelEntities(TextWithEntities *text) {
	auto &list = text->entities;
	for (auto i = list.begin(); i != list.end();) {
		const auto type = i->type();
		if (type == EntityType::Blockquote || type == EntityType::Pre) {
			i = list.erase(i);
		} else {
			++i;
		}
	}
}

[[nodiscard]] SplitCommittedFieldOffset SplitCommittedFieldOffsetAt(
		const std::vector<TextWithEntities> &chunks,
		int offset) {
	auto consumed = 0;
	for (auto i = 0, count = int(chunks.size()); i != count; ++i) {
		const auto size = int(chunks[i].text.size());
		if ((offset <= consumed + size) || (i + 1 == count)) {
			return {
				.chunkIndex = i,
				.localOffset = std::clamp(offset - consumed, 0, size),
			};
		}
		consumed += size;
	}
	return {};
}

[[nodiscard]] std::optional<StateLeafPath> SplitCommittedFieldLeafAt(
		const RichPage &page,
		const CommittedFieldSelectionCapture &capture,
		int chunkIndex) {
	if (chunkIndex < 0) {
		return std::nullopt;
	}
	if (capture.leaf.kind == StateLeafKind::BlockText) {
		const auto block = BlockFromPath(page, capture.leaf.block);
		if (!block) {
			return std::nullopt;
		} else if (block->kind == RichPage::BlockKind::Paragraph) {
			return StateLeafPath{
				.kind = StateLeafKind::BlockText,
				.block = {
					.container = capture.leaf.block.container,
					.index = capture.leaf.block.index + chunkIndex,
				},
			};
		} else if (block->kind == RichPage::BlockKind::Quote
			&& !block->pullquote) {
			return StateLeafPath{
				.kind = StateLeafKind::BlockText,
				.block = {
					.container = BlockChildrenContainer(capture.leaf.block),
					.index = chunkIndex,
				},
			};
		}
	} else if (capture.leaf.kind == StateLeafKind::ListItemText) {
		return StateLeafPath{
			.kind = StateLeafKind::BlockText,
			.block = {
				.container = ListItemChildrenContainer(
					capture.leaf.block,
					capture.leaf.listItemIndex),
				.index = chunkIndex,
			},
		};
	}
	return std::nullopt;
}

[[nodiscard]] auto CaptureCommittedFieldSelection(
		const Ui::InputField &field,
		const State &state,
		const Widget &widget,
		int (Widget::*richOffset)(const TextWithEntities &, int) const)
-> std::optional<CommittedFieldSelectionCapture> {
	if (field.isHidden()
		|| (state.activeFieldMode() != State::FieldMode::Rich)) {
		return std::optional<CommittedFieldSelectionCapture>();
	}
	const auto leaf = state.activeLeafPath();
	if (!leaf) {
		return std::optional<CommittedFieldSelectionCapture>();
	}
	const auto text = ConvertEditorTagsToRichText(
		field.getTextWithAppliedMarkdown());
	const auto cursor = field.textCursor();
	const auto length = int(text.text.size());
	return std::make_optional(CommittedFieldSelectionCapture{
		.leaf = *leaf,
		.text = text,
		.anchorOffset = std::clamp(
			(widget.*richOffset)(text, cursor.anchor()),
			0,
			length),
		.cursorOffset = std::clamp(
			(widget.*richOffset)(text, cursor.position()),
			0,
			length),
	});
}

[[nodiscard]] std::optional<CommittedFieldSelectionRestore>
MapCommittedFieldSelectionAfterCommit(
		const State &state,
		const CommittedFieldSelectionCapture &capture) {
	const auto fallback = [&](int anchor, int cursor)
			-> std::optional<CommittedFieldSelectionRestore> {
		const auto ordinal = state.activeTextOrdinal();
		if (ordinal < 0 || ordinal >= state.textNodeCount()) {
			return std::nullopt;
		}
		const auto length = state.activeTextLength();
		return CommittedFieldSelectionRestore{
			.ordinal = ordinal,
			.anchorOffset = std::clamp(anchor, 0, length),
			.cursorOffset = std::clamp(cursor, 0, length),
		};
	};
	const auto fullLength = int(capture.text.text.size());
	const auto anchorOffset = std::clamp(capture.anchorOffset, 0, fullLength);
	const auto cursorOffset = std::clamp(capture.cursorOffset, 0, fullLength);
	auto chunks = SplitFieldText(capture.text);
	if (chunks.size() <= 1) {
		const auto ordinal = state.textOrdinalForLeafPath(capture.leaf);
		if (ordinal >= 0) {
			const auto rich = RichTextFromPath(state.richPage(), capture.leaf);
			const auto length = rich ? int(rich->text.text.size()) : 0;
			return CommittedFieldSelectionRestore{
				.ordinal = ordinal,
				.anchorOffset = std::clamp(anchorOffset, 0, length),
				.cursorOffset = std::clamp(cursorOffset, 0, length),
			};
		}
		return fallback(anchorOffset, cursorOffset);
	}
	const auto anchor = SplitCommittedFieldOffsetAt(chunks, anchorOffset);
	const auto cursor = SplitCommittedFieldOffsetAt(chunks, cursorOffset);
	if (anchor.chunkIndex >= 0
		&& (anchor.chunkIndex == cursor.chunkIndex)) {
		if (const auto leaf = SplitCommittedFieldLeafAt(
				state.richPage(),
				capture,
				cursor.chunkIndex)) {
			const auto ordinal = state.textOrdinalForLeafPath(*leaf);
			const auto rich = RichTextFromPath(state.richPage(), *leaf);
			if (ordinal >= 0 && rich) {
				const auto length = int(rich->text.text.size());
				return CommittedFieldSelectionRestore{
					.ordinal = ordinal,
					.anchorOffset = std::clamp(anchor.localOffset, 0, length),
					.cursorOffset = std::clamp(cursor.localOffset, 0, length),
				};
			}
		}
	}
	return fallback(anchorOffset, cursorOffset);
}

[[nodiscard]] const QString *ToolbarActionTag(ToolbarFormatAction action) {
	switch (action) {
	case ToolbarFormatAction::Bold:
		return &Ui::InputField::kTagBold;
	case ToolbarFormatAction::Italic:
		return &Ui::InputField::kTagItalic;
	case ToolbarFormatAction::Underline:
		return &Ui::InputField::kTagUnderline;
	case ToolbarFormatAction::StrikeOut:
		return &Ui::InputField::kTagStrikeOut;
	case ToolbarFormatAction::Spoiler:
		return &Ui::InputField::kTagSpoiler;
	case ToolbarFormatAction::Subscript:
		return &Ui::InputField::kTagIvSubscript;
	case ToolbarFormatAction::Superscript:
		return &Ui::InputField::kTagIvSuperscript;
	case ToolbarFormatAction::Marked:
		return &Ui::InputField::kTagIvMarked;
	case ToolbarFormatAction::Math:
	case ToolbarFormatAction::Undo:
	case ToolbarFormatAction::Redo:
	case ToolbarFormatAction::PlainText:
	case ToolbarFormatAction::Link:
	case ToolbarFormatAction::Count:
		return nullptr;
	}
	return nullptr;
}

[[nodiscard]] std::optional<TextFormattingAction> BroaderFormattingAction(
		ToolbarFormatAction action) {
	switch (action) {
	case ToolbarFormatAction::Bold:
		return TextFormattingAction::Bold;
	case ToolbarFormatAction::Italic:
		return TextFormattingAction::Italic;
	case ToolbarFormatAction::Underline:
		return TextFormattingAction::Underline;
	case ToolbarFormatAction::StrikeOut:
		return TextFormattingAction::StrikeOut;
	case ToolbarFormatAction::Spoiler:
		return TextFormattingAction::Spoiler;
	case ToolbarFormatAction::Subscript:
		return TextFormattingAction::Subscript;
	case ToolbarFormatAction::Superscript:
		return TextFormattingAction::Superscript;
	case ToolbarFormatAction::Marked:
		return TextFormattingAction::Marked;
	case ToolbarFormatAction::PlainText:
		return TextFormattingAction::PlainText;
	case ToolbarFormatAction::Undo:
	case ToolbarFormatAction::Redo:
	case ToolbarFormatAction::Link:
	case ToolbarFormatAction::Math:
	case ToolbarFormatAction::Count:
		return std::nullopt;
	}
	return std::nullopt;
}

[[nodiscard]] const std::vector<RichPage::Block> *BlockContainer(
		const RichPage &page,
		const StateBlockContainerPath &path) {
	const auto *current = &page.blocks;
	for (const auto &step : path.steps) {
		if (!current) {
			return nullptr;
		}
		switch (step.kind) {
		case StateBlockContainerKind::Root:
			break;
		case StateBlockContainerKind::BlockChildren: {
			if (step.blockIndex < 0 || step.blockIndex >= int(current->size())) {
				return nullptr;
			}
			current = &(*current)[step.blockIndex].blocks;
		} break;
		case StateBlockContainerKind::ListItemChildren: {
			if (step.blockIndex < 0 || step.blockIndex >= int(current->size())) {
				return nullptr;
			}
			const auto &block = (*current)[step.blockIndex];
			if (step.listItemIndex < 0
				|| step.listItemIndex >= int(block.listItems.size())) {
				return nullptr;
			}
			current = &block.listItems[step.listItemIndex].blocks;
		} break;
		}
	}
	return current;
}

[[nodiscard]] const RichPage::Block *BlockFromPath(
		const RichPage &page,
		const StateBlockPath &path) {
	const auto *container = BlockContainer(page, path.container);
	if (!container || path.index < 0 || path.index >= int(container->size())) {
		return nullptr;
	}
	return &(*container)[path.index];
}

[[nodiscard]] const RichPage::RichText *RichTextFromPath(
		const RichPage &page,
		const StateLeafPath &path) {
	const auto block = BlockFromPath(page, path.block);
	if (!block) {
		return nullptr;
	}
	switch (path.kind) {
	case StateLeafKind::BlockText:
		return &block->text;
	case StateLeafKind::BlockCaption:
		return &block->caption;
	case StateLeafKind::ListItemText:
		if (path.listItemIndex < 0
			|| path.listItemIndex >= int(block->listItems.size())) {
			return nullptr;
		}
		return &block->listItems[path.listItemIndex].text;
	case StateLeafKind::TableCellText:
		if (path.tableRowIndex < 0
			|| path.tableRowIndex >= int(block->tableRows.size())) {
			return nullptr;
		}
		if (path.tableCellIndex < 0
			|| path.tableCellIndex
				>= int(block->tableRows[path.tableRowIndex].cells.size())) {
			return nullptr;
		}
		return &block->tableRows[path.tableRowIndex].cells[path.tableCellIndex]
			.text;
	case StateLeafKind::MathFormula:
		return nullptr;
	}
	return nullptr;
}

[[nodiscard]] bool TableGridCellMatchesLeaf(
		const TableGridCellReference &cell,
		const StateLeafPath &leaf,
		const StateBlockPath &block) {
	return (leaf.block == block)
		&& (leaf.kind == StateLeafKind::TableCellText)
		&& (leaf.tableRowIndex == cell.rowIndex)
		&& (leaf.tableCellIndex == cell.cellIndex);
}

[[nodiscard]] bool LeafSelectedStructurally(
		const RichPage &page,
		const StateLeafPath &leaf,
		const PreparedSelection &selection) {
	const auto path = ToPreparedBlockPath(leaf.block);
	switch (selection.kind) {
	case PreparedSelectionKind::Blocks:
		return PreparedPathInBlockRange(path, selection.blocks);
	case PreparedSelectionKind::ListItems:
		if (leaf.kind == StateLeafKind::ListItemText
			&& (path == selection.listItems.block)
			&& IndexInRange(
				leaf.listItemIndex,
				selection.listItems.from,
				selection.listItems.till)) {
			return true;
		}
		return PreparedPathInListItemRange(path, selection.listItems);
	case PreparedSelectionKind::TableRows:
		return (leaf.kind == StateLeafKind::TableCellText)
			&& (path == selection.tableRows.block)
			&& IndexInRange(
				leaf.tableRowIndex,
				selection.tableRows.from,
				selection.tableRows.till);
	case PreparedSelectionKind::TableCells: {
		if (leaf.kind != StateLeafKind::TableCellText
			|| (path != selection.tableCells.block)) {
			return false;
		}
		const auto owner = BlockFromPath(page, leaf.block);
		if (!owner || owner->kind != RichPage::BlockKind::Table) {
			return false;
		}
		for (const auto &reference : SelectedTableGridCells(
				BuildTableGrid(*owner),
				selection.tableCells)) {
			if (TableGridCellMatchesLeaf(reference, leaf, leaf.block)) {
				return true;
			}
		}
		return false;
	}
	case PreparedSelectionKind::None:
		return false;
	}
	return false;
}

[[nodiscard]] bool BlockSelectedStructurally(
		const StateBlockPath &path,
		const PreparedSelection &selection) {
	const auto prepared = ToPreparedBlockPath(path);
	switch (selection.kind) {
	case PreparedSelectionKind::Blocks:
		return PreparedPathInBlockRange(prepared, selection.blocks);
	case PreparedSelectionKind::ListItems:
		return PreparedPathInListItemRange(prepared, selection.listItems);
	case PreparedSelectionKind::TableRows:
	case PreparedSelectionKind::TableCells:
	case PreparedSelectionKind::None:
		return false;
	}
	return false;
}

[[nodiscard]] bool IsSimpleMediaBlockKind(RichPage::BlockKind kind) {
	switch (kind) {
	case RichPage::BlockKind::Photo:
	case RichPage::BlockKind::Video:
	case RichPage::BlockKind::Audio:
	case RichPage::BlockKind::File:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] uint64 MediaIdForBlock(const RichPage::Block &block) {
	switch (block.kind) {
	case RichPage::BlockKind::Photo:
		return block.photoId;
	case RichPage::BlockKind::Video:
	case RichPage::BlockKind::Audio:
	case RichPage::BlockKind::File:
		return block.documentId;
	default:
		return uint64(0);
	}
}

[[nodiscard]] uint64 MediaIdForGroupedItem(
		const RichPage::GroupedMediaItem &item) {
	switch (item.kind) {
	case RichPage::BlockKind::Photo:
		return item.photoId;
	case RichPage::BlockKind::Video:
	case RichPage::BlockKind::Audio:
		return item.documentId;
	default:
		return uint64(0);
	}
}

[[nodiscard]] bool GroupedMediaHasPhotoVideoItems(
		const RichPage::Block &block) {
	return (block.kind == RichPage::BlockKind::GroupedMedia)
		&& ranges::any_of(
			block.mediaItems,
			[](const RichPage::GroupedMediaItem &item) {
				return IsPhotoVideoBlockKind(item.kind);
			});
}

[[nodiscard]] bool GroupedPhotoVideoItemsHaveSpoiler(
		const RichPage::Block &block) {
	auto any = false;
	for (const auto &item : block.mediaItems) {
		if (!IsPhotoVideoBlockKind(item.kind)) {
			continue;
		}
		any = true;
		if (!item.spoiler) {
			return false;
		}
	}
	return any;
}

[[nodiscard]] bool RedirectTextToField(const QString &text) {
	for (const auto &ch : text) {
		if (ch.unicode() >= 32) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] TextWithTags RestoreInlineFieldEdges(
		TextWithTags text,
		const QString &left,
		const QString &right) {
	if (text.text.isEmpty() || (left.isEmpty() && right.isEmpty())) {
		return text;
	}
	if (!left.isEmpty()) {
		text.text = left + text.text;
		for (auto &tag : text.tags) {
			tag.offset += int(left.size());
		}
	}
	text.text += right;
	return text;
}

[[nodiscard]] InlineFieldTrimResult TrimInlineFieldText(
		TextWithTags text,
		bool trimLeft) {
	auto from = 0;
	auto till = int(text.text.size());
	if (trimLeft) {
		while (from < till && text.text[from].isSpace()) {
			++from;
		}
	}
	while (till > from && text.text[till - 1].isSpace()) {
		--till;
	}
	if (from == 0 && till == text.text.size()) {
		return { std::move(text), 0 };
	}
	text.text = text.text.mid(from, till - from);
	for (auto i = text.tags.begin(); i != text.tags.end();) {
		const auto tagFrom = i->offset;
		const auto tagTill = i->offset + i->length;
		const auto clippedFrom = std::max(tagFrom, from);
		const auto clippedTill = std::min(tagTill, till);
		if (clippedTill <= clippedFrom || i->length <= 0) {
			i = text.tags.erase(i);
		} else {
			i->offset = clippedFrom - from;
			i->length = clippedTill - clippedFrom;
			++i;
		}
	}
	return { std::move(text), from };
}

[[nodiscard]] QString TagWithoutCustomEmojiCounters(QStringView id) {
	auto components = QList<QStringView>();
	for (const auto &single : TextUtilities::SplitTags(id)) {
		const auto index = Ui::InputField::IsCustomEmojiLink(single)
			? single.indexOf('?')
			: -1;
		components.push_back((index < 0) ? single : single.left(index));
	}
	return TextUtilities::JoinTag(components);
}

[[nodiscard]] TextWithTags::Tags TagsWithoutCustomEmojiCounters(
		const TextWithTags::Tags &tags) {
	auto result = tags;
	for (auto &tag : result) {
		tag.id = TagWithoutCustomEmojiCounters(tag.id);
	}
	return result;
}

[[nodiscard]] bool InlineFieldTextsEqual(
		const TextWithTags &a,
		const TextWithTags &b) {
	return (a.text == b.text)
		&& (TagsWithoutCustomEmojiCounters(a.tags)
			== TagsWithoutCustomEmojiCounters(b.tags));
}

[[nodiscard]] int MapEditorOffsetToRichOffset(
		const std::vector<RichTextEditorOffsetReplacement> &replacements,
		int offset) {
	auto delta = 0;
	for (const auto &replacement : replacements) {
		if (replacement.richLength <= 0) {
			continue;
		}
		const auto richStart = replacement.richOffset;
		const auto editorStart = richStart + delta;
		const auto editorEnd = editorStart + replacement.editorLength;
		if (offset < editorStart) {
			break;
		} else if (offset <= editorEnd) {
			return richStart
				+ ((offset == editorEnd) ? replacement.richLength : 0);
		}
		delta += replacement.editorLength - replacement.richLength;
	}
	return offset - delta;
}

[[nodiscard]] bool HasRealEnterContent(const QString &text) {
	for (const auto &ch : text) {
		if (ch == QChar('\n') || !ch.isSpace()) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] std::optional<InputRule> MatchInputRule(QString typed) {
	using Type = State::InsertBlockType;
	if (typed.startsWith(QChar(' '))) {
		typed = typed.mid(1);
	}
	if (typed == u"-"_q || typed == u"*"_q || typed == u"+"_q) {
		return InputRule{ { .type = Type::BulletList } };
	} else if (typed == u">"_q) {
		return InputRule{ { .type = Type::Blockquote } };
	} else if (typed.startsWith(u"```"_q)) {
		const auto language = typed.mid(3);
		const auto plain = ranges::all_of(language, [](QChar ch) {
			return ch.isLetterOrNumber();
		});
		if (plain) {
			return InputRule{ {
				.type = Type::Code,
				.codeLanguage = language,
			} };
		}
	} else if (typed.startsWith(QChar('#'))) {
		const auto level = int(typed.size());
		const auto only = ranges::all_of(typed, [](QChar ch) {
			return (ch == QChar('#'));
		});
		if (only && (level <= 6)) {
			return InputRule{ {
				.type = Type::Heading,
				.headingLevel = level,
			} };
		}
	} else if (typed == u"[]"_q || typed == u"[ ]"_q) {
		return InputRule{ { .type = Type::TaskList } };
	} else if (typed == u"[x]"_q || typed == u"[X]"_q) {
		return InputRule{ { .type = Type::TaskList, .taskChecked = true } };
	} else if (typed.size() > 1 && typed.endsWith(QChar('.'))) {
		const auto digits = typed.left(typed.size() - 1);
		auto ok = false;
		const auto start = digits.toInt(&ok);
		if (ok && (start >= 0) && (digits == QString::number(start))) {
			return InputRule{ {
				.type = Type::OrderedList,
				.orderedStart = start,
				.orderedStartExplicit = true,
			} };
		}
	}
	return std::nullopt;
}

[[nodiscard]] State::ActiveEnterContext MakeActiveEnterContext(
		std::optional<State::ActiveTextInsertContext> context) {
	if (!context || !HasRealEnterContent(context->after.text)) {
		return {};
	} else if (!HasRealEnterContent(context->before.text)) {
		return { .position = State::EnterPosition::Beginning };
	}
	return {
		.position = State::EnterPosition::Middle,
		.head = std::move(context->before),
		.tail = std::move(context->after),
	};
}

[[nodiscard]] auto ClipboardPasteInsertContext(
		std::optional<State::ActiveTextInsertContext> context)
-> std::optional<State::ActiveTextInsertContext> {
	if (context) {
		context->selected = TextWithEntities();
	}
	return context;
}

[[nodiscard]] QString ValidateInstantViewEditorLink(QString link) {
	const auto normal = qthelp::validate_url(link);
	if (!normal.isEmpty()) {
		return normal;
	}
	link = link.trimmed();
	const auto hasPayload = [&](const QString &prefix) {
		return link.startsWith(prefix)
			&& !link.mid(prefix.size()).trimmed().isEmpty();
	};
	if (hasPayload(u"mailto:"_q)
		|| hasPayload(u"tel:"_q)
		|| (link.startsWith(u"#"_q)
			&& !Markdown::NormalizeFragmentId(link).isEmpty())) {
		return link;
	}
	return QString();
}

[[nodiscard]] bool ImeEventProducesInput(
		const QInputMethodEvent &e,
		const QTextCursor &cursor) {
	return !e.commitString().isEmpty()
		|| e.preeditString() != cursor.block().layout()->preeditAreaText()
		|| e.replacementLength() > 0;
}

[[nodiscard]] bool SnapshotEquals(
		const State::Snapshot &a,
		const State::Snapshot &b) {
	return RichPagesEqual(a.richPage, b.richPage)
		&& (a.activeLeaf == b.activeLeaf)
		&& (a.temporaryDownParagraph == b.temporaryDownParagraph);
}

[[nodiscard]] bool SingleRootPlainTextFieldSelectAllPassthrough(
		const RichPage &page,
		const std::optional<StateLeafPath> &leaf,
		bool fieldHidden) {
	if (fieldHidden
		|| !leaf
		|| (page.blocks.size() != 1)
		|| (leaf->kind != StateLeafKind::BlockText)
		|| !leaf->block.container.steps.empty()
		|| (leaf->block.index != 0)) {
		return false;
	}
	switch (page.blocks[0].kind) {
	case RichPage::BlockKind::Paragraph:
	case RichPage::BlockKind::Heading:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] std::optional<int> StructuralSelectionEdgeTextOrdinal(
		const State &state,
		const PreparedEditSelection &selection,
		bool forward) {
	const auto edge = EdgeSelection(selection, forward);
	if (edge.empty()) {
		return std::nullopt;
	}
	const auto &nodes = state.textNodes();
	const auto &page = state.richPage();
	if (forward) {
		for (auto i = int(nodes.size()); i != 0; --i) {
			const auto ordinal = i - 1;
			const auto &descriptor = nodes[ordinal];
			if (LeafSelectedStructurally(page, descriptor.leaf, edge)) {
				return ordinal;
			}
		}
	} else {
		for (auto ordinal = 0, count = int(nodes.size()); ordinal != count;
				++ordinal) {
			const auto &descriptor = nodes[ordinal];
			if (LeafSelectedStructurally(page, descriptor.leaf, edge)) {
				return ordinal;
			}
		}
	}
	return std::nullopt;
}

[[nodiscard]] int FieldNaturalHeight(not_null<Ui::InputField*> field) {
	const auto margins = field->fullTextMargins();
	return std::max(
		int(std::ceil(field->document()->size().height()))
			+ margins.top()
			+ margins.bottom(),
		1);
}

[[nodiscard]] QPoint LocalPosition(QWheelEvent *e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return e->position().toPoint();
#else // Qt >= 6.0
	return e->pos();
#endif // Qt >= 6.0
}

[[nodiscard]] QPoint GlobalPosition(QWheelEvent *e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return e->globalPosition().toPoint();
#else // Qt >= 6.0
	return e->globalPos();
#endif // Qt >= 6.0
}

[[nodiscard]] QString RowButtonTooltip(
		const Markdown::MarkdownArticleHitTestResult &hit) {
	if (hit.buttonRow.index < 0) {
		return QString();
	} else if (const auto link = hit.state.link) {
		if (const auto text = link->tooltip(); !text.isEmpty()) {
			return text;
		}
	}
	return hit.customTooltip;
}

} // namespace Iv::Editor::WidgetDetails
