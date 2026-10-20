#include "mainwidget.h"
#include "mainwindow.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "extras/features/window_material/window_material.h"
#include "history/history_widget.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_window.h"

#include <QtGui/QPainterPath>

namespace {

constexpr auto kOpenDuration = crl::time(300);
constexpr auto kCloseDuration = crl::time(180);

float64 Cube(float64 value) {
	return value * value * value;
}

float64 EaseOut(float64 progress) {
	return 1. - Cube(1. - progress);
}

float64 EaseBack(float64 progress) {
	const auto remaining = progress - 1.;
	return 1. + 2.3 * Cube(remaining) + 1.3 * remaining * remaining;
}

} // namespace

void MainWidget::prepareThirdSectionOpen() {
	if (_thirdSection
		|| anim::Disabled()
		|| isHidden()
		|| _showAnimation
		|| Core::App().passcodeLocked()
		|| (_mainSection && _mainSection->useFullWidth())) {
		return;
	}
	// 原生窗口加宽过程中保留聊天宽度，避免先撑大再收回。
	_thirdSectionChatWidth = _history->width();
}

bool MainWidget::animateThirdSectionClose(Fn<void()> finished) {
	if (_thirdSectionCloseFinished) {
		return true;
	}
	if (!_thirdSection
		|| anim::Disabled()
		|| isHidden()
		|| _showAnimation
		|| Core::App().passcodeLocked()
		|| (_mainSection && _mainSection->useFullWidth())) {
		return false;
	}
	_thirdSectionCloseFinished = std::move(finished);
	startThirdSectionAnimation(false);
	return true;
}

bool MainWidget::reopenThirdSection() {
	if (!_thirdSectionCloseFinished) {
		return false;
	}
	_thirdSectionCloseFinished = nullptr;
	startThirdSectionAnimation(true);
	return true;
}

void MainWidget::startThirdSectionAnimation(bool opening) {
	if (!_thirdSection || anim::Disabled()) {
		finishThirdSectionAnimation();
		updateControlsGeometry();
		return;
	}
	if (!_thirdSectionOverlay) {
		_thirdSectionAnimationGeometry = _thirdSection->geometry();
		_thirdSectionCache = Ui::GrabWidget(_thirdSection);
		_thirdSectionOverlay = base::make_unique_q<Ui::RpWidget>(this);
		_thirdSectionOverlay->setObjectName(u"third-section-animation"_q);
		_thirdSectionOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
		_thirdSectionOverlay->setGeometry(rect());
		_thirdSectionOverlay->paintRequest() | rpl::on_next([=] {
			paintThirdSectionAnimation();
		}, _thirdSectionOverlay->lifetime());
		_thirdSectionOverlay->stackUnder(_mainSection
			? static_cast<QWidget*>(_mainSection.data())
			: _history.get());
		_thirdSectionOverlay->show();
		_thirdSection->hide();
		_thirdSectionScale = opening ? .94 : 1.;
		_thirdSectionOpacity = opening ? 0. : 1.;
		_thirdSectionShift = opening ? st::windowCardFloatOpenShift : 0.;
	}
	_thirdSectionAnimation.stop();
	const auto fromWidth = _thirdSectionChatWidth.value_or(_history->width());
	const auto extendedBy = [&] {
		if (_controller->widget()->isMaximized()
			|| _controller->widget()->isFullScreen()) {
			return 0;
		}
		const auto saved = Core::App().settings().thirdSectionExtendedBy();
		return (saved == -1) ? _thirdColumnWidth : saved;
	}();
	const auto toWidth = opening
		? width() - _thirdColumnWidth - st::windowColumnGap - _history->x()
		: width() - extendedBy - _history->x();
	const auto fromScale = _thirdSectionScale;
	const auto fromOpacity = _thirdSectionOpacity;
	const auto fromShift = _thirdSectionShift;
	_thirdSectionChatWidth = fromWidth;
	_thirdSectionAnimation.start([=] {
		const auto progress = _thirdSectionAnimation.value(1.);
		const auto eased = opening ? EaseOut(progress) : Cube(progress);
		_thirdSectionChatWidth = fromWidth + (toWidth - fromWidth) * eased;
		_thirdSectionScale = fromScale + ((opening ? 1. : .96) - fromScale)
			* (opening ? EaseBack(progress) : eased);
		_thirdSectionOpacity = fromOpacity + ((opening ? 1. : 0.) - fromOpacity)
			* (opening ? EaseOut(std::min(1., progress * 1.6)) : eased);
		_thirdSectionShift = fromShift
			+ ((opening ? 0. : st::windowCardFloatCloseShift) - fromShift) * eased;
		if (!_thirdSectionAnimation.animating()) {
			finishThirdSectionAnimation();
			updateControlsGeometry();
			return;
		}
		if (_history->width() != int(base::SafeRound(*_thirdSectionChatWidth))) {
			updateControlsGeometry();
		}
		if (_thirdSectionOverlay) {
			_thirdSectionOverlay->update();
		}
	}, 0., 1., opening ? kOpenDuration : kCloseDuration);
	updateControlsGeometry();
}

void MainWidget::finishThirdSectionAnimation() {
	_thirdSectionAnimation.stop();
	_thirdSectionOverlay = nullptr;
	_thirdSectionCache = QPixmap();
	_thirdSectionChatWidth.reset();
	if (_thirdSection) {
		_thirdSection->show();
	}
	if (const auto finished = base::take(_thirdSectionCloseFinished)) {
		finished();
	}
}

void MainWidget::paintThirdSectionAnimation() {
	const auto geometry = QRectF(_thirdSectionAnimationGeometry);
	const auto center = geometry.center();
	auto p = QPainter(_thirdSectionOverlay.get());
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(_thirdSectionOpacity);
	p.translate(center + QPointF(0., _thirdSectionShift));
	p.scale(_thirdSectionScale, _thirdSectionScale);
	p.translate(-center);
	if (!ExtrasFeatures::WindowMaterial::isActive(this)) {
		// 与真实第三栏一致，只保留窗口外侧的右上圆角。
		const auto radius = st::windowCardRadius;
		auto clip = QPainterPath();
		clip.addRoundedRect(geometry, radius, radius);
		clip.addRect(geometry.adjusted(0., radius, 0., 0.));
		clip.addRect(geometry.adjusted(0., 0., -radius, 0.));
		clip.setFillRule(Qt::WindingFill);
		p.setClipPath(clip);
	}
	p.drawPixmap(geometry.topLeft(), _thirdSectionCache);
}
