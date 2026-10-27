#pragma once

#include "calls/group/calls_group_viewport_rhi.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0) && !defined DESKTOP_APP_DISABLE_PRIVATE_QT_API

namespace Calls::Group::RhiDetails {

// The NDC Y flip stays at offset 12 in every block, see NdcFlipY().
struct GroupFrameUniforms {
	float viewport[2];
	float _pad0;
	float flipY;
	float frameBg[4];
	float shadow[4];
	float paused;
	float _pad1[3];
	float roundRect[4];
	float radiusOutline[2];
	float _pad2[2];
	float roundBg[4];
	float outlineFg[4];
};
static_assert(sizeof(GroupFrameUniforms) == 128);

struct BlurUniforms {
	float texelOffset;
	float _pad[3];
};
static_assert(sizeof(BlurUniforms) == 16);

struct ImageUniforms {
	float viewport[2];
	float g_opacity;
	float flipY;
};
static_assert(sizeof(ImageUniforms) == 16);

} // namespace Calls::Group::RhiDetails

#endif
