#pragma once

#include "media/view/media_view_overlay_rhi.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0) && !defined DESKTOP_APP_DISABLE_PRIVATE_QT_API

namespace Media::View::RhiDetails {

// The NDC Y flip stays at offset 12 in every block, see NdcFlipY().
struct ImageUniforms {
	float viewport[2];
	float g_opacity;
	float flipY;
};
static_assert(sizeof(ImageUniforms) % 16 == 0);

struct ContentUniforms {
	float viewport[2];
	float fragCoordYUp;
	float flipY;
	float shadowTopRect[4];
	float shadowBottomSkipOpacityFullFade[4];
	float roundRect[4];
	float roundRadius;
	float g_opacity;
	float _pad1[2];
};
static_assert(sizeof(ContentUniforms) == 80);

struct TransparentContentUniforms {
	float viewport[2];
	float fragCoordYUp;
	float flipY;
	float shadowTopRect[4];
	float shadowBottomSkipOpacityFullFade[4];
	float transparentBg[4];
	float transparentFg[4];
	float transparentSize;
	float g_opacity;
	float _pad1[2];
};
static_assert(sizeof(TransparentContentUniforms) == 96);

struct RoundedCornersUniforms {
	float viewport[2];
	float fragCoordYUp;
	float flipY;
	float roundRect[4];
	float roundRadius;
	float _pad1[3];
};
static_assert(sizeof(RoundedCornersUniforms) == 48);

} // namespace Media::View::RhiDetails

#endif
