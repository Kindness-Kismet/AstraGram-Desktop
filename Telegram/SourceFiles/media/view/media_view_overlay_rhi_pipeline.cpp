#include "media/view/media_view_overlay_rhi_internal.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0) && !defined DESKTOP_APP_DISABLE_PRIVATE_QT_API

#include "ui/rhi/rhi_shader.h"
#include <rhi/qrhi.h>

namespace Media::View {

using namespace Ui::GL;
using namespace RhiDetails;

namespace {

[[nodiscard]] QShader LoadShader(const QString &name) {
	return Ui::Rhi::ShaderFromFile(
		u":/shaders/"_q + name + u".qsb"_q);
}

} // namespace

bool OverlayWidget::RendererRhi::createPipelines() {
	const auto rpDesc = _rt->renderPassDescriptor();

	const auto argb32Vert = LoadShader(u"argb32.vert"_q);
	const auto argb32Frag = LoadShader(u"argb32.frag"_q);
	const auto controlsFrag = LoadShader(u"controls.frag"_q);

	auto *sampleSrb = _rhi->newShaderResourceBindings();
	sampleSrb->setBindings({
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
			_sampler),
	});
	if (!sampleSrb->create()) {
		delete sampleSrb;
		return false;
	}
	_perDrawSrbs.push_back(sampleSrb);

	auto *imagePipeline = _rhi->newGraphicsPipeline();
	imagePipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, argb32Vert },
		{ QRhiShaderStage::Fragment, argb32Frag },
	});
	QRhiVertexInputLayout inputLayout;
	inputLayout.setBindings({ { 4 * sizeof(float) } });
	inputLayout.setAttributes({
		{ 0, 0, QRhiVertexInputAttribute::Float2, 0 },
		{ 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) },
	});
	imagePipeline->setVertexInputLayout(inputLayout);
	imagePipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	imagePipeline->setShaderResourceBindings(sampleSrb);
	imagePipeline->setRenderPassDescriptor(rpDesc);
	if (!imagePipeline->create()) {
		delete imagePipeline;
		return false;
	}
	_imagePipeline = imagePipeline;

	auto *imageBlendPipeline = _rhi->newGraphicsPipeline();
	imageBlendPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, argb32Vert },
		{ QRhiShaderStage::Fragment, controlsFrag },
	});
	imageBlendPipeline->setVertexInputLayout(inputLayout);
	QRhiGraphicsPipeline::TargetBlend blend;
	blend.enable = true;
	blend.srcColor = QRhiGraphicsPipeline::One;
	blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
	blend.srcAlpha = QRhiGraphicsPipeline::One;
	blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
	imageBlendPipeline->setTargetBlends({ blend });
	imageBlendPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	imageBlendPipeline->setShaderResourceBindings(sampleSrb);
	imageBlendPipeline->setRenderPassDescriptor(rpDesc);
	if (!imageBlendPipeline->create()) {
		delete imageBlendPipeline;
		return false;
	}
	_imageBlendPipeline = imageBlendPipeline;

	// 磨砂快照与背景色按比例相加，和 OpenGL 渲染器的混合方式一致。
	auto add = blend;
	add.dstColor = QRhiGraphicsPipeline::One;
	add.dstAlpha = QRhiGraphicsPipeline::One;
	auto *imageAddPipeline = _rhi->newGraphicsPipeline();
	imageAddPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, argb32Vert },
		{ QRhiShaderStage::Fragment, controlsFrag },
	});
	imageAddPipeline->setVertexInputLayout(inputLayout);
	imageAddPipeline->setTargetBlends({ add });
	imageAddPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	imageAddPipeline->setShaderResourceBindings(sampleSrb);
	imageAddPipeline->setRenderPassDescriptor(rpDesc);
	if (!imageAddPipeline->create()) {
		delete imageAddPipeline;
		return false;
	}
	_imageAddPipeline = imageAddPipeline;

	const auto staticContentFrag = LoadShader(u"static_content.frag"_q);
	const auto transparentContentFrag = LoadShader(
		u"transparent_content.frag"_q);

	auto *contentSrb = _rhi->newShaderResourceBindings();
	contentSrb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			0,
			sizeof(ContentUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
	});
	if (!contentSrb->create()) {
		delete contentSrb;
		return false;
	}
	_perDrawSrbs.push_back(contentSrb);

	auto *staticPipeline = _rhi->newGraphicsPipeline();
	staticPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, argb32Vert },
		{ QRhiShaderStage::Fragment, staticContentFrag },
	});
	staticPipeline->setVertexInputLayout(inputLayout);
	staticPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	staticPipeline->setShaderResourceBindings(contentSrb);
	staticPipeline->setRenderPassDescriptor(rpDesc);
	if (!staticPipeline->create()) {
		delete staticPipeline;
		return false;
	}
	_staticContentPipeline = staticPipeline;

	auto *staticBlendPipeline = _rhi->newGraphicsPipeline();
	staticBlendPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, argb32Vert },
		{ QRhiShaderStage::Fragment, staticContentFrag },
	});
	staticBlendPipeline->setVertexInputLayout(inputLayout);
	staticBlendPipeline->setTargetBlends({ blend });
	staticBlendPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	staticBlendPipeline->setShaderResourceBindings(contentSrb);
	staticBlendPipeline->setRenderPassDescriptor(rpDesc);
	if (!staticBlendPipeline->create()) {
		delete staticBlendPipeline;
		return false;
	}
	_staticContentBlendPipeline = staticBlendPipeline;

	auto *transparentSrb = _rhi->newShaderResourceBindings();
	transparentSrb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			0,
			sizeof(TransparentContentUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
	});
	if (!transparentSrb->create()) {
		delete transparentSrb;
		return false;
	}
	_perDrawSrbs.push_back(transparentSrb);

	auto *transparentPipeline = _rhi->newGraphicsPipeline();
	transparentPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, argb32Vert },
		{ QRhiShaderStage::Fragment, transparentContentFrag },
	});
	transparentPipeline->setVertexInputLayout(inputLayout);
	transparentPipeline->setTargetBlends({ blend });
	transparentPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	transparentPipeline->setShaderResourceBindings(transparentSrb);
	transparentPipeline->setRenderPassDescriptor(rpDesc);
	if (!transparentPipeline->create()) {
		delete transparentPipeline;
		return false;
	}
	_transparentContentPipeline = transparentPipeline;

	const auto fillVert = LoadShader(u"fill.vert"_q);
	const auto roundedCornersFrag = LoadShader(u"rounded_corners.frag"_q);

	auto *roundedSrb = _rhi->newShaderResourceBindings();
	roundedSrb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			0,
			sizeof(RoundedCornersUniforms)),
	});
	if (!roundedSrb->create()) {
		delete roundedSrb;
		return false;
	}
	_perDrawSrbs.push_back(roundedSrb);

	QRhiVertexInputLayout fillInputLayout;
	fillInputLayout.setBindings({ { 2 * sizeof(float) } });
	fillInputLayout.setAttributes({
		{ 0, 0, QRhiVertexInputAttribute::Float2, 0 },
	});

	QRhiGraphicsPipeline::TargetBlend cornerBlend;
	cornerBlend.enable = true;
	cornerBlend.srcColor = QRhiGraphicsPipeline::Zero;
	cornerBlend.dstColor = QRhiGraphicsPipeline::SrcAlpha;
	cornerBlend.srcAlpha = QRhiGraphicsPipeline::Zero;
	cornerBlend.dstAlpha = QRhiGraphicsPipeline::SrcAlpha;

	auto *roundedPipeline = _rhi->newGraphicsPipeline();
	roundedPipeline->setShaderStages({
		{ QRhiShaderStage::Vertex, fillVert },
		{ QRhiShaderStage::Fragment, roundedCornersFrag },
	});
	roundedPipeline->setVertexInputLayout(fillInputLayout);
	roundedPipeline->setTargetBlends({ cornerBlend });
	roundedPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
	roundedPipeline->setShaderResourceBindings(roundedSrb);
	roundedPipeline->setRenderPassDescriptor(rpDesc);
	if (!roundedPipeline->create()) {
		delete roundedPipeline;
		return false;
	}
	_roundedCornersPipeline = roundedPipeline;

	const auto yuv420Frag = LoadShader(u"yuv420_content.frag"_q);
	const auto nv12Frag = LoadShader(u"nv12_content.frag"_q);

	auto *yuv420Srb = _rhi->newShaderResourceBindings();
	yuv420Srb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			0,
			sizeof(ContentUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
		QRhiShaderResourceBinding::sampledTexture(
			3,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
		QRhiShaderResourceBinding::sampledTexture(
			4,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
	});
	if (!yuv420Srb->create()) {
		delete yuv420Srb;
		return false;
	}
	_perDrawSrbs.push_back(yuv420Srb);

	auto createYuvPipeline = [&](const QShader &frag, bool blending,
			QRhiShaderResourceBindings *srb) {
		auto *pipeline = _rhi->newGraphicsPipeline();
		pipeline->setShaderStages({
			{ QRhiShaderStage::Vertex, argb32Vert },
			{ QRhiShaderStage::Fragment, frag },
		});
		pipeline->setVertexInputLayout(inputLayout);
		if (blending) {
			pipeline->setTargetBlends({ blend });
		}
		pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
		pipeline->setShaderResourceBindings(srb);
		pipeline->setRenderPassDescriptor(rpDesc);
		if (!pipeline->create()) {
			delete pipeline;
			return static_cast<QRhiGraphicsPipeline*>(nullptr);
		}
		return pipeline;
	};

	_yuv420Pipeline = createYuvPipeline(yuv420Frag, false, yuv420Srb);
	_yuv420BlendPipeline = createYuvPipeline(yuv420Frag, true, yuv420Srb);

	auto *nv12Srb = _rhi->newShaderResourceBindings();
	nv12Srb->setBindings({
		QRhiShaderResourceBinding::uniformBuffer(
			0,
			QRhiShaderResourceBinding::VertexStage
				| QRhiShaderResourceBinding::FragmentStage,
			_uniformBuffer,
			0,
			sizeof(ContentUniforms)),
		QRhiShaderResourceBinding::sampledTexture(
			1,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
		QRhiShaderResourceBinding::sampledTexture(
			2,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
		QRhiShaderResourceBinding::sampledTexture(
			3,
			QRhiShaderResourceBinding::FragmentStage,
			_placeholderTexture,
			_sampler),
	});
	if (!nv12Srb->create()) {
		delete nv12Srb;
		return false;
	}
	_perDrawSrbs.push_back(nv12Srb);

	_nv12Pipeline = createYuvPipeline(nv12Frag, false, nv12Srb);
	_nv12BlendPipeline = createYuvPipeline(nv12Frag, true, nv12Srb);
	return _yuv420Pipeline && _yuv420BlendPipeline
		&& _nv12Pipeline && _nv12BlendPipeline;
}

} // namespace Media::View

#endif
