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

Widget::Widget(
	QWidget *parent,
	WidgetServices services,
	not_null<PeerData*> peer,
	std::shared_ptr<State> state,
	Fn<void(RichMessageLimitError)> showLimitToast)
: Ui::RpWidget(parent)
, _session(services.session)
, _show(std::move(services.show))
, _outer(services.outer)
, _customEmojiPaused(std::move(services.customEmojiPaused))
, _requestMedia(std::move(services.requestMedia))
, _requestMap(std::move(services.requestMap))
, _applyPreparedMedia(std::move(services.applyPreparedMedia))
, _prepareDeferredMedia(std::move(services.prepareDeferredMedia))
, _requestPhotoEditSource(std::move(services.requestPhotoEditSource))
, _replacePhotoWithList(std::move(services.replacePhotoWithList))
, _mediaUploadState(std::move(services.mediaUploadState))
, _cancelMediaUpload(std::move(services.cancelMediaUpload))
, _addMediaAndGroupWithBlock(std::move(services.addMediaAndGroupWithBlock))
, _submit(std::move(services.submit))
, _peer(peer)
, _state(std::move(state))
, _showLimitToast(std::move(showLimitToast))
, _articleStyle(std::make_shared<style::Markdown>(
	CreateEditorMarkdownStyle()))
, _article(std::make_shared<Markdown::MarkdownArticle>(*_articleStyle))
, _theme(Markdown::CreateStandaloneChatTheme())
, _style(std::make_unique<Ui::ChatStyle>(style::main_palette::get())) {
	_style->apply(_theme.get());
	_highlightColors = Markdown::HighlightColors(_style.get());

	setMouseTracking(true);
	setAttribute(Qt::WA_AcceptTouchEvents);
	setFocusPolicy(Qt::StrongFocus);
	setAttribute(Qt::WA_InputMethodEnabled);
	setAcceptDrops(true);

	std::move(services.imeCompositionStarts) | rpl::filter([=] {
		return redirectImeToField();
	}) | rpl::on_next([=] {
		if (prepareFieldForInput()) {
			_field->setFocusFast();
		}
	}, lifetime());

	Spellchecker::HighlightReady(
	) | rpl::on_next([=](Spellchecker::HighlightProcessId processId) {
		if (_article && _article->highlightProcessDone(processId)) {
			update();
		}
	}, _highlightReadyLifetime);

	style::PaletteChanged() | rpl::on_next([=] {
		refreshPalette();
	}, lifetime());

	const auto weak = QPointer<Widget>(this);
	_article->setTextRepaintCallbacks(
		[=] {
			if (weak) {
				weak->update();
			}
		},
		[=](QRect rect) {
			if (!weak) {
				return;
			} else if (rect.isEmpty()) {
				weak->update();
			} else {
				weak->update(rect.translated(weak->articleTopLeft()));
			}
		});
	_article->setMediaBlockHost(this);

	_selectScroll.scrolls(
	) | rpl::on_next([=](int delta) {
		const auto scroll = selectionScrollArea();
		if (!scroll) {
			_selectScroll.cancel();
			return;
		}
		scroll->scrollToY(scroll->scrollTop() + delta);
		updateArticleSelectionDragFromCursor();
	}, lifetime());

	const auto &fieldStyle = inlineFieldStyleFor(
		Markdown::MarkdownArticleTextLeafStyle());
	_activeFieldStyleKey = fieldStyle.key;
	_field = base::make_unique_q<Ui::InputField>(
		this,
		*fieldStyle.style,
		Ui::InputField::Mode::MultiLine,
		rpl::single(QString()));
	_insertSuggestions = std::make_unique<InsertSuggestionsController>(
		InsertSuggestionsDescriptor{
			.host = this,
			.outer = _outer,
			.field = [=] {
				return _field->isHidden() ? nullptr : _field.get();
			},
			.premium = AmPremiumValue(_session),
			.chosen = [=](InsertSuggestionCommand command) {
				applyInsertSuggestion(command);
			},
			.media = static_cast<bool>(_requestMedia),
			.map = static_cast<bool>(_requestMap),
		});
	setupInlineField();
	refreshPreparedContent();
	_history.push_back(captureHistoryEntry());
	_historyIndex = 0;

	base::install_event_filter(this, qApp, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::ShortcutOverride) {
			return base::EventFilterResult::Continue;
		}
		const auto top = window();
		if (!top || !top->isActiveWindow()) {
			return base::EventFilterResult::Continue;
		}
		const auto event = static_cast<QKeyEvent*>(e.get());
		if (event->isAccepted()) {
			return base::EventFilterResult::Continue;
		} else if ((event->modifiers() & Qt::ControlModifier)
			&& (event->key() == Qt::Key_F)
			&& !searchBlockedByLayer()) {
			event->accept();
			toggleSearch();
			return base::EventFilterResult::Cancel;
		} else if (handleSubmitShortcut(event)) {
			return base::EventFilterResult::Cancel;
		} else if (handleUndoRedoShortcutOverride(event)) {
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});
}

Widget::~Widget() {
	if (_keyboardStructuralSelectionActive) {
		releaseKeyboard();
	}
	if (_article) {
		_article->setTextRepaintCallbacks(nullptr, nullptr);
		_article->setMediaBlockHost(nullptr);
	}
}

void Widget::activateInitialNode() {
	const auto ordinal = (_activeOrdinal >= 0)
		? _activeOrdinal
		: _state->activeTextOrdinal();
	if (ordinal < 0) {
		const auto first = _article->firstEditableSegmentIndex();
		const auto fallback = editableOrdinalForSegment(first);
		if (fallback < 0) {
			return;
		}
		activateTextOrdinal(fallback, 0);
		return;
	}
	activateTextOrdinal(ordinal, 0);
}

void Widget::activateInitialNodeAtEnd() {
	if (_state->articleEmpty()) {
		activateInitialNode();
		return;
	} else if (!_state->lastBlockOwnsLastTextNode()) {
		activateTrailingParagraph(LimitToast::Skip);
		resetMutationHistory();
		return;
	}
	activateTextOrdinalAtEnd(_state->textNodeCount() - 1);
}

bool Widget::prepareFieldForInput() {
	if (hasStructuralSelection()) {
		if (const auto target = removeCurrentStructuralSelection(true)) {
			activateTextOrdinal(*target, 0);
		} else {
			activateInitialNode();
		}
	} else if (_field->isHidden()) {
		activateInitialNode();
	}
	return !_field->isHidden();
}

bool Widget::replayKeyIntoField(QKeyEvent *e) {
	if (!RedirectTextToField(e->text()) || !prepareFieldForInput()) {
		return false;
	}
	_field->setFocusFast();
	QCoreApplication::sendEvent(_field->rawTextEdit(), e);
	return true;
}

bool Widget::replayImeIntoField(QInputMethodEvent *e) {
	const auto cursor = _field->rawTextEdit()->textCursor();
	if (!ImeEventProducesInput(*e, cursor) || !prepareFieldForInput()) {
		return false;
	}
	_field->setFocusFast();
	QCoreApplication::sendEvent(_field->rawTextEdit(), e);
	return true;
}

ApplyResult Widget::commitInlineField() {
	const auto result = applyFieldTextToState();
	if (result == ApplyResult::Changed) {
		_preparedContentStaleAfterCommit = true;
	}
	if (result != ApplyResult::Failed) {
		return result;
	}
	revertInlineFieldToState();
	showLastLimitToast();
	return result;
}

ApplyResult Widget::commitInlineFieldForClose() {
	const auto result = recordMutationTransaction([&] {
		return applyFieldTextToState();
	});
	if (result == ApplyResult::Failed) {
		showLastLimitToast();
	} else if (result == ApplyResult::Changed) {
		refreshAfterInlineFieldCommit(result);
	}
	return result;
}

void Widget::hideInlineFieldAndRefresh() {
	if (_field->isHidden()) {
		return;
	}
	beginArticleRelayoutDeferral();
	const auto relayoutGuard = gsl::finally([&] {
		endArticleRelayoutDeferral();
	});
	const auto committed = recordMutationTransaction([&] {
		const auto committed = commitInlineField();
		_pendingOrdinal = -1;
		_pendingCursorOffset = 0;
		hideInlineField();
		clearInlineFieldEditSession();
		return committed;
	});
	refreshAfterInlineFieldCommit(committed);
}

bool Widget::commitAndActivateTextOrdinal(
		int ordinal,
		int selectionFrom,
		int selectionTo,
		ActivateReveal revealAfterRestore) {
	const auto restoreScroll = captureScrollTopRestorer();
	auto source = std::optional<Markdown::PreparedEditLeafSource>();
	auto committed = ApplyResult::Unchanged;
	beginArticleRelayoutDeferral();
	if (!_field->isHidden()) {
		source = _state->activePreparedLeafSource();
		committed = recordMutationTransaction([&] {
			const auto committed = commitInlineField();
			if (committed != ApplyResult::Failed) {
				_pendingOrdinal = -1;
				_pendingCursorOffset = 0;
				hideInlineField();
				clearInlineFieldEditSession();
			}
			return committed;
		});
		if (committed == ApplyResult::Failed) {
			endArticleRelayoutDeferral();
			return false;
		}
	}
	finishArticleSelection();
	beginInlineFieldRevealSuppression();
	{
		const auto revealGuard = gsl::finally([&] {
			endInlineFieldRevealSuppression();
		});
		activateTextOrdinal(
			ordinal,
			selectionFrom,
			selectionTo,
			ActivateReveal::Skip);
		refreshAfterInlineFieldCommit(committed, std::move(source));
	}
	endArticleRelayoutDeferral();
	if (restoreScroll) {
		restoreScroll();
	}
	if (revealAfterRestore == ActivateReveal::Reveal) {
		revealActiveInlineField();
	}
	return true;
}

void Widget::toggleSearch() {
	if (_search && _search->shown()) {
		_search->hide();
		return;
	}
	hideInlineFieldAndRefresh();
	clearStructuralSelection();
	if (!_search) {
		createSearchController();
	}
	_search->toggle();
}

bool Widget::closeSearch() {
	if (!_search || !_search->shown()) {
		return false;
	}
	_search->hide();
	return true;
}

void Widget::createSearchController() {
	auto host = SearchHost{
		.ready = [=] { return _article != nullptr; },
		.sources = [=] { return _article->searchSources(); },
		.applyMatches = [=](
				std::vector<Markdown::MarkdownArticleSearchMatch> matches,
				int current) {
			_article->setSearchMatches(std::move(matches), current);
			update();
		},
		.scrollToSegment = [=](int segmentIndex) {
			scrollToSearchSegment(segmentIndex);
		},
		.expandDetails = [](const QString &) { return false; },
		.focusContent = [=] { setFocus(); },
		.fieldFocused = [=] { hideInlineFieldAndRefresh(); },
	};
	_search = std::make_unique<SearchController>(
		_outer,
		widthValue() | rpl::map([=](int outerWidth) {
			return searchBarColumn(outerWidth).width;
		}),
		std::move(host),
		SearchBarMode::EditorPill);
	_searchSlideHeight = _search->barHeightValue();
	_searchSlideHeight.changes() | rpl::on_next([=] {
		resizeToWidth(width());
		update();
	}, lifetime());
	widthValue() | rpl::on_next([=] {
		updateSearchBarGeometry();
	}, lifetime());
	_search->raiseBar();
}

void Widget::scrollToSearchSegment(int segmentIndex) {
	const auto scroll = selectionScrollArea();
	if (!scroll || !_article) {
		return;
	}
	const auto rect = _article->segmentRect(
		segmentIndex
	).translated(articleTopLeft());
	if (rect.isEmpty()) {
		return;
	}
	const auto topMargin = _topContentPadding
		+ st::ivEditorToolbarPadding.top()
		+ st::ivEditorToolbarButtonSize
		+ 2 * st::ivEditorPillPadding;
	const auto current = scroll->scrollTop();
	const auto height = scroll->height();
	const auto from = rect.y() - topMargin;
	const auto till = rect.y() + rect.height() + _bottomContentPadding;
	auto target = current;
	if (from < current) {
		target = from;
	} else if (till > current + height) {
		target = std::min(till - height, from);
	}
	scroll->scrollToY(target);
}

void Widget::updateSearchBarGeometry() {
	if (!_search) {
		return;
	}
	_search->moveBar(searchBarColumn(width()).left, searchBarTop());
}

Widget::ArticleColumn Widget::searchBarColumn(int outerWidth) const {
	const auto column = articleColumnForWidth(outerWidth);
	return (column.width >= _contentMaxWidth)
		? column
		: ArticleColumn{ 0, outerWidth };
}

int Widget::searchBarTop() const {
	return st::ivEditorToolbarPadding.top()
		+ st::ivEditorToolbarButtonSize
		+ 2 * st::ivEditorPillPadding;
}

void Widget::refreshPreparedContent() {
	_preparedContentStaleAfterCommit = false;
	setDocument(_state->prepared());
	relayoutCurrentContent();
	update();
	if (_search) {
		if (articleRelayoutDeferralActive()) {
			_searchRefreshDeferred = true;
		} else {
			_search->refresh();
		}
	}
}

void Widget::refreshPreparedLeafAtSource(
		const Markdown::PreparedEditLeafSource &source) {
	_preparedContentStaleAfterCommit = false;
	_article->updatePreparedLeaf(source, _state->prepared());
	relayoutCurrentContent();
	if (_search) {
		if (articleRelayoutDeferralActive()) {
			_searchRefreshDeferred = true;
		} else {
			_search->refresh();
		}
	}
}

void Widget::applyExternalRichPageMutation(Fn<bool(RichPage&)> mutation) {
	if (!mutation) {
		return;
	}
	auto savedActiveIndexes = std::vector<std::pair<State::BlockPath, int>>();
	if (_article) {
		for (const auto &geo : _article->mediaBlockGeometries()) {
			if (!geo.grouped) {
				continue;
			}
			if (const auto path = _state->convertBlockPath(geo.block)) {
				savedActiveIndexes.emplace_back(*path, geo.activeItemIndex);
			}
		}
	}
	auto live = captureHistoryEntry();
	for (auto &entry : _history) {
		mutation(entry.snapshot.richPage);
	}
	mutation(live.snapshot.richPage);
	const auto wasPreservingExternalFieldRestore
		= PreservingExternalFieldRestore;
	PreservingExternalFieldRestore = this;
	const auto preserveExternalFieldRestore = gsl::finally([&] {
		PreservingExternalFieldRestore = wasPreservingExternalFieldRestore;
	});
	restoreHistoryEntry(live);
	for (const auto &[path, activeIndex] : savedActiveIndexes) {
		restoreGroupedActiveIndexForPath(path, activeIndex);
	}
	_fieldUndoAvailable = !_field->isHidden()
		? _field->isUndoAvailable()
		: false;
	_fieldRedoAvailable = !_field->isHidden()
		? _field->isRedoAvailable()
		: false;
}

void Widget::beginArticleRelayoutDeferral() {
	++_articleRelayoutDeferralDepth;
}

void Widget::endArticleRelayoutDeferral() {
	if (_articleRelayoutDeferralDepth <= 0) {
		return;
	}
	--_articleRelayoutDeferralDepth;
	if (_articleRelayoutDeferralDepth > 0) {
		return;
	}
	flushArticleRelayoutDeferral();
}

bool Widget::articleRelayoutDeferralActive() const {
	return (_articleRelayoutDeferralDepth > 0);
}

void Widget::requestDeferredArticleRelayout() {
	_articleRelayoutDeferred = true;
}

void Widget::requestDeferredInlineFieldGeometry() {
	_inlineFieldGeometryDeferred = true;
}

void Widget::requestDeferredInlineFieldHeightOverride() {
	_inlineFieldHeightOverrideDeferred = true;
}

void Widget::clearArticleEditableHeightOverride() {
	if (!_article) {
		return;
	} else if (articleRelayoutDeferralActive()) {
		_articleEditableHeightOverrideClearDeferred = true;
		requestDeferredArticleRelayout();
		return;
	}
	_article->clearEditableHeightOverride();
}

void Widget::flushArticleRelayoutDeferral() {
	if (articleRelayoutDeferralActive()) {
		return;
	}
	const auto clearHeightOverride
		= _articleEditableHeightOverrideClearDeferred;
	const auto relayout = _articleRelayoutDeferred || clearHeightOverride;
	const auto geometry = _inlineFieldGeometryDeferred;
	const auto heightOverride = _inlineFieldHeightOverrideDeferred;
	const auto searchRefresh = _searchRefreshDeferred;
	_articleEditableHeightOverrideClearDeferred = false;
	_articleRelayoutDeferred = false;
	_inlineFieldGeometryDeferred = false;
	_inlineFieldHeightOverrideDeferred = false;
	_searchRefreshDeferred = false;
	if (!relayout && !geometry && !heightOverride && !searchRefresh) {
		return;
	}
	if (clearHeightOverride && _article) {
		_article->clearEditableHeightOverride();
	}
	if (relayout) {
		relayoutCurrentContent();
		ensurePendingActivation();
	}
	if (geometry) {
		syncInlineFieldGeometry();
	}
	if (heightOverride) {
		updateInlineFieldHeightOverride();
	}
	if (searchRefresh && _search) {
		_search->refresh();
	}
	syncArticleVisibleTopBottom();
}

void Widget::beginInlineFieldRevealSuppression() {
	++_inlineFieldRevealSuppressionDepth;
}

void Widget::endInlineFieldRevealSuppression() {
	if (_inlineFieldRevealSuppressionDepth > 0) {
		--_inlineFieldRevealSuppressionDepth;
	}
}

bool Widget::inlineFieldRevealSuppressed() const {
	return (_inlineFieldRevealSuppressionDepth > 0);
}

void Widget::resizeCurrentContentToWidth(int width) {
	if (articleRelayoutDeferralActive()) {
		requestDeferredArticleRelayout();
		return;
	}
	if (width > 0) {
		resizeToWidth(width);
	} else {
		update();
	}
}

void Widget::relayoutCurrentContent() {
	const auto width = std::max(
		widthNoMargins(),
		parentWidget() ? parentWidget()->width() : 0);
	resizeCurrentContentToWidth(width);
}

void Widget::syncInlineFieldGeometry() {
	syncInlineFieldGeometry(widthNoMargins());
}

} // namespace Iv::Editor
