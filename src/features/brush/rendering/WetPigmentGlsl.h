// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <string_view>

namespace aether::wet_pigment_gpu {

// Shared by both wet pickup shaders and both wet apply shaders. The four vec4s
// map one-to-one to ReservoirPlane in WetPigmentGpuLayout.h. WetLatent values
// are straight in shader code; wetWritePlanes/wetSampleReservoir convert to
// and from the alpha-premultiplied texture representation used for filtering.
inline constexpr std::string_view kLatentGlsl = R"glsl(
uniform sampler3D uPigmentLut0;
uniform sampler3D uPigmentLut1;
uniform sampler2D uReservoirPigments0;
uniform sampler2D uReservoirPigments1;
uniform sampler2D uReservoirCorrectionAndAlpha;
uniform sampler2D uReservoirColorMoments;

struct WetLatent {
    vec4 pigments0;
    vec4 pigments1;
    // Reserved to keep the established four-plane reservoir layout stable.
    vec3 correction;
    vec3 colorMean;
    float colorSecondMoment;
    float alpha;
};

float wetFinite(float value, float fallbackValue) {
    return (isnan(value) || isinf(value)) ? fallbackValue : value;
}
vec3 wetFinite(vec3 value, vec3 fallbackValue) {
    return vec3(wetFinite(value.x, fallbackValue.x), wetFinite(value.y, fallbackValue.y),
        wetFinite(value.z, fallbackValue.z));
}
vec4 wetFinite(vec4 value, vec4 fallbackValue) {
    return vec4(wetFinite(value.x, fallbackValue.x), wetFinite(value.y, fallbackValue.y),
        wetFinite(value.z, fallbackValue.z), wetFinite(value.w, fallbackValue.w));
}

WetLatent wetZero() {
    WetLatent latent;
    latent.pigments0 = vec4(1.0, 0.0, 0.0, 0.0);
    latent.pigments1 = vec4(0.0);
    latent.correction = vec3(0.0);
    latent.colorMean = vec3(0.0);
    latent.colorSecondMoment = 0.0;
    latent.alpha = 0.0;
    return latent;
}

WetLatent wetNormalize(WetLatent latent) {
    latent.pigments0 = max(wetFinite(latent.pigments0, vec4(0.0)), vec4(0.0));
    latent.pigments1 = max(wetFinite(latent.pigments1, vec4(0.0)), vec4(0.0));
    float total = dot(latent.pigments0, vec4(1.0)) + dot(latent.pigments1, vec4(1.0));
    if (!(total > 1.0e-8)) {
        latent.pigments0 = vec4(1.0, 0.0, 0.0, 0.0);
        latent.pigments1 = vec4(0.0);
    } else {
        latent.pigments0 /= total;
        latent.pigments1 /= total;
    }
    latent.correction = vec3(0.0);
    latent.colorMean = wetFinite(latent.colorMean, vec3(0.0));
    latent.colorSecondMoment = max(wetFinite(latent.colorSecondMoment, 0.0), 0.0);
    latent.alpha = clamp(wetFinite(latent.alpha, 0.0), 0.0, 1.0);
    return latent;
}

float wetSrgbToLinear(float c) {
    c = clamp(c, 0.0, 1.0);
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
float wetLinearToSrgb(float c) {
    c = clamp(c, 0.0, 1.0);
    return c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}
vec3 wetSrgbToLinear(vec3 c) {
    return vec3(wetSrgbToLinear(c.r), wetSrgbToLinear(c.g), wetSrgbToLinear(c.b));
}
vec3 wetLinearToSrgb(vec3 c) {
    return vec3(wetLinearToSrgb(c.r), wetLinearToSrgb(c.g), wetLinearToSrgb(c.b));
}

float wetGaussian(float wavelength, float center, float width) {
    float x = (wavelength - center) / width;
    return exp(-0.5 * x * x);
}
float wetLogistic(float wavelength, float center, float width) {
    return 1.0 / (1.0 + exp(-(wavelength - center) / width));
}
float wetPigmentReflectance(int pigment, float wavelength) {
    if (pigment == 0) return 0.94;
    // Keep virtual black physically finite. The endpoint colorMean anchor still
    // renders exact RGB black, while a small reconstructed black fraction no
    // longer contributes an effectively infinite K/S value to later mixtures.
    if (pigment == 1) return 0.018;
    if (pigment == 2) return 0.035 + 0.90 * wetLogistic(wavelength, 585.0, 18.0);
    if (pigment == 3) return 0.025 + 0.88 * wetGaussian(wavelength, 535.0, 43.0);
    if (pigment == 4) return 0.025 + 0.86 * wetGaussian(wavelength, 455.0, 40.0);
    if (pigment == 5) return 0.035 + 0.89 * wetLogistic(wavelength, 500.0, 17.0);
    if (pigment == 6) return 0.91 - 0.84 * wetGaussian(wavelength, 545.0, 48.0);
    return 0.91 - 0.86 * wetLogistic(wavelength, 590.0, 22.0);
}

const float WET_X[16] = float[](0.0002,0.0082,0.1285,0.3362,0.2908,0.0956,0.0049,0.0633,0.2904,0.5945,0.9163,1.0559,0.8544,0.4464,0.1649,0.0468);
const float WET_Y[16] = float[](0.0003,0.0024,0.0258,0.0937,0.1813,0.3362,0.6182,0.8800,0.9918,0.9355,0.7100,0.4508,0.2383,0.1016,0.0341,0.0087);
const float WET_Z[16] = float[](0.0067,0.0380,0.3856,1.3898,1.7074,0.8129,0.2720,0.0822,0.0203,0.0045,0.0010,0.0002,0.0,0.0,0.0,0.0);
const float WET_D65[16] = float[](49.98,82.75,93.43,104.87,117.81,115.92,109.35,104.79,104.41,100.0,95.79,90.01,87.70,83.70,80.21,78.28);

float wetPigmentAt(WetLatent latent, int index) {
    return index < 4 ? latent.pigments0[index] : latent.pigments1[index - 4];
}
vec3 wetDecodePigmentsLinear(WetLatent latent) {
    latent = wetNormalize(latent);
    float normalization = 0.0;
    vec3 xyz = vec3(0.0);
    for (int sampleIndex = 0; sampleIndex < 16; ++sampleIndex) {
        float wavelength = 380.0 + 350.0 * float(sampleIndex) / 15.0;
        float ks = 0.0;
        for (int pigment = 0; pigment < 8; ++pigment) {
            float rho = clamp(wetPigmentReflectance(pigment, wavelength), 1.0e-4, 1.0);
            ks += wetPigmentAt(latent, pigment)
                * (1.0 - rho) * (1.0 - rho) / (2.0 * rho);
        }
        float rho = clamp(1.0 + ks - sqrt(ks * ks + 2.0 * ks), 1.0e-4, 1.0);
        normalization += WET_Y[sampleIndex] * WET_D65[sampleIndex];
        xyz += vec3(WET_X[sampleIndex], WET_Y[sampleIndex], WET_Z[sampleIndex])
            * WET_D65[sampleIndex] * rho;
    }
    xyz /= normalization;
    return vec3(3.2406255 * xyz.x - 1.5372080 * xyz.y - 0.4986286 * xyz.z,
        -0.9689307 * xyz.x + 1.8757561 * xyz.y + 0.0415175 * xyz.z,
        0.0557101 * xyz.x - 0.2040211 * xyz.y + 1.0569959 * xyz.z);
}

WetLatent wetEncode(vec3 srgb, float alpha) {
    WetLatent latent = wetZero();
    srgb = clamp(srgb, 0.0, 1.0);
    // PigmentLut stores blue as the fastest-changing index. OpenGL 3D textures
    // use X as the fastest-changing coordinate, hence BGR texture coordinates.
    // LUT samples live at texel centers for values i/(N-1), not at OpenGL's
    // normalized endpoints. Map the value domain to those centers so the GPU
    // interpolation matches PigmentLut::sample() on the CPU.
    vec3 lutSize = vec3(textureSize(uPigmentLut0, 0));
    vec3 lutUv = (srgb.bgr * (lutSize - vec3(1.0)) + vec3(0.5)) / lutSize;
    latent.pigments0 = texture(uPigmentLut0, lutUv);
    latent.pigments1 = texture(uPigmentLut1, lutUv);
    vec3 linear = wetSrgbToLinear(srgb);
    latent.colorMean = linear;
    latent.colorSecondMoment = dot(linear, linear);
    latent.alpha = clamp(alpha, 0.0, 1.0);
    return wetNormalize(latent);
}
WetLatent wetEncodePremultiplied(vec4 color) {
    float alpha = clamp(color.a, 0.0, 1.0);
    if (alpha <= 1.0e-6) return wetZero();
    return wetEncode(color.rgb / alpha, alpha);
}
WetLatent wetSampleReservoir(vec2 uv) {
    vec4 storedPigments0 = texture(uReservoirPigments0, uv);
    vec4 storedPigments1 = texture(uReservoirPigments1, uv);
    vec4 storedCorrectionAndAlpha = texture(uReservoirCorrectionAndAlpha, uv);
    vec4 storedColorMoments = texture(uReservoirColorMoments, uv);
    float alpha = clamp(wetFinite(storedCorrectionAndAlpha.a, 0.0), 0.0, 1.0);
    if (alpha <= 1.0e-6) return wetZero();

    // Reservoir planes are stored alpha-premultiplied. This is required for
    // GL_LINEAR: transparent texels must not contribute hidden pigments or
    // moments when the GPU filters across a coverage edge.
    float inverseAlpha = 1.0 / alpha;
    WetLatent latent = wetZero();
    latent.pigments0 = storedPigments0 * inverseAlpha;
    latent.pigments1 = storedPigments1 * inverseAlpha;
    latent.colorMean = storedColorMoments.rgb * inverseAlpha;
    latent.colorSecondMoment = storedColorMoments.a * inverseAlpha;
    latent.alpha = alpha;
    return wetNormalize(latent);
}
WetLatent wetMix4(WetLatent a, float wa, WetLatent b, float wb,
    WetLatent c, float wc, WetLatent d, float wd, float alpha) {
    a = wetNormalize(a);
    b = wetNormalize(b);
    c = wetNormalize(c);
    d = wetNormalize(d);
    wa = max(wetFinite(wa, 0.0), 0.0);
    wb = max(wetFinite(wb, 0.0), 0.0);
    wc = max(wetFinite(wc, 0.0), 0.0);
    wd = max(wetFinite(wd, 0.0), 0.0);
    float total = wa + wb + wc + wd;
    if (!(total > 1.0e-8)) {
        WetLatent empty = wetZero();
        empty.alpha = clamp(wetFinite(alpha, 0.0), 0.0, 1.0);
        return empty;
    }
    WetLatent outLatent;
    outLatent.pigments0 = (wa*a.pigments0 + wb*b.pigments0 + wc*c.pigments0 + wd*d.pigments0) / total;
    outLatent.pigments1 = (wa*a.pigments1 + wb*b.pigments1 + wc*c.pigments1 + wd*d.pigments1) / total;
    outLatent.correction = vec3(0.0);
    outLatent.colorMean = (wa*a.colorMean + wb*b.colorMean + wc*c.colorMean + wd*d.colorMean) / total;
    outLatent.colorSecondMoment = (wa*a.colorSecondMoment + wb*b.colorSecondMoment + wc*c.colorSecondMoment + wd*d.colorSecondMoment) / total;
    outLatent.alpha = clamp(alpha, 0.0, 1.0);
    return wetNormalize(outLatent);
}
WetLatent wetMixPremultiplied(WetLatent first, WetLatent second, float amount) {
    float t = clamp(amount, 0.0, 1.0);
    float alpha = mix(first.alpha, second.alpha, t);
    return wetMix4(first, (1.0 - t) * first.alpha,
        second, t * second.alpha, first, 0.0, second, 0.0, alpha);
}
vec4 wetDecodePremultiplied(WetLatent latent) {
    latent = wetNormalize(latent);
    if (latent.alpha <= 1.0e-6) return vec4(0.0);
    float meanSquared = dot(latent.colorMean, latent.colorMean);
    float variance = max(latent.colorSecondMoment - meanSquared, 0.0);
    vec3 pigmentLinear = wetDecodePigmentsLinear(latent);
    float endpointWeight = exp(-8.0 * variance);
    // A residual computed at an endpoint is not valid after nonlinear spectral
    // mixing. The stored linear-RGB mean is affine and provides the exact
    // endpoint anchor without accumulating a dark correction bias.
    vec3 linear = mix(pigmentLinear, latent.colorMean, endpointWeight);
    vec3 srgb = wetLinearToSrgb(linear);
    vec4 result = vec4(min(srgb * latent.alpha, vec3(latent.alpha)), latent.alpha);
    return wetFinite(result, vec4(0.0));
}
void wetWritePlanes(WetLatent latent, out vec4 pigments0, out vec4 pigments1,
    out vec4 correctionAndAlpha, out vec4 colorMoments) {
    latent = wetNormalize(latent);
    float alpha = latent.alpha;
    pigments0 = latent.pigments0 * alpha;
    pigments1 = latent.pigments1 * alpha;
    correctionAndAlpha = vec4(0.0, 0.0, 0.0, alpha);
    colorMoments = vec4(latent.colorMean * alpha, latent.colorSecondMoment * alpha);
}
)glsl";

} // namespace aether::wet_pigment_gpu
