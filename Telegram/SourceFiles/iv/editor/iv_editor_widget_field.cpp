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
#include <limits>
#include "iv/editor/iv_editor_widget_internal.h"

namespace Iv::Editor {
using namespace WidgetDetails;

void Widget::setupInlineField() {
	if (_fieldMode == State::FieldMode::Rich) {
		const auto allowPremiumEmoji = [peer = _peer](
				not_null<DocumentData*> emoji) {
			return AllowEmojiWithoutPremium(peer, emoji);
		};
		const auto keepInlineObjectData = [](QStringView data) {
			return Markdown::ParseInlineTextObjectEntity(data).has_value();
		};
		const auto inlineObjectFactory = [
			field = _field.get(),
			articleStyle = _articleStyle
		](
				QStringView data,
				const Ui::Text::MarkedContext &context
		) -> std::unique_ptr<Ui::Text::CustomEmoji> {
			return Markdown::MakeInlineButtonObject(
				data,
				field->st().style,
				*articleStyle,
				context);
		};
		_field->setInstantViewEditorTagsEnabled(true);
		InitMessageFieldHandlers({
			.session = _session,
			.show = _show,
			.field = _field.get(),
			.customEmojiPaused = _customEmojiPaused,
			.allowPremiumEmoji = allowPremiumEmoji,
			.keepCustomEmojiData = keepInlineObjectData,
			.customEmojiFactory = inlineObjectFactory,
			.fieldStyle = &_field->st(),
			.linkValidator = ValidateInstantViewEditorLink,
			.allowMarkdownTags = {
				Ui::InputField::kTagBold,
				Ui::InputField::kTagItalic,
				Ui::InputField::kTagUnderline,
				Ui::InputField::kTagStrikeOut,
				Ui::InputField::kTagCode,
				Ui::InputField::kTagSpoiler,
				Ui::InputField::kTagIvMarked,
				Ui::InputField::kTagIvSubscript,
				Ui::InputField::kTagIvSuperscript,
				Ui::InputField::kTagIvMath,
			},
			.allowTypedMarkdown = false,
			.instantMarkdown = true,
		});
		if (_show) {
			const auto weak = QPointer<Widget>(this);
			_field->setEditLinkCallback(DefaultEditLinkCallback(
				_show,
				_field.get(),
				nullptr,
				ValidateInstantViewEditorLink,
				[=](bool active) {
					if (weak) {
						weak->setInlineFieldExternalInteractionActive(active);
						weak->notifyToolbarStateChanged();
					}
				},
				[=] {
					if (weak && !weak->_field->isHidden()) {
						weak->_field->setFocusFast();
						weak->notifyToolbarStateChanged();
					}
				}));
		}
		_fieldSuggestions = Ui::Emoji::SuggestionsController::Init(
			_outer,
			_field.get(),
			_session,
			{
				.suggestCustomEmoji = true,
				.allowCustomWithoutPremium = allowPremiumEmoji,
			});
		auto messageFieldMimeHook = WrappedMessageFieldMimeHook(
			Ui::InputField::MimeDataHook(),
			_field.get());
		_field->setMimeDataHook([=,
				messageFieldMimeHook = std::move(messageFieldMimeHook)](
				not_null<const QMimeData*> data,
				Ui::InputField::MimeAction action) {
			return handleIvClipboardMime(data, action)
				|| (messageFieldMimeHook
					? messageFieldMimeHook(data, action)
					: false);
		});
	} else {
		_fieldSuggestions = nullptr;
		_field->setInstantViewEditorTagsEnabled(false);
		_field->setInstantReplacesEnabled(
			rpl::single(false),
			rpl::single(false));
		_field->setMarkdownReplacesEnabled(
			rpl::single(Ui::MarkdownEnabledState{
				Ui::MarkdownDisabled()
			}));
	}
	_field->setDocumentMargin(0.);
	_field->setAdditionalMargins({});
	_field->setSubmitSettings(Ui::InputField::SubmitSettings::None);
	_field->setMaxHeight(std::numeric_limits<int>::max());
	refreshInlineFieldPlaceholderColor();
	const auto raw = _field->rawTextEdit();
	const auto disableFieldShortcut = [&](const QKeySequence &sequence) {
		for (const auto shortcut : raw->findChildren<QShortcut*>()) {
			if (shortcut->key().matches(sequence)
				== QKeySequence::ExactMatch) {
				shortcut->setEnabled(false);
			}
		}
	};
	disableFieldShortcut(Ui::kBlockquoteSequence);
	disableFieldShortcut(Ui::kMonospaceSequence);
	_field->customUpDown(true);
	_field->installEventFilter(this);
	raw->installEventFilter(this);
	raw->viewport()->installEventFilter(this);
	_field->addContextMenuHook([this](
			Ui::InputField::ContextMenuRequest request) {
		handleFieldContextMenuRequest(std::move(request));
	});

	const auto field = QPointer<Ui::InputField>(_field.get());
	const auto revealActiveField = [=] {
		if (!field || (_field.get() != field.data())) {
			return;
		}
		revealActiveInlineField();
	};
	_field->heightChanges(
	) | rpl::on_next([=] {
		updateInlineFieldHeightOverride();
		revealActiveField();
	}, _field->lifetime());
	_field->focusedChanges(
	) | rpl::on_next([=](bool focused) {
		if (!focused
			&& !_settingField
			&& !_trackingPointerPress
			&& !_inlineFieldExternalInteractionActive) {
			const auto committed = recordMutationTransaction([=] {
				return commitInlineField();
			});
			if (committed == ApplyResult::Changed) {
				refreshAfterInlineFieldCommit(committed);
			}
		}
	}, _field->lifetime());
	QObject::connect(
		raw->document(),
		&QTextDocument::contentsChange,
		_field.get(),
		[this, field](int, int, int) {
			if (!field || (_field.get() != field.data())) {
				return;
			}
			const auto hadRedo = _fieldRedoAvailable;
			const auto hadHistoryRedo
				= (_historyIndex + 1 < int(_history.size()));
			if (!_restoringHistory
				&& !_performingUndoRedo
				&& !_settingField
				&& !_suppressHistoryRedoInvalidation
				&& (hadRedo || hadHistoryRedo)) {
				truncateHistoryRedo();
			}
			if (!_restoringHistory && !_performingUndoRedo && !_settingField) {
				refreshInlineFieldTextEmptyOverride();
				clearFieldUndoRedoNoopState();
				_autosaveEvents.fire({
					.type = AutosaveEventType::TextIdle,
				});
			}
			crl::on_main(this, [=] {
				if (!field || (_field.get() != field.data())) {
					return;
				}
				refreshInlineFieldMaxLineWidthOverride();
				_fieldUndoAvailable = field->isUndoAvailable();
				_fieldRedoAvailable = field->isRedoAvailable();
				notifyToolbarStateChanged();
			});
		});
	QObject::connect(
		raw,
		&QTextEdit::cursorPositionChanged,
		_field.get(),
		[this, revealActiveField] {
			revealActiveField();
			notifyToolbarStateChanged();
		});
	_fieldUndoAvailable = _field->isUndoAvailable();
	_fieldRedoAvailable = _field->isRedoAvailable();

	hideInlineField();
}

void Widget::recreateInlineField(const style::InputField &st) {
	const auto text = _field->getTextWithTags();
	const auto cursor = _field->textCursor();
	const auto position = cursor.position();
	const auto anchor = cursor.anchor();
	const auto wasHidden = _field->isHidden();
	const auto hadFocus = _field->hasFocus();

	_settingField = true;
	_field = base::make_unique_q<Ui::InputField>(
		this,
		st,
		Ui::InputField::Mode::MultiLine,
		rpl::single(QString()));
	setupInlineField();
	refreshInlineFieldPlaceholder();
	const auto wasSuppressingHistoryRedoInvalidation
		= _suppressHistoryRedoInvalidation;
	_suppressHistoryRedoInvalidation = true;
	const auto suppressRedoInvalidation = gsl::finally([&] {
		_suppressHistoryRedoInvalidation
			= wasSuppressingHistoryRedoInvalidation;
	});
	_field->setTextWithTags(text, Ui::InputField::HistoryAction::Clear);
	auto restored = _field->textCursor();
	const auto size = int(_field->getLastText().size());
	const auto restoredAnchor = std::clamp(anchor, 0, size);
	const auto restoredPosition = std::clamp(position, 0, size);
	restored.setPosition(restoredAnchor);
	if (restoredPosition != restoredAnchor) {
		restored.setPosition(restoredPosition, QTextCursor::KeepAnchor);
	}
	_field->setTextCursor(restored);
	if (!wasHidden) {
		_field->show();
		_field->raise();
		if (hadFocus) {
			_field->setFocusFast();
		}
	}
	_fieldLeaf = std::nullopt;
	_settingField = false;
	_fieldUndoAvailable = _field->isUndoAvailable();
	_fieldRedoAvailable = _field->isRedoAvailable();
	clearFieldUndoRedoNoopState();
}

void Widget::setInlineFieldFromActiveState(int selectionFrom, int selectionTo) {
	ensureInlineFieldForSegment(_activeSegmentIndex);
	const auto revivedRetainedField = _revivedRetainedField;
	_revivedRetainedField = false;
	refreshInlineFieldPlaceholder();
	_settingField = true;
	const auto activeLeaf = _state->activeLeafPath();
	const auto preserveRetainedFieldSession = _restoringHistory
		&& (PreservingExternalFieldRestore == this)
		&& activeLeaf
		&& _fieldLeaf
		&& (*_fieldLeaf == *activeLeaf);
	auto cursorSelectionFrom = selectionFrom;
	auto cursorSelectionTo = selectionTo;
	auto trimmedLeft = 0;
	const auto trimLeft = !_state->codeBlockLanguage(
		_state->activeTextOrdinal()).has_value();
	const auto wasSuppressingHistoryRedoInvalidation
		= _suppressHistoryRedoInvalidation;
	_suppressHistoryRedoInvalidation = true;
	const auto suppressRedoInvalidation = gsl::finally([&] {
		_suppressHistoryRedoInvalidation
			= wasSuppressingHistoryRedoInvalidation;
	});
	if (preserveRetainedFieldSession) {
		_fieldLeaf = activeLeaf;
		_settingField = false;
		_fieldUndoAvailable = _field->isUndoAvailable();
		_fieldRedoAvailable = _field->isRedoAvailable();
		notifyToolbarStateChanged();
		return;
	}
	const auto preserveRestoredRetainedField = [&](const TextWithTags &text) {
		const auto document = _field->rawTextEdit()->document();
		const auto matchingHistoryDirection = _restoringHistoryRedo
			&& (*_restoringHistoryRedo
				? (document->availableRedoSteps() > 0
					|| _field->isRedoAvailable())
				: (document->availableUndoSteps() > 0
					|| _field->isUndoAvailable()));
		return revivedRetainedField
			&& _restoringHistory
			&& activeLeaf
			&& _fieldLeaf
			&& (*_fieldLeaf == *activeLeaf)
			&& (matchingHistoryDirection
				|| InlineFieldTextsEqual(_field->getTextWithTags(), text));
	};
	const auto finishWithRetainedField = [&] {
		_fieldLeaf = activeLeaf;
		_settingField = false;
		_fieldUndoAvailable = _field->isUndoAvailable();
		_fieldRedoAvailable = _field->isRedoAvailable();
	};
	const auto resetFieldHistory = !activeLeaf
		|| !_fieldLeaf
		|| (*_fieldLeaf != *activeLeaf);
	if (_state->activeFieldMode() == State::FieldMode::Raw) {
		const auto trimmed = TrimInlineFieldText(
			{ _state->activeRawText(), {} },
			trimLeft);
		if (preserveRestoredRetainedField(trimmed.text)) {
			clearArticleEditableHeightOverride();
			finishWithRetainedField();
			notifyToolbarStateChanged();
			return;
		}
		if (resetFieldHistory
			|| !InlineFieldTextsEqual(
				_field->getTextWithTags(),
				trimmed.text)) {
			_field->setTextWithTags(
				trimmed.text,
				Ui::InputField::HistoryAction::Clear);
		}
		trimmedLeft = trimmed.left;
		rememberInlineFieldTrim(
			_state->activeRawText(),
			trimmed.left,
			int(trimmed.text.text.size()));
		clearArticleEditableHeightOverride();
	} else {
		const auto activeText = ConvertRichTextToEditorTags(
			_state->activeText());
		const auto trimmed = TrimInlineFieldText(activeText.text, trimLeft);
		if (preserveRestoredRetainedField(trimmed.text)) {
			finishWithRetainedField();
			notifyToolbarStateChanged();
			return;
		}
		if (resetFieldHistory
			|| !InlineFieldTextsEqual(
				_field->getTextWithTags(),
				trimmed.text)) {
			_field->setTextWithTags(
				trimmed.text,
				Ui::InputField::HistoryAction::Clear);
		}
		cursorSelectionFrom = MapRichTextOffsetToEditorOffset(
			activeText.replacements,
			selectionFrom);
		cursorSelectionTo = MapRichTextOffsetToEditorOffset(
			activeText.replacements,
			selectionTo);
		trimmedLeft = trimmed.left;
		rememberInlineFieldTrim(
			activeText.text.text,
			trimmed.left,
			int(trimmed.text.text.size()));
	}
	cursorSelectionFrom -= trimmedLeft;
	cursorSelectionTo -= trimmedLeft;
	auto cursor = _field->textCursor();
	const auto size = int(_field->getLastText().size());
	const auto from = cursorPositionForFieldTextOffset(
		std::clamp(cursorSelectionFrom, 0, size));
	const auto to = cursorPositionForFieldTextOffset(
		std::clamp(cursorSelectionTo, 0, size));
	cursor.setPosition(from);
	if (to != from) {
		cursor.setPosition(to, QTextCursor::KeepAnchor);
	}
	_field->setTextCursor(cursor);
	_fieldLeaf = _state->activeLeafPath();
	_settingField = false;
	_fieldUndoAvailable = _field->isUndoAvailable();
	_fieldRedoAvailable = _field->isRedoAvailable();
	clearFieldUndoRedoNoopState();
	notifyToolbarStateChanged();
}

void Widget::activateTextOrdinal(
		int ordinal,
		int cursorOffset,
		ActivateReveal reveal) {
	activateTextOrdinal(ordinal, cursorOffset, cursorOffset, reveal);
}

void Widget::activateTextOrdinal(
		int ordinal,
		int selectionFrom,
		int selectionTo,
		ActivateReveal reveal) {
	const auto targetLeaf = [&]() -> std::optional<State::LeafPath> {
		const auto &nodes = _state->textNodes();
		return (ordinal >= 0 && ordinal < int(nodes.size()))
			? std::make_optional(nodes[ordinal].leaf)
			: std::nullopt;
	}();
	if (targetLeaf
		&& _fieldLeaf
		&& (*_fieldLeaf != *targetLeaf)) {
		retainActiveLeafField();
	}
	if (!_state->setActiveTextByOrdinal(ordinal)) {
		return;
	}
	_boundarySelectionOrigin = std::nullopt;
	_activeOrdinal = ordinal;
	_pendingOrdinal = -1;
	_pendingCursorOffset = 0;

	const auto previousSegmentIndex = _activeSegmentIndex;
	const auto segmentIndex = segmentIndexForEditableOrdinal(ordinal);
	if (segmentIndex < 0) {
		_activeSegmentIndex = -1;
		_pendingOrdinal = ordinal;
		_pendingCursorOffset = selectionTo;
		hideInlineField();
		notifyToolbarStateChanged();
		return;
	}

	if (_article && previousSegmentIndex != segmentIndex) {
		clearArticleEditableHeightOverride();
	}
	if (previousSegmentIndex != segmentIndex) {
		clearDisplayMathEditSession();
	}
	_activeSegmentIndex = segmentIndex;
	if (_article->segmentIsDisplayMath(_activeSegmentIndex)) {
		clearDisplayMathEditSession();
		clearArticleEditableHeightOverride();
		hideInlineField();
		_selection = {};
		_selectionEndpoints = {};
		setStructuralSelection({});
		update();
		notifyToolbarStateChanged();
		return;
	} else {
		clearDisplayMathEditSession();
	}
	const auto hadArticleSelection = !_selection.empty()
		|| _selectionEndpoints.from.valid()
		|| _selectionEndpoints.to.valid()
		|| hasStructuralSelection();
	_selection = {};
	_selectionEndpoints = {};
	setStructuralSelection({});
	if (hadArticleSelection) {
		update();
	}
	setInlineFieldFromActiveState(selectionFrom, selectionTo);
	_field->show();
	syncInlineFieldGeometry();
	updateInlineFieldHeightOverride();
	syncArticleVisibleTopBottom();
	if (reveal == ActivateReveal::Reveal) {
		revealActiveInlineField();
	}
	_field->raise();
	_field->setFocusFast();
	notifyToolbarStateChanged();
}

QRect Widget::activeInlineFieldRevealRect() const {
	const auto raw = _field->rawTextEdit();
	const auto cursor = _field->textCursor();
	auto positionCursor = cursor;
	positionCursor.setPosition(cursor.position());
	auto revealRect = raw->cursorRect(positionCursor);
	if (cursor.hasSelection()) {
		auto anchorCursor = cursor;
		anchorCursor.setPosition(cursor.anchor());
		revealRect = revealRect.united(raw->cursorRect(anchorCursor));
	}
	if (!revealRect.isValid() || revealRect.isEmpty()) {
		return _field->rect();
	}
	revealRect.moveTopLeft(
		raw->viewport()->mapTo(_field, revealRect.topLeft()));
	return revealRect;
}

QRect Widget::mapFieldLocalRectToScrollContent(
		QWidget *inner,
		QRect rect) const {
	rect.moveTopLeft(_field->mapTo(inner, rect.topLeft()));
	return rect;
}

void Widget::revealActiveInlineField() {
	if (inlineFieldRevealSuppressed()
		|| _field->isHidden()
		|| _activeSegmentIndex < 0) {
		return;
	}
	if (_article->revealSegment(_activeSegmentIndex)) {
		syncInlineFieldGeometry();
		if (_field->isHidden()) {
			return;
		}
	}
	const auto scrollIn = [&](auto &&scroll) {
		if (const auto inner = scroll->widget()) {
			const auto localRect = mapFieldLocalRectToScrollContent(
				inner,
				activeInlineFieldRevealRect());
			scrollRangeToMakeVisible(
				scroll,
				localRect.y(),
				localRect.y() + localRect.height());
		}
	};
	// Scroll's synthetic mouse move extends a drag-selection and re-enters.
	beginInlineFieldRevealSuppression();
	const auto revealGuard = gsl::finally([&] {
		endInlineFieldRevealSuppression();
	});
	for (auto parent = parentWidget(); parent; parent = parent->parentWidget()) {
		if (const auto scroll = dynamic_cast<Ui::ScrollArea*>(parent)) {
			scrollIn(scroll);
			return;
		}
		if (const auto scroll = dynamic_cast<Ui::ElasticScroll*>(parent)) {
			scrollIn(scroll);
			return;
		}
	}
}

void Widget::activateTrailingParagraph(LimitToast toast) {
	recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		if (committed == ApplyResult::Failed) {
			return MutationTransactionResult{
				.committed = committed,
				.failed = true,
			};
		}
		const auto ordinal = _state->ensureTrailingParagraphActive();
		if (!ordinal) {
			if (toast == LimitToast::Show) {
				showLastLimitToast();
			}
			return MutationTransactionResult{
				.committed = committed,
				.changed = (committed == ApplyResult::Changed),
			};
		}
		refreshPreparedContent();
		activateTextOrdinal(*ordinal, _state->activeText().text.size());
		return MutationTransactionResult{
			.committed = committed,
			.changed = true,
		};
	});
}

void Widget::revertInlineFieldToState() {
	if (_field->isHidden() || _activeSegmentIndex < 0) {
		return;
	}
	const auto cursor = _field->textCursor();
	setInlineFieldFromActiveState(cursor.anchor(), cursor.position());
	syncInlineFieldGeometry();
	updateInlineFieldHeightOverride();
}

std::optional<State::ActiveTextInsertContext>
Widget::activeTextInsertContext() const {
	if (_settingField
		|| _field->isHidden()
		|| (_activeSegmentIndex < 0)
		|| (_state->activeFieldMode() == State::FieldMode::Raw)) {
		return std::nullopt;
	}
	auto full = ConvertEditorTagsToRichText(_field->getTextWithAppliedMarkdown());
	const auto cursor = _field->textCursor();
	auto from = richOffsetForFieldOffset(full, cursor.selectionStart());
	auto till = richOffsetForFieldOffset(full, cursor.selectionEnd());
	const auto textSize = int(full.text.size());
	from = std::clamp(from, 0, textSize);
	till = std::clamp(till, from, textSize);
	auto before = (from > 0)
		? Ui::Text::Mid(full, 0, from)
		: TextWithEntities();
	auto selected = (till > from)
		? Ui::Text::Mid(full, from, till - from)
		: TextWithEntities();
	auto after = (till < textSize)
		? Ui::Text::Mid(full, till)
		: TextWithEntities();
	return State::ActiveTextInsertContext{
		.before = std::move(before),
		.selected = std::move(selected),
		.after = std::move(after),
	};
}

int Widget::fieldTextOffsetForCursorPosition(int position) const {
	// An emoji is one character in the document and two or more in the text.
	return (_field && position > 0)
		? int(_field->getTextWithTagsPart(0, position).text.size())
		: 0;
}

int Widget::cursorPositionForFieldTextOffset(int offset) const {
	if (!_field || offset <= 0) {
		return std::max(offset, 0);
	}
	auto from = 0;
	auto till = _field->rawTextEdit()->document()->characterCount();
	while (from < till) {
		const auto middle = from + (till - from) / 2;
		if (fieldTextOffsetForCursorPosition(middle) < offset) {
			from = middle + 1;
		} else {
			till = middle;
		}
	}
	return from;
}

void Widget::rememberInlineFieldTrim(
		const QString &full,
		int left,
		int length) {
	_fieldTrimmedLeft = full.mid(0, left);
	_fieldTrimmedRight = full.mid(left + length);
	_fieldTrimmedLeaf = _state->activeLeafPath();
}

QString Widget::inlineFieldTrimmedLeft() const {
	const auto active = _state->activeLeafPath();
	return (_fieldTrimmedLeaf && active && (*_fieldTrimmedLeaf == *active))
		? _fieldTrimmedLeft
		: QString();
}

QString Widget::inlineFieldTrimmedRight() const {
	const auto active = _state->activeLeafPath();
	return (_fieldTrimmedLeaf && active && (*_fieldTrimmedLeaf == *active))
		? _fieldTrimmedRight
		: QString();
}

int Widget::richOffsetForFieldPosition(int position) const {
	return int(inlineFieldTrimmedLeft().size())
		+ int(ConvertEditorTagsToRichText(
			_field->getTextWithTagsPart(0, position)).text.size());
}

int Widget::richOffsetForFieldOffset(
		const TextWithEntities &text,
		int offset) const {
	const auto replacements = ConvertRichTextToEditorTags(text).replacements;
	return std::clamp(
		MapEditorOffsetToRichOffset(
			replacements,
			fieldTextOffsetForCursorPosition(offset)),
		0,
		int(text.text.size()));
}

ApplyResult Widget::applyFieldTextToState() {
	if (_settingField || _field->isHidden()) {
		return ApplyResult::Unchanged;
	}
	const auto left = inlineFieldTrimmedLeft();
	const auto right = inlineFieldTrimmedRight();
	if (_state->activeFieldMode() == State::FieldMode::Raw) {
		const auto raw = _field->getLastText();
		return _state->applyActiveRawText(raw.isEmpty()
			? raw
			: (left + raw + right));
	}
	const auto text = RestoreInlineFieldEdges(
		_field->getTextWithAppliedMarkdown(),
		left,
		right);
	return _state->applyActiveText(ConvertEditorTagsToRichText(text));
}

void Widget::hideInlineField() {
	if (_field->isHidden()) {
		return;
	}
	const auto wasSettingField = _settingField;
	_settingField = true;
	const auto guard = gsl::finally([&] {
		_settingField = wasSettingField;
	});
	_insertSuggestions->close();
	_field->hide();
}

void Widget::activateTextOrdinalAtEnd(int ordinal) {
	if (!_state->setActiveTextByOrdinal(ordinal)) {
		return;
	}
	activateTextOrdinal(ordinal, _state->activeTextLength());
}

void Widget::setActiveFieldCursorOffset(int offset) {
	auto cursor = _field->textCursor();
	cursor.setPosition(std::clamp(
		offset,
		0,
		int(_field->getLastText().size())));
	_field->setTextCursor(cursor);
	_field->setFocusFast();
	revealActiveInlineField();
}

std::optional<int> Widget::activeFieldPageCursorOffset(bool down) const {
	if (_field->isHidden()) {
		return std::nullopt;
	}
	const auto pageHeight = _visibleRange.bottom - _visibleRange.top;
	if (pageHeight <= 0) {
		return std::nullopt;
	}
	const auto raw = _field->rawTextEdit();
	const auto cursor = _field->textCursor();
	const auto rect = raw->cursorRect(cursor);
	if (!rect.isValid() || rect.isEmpty()) {
		return std::nullopt;
	}
	const auto point = rect.center()
		+ QPoint(0, down ? pageHeight : -pageHeight);
	if (!raw->viewport()->rect().contains(point)) {
		return std::nullopt;
	}
	return std::clamp(
		raw->cursorForPosition(point).position(),
		0,
		int(_field->getLastText().size()));
}

std::optional<QPoint> Widget::activeFieldCursorArticlePoint() const {
	if (_field->isHidden()) {
		return std::nullopt;
	}
	const auto raw = _field->rawTextEdit();
	auto cursor = _field->textCursor();
	cursor.setPosition(cursor.position());
	const auto rect = raw->cursorRect(cursor);
	return (!rect.isValid() || rect.isEmpty())
		? std::nullopt
		: std::make_optional(
			raw->viewport()->mapTo(this, rect.center()) - articleTopLeft());
}

bool Widget::fieldCursorLeavesVisibleRow(bool down) const {
	if (_field->isHidden()) {
		return false;
	}
	const auto raw = _field->rawTextEdit();
	const auto viewport = raw->viewport()->rect();
	const auto cursor = _field->textCursor();
	auto next = cursor;
	const auto moved = next.movePosition(
		down ? QTextCursor::Down : QTextCursor::Up,
		QTextCursor::MoveAnchor);
	if (!moved || (next.position() == cursor.position())) {
		return true;
	}
	const auto nextRect = raw->cursorRect(next);
	return !nextRect.isValid()
		|| nextRect.isEmpty()
		|| (nextRect.bottom() < viewport.top())
		|| (nextRect.top() > viewport.bottom());
}

} // namespace Iv::Editor
