// SPDX-License-Identifier: MPL-2.0

#include "features/brush/rendering/WetPigmentGlsl.h"
#include "features/brush/rendering/WetShaderSources.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>

using aether::wet_pigment_gpu::kLatentGlsl;

TEST_CASE("Wet pigment GLSL exposes the complete latent reservoir contract", "[pigment][gpu]")
{
    constexpr std::array<std::string_view, 6> samplers { "uPigmentLut0", "uPigmentLut1",
        "uReservoirPigments0", "uReservoirPigments1", "uReservoirCorrectionAndAlpha",
        "uReservoirColorMoments" };
    for (const auto sampler : samplers)
        REQUIRE(kLatentGlsl.find(sampler) != std::string_view::npos);
}

TEST_CASE("Wet pigment GLSL implements encode mix and decode", "[pigment][gpu]")
{
    REQUIRE(kLatentGlsl.find("struct WetLatent") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("WetLatent wetEncode(") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("srgb.bgr * (lutSize - vec3(1.0))") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("texture(uPigmentLut0, lutUv)") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("WetLatent wetSampleReservoir(") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("WetLatent wetMix4(") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("WetLatent wetMixPremultiplied(") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("vec4 wetDecodePremultiplied(") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("colorSecondMoment") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("WetLatent wetNormalize(") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("WetLatent wetZero(") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("void wetWritePlanes(") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("isnan(") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("isinf(") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("int sample =") == std::string_view::npos);
    REQUIRE(kLatentGlsl.find("if (pigment == 1) return 0.018;") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("if (alpha <= 1.0e-6) return wetZero();") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("storedPigments0 * inverseAlpha") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("latent.pigments0 * alpha") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("correctionAndAlpha = vec4(0.0, 0.0, 0.0, alpha)")
        != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("latent.colorMean * alpha") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("latent.colorSecondMoment * alpha") != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("mix(pigmentLinear, latent.colorMean, endpointWeight)")
        != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("const float endpointVarianceNoiseFloor = 1.0e-3;")
        != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("variance - endpointVarianceNoiseFloor")
        != std::string_view::npos);
    REQUIRE(kLatentGlsl.find("endpointWeight * latent.correction") == std::string_view::npos);
    REQUIRE(kLatentGlsl.find("linear - wetDecodePigmentsLinear(latent)") == std::string_view::npos);
}

TEST_CASE("Wet pickup variants share one four-plane latent update", "[pigment][gpu]")
{
    using namespace aether::wet_pigment_gpu;
    REQUIRE(kWetPickupUpdateGlsl.find("WetLatent wetPickupAtRate(") != std::string_view::npos);
    REQUIRE(kWetPickupUpdateGlsl.find("WetLatent wetPickupUpdate(") != std::string_view::npos);
    REQUIRE(kWetPickupUpdateGlsl.find("wetMix4(") != std::string_view::npos);
    REQUIRE(kWetPerDabPickupMain.find("wetPickupUpdate(") != std::string_view::npos);
    REQUIRE(kWetBatchedPickupMain.find("wetPickupUpdate(") != std::string_view::npos);
    REQUIRE(kWetPickupUpdateGlsl.find("if (uUsePen == 0)") != std::string_view::npos);
    REQUIRE(kWetPickupUpdateGlsl.find("mix(previous.alpha, canvas.alpha, pickup)")
        != std::string_view::npos);
    REQUIRE(kWetPerDabPickupPreamble.find("uniform int uUsePen") != std::string_view::npos);
    REQUIRE(kWetBatchedPickupPreamble.find("uniform int uUsePen") != std::string_view::npos);
    REQUIRE(kWetPerDabPickupPreamble.find("uniform int uCanvasIsRgba8")
        != std::string_view::npos);
    REQUIRE(kWetBatchedPickupPreamble.find("uniform int uCanvasIsRgba8")
        != std::string_view::npos);
    REQUIRE(kWetCanvasSamplingGlsl.find("vec4 wetResolveRgba8CanvasPremultiplied(")
        != std::string_view::npos);
    REQUIRE(kWetCanvasSamplingGlsl.find("const float fullyTrustedAlpha = 64.5 / 255.0;")
        != std::string_view::npos);
    REQUIRE(kWetCanvasSamplingGlsl.find("const float minimumCoverageMass = 24.5 / 255.0;")
        != std::string_view::npos);
    REQUIRE(kWetCanvasSamplingGlsl.find("smoothstep(") != std::string_view::npos);
    REQUIRE(kWetCanvasSamplingGlsl.find("straightColor * center.a, center.a")
        != std::string_view::npos);
    REQUIRE(kWetCanvasSamplingGlsl.find(
                "return center.a >= minimumCenterFallbackAlpha ? center : vec4(0.0);")
        != std::string_view::npos);
    REQUIRE(kWetPerDabPickupMain.find("wetResolveRgba8CanvasPremultiplied(")
        != std::string_view::npos);
    REQUIRE(kWetBatchedPickupMain.find("wetResolveRgba8CanvasPremultiplied(")
        != std::string_view::npos);
    REQUIRE(kWetPerDabPickupMain.find("if (uCanvasIsRgba8 != 0)")
        != std::string_view::npos);
    REQUIRE(kWetBatchedPickupMain.find("if (uCanvasIsRgba8 != 0)")
        != std::string_view::npos);
    REQUIRE(kWetPerDabPickupMain.find("uInit != 0 && uCanvasIsRgba8")
        == std::string_view::npos);
    REQUIRE(kWetBatchedPickupMain.find("uInit != 0 && uCanvasIsRgba8")
        == std::string_view::npos);
    REQUIRE(kWetPerDabPickupMain.find("wetPickupUpdate(previous, previous, canvas)")
        != std::string_view::npos);
    REQUIRE(kWetBatchedPickupMain.find("wetPickupUpdate(previous, previous, canvas)")
        != std::string_view::npos);
    REQUIRE(kWetPickupUpdateGlsl.find("float coverageFill") != std::string_view::npos);
    REQUIRE(kWetPickupUpdateGlsl.find("penWeight +=") == std::string_view::npos);
    REQUIRE(kWetPickupUpdateGlsl.find("resolved.alpha = min(resolvedAlpha + coverageFill")
        != std::string_view::npos);
    REQUIRE(kWetPickupUpdateGlsl.find("float coverageFill")
        < kWetPickupUpdateGlsl.find("float previousBase"));
    REQUIRE(kWetPickupUpdateGlsl.find("if (!(resolvedAlpha > 1.0e-8)) return wetZero();")
        != std::string_view::npos);
    // A new reservoir must use the same premultiplied-alpha weighting as every
    // later pickup. The removed bespoke initializer adjusted spread by alpha and
    // then multiplied the canvas contribution by alpha a second time, producing
    // a dark transient only at the partially covered boundary of two strokes.
    REQUIRE(kWetPickupUpdateGlsl.find("wetPickupAtRate(wetZero(), wetZero(), canvas, 1.0)")
        != std::string_view::npos);
    REQUIRE(kWetPickupUpdateGlsl.find("spread += (1.0 - spread) * (1.0 - canvas.alpha)")
        == std::string_view::npos);
    for (int location = 0; location < 4; ++location) {
        const std::string declaration = "layout(location = " + std::to_string(location) + ") out";
        REQUIRE(kWetPickupOutputsGlsl.find(declaration) != std::string_view::npos);
    }
}

TEST_CASE("RGBA8 coverage edge does not inject pen pigment", "[pigment][gpu][regression]")
{
    // At the first non-zero texel of a committed stroke edge, previous and
    // canvas both track the same partially covered paint. These values mirror
    // Wet Brush 1's 4% spread and a small spacing-normalized pickup. Treating
    // the alpha shortfall as pen pigment made alpha=1/255 already 72% pen and
    // forced every enter/exit transition through a dark complementary mix.
    constexpr float pickup = 0.01f;
    constexpr float spread = 0.04f;
    constexpr float edgeAlpha = 1.0f / 255.0f;
    constexpr float canvasWeight = pickup * (1.0f - spread) * edgeAlpha;
    constexpr float penWeight = pickup * spread;
    constexpr float previousBase = 1.0f - penWeight - canvasWeight;
    constexpr float previousWeight = previousBase * edgeAlpha;
    constexpr float resolvedAlpha = previousWeight + canvasWeight + penWeight;
    constexpr float penFraction = penWeight / resolvedAlpha;

    REQUIRE(penFraction < 0.10f);

    constexpr float incorrectPenRefill = pickup - canvasWeight - penWeight;
    constexpr float incorrectPenFraction = (penWeight + incorrectPenRefill)
        / (previousWeight + canvasWeight + penWeight + incorrectPenRefill);
    REQUIRE(incorrectPenFraction > 0.70f);
}

TEST_CASE("Wet apply variants decode four planes into one canvas output", "[pigment][gpu]")
{
    using namespace aether::wet_pigment_gpu;
    REQUIRE(kWetPerDabApplyPreamble.find("layout(location = 0) out vec4 outColor")
        != std::string_view::npos);
    REQUIRE(kWetBatchedApplyPreamble.find("layout(location = 0) out vec4 outColor")
        != std::string_view::npos);
    REQUIRE(kWetPerDabApplyMain.find("wetSampleReservoir(") != std::string_view::npos);
    REQUIRE(kWetBatchedApplyMain.find("wetSampleReservoir(") != std::string_view::npos);
    REQUIRE(kWetPerDabApplyMain.find("wetDecodePremultiplied(") != std::string_view::npos);
    REQUIRE(kWetBatchedApplyMain.find("wetDecodePremultiplied(") != std::string_view::npos);
    REQUIRE(kWetPerDabApplyPreamble.find("outPigments0") == std::string_view::npos);
    REQUIRE(kWetBatchedApplyPreamble.find("outPigments0") == std::string_view::npos);
    REQUIRE(
        kWetPerDabApplyPreamble.find("uniform int uPreserveCanvasAlpha") != std::string_view::npos);
    REQUIRE(kWetBatchedApplyPreamble.find("uniform int uPreserveCanvasAlpha")
        != std::string_view::npos);
    REQUIRE(kWetPerDabApplyPreamble.find("uniform int uQuantizeTo8Bit") != std::string_view::npos);
    REQUIRE(kWetBatchedApplyPreamble.find("uniform int uQuantizeTo8Bit") != std::string_view::npos);
    REQUIRE(kWetPerDabApplyPreamble.find("uniform int uCanvasIsRgba8")
        != std::string_view::npos);
    REQUIRE(kWetBatchedApplyPreamble.find("uniform int uCanvasIsRgba8")
        != std::string_view::npos);
    REQUIRE(kWetBatchedApplyPreamble.find("uniform vec2 uMaxValidUv")
        != std::string_view::npos);
    REQUIRE(kWetPerDabApplyMain.find("wetResolveRgba8CanvasPremultiplied(")
        != std::string_view::npos);
    REQUIRE(kWetBatchedApplyMain.find("wetResolveRgba8CanvasPremultiplied(")
        != std::string_view::npos);
    REQUIRE(kWetBatchedApplyMain.find(
                "if (falloff <= 0.0) { outColor = originalCanvas; return; }")
        != std::string_view::npos);
    REQUIRE(kWetBatchedApplyMain.find(
                "if (maskScale <= 0.0) { outColor = originalCanvas; return; }")
        != std::string_view::npos);
    REQUIRE(kWetApplyCoverageGlsl.find("deposited.a < canvas.a") != std::string_view::npos);
    REQUIRE(kWetApplyCoverageGlsl.find("straightColor * canvas.a") != std::string_view::npos);
    REQUIRE(kWetApplyCoverageGlsl.find("color.rgb = floor") == std::string_view::npos);
    REQUIRE(kWetApplyCoverageGlsl.find("straightColor * newAlpha") != std::string_view::npos);
    REQUIRE(kWetPerDabApplyMain.find("wetPreserveCanvasAlpha(wetDitherPremultiplied(")
        != std::string_view::npos);
    REQUIRE(kWetBatchedApplyMain.find("wetPreserveCanvasAlpha(wetDitherPremultiplied(")
        != std::string_view::npos);
    REQUIRE(kWetPickupUpdateGlsl.find("pigmentMixPremult") == std::string_view::npos);
    REQUIRE(kWetApplyCoverageGlsl.find("pigmentMixPremult") == std::string_view::npos);
}

TEST_CASE("Wet apply variants use Brush Editor texture grain", "[pigment][gpu][texture]")
{
    using namespace aether::wet_pigment_gpu;
    for (const auto preamble : { kWetPerDabApplyPreamble, kWetBatchedApplyPreamble }) {
        REQUIRE(preamble.find("uniform sampler2D uTextureTile") != std::string_view::npos);
        REQUIRE(preamble.find("uniform int uUseTexture") != std::string_view::npos);
        REQUIRE(preamble.find("uniform vec2 uInvTextureSize") != std::string_view::npos);
        REQUIRE(preamble.find("uniform float uTextureAmount") != std::string_view::npos);
        REQUIRE(preamble.find("uniform float uTextureEdgeBoost") != std::string_view::npos);
    }
    REQUIRE(kWetApplyCoverageGlsl.find("float wetApplyTextureShaping(")
        != std::string_view::npos);
    REQUIRE(kWetApplyCoverageGlsl.find("texture(uTextureTile, textureUv).r")
        != std::string_view::npos);
    REQUIRE(kWetApplyCoverageGlsl.find("uTextureContrast * 2.5") != std::string_view::npos);
    REQUIRE(kWetApplyCoverageGlsl.find("uTextureDepth * (1.0 - g)")
        != std::string_view::npos);
    REQUIRE(kWetApplyCoverageGlsl.find("uTextureBlend * (depthMix * depthMix)")
        != std::string_view::npos);
    REQUIRE(kWetApplyCoverageGlsl.find("edgeFactor * uTextureEdgeBoost * 8.0")
        != std::string_view::npos);
    REQUIRE(kWetPerDabApplyMain.find("wetTextureFactor(fragPixelCoord * uInvTextureSize")
        != std::string_view::npos);
    REQUIRE(kWetBatchedApplyMain.find("wetTextureFactor(fragPixelCoord * uInvTextureSize")
        != std::string_view::npos);
    REQUIRE(kWetPerDabApplyMain.find("falloff *= textureFactor") != std::string_view::npos);
    REQUIRE(kWetBatchedApplyMain.find("falloff *= textureFactor") != std::string_view::npos);
}
