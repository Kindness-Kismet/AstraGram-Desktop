/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_overlay_rhi_internal.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0) && !defined DESKTOP_APP_DISABLE_PRIVATE_QT_API

#include "ui/rhi/rhi_shader.h"
#include "ui/painter.h"
#include "data/data_peer_values.h"
#include "media/streaming/media_streaming_common.h"
#include "media/view/media_view_video_stream.h"
#include "platform/platform_overlay_widget.h"
#include "base/debug_log.h"
#include "styles/style_media_view.h"
#include "styles/style_extras_styles.h"

#include <rhi/qrhi.h>

namespace Media::View {

using namespace RhiDetails;
namespace {

using namespace Ui::GL;

[[nodiscard]] QRectF StoryCropTextureRect(
		QSizeF imageSize,
		QSizeF targetSize) {
	if (imageSize.isEmpty() || targetSize.isEmpty()) {
		return QRectF(0., 0., 1., 1.);
	}
	const auto targetAspect = targetSize.width() / targetSize.height();
	const auto imageAspect = imageSize.width() / imageSize.height();
	if (imageAspect > targetAspect) {
		const auto cropW = imageSize.height() * targetAspect;
		const auto offset = (imageSize.width() - cropW) / 2.;
		return QRectF(
			offset / imageSize.width(),
			0.,
			cropW / imageSize.width(),
			1.);
	} else if (imageAspect < targetAspect) {
		const auto cropH = imageSize.width() / targetAspect;
		const auto offset = (imageSize.height() - cropH) / 2.;
		return QRectF(
			0.,
			offset / imageSize.height(),
			1.,
			cropH / imageSize.height());
	}
	return QRectF(0., 0., 1., 1.);
}



// Vulkan is the only backend with the NDC Y axis pointing down.
[[nodiscard]] float NdcFlipY(not_null<QRhi*> rhi) {
	return rhi->isYUpInNDC() ? 1.f : -1.f;
}

// OpenGL is the only backend with gl_FragCoord.y counting from the bottom.
[[nodiscard]] float FragCoordYUp(not_null<QRhi*> rhi) {
	return rhi->isYUpInFramebuffer() ? 1.f : 0.f;
}

} // namespace

OverlayWidget::RendererRhi::~RendererRhi() {
	releaseResources();
}

OverlayWidget::RendererRhi::RendererRhi(not_null<OverlayWidget*> owner)
: _owner(owner) {
	style::PaletteChanged(
	) | rpl::on_next([=] {
		ranges::fill(_cacheKeys, quint64(0));
		invalidateControls();
	}, _lifetime);

	crl::on_main(this, [=] {
		_owner->_storiesChanged.events(
		) | rpl::on_next([=] {
			if (_owner->_storiesSession) {
				Data::AmPremiumValue(
					_owner->_storiesSession
				) | rpl::on_next([=] {
					ranges::fill(_cacheKeys, quint64(0));
					invalidateControls();
				}, _storiesLifetime);
			} else {
				_storiesLifetime.destroy();
				ranges::fill(_cacheKeys, quint64(0));
				invalidateControls();
			}
		}, _lifetime);
	});
}

void OverlayWidget::RendererRhi::initialize(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) {
	if (_initialized && _rhi == rhi) {
		return;
	}
	releaseResources();
	const auto guard = gsl::finally([&] {
		if (!_initialized) {
			releaseResources();
		}
	});

	_rhi = rhi;
	_rt = rt;
	_cb = cb;

	_vertexBuffer = rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::VertexBuffer,
		kMaxDraws * kVertexSize);
	if (!_vertexBuffer->create()) {
		return;
	}

	_fillVertexBuffer = rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::VertexBuffer,
		4 * 4 * 2 * sizeof(float));
	if (!_fillVertexBuffer->create()) {
		return;
	}

	_uniformBuffer = rhi->newBuffer(
		QRhiBuffer::Dynamic,
		QRhiBuffer::UniformBuffer,
		kMaxDraws * 256);
	if (!_uniformBuffer->create()) {
		return;
	}

	_sampler = rhi->newSampler(
		QRhiSampler::Linear,
		QRhiSampler::Linear,
		QRhiSampler::None,
		QRhiSampler::ClampToEdge,
		QRhiSampler::ClampToEdge);
	if (!_sampler->create()) {
		return;
	}

	_placeholderTexture = rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1));
	if (!_placeholderTexture->create()) {
		return;
	}

	if (!createPipelines()) {
		return;
	}
	_initialized = true;

	LOG(("[RENDERER_TEST] component=overlay backend=%1 device=%2 status=OK")
		.arg(rhi->backendName())
		.arg(rhi->driverInfo().deviceName));
}



void OverlayWidget::RendererRhi::render(
		QRhi *rhi,
		QRhiRenderTarget *rt,
		QRhiCommandBuffer *cb) {
	if (!rhi || rhi->isDeviceLost() || !rt || !cb) {
		return;
	}
	initialize(rhi, rt, cb);
	if (!_initialized) {
		return;
	}
	const auto guard = gsl::finally([&] {
		if (!_initialized && _rub) {
			base::take(_rub)->release();
		}
	});
	if (_owner->_hideWorkaround) {
		cb->beginPass(rt, QColor(0, 0, 0, 0), { 1.0f, 0 });
		cb->endPass();
		return;
	}
	_rhi = rhi;
	_rt = rt;
	_cb = cb;
	_nextVertexSlot = 0;
	_nextPoolIndex = 0;
	for (auto *srb : _perDrawSrbs) {
		delete srb;
	}
	_perDrawSrbs.clear();
	_drawCommands.clear();

	const auto size = rt->pixelSize();
	const auto factor = _owner->widget()->devicePixelRatioF();
	if (_factor != factor) {
		_factor = factor;
		_ifactor = int(std::ceil(factor));
		ranges::fill(_cacheKeys, quint64(0));
		invalidateControls();
		delete _controlsFadeTexture;
		_controlsFadeTexture = nullptr;
	}
	_viewport = QSize(
		int(size.width() / _factor),
		int(size.height() / _factor));

	_rub = rhi->nextResourceUpdateBatch();
	_pendingVideoStream = nullptr;

	_owner->paint(this);
	if (!_initialized) {
		return;
	}

	if (_pendingVideoStream) {
		_pendingVideoStream->borrowedPaintOffscreen(_rhi, _rt, _cb);
	}

	if (const auto notch = _owner->topNotchSkip()) {
		auto blackImage = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
		blackImage.fill(Qt::black);
		auto *blackTex = acquirePoolTexture(QSize(1, 1));
		if (!blackTex) {
			return;
		}
		_rub->uploadTexture(
			blackTex,
			QRhiTextureUploadDescription(
				QRhiTextureUploadEntry(0, 0,
					QRhiTextureSubresourceUploadDescription(blackImage))));
		const auto notchRect = transformRect(
			QRect(0, 0, _owner->width(), notch));
		const float notchCoords[] = {
			notchRect.left(), notchRect.bottom(), 0.f, 0.f,
			notchRect.right(), notchRect.bottom(), 1.f, 0.f,
			notchRect.left(), notchRect.top(), 0.f, 1.f,
			notchRect.right(), notchRect.top(), 1.f, 1.f,
		};
		drawTexturedQuad(_imagePipeline, blackTex, notchCoords);
	}

	cb->beginPass(rt, QColor(0, 0, 0, 0), { 1.0f, 0 }, _rub);
	_rub = nullptr;

	for (const auto &cmd : _drawCommands) {
		cb->setGraphicsPipeline(cmd.pipeline);
		cb->setShaderResources(cmd.srb);
		cb->setViewport({
			0, 0,
			float(rt->pixelSize().width()),
			float(rt->pixelSize().height()) });
		if (cmd.fillVertex) {
			const QRhiCommandBuffer::VertexInput vbufBinding(
				_fillVertexBuffer,
				cmd.vertexIndex * 4 * 2 * sizeof(float));
			cb->setVertexInput(0, 1, &vbufBinding);
		} else {
			const QRhiCommandBuffer::VertexInput vbufBinding(
				_vertexBuffer, cmd.vertexIndex * kVertexSize);
			cb->setVertexInput(0, 1, &vbufBinding);
		}
		cb->draw(4);
	}
	_drawCommands.clear();

	if (_pendingVideoStream) {
		_pendingVideoStream->borrowedPaintOnscreen(_rhi, _rt, _cb);
		_pendingVideoStream = nullptr;
	}

	cb->endPass();
}

QRhiTexture *OverlayWidget::RendererRhi::acquirePoolTexture(QSize size) {
	if (_nextPoolIndex < int(_texturePool.size())) {
		auto &entry = _texturePool[_nextPoolIndex++];
		if (entry.size == size) {
			return entry.texture;
		}
		delete entry.texture;
		entry.texture = _rhi->newTexture(QRhiTexture::BGRA8, size);
		if (!entry.texture->create()) {
			_initialized = false;
			return nullptr;
		}
		entry.size = size;
		return entry.texture;
	}
	auto *tex = _rhi->newTexture(QRhiTexture::BGRA8, size);
	if (!tex->create()) {
		delete tex;
		_initialized = false;
		return nullptr;
	}
	_texturePool.push_back({ tex, size });
	_nextPoolIndex++;
	return tex;
}

void OverlayWidget::RendererRhi::releaseResources() {
	if (const auto rub = base::take(_rub)) {
		rub->release();
	}
	_drawCommands.clear();

	for (auto &entry : _texturePool) {
		delete entry.texture;
	}
	_texturePool.clear();

	delete _imagePipeline;
	_imagePipeline = nullptr;
	delete _imageBlendPipeline;
	_imageBlendPipeline = nullptr;
	delete _imageAddPipeline;
	_imageAddPipeline = nullptr;
	delete _staticContentPipeline;
	_staticContentPipeline = nullptr;
	delete _staticContentBlendPipeline;
	_staticContentBlendPipeline = nullptr;
	delete _transparentContentPipeline;
	_transparentContentPipeline = nullptr;
	delete _roundedCornersPipeline;
	_roundedCornersPipeline = nullptr;
	delete _yuv420Pipeline;
	_yuv420Pipeline = nullptr;
	delete _yuv420BlendPipeline;
	_yuv420BlendPipeline = nullptr;
	delete _nv12Pipeline;
	_nv12Pipeline = nullptr;
	delete _nv12BlendPipeline;
	_nv12BlendPipeline = nullptr;

	for (auto &tex : _storiesSiblingTextures) {
		delete tex;
		tex = nullptr;
	}
	ranges::fill(_storiesSiblingSizes, QSize());
	ranges::fill(_storiesSiblingCacheKeys, quint64(0));

	delete _controlsAtlasTexture;
	_controlsAtlasTexture = nullptr;
	_controlsAtlasSize = QSize();
	ranges::fill(_controlsTextures, QRect());

	delete _controlsFadeTexture;
	_controlsFadeTexture = nullptr;
	_controlsFadeSize = QSize();
	delete _backdropTexture;
	_backdropTexture = nullptr;
	_backdropSize = QSize();
	_backdropKey = 0;

	for (auto *srb : _perDrawSrbs) {
		delete srb;
	}
	_perDrawSrbs.clear();

	for (auto &tex : _rgbaTextures) {
		delete tex;
		tex = nullptr;
	}
	ranges::fill(_rgbaSizes, QSize());
	ranges::fill(_cacheKeys, quint64(0));

	delete _yTexture;
	_yTexture = nullptr;
	delete _uTexture;
	_uTexture = nullptr;
	delete _vTexture;
	_vTexture = nullptr;
	delete _uvTexture;
	_uvTexture = nullptr;
	_lumaSize = QSize();
	_chromaSize = QSize();
	_chromaNV12 = false;
	_usingExternalVideoTextures = false;
	_trackFrameIndex = -1;
	_streamedIndex = 0;
#ifdef Q_OS_MAC
	_metalTextureCache.flush();
#endif // Q_OS_MAC

	delete _placeholderTexture;
	_placeholderTexture = nullptr;

	delete _vertexBuffer;
	_vertexBuffer = nullptr;
	delete _fillVertexBuffer;
	_fillVertexBuffer = nullptr;
	delete _uniformBuffer;
	_uniformBuffer = nullptr;
	delete _sampler;
	_sampler = nullptr;

	_initialized = false;
}

QColor OverlayWidget::RendererRhi::rhiClearColor() {
	if (_owner->_hideWorkaround) {
		return QColor(0, 0, 0, 0);
	} else if (_owner->_fullScreenVideo) {
		return st::mediaviewVideoBg->c;
	} else {
		return st::mediaviewBg->c;
	}
}

std::optional<QColor> OverlayWidget::RendererRhi::clearColor() {
	if (_owner->_hideWorkaround) {
		return QColor(0, 0, 0, 0);
	} else if (_owner->_fullScreenVideo) {
		return st::mediaviewVideoBg->c;
	} else {
		return st::mediaviewBg->c;
	}
}

void OverlayWidget::RendererRhi::drawTexturedQuad(
		QRhiGraphicsPipeline *pipeline,
		QRhiTexture *texture,
		const float *coords,
		float opacity,
		bool blend) {
	if (!_initialized) {
		return;
	}
	if (_nextVertexSlot >= kMaxDraws || !texture) {
		return;
	}
	const auto slot = _nextVertexSlot++;
	const auto vOffset = slot * kVertexSize;
	const auto uOffset = slot * 256;

	_rub->updateDynamicBuffer(
		_vertexBuffer, vOffset, kVertexSize, coords);

	ImageUniforms uniforms{};
	uniforms.viewport[0] = _viewport.width() * _factor;
	uniforms.viewport[1] = _viewport.height() * _factor;
	uniforms.g_opacity = opacity;
	uniforms.flipY = NdcFlipY(_rhi);
	_rub->updateDynamicBuffer(
		_uniformBuffer, uOffset, sizeof(ImageUniforms), &uniforms);

	auto *srb = _rhi->newShaderResourceBindings();
	srb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			uOffset,
			sizeof(ImageUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			texture,
			_sampler),
	});
	if (!srb->create()) {
		delete srb;
		_initialized = false;
		return;
	}
	_perDrawSrbs.push_back(srb);

	_drawCommands.push_back({
		.pipeline = blend ? _imageBlendPipeline : pipeline,
		.srb = srb,
		.vertexIndex = slot,
	});
}

void OverlayWidget::RendererRhi::paintUsingRaster(
		QRect rect,
		Fn<void(Painter&)> method,
		bool transparent,
		float opacity) {
	if (!_initialized) {
		return;
	}
	if (!_imagePipeline || rect.isEmpty()) {
		return;
	}
	const auto size = rect.size() * _ifactor;
	auto raster = QImage(size, QImage::Format_ARGB32_Premultiplied);
	raster.setDevicePixelRatio(_factor);
	raster.fill(Qt::transparent);
	{
		auto painter = Painter(&raster);
		method(painter);
	}

	auto *tex = acquirePoolTexture(size);
	if (!tex) {
		return;
	}
	_rub->uploadTexture(
		tex,
		QRhiTextureUploadDescription(
			QRhiTextureUploadEntry(0, 0,
				QRhiTextureSubresourceUploadDescription(raster))));

	const auto rRect = transformRect(rect);
	const float coords[] = {
		rRect.left(), rRect.bottom(),
		0.f, 0.f,

		rRect.right(), rRect.bottom(),
		1.f, 0.f,

		rRect.left(), rRect.top(),
		0.f, 1.f,

		rRect.right(), rRect.top(),
		1.f, 1.f,
	};

	drawTexturedQuad(
		_imagePipeline,
		tex,
		coords,
		opacity,
		transparent);
}

void OverlayWidget::RendererRhi::validateControlsFade() {
	const auto forStories = (_owner->_stories != nullptr);
	const auto flip = !forStories && !_owner->topShadowOnTheRight();
	if (_controlsFadeTexture
		&& _shadowTopFlip == flip
		&& _shadowsForStories == forStories) {
		return;
	}
	_shadowTopFlip = flip;
	_shadowsForStories = forStories;
	const auto &top = _shadowsForStories
		? st::storiesShadowTop
		: st::mediaviewShadowTop;
	const auto &bottom = _shadowsForStories
		? st::storiesShadowBottom
		: st::mediaviewShadowBottom;
	const auto width = top.width();
	const auto bottomTop = top.height();
	const auto height = bottomTop + bottom.height();

	auto image = QImage(
		QSize(width, height) * _ifactor,
		QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	image.setDevicePixelRatio(_ifactor);
	{
		auto p = QPainter(&image);
		top.paint(p, 0, 0, width);
		bottom.fill(p, QRect(0, bottomTop, width, bottom.height()));
	}
	if (flip) {
		image = std::move(image).mirrored(true, false);
	}

	const auto size = image.size();
	if (!_controlsFadeTexture || _controlsFadeSize != size) {
		delete _controlsFadeTexture;
		_controlsFadeTexture = _rhi->newTexture(
			QRhiTexture::BGRA8,
			size);
		if (!_controlsFadeTexture->create()) {
			_initialized = false;
			return;
		}
		_controlsFadeSize = size;
	}
	_rub->uploadTexture(
		_controlsFadeTexture,
		QRhiTextureUploadDescription(
			QRhiTextureUploadEntry(0, 0,
				QRhiTextureSubresourceUploadDescription(image))));
}

void OverlayWidget::RendererRhi::fillShadowUniforms(
		float *shadowTopRect,
		float *shadowBottomSkipOpacityFullFade,
		ContentGeometry geometry) const {
	if (_owner->_stories) {
		const auto &top = st::storiesShadowTop.size();
		const auto shadowTop = geometry.topShadowShown
			? geometry.rect.y()
			: geometry.rect.y() - top.height();
		const auto tRect = transformRect(
			QRect(QPoint(geometry.rect.x(), shadowTop), top));
		shadowTopRect[0] = tRect.x();
		shadowTopRect[1] = tRect.y();
		shadowTopRect[2] = tRect.width();
		shadowTopRect[3] = tRect.height();
	} else {
		const auto &top = st::mediaviewShadowTop.size();
		const auto point = QPoint(
			_shadowTopFlip ? 0 : (_owner->width() - top.width()),
			0);
		const auto tRect = transformRect(QRect(point, top));
		shadowTopRect[0] = tRect.x();
		shadowTopRect[1] = tRect.y();
		shadowTopRect[2] = tRect.width();
		shadowTopRect[3] = tRect.height();
	}
	const auto &bottom = _owner->_stories
		? st::storiesShadowBottom
		: st::mediaviewShadowBottom;
	shadowBottomSkipOpacityFullFade[0] = bottom.height() * _factor;
	shadowBottomSkipOpacityFullFade[1] =
		geometry.bottomShadowSkip * _factor;
	shadowBottomSkipOpacityFullFade[2] = geometry.controlsOpacity;
	shadowBottomSkipOpacityFullFade[3] = 1.f - float(geometry.fade);
}

void OverlayWidget::RendererRhi::drawContentQuad(
		QRhiTexture *contentTexture,
		const float *coords,
		ContentGeometry geometry,
		bool fillTransparentBackground,
		bool blend) {
	if (!_initialized) {
		return;
	}
	if (_nextVertexSlot >= kMaxDraws || !contentTexture) {
		return;
	}

	validateControlsFade();
	if (!_initialized) {
		return;
	}

	const auto slot = _nextVertexSlot++;
	const auto vOffset = slot * kVertexSize;
	const auto uOffset = slot * 256;

	_rub->updateDynamicBuffer(
		_vertexBuffer, vOffset, kVertexSize, coords);

	const auto vw = _viewport.width() * _factor;
	const auto vh = _viewport.height() * _factor;

	auto *pipeline = fillTransparentBackground
		? _transparentContentPipeline
		: (blend ? _staticContentBlendPipeline : _staticContentPipeline);

	if (fillTransparentBackground) {
		const auto c_bg = st::mediaviewTransparentBg->c;
		const auto c_fg = st::mediaviewTransparentFg->c;
		TransparentContentUniforms uniforms{};
		uniforms.viewport[0] = vw;
		uniforms.viewport[1] = vh;
		uniforms.flipY = NdcFlipY(_rhi);
		uniforms.fragCoordYUp = FragCoordYUp(_rhi);
		fillShadowUniforms(
			uniforms.shadowTopRect,
			uniforms.shadowBottomSkipOpacityFullFade,
			geometry);
		uniforms.transparentBg[0] = float(c_bg.redF());
		uniforms.transparentBg[1] = float(c_bg.greenF());
		uniforms.transparentBg[2] = float(c_bg.blueF());
		uniforms.transparentBg[3] = float(c_bg.alphaF());
		uniforms.transparentFg[0] = float(c_fg.redF());
		uniforms.transparentFg[1] = float(c_fg.greenF());
		uniforms.transparentFg[2] = float(c_fg.blueF());
		uniforms.transparentFg[3] = float(c_fg.alphaF());
		uniforms.transparentSize =
			st::transparentPlaceholderSize * _factor;
		uniforms.g_opacity = float(geometry.opacity);
		_rub->updateDynamicBuffer(
			_uniformBuffer,
			uOffset,
			sizeof(TransparentContentUniforms),
			&uniforms);
	} else {
		ContentUniforms uniforms{};
		uniforms.viewport[0] = vw;
		uniforms.viewport[1] = vh;
		uniforms.flipY = NdcFlipY(_rhi);
		uniforms.fragCoordYUp = FragCoordYUp(_rhi);
		fillShadowUniforms(
			uniforms.shadowTopRect,
			uniforms.shadowBottomSkipOpacityFullFade,
			geometry);
		const auto rRect = scaleRect(
			transformRect(geometry.rect),
			geometry.scale);
		if (geometry.roundRadius) {
			uniforms.roundRect[0] = rRect.x();
			uniforms.roundRect[1] = rRect.y();
			uniforms.roundRect[2] = rRect.width();
			uniforms.roundRect[3] = rRect.height();
		} else {
			uniforms.roundRect[0] = 0;
			uniforms.roundRect[1] = 0;
			uniforms.roundRect[2] = vw;
			uniforms.roundRect[3] = vh;
		}
		uniforms.roundRadius = geometry.roundRadius * _factor;
		uniforms.g_opacity = float(geometry.opacity);
		_rub->updateDynamicBuffer(
			_uniformBuffer,
			uOffset,
			sizeof(ContentUniforms),
			&uniforms);
	}

	auto *srb = _rhi->newShaderResourceBindings();
	srb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			uOffset,
			fillTransparentBackground
				? int(sizeof(TransparentContentUniforms))
				: int(sizeof(ContentUniforms))),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			contentTexture,
			_sampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::FragmentStage,
			_controlsFadeTexture
				? _controlsFadeTexture
				: _placeholderTexture,
			_sampler),
	});
	if (!srb->create()) {
		delete srb;
		_initialized = false;
		return;
	}
	_perDrawSrbs.push_back(srb);

	_drawCommands.push_back({
		.pipeline = pipeline,
		.srb = srb,
		.vertexIndex = slot,
	});
}

void OverlayWidget::RendererRhi::paintBackground() {
	if (!_initialized) {
		return;
	}
	const auto shown = _owner->shownProgress();
	auto c = _owner->_fullScreenVideo
		? st::mediaviewVideoBg->c
		: st::mediaviewBg->c;
	if (shown < 1. || _owner->lightboxStyle()) {
		// 开合动画中背景随进度出现，磨砂快照按剩余比例叠加在背景色上。
		c = _owner->backdropColor();
		c.setAlphaF(shown * (1. - _owner->backdropOpacity()) * c.alphaF());
	}
	const auto vw = float(_viewport.width() * _factor);
	const auto vh = float(_viewport.height() * _factor);

	auto bgImage = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
	bgImage.fill(c);

	auto *tex = acquirePoolTexture(QSize(1, 1));
	if (!tex) {
		return;
	}
	_rub->uploadTexture(
		tex,
		QRhiTextureUploadDescription(
			QRhiTextureUploadEntry(0, 0,
				QRhiTextureSubresourceUploadDescription(bgImage))));

	const float coords[] = {
		0.f, vh, 0.f, 0.f,
		vw,  vh, 1.f, 0.f,
		0.f, 0.f, 0.f, 1.f,
		vw,  0.f, 1.f, 1.f,
	};
	drawTexturedQuad(_imagePipeline, tex, coords);

	if (const auto opacity = _owner->backdropOpacity() * shown; opacity > 0.) {
		paintBackdrop(opacity);
	}
}

void OverlayWidget::RendererRhi::paintBackdrop(float64 opacity) {
	const auto &image = _owner->_backdrop;
	if (_backdropKey != image.cacheKey()) {
		_backdropKey = image.cacheKey();
		if (!_backdropTexture || _backdropSize != image.size()) {
			delete _backdropTexture;
			_backdropTexture = _rhi->newTexture(
				QRhiTexture::BGRA8,
				image.size());
			_backdropTexture->create();
			_backdropSize = image.size();
		}
		_rub->uploadTexture(
			_backdropTexture,
			QRhiTextureUploadDescription(
				QRhiTextureUploadEntry(0, 0,
					QRhiTextureSubresourceUploadDescription(
						image.convertToFormat(
							QImage::Format_ARGB32_Premultiplied)))));
	}
	// 快照保持屏幕原位，只淡出到系统模糊，不随查看器放大。
	const auto rect = transformRect(_owner->backdropRect());
	const float coords[] = {
		rect.left(), rect.bottom(), 0.f, 0.f,
		rect.right(), rect.bottom(), 1.f, 0.f,
		rect.left(), rect.top(), 0.f, 1.f,
		rect.right(), rect.top(), 1.f, 1.f,
	};
	drawTexturedQuad(
		_imageAddPipeline,
		_backdropTexture,
		coords,
		float(opacity));
}

void OverlayWidget::RendererRhi::paintVideoStream() {
	if (!_initialized) {
		return;
	}
	_pendingVideoStream = _owner->_videoStream.get();
}

void OverlayWidget::RendererRhi::paintTransformedVideoFrame(
		ContentGeometry geometry) {
	if (!_initialized) {
		return;
	}
	const auto data = _owner->videoFrameWithInfo();
	if (data.format == Streaming::FrameFormat::None) {
		return;
	} else if (data.format == Streaming::FrameFormat::ARGB32) {
		Assert(!data.image.isNull());
		paintTransformedStaticContent(
			data.image,
			geometry,
			data.alpha,
			data.alpha);
		return;
	}
	const auto nativeTexture =
		(data.format == Streaming::FrameFormat::NativeTexture);
	const auto nv12 = nativeTexture
		|| (data.format == Streaming::FrameFormat::NV12);
	const auto yuv = data.yuv;
	const auto nv12changed = !nativeTexture && (_chromaNV12 != nv12);

	const auto upload = (_trackFrameIndex != data.index)
		|| (_streamedIndex != _owner->streamedIndex())
		|| !_yTexture
		|| (nv12 ? !_uvTexture : (!_uTexture || !_vTexture));

	if (upload) {
		auto zeroCopied = false;
#ifdef Q_OS_MAC
		if (nativeTexture && data.nativeFrame
			&& data.nativeFrame->pixelBuffer) {
			if (_metalTextureCache.createTexturesFromPixelBuffer(
					_rhi,
					data.nativeFrame->pixelBuffer,
					&_yTexture,
					&_uvTexture,
					&_lumaSize,
					&_chromaSize)) {
				_chromaNV12 = true;
				zeroCopied = true;
			} else {
				return;
			}
		}
#endif // Q_OS_MAC
		if (!zeroCopied) {
		if (_usingExternalVideoTextures) {
			delete _yTexture;
			_yTexture = nullptr;
			delete _uTexture;
			_uTexture = nullptr;
			delete _vTexture;
			_vTexture = nullptr;
			delete _uvTexture;
			_uvTexture = nullptr;
			_lumaSize = QSize();
			_chromaSize = QSize();
#ifdef Q_OS_MAC
			_metalTextureCache.flush();
#endif // Q_OS_MAC
		}
		if (!yuv || yuv->size.isEmpty()) {
			return;
		}
		if (!_yTexture || _lumaSize != yuv->size) {
			delete _yTexture;
			_yTexture = _rhi->newTexture(QRhiTexture::R8, yuv->size);
			if (!_yTexture->create()) {
				_initialized = false;
				return;
			}
			_lumaSize = yuv->size;
		}
		auto yDesc = QRhiTextureSubresourceUploadDescription(
			yuv->y.data,
			yuv->y.stride * yuv->size.height());
		yDesc.setDataStride(yuv->y.stride);
		_rub->uploadTexture(
			_yTexture,
			QRhiTextureUploadDescription(
				QRhiTextureUploadEntry(0, 0, yDesc)));

		if (nv12) {
			if (!_uvTexture || nv12changed
				|| _chromaSize != yuv->chromaSize) {
				delete _uvTexture;
				_uvTexture = _rhi->newTexture(
					QRhiTexture::RG8,
					yuv->chromaSize);
				if (!_uvTexture->create()) {
					_initialized = false;
					return;
				}
				_chromaSize = yuv->chromaSize;
			}
			auto uvDesc = QRhiTextureSubresourceUploadDescription(
				yuv->u.data,
				yuv->u.stride * yuv->chromaSize.height());
			uvDesc.setDataStride(yuv->u.stride);
			_rub->uploadTexture(
				_uvTexture,
				QRhiTextureUploadDescription(
					QRhiTextureUploadEntry(0, 0, uvDesc)));
		} else {
			if (!_uTexture || nv12changed
				|| _chromaSize != yuv->chromaSize) {
				delete _uTexture;
				_uTexture = _rhi->newTexture(
					QRhiTexture::R8,
					yuv->chromaSize);
				if (!_uTexture->create()) {
					_initialized = false;
					return;
				}
				delete _vTexture;
				_vTexture = _rhi->newTexture(
					QRhiTexture::R8,
					yuv->chromaSize);
				if (!_vTexture->create()) {
					_initialized = false;
					return;
				}
				_chromaSize = yuv->chromaSize;
			}
			auto uDesc = QRhiTextureSubresourceUploadDescription(
				yuv->u.data,
				yuv->u.stride * yuv->chromaSize.height());
			uDesc.setDataStride(yuv->u.stride);
			_rub->uploadTexture(
				_uTexture,
				QRhiTextureUploadDescription(
					QRhiTextureUploadEntry(0, 0, uDesc)));
			auto vDesc = QRhiTextureSubresourceUploadDescription(
				yuv->v.data,
				yuv->v.stride * yuv->chromaSize.height());
			vDesc.setDataStride(yuv->v.stride);
			_rub->uploadTexture(
				_vTexture,
				QRhiTextureUploadDescription(
					QRhiTextureUploadEntry(0, 0, vDesc)));
		}
		_chromaNV12 = nv12;
		} // if (!zeroCopied)
		_usingExternalVideoTextures = zeroCopied;
		_trackFrameIndex = data.index;
		_streamedIndex = _owner->streamedIndex();
	}

	validateControlsFade();
	if (!_initialized) {
		return;
	}

	if (_nextVertexSlot >= kMaxDraws) {
		return;
	}

	const auto textureRect = _owner->_stories
		? StoryCropTextureRect(
			QSizeF(nativeTexture ? _lumaSize : yuv->size),
			geometry.rect.size())
		: QRectF(0., 0., 1., 1.);
	const auto texLeft = float(textureRect.x());
	const auto texRight = float(textureRect.x() + textureRect.width());
	const auto texTop = float(textureRect.y());
	const auto texBottom = float(textureRect.y() + textureRect.height());

	const auto rRect = scaleRect(
		transformRect(geometry.rect),
		geometry.scale);
	const auto centerx = rRect.x() + rRect.width() / 2;
	const auto centery = rRect.y() + rRect.height() / 2;
	const auto rsin = float(std::sin(geometry.rotation * M_PI / 180.));
	const auto rcos = float(std::cos(geometry.rotation * M_PI / 180.));
	const auto rotated = [&](float x, float y) -> std::array<float, 2> {
		x -= centerx;
		y -= centery;
		return { centerx + x * rcos + y * rsin,
		         centery + y * rcos - x * rsin };
	};
	const auto tl = rotated(rRect.left(), rRect.bottom());
	const auto tr = rotated(rRect.right(), rRect.bottom());
	const auto bl = rotated(rRect.left(), rRect.top());
	const auto br = rotated(rRect.right(), rRect.top());

	const auto slot = _nextVertexSlot++;
	const auto vOffset = slot * kVertexSize;
	const auto uOffset = slot * 256;
	const float coords[] = {
		tl[0], tl[1], texLeft, texTop,
		tr[0], tr[1], texRight, texTop,
		bl[0], bl[1], texLeft, texBottom,
		br[0], br[1], texRight, texBottom,
	};
	_rub->updateDynamicBuffer(
		_vertexBuffer, vOffset, kVertexSize, coords);

	const auto vw = _viewport.width() * _factor;
	const auto vh = _viewport.height() * _factor;
	ContentUniforms uniforms{};
	uniforms.viewport[0] = vw;
	uniforms.viewport[1] = vh;
	uniforms.flipY = NdcFlipY(_rhi);
	uniforms.fragCoordYUp = FragCoordYUp(_rhi);
	fillShadowUniforms(
		uniforms.shadowTopRect,
		uniforms.shadowBottomSkipOpacityFullFade,
		geometry);
	if (geometry.roundRadius) {
		uniforms.roundRect[0] = rRect.x();
		uniforms.roundRect[1] = rRect.y();
		uniforms.roundRect[2] = rRect.width();
		uniforms.roundRect[3] = rRect.height();
	} else {
		uniforms.roundRect[0] = 0;
		uniforms.roundRect[1] = 0;
		uniforms.roundRect[2] = vw;
		uniforms.roundRect[3] = vh;
	}
	uniforms.roundRadius = geometry.roundRadius * _factor;
	uniforms.g_opacity = float(geometry.opacity);
	_rub->updateDynamicBuffer(
		_uniformBuffer, uOffset, sizeof(ContentUniforms), &uniforms);

	const auto blendEnabled = (geometry.roundRadius > 0.)
		|| (geometry.opacity < 1.);
	auto *srb = _rhi->newShaderResourceBindings();
	if (nv12) {
		srb->setBindings({
			QRhiShaderResourceBinding::uniformBuffer(
				0,
				QRhiShaderResourceBinding::VertexStage
					| QRhiShaderResourceBinding::FragmentStage,
				_uniformBuffer,
				uOffset,
				sizeof(ContentUniforms)),
			QRhiShaderResourceBinding::sampledTexture(
				1,
				QRhiShaderResourceBinding::FragmentStage,
				_yTexture,
				_sampler),
			QRhiShaderResourceBinding::sampledTexture(
				2,
				QRhiShaderResourceBinding::FragmentStage,
				_uvTexture,
				_sampler),
			QRhiShaderResourceBinding::sampledTexture(
				3,
				QRhiShaderResourceBinding::FragmentStage,
				_controlsFadeTexture
					? _controlsFadeTexture
					: _placeholderTexture,
				_sampler),
		});
	} else {
		srb->setBindings({
			QRhiShaderResourceBinding::uniformBuffer(
				0,
				QRhiShaderResourceBinding::VertexStage
					| QRhiShaderResourceBinding::FragmentStage,
				_uniformBuffer,
				uOffset,
				sizeof(ContentUniforms)),
			QRhiShaderResourceBinding::sampledTexture(
				1,
				QRhiShaderResourceBinding::FragmentStage,
				_yTexture,
				_sampler),
			QRhiShaderResourceBinding::sampledTexture(
				2,
				QRhiShaderResourceBinding::FragmentStage,
				_uTexture,
				_sampler),
			QRhiShaderResourceBinding::sampledTexture(
				3,
				QRhiShaderResourceBinding::FragmentStage,
				_vTexture,
				_sampler),
			QRhiShaderResourceBinding::sampledTexture(
				4,
				QRhiShaderResourceBinding::FragmentStage,
				_controlsFadeTexture
					? _controlsFadeTexture
					: _placeholderTexture,
				_sampler),
		});
	}
	if (!srb->create()) {
		delete srb;
		_initialized = false;
		return;
	}
	_perDrawSrbs.push_back(srb);

	auto *pipeline = nv12
		? (blendEnabled ? _nv12BlendPipeline : _nv12Pipeline)
		: (blendEnabled ? _yuv420BlendPipeline : _yuv420Pipeline);
	_drawCommands.push_back({
		.pipeline = pipeline,
		.srb = srb,
		.vertexIndex = slot,
	});

	if (_owner->_recognitionResult.success
		&& !_owner->_recognitionResult.items.empty()) {
		const auto opacity = _owner->_recognitionAnimation.value(
			_owner->_showRecognitionResults ? 1. : 0.);
		if (opacity > 0.) {
			paintRecognitionOverlay(data.image, geometry);
		}
	}
}

void OverlayWidget::RendererRhi::paintRecognitionOverlay(
		const QImage &image,
		ContentGeometry geometry) {
	if (!_initialized) {
		return;
	}
	if (!_owner->_recognitionResult.success
		|| _owner->_recognitionResult.items.empty()) {
		return;
	}
	const auto opacity = _owner->_recognitionAnimation.value(
		_owner->_showRecognitionResults ? 1. : 0.);
	if (opacity <= 0.) {
		return;
	}
	const auto rect = geometry.rect;
	const auto overlaySize = rect.size().toSize() * _ifactor;
	if (overlaySize.isEmpty()) {
		return;
	}
	auto overlay = QImage(
		overlaySize,
		QImage::Format_ARGB32_Premultiplied);
	overlay.setDevicePixelRatio(_factor);
	overlay.fill(QColor(0, 0, 0, int(77 * opacity)));

	const auto scale = rect.width() / float(image.width());
	{
		auto p = QPainter(&overlay);
		p.setCompositionMode(QPainter::CompositionMode_Clear);
		for (const auto &item : _owner->_recognitionResult.items) {
			const auto &r = item.rect;
			p.fillRect(QRectF(
				r.x() * _ifactor * scale,
				r.y() * _ifactor * scale,
				r.width() * _ifactor * scale,
				r.height() * _ifactor * scale), Qt::transparent);
		}
		const auto spans = _owner->_recognition.spans();
		if (!spans.empty()) {
			p.setCompositionMode(QPainter::CompositionMode_SourceOver);
			const auto color = QColor(48, 128, 255, int(128 * opacity));
			for (const auto &span : spans) {
				const auto band = _owner->_recognition.bandFor(
					span.item,
					span.from,
					span.till);
				if (!band.isEmpty()) {
					p.fillRect(QRectF(
						band.x() * _ifactor * scale,
						band.y() * _ifactor * scale,
						band.width() * _ifactor * scale,
						band.height() * _ifactor * scale), color);
				}
			}
		}
	}

	auto *tex = acquirePoolTexture(overlaySize);
	if (!tex) {
		return;
	}
	_rub->uploadTexture(
		tex,
		QRhiTextureUploadDescription(
			QRhiTextureUploadEntry(0, 0,
				QRhiTextureSubresourceUploadDescription(overlay))));

	const auto rRect = scaleRect(transformRect(rect), geometry.scale);
	const auto centerx = rRect.x() + rRect.width() / 2;
	const auto centery = rRect.y() + rRect.height() / 2;
	const auto rsin = float(std::sin(geometry.rotation * M_PI / 180.));
	const auto rcos = float(std::cos(geometry.rotation * M_PI / 180.));
	const auto rotated = [&](float x, float y) -> std::array<float, 2> {
		x -= centerx;
		y -= centery;
		return { centerx + x * rcos + y * rsin,
		         centery + y * rcos - x * rsin };
	};
	const auto tl = rotated(rRect.left(), rRect.bottom());
	const auto tr = rotated(rRect.right(), rRect.bottom());
	const auto bl = rotated(rRect.left(), rRect.top());
	const auto br = rotated(rRect.right(), rRect.top());
	const float coords[] = {
		tl[0], tl[1], 0.f, 0.f,
		tr[0], tr[1], 1.f, 0.f,
		bl[0], bl[1], 0.f, 1.f,
		br[0], br[1], 1.f, 1.f,
	};
	drawTexturedQuad(_imagePipeline, tex, coords, 1.f, true);
}

void OverlayWidget::RendererRhi::paintTransformedStaticContent(
		const QImage &image,
		ContentGeometry geometry,
		bool semiTransparent,
		bool fillTransparentBackground,
		int index) {
	if (!_initialized) {
		return;
	}
	Expects(index >= 0 && index < 3);

	if (image.isNull() || geometry.rect.isEmpty() || !_imagePipeline) {
		return;
	}

	const auto cacheKey = image.cacheKey();
	const auto upload = (_cacheKeys[index] != cacheKey);
	if (upload) {
		if (!_rgbaTextures[index]
			|| _rgbaSizes[index] != image.size()) {
			delete _rgbaTextures[index];
			_rgbaTextures[index] = _rhi->newTexture(
				QRhiTexture::BGRA8,
				image.size());
			if (!_rgbaTextures[index]->create()) {
				_initialized = false;
				return;
			}
			_rgbaSizes[index] = image.size();
		}
		_rub->uploadTexture(
			_rgbaTextures[index],
			QRhiTextureUploadDescription(
				QRhiTextureUploadEntry(0, 0,
					QRhiTextureSubresourceUploadDescription(image))));
		_cacheKeys[index] = cacheKey;
	}

	const auto textureRect = _owner->_stories
		? StoryCropTextureRect(
			QSizeF(image.size()),
			geometry.rect.size())
		: QRectF(0., 0., 1., 1.);
	const auto texLeft = float(textureRect.x());
	const auto texRight = float(textureRect.x() + textureRect.width());
	const auto texTop = float(textureRect.y());
	const auto texBottom = float(textureRect.y() + textureRect.height());

	const auto rRect = scaleRect(
		transformRect(geometry.rect),
		geometry.scale);
	const auto centerx = rRect.x() + rRect.width() / 2;
	const auto centery = rRect.y() + rRect.height() / 2;
	const auto rsin = float(std::sin(geometry.rotation * M_PI / 180.));
	const auto rcos = float(std::cos(geometry.rotation * M_PI / 180.));
	const auto rotated = [&](float x, float y) -> std::array<float, 2> {
		x -= centerx;
		y -= centery;
		return { centerx + x * rcos + y * rsin,
		         centery + y * rcos - x * rsin };
	};
	const auto tl = rotated(rRect.left(), rRect.bottom());
	const auto tr = rotated(rRect.right(), rRect.bottom());
	const auto bl = rotated(rRect.left(), rRect.top());
	const auto br = rotated(rRect.right(), rRect.top());
	const float coords[] = {
		tl[0], tl[1], texLeft, texTop,
		tr[0], tr[1], texRight, texTop,
		bl[0], bl[1], texLeft, texBottom,
		br[0], br[1], texRight, texBottom,
	};

	const auto blend = (geometry.roundRadius > 0.)
		|| (geometry.opacity < 1.)
		|| (semiTransparent && !fillTransparentBackground);
	drawContentQuad(
		_rgbaTextures[index],
		coords,
		geometry,
		fillTransparentBackground,
		blend);
	paintRecognitionOverlay(image, geometry);
}

void OverlayWidget::RendererRhi::paintRadialLoading(
		QRect inner,
		bool radial,
		float64 radialOpacity) {
	if (!_initialized) {
		return;
	}
	paintUsingRaster(inner, [&](Painter &p) {
		const auto newInner = QRect(QPoint(), inner.size());
		_owner->paintRadialLoadingContent(p, newInner, radial, radialOpacity);
	}, true);
}

void OverlayWidget::RendererRhi::paintThemePreview(QRect outer) {
	if (!_initialized) {
		return;
	}
	paintUsingRaster(outer, [&](Painter &p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintThemePreviewContent(p, newOuter, newOuter);
	});
}

void OverlayWidget::RendererRhi::paintDocumentBubble(
		QRect outer,
		QRect icon) {
	if (!_initialized) {
		return;
	}
	paintUsingRaster(outer, [&](Painter &p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		const auto newIcon = icon.translated(-outer.topLeft());
		_owner->paintDocumentBubbleContent(p, newOuter, newIcon, newOuter);
	}, true);
}

void OverlayWidget::RendererRhi::paintSaveMsg(QRect outer) {
	if (!_initialized) {
		return;
	}
	paintUsingRaster(outer, [&](Painter &p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintSaveMsgContent(p, newOuter, newOuter);
	}, true);
}

void OverlayWidget::RendererRhi::paintChapter(QRect outer) {
	if (!_initialized) {
		return;
	}
	paintUsingRaster(outer, [&](Painter &p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintChapterContent(p, newOuter, newOuter);
	}, true);
}

void OverlayWidget::RendererRhi::paintSpeedBoost(QRect outer) {
	if (!_initialized) {
		return;
	}
	paintUsingRaster(outer, [&](Painter &p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintSpeedBoostContent(p, newOuter, newOuter);
	}, true);
}

auto OverlayWidget::RendererRhi::controlMeta(Over control) const
-> Control {
	const auto stories = [&] {
		return (_owner->_stories != nullptr);
	};
	switch (control) {
	case Over::Left: return {
		0,
		stories() ? &st::storiesLeft : &st::mediaviewLeft
	};
	case Over::Right: return {
		1,
		stories() ? &st::storiesRight : &st::mediaviewRight
	};
	case Over::Save: return {
		2,
		(_owner->saveControlLocked()
			? &st::mediaviewSaveLocked
			: &st::mediaviewSave)
	};
	case Over::Share: return { 3, &st::mediaviewShare };
	case Over::Rotate: return { 4, &st::mediaviewRotate };
	case Over::More: return { 5, &st::mediaviewMore };
	case Over::Draw: return { 6, &st::mediaviewDraw };
	case Over::Recognize: return { 7, &st::mediaviewRecognize };
	case Over::FullScreen: return _owner->_fullscreen
		? Control{ 9, &st::extrasViewerFullScreenOut }
		: Control{ 8, &st::extrasViewerFullScreen };
	case Over::Close: return { 10, &st::extrasViewerClose };
	}
	Unexpected("Control value in OverlayWidget::RendererRhi::controlMeta.");
}

void OverlayWidget::RendererRhi::validateControls() {
	if (_controlsAtlasTexture) {
		return;
	}
	const auto metas = {
		controlMeta(Over::Left),
		controlMeta(Over::Right),
		controlMeta(Over::Save),
		controlMeta(Over::Share),
		controlMeta(Over::Rotate),
		controlMeta(Over::More),
		controlMeta(Over::Draw),
		controlMeta(Over::Recognize),
		Control{ 8, &st::extrasViewerFullScreen },
		Control{ 9, &st::extrasViewerFullScreenOut },
		Control{ 10, &st::extrasViewerClose },
	};
	const auto rounded = st::extrasViewerTopBarButton;
	const auto nav = st::extrasViewerNavSize;
	// 纯色块四周留空，取中间部分采样，避免线性过滤混入透明像素。
	const auto solid = 8;
	const auto solidSkip = 2;
	auto maxWidth = 0;
	auto fullHeight = 0;
	for (const auto &meta : metas) {
		maxWidth = std::max(meta.icon->width(), maxWidth);
		fullHeight += meta.icon->height();
	}
	maxWidth = std::max({ st::mediaviewIconOver, rounded, nav, maxWidth });
	fullHeight += st::mediaviewIconOver + rounded + solid + nav;
	auto image = QImage(
		QSize(maxWidth, fullHeight) * _ifactor,
		QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	image.setDevicePixelRatio(_ifactor);
	{
		auto p = QPainter(&image);
		auto index = 0;
		auto height = 0;
		for (const auto &meta : metas) {
			meta.icon->paint(p, 0, height, maxWidth);
			_controlsTextures[index++] = QRect(
				QPoint(0, height) * _ifactor,
				meta.icon->size() * _ifactor);
			height += meta.icon->height();
		}
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(OverBackgroundColor());
		p.drawEllipse(
			QRect(0, height, st::mediaviewIconOver, st::mediaviewIconOver));
		_controlsTextures[index++] = QRect(
			QPoint(0, height) * _ifactor,
			QSize(st::mediaviewIconOver, st::mediaviewIconOver) * _ifactor);
		height += st::mediaviewIconOver;

		const auto radius = st::extrasViewerTopBarButtonRadius;
		p.drawRoundedRect(QRect(0, height, rounded, rounded), radius, radius);
		_controlsTextures[index++] = QRect(
			QPoint(0, height) * _ifactor,
			QSize(rounded, rounded) * _ifactor);
		height += rounded;

		p.fillRect(QRect(0, height, solid, solid), st::mediaviewControlFg);
		_controlsTextures[index++] = QRect(
			QPoint(solidSkip, height + solidSkip) * _ifactor,
			QSize(solid - 2 * solidSkip, solid - 2 * solidSkip) * _ifactor);
		height += solid;

		p.drawEllipse(QRect(0, height, nav, nav));
		_controlsTextures[index++] = QRect(
			QPoint(0, height) * _ifactor,
			QSize(nav, nav) * _ifactor);
	}

	const auto size = image.size();
	_controlsAtlasTexture = _rhi->newTexture(QRhiTexture::BGRA8, size);
	if (!_controlsAtlasTexture->create()) {
		_initialized = false;
		return;
	}
	_controlsAtlasSize = size;
	_rub->uploadTexture(
		_controlsAtlasTexture,
		QRhiTextureUploadDescription(
			QRhiTextureUploadEntry(0, 0,
				QRhiTextureSubresourceUploadDescription(image))));
}

void OverlayWidget::RendererRhi::invalidateControls() {
	delete _controlsAtlasTexture;
	_controlsAtlasTexture = nullptr;
	ranges::fill(_controlsTextures, QRect());
}

void OverlayWidget::RendererRhi::paintControlsStart() {
	if (!_initialized) {
		return;
	}
	validateControls();
	if (!_initialized) {
		return;
	}
}

void OverlayWidget::RendererRhi::paintControl(
		Over control,
		QRect over,
		float64 overOpacity,
		QRect inner,
		float64 innerOpacity,
		const style::icon &icon) {
	if (!_initialized) {
		return;
	}
	if (!_controlsAtlasTexture) {
		return;
	}
	const auto meta = controlMeta(control);
	Assert(meta.icon == &icon);

	if (!over.isEmpty() && overOpacity > 0.) {
		const auto nav = (control == Over::Left) || (control == Over::Right);
		const auto overTexture = !_owner->lightboxStyle()
			? kControlsCount
			: nav
			? (kControlsCount + 3)
			: (kControlsCount + 1);
		drawAtlasQuad(
			over,
			_controlsTextures[overTexture],
			float(overOpacity * kOverBackgroundOpacity));
	}
	drawAtlasQuad(inner, _controlsTextures[meta.index], float(innerOpacity));
}

void OverlayWidget::RendererRhi::paintTopBarSeparator(
		QRect rect,
		float64 opacity) {
	validateControls();
	drawAtlasQuad(rect, _controlsTextures[kControlsCount + 2], float(opacity));
}

void OverlayWidget::RendererRhi::drawAtlasQuad(
		QRect rect,
		QRect texture,
		float opacity) {
	const auto atlasW = float(_controlsAtlasSize.width());
	const auto atlasH = float(_controlsAtlasSize.height());
	const auto geometry = transformRect(rect);
	// 用 x + width 取右下边界，QRect::right() 少一个像素会把图标拉宽。
	const auto tl = texture.x() / atlasW;
	const auto tr = (texture.x() + texture.width()) / atlasW;
	const auto tt = texture.y() / atlasH;
	const auto tb = (texture.y() + texture.height()) / atlasH;
	const float coords[] = {
		geometry.left(), geometry.bottom(), tl, tt,
		geometry.right(), geometry.bottom(), tr, tt,
		geometry.left(), geometry.top(), tl, tb,
		geometry.right(), geometry.top(), tr, tb,
	};
	drawTexturedQuad(
		_imagePipeline,
		_controlsAtlasTexture,
		coords,
		opacity,
		true);
}

void OverlayWidget::RendererRhi::paintFooter(
		QRect outer,
		float64 opacity) {
	if (!_initialized) {
		return;
	}
	if (outer.isEmpty() || opacity <= 0.) {
		return;
	}
	paintUsingRaster(outer, [&](Painter &p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintFooterContent(p, newOuter, newOuter, opacity);
	}, true);
}

void OverlayWidget::RendererRhi::paintCaption(
		QRect outer,
		float64 opacity) {
	if (!_initialized) {
		return;
	}
	if (outer.isEmpty() || opacity <= 0.) {
		return;
	}
	paintUsingRaster(outer, [&](Painter &p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintCaptionContent(p, newOuter, newOuter, opacity);
	}, true);
}

void OverlayWidget::RendererRhi::paintGroupThumbs(
		QRect outer,
		float64 opacity) {
	if (!_initialized) {
		return;
	}
	if (outer.isEmpty() || opacity <= 0.) {
		return;
	}
	paintUsingRaster(outer, [&](Painter &p) {
		const auto newOuter = QRect(QPoint(), outer.size());
		_owner->paintGroupThumbsContent(p, newOuter, newOuter, opacity);
	}, true);
}

void OverlayWidget::RendererRhi::paintRoundedCorners(float64 radius) {
	if (!_initialized) {
		return;
	}
	if (!_roundedCornersPipeline || radius <= 0.) {
		return;
	}

	const auto vw = _viewport.width() * _factor;
	const auto vh = _viewport.height() * _factor;

	RoundedCornersUniforms uniforms{};
	uniforms.viewport[0] = vw;
	uniforms.viewport[1] = vh;
	uniforms.flipY = NdcFlipY(_rhi);
	uniforms.fragCoordYUp = FragCoordYUp(_rhi);
	const auto rounding = _owner->roundingRect();
	const auto roundRect = transformRect(rounding);
	uniforms.roundRect[0] = roundRect.x();
	uniforms.roundRect[1] = roundRect.y();
	uniforms.roundRect[2] = roundRect.width();
	uniforms.roundRect[3] = roundRect.height();
	uniforms.roundRadius = radius * _factor;

	// 圆角矩形可能向内收（露出宿主边框），沿四边各擦一条带，宽度覆盖圆角与内收部分。
	const auto w = _owner->width();
	const auto h = _owner->height();
	const auto inset = std::max({
		0.,
		rounding.left(),
		rounding.top(),
		w - rounding.right(),
		h - rounding.bottom(),
	});
	const auto band = int(std::ceil(radius + inset));
	const auto bands = {
		transformRect(QRect(0, 0, w, band)),
		transformRect(QRect(0, h - band, w, band)),
		transformRect(QRect(0, band, band, h - 2 * band)),
		transformRect(QRect(w - band, band, band, h - 2 * band)),
	};

	auto cornerIndex = 0;
	for (const auto &corner : bands) {
		if (_nextVertexSlot >= kMaxDraws) {
			break;
		}
		const auto slot = _nextVertexSlot++;
		const auto uOffset = slot * 256;

		const float coords[] = {
			corner.left(), corner.bottom(),
			corner.right(), corner.bottom(),
			corner.left(), corner.top(),
			corner.right(), corner.top(),
		};

		_rub->updateDynamicBuffer(
			_fillVertexBuffer,
			cornerIndex * int(sizeof(coords)),
			sizeof(coords),
			coords);

		_rub->updateDynamicBuffer(
			_uniformBuffer,
			uOffset,
			sizeof(RoundedCornersUniforms),
			&uniforms);

		auto *srb = _rhi->newShaderResourceBindings();
		srb->setBindings({
			QRhiShaderResourceBinding::uniformBuffer(
				0,
				QRhiShaderResourceBinding::VertexStage
					| QRhiShaderResourceBinding::FragmentStage,
				_uniformBuffer,
				uOffset,
				sizeof(RoundedCornersUniforms)),
		});
		if (!srb->create()) {
			delete srb;
			_initialized = false;
			return;
		}
		_perDrawSrbs.push_back(srb);

		_drawCommands.push_back({
			.pipeline = _roundedCornersPipeline,
			.srb = srb,
			.vertexIndex = cornerIndex,
			.fillVertex = true,
		});
		++cornerIndex;
	}
}

void OverlayWidget::RendererRhi::paintStoriesSiblingPart(
		int index,
		const QImage &image,
		QRect rect,
		float64 opacity) {
	if (!_initialized) {
		return;
	}
	Expects(index >= 0 && index < kStoriesSiblingPartsCount);

	if (image.isNull() || rect.isEmpty()) {
		return;
	}

	const auto cacheKey = image.cacheKey();
	if (_storiesSiblingCacheKeys[index] != cacheKey
		|| !_storiesSiblingTextures[index]
		|| _storiesSiblingSizes[index] != image.size()) {
		delete _storiesSiblingTextures[index];
		_storiesSiblingTextures[index] = _rhi->newTexture(
			QRhiTexture::BGRA8,
			image.size());
		if (!_storiesSiblingTextures[index]->create()) {
			_initialized = false;
			return;
		}
		_storiesSiblingSizes[index] = image.size();
		_rub->uploadTexture(
			_storiesSiblingTextures[index],
			QRhiTextureUploadDescription(
				QRhiTextureUploadEntry(0, 0,
					QRhiTextureSubresourceUploadDescription(image))));
		_storiesSiblingCacheKeys[index] = cacheKey;
	}

	const auto rRect = transformRect(rect);
	const float coords[] = {
		rRect.left(), rRect.bottom(), 0.f, 0.f,
		rRect.right(), rRect.bottom(), 1.f, 0.f,
		rRect.left(), rRect.top(), 0.f, 1.f,
		rRect.right(), rRect.top(), 1.f, 1.f,
	};
	drawTexturedQuad(
		_imagePipeline,
		_storiesSiblingTextures[index],
		coords,
		float(opacity),
		true);
}

Rect OverlayWidget::RendererRhi::transformRect(const Rect &raster) const {
	return TransformRect(raster, _viewport, _factor);
}

Rect OverlayWidget::RendererRhi::transformRect(const QRectF &raster) const {
	return TransformRect(raster, _viewport, _factor);
}

Rect OverlayWidget::RendererRhi::transformRect(const QRect &raster) const {
	return TransformRect(Rect(raster), _viewport, _factor);
}

Rect OverlayWidget::RendererRhi::scaleRect(
		const Rect &unscaled,
		float64 scale) const {
	const auto added = scale - 1.;
	const auto addw = unscaled.width() * added;
	const auto addh = unscaled.height() * added;
	return Rect(
		unscaled.x() - addw / 2,
		unscaled.y() - addh / 2,
		unscaled.width() + addw,
		unscaled.height() + addh);
}

} // namespace Media::View

#endif // Qt >= 6.7 && !DESKTOP_APP_DISABLE_PRIVATE_QT_API
