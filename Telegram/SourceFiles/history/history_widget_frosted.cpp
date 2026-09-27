#include "history/history_widget.h"

#include "ayu/ui/components/chat_frosted_background.h"
#include "history/history_inner_widget.h"
#include "history/view/history_view_translate_bar.h"
#include "mainwidget.h"
#include "ui/chat/floating_bar.h"
#include "ui/chat/pinned_bar.h"
#include "ui/chat/requests_bar.h"
#include "ui/painter.h"
#include "ui/widgets/elastic_scroll.h"
#include "styles/style_chat_helpers.h"
#include "styles/palette.h"

#include <QtGui/QPainterPath>
#include <algorithm>

void HistoryWidget::setupFrostedBackground() {
	_composeSurface->setObjectName(u"chatBar.compose"_q);
	_composeSurface->setMouseTracking(true);
	_composeSurface->hide();
	_frostedBackground = std::make_unique<AyuUi::ChatFrostedBackground>(
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
		},
		[=] {
			_composeSurface->update();
			for (const auto &surface : _frostedSurfaces) {
				if (surface) {
					surface->update();
				}
			}
			if (_pinnedBar) {
				_pinnedBar->updateBackground();
			}
			if (_hidingPinnedBar) {
				_hidingPinnedBar->updateBackground();
			}
			if (_translateBar) {
				_translateBar->updateBackground();
			}
			if (_requestsBar) {
				_requestsBar->updateBackground();
			}
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

void HistoryWidget::setupFrostedSurface(not_null<Ui::RpWidget*> surface) {
	const auto effect = dynamic_cast<Ui::ChatControlSurface*>(
		surface->graphicsEffect());
	Expects(effect != nullptr);
	effect->setBackgroundPainter([=](QPainter &p, QRect rect, QColor tint) {
		const auto offset = surface->mapTo(this, QPoint());
		p.save();
		p.translate(-offset);
		paintFrostedBackground(p, rect.translated(offset), tint);
		p.restore();
	});
	_frostedSurfaces.push_back(surface.get());
	surface->geometryValue() | rpl::on_next([=] {
		updateFrostedAreas();
	}, surface->lifetime());
	surface->shownValue() | rpl::on_next([=] {
		updateFrostedAreas();
	}, surface->lifetime());
}

Fn<void(QPainter&, QRect)> HistoryWidget::createFrostedBarPainter(
		Fn<QRect()> geometry) {
	return [=, geometry = std::move(geometry)](QPainter &p, QRect rect) {
		const auto area = geometry();
		p.save();
		p.translate(-area.topLeft());
		paintFrostedBackground(p, rect.translated(area.topLeft()),
			st::historyPinnedBg->c);
		p.restore();
	};
}

void HistoryWidget::updateComposeSurface(QRect capsule) {
	_composeSurfaceRect = capsule;
	// 列表保持完整；输入区自己承担圆角裁剪和点击拦截。
	_scroll->clearMask();
	if (!capsule.isEmpty()) {
		_composeSurface->setGeometry(capsule);
		const auto radius = std::min(
			qreal(st::historyComposeCapsuleRadius), capsule.height() / 2.);
		auto path = QPainterPath();
		path.addRoundedRect(QRectF(_composeSurface->rect()), radius, radius);
		const auto mask = QRegion(path.toFillPolygon().toPolygon());
		if (_composeSurface->mask() != mask) {
			_composeSurface->setMask(mask);
		}
	}
	updateComposeSurfaceVisibility();
}

void HistoryWidget::updateComposeSurfaceVisibility() {
	_composeSurface->setVisible(_list
		&& !_showAnimation
		&& !_scroll->isHidden()
		&& !_composeSurfaceRect.isEmpty()
		&& !isSearching()
		&& (fieldOrDisabledShown() || isRecording()
			|| replyTo() || readyToForward() || _kbShown || _suggestOptions));
	updateFrostedAreas();
}

void HistoryWidget::updateFrostedAreas() {
	if (!_frostedBackground) {
		return;
	}
	auto areas = std::vector<QRect>();
	_frostedSurfaces.erase(std::remove_if(
		_frostedSurfaces.begin(), _frostedSurfaces.end(),
		[](const auto &surface) { return surface.isNull(); }),
		_frostedSurfaces.end());
	for (const auto &surface : _frostedSurfaces) {
		if (surface->isVisibleTo(this)) {
			areas.emplace_back(surface->mapTo(this, QPoint()), surface->size());
		}
	}
	if (!_composeSurface->isHidden()) {
		areas.push_back(_composeSurfaceRect);
	}
	if (_pinnedBar && _pinnedBar->height()) {
		areas.push_back(_pinnedBar->backgroundRect(this));
	}
	if (_translateBar && _translateBar->height()) {
		areas.push_back(_translateBar->backgroundRect(this));
	}
	if (_requestsBar && _requestsBar->height()) {
		areas.push_back(_requestsBar->backgroundRect(this));
	}
	_frostedBackground->setAreas(std::move(areas));
}

void HistoryWidget::invalidateFrostedBackground(QRect area) {
	if (_frostedBackground) {
		_frostedBackground->invalidate(area);
	}
}

void HistoryWidget::resetFrostedBackground() {
	if (_frostedBackground) {
		_frostedBackground->clear();
	}
}

void HistoryWidget::paintFrostedBackground(QPainter &p, QRect area, QColor tint) {
	if (_frostedBackground) {
		_frostedBackground->paint(p, area, tint);
	} else {
		p.fillRect(area, tint);
	}
}
