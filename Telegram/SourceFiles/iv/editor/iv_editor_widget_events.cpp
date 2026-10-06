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

namespace Iv::Editor {
using namespace WidgetDetails;

bool Widget::eventFilter(QObject *object, QEvent *event) {
	if (_field) {
		const auto raw = _field->rawTextEdit();
		const auto fieldObject = (object == _field.get());
		const auto rawObject = (object == raw.get())
			|| (object == raw->viewport());
		if (fieldObject || rawObject) {
			const auto type = event->type();
			if (type == QEvent::ShortcutOverride || type == QEvent::KeyPress) {
				const auto keyEvent = static_cast<QKeyEvent*>(event);
				if (handleSubmitShortcut(keyEvent)
					|| handleFieldBlockInsertShortcut(keyEvent)
					|| handleStructuralBlockInsertShortcut(keyEvent)
					|| handleBroaderFormatShortcut(keyEvent)) {
					return true;
				} else if (type == QEvent::KeyPress
					&& (handleUndoRedoShortcut(keyEvent)
						|| handleSelectAllShortcut(keyEvent)
						|| handleTabNavigation(keyEvent)
						|| handleStructuralSelectionKey(keyEvent)
						|| handleFieldKey(keyEvent))) {
					return true;
				}
			} else if (rawObject && type == QEvent::Wheel) {
				if (_article && _activeSegmentIndex >= 0) {
					const auto wheel = static_cast<QWheelEvent*>(event);
					auto articlePoint = std::optional<QPoint>();
					if (const auto widget = qobject_cast<QWidget*>(object)) {
						articlePoint = widget->mapTo(this, LocalPosition(wheel))
							- articleTopLeft();
					} else {
						articlePoint = mapFromGlobal(GlobalPosition(wheel))
							- articleTopLeft();
					}
					if (!articlePoint) {
						const auto segmentRect = _article->segmentRect(
							_activeSegmentIndex);
						if (!segmentRect.isEmpty()) {
							articlePoint = segmentRect.center();
						}
					}
					if (articlePoint
						&& handleHorizontalScrollWheel(wheel, *articlePoint)) {
						return true;
					}
				}
			} else if (rawObject
				&& (type == QEvent::MouseButtonPress
				|| type == QEvent::MouseMove
				|| type == QEvent::MouseButtonRelease)
				&& handleFieldMouseEvent(event)) {
				return true;
			}
		}
	}
	return Ui::RpWidget::eventFilter(object, event);
}

bool Widget::eventHook(QEvent *e) {
	if (e->type() == QEvent::ShortcutOverride) {
		if (handleSubmitShortcut(static_cast<QKeyEvent*>(e))
			|| handleFieldBlockInsertShortcut(
				static_cast<QKeyEvent*>(e))
			|| handleStructuralBlockInsertShortcut(
				static_cast<QKeyEvent*>(e))
			|| handleBroaderFormatShortcut(static_cast<QKeyEvent*>(e))) {
			return true;
		}
	}
	if (e->type() == QEvent::TouchBegin
		|| e->type() == QEvent::TouchUpdate
		|| e->type() == QEvent::TouchEnd
		|| e->type() == QEvent::TouchCancel) {
		auto *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == base::TouchDevice::TouchScreen) {
			const auto active = (_horizontalScrollDrag
				== HorizontalScrollDrag::Touch);
			touchEvent(ev);
			if (active
				|| (_horizontalScrollDrag == HorizontalScrollDrag::Touch)) {
				return true;
			}
		}
	}
	return Ui::RpWidget::eventHook(e);
}

void Widget::contextMenuEvent(QContextMenuEvent *e) {
	if (!_article) {
		Ui::RpWidget::contextMenuEvent(e);
		return;
	}
	const auto articlePoint = e->pos() - articleTopLeft();
	const auto hit = _article->hitTest(
		articlePoint,
		Ui::Text::StateRequest::Flag::LookupSymbol);
	const auto editHit = _article->editHitTest(articlePoint);
	if (showMediaMenuFromHit(
			editHit,
			hit,
			e->globalPos(),
			MediaClickKind::ContextMenu)) {
		e->accept();
		return;
	}
	const auto rowButton = _article->buttonRowButtonHitTest(articlePoint);
	if (rowButton.valid()) {
		showRowButtonMenu(
			*rowButton.block,
			rowButton.index,
			rowButton.disabled,
			e->globalPos());
		e->accept();
		return;
	}
	const auto owner = StructuralOwnerFromHit(editHit);
	const auto cell = TableCellFromOwner(owner);
	if (cell) {
		const auto range = effectiveTableRangeForCell(*cell);
		if (range.empty()) {
			Ui::RpWidget::contextMenuEvent(e);
			return;
		}
		showTableContextMenu(range, e->globalPos());
		e->accept();
		return;
	}
	const auto listSources = ListContextSources(
		ListItemFromOwner(owner),
		BlockPathFromOwner(owner));
	if (!listSources.empty()) {
		if (const auto range = fullListRangeForSource(listSources.front())) {
			if (_state->listSelectionInfo(*range).valid) {
				showListContextMenu(*range, e->globalPos());
				e->accept();
				return;
			}
		}
	}
	Ui::RpWidget::contextMenuEvent(e);
}

void Widget::focusInEvent(QFocusEvent *e) {
	Ui::RpWidget::focusInEvent(e);
	if (!_settingField && !_field->isHidden()) {
		_field->setFocusFast();
	}
}

bool Widget::focusNextPrevChild(bool next) {
	if (hasFocus() && _field->isHidden() && moveTabBoundary(next)) {
		return true;
	}
	return Ui::RpWidget::focusNextPrevChild(next);
}

void Widget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape && closeSearch()) {
		e->accept();
		return;
	} else if (handleSubmitShortcut(e)) {
		return;
	} else if (handleUndoRedoShortcut(e)) {
		return;
	} else if (handleSelectAllShortcut(e)) {
		return;
	} else if (handleClipboardKey(e)) {
		return;
	} else if (handleFieldBlockInsertShortcut(e)) {
		return;
	} else if (handleStructuralBlockInsertShortcut(e)) {
		return;
	} else if (handleBroaderFormatShortcut(e)) {
		return;
	} else if (handleStructuralSelectionKey(e)) {
		return;
	} else if (_field->isHidden() && handleTabNavigation(e)) {
		return;
	} else if (redirectKeyToField(e) && replayKeyIntoField(e)) {
		e->accept();
		return;
	}
	Ui::RpWidget::keyPressEvent(e);
}

bool Widget::handleHorizontalScrollWheel(
		QWheelEvent *e,
		QPoint articlePoint) {
	const auto phase = e->phase();
	if (phase == Qt::NoScrollPhase) {
		_horizontalScrollLock = std::nullopt;
	} else if (phase == Qt::ScrollBegin) {
		_horizontalScrollLock = std::nullopt;
	}
	if (!_article) {
		return false;
	}
	const auto delta = Ui::ScrollDeltaF(e);
	const auto horizontal = (std::abs(delta.x()) > std::abs(delta.y()));
	if (phase != Qt::NoScrollPhase
		&& phase != Qt::ScrollBegin
		&& !_horizontalScrollLock) {
		_horizontalScrollLock = horizontal ? Qt::Horizontal : Qt::Vertical;
	}
	if (!_article->horizontalScrollHit(articlePoint).scrollable) {
		return false;
	}
	if (horizontal && _horizontalScrollLock == Qt::Vertical) {
		return false;
	}
	if (horizontal || _horizontalScrollLock == Qt::Horizontal) {
		if (_article->consumeHorizontalScroll(
				articlePoint,
				int(std::round(delta.x())),
				phase)) {
			syncInlineFieldGeometry();
		}
		e->accept();
		return true;
	}
	return false;
}

std::optional<PreparedEditTableCellSource> Widget::activeTableCellSourceAt(
		QObject *object,
		const QContextMenuEvent &e) const {
	if (!_article || _activeSegmentIndex < 0) {
		return std::nullopt;
	}
	const auto cellAt = [&](QPoint articlePoint) {
		const auto owner = StructuralOwnerFromHit(
			_article->editHitTest(articlePoint));
		return TableCellFromOwner(owner);
	};
	if (const auto widget = qobject_cast<QWidget*>(object)) {
		if (const auto cell = cellAt(
				widget->mapTo(this, e.pos()) - articleTopLeft())) {
			return cell;
		}
	}
	const auto segmentRect = _article->segmentRect(_activeSegmentIndex);
	return !segmentRect.isEmpty()
		? cellAt(segmentRect.center())
		: std::optional<PreparedEditTableCellSource>();
}

void Widget::touchEvent(QTouchEvent *e) {
	if (e->type() == QEvent::TouchCancel) {
		_pendingTouchHorizontalScrollPoint = std::nullopt;
		if (_horizontalScrollDrag != HorizontalScrollDrag::Touch) {
			return;
		}
		_horizontalScrollDrag = HorizontalScrollDrag::None;
		if (_article) {
			_article->endHorizontalScroll();
		}
		e->accept();
		return;
	}
	if (!_article || e->touchPoints().isEmpty()) {
		return;
	}
	const auto articlePoint = mapFromGlobal(
		e->touchPoints().cbegin()->screenPos().toPoint()) - articleTopLeft();
	switch (e->type()) {
	case QEvent::TouchBegin: {
		_pendingTouchHorizontalScrollPoint = std::nullopt;
		const auto hit = _article->horizontalScrollHit(articlePoint);
		if (hit.overScrollbar
			&& _article->beginHorizontalScroll(articlePoint, false)) {
			_horizontalScrollDrag = HorizontalScrollDrag::Touch;
			syncInlineFieldGeometry();
			e->accept();
		} else if (hit.overViewport) {
			_pendingTouchHorizontalScrollPoint = articlePoint;
		}
	} break;
	case QEvent::TouchUpdate:
		if (_horizontalScrollDrag == HorizontalScrollDrag::Touch) {
			if (_article->updateHorizontalScroll(articlePoint)) {
				syncInlineFieldGeometry();
			}
			e->accept();
		} else if (_pendingTouchHorizontalScrollPoint) {
			const auto delta = articlePoint - *_pendingTouchHorizontalScrollPoint;
			if (delta.manhattanLength() < QApplication::startDragDistance()) {
				break;
			}
			const auto horizontal = (std::abs(delta.x()) > std::abs(delta.y()));
			if (!horizontal) {
				_pendingTouchHorizontalScrollPoint = std::nullopt;
				break;
			}
			if (_article->beginHorizontalScroll(
					*_pendingTouchHorizontalScrollPoint,
					true)) {
				_horizontalScrollDrag = HorizontalScrollDrag::Touch;
				if (_article->updateHorizontalScroll(articlePoint)) {
					syncInlineFieldGeometry();
				}
				e->accept();
			}
			_pendingTouchHorizontalScrollPoint = std::nullopt;
		}
		break;
	case QEvent::TouchEnd:
		_pendingTouchHorizontalScrollPoint = std::nullopt;
		if (_horizontalScrollDrag == HorizontalScrollDrag::Touch) {
			_horizontalScrollDrag = HorizontalScrollDrag::None;
			_article->endHorizontalScroll();
			e->accept();
		}
		break;
	default:
		break;
	}
}

void Widget::wheelEvent(QWheelEvent *e) {
	if (handleHorizontalScrollWheel(
			e,
			LocalPosition(e) - articleTopLeft())) {
		return;
	}
	e->ignore();
}

bool Widget::redirectKeyToField(QKeyEvent *e) const {
	if (!hasFocus()) {
		return false;
	}
	const auto modifiers = e->modifiers()
		& ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
	return (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier)
		&& (e->key() != Qt::Key_Shift)
		&& RedirectTextToField(e->text());
}

void Widget::inputMethodEvent(QInputMethodEvent *e) {
	if (!_field) {
		Ui::RpWidget::inputMethodEvent(e);
		return;
	}
	const auto cursor = _field->rawTextEdit()->textCursor();
	if (!ImeEventProducesInput(*e, cursor) || !redirectImeToField()) {
		Ui::RpWidget::inputMethodEvent(e);
		return;
	}
	if (!replayImeIntoField(e)) {
		Ui::RpWidget::inputMethodEvent(e);
		return;
	}
	e->accept();
	return;
}

QVariant Widget::inputMethodQuery(Qt::InputMethodQuery query) const {
	if (!_field) {
		return Ui::RpWidget::inputMethodQuery(query);
	}
	return _field->rawTextEdit()->inputMethodQuery(query);
}

bool Widget::redirectImeToField() const {
	return hasFocus()
		&& (hasStructuralSelection() || _field->isHidden());
}

void Widget::leaveEventHook(QEvent *e) {
	updateHoverTooltip(QString());
	Ui::RpWidget::leaveEventHook(e);
}

void Widget::updateHoverTooltip(const QString &text) {
	if (_hoverTooltip != text) {
		_hoverTooltip = text;
		Ui::Tooltip::Hide();
	}
	if (!_hoverTooltip.isEmpty()) {
		Ui::Tooltip::Show(kTooltipDelay, this);
	}
}

QString Widget::tooltipText() const {
	return _hoverTooltip;
}

QPoint Widget::tooltipPos() const {
	return QCursor::pos();
}

bool Widget::tooltipWindowActive() const {
	return Ui::AppInFocus() && Ui::InFocusChain(window());
}

void Widget::mouseMoveEvent(QMouseEvent *e) {
	const auto articlePoint = e->pos() - articleTopLeft();
	if (_horizontalScrollDrag == HorizontalScrollDrag::Mouse) {
		if (_article->updateHorizontalScroll(articlePoint)) {
			syncInlineFieldGeometry();
		}
		e->accept();
		return;
	}
	if (_articleSelectionDrag.active && !(e->buttons() & Qt::LeftButton)) {
		finishArticleSelection();
	}
	if (!_articleSelectionDrag.active) {
		auto cursor = style::cur_default;
		auto tooltip = QString();
		const auto controlHit = _article->editControlHitTest(articlePoint);
		if (controlHit.valid()) {
			cursor = style::cur_pointer;
			using Kind = Markdown::MarkdownArticleEditControlHitKind;
			if (controlHit.kind == Kind::ButtonEdit) {
				tooltip = RowButtonTooltip(_article->hitTest(
					articlePoint,
					Ui::Text::StateRequest::Flag::LookupSymbol));
			}
		} else {
			const auto editHit = _article->editHitTest(articlePoint);
			if (simpleMediaBlockPathFromHit(editHit)
				|| groupedMediaBlockPathFromHit(editHit)
				|| clickHitsStructuralPhotoVideoSelection(editHit)) {
				cursor = style::cur_pointer;
			} else {
				const auto hit = _article->hitTest(
					articlePoint,
					Ui::Text::StateRequest::Flag::LookupSymbol);
				const auto inlineButton
					= inlineButtonEditRequestFromArticleHit(hit);
				tooltip = inlineButton
					? Markdown::RichButtonTooltip(
						inlineButton->data.type,
						inlineButton->data.payload,
						QString())
					: RowButtonTooltip(hit);
				if ((hit.valid() && hit.codeHeaderCopy) || inlineButton) {
					cursor = style::cur_pointer;
				} else if (hit.valid()
					&& hit.direct
					&& _article->segmentIsText(hit.segmentIndex)) {
					cursor = style::cur_text;
				}
			}
		}
		updateHoverTooltip(tooltip);
		setCursor(cursor);
		Ui::RpWidget::mouseMoveEvent(e);
		return;
	}
	updateHoverTooltip(QString());
	const auto hit = _article->hitTest(
		articlePoint,
		Ui::Text::StateRequest::Flag::LookupSymbol);
	const auto editHit = _article->editHitTest(articlePoint);
	const auto movedFarEnough = (e->globalPos()
		- _articleSelectionDrag.globalPressPoint).manhattanLength()
		>= QApplication::startDragDistance();
	if (!_articleSelectionDrag.dragStarted) {
		if (!movedFarEnough) {
			_selectScroll.cancel();
			e->accept();
			return;
		}
		_articleSelectionDrag.dragStarted = true;
	}
	updateArticleSelectionDragAtArticlePoint(articlePoint, hit, editHit);
	updateArticleSelectionAutoScroll(e->pos());
	e->accept();
}

void Widget::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		Ui::RpWidget::mousePressEvent(e);
		return;
	}
	if (_articleSelectionDrag.active) {
		finishArticleSelection();
	}
	_trackingPointerPress = true;
	_selectScroll.cancel();
	_pressedControl = {};
	_pressedControlPoint = std::nullopt;
	_pressedMediaControl = {};
	_pressedMediaControlPoint = std::nullopt;
	_pressedInlineButton = std::nullopt;
	_pressedInlineButtonPoint = std::nullopt;
	auto articlePoint = e->pos() - articleTopLeft();
	const auto horizontalScrollHit = _article->horizontalScrollHit(
		articlePoint);
	if (horizontalScrollHit.overScrollbar
		&& _article->beginHorizontalScroll(articlePoint, false)) {
		_horizontalScrollDrag = HorizontalScrollDrag::Mouse;
		syncInlineFieldGeometry();
		e->accept();
		return;
	}
	const auto controlHit = _article->editControlHitTest(articlePoint);
	if (controlHit.valid()) {
		_pressedControl = controlHit;
		_pressedControlPoint = articlePoint;
		e->accept();
		return;
	}
	const auto mediaControl = mediaControlHitTest(articlePoint);
	if (mediaControl.valid()) {
		_pressedMediaControl = mediaControl;
		_pressedMediaControlPoint = articlePoint;
		e->accept();
		return;
	}
	auto hit = _article->hitTest(
		articlePoint,
		Ui::Text::StateRequest::Flag::LookupSymbol);
	const auto editHit = _article->editHitTest(articlePoint);
	const auto startedBelow = (articlePoint.y() >= _articleHeight);
	const auto pressedSelectedText = _article->selectionContains(
		_selection,
		&_selectionEndpoints,
		hit);
	const auto pressedSelectedStructuralOwner = [&] {
		if (_structuralSelection.empty()) {
			return false;
		}
		const auto owner = StructuralOwnerFromHit(editHit);
		if (!owner.valid()) {
			return false;
		}
		switch (_structuralSelection.kind) {
		case PreparedEditSelectionKind::Blocks:
			if (const auto path = BlockPathFromOwner(owner)) {
				return PreparedPathInBlockRange(
					*path,
					_structuralSelection.blocks);
			}
			return false;
		case PreparedEditSelectionKind::ListItems:
			if (const auto listItem = ListItemFromOwner(owner)) {
				return SamePreparedEditBlockPath(
					listItem->block,
					_structuralSelection.listItems.block)
					&& IndexInRange(
						listItem->listItemIndex,
						_structuralSelection.listItems.from,
						_structuralSelection.listItems.till);
			}
			if (const auto path = BlockPathFromOwner(owner)) {
				return PreparedPathInListItemRange(
					*path,
					_structuralSelection.listItems);
			}
			return false;
		case PreparedEditSelectionKind::None:
		case PreparedEditSelectionKind::TableRows:
		case PreparedEditSelectionKind::TableCells:
			return false;
		}
		return false;
	}();
	if ((pressedSelectedText || pressedSelectedStructuralOwner)
		&& startSelectionDragFromExistingState(
			articlePoint,
			e->globalPos(),
			editHit)) {
		e->accept();
		return;
	}
	if (hit.codeHeaderCopy) {
		startArticleSelection(articlePoint, e->globalPos(), hit, editHit);
		e->accept();
		return;
	}
	_pressedInlineButton = inlineButtonEditRequestFromArticleHit(hit);
	if (_pressedInlineButton) {
		_pressedInlineButtonPoint = articlePoint;
		e->accept();
		return;
	}
	if (hit.valid() && hit.direct && _article->segmentIsText(hit.segmentIndex)) {
		startArticleSelection(articlePoint, e->globalPos(), hit, editHit);
		e->accept();
		return;
	}
	if (startedBelow) {
		if (editHit.valid()) {
			startArticleSelection(
				articlePoint,
				e->globalPos(),
				hit,
				editHit,
				false,
				true);
		} else {
			clearSelection();
		}
		e->accept();
		return;
	}
	if (editHit.valid()) {
		startArticleSelection(articlePoint, e->globalPos(), hit, editHit);
		e->accept();
		return;
	}
	clearSelection();
	e->accept();
}

void Widget::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		Ui::RpWidget::mouseReleaseEvent(e);
		return;
	}
	const auto guard = gsl::finally([&] {
		_trackingPointerPress = false;
	});
	const auto finishDrag = gsl::finally([&] {
		finishArticleSelection();
	});
	const auto articlePoint = e->pos() - articleTopLeft();
	if (_horizontalScrollDrag == HorizontalScrollDrag::Mouse) {
		const auto changed = _article->updateHorizontalScroll(articlePoint);
		_article->endHorizontalScroll();
		_horizontalScrollDrag = HorizontalScrollDrag::None;
		if (changed) {
			syncInlineFieldGeometry();
		}
		e->accept();
		return;
	}
	const auto controlHit = _article->editControlHitTest(articlePoint);
	const auto applyControlToggle = [&](
			Fn<bool()> toggle,
			Fn<void()> afterRefresh) {
		const auto mutated = applyMutationWithFieldCommit([&] {
			return toggle()
				? ApplyResult::Changed
				: ApplyResult::Unchanged;
		}, std::move(afterRefresh));
		return (mutated == ApplyResult::Changed);
	};
	if (_pressedControl.valid()) {
		const auto pressedControl = _pressedControl;
		const auto pressedControlPoint = _pressedControlPoint;
		_pressedControl = {};
		_pressedControlPoint = std::nullopt;
		const auto matchedControl = pressedControlPoint
			&& ((articlePoint - *pressedControlPoint).manhattanLength()
				< QApplication::startDragDistance())
			&& (controlHit == pressedControl);
		if (matchedControl) {
			switch (pressedControl.kind) {
			case Markdown::MarkdownArticleEditControlHitKind::TaskMarker:
				if (pressedControl.listItem) {
					applyControlToggle([&] {
						return _state->toggleTaskState(*pressedControl.listItem);
					}, [&] {
						_article->addTaskMarkerRipple(
							*pressedControl.listItem,
							articlePoint);
					});
				}
				break;
			case Markdown::MarkdownArticleEditControlHitKind::DetailsToggle:
				if (pressedControl.block) {
					applyControlToggle([&] {
						return _state->toggleDetailsOpen(*pressedControl.block);
					}, [] {
					});
				}
				break;
			case Markdown::MarkdownArticleEditControlHitKind::QuoteCollapse:
				if (pressedControl.block) {
					const auto source = *pressedControl.block;
					auto movedCaret = false;
					applyControlToggle([&] {
						return _state->toggleQuoteCollapsed(
							source,
							&movedCaret);
					}, [&] {
						const auto ordinal = _state->textOrdinalForLeaf({
							.kind = Markdown::PreparedEditLeafKind::BlockText,
							.block = source.path,
						});
						activateTextOrdinal(
							(ordinal >= 0)
								? ordinal
								: _state->activeTextOrdinal(),
							0,
							(movedCaret || (ordinal >= 0))
								? ActivateReveal::Reveal
								: ActivateReveal::Skip);
					});
				}
				break;
			case Markdown::MarkdownArticleEditControlHitKind::ButtonEdit:
				if (pressedControl.block) {
					const auto request = rowButtonEditRequest(
						*pressedControl.block,
						pressedControl.buttonIndex);
					if (request) {
						showButtonEditBox(*request);
					}
				}
				break;
			case Markdown::MarkdownArticleEditControlHitKind::ButtonRowMenu:
				if (pressedControl.block) {
					showButtonRowMenu(*pressedControl.block, e->globalPos());
				}
				break;
			case Markdown::MarkdownArticleEditControlHitKind::None:
				break;
			}
		}
		e->accept();
		return;
	}
	if (_pressedInlineButton) {
		const auto pressed = base::take(_pressedInlineButton);
		const auto pressedPoint = base::take(_pressedInlineButtonPoint);
		const auto matched = pressedPoint
			&& ((articlePoint - *pressedPoint).manhattanLength()
				< QApplication::startDragDistance());
		if (matched) {
			const auto releaseHit = _article->hitTest(
				articlePoint,
				Ui::Text::StateRequest::Flag::LookupSymbol);
			const auto request = inlineButtonEditRequestFromArticleHit(
				releaseHit);
			if (request
				&& (request->ordinal == pressed->ordinal)
				&& (request->offset == pressed->offset)) {
				showButtonEditBox(*request);
			}
		}
		e->accept();
		return;
	}
	if (_pressedMediaControl.valid()) {
		const auto pressed = _pressedMediaControl;
		const auto pressedPoint = _pressedMediaControlPoint;
		_pressedMediaControl = {};
		_pressedMediaControlPoint = std::nullopt;
		const auto current = mediaControlHitTest(articlePoint);
		const auto matched = pressedPoint
			&& ((articlePoint - *pressedPoint).manhattanLength()
				< QApplication::startDragDistance())
			&& (current.control == pressed.control)
			&& (current.path == pressed.path)
			&& (current.itemIndex == pressed.itemIndex);
		if (matched) {
			switch (pressed.control) {
			case MediaControl::ThreeDots:
				if (pressed.itemIndex >= 0) {
					showGroupedMediaMenu(
						pressed.path,
						pressed.itemIndex,
						e->globalPos());
				} else {
					showSimpleMediaMenu(pressed.path, e->globalPos());
				}
				break;
			case MediaControl::Plus:
				addToCollageFromBlock(pressed.path);
				break;
			case MediaControl::UploadRadial:
				if (pressed.itemIndex >= 0) {
					cancelMediaUploadForGroupedItem(
						pressed.path,
						pressed.itemIndex);
				} else {
					cancelMediaUploadForBlock(pressed.path);
				}
				break;
			case MediaControl::LayoutSwitch:
				toggleGroupedMediaIntent(pressed.path);
				break;
			case MediaControl::None:
				break;
			}
		}
		e->accept();
		return;
	}
	const auto hit = _article->hitTest(
		articlePoint,
		Ui::Text::StateRequest::Flag::LookupSymbol);
	const auto editHit = _article->editHitTest(articlePoint);
	const auto formulaOrdinalFromEditHit = [&] {
		return editHit.leaf
			&& (editHit.leaf->kind
				== Markdown::PreparedEditLeafKind::MathFormula)
			? _state->textOrdinalForLeaf(*editHit.leaf)
			: -1;
	};
	const auto directEditableHit = [&] {
		return (hit.valid()
			&& hit.direct
			&& _article->segmentIsEditable(hit.segmentIndex))
			|| (formulaOrdinalFromEditHit() >= 0);
	};
	const auto commitVisibleInlineField = [&] {
		if (_field->isHidden()) {
			return false;
		}
		beginArticleRelayoutDeferral();
		const auto relayoutGuard = gsl::finally([&] {
			endArticleRelayoutDeferral();
		});
		const auto source = _state->activePreparedLeafSource();
		const auto committed = recordMutationTransaction([&] {
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
			return false;
		}
		refreshAfterInlineFieldCommit(committed, source);
		return true;
	};
	const auto focusOrActivateInitial = [&] {
		if (_field->isHidden()) {
			activateInitialNode();
		} else {
			_field->setFocusFast();
		}
	};
	const auto editCodeBlockLanguage = [&] {
		if (!hit.codeHeaderCopy) {
			return false;
		}
		auto languageHit = hit;
		if (!_field->isHidden()) {
			if (!commitVisibleInlineField()) {
				return true;
			}
			languageHit = _article->hitTest(
				articlePoint,
				Ui::Text::StateRequest::Flag::LookupSymbol);
		}
		const auto ordinal = languageHit.codeHeaderCopy
			? editableOrdinalForSegment(languageHit.segmentIndex)
			: -1;
		if (const auto now = _state->codeBlockLanguage(ordinal)) {
			const auto weak = QPointer<Widget>(this);
			DefaultEditLanguageCallback(_show)(
				*now,
				[=](QString language) {
					if (!weak) {
						return;
					}
					weak->recordMutationTransaction([&] {
						const auto changed = weak->_state->setCodeBlockLanguage(
							ordinal,
							language);
						if (changed) {
							weak->refreshPreparedContent();
							weak->update();
						}
						return changed;
					});
				});
		}
		return true;
	};
	if (_articleSelectionDrag.active) {
		const auto fromField = _articleSelectionDrag.fromField;
		const auto pendingCodeHeader = _articleSelectionDrag.codeHeader;
		const auto startedBelow = _articleSelectionDrag.startedBelow;
		const auto operation = _articleSelectionDrag.operation;
		const auto clickLike = !_articleSelectionDrag.dragStarted
			&& ((e->globalPos()
				- _articleSelectionDrag.globalPressPoint).manhattanLength()
				< QApplication::startDragDistance());
		const auto updateOnRelease
			= !clickLike
			&& ((_articleSelectionDrag.mode != DragSelectionMode::None)
				|| (!pendingCodeHeader
					&& (!startedBelow || articlePoint.y() < _articleHeight)));
		if (updateOnRelease) {
			if (operation == ArticleSelectionOperation::DragSelection) {
				updateArticleDropTarget(articlePoint);
			} else {
				updateArticleSelection(articlePoint, hit, editHit);
			}
		}
		if (clickLike) {
			if (activateMediaBlockLinkFromHit(editHit, hit, e->button())) {
				e->accept();
				return;
			}
			if (showMediaMenuFromHit(
					editHit,
					hit,
					e->globalPos(),
					MediaClickKind::Left)) {
				e->accept();
				return;
			}
			const auto changed = !_selection.empty()
				|| _selectionEndpoints.from.valid()
				|| _selectionEndpoints.to.valid()
				|| hasStructuralSelection();
			_selection = {};
			_selectionEndpoints = {};
			setStructuralSelection({});
			if (changed) {
				update();
			}
		}
		if (!clickLike
			&& (operation == ArticleSelectionOperation::DragSelection)) {
			if (_articleSelectionDrag.dropTarget) {
				if (_articleSelectionDrag.mode == DragSelectionMode::Structural) {
					applyStructuralSelectionDrop();
				} else if (_articleSelectionDrag.mode
					== DragSelectionMode::Text) {
					applyInlineSelectionDrop();
				}
			}
			clearArticleDropTarget();
			e->accept();
			return;
		}
		if (!clickLike && hasStructuralSelection()) {
			commitVisibleInlineField();
			e->accept();
			return;
		}
		if (_articleSelectionDrag.mode == DragSelectionMode::Text) {
			const auto selection = _selection;
			const auto sameSegmentSelection = !selection.empty()
				&& (selection.from.segment == selection.to.segment)
				&& _article->segmentIsText(selection.from.segment);
			const auto selectionOrdinal = sameSegmentSelection
				? editableOrdinalForSegment(selection.from.segment)
				: -1;
			if (!fromField && selectionOrdinal >= 0) {
				const auto selectionFrom = selection.from.offset;
				const auto selectionTo = selection.to.offset;
				clearTextSelection();
				commitAndActivateTextOrdinal(
					selectionOrdinal,
					selectionFrom,
					selectionTo);
				e->accept();
				return;
			} else if (fromField) {
				e->accept();
				return;
			}
		}
		if (pendingCodeHeader
			&& _articleSelectionDrag.mode == DragSelectionMode::None
			&& editCodeBlockLanguage()) {
			e->accept();
			return;
		}
		const auto changed = !_selection.empty()
			|| _selectionEndpoints.from.valid()
			|| _selectionEndpoints.to.valid()
			|| hasStructuralSelection();
		_selection = {};
		_selectionEndpoints = {};
		setStructuralSelection({});
		if (changed) {
			update();
		}
	} else if (hit.codeHeaderCopy && editCodeBlockLanguage()) {
		e->accept();
		return;
	}
	if (directEditableHit()) {
		const auto formulaOrdinal = formulaOrdinalFromEditHit();
		if (formulaOrdinal >= 0) {
			const auto activeDisplayMath = !_field->isHidden()
				&& (_state->activeTextOrdinal() == formulaOrdinal)
				&& (_state->activeFieldMode() == State::FieldMode::Raw);
			if (!activeDisplayMath) {
				if (!_field->isHidden() && !commitVisibleInlineField()) {
					e->accept();
					return;
				}
				activateTextOrdinal(formulaOrdinal, 0);
			}
			editMathFromToolbar();
			e->accept();
			return;
		}
		const auto segmentHit = hit.valid()
			&& hit.direct
			&& _article->segmentIsEditable(hit.segmentIndex);
		const auto targetOrdinal = segmentHit
			? editableOrdinalForSegment(hit.segmentIndex)
			: formulaOrdinal;
		const auto offset = segmentHit
			? _article->selectionOffsetFromHit(
				hit,
				TextSelectType::Letters)
			: 0;
		if (targetOrdinal >= 0
			&& !_field->isHidden()
			&& hit.segmentIndex == _activeSegmentIndex) {
			auto cursor = _field->textCursor();
			cursor.setPosition(std::clamp(
				offset,
				0,
				int(_field->getLastText().size())));
			_field->setTextCursor(cursor);
			_field->setFocusFast();
		} else if (targetOrdinal >= 0) {
			commitAndActivateTextOrdinal(
				targetOrdinal,
				offset,
				offset);
		}
	} else if (articlePoint.y() >= _articleHeight) {
		activateTrailingParagraph();
	} else if (activateMediaBlockLinkFromHit(editHit, hit, e->button())) {
		e->accept();
		return;
	} else if (!showMediaMenuFromHit(
			editHit,
			hit,
			e->globalPos(),
			MediaClickKind::Left)) {
		focusOrActivateInitial();
	}
	e->accept();
}

bool Widget::handleFieldMouseEvent(QEvent *event) {
	if (!_article || _field->isHidden() || _activeSegmentIndex < 0) {
		return false;
	}
	const auto type = event->type();
	const auto mouse = static_cast<QMouseEvent*>(event);
	if (type == QEvent::MouseButtonPress) {
		if (mouse->button() != Qt::LeftButton) {
			return false;
		}
		const auto segmentRect = _article->segmentRect(_activeSegmentIndex);
		if (segmentRect.isEmpty()) {
			return false;
		}
		auto anchorHit = _article->editHitTest(segmentRect.center());
		if (!anchorHit.valid()) {
			anchorHit = _article->editHitTest(segmentRect.topLeft());
		}
		if (!anchorHit.valid()) {
			return false;
		}
		const auto globalPoint = mouse->globalPos();
		const auto widgetPoint = mapFromGlobal(globalPoint);
		const auto articlePoint = widgetPoint - articleTopLeft();
		const auto cursor = _field->textCursor();
		const auto raw = _field->rawTextEdit();
		const auto pressCursor = raw->cursorForPosition(
			raw->viewport()->mapFromGlobal(globalPoint));
		const auto pressingCurrentSelection
			= (_state->activeFieldMode() == State::FieldMode::Rich)
			&& cursor.hasSelection()
			&& (pressCursor.position() >= cursor.selectionStart())
			&& (pressCursor.position() < cursor.selectionEnd());
		_selectScroll.cancel();
		_trackingPointerPress = true;
		if (pressingCurrentSelection
			&& startSelectionDragFromExistingState(
				articlePoint,
				globalPoint,
				anchorHit,
				true)) {
			return false;
		}
		const auto buttonRequest = inlineButtonEditRequestFromFieldPoint(
			globalPoint);
		if (buttonRequest) {
			_trackingPointerPress = false;
			showButtonEditBox(*buttonRequest);
			return true;
		}
		clearTextSelection();
		clearStructuralSelection();
		_articleSelectionDrag = {
			.active = true,
			.fromField = true,
			.startedBelow = false,
			.codeHeader = false,
			.pressPoint = articlePoint,
			.globalPressPoint = globalPoint,
			.anchorHit = anchorHit,
			.textSegment = _activeSegmentIndex,
			.textOffset = std::clamp(
				cursor.position(),
				0,
				int(_field->getLastText().size())),
			.operation = ArticleSelectionOperation::GrowSelection,
			.mode = DragSelectionMode::Text,
		};
		return false;
	} else if (!_articleSelectionDrag.active
		|| !_articleSelectionDrag.fromField) {
		return false;
	} else if (type == QEvent::MouseButtonRelease
		&& mouse->button() != Qt::LeftButton) {
		return false;
	} else if (type == QEvent::MouseMove
		&& !(mouse->buttons() & Qt::LeftButton)) {
		finishArticleSelection();
		_trackingPointerPress = false;
		return false;
	}

	const auto globalPoint = mouse->globalPos();
	const auto widgetPoint = mapFromGlobal(globalPoint);
	const auto articlePoint = widgetPoint - articleTopLeft();
	const auto operation = _articleSelectionDrag.operation;
	const auto movedFarEnough = (globalPoint
		- _articleSelectionDrag.globalPressPoint).manhattanLength()
		>= QApplication::startDragDistance();
	if (type == QEvent::MouseMove && !_articleSelectionDrag.dragStarted) {
		if (!movedFarEnough) {
			_selectScroll.cancel();
			return false;
		}
		_articleSelectionDrag.dragStarted = true;
	}
	const auto clickLike = (type == QEvent::MouseButtonRelease)
		&& !_articleSelectionDrag.dragStarted
		&& !movedFarEnough;
	const auto hit = _article->hitTest(
		articlePoint,
		Ui::Text::StateRequest::Flag::LookupSymbol);
	const auto editHit = _article->editHitTest(articlePoint);
	const auto fieldPoint = _field->mapFromGlobal(globalPoint);
	const auto insideActiveField = _field->rect().contains(fieldPoint);
	const auto originalSegmentHit = hit.valid()
		&& hit.direct
		&& (hit.segmentIndex == _articleSelectionDrag.textSegment)
		&& _article->segmentIsEditable(hit.segmentIndex);
	const auto originalMathFormulaHit = _articleSelectionDrag.anchorHit.leaf
		&& (_articleSelectionDrag.anchorHit.leaf->kind
			== Markdown::PreparedEditLeafKind::MathFormula)
		&& editHit.leaf
		&& (*editHit.leaf == *_articleSelectionDrag.anchorHit.leaf);
	const auto insideFieldBand = (fieldPoint.y() >= 0)
		&& (fieldPoint.y() < _field->height());
	const auto bandSelectsInField = insideFieldBand
		&& !insideActiveField
		&& !originalSegmentHit
		&& !originalMathFormulaHit
		&& (operation == ArticleSelectionOperation::GrowSelection)
		&& !_articleSelectionDrag.anchorHit.tableCell;
	const auto clearArticleSelection = [&] {
		const auto changed = !_selection.empty()
			|| _selectionEndpoints.from.valid()
			|| _selectionEndpoints.to.valid()
			|| hasStructuralSelection();
		_selection = {};
		_selectionEndpoints = {};
		setStructuralSelection({});
		if (changed) {
			update();
		}
	};
	if (insideActiveField
		|| originalSegmentHit
		|| originalMathFormulaHit
		|| bandSelectsInField) {
		if ((operation == ArticleSelectionOperation::GrowSelection)
			&& (_articleSelectionDrag.mode == DragSelectionMode::Structural)) {
			clearArticleSelection();
			_articleSelectionDrag.mode = DragSelectionMode::Text;
		}
		if ((operation == ArticleSelectionOperation::GrowSelection)
			&& _articleSelectionDrag.interruptedFieldAnchor) {
			const auto raw = _field->rawTextEdit();
			const auto pointerCursor = raw->cursorForPosition(
				raw->viewport()->mapFromGlobal(globalPoint));
			const auto size = int(_field->getLastText().size());
			const auto anchor = std::clamp(
				*_articleSelectionDrag.interruptedFieldAnchor,
				0,
				size);
			const auto position = std::clamp(
				pointerCursor.position(),
				0,
				size);
			auto cursor = _field->textCursor();
			cursor.setPosition(anchor);
			if (position != anchor) {
				cursor.setPosition(position, QTextCursor::KeepAnchor);
			}
			_field->setTextCursor(cursor);
			_articleSelectionDrag.interruptedFieldAnchor = std::nullopt;
		}
		if (type == QEvent::MouseButtonRelease) {
			clearArticleDropTarget();
			finishArticleSelection();
			_trackingPointerPress = false;
		} else {
			_selectScroll.cancel();
			if (bandSelectsInField) {
				const auto raw = _field->rawTextEdit();
				const auto pointerCursor = raw->cursorForPosition(
					raw->viewport()->mapFromGlobal(globalPoint));
				const auto size = int(_field->getLastText().size());
				const auto position = std::clamp(
					pointerCursor.position(),
					0,
					size);
				auto cursor = _field->textCursor();
				if (cursor.position() != position) {
					cursor.setPosition(position, QTextCursor::KeepAnchor);
					_field->setTextCursor(cursor);
				}
				mouse->accept();
				return true;
			}
			if (operation == ArticleSelectionOperation::DragSelection) {
				clearArticleDropTarget();
				mouse->accept();
				return true;
			}
		}
		return false;
	}

	if (clickLike) {
		clearArticleDropTarget();
		finishArticleSelection();
		_trackingPointerPress = false;
		return false;
	}
	updateArticleSelectionDragAtArticlePoint(articlePoint, hit, editHit);
	updateArticleSelectionAutoScroll(widgetPoint);
	if (type == QEvent::MouseButtonRelease) {
		if (operation == ArticleSelectionOperation::DragSelection) {
			if (_articleSelectionDrag.dropTarget) {
				if (_articleSelectionDrag.mode == DragSelectionMode::Structural) {
					applyStructuralSelectionDrop();
				} else if (_articleSelectionDrag.mode
					== DragSelectionMode::Text) {
					applyInlineSelectionDrop();
				}
			}
			clearArticleDropTarget();
			finishArticleSelection();
			_trackingPointerPress = false;
			mouse->accept();
			return true;
		}
		if (hasStructuralSelection()) {
			const auto committed = recordMutationTransaction([&] {
				return commitInlineField();
			});
			if (committed == ApplyResult::Failed) {
				mouse->accept();
				return true;
			}
			_pendingOrdinal = -1;
			_pendingCursorOffset = 0;
			hideInlineField();
			clearInlineFieldEditSession();
			refreshAfterInlineFieldCommit(committed);
			finishArticleSelection();
			_trackingPointerPress = false;
			mouse->accept();
			return true;
		}
		finishArticleSelection();
		_trackingPointerPress = false;
		return false;
	}
	if ((operation == ArticleSelectionOperation::DragSelection)
		|| (_articleSelectionDrag.mode == DragSelectionMode::Structural)) {
		mouse->accept();
		return true;
	}
	return false;
}

} // namespace Iv::Editor
