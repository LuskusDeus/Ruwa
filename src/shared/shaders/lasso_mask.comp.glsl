#version 450 core
// SPDX-License-Identifier: MPL-2.0

layout(local_size_x = 16, local_size_y = 16) in;

layout(r8, binding = 0) uniform writeonly image2D uMaskImage;

layout(std430, binding = 0) readonly buffer PolygonPointBuffer {
    vec2 points[];
} uPolygonPoints;

uniform ivec2 uMaskSize;
uniform int uPointCount;

bool pointInPolygon(vec2 p) {
    bool inside = false;
    for (int i = 0, j = uPointCount - 1; i < uPointCount; j = i++) {
        vec2 a = uPolygonPoints.points[i];
        vec2 b = uPolygonPoints.points[j];
        bool crosses = ((a.y > p.y) != (b.y > p.y))
            && (p.x < ((b.x - a.x) * (p.y - a.y) / (b.y - a.y + 0.0000001)) + a.x);
        if (crosses) {
            inside = !inside;
        }
    }
    return inside;
}

float segmentDistanceSq(vec2 p, vec2 a, vec2 b) {
    vec2 ab = b - a;
    float denom = max(dot(ab, ab), 0.0000001);
    float t = clamp(dot(p - a, ab) / denom, 0.0, 1.0);
    vec2 closest = a + ab * t;
    vec2 d = p - closest;
    return dot(d, d);
}

bool nearPolygonEdge(vec2 p) {
    const float edgePadSq = 0.75 * 0.75;
    for (int i = 0, j = uPointCount - 1; i < uPointCount; j = i++) {
        vec2 a = uPolygonPoints.points[i];
        vec2 b = uPolygonPoints.points[j];
        if (segmentDistanceSq(p, a, b) <= edgePadSq) {
            return true;
        }
    }
    return false;
}

bool pixelTouchesPolygon(ivec2 pixel) {
    vec2 p = vec2(pixel) + vec2(0.5);
    return pointInPolygon(p) || nearPolygonEdge(p);
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= uMaskSize.x || pixel.y >= uMaskSize.y) {
        return;
    }

    float inside = pixelTouchesPolygon(pixel) ? 1.0 : 0.0;
    imageStore(uMaskImage, pixel, vec4(inside, 0.0, 0.0, 1.0));
}
