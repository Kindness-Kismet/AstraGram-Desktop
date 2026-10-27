#include "calls/group/calls_group_viewport_rhi_internal.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0) && !defined DESKTOP_APP_DISABLE_PRIVATE_QT_API

#include "ui/rhi/rhi_shader.h"
#include <rhi/qrhi.h>

namespace Calls::Group {

using namespace Ui::GL;
using namespace RhiDetails;

namespace {

[[nodiscard]] QShader LoadShader(const QString &name) {
	return Ui::Rhi::ShaderFromFile(
		u":/shaders/"_q + name + u".qsb"_q);
}

} // namespace

bool Viewport::RendererRhi::createPipelines() {
	const auto rpDesc = _rt->renderPassDescriptor();

	const auto passthroughVert = LoadShader(u"passthrough.vert"_q);
	const auto argb32Vert = LoadShader(u"argb32.vert"_q);
	const auto groupFrameVert = LoadShader(u"group_frame.vert"_q);
	const auto argb32Frag = LoadShader(u"argb32.frag"_q);
	const auto yuv420Frag = LoadShader(u"yuv420.frag"_q);
	const auto blurHFrag = LoadShader(u"blur_h.frag"_q);
	const auto blurVFrag = LoadShader(u"blur_v.frag"_q);
	const auto groupFrameFrag = LoadShader(u"group_frame.frag"_q);
	const auto controlsFrag = LoadShader(u"controls.frag"_q);

	QRhiVertexInputLayout passthroughLayout;
	passthroughLayout.setBindings({ { 4 * sizeof(float) } });
	passthroughLayout.setAttributes({
		{ 0, 0, QRhiVertexInputAttribute::Float2, 0 },
		{ 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
	});

	QRhiVertexInputLayout argb32Layout;
	argb32Layout.setBindings({ { 4 * sizeof(float) } });
	argb32Layout.setAttributes({
		{ 0, 0, QRhiVertexInputAttribute::Float2, 0 },
		{ 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
	});

	QRhiVertexInputLayout groupFrameLayout;
	groupFrameLayout.setBindings({ { 6 * sizeof(float) } });
	groupFrameLayout.setAttributes({
		{ 0, 0, QRhiVertexInputAttribute::Float2, 0 },
		{ 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
		{ 0, 2, QRhiVertexInputAttribute::Float2, 4 * sizeof(float) },
	});

	_downscaleArgb32Srb = _rhi->newShaderResourceBindings();
	_downscaleArgb32Srb->setBindings({
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_linearSampler),
	});
	if (!_downscaleArgb32Srb->create()) {
		return false;
	}

	_downscaleYuv420Srb = _rhi->newShaderResourceBindings();
	_downscaleYuv420Srb->setBindings({
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_linearSampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_linearSampler),
		QRhiShaderResourceBinding::sampledTexture(
			3,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_linearSampler),
	});
	if (!_downscaleYuv420Srb->create()) {
		return false;
	}

	_blurHSrb = _rhi->newShaderResourceBindings();
	_blurHSrb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			0,
			sizeof(BlurUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_nearestSampler),
	});
	if (!_blurHSrb->create()) {
		return false;
	}

	{
		auto tex = std::unique_ptr<QRhiTexture>(_rhi->newTexture(
			QRhiTexture::RGBA8,
			QSize(1, 1),
			1,
			QRhiTexture::RenderTarget));
		if (!tex->create()) {
			return false;
		}
		auto colorAtt = QRhiColorAttachment(tex.get());
		auto offscreenRT = std::unique_ptr<QRhiTextureRenderTarget>(
			_rhi->newTextureRenderTarget(
				QRhiTextureRenderTargetDescription(colorAtt)));
		_offscreenRpDesc = offscreenRT->newCompatibleRenderPassDescriptor();
		offscreenRT->setRenderPassDescriptor(_offscreenRpDesc);
		if (!offscreenRT->create()) {
			return false;
		}
	}

	_downscaleArgb32Pipeline = _rhi->newGraphicsPipeline();
	_downscaleArgb32Pipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, passthroughVert },
		{ QRhiShaderStage::Fragment, argb32Frag },
	});
	_downscaleArgb32Pipeline->setVertexInputLayout(passthroughLayout);
	_downscaleArgb32Pipeline->setTopology(
		QRhiGraphicsPipeline::TriangleStrip);
	_downscaleArgb32Pipeline->setShaderResourceBindings(_downscaleArgb32Srb);
	_downscaleArgb32Pipeline->setRenderPassDescriptor(_offscreenRpDesc);
	if (!_downscaleArgb32Pipeline->create()) {
		return false;
	}

	_downscaleYuv420Pipeline = _rhi->newGraphicsPipeline();
	_downscaleYuv420Pipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, passthroughVert },
		{ QRhiShaderStage::Fragment, yuv420Frag },
	});
	_downscaleYuv420Pipeline->setVertexInputLayout(passthroughLayout);
	_downscaleYuv420Pipeline->setTopology(
		QRhiGraphicsPipeline::TriangleStrip);
	_downscaleYuv420Pipeline->setShaderResourceBindings(
		_downscaleYuv420Srb);
	_downscaleYuv420Pipeline->setRenderPassDescriptor(_offscreenRpDesc);
	if (!_downscaleYuv420Pipeline->create()) {
		return false;
	}

	_blurHPipeline = _rhi->newGraphicsPipeline();
	_blurHPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, passthroughVert },
		{ QRhiShaderStage::Fragment, blurHFrag },
	});
	_blurHPipeline->setVertexInputLayout(passthroughLayout);
	_blurHPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	_blurHPipeline->setShaderResourceBindings(_blurHSrb);
	_blurHPipeline->setRenderPassDescriptor(_offscreenRpDesc);
	if (!_blurHPipeline->create()) {
		return false;
	}

	_blurVPipeline = _rhi->newGraphicsPipeline();
	_blurVPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, passthroughVert },
		{ QRhiShaderStage::Fragment, blurVFrag },
	});
	_blurVPipeline->setVertexInputLayout(passthroughLayout);
	_blurVPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	_blurVPipeline->setShaderResourceBindings(_blurHSrb);
	_blurVPipeline->setRenderPassDescriptor(_offscreenRpDesc);
	if (!_blurVPipeline->create()) {
		return false;
	}

	auto *frameSrb = _rhi->newShaderResourceBindings();
	frameSrb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			0,
			sizeof(GroupFrameUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_linearSampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_linearSampler),
		QRhiShaderResourceBinding::sampledTexture(
			3,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_noiseRepeatSampler),
	});
	if (!frameSrb->create()) {
		delete frameSrb;
		return false;
	}
	_perDrawSrbs.push_back(frameSrb);

	_framePipeline = _rhi->newGraphicsPipeline();
	_framePipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, groupFrameVert },
		{ QRhiShaderStage::Fragment, groupFrameFrag },
	});
	_framePipeline->setVertexInputLayout(groupFrameLayout);
	_framePipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	_framePipeline->setShaderResourceBindings(frameSrb);
	_framePipeline->setRenderPassDescriptor(rpDesc);
	if (!_framePipeline->create()) {
		return false;
	}

	auto *controlsSrb = _rhi->newShaderResourceBindings();
	controlsSrb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			0,
			sizeof(ImageUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_linearSampler),
	});
	if (!controlsSrb->create()) {
		delete controlsSrb;
		return false;
	}
	_perDrawSrbs.push_back(controlsSrb);

	QRhiGraphicsPipeline::TargetBlend blend;
	blend.enable = true;
	blend.srcColor = QRhiGraphicsPipeline::One;
	blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
	blend.srcAlpha = QRhiGraphicsPipeline::One;
	blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

	_controlsPipeline = _rhi->newGraphicsPipeline();
	_controlsPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, argb32Vert },
		{ QRhiShaderStage::Fragment, controlsFrag },
	});
	_controlsPipeline->setVertexInputLayout(argb32Layout);
	_controlsPipeline->setTargetBlends({ blend });
	_controlsPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	_controlsPipeline->setShaderResourceBindings(controlsSrb);
	_controlsPipeline->setRenderPassDescriptor(rpDesc);
	if (!_controlsPipeline->create()) {
		return false;
	}
	return true;
}

} // namespace Calls::Group

#endif
