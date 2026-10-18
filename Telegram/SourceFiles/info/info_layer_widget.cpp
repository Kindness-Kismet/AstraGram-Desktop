/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/info_layer_widget.h"

#include "info/info_content_widget.h"
#include "info/info_controller.h"
#include "info/info_top_bar.h"
#include "info/info_memento.h"
#include "info/media/info_media_widget.h"
#include "ui/rp_widget.h"
#include "ui/focus_persister.h"
#include "ui/widgets/buttons.h"
#include "ui/cached_round_corners.h"
#include "window/section_widget.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/main_window.h"
#include "main/main_session.h"
#include "core/application.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"

namespace Info {

LayerWidget::LayerWidget(
	not_null<Window::SessionController*> controller,
	not_null<Memento*> memento)
: _controller(controller)
, _contentWrap(this, controller, Wrap::Layer, memento) {
	controller->registerActiveLayerSection(_contentWrap.data());
	setupHeightConsumers();
	controller->window().replaceFloatPlayerDelegate(floatPlayerDelegate());
}

LayerWidget::LayerWidget(
	not_null<Window::SessionController*> controller,
	not_null<MoveMemento*> memento)
: _controller(controller)
, _contentWrap(memento->takeContent(this, Wrap::Layer)) {
	controller->registerActiveLayerSection(_contentWrap.data());
	setupHeightConsumers();
	controller->window().replaceFloatPlayerDelegate(floatPlayerDelegate());
}

auto LayerWidget::floatPlayerDelegate()
-> not_null<::Media::Player::FloatDelegate*> {
	return static_cast<::Media::Player::FloatDelegate*>(this);
}

not_null<Ui::RpWidget*> LayerWidget::floatPlayerWidget() {
	return this;
}

void LayerWidget::floatPlayerToggleGifsPaused(bool paused) {
	constexpr auto kReason = Window::GifPauseReason::RoundPlaying;
	if (paused) {
		_controller->enableGifPauseReason(kReason);
	} else {
		_controller->disableGifPauseReason(kReason);
	}
}

auto LayerWidget::floatPlayerGetSection(Window::Column column)
-> not_null<::Media::Player::FloatSectionDelegate*> {
	Expects(_contentWrap != nullptr);

	return _contentWrap;
}

void LayerWidget::floatPlayerEnumerateSections(Fn<void(
		not_null<::Media::Player::FloatSectionDelegate*> widget,
		Window::Column widgetColumn)> callback) {
	Expects(_contentWrap != nullptr);

	callback(_contentWrap, Window::Column::Second);
}

bool LayerWidget::floatPlayerIsVisible(not_null<HistoryItem*> item) {
	return false;
}

void LayerWidget::floatPlayerDoubleClickEvent(
		not_null<const HistoryItem*> item) {
	_controller->showMessage(item);
}

void LayerWidget::setupHeightConsumers() {
	Expects(_contentWrap != nullptr);
	setObjectName(u"info.layer"_q);

	_contentWrap->scrollTillBottomChanges(
	) | rpl::filter([this] {
		if (!_inResize) {
			return true;
		}
		_pendingResize = true;
		return false;
	}) | rpl::on_next([this] {
		resizeToWidth(width());
	}, lifetime());

	// 宽高共用一次过渡，避免同一帧重复排版。
	_contentWrap->contentChanged(
	) | rpl::filter([=] {
		return parentWidget() != nullptr;
	}) | rpl::on_next([=] {
		_changingContent = false;
		animateGeometry();
	}, lifetime());

	// 截取新页面时临时撑到目标尺寸，截完恢复过渡中的尺寸。
	_contentWrap->grabbingForExpanding(
	) | rpl::on_next([=](bool grabbing) {
		if (grabbing) {
			_savedWidth = width();
			_savedHeight = _contentWrapHeight;
			_savedGeometryAnimation = base::take(_geometryAnimation);
			_preparingGeometry = true;
			_contentWrapHeight = std::min(_desiredHeight, maxContentHeight());
			resizeToWidth(targetWidth());
		} else {
			_restoringGeometry = true;
			_contentWrapHeight = _savedHeight;
			resizeToWidth(_savedWidth);
			_restoringGeometry = false;
			_preparingGeometry = false;
			_geometryAnimation = base::take(_savedGeometryAnimation);
		}
	}, lifetime());

	_contentWrap->desiredHeightValue(
	) | rpl::on_next([this](int height) {
		if (!height) {
			_changingContent = true;
			return;
		}
		if (_restoringGeometry
			|| (_inResize && _geometryAnimation.animating())) {
			return;
		}
		if (_desiredHeight == height) {
			return;
		}
		_desiredHeight = height;
		if (_preparingGeometry || _inResize) {
			setContentHeight(std::min(height, maxContentHeight()));
			return;
		}
		if (_changingContent) {
			return;
		}
		if (!parentWidget()) {
			setContentHeight(height);
			return;
		}
		animateGeometry();
	}, lifetime());
	_changingContent = false;
}

void LayerWidget::setContentHeight(int height) {
	if (_contentWrapHeight == height) {
		return;
	}
	_contentWrapHeight = height;
	if (_inResize) {
		_pendingResize = true;
	} else if (_contentWrap) {
		resizeToWidth(width());
	}
}

void LayerWidget::showFinished() {
	if (!_contentWrap) {
		// parentResized() may have moved the content out into a
		// MoveMemento and only queued hideSpecialLayer(), so we stay
		// alive with no content for at least one event loop turn.
		return;
	}
	floatPlayerShowVisible();
	_contentWrap->showFast();
}

void LayerWidget::parentResized() {
	if (!_contentWrap) {
		return;
	}

	auto parentSize = parentWidget()->size();
	auto parentWidth = parentSize.width();
	const auto settings = _contentWrap->controller()->section().type()
		== Section::Type::Settings;
	if (parentWidth < MinimalSupportedWidth() && !settings) {
		Ui::FocusPersister persister(this);
		restoreFloatPlayerDelegate();
		unregisterActiveLayerSection();

		auto memento = std::make_shared<MoveMemento>(std::move(_contentWrap));

		// We want to call hideSpecialLayer synchronously to avoid glitches,
		// but we can't destroy LayerStackWidget from its' resizeEvent,
		// because QWidget has such code for resizing:
		//
		// QResizeEvent e(r.size(), olds);
		// QApplication::sendEvent(q, &e);
		// if (q->windowHandle())
		//   q->update();
		//
		// So we call it queued. It would be cool to call it 'right after'
		// the resize event handling was finished.
		InvokeQueued(this, [=] {
			_controller->hideSpecialLayer(anim::type::instant);
		});
		_controller->showSection(
			std::move(memento),
			Window::SectionShow(
				Window::SectionShow::Way::Forward,
				anim::type::instant,
				anim::activation::background));
	//
	// There was a layout logic which caused layer info to become a
	// third column info if the window size allows, but it was decided
	// to keep layer info and third column info separated.
	//
	//} else if (_controller->canShowThirdSectionWithoutResize()) {
	//	takeToThirdSection();
	} else {
		_geometryAnimation.stop();
		_contentWrapHeight = std::min(_desiredHeight, maxContentHeight());
		resizeToWidth(targetWidth());
	}
}

int LayerWidget::desiredWidth() const {
	return Media::UsesGalleryLayout(_contentWrap->controller())
		? st::infoMediaLayerWidth
		: st::infoDesiredWidth;
}

int LayerWidget::targetWidth() const {
	return std::min(
		parentWidget()->width() - 2 * st::infoMinimalLayerMargin,
		desiredWidth());
}

int LayerWidget::maxContentHeight() const {
	const auto height = parentWidget()->height();
	return height - 2 * ((height + 5) / 10) - st::boxRadius;
}

void LayerWidget::animateGeometry() {
	if (_preparingGeometry || _restoringGeometry) {
		return;
	}
	const auto toWidth = targetWidth();
	const auto toHeight = std::min(_desiredHeight, maxContentHeight());
	if (_geometryAnimation.animating()
		&& _geometryTo == QSize(toWidth, toHeight)) {
		return;
	}
	const auto from = QSize(width(), std::min(
		_contentWrapHeight,
		maxContentHeight()));
	_geometryAnimation.stop();
	_preparingGeometry = true;
	_contentWrapHeight = toHeight;
	resizeToWidth(toWidth);
	const auto to = QSize(toWidth, std::min(
		_desiredHeight,
		maxContentHeight()));
	if (!from.width() || isHidden() || from == to) {
		_preparingGeometry = false;
		return;
	}
	_restoringGeometry = true;
	_contentWrapHeight = from.height();
	resizeToWidth(from.width());
	_restoringGeometry = false;
	_preparingGeometry = false;
	_geometryFrom = from;
	_geometryTo = to;
	_geometryAnimation.start([=](float64 progress) {
		_contentWrapHeight = anim::interpolate(
			_geometryFrom.height(), _geometryTo.height(), progress);
		resizeToWidth(anim::interpolate(
			_geometryFrom.width(), _geometryTo.width(), progress));
	}, 0., 1., st::infoLayerMorphDuration, anim::easeOutCubic);
	resizeToWidth(from.width());
}

bool LayerWidget::takeToThirdSection() {
	return false;
	//
	// There was a layout logic which caused layer info to become a
	// third column info if the window size allows, but it was decided
	// to keep layer info and third column info separated.
	//
	//Ui::FocusPersister persister(this);
	//auto localCopy = _controller;
	//auto memento = MoveMemento(std::move(_contentWrap));
	//localCopy->hideSpecialLayer(anim::type::instant);

	//// When creating third section in response to the window
	//// size allowing it to fit without window resize we want
	//// to save that we didn't extend the window while showing
	//// the third section, so that when we close it we won't
	//// shrink the window size.
	////
	//// See https://github.com/telegramdesktop/tdesktop/issues/4091
	//localCopy->session()().settings().setThirdSectionExtendedBy(0);

	//localCopy->session()().settings().setThirdSectionInfoEnabled(true);
	//localCopy->session()().saveSettingsDelayed();
	//localCopy->showSection(
	//	std::move(memento),
	//	Window::SectionShow(
	//		Window::SectionShow::Way::ClearStack,
	//		anim::type::instant,
	//		anim::activation::background));
	//return true;
}

bool LayerWidget::showSectionInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (_contentWrap && _contentWrap->showInternal(memento, params)) {
		if (params.activation != anim::activation::background) {
			_controller->parentController()->hideLayer();
		}
		return true;
	}
	return false;
}

bool LayerWidget::closeByOutsideClick() const {
	return _contentWrap ? _contentWrap->closeByOutsideClick() : true;
}

bool LayerWidget::closeByBackButton() {
	return _contentWrap
		? _contentWrap->closeByBackButton()
		: Ui::LayerWidget::closeByBackButton();
}

int LayerWidget::MinimalSupportedWidth() {
	const auto minimalMargins = 2 * st::infoMinimalLayerMargin;
	return st::infoMinimalWidth + minimalMargins;
}

int LayerWidget::resizeGetHeight(int newWidth) {
	if (!parentWidget() || !_contentWrap || !newWidth) {
		return 0;
	}
	constexpr auto kMaxAttempts = 16;
	auto attempts = 0;
	while (true) {
		_inResize = true;
		const auto newGeometry = countGeometry(newWidth);
		_inResize = false;
		if (!_pendingResize) {
			const auto oldGeometry = geometry();
			if (newGeometry != oldGeometry) {
				_contentWrap->forceContentRepaint();
			}
			if (newGeometry.topLeft() != oldGeometry.topLeft()) {
				move(newGeometry.topLeft());
			}
			floatPlayerUpdatePositions();
			return newGeometry.height();
		}
		_pendingResize = false;
		Assert(attempts++ < kMaxAttempts);
	}
}

QRect LayerWidget::countGeometry(int newWidth) {
	const auto &parentSize = parentWidget()->size();
	const auto windowWidth = parentSize.width();
	const auto windowHeight = parentSize.height();
	const auto newLeft = (windowWidth - newWidth) / 2;
	// 内容不足时收缩居中，长内容滚动不能占用上下各 10% 的留白。
	const auto margin = (windowHeight + 5) / 10;
	const auto maxHeight = windowHeight - 2 * margin;
	const auto panelHeight = std::min(
		_contentWrapHeight + st::boxRadius,
		maxHeight);
	const auto newTop = (windowHeight - panelHeight) / 2;
	const auto expanding = _geometryAnimation.animating()
		|| (std::min(_desiredHeight + st::boxRadius, maxHeight)
			> panelHeight);
	_contentWrap->updateGeometry(
		{ 0, 0, newWidth, panelHeight - st::boxRadius },
		expanding,
		false,
		0,
		maxHeight);
	return QRect(newLeft, newTop, newWidth, panelHeight);
}

void LayerWidget::doSetInnerFocus() {
	if (_contentWrap) {
		_contentWrap->setInnerFocus();
	}
}

void LayerWidget::paintEvent(QPaintEvent *e) {
	if (!_contentWrap) {
		// parentResized() may have moved the content out into a MoveMemento
		// and only queued hideSpecialLayer(), and LayerStackWidget renders
		// us synchronously through Ui::Shadow::grab() during transitions.
		return;
	}
	auto p = QPainter(this);

	const auto clip = e->rect();
	const auto radius = st::boxRadius;
	const auto &corners = Ui::CachedCornerPixmaps(Ui::BoxCorners);
	const auto bottom = QRect{ 0, height() - radius, width(), radius };
	if (clip.intersects(bottom)) {
		if (const auto rounding = _contentWrap->bottomSkipRounding()) {
			rounding->paint(p, rect(), RectPart::FullBottom);
		} else {
			Ui::FillRoundRect(p, bottom, st::boxBg, {
				.p = { QPixmap(), QPixmap(), corners.p[2], corners.p[3] }
			});
		}
	}
	if (_contentWrap->animatingShow()) {
		const auto top = QRect{ 0, 0, width(), radius };
		if (clip.intersects(top)) {
			Ui::FillRoundRect(p, top, st::boxBg, {
				.p = { corners.p[0], corners.p[1], QPixmap(), QPixmap() }
			});
		}
		p.fillRect(0, radius, width(), height() - 2 * radius, st::boxBg);
	}
}

void LayerWidget::restoreFloatPlayerDelegate() {
	if (!_floatPlayerDelegateRestored) {
		_floatPlayerDelegateRestored = true;
		_controller->window().restoreFloatPlayerDelegate(
			floatPlayerDelegate());
	}
}

void LayerWidget::unregisterActiveLayerSection() {
	if (_contentWrap) {
		_controller->unregisterActiveLayerSection(_contentWrap.data());
	}
}

void LayerWidget::closeHook() {
	unregisterActiveLayerSection();
	restoreFloatPlayerDelegate();
}

LayerWidget::~LayerWidget() {
	unregisterActiveLayerSection();
	if (!Core::Quitting()) {
		restoreFloatPlayerDelegate();
	}
}

} // namespace Info
