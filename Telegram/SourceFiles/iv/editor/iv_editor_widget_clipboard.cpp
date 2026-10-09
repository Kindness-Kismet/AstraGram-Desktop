/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_widget.h"
#include "iv/iv_rich_message_html_export.h"

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

#include <QtCore/QCoreApplication>
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

TextForMimeData Widget::currentSelectionTextForClipboard() const {
	return _article
		? _article->textForSelection(
			_selection,
			&_selectionEndpoints,
			hasStructuralSelection() ? &_structuralSelection : nullptr)
		: TextForMimeData();
}

void Widget::copyCurrentSelectionToClipboard() {
	auto structured = std::optional<ClipboardData>();
	if (hasStructuralSelection()) {
		structured = _state->structuredClipboardDataForSelection(
			_structuralSelection);
	}
	const auto text = currentSelectionTextForClipboard();
	auto mimeData = structured
		? MimeDataFromClipboardData(*structured)
		: TextUtilities::MimeDataFromText(text);
	if (!mimeData) {
		return;
	}
	if (structured) {
		if (const auto textMimeData = TextUtilities::MimeDataFromText(text)) {
			for (const auto &format : textMimeData->formats()) {
				mimeData->setData(format, textMimeData->data(format));
			}
		}
		if (const auto page = richPageForCurrentSelection()) {
			const auto html = RichBlocksClipboardHtml({
				.blocks = page->blocks,
				.rtl = _state->richPage().rtl,
			}, _session);
			if (!html.isEmpty()) {
				mimeData->setHtml(QString::fromUtf8(html));
			}
		}
	}
	QApplication::clipboard()->setMimeData(mimeData.release());
}

std::optional<TableImportResult> Widget::importTableFromMimeData(
		not_null<const QMimeData*> data) const {
	return TableFromMimeData(
		data,
		TableImportLimitsFor(
			_state->limits(),
			CountRichPageBlocks(_state->richPage())));
}

void Widget::pasteImportedTable(TableImportResult &&imported) {
	auto tableData = ClipboardBlockData();
	tableData.blocks.push_back(std::move(imported.block));
	pasteStructuredClipboardData(ClipboardData(std::move(tableData)));
	if (imported.truncated) {
		_show->showToast(tr::lng_article_table_truncated(tr::now));
	}
}

std::optional<BlocksImportResult> Widget::importBlocksFromMimeData(
		not_null<const QMimeData*> data) const {
	return BlocksFromMimeData(
		_session,
		data,
		_state->limits(),
		CountRichPageBlocks(_state->richPage()));
}

auto Widget::markdownForLiteralHtmlImport(
		const BlocksImportResult &imported,
		not_null<const QMimeData*> data) const
-> std::optional<BlocksImportResult> {
	if (!imported.localMedia.empty() || !data->hasText()) {
		return std::nullopt;
	}
	auto lines = QStringList();
	for (const auto &block : imported.blocks) {
		switch (block.kind) {
		case RichPage::BlockKind::Paragraph:
		case RichPage::BlockKind::Heading:
		case RichPage::BlockKind::Code:
			if (!block.text.text.entities.isEmpty()) {
				return std::nullopt;
			}
			lines.push_back(block.text.text.text);
			break;
		default:
			return std::nullopt;
		}
	}
	const auto limits = _state->limits();
	const auto used = CountRichPageBlocks(_state->richPage());
	if (!BlocksFromMarkdown(lines.join(QChar('\n')), limits, used)) {
		return std::nullopt;
	}
	return BlocksFromMarkdown(data->text(), limits, used);
}

[[nodiscard]] std::vector<RichPage::Block> DropImportedMediaPlaceholders(
		std::vector<RichPage::Block> blocks,
		const std::vector<std::optional<RichPage::Block>> &prepared) {
	const auto resolve = [&](uint64 id) -> const RichPage::Block* {
		const auto index = ImportedMediaPlaceholderIndex(id);
		if (!index || *index >= int(prepared.size()) || !prepared[*index]) {
			return nullptr;
		}
		return &*prepared[*index];
	};
	auto result = std::vector<RichPage::Block>();
	result.reserve(blocks.size());
	for (auto &block : blocks) {
		const auto placeholder = ImportedMediaPlaceholderIndex(
			block.photoId ? block.photoId : block.documentId);
		if (placeholder) {
			const auto ready = resolve(
				block.photoId ? block.photoId : block.documentId);
			if (!ready) {
				continue;
			}
			auto caption = std::move(block.caption);
			auto anchorId = std::move(block.anchorId);
			block = *ready;
			block.caption = std::move(caption);
			block.anchorId = std::move(anchorId);
		}
		for (auto i = block.mediaItems.begin()
			; i != block.mediaItems.end();) {
			const auto id = i->photoId ? i->photoId : i->documentId;
			if (!ImportedMediaPlaceholderIndex(id)) {
				++i;
				continue;
			}
			const auto ready = resolve(id);
			if (!ready) {
				i = block.mediaItems.erase(i);
				continue;
			}
			i->kind = ready->kind;
			i->photoId = ready->photoId;
			i->documentId = ready->documentId;
			i->width = ready->width;
			i->height = ready->height;
			i->autoplay = ready->autoplay;
			i->loop = ready->loop;
			i->spoiler = ready->spoiler;
			++i;
		}
		if ((block.kind == RichPage::BlockKind::GroupedMedia)
			&& block.mediaItems.empty()) {
			continue;
		}
		block.blocks = DropImportedMediaPlaceholders(
			std::move(block.blocks),
			prepared);
		for (auto &item : block.listItems) {
			item.blocks = DropImportedMediaPlaceholders(
				std::move(item.blocks),
				prepared);
		}
		result.push_back(std::move(block));
	}
	return result;
}

void Widget::pasteImportedBlocks(BlocksImportResult &&imported) {
	if (!imported.localMedia.empty()) {
		resolveImportedLocalMedia(std::move(imported));
		return;
	}
	if (!imported.blocks.empty()) {
		auto blocksData = ClipboardBlockData();
		blocksData.blocks = std::move(imported.blocks);
		pasteStructuredClipboardData(ClipboardData(std::move(blocksData)));
	}
	if (imported.truncated) {
		_show->showToast(tr::lng_article_paste_truncated(tr::now));
	}
}

void Widget::resolveImportedLocalMedia(BlocksImportResult &&imported) {
	auto media = base::take(imported.localMedia);
	auto list = Ui::PreparedList();
	auto order = std::vector<int>();
	if (_prepareDeferredMedia) {
		for (auto i = 0; i != int(media.size()); ++i) {
			auto content = base::take(media[i].content);
			auto image = content.isEmpty()
				? QImage()
				: QImage::fromData(content);
			auto single = content.isEmpty()
				? Storage::PrepareMediaList(
					QStringList{ media[i].path },
					st::sendMediaPreviewSize,
					SessionPremium(_session))
				: image.isNull()
				? Ui::PreparedList()
				: Storage::PrepareMediaFromImage(
					std::move(image),
					std::move(content),
					st::sendMediaPreviewSize);
			if (single.error != Ui::PreparedList::Error::None
				|| single.files.size() != 1) {
				continue;
			}
			list.files.push_back(std::move(single.files.front()));
			order.push_back(i);
		}
	}
	if (list.files.empty()) {
		imported.blocks = DropImportedMediaPlaceholders(
			std::move(imported.blocks),
			{});
		pasteImportedBlocks(std::move(imported));
		return;
	}
	const auto total = int(media.size());
	_prepareDeferredMedia(
		not_null<Widget*>(this),
		std::move(list),
		crl::guard(this, [=, imported = std::move(imported)](
				std::vector<std::optional<RichPage::Block>> prepared)
		mutable {
			auto mapped = std::vector<std::optional<RichPage::Block>>(total);
			for (auto i = 0; i != int(prepared.size()); ++i) {
				if (i < int(order.size()) && order[i] < total) {
					mapped[order[i]] = std::move(prepared[i]);
				}
			}
			imported.blocks = DropImportedMediaPlaceholders(
				std::move(imported.blocks),
				mapped);
			pasteImportedBlocks(std::move(imported));
		}));
}

void Widget::pasteStructuredClipboardData(const ClipboardData &data) {
	const auto blocks = std::get_if<ClipboardBlockData>(&data);
	const auto items = std::get_if<ClipboardListItemsData>(&data);
	if (blocks) {
		if (blocks->blocks.empty()) {
			return;
		}
	} else if (!items || items->items.empty()) {
		return;
	}
	recordMutationTransaction([&] {
		const auto context = ClipboardPasteInsertContext(
			activeTextInsertContext());
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
		const auto hadStructuralSelection = hasStructuralSelection();
		if (hadStructuralSelection || restoreField) {
			_pendingOrdinal = -1;
			_pendingCursorOffset = 0;
			hideInlineField();
			clearInlineFieldEditSession(restoreField);
		}
		auto restore = restoreField;
		const auto restoreInlineField = gsl::finally([&] {
			if (!restore) {
				return;
			}
			if (restoreLeaf && restoreStyleKey) {
				if (auto revived = reviveRetainedLeafField(
						_historyIndex,
						*restoreLeaf,
						restoreMode,
						*restoreStyleKey)) {
					_field = std::move(revived);
					_activeFieldStyleKey = restoreStyleKey;
					_fieldMode = restoreMode;
					_fieldLeaf = *restoreLeaf;
					refreshInlineFieldPlaceholder();
					_fieldUndoAvailable = _field->isUndoAvailable();
					_fieldRedoAvailable = _field->isRedoAvailable();
					clearFieldUndoRedoNoopState();
				}
			}
			if (!_fieldLeaf && restoreSelection) {
				const auto ordinal = _state->textOrdinalForLeafPath(
					restoreSelection->leaf);
				if (ordinal >= 0) {
					activateTextOrdinal(
						ordinal,
						restoreSelection->anchorOffset,
						restoreSelection->cursorOffset);
					return;
				}
			}
			_field->show();
			syncInlineFieldGeometry();
			updateInlineFieldHeightOverride();
			syncArticleVisibleTopBottom();
			revealActiveInlineField();
			_field->raise();
			_field->setFocusFast();
			notifyToolbarStateChanged();
		});
		const auto applied = hadStructuralSelection
			? (blocks
				? _state->replaceStructuralSelectionWithPreparedBlocks(
					_structuralSelection,
					blocks->blocks,
					context)
				: _state->replaceStructuralSelectionWithClipboardListItems(
					_structuralSelection,
					*items,
					context))
			: (blocks
				? _state->insertPreparedBlocksAfterActive(
					blocks->blocks,
					context)
				: _state->pasteClipboardListItemsAfterActive(
					*items,
					context));
		if (!applied) {
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		restore = false;
		if (hadStructuralSelection) {
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

bool Widget::hasFieldTextSpanSelection() const {
	return !_settingField
		&& !_field->isHidden()
		&& (_activeSegmentIndex >= 0)
		&& (_state->activeFieldMode() != State::FieldMode::Raw)
		&& _field->textCursor().hasSelection();
}

bool Widget::hasActiveSelection() const {
	return hasStructuralSelection()
		|| !_selection.empty()
		|| hasFieldTextSpanSelection();
}

rpl::producer<bool> Widget::hasSelectionValue() const {
	return _hasSelection.value();
}

void Widget::updateHasSelection() {
	_hasSelection = hasActiveSelection();
}

TextWithEntities Widget::textSpanForCurrentSelection() {
	if (hasStructuralSelection()) {
		return {};
	}
	if (hasFieldTextSpanSelection()) {
		if (const auto context = activeTextInsertContext()) {
			return context->selected;
		}
		return {};
	}
	const auto selection = _selection;
	const auto sameSegmentSelection = !selection.empty()
		&& _article
		&& (selection.from.segment == selection.to.segment)
		&& _article->segmentIsText(selection.from.segment);
	const auto ordinal = sameSegmentSelection
		? editableOrdinalForSegment(selection.from.segment)
		: -1;
	if (ordinal < 0) {
		return {};
	}
	const auto selectionFrom = selection.from.offset;
	const auto selectionTo = selection.to.offset;
	clearTextSelection();
	if (!commitAndActivateTextOrdinal(
			ordinal,
			selectionFrom,
			selectionTo)) {
		return {};
	}
	if (const auto context = activeTextInsertContext()) {
		return context->selected;
	}
	return {};
}

std::shared_ptr<const RichPage> Widget::richPageForCurrentSelection() const {
	if (hasStructuralSelection()) {
		const auto kind = _structuralSelection.kind;
		if (kind == PreparedEditSelectionKind::TableRows
			|| kind == PreparedEditSelectionKind::TableCells) {
			return _state->richPageForTableSelection(_structuralSelection);
		}
		const auto data = _state->structuredClipboardDataForSelection(
			_structuralSelection);
		if (!data) {
			return nullptr;
		}
		auto page = std::make_shared<RichPage>();
		if (const auto blocks = std::get_if<ClipboardBlockData>(&*data)) {
			page->blocks = blocks->blocks;
		} else if (const auto items
				= std::get_if<ClipboardListItemsData>(&*data)) {
			auto list = RichPage::Block();
			list.kind = RichPage::BlockKind::List;
			list.listKind = items->listKind;
			list.orderedList = items->orderedList;
			list.listItems = items->items;
			page->blocks.push_back(std::move(list));
		}
		if (page->blocks.empty()) {
			return nullptr;
		}
		return page;
	}
	return nullptr;
}

void Widget::replaceCurrentSelectionWithRichPage(
		std::shared_ptr<const RichPage> page) {
	if (!page || page->blocks.empty()) {
		return;
	}
	if (hasStructuralSelection()) {
		const auto kind = _structuralSelection.kind;
		if (kind == PreparedEditSelectionKind::TableRows
			|| kind == PreparedEditSelectionKind::TableCells) {
			auto blocks = page->blocks;
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
				using InPlace = State::TableInPlaceApplyResult;
				const auto inPlace
					= _state->replaceTableSelectionCellsInPlace(
						_structuralSelection,
						*page);
				if (inPlace == InPlace::Failed
					|| inPlace == InPlace::Unchanged) {
					if (inPlace == InPlace::Failed) {
						showLastLimitToast();
					}
					return MutationTransactionResult{
						.committed = committed,
						.changed = (committed == ApplyResult::Changed),
					};
				}
				if (inPlace == InPlace::StructureMismatch
					&& !_state->insertPreparedBlocksAfterTableSelection(
						_structuralSelection,
						std::move(blocks))) {
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
			return;
		}
		if (kind == PreparedEditSelectionKind::ListItems
			&& page->blocks.size() == 1
			&& page->blocks.front().kind == RichPage::BlockKind::List) {
			const auto &list = page->blocks.front();
			auto data = ClipboardListItemsData();
			data.listKind = list.listKind;
			data.orderedList = list.orderedList;
			data.items = list.listItems;
			data.taskList = !data.items.empty()
				&& (data.items.front().taskState
					!= RichPage::TaskState::None);
			if (!data.items.empty()) {
				pasteStructuredClipboardData(ClipboardData(std::move(data)));
				return;
			}
		}
	}
	auto data = ClipboardBlockData();
	data.blocks = page->blocks;
	pasteStructuredClipboardData(ClipboardData(std::move(data)));
}

void Widget::replaceCurrentSelectionWithText(TextWithEntities text) {
	RemoveBlockLevelEntities(&text);
	if (text.text.isEmpty()) {
		return;
	}
	auto context = activeTextInsertContext();
	if (!context || context->selected.text.isEmpty()) {
		return;
	}
	recordMutationTransaction([&] {
		const auto restoreLeaf = _fieldLeaf;
		const auto restoreStyleKey = _activeFieldStyleKey;
		const auto restoreMode = _fieldMode;
		const auto restoreSelection
			= captureHistoryViewState().leafSelection;
		_pendingOrdinal = -1;
		_pendingCursorOffset = 0;
		hideInlineField();
		clearInlineFieldEditSession(true);
		auto restore = true;
		const auto restoreInlineField = gsl::finally([&] {
			if (!restore) {
				return;
			}
			if (restoreLeaf && restoreStyleKey) {
				if (auto revived = reviveRetainedLeafField(
						_historyIndex,
						*restoreLeaf,
						restoreMode,
						*restoreStyleKey)) {
					_field = std::move(revived);
					_activeFieldStyleKey = restoreStyleKey;
					_fieldMode = restoreMode;
					_fieldLeaf = *restoreLeaf;
					refreshInlineFieldPlaceholder();
					_fieldUndoAvailable = _field->isUndoAvailable();
					_fieldRedoAvailable = _field->isRedoAvailable();
					clearFieldUndoRedoNoopState();
				}
			}
			if (!_fieldLeaf && restoreSelection) {
				const auto ordinal = _state->textOrdinalForLeafPath(
					restoreSelection->leaf);
				if (ordinal >= 0) {
					activateTextOrdinal(
						ordinal,
						restoreSelection->anchorOffset,
						restoreSelection->cursorOffset);
					return;
				}
			}
			_field->show();
			syncInlineFieldGeometry();
			updateInlineFieldHeightOverride();
			syncArticleVisibleTopBottom();
			revealActiveInlineField();
			_field->raise();
			_field->setFocusFast();
			notifyToolbarStateChanged();
		});
		const auto applied = _state->replaceActiveTextSelectionWithText(
			std::move(text),
			*context);
		if (applied.result != ApplyResult::Changed) {
			showLastLimitToast();
			return MutationTransactionResult{
				.committed = ApplyResult::Unchanged,
			};
		}
		restore = false;
		refreshPreparedContent();
		auto restored = false;
		if (applied.destinationLeaf) {
			const auto ordinal = _state->textOrdinalForLeafPath(
				*applied.destinationLeaf);
			if (ordinal >= 0) {
				activateTextOrdinal(
					ordinal,
					applied.selectionFrom,
					applied.selectionTo);
				restored = true;
			}
		}
		if (!restored) {
			const auto ordinal = _state->activeTextOrdinal();
			if (ordinal >= 0 && ordinal < _state->textNodeCount()) {
				activateTextOrdinal(ordinal, 0);
			} else {
				activateInitialNode();
			}
		}
		return MutationTransactionResult{
			.committed = ApplyResult::Unchanged,
			.changed = true,
		};
	});
}

bool Widget::handleClipboardKey(QKeyEvent *e) {
	if (e == QKeySequence::Copy) {
		if (_selection.empty() && !hasStructuralSelection()) {
			return false;
		}
		copyCurrentSelectionToClipboard();
		e->accept();
		return true;
	} else if (e == QKeySequence::Cut) {
		if (!hasStructuralSelection()
			|| !_state->canRemoveStructuralSelection(_structuralSelection)) {
			return false;
		}
		copyCurrentSelectionToClipboard();
		removeStructuralSelectionAndReposition(true);
		e->accept();
		return true;
	} else if ((e == QKeySequence::Paste) && _field->isHidden()) {
		const auto mimeData = QApplication::clipboard()->mimeData();
		if (const auto data = ClipboardDataFromMimeData(mimeData, _session)) {
			pasteStructuredClipboardData(*data);
			e->accept();
			return true;
		}
		if (mimeData && mimeData->hasHtml()) {
			if (auto imported = importBlocksFromMimeData(
					not_null<const QMimeData*>(mimeData))) {
				pasteImportedBlocks(std::move(*imported));
				e->accept();
				return true;
			}
		}
		if (mimeData && MimeDataLooksLikeTable(mimeData)) {
			if (auto imported = importTableFromMimeData(mimeData)) {
				pasteImportedTable(std::move(*imported));
				e->accept();
				return true;
			}
		}
		if (mimeData && _applyPreparedMedia) {
			if (auto list = PreparedMediaFromClipboard(
					not_null<const QMimeData*>(mimeData),
					SessionPremium(_session))) {
				_applyPreparedMedia(
					not_null<Widget*>(this),
					std::move(*list),
					preparedMediaPasteTarget());
				e->accept();
				return true;
			}
		}
		if (prepareFieldForInput()) {
			_field->setFocusFast();
			QCoreApplication::sendEvent(_field->rawTextEdit(), e);
			e->accept();
			return true;
		}
	}
	return false;
}

bool Widget::handleIvClipboardMime(
		not_null<const QMimeData*> data,
		Ui::InputField::MimeAction action) {
	if (PasteAsPlainTextRequested()) {
		return false;
	}
	const auto insertContext = ClipboardPasteInsertContext(
		activeTextInsertContext());
	const auto clipboardData = ClipboardDataFromMimeData(
		data.get(),
		_session);
	if (clipboardData && insertContext) {
		if (action == Ui::InputField::MimeAction::Check) {
			return true;
		}
		crl::on_main(this, [=, clipboardData = *clipboardData] {
			pasteStructuredClipboardData(clipboardData);
		});
		return true;
	}
	auto blockData = BlockClipboardDataFromFieldTags(data);
	if (!blockData
		&& insertContext
		&& (data->hasHtml() || MimeDataLooksLikeExportedHtml(data))) {
		if (auto imported = importBlocksFromMimeData(data)) {
			if (action == Ui::InputField::MimeAction::Check) {
				return true;
			}
			if (auto markdown = markdownForLiteralHtmlImport(
					*imported,
					data)) {
				imported = std::move(markdown);
			}
			crl::on_main(this, [=, imported = std::move(*imported)]() mutable {
				pasteImportedBlocks(std::move(imported));
			});
			return true;
		}
	}
	if (!blockData && insertContext && MimeDataLooksLikeTable(data)) {
		if (action == Ui::InputField::MimeAction::Check) {
			return true;
		} else if (auto imported = importTableFromMimeData(data)) {
			crl::on_main(this, [=, imported = std::move(*imported)]() mutable {
				pasteImportedTable(std::move(imported));
			});
			return true;
		}
	}
	if (!blockData && insertContext && data->hasText()) {
		if (auto imported = BlocksFromMarkdown(
				data->text(),
				_state->limits(),
				CountRichPageBlocks(_state->richPage()))) {
			if (action == Ui::InputField::MimeAction::Check) {
				return true;
			}
			const auto text = data->text();
			crl::on_main(this, [=, imported = std::move(*imported)]() mutable {
				pasteImportedBlocks(std::move(imported));
				offerPlainMarkdownPaste(text);
			});
			return true;
		}
	}
	if (blockData && insertContext) {
		if (action == Ui::InputField::MimeAction::Check) {
			return true;
		}
		crl::on_main(this, [=, blockData = std::move(*blockData)] {
			pasteStructuredClipboardData(blockData);
		});
		return true;
	}
	if (action == Ui::InputField::MimeAction::Check) {
		return CanPrepareMediaFromClipboard(data);
	} else if (auto list = PreparedMediaFromClipboard(
			data,
			SessionPremium(_session))) {
		if (_applyPreparedMedia) {
			auto target = preparedMediaPasteTarget();
			crl::on_main(this, [=, list = std::move(*list)]() mutable {
				_applyPreparedMedia(
					not_null<Widget*>(this),
					std::move(list),
					std::move(target));
			});
			return true;
		}
	}
	return false;
}

void Widget::offerPlainMarkdownPaste(const QString &text) {
	auto pasted = std::make_shared<RichPage>(_state->richPage());
	ChatHelpers::ShowRichPasteToast({
		.session = _session,
		.parent = _outer,
		.bottomOffset = rpl::single(_bottomContentPadding),
		.cancel = autosaveEvents() | rpl::to_empty,
		.offer = ChatHelpers::RichPasteOffer::Plain,
		.action = crl::guard(this, [=] {
			undoMarkdownPaste(text, *pasted);
		}),
	});
}

void Widget::undoMarkdownPaste(const QString &text, const RichPage &pasted) {
	if (_state->richPage() != pasted) {
		return;
	}
	performUndoRedo(false);
	auto page = SplitTextIntoRichPage(TextWithEntities{ text });
	if (page.blocks.empty()) {
		return;
	}
	crl::on_main(this, [=, blocks = std::move(page.blocks)]() mutable {
		pasteImportedBlocks({ .blocks = std::move(blocks) });
	});
}

} // namespace Iv::Editor
