#include "history/history_widget.h"

#include "ayu/ayu_settings.h"
#include "ayu/ui/components/floating_surface_host.h"
#include "history/history_inner_widget.h"
#include "mainwidget.h"
#include "ui/chat/floating_bar.h"
#include "ui/painter.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/fields/input_field.h"
#include "styles/style_chat_helpers.h"
#include "styles/palette.h"

void HistoryWidget::setupFrostedBackground() {
	_composeSurface->setObjectName(u"chatBar.compose"_q);
	_composeSurface->setMouseTracking(true);
	_composeSurface->hide();
	AyuUi::FloatingSurface::attach(_composeSurface.data(), {
		.radius = st::historyComposeCapsuleRadius,
		.background = [] {
			return (AyuSettings::getInstance().disableChatBackground()
				? st::windowBgOver : st::historyComposeAreaBg)->c;
		},
		.border = [] {
			return (AyuSettings::getInstance().disableChatBackground()
				? st::filterInputBorderFg : st::windowDividerFg)->c;
		},
		.borderWidth = st::lineWidth,
		.maskInput = true,
	});
	_floatingSurfaceHost = std::make_unique<AyuUi::FloatingSurfaceHost>(
		this,
		[=](Painter &p, QRect area) {
			if (!_list || _scroll->isHidden() || _firstLoadRequest
				|| _showAnimation || hasPendingResizedItems()) {
				return false;
			}
			const auto content = controller()->content();
			const auto fromY = content->backgroundFromY();
			p.save();
			p.translate(0, fromY);
			Window::SectionWidget::PaintBackground(
				p,
				controller()->currentChatTheme(),
				QSize(width(), content->height()),
				area.translated(0, -fromY),
				controller()->isGifPausedAtLeastFor(Window::GifPauseReason::Any));
			p.restore();

			const auto position = _list->mapTo(this, QPoint());
			const auto clip = area.intersected(_scroll->geometry());
			if (clip.isEmpty()) {
				return true;
			}
			p.translate(position);
			return _list->paintBackdrop(p, clip.translated(-position));
		});
	_composeSurface->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		auto p = Painter(_composeSurface.data());
		p.translate(-_composeSurface->pos());
		drawField(p, clip.translated(_composeSurface->pos()));
	}, _composeSurface->lifetime());
	shownValue() | rpl::on_next([=](bool shown) {
		if (shown) {
			invalidateFrostedBackground();
		}
	}, lifetime());
}

void HistoryWidget::updateComposeSurface() {
	const auto header = _editMsgId || _replyTo || readyToForward()
		|| _kbReplyTo || _previewDrawPreview || _suggestOptions;
	const auto headerHeight = header ? st::historyReplyHeight : 0;
	const auto margin = st::historyComposeCapsuleMargin;
	// 使用输入控件的实际位置，底部标签栏和只显示回复条时也走同一表面。
	_composeSurface->setGeometry(myrtlrect(
		margin,
		_field->y() - st::historySendPadding - headerHeight,
		width() - 2 * margin,
		fieldHeight() + 2 * st::historySendPadding + headerHeight));
}

void HistoryWidget::updateComposeSurfaceVisibility() {
	const auto visible = _list
		&& !_showAnimation
		&& !_scroll->isHidden()
		&& !isSearching()
		&& (fieldOrDisabledShown() || isRecording()
			|| replyTo() || readyToForward() || _kbShown || _suggestOptions);
	if (visible) {
		updateComposeSurface();
	}
	_composeSurface->setVisible(visible);
}

void HistoryWidget::invalidateFrostedBackground(QRect area) {
	if (_floatingSurfaceHost) {
		_floatingSurfaceHost->invalidate(area);
	}
}

void HistoryWidget::resetFrostedBackground() {
	if (_floatingSurfaceHost) {
		_floatingSurfaceHost->clear();
	}
}
