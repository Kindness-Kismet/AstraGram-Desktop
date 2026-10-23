#version 450

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Params {
	vec2 viewport;
	// 1.0 when gl_FragCoord.y counts from the bottom (OpenGL).
	float fragCoordYUp;
	vec4 roundRect;
	float roundRadius;
};

// 弧线比宿主圆角向外多盖半个像素，压住系统边框内沿漏出的宿主内容；
// 直边仍按原矩形裁齐，不盖住边框。
float hostCorner() {
	const float overlap = 0.5;
	float fragY = (fragCoordYUp > 0.0)
		? gl_FragCoord.y
		: (viewport.y - gl_FragCoord.y);
	vec2 fragCoord = vec2(gl_FragCoord.x, fragY);
	vec2 rectHalf = roundRect.zw / 2.0;
	vec2 rectCenter = roundRect.xy + rectHalf;
	vec2 fromRectCenter = abs(fragCoord - rectCenter);
	vec2 edges = clamp(rectHalf + 0.5 - fromRectCenter, 0.0, 1.0);
	float radius = roundRadius + overlap;
	vec2 outerHalf = rectHalf + overlap;
	vec2 fromRoundingCenter = max(
		fromRectCenter + vec2(radius + 0.5),
		outerHalf) - outerHalf;
	float rounded = length(fromRoundingCenter) - radius;
	return edges.x * edges.y * (1.0 - smoothstep(0.0, 1.0, rounded));
}

void main() {
	fragColor = vec4(1.0) * hostCorner();
}
