#version 450 core
// SPDX-License-Identifier: MPL-2.0

uniform sampler2D uBaseTexture;
uniform sampler2D uMaskTexture;
uniform sampler2D uSelectionMaskTexture;
uniform int uUseSelectionMask;
uniform int uPreserveBaseAlpha;
uniform ivec2 uViewportSize;
uniform ivec2 uMaskOrigin;
uniform ivec2 uMaskSize;
uniform vec4 uFillColor;

in vec2 fragTexCoord;
out vec4 outColor;

float sampleMask() {
    vec2 pixel = vec2(gl_FragCoord.x - 0.5,
                      float(uViewportSize.y) - gl_FragCoord.y - 0.5);
    vec2 maskPixel = pixel - vec2(uMaskOrigin);
    if (maskPixel.x < 0.0 || maskPixel.y < 0.0
        || maskPixel.x >= float(uMaskSize.x) || maskPixel.y >= float(uMaskSize.y)) {
        return 0.0;
    }

    return texelFetch(uMaskTexture, ivec2(maskPixel), 0).r;
}

void main() {
    vec4 base = texture(uBaseTexture, fragTexCoord);
    float mask = sampleMask();
    if (uUseSelectionMask != 0) {
        mask *= texture(uSelectionMaskTexture, fragTexCoord).a;
    }

    mask = clamp(mask, 0.0, 1.0);
    float baseAlpha = clamp(base.a, 0.0, 1.0);
    float fillAlpha = clamp(uFillColor.a, 0.0, 1.0) * mask;
    vec4 fillPremul = vec4(uFillColor.rgb * fillAlpha, fillAlpha);
    if (uPreserveBaseAlpha != 0) {
        vec4 fillCoverage = vec4(
            uFillColor.rgb * (fillAlpha * baseAlpha),
            fillAlpha * baseAlpha);
        outColor = vec4(
            fillCoverage.rgb + base.rgb * (1.0 - fillAlpha),
            baseAlpha);
        return;
    }

    outColor = fillPremul + base * (1.0 - fillAlpha);
}
