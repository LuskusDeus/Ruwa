// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   T I L E   B R U S H
// ==========================================================================

#ifndef RUWA_CORE_TILES_TILEBRUSH_H
#define RUWA_CORE_TILES_TILEBRUSH_H

#include "DabShapeFalloff.h"
#include "TileTypes.h"
#include "TileGrid.h"
#include "features/brush/manager/BrushSettings.h"
#include "features/layers/model/BlendModeUtils.h"
#include "shared/types/Types.h"

#include <cmath>
#include <algorithm>
#include <array>
#include <bit>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aether {

// ==========================================================================
//   T I L E   B R U S H
// ==========================================================================

class TileBrush {
public:
    struct DabPoint {
        float worldX = 0.0f;
        float worldY = 0.0f;
        float baseWorldX = 0.0f;
        float baseWorldY = 0.0f;
        float pressure = 1.0f;
        float strokeElapsedSeconds = 0.0f;
        float textureAmount = 0.0f;
        float textureScale = 1.0f;
        float textureContrast = 0.5f;
        float textureDepth = 1.0f;
        float textureBlend = 0.5f;
        float textureEdgeBoost = 0.0f;
        float startTaper = 0.0f;
        float endTaper = 0.0f;
        float postCorrection = 0.0f;
        float startCorrectionLength = 0.0f;
        float endCorrectionLength = 0.0f;
        float scatterPosition = 0.0f;
        float radius = 1.0f;
        float baseRadius = 1.0f;
        float hardness = 0.7f;
        float roundness = 1.0f;
        float angleDegrees = 0.0f;
        bool useMaxBlend = true;
        bool strokeTimeAvailable = false;
        uint8_t colorR = 255;
        uint8_t colorG = 255;
        uint8_t colorB = 255;
        uint8_t alpha = 255;
        uint8_t baseAlpha = 255;
    };

    TileBrush() = default;

    void setBrushSettings(const ruwa::core::brushes::BrushSettingsData& settings)
    {
        m_brushSettingsModel = settings;
        m_useBrushSettingsModel = true;
        refreshRandomBoundSettingMask();

        setHardness(m_brushSettingsModel.hardness);
        setSpacing(m_brushSettingsModel.spacing);
        setFlow(m_brushSettingsModel.flow);
        setFlowBlendMode(m_brushSettingsModel.flowBlendMode);
        setRoundness(m_brushSettingsModel.roundness);
        setAngleDegrees(m_brushSettingsModel.angle);
        setSizePressureEnabled(m_brushSettingsModel.sizePressureEnabled);
        setOpacityPressureEnabled(m_brushSettingsModel.opacityPressureEnabled);
        setSizePressureRange(
            m_brushSettingsModel.sizePressureMin, m_brushSettingsModel.sizePressureMax);
        setOpacityPressureRange(
            m_brushSettingsModel.opacityPressureMin, m_brushSettingsModel.opacityPressureMax);
        setFlowPressureRange(
            m_brushSettingsModel.flowPressureMin, m_brushSettingsModel.flowPressureMax);
        setStartTaper(m_brushSettingsModel.startTaper);
        setEndTaper(m_brushSettingsModel.endTaper);
        setPostCorrection(m_brushSettingsModel.postCorrection);
        setStabilization(m_brushSettingsModel.stabilization);
        setAdjustCorrectionBySpeed(m_brushSettingsModel.adjustCorrectionBySpeed);
        setStartCorrectionEnabled(m_brushSettingsModel.startCorrectionEnabled);
        setStartCorrectionLength(m_brushSettingsModel.startCorrectionLength);
        setEndCorrectionEnabled(m_brushSettingsModel.endCorrectionEnabled);
        setEndCorrectionLength(m_brushSettingsModel.endCorrectionLength);
        setTextureType(m_brushSettingsModel.textureType);
        setTextureAmount(m_brushSettingsModel.textureAmount);
        setTextureScale(m_brushSettingsModel.textureScale);
        setTextureContrast(m_brushSettingsModel.textureContrast);
        setTextureDepth(m_brushSettingsModel.textureDepth);
        setTextureBlend(m_brushSettingsModel.textureBlend);
        setTextureEdgeBoost(m_brushSettingsModel.textureEdgeBoost);
        setDabType(m_brushSettingsModel.dabType);
        setDabCustomImagePath(m_brushSettingsModel.dabCustomImagePath);
        setDabXScale(m_brushSettingsModel.dabXScale);
        setDabYScale(m_brushSettingsModel.dabYScale);
        setDabRotation(m_brushSettingsModel.dabRotation);
        setDabThreshold(m_brushSettingsModel.dabThreshold);
        setDabCompression(m_brushSettingsModel.dabCompression);
        setDabInterpolation(m_brushSettingsModel.dabInterpolation);
        setScatterPosition(m_brushSettingsModel.scatterPosition);
        setBrushFeather(m_brushSettingsModel.brushFeather);
        setStrokeBlendMode(m_brushSettingsModel.strokeBlendMode);
        setWetMix(m_brushSettingsModel.wetMix);
        setColorBlending(m_brushSettingsModel.colorBlending);
        setColorDilution(m_brushSettingsModel.colorDilution);
        setColorSpread(m_brushSettingsModel.colorSpread);
        setColorLength(m_brushSettingsModel.colorLength);
        setColorWetFlow(m_brushSettingsModel.colorWetFlow);
        setColorDryRate(m_brushSettingsModel.colorDryRate);
        setColorBuildup(m_brushSettingsModel.colorBuildup);
    }

    void setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
    {
        m_r = r;
        m_g = g;
        m_b = b;
        m_a = a;
    }

    void setRadius(float radius) { m_radius = std::max(0.5f, radius); }

    void setHardness(float hardness) { m_hardness = std::clamp(hardness, 0.0f, 1.0f); }

    void setSpacing(float spacing) { m_spacing = ruwa::core::brushes::clampSpacingValue(spacing); }

    void setFlow(float flow) { m_flow = std::clamp(flow, 0.0f, 1.0f); }
    void setFlowBlendMode(int mode)
    {
        m_flowBlendMode = (mode == ruwa::core::brushes::BrushSettingsData::FlowBlendSrcOver)
            ? ruwa::core::brushes::BrushSettingsData::FlowBlendSrcOver
            : ruwa::core::brushes::BrushSettingsData::FlowBlendMax;
    }
    void setTextureAmount(float v)
    {
        const float nv = std::clamp(v, 0.0f, 1.0f);
        if (std::abs(m_textureAmount - nv) > 0.0001f) {
            m_textureAmount = nv;
            m_proceduralTextureTiles.clear();
            ++m_textureRevision;
        }
    }
    void setTextureScale(float v)
    {
        const float nv = std::clamp(v, 0.1f, 4.0f);
        if (std::abs(m_textureScale - nv) > 0.0001f) {
            m_textureScale = nv;
            m_proceduralTextureTiles.clear();
            ++m_textureRevision;
        }
    }
    void setTextureContrast(float v)
    {
        const float nv = std::clamp(v, 0.0f, 1.0f);
        if (std::abs(m_textureContrast - nv) > 0.0001f) {
            m_textureContrast = nv;
            m_proceduralTextureTiles.clear();
            ++m_textureRevision;
        }
    }
    void setTextureDepth(float v)
    {
        const float nv = std::clamp(v, 0.0f, 1.0f);
        if (std::abs(m_textureDepth - nv) > 0.0001f) {
            m_textureDepth = nv;
            m_proceduralTextureTiles.clear();
            ++m_textureRevision;
        }
    }
    void setTextureBlend(float v)
    {
        const float nv = std::clamp(v, 0.0f, 1.0f);
        if (std::abs(m_textureBlend - nv) > 0.0001f) {
            m_textureBlend = nv;
            m_proceduralTextureTiles.clear();
            ++m_textureRevision;
        }
    }
    void setTextureEdgeBoost(float v) { m_textureEdgeBoost = std::clamp(v, 0.0f, 1.0f); }
    void setTextureType(int v)
    {
        const int nv = std::clamp(v, 0, 2);
        if (m_textureType != nv) {
            m_textureType = nv;
            m_proceduralTextureTiles.clear();
            ++m_textureRevision;
        }
    }
    void setDabType(int v) { m_dabType = std::clamp(v, 0, 5); }
    int dabType() const { return m_dabType; }
    void setDabCustomImagePath(const QString& path) { m_dabCustomImagePath = path; }
    const QString& dabCustomImagePath() const { return m_dabCustomImagePath; }
    float dabThreshold() const { return m_dabThreshold; }
    float dabCompression() const { return m_dabCompression; }
    int dabInterpolation() const { return m_dabInterpolation; }
    int dabShapeWidth() const { return m_dabShapeW; }
    int dabShapeHeight() const { return m_dabShapeH; }
    bool hasDabShapeMask() const
    {
        return m_dabType > 0 && !m_dabShapeAlpha.empty() && m_dabShapeW > 0 && m_dabShapeH > 0;
    }
    bool dabShapeMaskMatches(const ruwa::core::brushes::BrushSettingsData& settings) const
    {
        const bool hasStoredMask = !m_dabShapeAlpha.empty() && !m_dabShapeSoftAlpha.empty()
            && m_dabShapeW > 0 && m_dabShapeH > 0;
        if (!settings.dabCustomImagePath.isEmpty()) {
            return hasStoredMask && m_dabType == 1
                && m_dabCustomImagePath == settings.dabCustomImagePath
                && m_dabThreshold == std::clamp(settings.dabThreshold, 0.0f, 1.0f)
                && m_dabCompression == std::clamp(settings.dabCompression, 0.0f, 1.0f)
                && m_dabInterpolation == std::clamp(settings.dabInterpolation, 0, 1);
        }

        const int dabType = std::clamp(settings.dabType, 0, 5);
        return dabType > 0 ? hasStoredMask && m_dabType == dabType : !hasStoredMask;
    }
    float dabCoverageExtent(float radius, float hardness, float roundness, float angleDegrees,
        bool includeRasterPadding = false) const
    {
        return computeDabCoverageBounds(
            radius, hardness, roundness, angleDegrees, includeRasterPadding)
            .axisAlignedExtent;
    }
    float dabRotationInvariantCoverageExtent(
        float radius, float hardness, float roundness, bool includeRasterPadding = false) const
    {
        return computeDabCoverageBounds(radius, hardness, roundness, 0.0f, includeRasterPadding)
            .rotationInvariantExtent;
    }
    void setDabXScale(float v) { m_dabXScale = std::clamp(v, 0.0f, 1.0f); }
    void setDabYScale(float v) { m_dabYScale = std::clamp(v, 0.0f, 1.0f); }
    float dabXScale() const { return m_dabXScale; }
    float dabYScale() const { return m_dabYScale; }
    void setDabRotation(float degrees)
    {
        float v = std::fmod(degrees, 360.0f);
        if (v < 0.0f)
            v += 360.0f;
        m_dabRotation = v;
    }
    float dabRotation() const { return m_dabRotation; }
    void setDabThreshold(float v) { m_dabThreshold = std::clamp(v, 0.0f, 1.0f); }
    void setDabCompression(float v) { m_dabCompression = std::clamp(v, 0.0f, 1.0f); }
    void setDabInterpolation(int v) { m_dabInterpolation = std::clamp(v, 0, 1); }
    /// Set PNG dab shape mask for dabType 1-5. data is alpha channel, row-major.
    /// Pass nullptr to clear (use circle for all types).
    void setDabShapeMask(const uint8_t* data, int width, int height)
    {
        setDabShapeMask(data, nullptr, width, height);
    }

    void setDabShapeMask(
        const uint8_t* alphaData, const uint8_t* softAlphaData, int width, int height)
    {
        if (!alphaData || width <= 0 || height <= 0) {
            m_dabShapeAlpha.clear();
            m_dabShapeSoftAlpha.clear();
            m_dabShapeW = 0;
            m_dabShapeH = 0;
            return;
        }
        const size_t n = static_cast<size_t>(width * height);
        m_dabShapeAlpha.assign(alphaData, alphaData + n);
        if (softAlphaData) {
            m_dabShapeSoftAlpha.assign(softAlphaData, softAlphaData + n);
        } else {
            m_dabShapeSoftAlpha = m_dabShapeAlpha;
        }
        m_dabShapeW = width;
        m_dabShapeH = height;
    }
    void setScatterPosition(float v) { m_scatterPosition = std::clamp(v, 0.0f, 1.0f); }
    void setBrushFeather(bool enabled) { m_brushFeather = enabled; }

    void setRoundness(float roundness) { m_roundness = std::clamp(roundness, 0.0f, 1.0f); }

    void setAngleDegrees(float angleDegrees)
    {
        m_angleDegrees = std::fmod(angleDegrees, 360.0f);
        if (m_angleDegrees < 0.0f)
            m_angleDegrees += 360.0f;
    }

    void setEraseMode(bool erase) { m_eraseMode = erase; }
    bool isEraseMode() const { return m_eraseMode; }

    void setBlurMode(bool blur) { m_blurMode = blur; }
    bool isBlurMode() const { return m_blurMode; }

    void setSmudgeMode(bool smudge) { m_smudgeMode = smudge; }
    bool isSmudgeMode() const { return m_smudgeMode; }

    // Liquify tool (geometric warp). Like blur/smudge it is a canvas-reading
    // tool: it reads the layer ROI and writes displaced pixels back via the
    // dedicated GL path (stampLiquifySegmentGPU) instead of depositing color.
    void setLiquifyMode(bool liquify) { m_liquifyMode = liquify; }
    bool isLiquifyMode() const { return m_liquifyMode; }

    // Liquify sub-mode (warp kind): 0=Push, 1=Twirl CW, 2=Twirl CCW,
    // 3=Bloat, 4=Pucker. Read by the GL field shader (uMode).
    void setLiquifyToolMode(int mode) { m_liquifyToolMode = mode; }
    int liquifyToolMode() const { return m_liquifyToolMode; }

    // Smudge pickup rate: how much canvas content under the brush is mixed
    // into the pigment-latent carry reservoir on each dab.
    // Wet brushes use their dedicated colorBlending/length exchange.
    void setWetMix(float v) { m_wetMix = std::clamp(v, 0.0f, 1.0f); }
    float wetMix() const { return m_wetMix; }

    // Wet-brush color mixing (SAI watercolor model). See BrushSettingsData
    // for parameter meanings. When isWetMode() is true the brush runs through
    // the canvas-reading reservoir pipeline (shared with smudge) instead of
    // the plain accumulate-into-stroke-buffer path.
    void setColorBlending(float v) { m_colorBlending = std::clamp(v, 0.0f, 1.0f); }
    void setColorDilution(float v) { m_colorDilution = std::clamp(v, 0.0f, 1.0f); }
    void setColorSpread(float v) { m_colorSpread = std::clamp(v, 0.0f, 1.0f); }
    void setColorLength(float v) { m_colorLength = std::clamp(v, 0.0f, 1.0f); }
    void setColorWetFlow(float v) { m_colorWetFlow = std::clamp(v, 0.0f, 1.0f); }
    void setColorDryRate(float v) { m_colorDryRate = std::clamp(v, 0.0f, 1.0f); }
    void setColorBuildup(float v) { m_colorBuildup = std::clamp(v, 0.0f, 1.0f); }
    float colorBlending() const { return m_colorBlending; }
    float colorDilution() const { return m_colorDilution; }
    float colorSpread() const { return m_colorSpread; }
    float colorLength() const { return m_colorLength; }
    float colorWetFlow() const { return m_colorWetFlow; }
    float colorDryRate() const { return m_colorDryRate; }
    float colorBuildup() const { return m_colorBuildup; }
    /// True when wet color mixing should be active. Mutually exclusive with the
    /// dedicated erase/blur/smudge tool modes (those own the reservoir/flatten
    /// path for their own purposes).
    bool isWetMode() const
    {
        constexpr float kEps = 0.0001f;
        return !m_eraseMode && !m_blurMode && !m_smudgeMode && !m_liquifyMode
            && (m_colorBlending > kEps || m_colorDilution > kEps || m_colorSpread > kEps);
    }
    void setStrokeBlendMode(ruwa::core::layers::BlendMode mode) { m_strokeBlendMode = mode; }
    void setStrokeBlendMode(int mode)
    {
        setStrokeBlendMode(
            ruwa::core::layers::blendModeFromValue(mode, ruwa::core::layers::BlendMode::Normal));
    }
    ruwa::core::layers::BlendMode strokeBlendMode() const { return m_strokeBlendMode; }
    void setSelectionMaskAffectsAlpha(bool affectsAlpha)
    {
        m_selectionMaskAffectsAlpha = affectsAlpha;
    }
    bool selectionMaskAffectsAlpha() const { return m_selectionMaskAffectsAlpha; }

    // --- Pen pressure ---
    void setPressure(float pressure) { m_pressure = std::clamp(pressure, 0.0f, 1.0f); }
    float pressure() const { return m_pressure; }
    void setStrokeElapsedSeconds(float seconds, bool available = true)
    {
        if (!available || !std::isfinite(seconds)) {
            m_strokeElapsedSeconds = 0.0f;
            m_strokeTimeAvailable = false;
            return;
        }
        m_strokeElapsedSeconds = std::max(0.0f, seconds);
        m_strokeTimeAvailable = true;
    }
    float strokeElapsedSeconds() const { return m_strokeElapsedSeconds; }
    bool strokeTimeAvailable() const { return m_strokeTimeAvailable; }
    void clearStrokeTime()
    {
        m_strokeElapsedSeconds = 0.0f;
        m_strokeTimeAvailable = false;
    }

    void setSizePressureEnabled(bool enabled) { m_sizePressureEnabled = enabled; }
    void setOpacityPressureEnabled(bool enabled) { m_opacityPressureEnabled = enabled; }
    void setStartTaper(float taper) { m_startTaper = std::clamp(taper, 0.0f, 1.0f); }
    void setEndTaper(float taper) { m_endTaper = std::clamp(taper, 0.0f, 1.0f); }
    void setPostCorrection(float correction)
    {
        m_postCorrection = std::clamp(correction, 0.0f, 1.0f);
    }
    void setStabilization(float stabilization)
    {
        m_stabilization = std::clamp(stabilization, 0.0f, 1.0f);
    }
    void setAdjustCorrectionBySpeed(bool enabled) { m_adjustCorrectionBySpeed = enabled; }
    void setStartCorrectionEnabled(bool enabled) { m_startCorrectionEnabled = enabled; }
    void setStartCorrectionLength(float lengthPx)
    {
        m_startCorrectionLength = std::clamp(lengthPx, 0.0f, 500.0f);
    }
    void setEndCorrectionEnabled(bool enabled) { m_endCorrectionEnabled = enabled; }
    void setEndCorrectionLength(float lengthPx)
    {
        m_endCorrectionLength = std::clamp(lengthPx, 0.0f, 500.0f);
    }
    float postCorrection() const
    {
        return m_useBrushSettingsModel ? evaluateDynamicsForCurrentInput().postCorrection
                                       : m_postCorrection;
    }
    float stabilization() const
    {
        return m_useBrushSettingsModel ? evaluateDynamicsForCurrentInput().stabilization
                                       : m_stabilization;
    }
    bool adjustCorrectionBySpeed() const { return m_adjustCorrectionBySpeed; }
    bool startCorrectionEnabled() const { return m_startCorrectionEnabled; }
    float startTaper() const
    {
        return m_useBrushSettingsModel ? evaluateDynamicsForCurrentInput().startTaper
                                       : m_startTaper;
    }
    float startCorrectionLength() const
    {
        return m_useBrushSettingsModel ? evaluateDynamicsForCurrentInput().startCorrectionLength
                                       : m_startCorrectionLength;
    }
    bool endCorrectionEnabled() const { return m_endCorrectionEnabled; }
    float endTaper() const
    {
        return m_useBrushSettingsModel ? evaluateDynamicsForCurrentInput().endTaper : m_endTaper;
    }
    float endCorrectionLength() const
    {
        return m_useBrushSettingsModel ? evaluateDynamicsForCurrentInput().endCorrectionLength
                                       : m_endCorrectionLength;
    }
    void setSizePressureStrength(float strength)
    {
        const float s = std::clamp(strength, 0.0f, 1.0f);
        setSizePressureRange(1.0f - s, 1.0f);
    }
    void setOpacityPressureStrength(float strength)
    {
        const float s = std::clamp(strength, 0.0f, 1.0f);
        setOpacityPressureRange(1.0f - s, 1.0f);
    }
    void setSizePressureRange(float minScale, float maxScale)
    {
        normalizeRange(minScale, maxScale);
        m_sizePressureMin = minScale;
        m_sizePressureMax = maxScale;
    }
    void setOpacityPressureRange(float minScale, float maxScale)
    {
        normalizeRange(minScale, maxScale);
        m_opacityPressureMin = minScale;
        m_opacityPressureMax = maxScale;
    }
    void setFlowPressureRange(float minScale, float maxScale)
    {
        normalizeRange(minScale, maxScale);
        m_flowPressureMin = minScale;
        m_flowPressureMax = maxScale;
    }

    bool sizePressureEnabled() const { return m_sizePressureEnabled; }
    bool opacityPressureEnabled() const { return m_opacityPressureEnabled; }
    bool usesProceduralTexture() const
    {
        if (m_useBrushSettingsModel) {
            const auto dynamicState = evaluateDynamicsForCurrentInput();
            return dynamicState.textureAmount > 0.0001f && dynamicState.textureDepth > 0.0001f;
        }
        return m_textureAmount > 0.0001f && m_textureDepth > 0.0001f;
    }
    float textureAmount() const
    {
        return m_useBrushSettingsModel ? evaluateDynamicsForCurrentInput().textureAmount
                                       : m_textureAmount;
    }
    float textureScale() const
    {
        return m_useBrushSettingsModel ? evaluateDynamicsForCurrentInput().textureScale
                                       : m_textureScale;
    }
    float textureContrast() const
    {
        return m_useBrushSettingsModel ? evaluateDynamicsForCurrentInput().textureContrast
                                       : m_textureContrast;
    }
    float textureDepth() const
    {
        return m_useBrushSettingsModel ? evaluateDynamicsForCurrentInput().textureDepth
                                       : m_textureDepth;
    }
    float textureBlend() const
    {
        return m_useBrushSettingsModel ? evaluateDynamicsForCurrentInput().textureBlend
                                       : m_textureBlend;
    }
    float textureEdgeBoost() const
    {
        return m_useBrushSettingsModel ? evaluateDynamicsForCurrentInput().textureEdgeBoost
                                       : m_textureEdgeBoost;
    }
    int textureType() const { return m_textureType; }
    uint32_t textureRevision() const
    {
        if (!hasDynamicTextureBinding()) {
            return m_textureRevision;
        }
        auto quantize = [](float value) -> uint32_t {
            return static_cast<uint32_t>(std::lround(std::clamp(value, 0.0f, 16.0f) * 1024.0f));
        };
        uint32_t derived = m_textureRevision;
        derived ^= quantize(textureAmount()) * 0x45d9f3bu;
        derived ^= quantize(textureScale()) * 0x27d4eb2du;
        derived ^= quantize(textureContrast()) * 0x165667b1u;
        derived ^= quantize(textureDepth()) * 0x9e3779b9u;
        derived ^= quantize(textureBlend()) * 0x85ebca6bu;
        derived ^= quantize(textureEdgeBoost()) * 0xc2b2ae35u;
        return derived;
    }
    const std::vector<uint8_t>& proceduralTextureTileAlpha(const TileKey& key) const
    {
        auto it = m_proceduralTextureTiles.find(key);
        if (it == m_proceduralTextureTiles.end()) {
            // Evict oldest entries when cache grows too large (each entry ~64 KB)
            constexpr size_t kMaxCachedTextureTiles = 128;
            if (m_proceduralTextureTiles.size() >= kMaxCachedTextureTiles) {
                m_proceduralTextureTiles.clear();
            }

            ProceduralTextureTile tile;
            tile.alpha.resize(TILE_SIZE * TILE_SIZE, 255);
            const float contrast = textureContrast();
            const float depth = textureDepth();
            const float blend = textureBlend();
            const float amount = textureAmount();
            const float scale = textureScale();
            const float contrastStrength = 0.5f + contrast * 2.5f;

            for (uint32_t y = 0; y < TILE_SIZE; ++y) {
                for (uint32_t x = 0; x < TILE_SIZE; ++x) {
                    const float worldX = static_cast<float>(key.x) * static_cast<float>(TILE_SIZE)
                        + static_cast<float>(x) + 0.5f;
                    const float worldY = static_cast<float>(key.y) * static_cast<float>(TILE_SIZE)
                        + static_cast<float>(y) + 0.5f;

                    float g = proceduralGrainByType(worldX, worldY, scale);
                    g = std::clamp(0.5f + (g - 0.5f) * contrastStrength, 0.0f, 1.0f);

                    const float depthMix = 1.0f - depth * (1.0f - g);
                    const float blendMix
                        = (1.0f - blend) * depthMix + blend * (depthMix * depthMix);
                    const float factor = (1.0f - amount) + amount * blendMix;
                    tile.alpha[y * TILE_SIZE + x]
                        = static_cast<uint8_t>(std::clamp(factor * 255.0f, 0.0f, 255.0f));
                }
            }
            it = m_proceduralTextureTiles.emplace(key, std::move(tile)).first;
        }
        return it->second.alpha;
    }
    bool hasTaperEffect() const
    {
        // Taper is meaningless for the canvas-reading reservoir tools
        // (blur/smudge/wet): scaling end-dab radius and rebuilding the stroke
        // from dabs corrupts their accumulation. Never taper those.
        if (m_blurMode || m_smudgeMode || m_liquifyMode || isWetMode()) {
            return false;
        }
        constexpr float kEpsilon = 0.0001f;
        const bool taperEnabled = (startTaper() > kEpsilon) || (endTaper() > kEpsilon);
        if (!taperEnabled)
            return false;
        return hasRadiusPressureResponse() || hasOpacityPressureResponse();
    }
    bool hasEndpointCorrectionEffect() const
    {
        return (startCorrectionEnabled() && startCorrectionLength() > 0.0001f)
            || (endCorrectionEnabled() && endCorrectionLength() > 0.0001f);
    }
    bool hasPostCorrectionEffect() const { return postCorrection() > 0.0001f; }
    bool hasStrokePostProcessingEffect() const
    {
        return hasPostCorrectionEffect() || hasEndpointCorrectionEffect();
    }
    /// Position scatter expands the tile set; use stroke buffer keys for dirty tracking.
    bool hasPositionScatterEffect() const { return scatterPositionValue() > 0.0001f; }

    float radius() const { return m_radius; }
    float hardness() const { return m_hardness; }
    float spacing() const { return m_spacing; }
    float flow() const { return m_flow; }
    float roundness() const { return m_roundness; }
    float angleDegrees() const { return m_angleDegrees; }
    uint8_t colorR() const { return m_r; }
    uint8_t colorG() const { return m_g; }
    uint8_t colorB() const { return m_b; }
    uint8_t colorA() const { return m_a; }

    /// Radius modulated by pen pressure (when size pressure is enabled)
    float effectiveRadius() const
    {
        return m_useBrushSettingsModel
            ? std::max(0.5f, m_radius * evaluateDynamicsForCurrentInput().radiusMultiplier)
            : effectiveRadiusForPressure(m_pressure);
    }

    /// Radius modulated by explicit pressure value (when size pressure is enabled)
    float effectiveRadiusForPressure(
        float pressure, float strokeElapsedSeconds = 0.0f, bool strokeTimeAvailable = false) const
    {
        if (m_useBrushSettingsModel) {
            const auto dynamicState = evaluateDynamicsForPressure(
                pressure, 0.0f, 0.0f, strokeElapsedSeconds, strokeTimeAvailable);
            return std::max(0.5f, m_radius * dynamicState.radiusMultiplier);
        }
        if (!m_sizePressureEnabled)
            return m_radius;
        const float scale = rangeValueForPressure(m_sizePressureMin, m_sizePressureMax, pressure);
        return std::max(0.5f, m_radius * scale);
    }

    /// Final stroke opacity (applied at flatten/compositing stage).
    /// This is always applied to the whole stroke, including src-over flow mode.
    float strokeOpacity() const { return std::clamp(static_cast<float>(m_a) / 255.0f, 0.0f, 1.0f); }

    /// Per-dab alpha modulated by flow/pressure only.
    /// User opacity is intentionally applied later to the whole stroke.
    uint8_t effectiveAlpha() const
    {
        if (m_useBrushSettingsModel) {
            const auto dynamicState = evaluateDynamicsForCurrentInput();
            return computeFlowDabAlpha(dynamicState.flow, dynamicState.opacityMultiplier);
        }
        float normalizedOpacity = effectiveFlowForPressure(m_pressure);
        if (m_opacityPressureEnabled) {
            normalizedOpacity
                *= rangeValueForPressure(m_opacityPressureMin, m_opacityPressureMax, m_pressure);
        }
        return computeFlowDabAlpha(normalizedOpacity, 1.0f);
    }

    // === Stroke buffer ===

    /// Begin a new stroke — clears the stroke buffer
    void beginStroke()
    {
        m_strokeBuffer.clear();
        m_strokeDabs.clear();
        // At small spacing (0.5%) dab counts grow into the thousands within a
        // single stroke; pre-reserve so push_back during the hot path doesn't
        // realloc/copy DabPoint (~140 bytes each) repeatedly.
        if (m_strokeDabs.capacity() < 4096) {
            m_strokeDabs.reserve(4096);
        }
        m_spacingDistanceSinceLastDab = 0.0f;
        m_strokeActive = true;
        m_strokeDirCos = 1.0f;
        m_strokeDirSin = 0.0f;
        m_strokeDirSumX = 0.0f;
        m_strokeDirSumY = 0.0f;
        m_strokeTravelTotal = 0.0f;
        m_strokeDirInitialized = false;
        clearStrokeTime();
    }

    /// Flatten stroke buffer onto target layer grid (CPU src-over blend),
    /// then clear the buffer. Returns set of affected tile keys.
    std::unordered_set<TileKey, TileKeyHash> endStroke(TileGrid& targetGrid, bool alphaLock = false,
        const TileGrid* strokeBlendBackdrop = nullptr,
        const Color& strokeBlendBackdropColor = Color::transparent(),
        const TileGrid* finalSourceMask = nullptr, bool selectionAlphaCap = false,
        bool maskErase = false)
    {
        std::unordered_set<TileKey, TileKeyHash> affected;
        const float finalOpacity = strokeOpacity();

        for (auto& [key, strokeTile] : m_strokeBuffer.tiles()) {
            if (finalSourceMask && !finalSourceMask->getTile(key)) {
                continue;
            }
            if (m_eraseMode && !maskErase) {
                // Erase: only process tiles that exist in the target
                TileData* layerTile = targetGrid.getTile(key);
                if (!layerTile)
                    continue;
                eraseFlattenTile(strokeTile, *layerTile, finalOpacity, key, finalSourceMask);
                layerTile->markDirty();
                targetGrid.markDirty(key);
            } else {
                TileData& layerTile = targetGrid.getOrCreateTile(key);
                if (maskErase) {
                    // Eraser on a layer mask = paint BLACK (hide), the inverse of
                    // revealing white. The stroke buffer's RGB is already black
                    // (see addDab), so a plain src-over lays down black scaled by
                    // the eraser's opacity/coverage. getOrCreateTile honors the
                    // mask's defaultFill so the untouched background is preserved.
                    flattenTile(strokeTile, layerTile, finalOpacity, key, finalSourceMask, false);
                } else if (!m_blurMode && !m_smudgeMode
                    && m_strokeBlendMode != ruwa::core::layers::BlendMode::Normal) {
                    const TileData* backdropTile
                        = strokeBlendBackdrop ? strokeBlendBackdrop->getTile(key) : nullptr;
                    flattenTileWithBlendMode(strokeTile, backdropTile, layerTile, finalOpacity,
                        m_strokeBlendMode, alphaLock, key, strokeBlendBackdropColor,
                        finalSourceMask);
                } else if (alphaLock) {
                    flattenTileAlphaLocked(strokeTile, layerTile, finalOpacity);
                } else {
                    // Soft-selection alpha cap is meaningful only when we are NOT
                    // alpha-locking (alpha-lock already caps to dst.a) and have a
                    // selection mask to read per-pixel cap from.
                    const bool useAlphaCap = selectionAlphaCap && finalSourceMask != nullptr;
                    flattenTile(
                        strokeTile, layerTile, finalOpacity, key, finalSourceMask, useAlphaCap);
                }
                layerTile.markDirty();
                targetGrid.markDirty(key);
            }
            affected.insert(key);
        }

        m_strokeBuffer.clear();
        m_strokeDabs.clear();
        m_spacingDistanceSinceLastDab = 0.0f;
        m_strokeActive = false;
        m_strokeDirSumX = 0.0f;
        m_strokeDirSumY = 0.0f;
        m_strokeTravelTotal = 0.0f;
        m_strokeDirInitialized = false;
        clearStrokeTime();
        return affected;
    }

    bool hasActiveStroke() const { return m_strokeActive; }
    TileGrid& strokeBuffer() { return m_strokeBuffer; }
    const TileGrid& strokeBuffer() const { return m_strokeBuffer; }
    std::vector<DabPoint>& strokeDabs() { return m_strokeDabs; }
    const std::vector<DabPoint>& strokeDabs() const { return m_strokeDabs; }
    static constexpr size_t kMaxTaperAffectedDabs = 1500;

    void collectStrokeDabRangeCoveredTiles(size_t startDabIndex, size_t dabCount,
        std::unordered_set<TileKey, TileKeyHash>& outTiles, bool includeBaseExtent = false) const
    {
        if (dabCount == 0 || startDabIndex >= m_strokeDabs.size())
            return;

        const size_t endDabIndex = std::min(startDabIndex + dabCount, m_strokeDabs.size());
        for (size_t i = startDabIndex; i < endDabIndex; ++i) {
            collectDabCoveredTiles(m_strokeDabs[i], outTiles, includeBaseExtent);
        }
    }

    size_t startTaperDabCount() const
    {
        const float taper = m_strokeDabs.empty() ? startTaper() : m_strokeDabs.front().startTaper;
        return taperAffectedDabCount(taper, m_strokeDabs.size());
    }

    size_t endTaperDabCount() const
    {
        const float taper = m_strokeDabs.empty() ? endTaper() : m_strokeDabs.back().endTaper;
        return taperAffectedDabCount(taper, m_strokeDabs.size());
    }

    bool applyStrokeTaperToDabRange(size_t startDabIndex, size_t dabCount)
    {
        if (m_strokeDabs.empty())
            return false;
        if (!hasTaperEffect())
            return false;
        if (dabCount == 0 || startDabIndex >= m_strokeDabs.size())
            return false;

        const size_t startCount = startTaperDabCount();
        const size_t endCount = endTaperDabCount();
        if (startCount == 0 && endCount == 0)
            return false;

        const size_t endDabIndex = std::min(startDabIndex + dabCount, m_strokeDabs.size());
        const size_t lastIndex = m_strokeDabs.size() - 1;
        bool changed = false;

        for (size_t i = startDabIndex; i < endDabIndex; ++i) {
            float taperScale = 1.0f;
            if (startCount > 0 && i < startCount) {
                taperScale = std::min(taperScale, taperScaleForEdgeIndex(i, startCount));
            }
            const size_t distToEnd = lastIndex - i;
            if (endCount > 0 && distToEnd < endCount) {
                taperScale = std::min(taperScale, taperScaleForEdgeIndex(distToEnd, endCount));
            }

            DabPoint& dab = m_strokeDabs[i];
            const float oldRadius = dab.radius;
            const uint8_t oldAlpha = dab.alpha;

            const float baseRadius = std::max(0.0f, dab.baseRadius);
            const float baseAlpha = static_cast<float>(dab.baseAlpha);

            if (hasRadiusPressureResponse()) {
                dab.radius = std::max(0.0f, baseRadius * taperScale);
            } else {
                dab.radius = baseRadius;
            }

            if (hasOpacityPressureResponse()) {
                dab.alpha = static_cast<uint8_t>(std::clamp(baseAlpha * taperScale, 0.0f, 255.0f));
            } else {
                dab.alpha = dab.baseAlpha;
            }

            if (dab.radius != oldRadius || dab.alpha != oldAlpha) {
                changed = true;
            }
        }

        return changed;
    }

    bool applyStrokeTaperToDabs() { return applyStrokeTaperToDabRange(0, m_strokeDabs.size()); }

    bool applyEndpointCorrectionToDabs()
    {
        if (m_strokeDabs.size() < 3 || !hasEndpointCorrectionEffect()) {
            return false;
        }

        std::vector<float> cumulative(m_strokeDabs.size(), 0.0f);
        for (size_t i = 1; i < m_strokeDabs.size(); ++i) {
            const float dx = m_strokeDabs[i].worldX - m_strokeDabs[i - 1].worldX;
            const float dy = m_strokeDabs[i].worldY - m_strokeDabs[i - 1].worldY;
            cumulative[i] = cumulative[i - 1] + std::sqrt(dx * dx + dy * dy);
        }

        auto applyRange = [this, &cumulative](bool fromStart, float lengthPx) -> bool {
            if (lengthPx <= 0.0001f || m_strokeDabs.size() < 2) {
                return false;
            }

            const size_t lastIndex = m_strokeDabs.size() - 1;
            size_t anchorIndex = fromStart ? lastIndex : 0;
            if (fromStart) {
                while (anchorIndex > 0 && cumulative[anchorIndex] > lengthPx) {
                    --anchorIndex;
                }
                if (anchorIndex < lastIndex && (lengthPx - cumulative[anchorIndex]) > 0.0001f) {
                    ++anchorIndex;
                }
            } else {
                while (anchorIndex < lastIndex
                    && (cumulative[lastIndex] - cumulative[anchorIndex]) > lengthPx) {
                    ++anchorIndex;
                }
                if (anchorIndex > 0
                    && ((cumulative[lastIndex] - cumulative[anchorIndex]) < lengthPx)) {
                    --anchorIndex;
                }
            }

            if (anchorIndex == (fromStart ? 0u : lastIndex)) {
                return false;
            }

            const size_t edgeIndex = fromStart ? 0u : lastIndex;
            const float edgeX = m_strokeDabs[edgeIndex].worldX;
            const float edgeY = m_strokeDabs[edgeIndex].worldY;
            const float anchorX = m_strokeDabs[anchorIndex].worldX;
            const float anchorY = m_strokeDabs[anchorIndex].worldY;
            const float anchorDistance = std::max(0.0001f,
                fromStart ? cumulative[anchorIndex]
                          : (cumulative[lastIndex] - cumulative[anchorIndex]));

            const size_t begin = fromStart ? 1u : anchorIndex;
            const size_t end = fromStart ? anchorIndex : lastIndex - 1u;
            bool changed = false;
            for (size_t i = begin; i <= end; ++i) {
                const float distanceFromEdge
                    = fromStart ? cumulative[i] : (cumulative[lastIndex] - cumulative[i]);
                if (distanceFromEdge <= 0.0001f || distanceFromEdge > lengthPx) {
                    continue;
                }

                const float lineT = std::clamp(distanceFromEdge / anchorDistance, 0.0f, 1.0f);
                const float targetX = edgeX + (anchorX - edgeX) * lineT;
                const float targetY = edgeY + (anchorY - edgeY) * lineT;
                const float normalized = 1.0f - std::clamp(distanceFromEdge / lengthPx, 0.0f, 1.0f);
                const float influence = 0.85f * smoothstep01(normalized);

                DabPoint& dab = m_strokeDabs[i];
                const float newX = dab.worldX + (targetX - dab.worldX) * influence;
                const float newY = dab.worldY + (targetY - dab.worldY) * influence;
                if (std::abs(dab.worldX - newX) > 0.0001f
                    || std::abs(dab.worldY - newY) > 0.0001f) {
                    dab.worldX = newX;
                    dab.worldY = newY;
                    changed = true;
                }
            }
            return changed;
        };

        bool changed = false;
        if (startCorrectionEnabled()) {
            const float lengthPx = m_useBrushSettingsModel
                ? m_strokeDabs.front().startCorrectionLength
                : m_startCorrectionLength;
            changed = applyRange(true, lengthPx) || changed;
        }
        if (endCorrectionEnabled()) {
            const float lengthPx = m_useBrushSettingsModel ? m_strokeDabs.back().endCorrectionLength
                                                           : m_endCorrectionLength;
            changed = applyRange(false, lengthPx) || changed;
        }
        return changed;
    }

    bool applyPostCorrectionToDabs()
    {
        if (m_strokeDabs.size() < 3 || !hasPostCorrectionEffect()) {
            return false;
        }

        float radiusSum = 0.0f;
        float correctionSum = 0.0f;
        for (const DabPoint& dab : m_strokeDabs) {
            radiusSum += std::max(0.5f, dab.baseRadius);
            correctionSum += m_useBrushSettingsModel ? dab.postCorrection : m_postCorrection;
        }
        const float avgRadius = radiusSum / static_cast<float>(m_strokeDabs.size());
        const float correctionStrength
            = std::clamp(correctionSum / static_cast<float>(m_strokeDabs.size()), 0.0f, 1.0f);
        const float radiusFactor = std::clamp(avgRadius / 10.0f, 0.6f, 8.0f);
        const int halfWindow = std::clamp(
            static_cast<int>(std::lround((1.0f + correctionStrength * 10.0f) * radiusFactor)), 2,
            128);
        const int passes
            = std::clamp(static_cast<int>(std::lround(1.0f + correctionStrength * 3.0f)), 1, 4);

        std::vector<float> baseX(m_strokeDabs.size());
        std::vector<float> baseY(m_strokeDabs.size());
        for (size_t i = 0; i < m_strokeDabs.size(); ++i) {
            baseX[i] = m_strokeDabs[i].baseWorldX;
            baseY[i] = m_strokeDabs[i].baseWorldY;
        }
        std::vector<float> cumulativeLengths(m_strokeDabs.size(), 0.0f);
        for (size_t i = 1; i < m_strokeDabs.size(); ++i) {
            const float dx = baseX[i] - baseX[i - 1];
            const float dy = baseY[i] - baseY[i - 1];
            cumulativeLengths[i] = cumulativeLengths[i - 1] + std::sqrt(dx * dx + dy * dy);
        }
        const float strokeLength = cumulativeLengths.back();
        const float avgSpacing = (m_strokeDabs.size() > 1)
            ? (strokeLength / static_cast<float>(m_strokeDabs.size() - 1))
            : 0.0f;
        const float edgeFadeLength = std::max(
            avgRadius * 2.0f, std::max(1.0f, avgSpacing * static_cast<float>(halfWindow)) * 0.75f);
        std::vector<float> curX = baseX;
        std::vector<float> curY = baseY;
        std::vector<float> nextX(m_strokeDabs.size());
        std::vector<float> nextY(m_strokeDabs.size());
        auto postCorrectionSpeedScale = [this](size_t index) {
            const size_t prevIndex = (index > 0) ? (index - 1) : index;
            const size_t nextIndex = std::min(index + 1, m_strokeDabs.size() - 1);
            const float dx
                = m_strokeDabs[nextIndex].baseWorldX - m_strokeDabs[prevIndex].baseWorldX;
            const float dy
                = m_strokeDabs[nextIndex].baseWorldY - m_strokeDabs[prevIndex].baseWorldY;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float dt = std::max(0.0001f,
                m_strokeDabs[nextIndex].strokeElapsedSeconds
                    - m_strokeDabs[prevIndex].strokeElapsedSeconds);
            const float speedPxPerSec = distance / dt;
            const float sampleRateHz = 1.0f / dt;
            const float radiusPx = std::max(0.5f, m_strokeDabs[index].baseRadius);
            const float samplesPerRadius
                = (sampleRateHz * radiusPx) / std::max(speedPxPerSec, 1.0f);
            constexpr float kDensityMidpoint = 0.75f;
            constexpr float kMinSpeedScale = 0.25f;
            const float densityResponse = samplesPerRadius / (samplesPerRadius + kDensityMidpoint);
            return std::clamp(
                kMinSpeedScale + (1.0f - kMinSpeedScale) * densityResponse, kMinSpeedScale, 1.0f);
        };

        for (int pass = 0; pass < passes; ++pass) {
            if (!curX.empty()) {
                nextX.front() = curX.front();
                nextY.front() = curY.front();
                nextX.back() = curX.back();
                nextY.back() = curY.back();
            }

            for (size_t i = 1; i + 1 < m_strokeDabs.size(); ++i) {
                const int center = static_cast<int>(i);
                const int start = std::max(0, center - halfWindow);
                const int end
                    = std::min(static_cast<int>(m_strokeDabs.size()) - 1, center + halfWindow);

                float sumW = 0.0f;
                float sumX = 0.0f;
                float sumY = 0.0f;
                for (int j = start; j <= end; ++j) {
                    const float distance = static_cast<float>(std::abs(j - center));
                    const float weight = static_cast<float>(halfWindow + 1) - distance;
                    sumW += weight;
                    sumX += curX[static_cast<size_t>(j)] * weight;
                    sumY += curY[static_cast<size_t>(j)] * weight;
                }
                if (sumW <= std::numeric_limits<float>::epsilon()) {
                    nextX[i] = curX[i];
                    nextY[i] = curY[i];
                    continue;
                }
                nextX[i] = sumX / sumW;
                nextY[i] = sumY / sumW;
            }
            curX.swap(nextX);
            curY.swap(nextY);
        }

        bool changed = false;
        for (size_t i = 0; i < m_strokeDabs.size(); ++i) {
            float blend = m_useBrushSettingsModel ? m_strokeDabs[i].postCorrection
                                                  : std::clamp(m_postCorrection, 0.0f, 1.0f);

            if (m_adjustCorrectionBySpeed) {
                blend *= postCorrectionSpeedScale(i);
            }
            if (strokeLength > 0.0001f && edgeFadeLength > 0.0001f) {
                const float distanceFromStart = cumulativeLengths[i];
                const float distanceFromEnd = strokeLength - distanceFromStart;
                const float edgeDistance = std::min(distanceFromStart, distanceFromEnd);
                blend *= smoothstep01(edgeDistance / edgeFadeLength);
            }

            float newX = baseX[i] + (curX[i] - baseX[i]) * blend;
            float newY = baseY[i] + (curY[i] - baseY[i]) * blend;

            const float scatterPosition
                = m_useBrushSettingsModel ? m_strokeDabs[i].scatterPosition : m_scatterPosition;
            if (scatterPosition > 0.0001f) {
                const uint32_t randomSeed = static_cast<uint32_t>(i) * 2654435769u;
                const int32_t ix = static_cast<int32_t>(std::floor(newX));
                const int32_t iy = static_cast<int32_t>(std::floor(newY));
                const float rx = hash01(ix, iy, randomSeed) * 2.0f - 1.0f;
                const float ry = hash01(ix + 7, iy + 13, randomSeed + 1u) * 2.0f - 1.0f;
                const float maxOffset = m_strokeDabs[i].radius * scatterPosition * 2.0f;
                newX += rx * maxOffset;
                newY += ry * maxOffset;
            }

            DabPoint& dab = m_strokeDabs[i];
            if (std::abs(dab.worldX - newX) > 0.0001f || std::abs(dab.worldY - newY) > 0.0001f) {
                dab.worldX = newX;
                dab.worldY = newY;
                changed = true;
            }
        }

        return changed;
    }

    /// Rebuild stroke buffer from stored dab points.
    /// Useful for post-processing dab trajectories (e.g. quick-shape correction).
    void rebuildStrokeBufferFromDabs(const TileGrid* selectionMask = nullptr, size_t maxDabs = 0)
    {
        // Keep allocated tiles/textures alive; just clear CPU pixels and re-rasterize.
        // This avoids dropping texture handles during interactive morphing.
        for (auto& [key, tile] : m_strokeBuffer.tiles()) {
            tile.clear();
            m_strokeBuffer.markDirty(key);
        }

        auto rasterizeByIndex = [this, selectionMask](size_t idx) {
            const DabPoint& dab = m_strokeDabs[idx];
            if (dab.alpha == 0 || dab.radius <= 0.0f)
                return;
            rasterizeDab(m_strokeBuffer, dab, selectionMask, dab.useMaxBlend);
        };

        if (maxDabs > 2 && m_strokeDabs.size() > maxDabs) {
            const size_t last = m_strokeDabs.size() - 1;
            size_t prevIdx = static_cast<size_t>(-1);
            for (size_t i = 0; i < maxDabs; ++i) {
                float u = (maxDabs <= 1) ? 0.0f
                                         : static_cast<float>(i) / static_cast<float>(maxDabs - 1);
                size_t idx = static_cast<size_t>(std::round(u * static_cast<float>(last)));
                if (idx == prevIdx)
                    continue;
                rasterizeByIndex(idx);
                prevIdx = idx;
            }
            return;
        }

        for (size_t i = 0; i < m_strokeDabs.size(); ++i) {
            rasterizeByIndex(i);
        }
    }

    /// Rebuild only a contiguous dab range in the stroke buffer.
    /// Clears tiles covered by the range, then re-rasterizes every current dab
    /// that overlaps those tiles so the untouched middle of the stroke stays intact.
    void rebuildStrokeBufferRangeFromDabs(
        size_t startDabIndex, size_t dabCount, const TileGrid* selectionMask = nullptr)
    {
        if (dabCount == 0 || startDabIndex >= m_strokeDabs.size())
            return;

        const size_t endDabIndex = std::min(startDabIndex + dabCount, m_strokeDabs.size());
        if (startDabIndex == 0 && endDabIndex == m_strokeDabs.size()) {
            rebuildStrokeBufferFromDabs(selectionMask);
            return;
        }

        std::unordered_set<TileKey, TileKeyHash> rebuildTiles;
        rebuildTiles.reserve((endDabIndex - startDabIndex) * 4u);
        for (size_t i = startDabIndex; i < endDabIndex; ++i) {
            collectDabCoveredTiles(m_strokeDabs[i], rebuildTiles, true);
        }
        if (rebuildTiles.empty())
            return;

        for (const TileKey& key : rebuildTiles) {
            TileData* tile = m_strokeBuffer.getTile(key);
            if (!tile)
                continue;
            tile->clear();
            m_strokeBuffer.markDirty(key);
        }

        for (const DabPoint& dab : m_strokeDabs) {
            if (dab.alpha == 0 || dab.radius <= 0.0f)
                continue;
            if (!dabIntersectsTileSet(dab, rebuildTiles, false))
                continue;
            rasterizeDab(m_strokeBuffer, dab, selectionMask, dab.useMaxBlend, &rebuildTiles);
        }
    }

    /// Discard stroke buffer without flattening (e.g. no valid target layer)
    void cancelStroke()
    {
        m_strokeBuffer.clear();
        m_strokeDabs.clear();
        m_spacingDistanceSinceLastDab = 0.0f;
        m_strokeActive = false;
        m_strokeDirSumX = 0.0f;
        m_strokeDirSumY = 0.0f;
        m_strokeTravelTotal = 0.0f;
        m_strokeDirInitialized = false;
        clearStrokeTime();
    }

    /// Capture brush parameters into an immutable dab point.
    /// If stroke is active, the dab is appended to the stroke dab buffer.
    /// Applies scatter when enabled.
    DabPoint recordDabPoint(float worldX, float worldY, float radiusOverride = -1.0f,
        float strokeDirection = 0.0f, bool strokeDirectionAvailable = false)
    {
        // Suppress the very first dab of a stroke when the brush angle is
        // driven by stroke direction but no direction is known yet. Without
        // this, the click-stamp (and any pre-deferral dabs) paint with the
        // brush's base angle, then dabs after the head reorient to the path
        // direction — producing the visible orientation step at the head of
        // the stroke. Only the angle dynamic is checked because it's the
        // one whose mismatch is visually obvious; other direction-bound
        // dynamics (size, scatter, ...) don't produce a comparable artifact.
        if (m_strokeActive && m_strokeDabs.empty() && !strokeDirectionAvailable
            && angleBoundToStrokeDirection()) {
            DabPoint suppressed {};
            suppressed.alpha = 0;
            return suppressed;
        }
        const uint32_t randomSeed = dabRandomSeed(m_strokeDabs.size());
        const int32_t ix = static_cast<int32_t>(std::floor(worldX));
        const int32_t iy = static_cast<int32_t>(std::floor(worldY));
        const float randomValue = hash01(ix, iy, randomSeed);
        auto inputContext = currentInputContext(randomValue, 0.0f);
        inputContext.pressure = std::clamp(m_pressure, 0.0f, 1.0f);
        inputContext.strokeElapsedSeconds = m_strokeElapsedSeconds;
        inputContext.strokeTimeAvailable = m_strokeTimeAvailable;
        inputContext.strokeDirection = std::clamp(strokeDirection, 0.0f, 1.0f);
        inputContext.strokeDirectionAvailable = strokeDirectionAvailable;
        populateSettingRandomValues(inputContext, ix, iy, randomSeed);
        const auto dynamicState = evaluateDynamicsForInput(inputContext);

        DabPoint dab;
        dab.worldX = worldX;
        dab.worldY = worldY;
        dab.baseWorldX = worldX;
        dab.baseWorldY = worldY;
        dab.pressure = m_pressure;
        dab.strokeElapsedSeconds = m_strokeElapsedSeconds;
        dab.strokeTimeAvailable = m_strokeTimeAvailable;
        dab.textureAmount = dynamicState.textureAmount;
        dab.textureScale = dynamicState.textureScale;
        dab.textureContrast = dynamicState.textureContrast;
        dab.textureDepth = dynamicState.textureDepth;
        dab.textureBlend = dynamicState.textureBlend;
        dab.textureEdgeBoost = dynamicState.textureEdgeBoost;
        dab.startTaper = dynamicState.startTaper;
        dab.endTaper = dynamicState.endTaper;
        dab.postCorrection = dynamicState.postCorrection;
        dab.startCorrectionLength = dynamicState.startCorrectionLength;
        dab.endCorrectionLength = dynamicState.endCorrectionLength;
        dab.scatterPosition = dynamicState.scatterPosition;
        const float baseRadius = (radiusOverride > 0.0f) ? radiusOverride : m_radius;
        dab.radius = std::max(0.5f, baseRadius * dynamicState.radiusMultiplier);
        dab.baseRadius = dab.radius;
        if (m_brushFeather) {
            // Photoshop-style: guarantee a minimum absolute soft-edge width in
            // pixels so feather stays visible on small brushes instead of
            // collapsing to a fraction of a pixel as it did when we only capped
            // hardness at 0.995.
            constexpr float kFeatherMinPixels = 1.5f;
            const float minSoftness
                = std::min(1.0f, kFeatherMinPixels / std::max(dab.radius, 0.5f));
            const float maxHardness = std::max(0.0f, 1.0f - minSoftness);
            dab.hardness = std::min(dynamicState.hardness, maxHardness);
        } else {
            dab.hardness = dynamicState.hardness;
        }
        dab.roundness = dynamicState.roundness;
        dab.angleDegrees = dynamicState.angleDegrees;
        dab.useMaxBlend = useMaxBlendForCurrentMode();
        dab.colorR = m_r;
        dab.colorG = m_g;
        dab.colorB = m_b;
        applyColorAdjustments(dynamicState.colorHue, dynamicState.colorLightness,
            dynamicState.colorSaturation, dab.colorR, dab.colorG, dab.colorB);
        if (m_eraseMode) {
            // The eraser's dab RGB is unused on a normal layer (flatten does
            // destination-out, which only reads alpha). On a layer mask the
            // eraser instead paints BLACK (hides), so force the dab to black:
            // this keeps the stroke buffer's luminance at 0 and makes the live
            // mask-edit preview (which reads lum(stroke)) match the committed
            // black src-over exactly. No effect on normal-layer erasing.
            dab.colorR = 0;
            dab.colorG = 0;
            dab.colorB = 0;
        }
        dab.alpha = computeFlowDabAlpha(dynamicState.flow, dynamicState.opacityMultiplier);
        dab.baseAlpha = dab.alpha;

        if (dynamicState.scatterPosition > 0.0001f) {
            const float rx = hash01(ix, iy, randomSeed) * 2.0f - 1.0f;
            const float ry = hash01(ix + 7, iy + 13, randomSeed + 1u) * 2.0f - 1.0f;
            const float maxOffset = dab.radius * dynamicState.scatterPosition * 2.0f;
            dab.worldX += rx * maxOffset;
            dab.worldY += ry * maxOffset;
        }

        if (m_strokeActive && dab.alpha != 0) {
            m_strokeDabs.push_back(dab);
        }

        return dab;
    }

    /// Stamp the brush at a world-space position.
    /// If stroke is active, stores a dab point and draws to stroke buffer.
    /// Blend mode is selected explicitly by flowBlendMode parameter.
    /// Otherwise draws directly to grid with src-over (legacy).
    void stamp(TileGrid& grid, float worldX, float worldY, const TileGrid* selectionMask = nullptr)
    {
        TileGrid& target = m_strokeActive ? m_strokeBuffer : grid;
        DabPoint dab = recordDabPoint(worldX, worldY);
        if (dab.alpha == 0)
            return;
        const bool maxBlend = m_strokeActive ? dab.useMaxBlend : false;
        rasterizeDab(target, dab, selectionMask, maxBlend);
    }

    /// Stroke from pointA to pointB with spacing
    void strokeTo(TileGrid& grid, float fromX, float fromY, float toX, float toY,
        const TileGrid* selectionMask = nullptr, float fromStrokeElapsedSeconds = 0.0f,
        float toStrokeElapsedSeconds = 0.0f, bool strokeTimeAvailable = false)
    {
        if (!strokeTimeAvailable && m_strokeTimeAvailable) {
            fromStrokeElapsedSeconds = m_strokeElapsedSeconds;
            toStrokeElapsedSeconds = m_strokeElapsedSeconds;
            strokeTimeAvailable = true;
        }
        std::vector<DabPoint> segmentDabs;
        appendInterpolatedStrokeDabs(fromX, fromY, toX, toY, m_pressure, m_pressure, segmentDabs,
            fromStrokeElapsedSeconds, toStrokeElapsedSeconds, strokeTimeAvailable);
        TileGrid& target = m_strokeActive ? m_strokeBuffer : grid;
        for (const DabPoint& dab : segmentDabs) {
            const bool maxBlend = m_strokeActive ? dab.useMaxBlend : false;
            rasterizeDab(target, dab, selectionMask, maxBlend);
        }
    }

    /// Stroke from pointA to pointB with spacing and pressure-driven size interpolation.
    void strokeToInterpolatedSize(TileGrid& grid, float fromX, float fromY, float toX, float toY,
        float fromPressure, float toPressure, const TileGrid* selectionMask = nullptr,
        float fromStrokeElapsedSeconds = 0.0f, float toStrokeElapsedSeconds = 0.0f,
        bool strokeTimeAvailable = false)
    {
        if (!strokeTimeAvailable && m_strokeTimeAvailable) {
            fromStrokeElapsedSeconds = m_strokeElapsedSeconds;
            toStrokeElapsedSeconds = m_strokeElapsedSeconds;
            strokeTimeAvailable = true;
        }
        std::vector<DabPoint> segmentDabs;
        appendInterpolatedStrokeDabs(fromX, fromY, toX, toY, fromPressure, toPressure, segmentDabs,
            fromStrokeElapsedSeconds, toStrokeElapsedSeconds, strokeTimeAvailable);
        TileGrid& target = m_strokeActive ? m_strokeBuffer : grid;
        for (const DabPoint& dab : segmentDabs) {
            const bool maxBlend = m_strokeActive ? dab.useMaxBlend : false;
            rasterizeDab(target, dab, selectionMask, maxBlend);
        }
    }

    /// Generate distance-based dabs for a stroke segment.
    /// Keeps an internal carry so event rate does not affect dab spacing.
    void appendInterpolatedStrokeDabs(float fromX, float fromY, float toX, float toY,
        float fromPressure, float toPressure, std::vector<DabPoint>& outDabs,
        float fromStrokeElapsedSeconds = 0.0f, float toStrokeElapsedSeconds = 0.0f,
        bool strokeTimeAvailable = false)
    {
        const float dx = toX - fromX;
        const float dy = toY - fromY;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= std::numeric_limits<float>::epsilon()) {
            // Liquify "dwell": the position-based modes (twirl/bloat/pucker)
            // keep applying while the brush is held still. The dwell timer feeds
            // zero-movement segments here at a steady rate — emit one dab each so
            // the warp accumulates over TIME, not only over distance. Push (mode
            // 0) needs travel, so it is excluded.
            if (m_liquifyMode && m_liquifyToolMode != 0) {
                setPressure(toPressure);
                setStrokeElapsedSeconds(toStrokeElapsedSeconds, strokeTimeAvailable);
                DabPoint dab = recordDabPoint(toX, toY);
                if (dab.alpha != 0) {
                    outDabs.push_back(dab);
                }
            }
            return;
        }
        // Leaky integrator over RAW delta vectors (not unit vectors). A 0.5 px
        // jitter delta contributes magnitude 0.5; a clean 30 px segment
        // contributes magnitude 30. Sub-pixel startup noise therefore cannot
        // dominate the accumulator the way unit-vector EMA let it.
        //
        // Fixed 6 px window: small enough that the direction tracks real path
        // turns within one input segment (so corner segments expose a clean
        // pre→post direction span for the per-dab lerp below). A radius-scaled
        // window — which I tried first — made direction inertial: on thick
        // brushes the orientation lagged the path so dabs around a corner
        // looked sheared rather than rotated.
        constexpr float kPiLocal = 3.14159265358979323846f;
        constexpr float kDirectionSmoothingPixels = 6.0f;
        constexpr float kStartupTravelPx = kDirectionSmoothingPixels;

        const float prevTravelTotal = m_strokeTravelTotal;
        const float prevSumX = m_strokeDirSumX;
        const float prevSumY = m_strokeDirSumY;
        const float decay = std::exp(-dist / kDirectionSmoothingPixels);
        m_strokeDirSumX = prevSumX * decay + dx;
        m_strokeDirSumY = prevSumY * decay + dy;
        m_strokeTravelTotal += dist;

        auto unitFrom = [](float sx, float sy, float* outCos, float* outSin) -> bool {
            const float len = std::sqrt(sx * sx + sy * sy);
            if (len < 1e-6f)
                return false;
            *outCos = sx / len;
            *outSin = sy / len;
            return true;
        };

        // The integrator state BEFORE this segment is only reliable once
        // startup deferral has completed. If we used a still-noisy prev state
        // as the t=0 endpoint of the per-dab lerp, the segment that exits
        // deferral would interpolate from jitter into the real direction —
        // smearing the head of the stroke again.
        float prevCos = 1.0f, prevSin = 0.0f, newCos = 1.0f, newSin = 0.0f;
        const bool prevPostStartup = prevTravelTotal >= kStartupTravelPx;
        const bool prevAvail = prevPostStartup && unitFrom(prevSumX, prevSumY, &prevCos, &prevSin);
        const bool newAvail = unitFrom(m_strokeDirSumX, m_strokeDirSumY, &newCos, &newSin);

        // Auto sharp-vs-smooth corner handling. Lerping the dab orientation
        // across the segment is the right behaviour for a gradual curve —
        // it hides the per-segment quantisation. But at a hard corner the
        // lerp passes the brush through the bisector angle on its way from
        // prev to new, and the dabs sitting on that interpolation look
        // sheared instead of "the brush turned the corner". Above a turn
        // threshold the corner reads cleaner as a hard rotation: every dab
        // in the post-corner segment uses the new direction, producing a
        // visible vertex that matches the geometry the user actually drew.
        //
        // Crucial: compare the segment's RAW direction against the prev
        // SMOOTHED direction, not new vs prev (both smoothed). At a real
        // corner formed across several short segments, the integrator's
        // new state has only partially rotated toward the post-corner
        // direction — comparing smoothed-to-smoothed never crosses the
        // threshold and the curl artifact returns. Raw-vs-smoothed catches
        // the user's intent on the first segment that turns.
        //
        // dot is cos(turn angle); 0.5 corresponds to 60°.
        const float rawCos = dx / dist;
        const float rawSin = dy / dist;
        const float prevToRawDot = prevAvail ? prevCos * rawCos + prevSin * rawSin : 1.0f;
        constexpr float kHardCornerCosThreshold = 0.5f;
        const bool hardCorner = prevAvail && prevToRawDot < kHardCornerCosThreshold;

        // Snap the integrator to the raw input direction at a hard corner.
        // Without this, newDir is only the partially-rotated smoothed state,
        // and the post-corner dabs would still be biased toward the pre-
        // corner heading. The snap also seeds the next segment from the
        // actual post-corner direction so subsequent smoothing is clean.
        if (hardCorner) {
            m_strokeDirSumX = dx;
            m_strokeDirSumY = dy;
            newCos = rawCos;
            newSin = rawSin;
        }

        if (newAvail) {
            m_strokeDirCos = newCos;
            m_strokeDirSin = newSin;
            m_strokeDirInitialized = true;
        }

        // Per-dab direction inside this segment: when the corner is gentle,
        // vector-space lerp on the smoothed unit endpoints to distribute the
        // orientation change across the segment's dabs (kills the seam that
        // a single snap would leave). On a hard corner, every dab uses the
        // post-corner direction — see the comment above.
        auto directionAt = [&](float t, float* outDir, bool* outAvail) {
            if (!newAvail) {
                *outDir = 0.0f;
                *outAvail = false;
                return;
            }
            float c = newCos, s = newSin;
            if (prevAvail && !hardCorner) {
                c = prevCos * (1.0f - t) + newCos * t;
                s = prevSin * (1.0f - t) + newSin * t;
                const float l = std::sqrt(c * c + s * s);
                if (l < 1e-6f) {
                    c = (t < 0.5f) ? prevCos : newCos;
                    s = (t < 0.5f) ? prevSin : newSin;
                } else {
                    c /= l;
                    s /= l;
                }
            }
            float deg = std::atan2(s, c) * 180.0f / kPiLocal;
            if (deg < 0.0f)
                deg += 360.0f;
            *outDir = deg / 360.0f;
            *outAvail = true;
        };

        // Blur is idempotent within a stroke (re-blurring the same region wastes
        // work), so we force a coarse minimum spacing for it. Smudge is NOT
        // idempotent — each dab drags pigment by (curr - prev), so dense dab
        // spacing is required to produce a smooth drag instead of visible bands.
        const float minSpacingFactor = m_blurMode ? 0.35f : ruwa::core::brushes::kBrushSpacingMin;
        const auto spacingStepAt = [&](float t) {
            const float pressure = fromPressure + (toPressure - fromPressure) * t;
            const float strokeElapsedSeconds = fromStrokeElapsedSeconds
                + (toStrokeElapsedSeconds - fromStrokeElapsedSeconds) * t;
            const float x = fromX + dx * t;
            const float y = fromY + dy * t;
            float dirT = 0.0f;
            bool dirAvailT = false;
            directionAt(t, &dirT, &dirAvailT);
            const float radius = effectiveSpacingRadiusForDabInput(pressure, x, y,
                m_strokeDabs.size(), strokeElapsedSeconds, strokeTimeAvailable, dirT, dirAvailT);
            const float spacingFactor
                = std::clamp(effectiveSpacingForDabInput(pressure, x, y, m_strokeDabs.size(),
                                 strokeElapsedSeconds, strokeTimeAvailable, dirT, dirAvailT),
                    minSpacingFactor, ruwa::core::brushes::kBrushSpacingMax);
            return std::max(1.0f, std::max(0.5f, radius) * spacingFactor);
        };

        float spacingStep = spacingStepAt(0.0f);
        float sinceLast = std::clamp(m_spacingDistanceSinceLastDab, 0.0f, spacingStep);
        float traveled = 0.0f;

        // Only direction-bound-angle brushes need the deferred-startup path.
        // For everything else, dabs at the head of the stroke have no
        // visible orientation step, so we skip deferral and place dabs
        // normally — preserving the click-stamp + immediate stroke response
        // that users expect.
        const bool needsDirectionDeferral = angleBoundToStrokeDirection();

        if (needsDirectionDeferral && m_strokeTravelTotal < kStartupTravelPx) {
            // No dabs placed yet on this segment: recordDabPoint also
            // suppresses the click-stamp for this brush class. Keep the
            // spacing accumulator zeroed so post-deferral dabs don't pile
            // up at the resume point.
            m_spacingDistanceSinceLastDab = 0.0f;
            setPressure(toPressure);
            setStrokeElapsedSeconds(toStrokeElapsedSeconds, strokeTimeAvailable);
            return;
        }

        if (m_strokeActive && m_strokeDabs.empty()) {
            // First emitted dab of the stroke. For direction-bound brushes
            // we've just exited deferral, so newAvail/newDir reflect a
            // settled direction; the dab here lands at the segment start
            // (a few pixels past the click point) but oriented correctly.
            // For non-direction brushes the click-stamp normally already
            // placed a dab, so this only fires on the rare path where stamp
            // wasn't called.
            setPressure(fromPressure);
            setStrokeElapsedSeconds(fromStrokeElapsedSeconds, strokeTimeAvailable);
            float dir0 = 0.0f;
            bool avail0 = false;
            directionAt(0.0f, &dir0, &avail0);
            DabPoint initialDab = recordDabPoint(fromX, fromY, -1.0f, dir0, avail0);
            if (initialDab.alpha != 0) {
                outDabs.push_back(initialDab);
            }
            sinceLast = 0.0f;
            spacingStep = spacingStepAt(0.0f);
        }

        while (sinceLast + (dist - traveled) >= spacingStep) {
            const float need = spacingStep - sinceLast;
            traveled += need;
            const float t = std::clamp(traveled / dist, 0.0f, 1.0f);

            const float pressure = fromPressure + (toPressure - fromPressure) * t;
            const float strokeElapsedSeconds = fromStrokeElapsedSeconds
                + (toStrokeElapsedSeconds - fromStrokeElapsedSeconds) * t;
            const float x = fromX + dx * t;
            const float y = fromY + dy * t;

            setPressure(pressure);
            setStrokeElapsedSeconds(strokeElapsedSeconds, strokeTimeAvailable);
            float dirT = 0.0f;
            bool availT = false;
            directionAt(t, &dirT, &availT);
            DabPoint dab = recordDabPoint(x, y, -1.0f, dirT, availT);
            if (dab.alpha != 0) {
                outDabs.push_back(dab);
            }
            sinceLast = 0.0f;
            spacingStep = spacingStepAt(t);
        }

        sinceLast += (dist - traveled);
        spacingStep = spacingStepAt(1.0f);
        if (sinceLast >= spacingStep) {
            sinceLast = std::fmod(sinceLast, spacingStep);
        }
        m_spacingDistanceSinceLastDab = sinceLast;
        setPressure(toPressure);
        setStrokeElapsedSeconds(toStrokeElapsedSeconds, strokeTimeAvailable);
    }

    /// Rasterize an externally provided dab point.
    /// Uses dab.useMaxBlend when stroke is active; otherwise src-over.
    void stampFromDab(TileGrid& grid, const DabPoint& dab, const TileGrid* selectionMask = nullptr)
    {
        if (dab.alpha == 0)
            return;
        TileGrid& target = m_strokeActive ? m_strokeBuffer : grid;
        const bool maxBlend = m_strokeActive ? dab.useMaxBlend : false;
        rasterizeDab(target, dab, selectionMask, maxBlend);
    }

    /// The batched GPU replay consumes the visual values already captured in
    /// each DabPoint (geometry, color and alpha). Procedural texture sampling
    /// is the exception: its controls are currently uniforms shared by the
    /// whole batch, so per-dab texture dynamics still need the CPU replay path.
    bool hasDynamicsRequiringCpuReplay() const { return hasDynamicTextureBinding(); }

    bool hasActiveStrokeDirectionDynamicsBinding() const
    {
        if (!m_useBrushSettingsModel) {
            return false;
        }
        for (const auto& slot : m_brushSettingsModel.dynamics.settingSlots) {
            if (slot.binding(ruwa::core::brushes::BrushInputSourceKey::StrokeDirection)
                    .isActive()) {
                return true;
            }
        }
        return false;
    }

    /// Narrower check: is the dab's shape-angle slot specifically driven by
    /// stroke direction? Used to gate the head-of-stroke suppression: only
    /// when angle reacts to direction does an orientation step at the head
    /// produce a visible artifact.
    bool angleBoundToStrokeDirection() const
    {
        if (!m_useBrushSettingsModel) {
            return false;
        }
        const auto& angleSlot = m_brushSettingsModel.dynamics.slotForSetting(
            ruwa::core::brushes::BrushDynamicsSettingKey::ShapeAngle);
        return angleSlot.binding(ruwa::core::brushes::BrushInputSourceKey::StrokeDirection)
            .isActive();
    }

    bool hasInitializedStrokeDirection() const { return m_strokeDirInitialized; }

    /// Rotation that StrokeDirection dynamics specifically adds to the dab
    /// angle at the current path direction (in radians, clockwise in screen
    /// y-down). Subtracted: any contribution from non-direction bindings on
    /// the angle slot (random, pressure, etc.) — those vary per-dab and
    /// can't be represented in a single cursor preview, so we isolate the
    /// direction-driven part. Returns 0 when direction is not yet known
    /// or no direction binding targets the angle slot.
    /// Used by the brush cursor overlay to spin the cursor with the path.
    float previewDabRotationDeltaRadians() const
    {
        if (!m_strokeDirInitialized || !m_useBrushSettingsModel) {
            return 0.0f;
        }
        constexpr float kPi = 3.14159265358979323846f;
        const auto& angleSlot = m_brushSettingsModel.dynamics.slotForSetting(
            ruwa::core::brushes::BrushDynamicsSettingKey::ShapeAngle);
        if (!angleSlot.binding(ruwa::core::brushes::BrushInputSourceKey::StrokeDirection)
                .isActive()) {
            return 0.0f;
        }

        float strokeDirDeg = std::atan2(m_strokeDirSin, m_strokeDirCos) * (180.0f / kPi);
        if (strokeDirDeg < 0.0f)
            strokeDirDeg += 360.0f;
        const float strokeDirNorm = strokeDirDeg / 360.0f;

        auto withDir = currentInputContext(0.0f, 0.0f);
        withDir.strokeDirection = std::clamp(strokeDirNorm, 0.0f, 1.0f);
        withDir.strokeDirectionAvailable = true;
        auto withoutDir = withDir;
        withoutDir.strokeDirectionAvailable = false;

        const float angleWith
            = ruwa::core::brushes::evaluateDynamicsSlotValue(m_angleDegrees, angleSlot, withDir);
        const float angleWithout
            = ruwa::core::brushes::evaluateDynamicsSlotValueExcludingSource(m_angleDegrees,
                angleSlot, withoutDir, ruwa::core::brushes::BrushInputSourceKey::StrokeDirection);
        float deltaDeg = angleWith - angleWithout;
        // Wrap to the shortest-arc representative in (-180, 180]; otherwise
        // a 359° → 1° transition would visually spin the cursor the long way.
        if (deltaDeg > 180.0f)
            deltaDeg -= 360.0f;
        else if (deltaDeg < -180.0f)
            deltaDeg += 360.0f;
        return deltaDeg * (kPi / 180.0f);
    }

    bool hasDynamicColorBinding() const
    {
        if (!m_useBrushSettingsModel) {
            return false;
        }
        using ruwa::core::brushes::BrushDynamicsSettingKey;
        return m_brushSettingsModel.dynamics.slotForSetting(BrushDynamicsSettingKey::ColorHue)
                   .hasActiveBindings()
            || m_brushSettingsModel.dynamics.slotForSetting(BrushDynamicsSettingKey::ColorLightness)
                   .hasActiveBindings()
            || m_brushSettingsModel.dynamics
                   .slotForSetting(BrushDynamicsSettingKey::ColorSaturation)
                   .hasActiveBindings();
    }

private:
    struct DabCoverageBounds {
        float axisAlignedExtent = 0.0f;
        float rotationInvariantExtent = 0.0f;
    };

    DabCoverageBounds computeDabCoverageBounds(float radius, float hardness, float roundness,
        float angleDegrees, bool includeRasterPadding) const
    {
        static_cast<void>(hardness);
        if (!std::isfinite(radius) || radius <= 0.0f) {
            return {};
        }

        const float rasterPadding = includeRasterPadding ? 1.0f : 0.0f;
        if (m_dabType <= 0) {
            return { radius + rasterPadding, radius + rasterPadding };
        }
        if (m_dabXScale <= 0.0001f || m_dabYScale <= 0.0001f) {
            return { rasterPadding, rasterPadding };
        }

        const float halfShapeX = radius * m_dabXScale;
        const float halfShapeY = radius * m_dabYScale;
        const float clampedRoundness = std::max(0.01f, std::clamp(roundness, 0.0f, 1.0f));
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        const float brushAngle = angleDegrees * kDegToRad;
        const float shapeAngle = m_dabRotation * kDegToRad;
        const float brushCos = std::cos(brushAngle);
        const float brushSin = std::sin(brushAngle);
        const float shapeCos = std::cos(shapeAngle);
        const float shapeSin = std::sin(shapeAngle);

        float maxAbsX = 0.0f;
        float maxAbsY = 0.0f;
        float maxDistance = 0.0f;
        const float signs[2] = { -1.0f, 1.0f };
        for (float signX : signs) {
            for (float signY : signs) {
                const float shapeX = signX * halfShapeX;
                const float shapeY = signY * halfShapeY;
                const float brushX = shapeX * shapeCos + shapeY * shapeSin;
                const float brushY = (-shapeX * shapeSin + shapeY * shapeCos) * clampedRoundness;
                const float worldX = brushX * brushCos - brushY * brushSin;
                const float worldY = brushX * brushSin + brushY * brushCos;
                maxAbsX = std::max(maxAbsX, std::abs(worldX));
                maxAbsY = std::max(maxAbsY, std::abs(worldY));
                maxDistance = std::max(maxDistance, std::hypot(brushX, brushY));
            }
        }

        return { std::max(maxAbsX, maxAbsY) + rasterPadding, maxDistance + rasterPadding };
    }

    struct ProceduralTextureTile {
        std::vector<uint8_t> alpha; // TILE_SIZE * TILE_SIZE
    };

    static void normalizeRange(float& minValue, float& maxValue)
    {
        minValue = std::clamp(minValue, 0.0f, 1.0f);
        maxValue = std::clamp(maxValue, 0.0f, 1.0f);
        if (minValue > maxValue) {
            std::swap(minValue, maxValue);
        }
    }

    static float rangeValueForPressure(float minValue, float maxValue, float pressure)
    {
        const float p = std::clamp(pressure, 0.0f, 1.0f);
        return minValue + (maxValue - minValue) * p;
    }

    bool hasRadiusPressureResponse() const
    {
        if (!m_useBrushSettingsModel) {
            return m_sizePressureEnabled;
        }
        return m_brushSettingsModel.dynamics
                   .slotForSetting(ruwa::core::brushes::BrushDynamicsSettingKey::RadiusMultiplier)
                   .hasActiveBindings()
            || m_brushSettingsModel.sizePressureEnabled;
    }

    bool hasOpacityPressureResponse() const
    {
        if (!m_useBrushSettingsModel) {
            return m_opacityPressureEnabled;
        }
        return m_brushSettingsModel.dynamics
                   .slotForSetting(ruwa::core::brushes::BrushDynamicsSettingKey::OpacityMultiplier)
                   .hasActiveBindings()
            || m_brushSettingsModel.opacityPressureEnabled;
    }

    bool hasDynamicTextureBinding() const
    {
        if (!m_useBrushSettingsModel) {
            return false;
        }
        using ruwa::core::brushes::BrushDynamicsSettingKey;
        return m_brushSettingsModel.dynamics.slotForSetting(BrushDynamicsSettingKey::TextureAmount)
                   .hasActiveBindings()
            || m_brushSettingsModel.dynamics.slotForSetting(BrushDynamicsSettingKey::TextureScale)
                   .hasActiveBindings()
            || m_brushSettingsModel.dynamics
                   .slotForSetting(BrushDynamicsSettingKey::TextureContrast)
                   .hasActiveBindings()
            || m_brushSettingsModel.dynamics.slotForSetting(BrushDynamicsSettingKey::TextureDepth)
                   .hasActiveBindings()
            || m_brushSettingsModel.dynamics.slotForSetting(BrushDynamicsSettingKey::TextureBlend)
                   .hasActiveBindings()
            || m_brushSettingsModel.dynamics
                   .slotForSetting(BrushDynamicsSettingKey::TextureEdgeBoost)
                   .hasActiveBindings();
    }

    float scatterPositionValue() const
    {
        return m_useBrushSettingsModel ? evaluateDynamicsForCurrentInput().scatterPosition
                                       : m_scatterPosition;
    }

    float startTaperValue() const
    {
        return m_useBrushSettingsModel ? evaluateDynamicsForCurrentInput().startTaper
                                       : m_startTaper;
    }

    float endTaperValue() const
    {
        return m_useBrushSettingsModel ? evaluateDynamicsForCurrentInput().endTaper : m_endTaper;
    }

    float startCorrectionLengthValue() const
    {
        return m_useBrushSettingsModel ? evaluateDynamicsForCurrentInput().startCorrectionLength
                                       : m_startCorrectionLength;
    }

    float endCorrectionLengthValue() const
    {
        return m_useBrushSettingsModel ? evaluateDynamicsForCurrentInput().endCorrectionLength
                                       : m_endCorrectionLength;
    }

    ruwa::core::brushes::BrushInputContext currentInputContext(
        float randomValue = 0.0f, float strokeProgress = 0.0f) const
    {
        ruwa::core::brushes::BrushInputContext inputContext;
        inputContext.pressure = std::clamp(m_pressure, 0.0f, 1.0f);
        inputContext.randomValue = std::clamp(randomValue, 0.0f, 1.0f);
        inputContext.strokeProgress = std::clamp(strokeProgress, 0.0f, 1.0f);
        inputContext.strokeElapsedSeconds = m_strokeElapsedSeconds;
        inputContext.strokeTimeAvailable = m_strokeTimeAvailable;
        return inputContext;
    }

    void populateSettingRandomValues(ruwa::core::brushes::BrushInputContext& inputContext,
        int32_t ix, int32_t iy, uint32_t dabSeed) const
    {
        // Per-dab hot path. We only need a random value for setting slots that
        // actually have an active binding to BrushInputSourceKey::RandomValue.
        // m_randomBoundSettingMask is precomputed by refreshRandomBoundSettingMask()
        // when settings change. Slots without a random binding stay marked
        // unavailable, which causes brushInputSourceValue() to fall back to the
        // shared inputContext.randomValue without consulting this array.
        const std::size_t slotCount = inputContext.settingRandomValues.size();
        uint32_t remaining = m_useBrushSettingsModel ? m_randomBoundSettingMask : 0u;
        // Always clear availability flags for slots we will not fill below.
        for (std::size_t i = 0; i < slotCount; ++i) {
            inputContext.settingRandomValueAvailable[i] = false;
        }
        while (remaining != 0u) {
            // Index of lowest set bit. countr_zero is constexpr on MSVC/GCC/clang.
            const std::size_t settingIndex = static_cast<std::size_t>(std::countr_zero(remaining));
            remaining &= remaining - 1u;
            if (settingIndex >= slotCount)
                continue;
            const uint32_t settingSeed
                = dabSeed + static_cast<uint32_t>(settingIndex + 1u) * 2246822519u;
            inputContext.settingRandomValues[settingIndex]
                = hash01(ix + static_cast<int32_t>(settingIndex * 17u),
                    iy + static_cast<int32_t>(settingIndex * 29u), settingSeed);
            inputContext.settingRandomValueAvailable[settingIndex] = true;
        }
    }

    void refreshRandomBoundSettingMask()
    {
        uint32_t mask = 0u;
        if (!m_useBrushSettingsModel) {
            m_randomBoundSettingMask = 0u;
            return;
        }
        const auto& settingSlotsArr = m_brushSettingsModel.dynamics.settingSlots;
        for (std::size_t i = 0; i < settingSlotsArr.size(); ++i) {
            const auto& randomBinding
                = settingSlotsArr[i].binding(ruwa::core::brushes::BrushInputSourceKey::RandomValue);
            if (randomBinding.isActive()) {
                mask |= (1u << i);
            }
        }
        m_randomBoundSettingMask = mask;
    }

    ruwa::core::brushes::BrushEvaluatedState evaluateDynamicsForCurrentInput() const
    {
        return evaluateDynamicsForInput(currentInputContext());
    }

    ruwa::core::brushes::BrushEvaluatedState evaluateDynamicsForInput(
        const ruwa::core::brushes::BrushInputContext& inputContext) const
    {
        if (!m_useBrushSettingsModel) {
            ruwa::core::brushes::BrushEvaluatedState legacy;
            legacy.radiusMultiplier = m_sizePressureEnabled
                ? rangeValueForPressure(m_sizePressureMin, m_sizePressureMax, inputContext.pressure)
                : 1.0f;
            legacy.opacityMultiplier = m_opacityPressureEnabled
                ? rangeValueForPressure(
                      m_opacityPressureMin, m_opacityPressureMax, inputContext.pressure)
                : 1.0f;
            legacy.hardness = m_hardness;
            legacy.spacing = m_spacing;
            legacy.flow = effectiveFlowForPressure(inputContext.pressure);
            legacy.roundness = m_roundness;
            legacy.angleDegrees = m_angleDegrees;
            legacy.textureAmount = m_textureAmount;
            legacy.textureScale = m_textureScale;
            legacy.textureContrast = m_textureContrast;
            legacy.textureDepth = m_textureDepth;
            legacy.textureBlend = m_textureBlend;
            legacy.textureEdgeBoost = m_textureEdgeBoost;
            legacy.colorHue = 0.0f;
            legacy.colorLightness = 1.0f;
            legacy.colorSaturation = 1.0f;
            legacy.scatterPosition = m_scatterPosition;
            legacy.startTaper = m_startTaper;
            legacy.endTaper = m_endTaper;
            legacy.postCorrection = m_postCorrection;
            legacy.stabilization = m_stabilization;
            legacy.startCorrectionLength = m_startCorrectionLength;
            legacy.endCorrectionLength = m_endCorrectionLength;
            return legacy;
        }

        return ruwa::core::brushes::evaluateBrushDynamics(m_brushSettingsModel, inputContext);
    }

    ruwa::core::brushes::BrushEvaluatedState evaluateDynamicsForPressure(float pressure,
        float randomValue, float strokeProgress, float strokeElapsedSeconds = 0.0f,
        bool strokeTimeAvailable = false) const
    {
        auto inputContext = currentInputContext(randomValue, strokeProgress);
        inputContext.pressure = std::clamp(pressure, 0.0f, 1.0f);
        inputContext.strokeElapsedSeconds = std::max(0.0f, strokeElapsedSeconds);
        inputContext.strokeTimeAvailable = strokeTimeAvailable;
        return evaluateDynamicsForInput(inputContext);
    }

    float effectiveSpacingForPressure(
        float pressure, float strokeElapsedSeconds = 0.0f, bool strokeTimeAvailable = false) const
    {
        return evaluateDynamicsForPressure(
            pressure, 0.0f, 0.0f, strokeElapsedSeconds, strokeTimeAvailable)
            .spacing;
    }

    float effectiveSpacingForDabInput(float pressure, float worldX, float worldY, size_t dabIndex,
        float strokeElapsedSeconds, bool strokeTimeAvailable, float strokeDirection = 0.0f,
        bool strokeDirectionAvailable = false) const
    {
        const int32_t ix = static_cast<int32_t>(std::floor(worldX));
        const int32_t iy = static_cast<int32_t>(std::floor(worldY));
        const uint32_t dabSeed = dabRandomSeed(dabIndex);
        const float randomValue = hash01(ix, iy, dabSeed);
        auto inputContext = currentInputContext(randomValue, 0.0f);
        inputContext.pressure = std::clamp(pressure, 0.0f, 1.0f);
        inputContext.strokeElapsedSeconds = std::max(0.0f, strokeElapsedSeconds);
        inputContext.strokeTimeAvailable = strokeTimeAvailable;
        inputContext.strokeDirection = std::clamp(strokeDirection, 0.0f, 1.0f);
        inputContext.strokeDirectionAvailable = strokeDirectionAvailable;
        populateSettingRandomValues(inputContext, ix, iy, dabSeed);
        return evaluateDynamicsForInput(inputContext).spacing;
    }

    float effectiveRadiusForDabInput(float pressure, float worldX, float worldY, size_t dabIndex,
        float strokeElapsedSeconds, bool strokeTimeAvailable) const
    {
        const int32_t ix = static_cast<int32_t>(std::floor(worldX));
        const int32_t iy = static_cast<int32_t>(std::floor(worldY));
        const uint32_t dabSeed = dabRandomSeed(dabIndex);
        const float randomValue = hash01(ix, iy, dabSeed);
        auto inputContext = currentInputContext(randomValue, 0.0f);
        inputContext.pressure = std::clamp(pressure, 0.0f, 1.0f);
        inputContext.strokeElapsedSeconds = std::max(0.0f, strokeElapsedSeconds);
        inputContext.strokeTimeAvailable = strokeTimeAvailable;
        populateSettingRandomValues(inputContext, ix, iy, dabSeed);
        return std::max(0.5f, m_radius * evaluateDynamicsForInput(inputContext).radiusMultiplier);
    }

    float effectiveSpacingRadiusForDabInput(float pressure, float worldX, float worldY,
        size_t dabIndex, float strokeElapsedSeconds, bool strokeTimeAvailable,
        float strokeDirection = 0.0f, bool strokeDirectionAvailable = false) const
    {
        if (!m_useBrushSettingsModel) {
            return effectiveRadiusForDabInput(
                pressure, worldX, worldY, dabIndex, strokeElapsedSeconds, strokeTimeAvailable);
        }

        const int32_t ix = static_cast<int32_t>(std::floor(worldX));
        const int32_t iy = static_cast<int32_t>(std::floor(worldY));
        const uint32_t dabSeed = dabRandomSeed(dabIndex);
        const float randomValue = hash01(ix, iy, dabSeed);
        auto inputContext = currentInputContext(randomValue, 0.0f);
        inputContext.pressure = std::clamp(pressure, 0.0f, 1.0f);
        inputContext.strokeElapsedSeconds = std::max(0.0f, strokeElapsedSeconds);
        inputContext.strokeTimeAvailable = strokeTimeAvailable;
        inputContext.strokeDirection = std::clamp(strokeDirection, 0.0f, 1.0f);
        inputContext.strokeDirectionAvailable = strokeDirectionAvailable;
        populateSettingRandomValues(inputContext, ix, iy, dabSeed);

        const auto& radiusSlot = m_brushSettingsModel.dynamics.slotForSetting(
            ruwa::core::brushes::BrushDynamicsSettingKey::RadiusMultiplier);
        const float radiusMultiplier = ruwa::core::brushes::clampNonNegative(
            ruwa::core::brushes::evaluateDynamicsSlotValueExcludingSource(1.0f, radiusSlot,
                inputContext, ruwa::core::brushes::BrushInputSourceKey::RandomValue));
        return std::max(0.5f, m_radius * radiusMultiplier);
    }

    float effectiveFlowForPressure(float pressure) const
    {
        const float flowScale
            = rangeValueForPressure(m_flowPressureMin, m_flowPressureMax, pressure);
        return std::clamp(m_flow * flowScale, 0.0f, 1.0f);
    }

    bool useMaxBlendForCurrentMode() const
    {
        return m_flowBlendMode == ruwa::core::brushes::BrushSettingsData::FlowBlendMax
            && !hasDynamicColorBinding();
    }

    static uint32_t hash2D(int32_t x, int32_t y, uint32_t seed)
    {
        uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u
            + seed * 982451653u;
        h = (h ^ (h >> 13u)) * 1274126177u;
        return h ^ (h >> 16u);
    }

    static float hash01(int32_t x, int32_t y, uint32_t seed)
    {
        const uint32_t h = hash2D(x, y, seed);
        return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
    }

    static uint32_t dabRandomSeed(size_t dabIndex)
    {
        return static_cast<uint32_t>(dabIndex) * 2654435769u;
    }

    static float smoothstep01(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    static size_t taperAffectedDabCount(float taper, size_t dabCount)
    {
        if (dabCount == 0)
            return 0;
        const float clampedTaper = std::clamp(taper, 0.0f, 1.0f);
        if (clampedTaper <= 0.0001f)
            return 0;
        const size_t affected = static_cast<size_t>(
            std::ceil(clampedTaper * static_cast<float>(kMaxTaperAffectedDabs)));
        return std::min(dabCount, std::max<size_t>(1, affected));
    }

    static float taperScaleForEdgeIndex(size_t edgeIndex, size_t affectedCount)
    {
        if (affectedCount <= 1)
            return 0.0f;
        return std::clamp(
            static_cast<float>(edgeIndex) / static_cast<float>(affectedCount - 1), 0.0f, 1.0f);
    }

    static uint8_t computeDabAlpha(float normalizedOpacity)
    {
        return static_cast<uint8_t>(std::clamp(normalizedOpacity, 0.0f, 1.0f) * 255.0f);
    }

    uint8_t computeFlowDabAlpha(float flow, float opacityMultiplier) const
    {
        const float targetAlpha = std::clamp(flow * opacityMultiplier, 0.0f, 1.0f);
        return computeDabAlpha(targetAlpha);
    }

    static float hueToRgb(float p, float q, float t)
    {
        if (t < 0.0f)
            t += 1.0f;
        if (t > 1.0f)
            t -= 1.0f;
        if (t < 1.0f / 6.0f)
            return p + (q - p) * 6.0f * t;
        if (t < 1.0f / 2.0f)
            return q;
        if (t < 2.0f / 3.0f)
            return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
        return p;
    }

    static void rgbToHsl(
        uint8_t r, uint8_t g, uint8_t b, float& hueDegrees, float& saturation, float& lightness)
    {
        const float rf = static_cast<float>(r) / 255.0f;
        const float gf = static_cast<float>(g) / 255.0f;
        const float bf = static_cast<float>(b) / 255.0f;
        const float maxValue = std::max(rf, std::max(gf, bf));
        const float minValue = std::min(rf, std::min(gf, bf));
        lightness = (maxValue + minValue) * 0.5f;

        if (std::abs(maxValue - minValue) <= 0.000001f) {
            hueDegrees = 0.0f;
            saturation = 0.0f;
            return;
        }

        const float delta = maxValue - minValue;
        saturation = lightness > 0.5f ? delta / (2.0f - maxValue - minValue)
                                      : delta / (maxValue + minValue);

        if (maxValue == rf) {
            hueDegrees = (gf - bf) / delta + (gf < bf ? 6.0f : 0.0f);
        } else if (maxValue == gf) {
            hueDegrees = (bf - rf) / delta + 2.0f;
        } else {
            hueDegrees = (rf - gf) / delta + 4.0f;
        }
        hueDegrees *= 60.0f;
    }

    static void hslToRgb(
        float hueDegrees, float saturation, float lightness, uint8_t& r, uint8_t& g, uint8_t& b)
    {
        const float h = ruwa::core::brushes::normalizeAngleDegrees(hueDegrees) / 360.0f;
        const float s = std::clamp(saturation, 0.0f, 1.0f);
        const float l = std::clamp(lightness, 0.0f, 1.0f);

        float rf = l;
        float gf = l;
        float bf = l;
        if (s > 0.000001f) {
            const float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
            const float p = 2.0f * l - q;
            rf = hueToRgb(p, q, h + 1.0f / 3.0f);
            gf = hueToRgb(p, q, h);
            bf = hueToRgb(p, q, h - 1.0f / 3.0f);
        }

        const float roundedR = std::round(rf * 255.0f);
        const float roundedG = std::round(gf * 255.0f);
        const float roundedB = std::round(bf * 255.0f);
        r = static_cast<uint8_t>(std::clamp(roundedR, 0.0f, 255.0f));
        g = static_cast<uint8_t>(std::clamp(roundedG, 0.0f, 255.0f));
        b = static_cast<uint8_t>(std::clamp(roundedB, 0.0f, 255.0f));
    }

    static void applyColorAdjustments(float hueDegrees, float lightnessMultiplier,
        float saturationMultiplier, uint8_t& r, uint8_t& g, uint8_t& b)
    {
        const float normalizedHue = ruwa::core::brushes::normalizeAngleDegrees(hueDegrees);
        const float clampedLightness = std::clamp(lightnessMultiplier, 0.0f, 2.0f);
        const float clampedSaturation = std::clamp(saturationMultiplier, 0.0f, 2.0f);
        if (normalizedHue <= 0.0001f && std::abs(clampedLightness - 1.0f) <= 0.0001f
            && std::abs(clampedSaturation - 1.0f) <= 0.0001f) {
            return;
        }

        float hue = 0.0f;
        float saturation = 0.0f;
        float lightness = 0.0f;
        rgbToHsl(r, g, b, hue, saturation, lightness);
        hue = ruwa::core::brushes::normalizeAngleDegrees(hue + normalizedHue);
        saturation = std::clamp(saturation * clampedSaturation, 0.0f, 1.0f);
        lightness = std::clamp(lightness * clampedLightness, 0.0f, 1.0f);
        hslToRgb(hue, saturation, lightness, r, g, b);
    }

    static float valueNoise(float x, float y, uint32_t seed)
    {
        const int32_t ix = static_cast<int32_t>(std::floor(x));
        const int32_t iy = static_cast<int32_t>(std::floor(y));
        const float fx = x - static_cast<float>(ix);
        const float fy = y - static_cast<float>(iy);

        const float v00 = hash01(ix, iy, seed);
        const float v10 = hash01(ix + 1, iy, seed);
        const float v01 = hash01(ix, iy + 1, seed);
        const float v11 = hash01(ix + 1, iy + 1, seed);

        const float sx = smoothstep01(fx);
        const float sy = smoothstep01(fy);
        const float vx0 = v00 + (v10 - v00) * sx;
        const float vx1 = v01 + (v11 - v01) * sx;
        return vx0 + (vx1 - vx0) * sy;
    }

    /// Type 0: Procedural — layered value noise with anisotropic streak (pencil/graphite).
    float proceduralPencilGrain(
        float worldX, float worldY, float textureScaleOverride = -1.0f) const
    {
        const float scale = std::clamp(
            textureScaleOverride > 0.0f ? textureScaleOverride : textureScale(), 0.1f, 4.0f);
        const float f0 = 0.018f * scale;
        const float f1 = f0 * 2.13f;
        const float f2 = f1 * 1.97f;

        const float n0 = valueNoise(worldX * f0, worldY * f0, 0xA53F91u);
        const float n1 = valueNoise(worldX * f1, worldY * f1, 0xC17AB1u);
        const float n2 = valueNoise(worldX * f2, worldY * f2, 0x91BB37u);

        const float streak = valueNoise(
            worldX * (f0 * 2.8f) + worldY * (f0 * 0.55f), worldY * (f0 * 0.22f), 0x7F4A21u);
        return std::clamp(n0 * 0.50f + n1 * 0.25f + n2 * 0.10f + streak * 0.15f, 0.0f, 1.0f);
    }

    /// Type 1: Noise — multi-octave value noise, grittier granular feel.
    float proceduralNoiseGrain(float worldX, float worldY, float textureScaleOverride = -1.0f) const
    {
        const float scale = std::clamp(
            textureScaleOverride > 0.0f ? textureScaleOverride : textureScale(), 0.1f, 4.0f);
        const float f0 = 0.025f * scale;
        const float f1 = f0 * 2.5f;
        const float f2 = f1 * 2.2f;

        const float n0 = valueNoise(worldX * f0, worldY * f0, 0xB2C4D1u);
        const float n1 = valueNoise(worldX * f1, worldY * f1, 0x8E3A5Fu);
        const float n2 = valueNoise(worldX * f2, worldY * f2, 0xD7F21Au);

        return std::clamp(n0 * 0.55f + n1 * 0.30f + n2 * 0.15f, 0.0f, 1.0f);
    }

    /// Type 2: Perlin — gradient noise, smoother organic blobs.
    float proceduralPerlinGrain(
        float worldX, float worldY, float textureScaleOverride = -1.0f) const
    {
        const float scale = std::clamp(
            textureScaleOverride > 0.0f ? textureScaleOverride : textureScale(), 0.1f, 4.0f);
        const float f0 = 0.015f * scale;
        const float f1 = f0 * 2.0f;

        const float n0 = gradientNoise(worldX * f0, worldY * f0, 0x5A3C91u);
        const float n1 = gradientNoise(worldX * f1, worldY * f1, 0x7B2D41u);

        return std::clamp(0.5f + n0 * 0.4f + n1 * 0.25f, 0.0f, 1.0f);
    }

    float proceduralGrainByType(
        float worldX, float worldY, float textureScaleOverride = -1.0f) const
    {
        switch (m_textureType) {
        case 1:
            return proceduralNoiseGrain(worldX, worldY, textureScaleOverride);
        case 2:
            return proceduralPerlinGrain(worldX, worldY, textureScaleOverride);
        default:
            return proceduralPencilGrain(worldX, worldY, textureScaleOverride);
        }
    }

    static void grad2(int32_t ix, int32_t iy, uint32_t seed, float& gx, float& gy)
    {
        const uint32_t h = hash2D(ix, iy, seed);
        const float angle = static_cast<float>(h & 0xFFFFu) / 65536.0f * 6.28318530718f;
        gx = std::cos(angle);
        gy = std::sin(angle);
    }

    static float gradientNoise(float x, float y, uint32_t seed)
    {
        const int32_t ix = static_cast<int32_t>(std::floor(x));
        const int32_t iy = static_cast<int32_t>(std::floor(y));
        const float fx = x - static_cast<float>(ix);
        const float fy = y - static_cast<float>(iy);

        float g00x, g00y;
        grad2(ix, iy, seed, g00x, g00y);
        float g10x, g10y;
        grad2(ix + 1, iy, seed, g10x, g10y);
        float g01x, g01y;
        grad2(ix, iy + 1, seed, g01x, g01y);
        float g11x, g11y;
        grad2(ix + 1, iy + 1, seed, g11x, g11y);

        const float d00 = fx * g00x + fy * g00y;
        const float d10 = (fx - 1.0f) * g10x + fy * g10y;
        const float d01 = fx * g01x + (fy - 1.0f) * g01y;
        const float d11 = (fx - 1.0f) * g11x + (fy - 1.0f) * g11y;

        const float sx = smoothstep01(fx);
        const float sy = smoothstep01(fy);
        const float vx0 = d00 + (d10 - d00) * sx;
        const float vx1 = d01 + (d11 - d01) * sx;
        const float v = vx0 + (vx1 - vx0) * sy;
        return std::clamp(v * 0.5f + 0.5f, 0.0f, 1.0f);
    }

    static float hardnessFalloffFromEdgeDistance(float edgeDistance, float hardness)
    {
        if (edgeDistance <= 0.0f) {
            return 0.0f;
        }

        const float clampedHardness = std::clamp(hardness, 0.0f, 1.0f);
        const float softness = std::max(1.0f - clampedHardness, 0.0f);
        if (softness <= 0.0001f) {
            return 1.0f;
        }

        const float x = std::clamp(edgeDistance / softness, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    static float sampleMaskAlphaBilinear(
        const std::vector<uint8_t>& mask, int width, int height, float u, float v)
    {
        if (mask.empty() || width <= 0 || height <= 0) {
            return 0.0f;
        }

        if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
            return 0.0f;
        }

        const float x = u * static_cast<float>(width - 1);
        const float y = v * static_cast<float>(height - 1);
        const int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, width - 1);
        const int y0 = std::clamp(static_cast<int>(std::floor(y)), 0, height - 1);
        const int x1 = std::min(x0 + 1, width - 1);
        const int y1 = std::min(y0 + 1, height - 1);
        const float tx = x - static_cast<float>(x0);
        const float ty = y - static_cast<float>(y0);

        const auto sampleAt = [&](int sx, int sy) {
            return static_cast<float>(mask[static_cast<size_t>(sy * width + sx)]) / 255.0f;
        };

        const float a00 = sampleAt(x0, y0);
        const float a10 = sampleAt(x1, y0);
        const float a01 = sampleAt(x0, y1);
        const float a11 = sampleAt(x1, y1);
        const float ax0 = a00 + (a10 - a00) * tx;
        const float ax1 = a01 + (a11 - a01) * tx;
        return ax0 + (ax1 - ax0) * ty;
    }

    float sampleDabShapeSoftAlpha(float u, float v, float hardness) const
    {
        const float alpha
            = sampleMaskAlphaBilinear(m_dabShapeAlpha, m_dabShapeW, m_dabShapeH, u, v);
        const float softAlpha
            = sampleMaskAlphaBilinear(m_dabShapeSoftAlpha, m_dabShapeW, m_dabShapeH, u, v);
        return dab_shape_falloff::softenAlpha(alpha, softAlpha, hardness);
    }

    float sampleDabFalloff(
        const DabPoint& dab, float brushX, float brushY, float hardness, float radius) const
    {
        const float invRadius = 1.0f / std::max(radius, 0.0001f);
        float shapeX = brushX * invRadius;
        float shapeY = brushY * invRadius;

        if (m_dabRotation != 0.0f) {
            const float a = m_dabRotation * (3.14159265358979323846f / 180.0f);
            const float ca = std::cos(a);
            const float sa = std::sin(a);
            const float rx = shapeX * ca - shapeY * sa;
            const float ry = shapeX * sa + shapeY * ca;
            shapeX = rx;
            shapeY = ry;
        }

        if (m_dabXScale <= 0.0001f || m_dabYScale <= 0.0001f) {
            return 0.0f;
        }
        shapeX /= m_dabXScale;
        shapeY /= m_dabYScale;

        if (m_dabType > 0 && !m_dabShapeAlpha.empty() && m_dabShapeW > 0 && m_dabShapeH > 0) {
            if (std::abs(shapeX) > 1.0f || std::abs(shapeY) > 1.0f) {
                return 0.0f;
            }

            const float u = (shapeX + 1.0f) * 0.5f;
            const float v = (shapeY + 1.0f) * 0.5f;
            const int maskX
                = std::clamp(static_cast<int>(u * (m_dabShapeW - 1)), 0, m_dabShapeW - 1);
            const int maskY
                = std::clamp(static_cast<int>(v * (m_dabShapeH - 1)), 0, m_dabShapeH - 1);
            const size_t maskIndex = static_cast<size_t>(maskY * m_dabShapeW + maskX);

            if (m_dabInterpolation == 1) {
                const float alpha = static_cast<float>(m_dabShapeAlpha[maskIndex]) / 255.0f;
                const float softAlpha = static_cast<float>(m_dabShapeSoftAlpha[maskIndex]) / 255.0f;
                return dab_shape_falloff::softenAlpha(alpha, softAlpha, hardness);
            }
            return sampleDabShapeSoftAlpha(u, v, hardness);
        }

        const float t = std::sqrt(shapeX * shapeX + shapeY * shapeY);
        if (t > 1.0f) {
            return 0.0f;
        }
        return hardnessFalloffFromEdgeDistance(std::max(0.0f, 1.0f - t), hardness);
    }

    uint8_t textureAlphaFactorAt(const TileKey& key, uint32_t localX, uint32_t localY) const
    {
        if (!usesProceduralTexture()) {
            return 255;
        }
        if (hasDynamicTextureBinding()) {
            const float worldX = static_cast<float>(key.x) * static_cast<float>(TILE_SIZE)
                + static_cast<float>(localX) + 0.5f;
            const float worldY = static_cast<float>(key.y) * static_cast<float>(TILE_SIZE)
                + static_cast<float>(localY) + 0.5f;
            float g = proceduralGrainByType(worldX, worldY);
            const float contrastStrength = 0.5f + textureContrast() * 2.5f;
            g = std::clamp(0.5f + (g - 0.5f) * contrastStrength, 0.0f, 1.0f);
            const float depthMix = 1.0f - textureDepth() * (1.0f - g);
            const float blendMix
                = (1.0f - textureBlend()) * depthMix + textureBlend() * (depthMix * depthMix);
            const float factor = (1.0f - textureAmount()) + textureAmount() * blendMix;
            return static_cast<uint8_t>(std::clamp(factor * 255.0f, 0.0f, 255.0f));
        }
        const std::vector<uint8_t>& alpha = proceduralTextureTileAlpha(key);
        return alpha[localY * TILE_SIZE + localX];
    }

    bool dabUsesProceduralTexture(const DabPoint& dab) const
    {
        return dab.textureAmount > 0.0001f && dab.textureDepth > 0.0001f;
    }

    uint8_t textureAlphaFactorAt(
        const DabPoint& dab, const TileKey& key, uint32_t localX, uint32_t localY) const
    {
        if (!dabUsesProceduralTexture(dab)) {
            return 255;
        }

        const float worldX = static_cast<float>(key.x) * static_cast<float>(TILE_SIZE)
            + static_cast<float>(localX) + 0.5f;
        const float worldY = static_cast<float>(key.y) * static_cast<float>(TILE_SIZE)
            + static_cast<float>(localY) + 0.5f;
        float g = proceduralGrainByType(worldX, worldY, dab.textureScale);
        const float contrastStrength = 0.5f + dab.textureContrast * 2.5f;
        g = std::clamp(0.5f + (g - 0.5f) * contrastStrength, 0.0f, 1.0f);
        const float depthMix = 1.0f - dab.textureDepth * (1.0f - g);
        const float blendMix
            = (1.0f - dab.textureBlend) * depthMix + dab.textureBlend * (depthMix * depthMix);
        const float factor = (1.0f - dab.textureAmount) + dab.textureAmount * blendMix;
        return static_cast<uint8_t>(std::clamp(factor * 255.0f, 0.0f, 255.0f));
    }

    static void blendMax(
        uint8_t* pixels, uint32_t idx, const DabPoint& dab, uint8_t alpha, float colorScale)
    {
        uint8_t existingA = pixels[idx + 3];
        if (alpha <= existingA)
            return;
        float sa = static_cast<float>(alpha) / 255.0f;
        pixels[idx + 0]
            = static_cast<uint8_t>(std::clamp(dab.colorR * sa * colorScale, 0.0f, 255.0f));
        pixels[idx + 1]
            = static_cast<uint8_t>(std::clamp(dab.colorG * sa * colorScale, 0.0f, 255.0f));
        pixels[idx + 2]
            = static_cast<uint8_t>(std::clamp(dab.colorB * sa * colorScale, 0.0f, 255.0f));
        pixels[idx + 3] = alpha;
    }

    static void blendSrcOver(
        uint8_t* pixels, uint32_t idx, const DabPoint& dab, uint8_t alpha, float colorScale)
    {
        float sa = static_cast<float>(alpha) / 255.0f;
        float inv = 1.0f - sa;
        const float srcR = dab.colorR * sa * colorScale;
        const float srcG = dab.colorG * sa * colorScale;
        const float srcB = dab.colorB * sa * colorScale;
        pixels[idx + 0] = static_cast<uint8_t>(std::min(255.0f, srcR + pixels[idx + 0] * inv));
        pixels[idx + 1] = static_cast<uint8_t>(std::min(255.0f, srcG + pixels[idx + 1] * inv));
        pixels[idx + 2] = static_cast<uint8_t>(std::min(255.0f, srcB + pixels[idx + 2] * inv));
        pixels[idx + 3] = static_cast<uint8_t>(
            std::min(255.0f, static_cast<float>(alpha) + pixels[idx + 3] * inv));
    }

    void collectDabCoveredTiles(const DabPoint& dab,
        std::unordered_set<TileKey, TileKeyHash>& outTiles, bool includeBaseExtent) const
    {
        const float radius = includeBaseExtent ? std::max(dab.radius, dab.baseRadius) : dab.radius;
        if (radius <= 0.0f)
            return;
        if (dab.alpha == 0 && (!includeBaseExtent || dab.baseAlpha == 0))
            return;

        const float rasterExtent
            = dabCoverageExtent(radius, dab.hardness, dab.roundness, dab.angleDegrees, true);
        const int32_t tMinX
            = static_cast<int32_t>(std::floor((dab.worldX - rasterExtent) / TILE_SIZE));
        const int32_t tMinY
            = static_cast<int32_t>(std::floor((dab.worldY - rasterExtent) / TILE_SIZE));
        const int32_t tMaxX
            = static_cast<int32_t>(std::floor((dab.worldX + rasterExtent) / TILE_SIZE));
        const int32_t tMaxY
            = static_cast<int32_t>(std::floor((dab.worldY + rasterExtent) / TILE_SIZE));

        for (int32_t ty = tMinY; ty <= tMaxY; ++ty) {
            for (int32_t tx = tMinX; tx <= tMaxX; ++tx) {
                outTiles.insert(TileKey { tx, ty });
            }
        }
    }

    bool dabIntersectsTileSet(const DabPoint& dab,
        const std::unordered_set<TileKey, TileKeyHash>& tiles, bool includeBaseExtent) const
    {
        if (tiles.empty())
            return false;
        const float radius = includeBaseExtent ? std::max(dab.radius, dab.baseRadius) : dab.radius;
        if (radius <= 0.0f)
            return false;
        if (dab.alpha == 0 && (!includeBaseExtent || dab.baseAlpha == 0))
            return false;

        const float rasterExtent
            = dabCoverageExtent(radius, dab.hardness, dab.roundness, dab.angleDegrees, true);
        const int32_t tMinX
            = static_cast<int32_t>(std::floor((dab.worldX - rasterExtent) / TILE_SIZE));
        const int32_t tMinY
            = static_cast<int32_t>(std::floor((dab.worldY - rasterExtent) / TILE_SIZE));
        const int32_t tMaxX
            = static_cast<int32_t>(std::floor((dab.worldX + rasterExtent) / TILE_SIZE));
        const int32_t tMaxY
            = static_cast<int32_t>(std::floor((dab.worldY + rasterExtent) / TILE_SIZE));

        for (int32_t ty = tMinY; ty <= tMaxY; ++ty) {
            for (int32_t tx = tMinX; tx <= tMaxX; ++tx) {
                if (tiles.find(TileKey { tx, ty }) != tiles.end()) {
                    return true;
                }
            }
        }
        return false;
    }

    void rasterizeDab(TileGrid& target, const DabPoint& dab, const TileGrid* selectionMask,
        bool maxBlend, const std::unordered_set<TileKey, TileKeyHash>* allowedTiles = nullptr)
    {
        if (dab.radius <= 0.0f)
            return;
        const float r = dab.radius;
        const float hardness = std::clamp(dab.hardness, 0.0f, 1.0f);
        const float roundness = std::max(0.01f, std::clamp(dab.roundness, 0.0f, 1.0f));
        const float angleRadians = dab.angleDegrees * (3.14159265358979323846f / 180.0f);
        const float cosA = std::cos(angleRadians);
        const float sinA = std::sin(angleRadians);
        const bool useMask = (selectionMask != nullptr);
        const bool roundLowAlpha
            = m_flowBlendMode == ruwa::core::brushes::BrushSettingsData::FlowBlendSrcOver;
        const float rasterExtent
            = dabCoverageExtent(r, hardness, roundness, dab.angleDegrees, true);

        int32_t tMinX = static_cast<int32_t>(std::floor((dab.worldX - rasterExtent) / TILE_SIZE));
        int32_t tMinY = static_cast<int32_t>(std::floor((dab.worldY - rasterExtent) / TILE_SIZE));
        int32_t tMaxX = static_cast<int32_t>(std::floor((dab.worldX + rasterExtent) / TILE_SIZE));
        int32_t tMaxY = static_cast<int32_t>(std::floor((dab.worldY + rasterExtent) / TILE_SIZE));

        for (int32_t ty = tMinY; ty <= tMaxY; ++ty) {
            for (int32_t tx = tMinX; tx <= tMaxX; ++tx) {
                TileKey key { tx, ty };
                if (allowedTiles && allowedTiles->find(key) == allowedTiles->end()) {
                    continue;
                }
                float tileOriginX = tx * static_cast<float>(TILE_SIZE);
                float tileOriginY = ty * static_cast<float>(TILE_SIZE);

                int32_t localMinX = std::max(
                    0, static_cast<int32_t>(std::floor(dab.worldX - rasterExtent - tileOriginX)));
                int32_t localMinY = std::max(
                    0, static_cast<int32_t>(std::floor(dab.worldY - rasterExtent - tileOriginY)));
                int32_t localMaxX = std::min(static_cast<int32_t>(TILE_SIZE) - 1,
                    static_cast<int32_t>(std::ceil(dab.worldX + rasterExtent - tileOriginX)));
                int32_t localMaxY = std::min(static_cast<int32_t>(TILE_SIZE) - 1,
                    static_cast<int32_t>(std::ceil(dab.worldY + rasterExtent - tileOriginY)));

                bool modified = false;
                TileData* tilePtr = nullptr;

                for (int32_t ly = localMinY; ly <= localMaxY; ++ly) {
                    for (int32_t lx = localMinX; lx <= localMaxX; ++lx) {
                        const float sampleX = tileOriginX + static_cast<float>(lx) + 0.5f;
                        const float sampleY = tileOriginY + static_cast<float>(ly) + 0.5f;
                        const float dx = sampleX - dab.worldX;
                        const float dy = sampleY - dab.worldY;
                        const float brushX = dx * cosA + dy * sinA;
                        const float brushY = (-dx * sinA + dy * cosA) / roundness;
                        const float coverage = sampleDabFalloff(dab, brushX, brushY, hardness, r);
                        if (coverage <= 0.0001f)
                            continue;

                        const float coveredAlpha
                            = std::clamp(static_cast<float>(dab.alpha) * coverage, 0.0f, 255.0f);
                        uint8_t alpha = roundLowAlpha
                            ? static_cast<uint8_t>(std::lround(coveredAlpha))
                            : static_cast<uint8_t>(coveredAlpha);
                        float colorScale = 1.0f;
                        if (useMask) {
                            int32_t worldPx = tx * static_cast<int32_t>(TILE_SIZE) + lx;
                            int32_t worldPy = ty * static_cast<int32_t>(TILE_SIZE) + ly;
                            uint8_t maskA = maskAlphaAt(selectionMask, worldPx, worldPy);
                            if (maskA == 0)
                                continue;
                            const float maskScale = static_cast<float>(maskA) / 255.0f;
                            if (!m_selectionMaskAffectsAlpha) {
                                colorScale = maskScale;
                            }
                        }
                        if (alpha == 0)
                            continue;

                        if (dabUsesProceduralTexture(dab)) {
                            const float edgeBoost = dab.textureEdgeBoost;
                            float textureA
                                = static_cast<float>(textureAlphaFactorAt(dab, key,
                                      static_cast<uint32_t>(lx), static_cast<uint32_t>(ly)))
                                / 255.0f;
                            if (edgeBoost > 0.0001f) {
                                const float edge = smoothstep01(1.0f - coverage);
                                const float contrast = 1.0f + edge * edgeBoost * 8.0f;
                                textureA
                                    = std::clamp(0.5f + (textureA - 0.5f) * contrast, 0.0f, 1.0f);
                            }
                            const float texturedAlpha
                                = std::clamp(static_cast<float>(alpha) * textureA, 0.0f, 255.0f);
                            alpha = roundLowAlpha ? static_cast<uint8_t>(std::lround(texturedAlpha))
                                                  : static_cast<uint8_t>(texturedAlpha);
                            if (alpha == 0)
                                continue;
                        }

                        if (!tilePtr) {
                            tilePtr = &target.getOrCreateTile(key);
                        }
                        uint8_t* pixels = tilePtr->pixels();
                        uint32_t idx
                            = (static_cast<uint32_t>(ly) * TILE_SIZE + static_cast<uint32_t>(lx))
                            * TILE_CHANNELS;

                        if (maxBlend) {
                            uint8_t prevA = pixels[idx + 3];
                            blendMax(pixels, idx, dab, alpha, colorScale);
                            if (pixels[idx + 3] != prevA)
                                modified = true;
                        } else {
                            blendSrcOver(pixels, idx, dab, alpha, colorScale);
                            modified = true;
                        }
                    }
                }

                if (modified && tilePtr) {
                    tilePtr->markDirty();
                    target.markDirty(key);
                }
            }
        }
    }

    using Float3 = std::array<float, 3>;

    static float clampUnit(float value) { return std::clamp(value, 0.0f, 1.0f); }

    static float blendLuminance(const Float3& color)
    {
        return color[0] * 0.299f + color[1] * 0.587f + color[2] * 0.114f;
    }

    static float blendSaturation(const Float3& color)
    {
        return std::max({ color[0], color[1], color[2] })
            - std::min({ color[0], color[1], color[2] });
    }

    static Float3 clipBlendColor(Float3 color)
    {
        const float lum = blendLuminance(color);
        const float minComponent = std::min({ color[0], color[1], color[2] });
        const float maxComponent = std::max({ color[0], color[1], color[2] });

        if (minComponent < 0.0f) {
            const float scale = lum / std::max(lum - minComponent, 0.00001f);
            for (float& channel : color) {
                channel = lum + (channel - lum) * scale;
            }
        }
        if (maxComponent > 1.0f) {
            const float scale = (1.0f - lum) / std::max(maxComponent - lum, 0.00001f);
            for (float& channel : color) {
                channel = lum + (channel - lum) * scale;
            }
        }

        for (float& channel : color) {
            channel = clampUnit(channel);
        }
        return color;
    }

    static Float3 setBlendLuminance(Float3 color, float luminance)
    {
        const float delta = luminance - blendLuminance(color);
        for (float& channel : color) {
            channel += delta;
        }
        return clipBlendColor(color);
    }

    static Float3 setBlendSaturation(const Float3& color, float saturation)
    {
        const int minIndex
            = static_cast<int>(std::min_element(color.begin(), color.end()) - color.begin());
        const int maxIndex
            = static_cast<int>(std::max_element(color.begin(), color.end()) - color.begin());

        if (color[maxIndex] <= color[minIndex]) {
            return { 0.0f, 0.0f, 0.0f };
        }

        const int midIndex = 3 - minIndex - maxIndex;
        Float3 result { 0.0f, 0.0f, 0.0f };
        result[maxIndex] = saturation;
        result[midIndex] = ((color[midIndex] - color[minIndex]) * saturation)
            / (color[maxIndex] - color[minIndex]);
        return { clampUnit(result[0]), clampUnit(result[1]), clampUnit(result[2]) };
    }

    static float blendColorDodgeComponent(float base, float src)
    {
        return (src >= 1.0f) ? 1.0f : std::min(1.0f, base / std::max(1.0f - src, 0.00001f));
    }

    static float blendColorBurnComponent(float base, float src)
    {
        return (src <= 0.0f) ? 0.0f
                             : std::max(0.0f, 1.0f - (1.0f - base) / std::max(src, 0.00001f));
    }

    static float blendVividLightComponent(float base, float src)
    {
        return (src < 0.5f) ? blendColorBurnComponent(base, clampUnit(2.0f * src))
                            : blendColorDodgeComponent(base, clampUnit(2.0f * (src - 0.5f)));
    }

    static uint32_t hashBlendPixel(int32_t x, int32_t y)
    {
        uint32_t hash = static_cast<uint32_t>(x) * 1597334677u
            + static_cast<uint32_t>(y) * 3812015801u + 2246822519u;
        hash ^= hash >> 16;
        hash *= 2246822519u;
        hash ^= hash >> 13;
        hash *= 3266489917u;
        hash ^= hash >> 16;
        return hash;
    }

    static float dissolveBlendAlpha(float combinedAlpha, int32_t worldX, int32_t worldY)
    {
        if (combinedAlpha <= 0.0f) {
            return 0.0f;
        }
        if (combinedAlpha >= 1.0f) {
            return 1.0f;
        }

        const float randomValue
            = static_cast<float>(hashBlendPixel(worldX, worldY) & 0x00ffffffu) / 16777215.0f;
        return (randomValue <= combinedAlpha) ? 1.0f : 0.0f;
    }

    static Float3 blendModeColor(
        const Float3& base, const Float3& src, ruwa::core::layers::BlendMode mode)
    {
        auto overlayComponent = [](float cb, float cs) {
            return (cb < 0.5f) ? (2.0f * cb * cs) : (1.0f - 2.0f * (1.0f - cb) * (1.0f - cs));
        };
        auto softLightComponent = [](float cb, float cs) {
            const float d = (cb <= 0.25f) ? ((16.0f * cb - 12.0f) * cb + 4.0f) * cb : std::sqrt(cb);
            return (cs <= 0.5f) ? cb - (1.0f - 2.0f * cs) * cb * (1.0f - cb)
                                : cb + (2.0f * cs - 1.0f) * (d - cb);
        };

        using ruwa::core::layers::BlendMode;
        switch (mode) {
        case BlendMode::Normal:
        case BlendMode::Dissolve:
            return src;
        case BlendMode::Multiply:
            return { base[0] * src[0], base[1] * src[1], base[2] * src[2] };
        case BlendMode::Screen:
            return { base[0] + src[0] - base[0] * src[0], base[1] + src[1] - base[1] * src[1],
                base[2] + src[2] - base[2] * src[2] };
        case BlendMode::Overlay:
            return { overlayComponent(base[0], src[0]), overlayComponent(base[1], src[1]),
                overlayComponent(base[2], src[2]) };
        case BlendMode::SoftLight:
            return { softLightComponent(base[0], src[0]), softLightComponent(base[1], src[1]),
                softLightComponent(base[2], src[2]) };
        case BlendMode::HardLight:
            return { overlayComponent(src[0], base[0]), overlayComponent(src[1], base[1]),
                overlayComponent(src[2], base[2]) };
        case BlendMode::ColorDodge:
            return { blendColorDodgeComponent(base[0], src[0]),
                blendColorDodgeComponent(base[1], src[1]),
                blendColorDodgeComponent(base[2], src[2]) };
        case BlendMode::ColorBurn:
            return { blendColorBurnComponent(base[0], src[0]),
                blendColorBurnComponent(base[1], src[1]),
                blendColorBurnComponent(base[2], src[2]) };
        case BlendMode::Darken:
            return { std::min(base[0], src[0]), std::min(base[1], src[1]),
                std::min(base[2], src[2]) };
        case BlendMode::Lighten:
            return { std::max(base[0], src[0]), std::max(base[1], src[1]),
                std::max(base[2], src[2]) };
        case BlendMode::Difference:
            return { std::fabs(base[0] - src[0]), std::fabs(base[1] - src[1]),
                std::fabs(base[2] - src[2]) };
        case BlendMode::Exclusion:
            return { base[0] + src[0] - 2.0f * base[0] * src[0],
                base[1] + src[1] - 2.0f * base[1] * src[1],
                base[2] + src[2] - 2.0f * base[2] * src[2] };
        case BlendMode::LinearBurn:
            return { std::max(0.0f, base[0] + src[0] - 1.0f),
                std::max(0.0f, base[1] + src[1] - 1.0f), std::max(0.0f, base[2] + src[2] - 1.0f) };
        case BlendMode::DarkerColor:
            return (blendLuminance(src) <= blendLuminance(base)) ? src : base;
        case BlendMode::LinearDodge:
            return { std::min(1.0f, base[0] + src[0]), std::min(1.0f, base[1] + src[1]),
                std::min(1.0f, base[2] + src[2]) };
        case BlendMode::LighterColor:
            return (blendLuminance(src) >= blendLuminance(base)) ? src : base;
        case BlendMode::VividLight:
            return { blendVividLightComponent(base[0], src[0]),
                blendVividLightComponent(base[1], src[1]),
                blendVividLightComponent(base[2], src[2]) };
        case BlendMode::LinearLight:
            return { clampUnit(base[0] + 2.0f * src[0] - 1.0f),
                clampUnit(base[1] + 2.0f * src[1] - 1.0f),
                clampUnit(base[2] + 2.0f * src[2] - 1.0f) };
        case BlendMode::PinLight:
            return { (src[0] < 0.5f) ? std::min(base[0], 2.0f * src[0])
                                     : std::max(base[0], 2.0f * (src[0] - 0.5f)),
                (src[1] < 0.5f) ? std::min(base[1], 2.0f * src[1])
                                : std::max(base[1], 2.0f * (src[1] - 0.5f)),
                (src[2] < 0.5f) ? std::min(base[2], 2.0f * src[2])
                                : std::max(base[2], 2.0f * (src[2] - 0.5f)) };
        case BlendMode::HardMix: {
            const Float3 vivid = blendModeColor(base, src, BlendMode::VividLight);
            return { vivid[0] >= 0.5f ? 1.0f : 0.0f, vivid[1] >= 0.5f ? 1.0f : 0.0f,
                vivid[2] >= 0.5f ? 1.0f : 0.0f };
        }
        case BlendMode::Subtract:
            return { std::max(0.0f, base[0] - src[0]), std::max(0.0f, base[1] - src[1]),
                std::max(0.0f, base[2] - src[2]) };
        case BlendMode::Divide:
            return { (src[0] <= 0.0f) ? 1.0f : std::min(1.0f, base[0] / std::max(src[0], 0.001f)),
                (src[1] <= 0.0f) ? 1.0f : std::min(1.0f, base[1] / std::max(src[1], 0.001f)),
                (src[2] <= 0.0f) ? 1.0f : std::min(1.0f, base[2] / std::max(src[2], 0.001f)) };
        case BlendMode::Hue:
            return blendSaturation(src) <= 0.00001f
                ? base
                : setBlendLuminance(
                      setBlendSaturation(src, blendSaturation(base)), blendLuminance(base));
        case BlendMode::Saturation:
            return setBlendLuminance(
                setBlendSaturation(base, blendSaturation(src)), blendLuminance(base));
        case BlendMode::Color:
            return setBlendLuminance(src, blendLuminance(base));
        case BlendMode::Luminosity:
            return setBlendLuminance(base, blendLuminance(src));
        }

        return src;
    }

    static uint8_t maskAlphaAt(const TileGrid* grid, int32_t x, int32_t y)
    {
        if (!grid)
            return 255;
        if (x < 0 || y < 0)
            return 0;

        int32_t tx = x / static_cast<int32_t>(TILE_SIZE);
        int32_t ty = y / static_cast<int32_t>(TILE_SIZE);
        uint32_t localX = static_cast<uint32_t>(x % static_cast<int32_t>(TILE_SIZE));
        uint32_t localY = static_cast<uint32_t>(y % static_cast<int32_t>(TILE_SIZE));
        const TileData* tile = grid->getTile(TileKey { tx, ty });
        if (!tile)
            return 0;

        uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
        return tile->pixels()[idx + 3];
    }

    static float sourceMaskScale(const TileGrid* mask, const TileKey& tileKey, uint32_t pixelIndex)
    {
        if (!mask)
            return 1.0f;
        const int32_t localX = static_cast<int32_t>(pixelIndex % TILE_SIZE);
        const int32_t localY = static_cast<int32_t>(pixelIndex / TILE_SIZE);
        const int32_t worldX = tileKey.x * static_cast<int32_t>(TILE_SIZE) + localX;
        const int32_t worldY = tileKey.y * static_cast<int32_t>(TILE_SIZE) + localY;
        return static_cast<float>(maskAlphaAt(mask, worldX, worldY)) / 255.0f;
    }

    /// Src-over blend strokeTile onto layerTile (both premultiplied RGBA).
    /// When `useAlphaCap` is true, the result alpha is clamped per-pixel to the
    /// selection mask alpha (cap), and pixels whose dst.a is already above the
    /// cap are preserved verbatim. RGB scales proportionally on clamp.
    static void flattenTile(const TileData& src, TileData& dst, float opacity,
        const TileKey& tileKey, const TileGrid* finalSourceMask, bool useAlphaCap = false)
    {
        const uint8_t* sp = src.pixels();
        uint8_t* dp = dst.pixels();
        constexpr uint32_t pixelCount = TILE_SIZE * TILE_SIZE;
        const float op = std::clamp(opacity, 0.0f, 1.0f);
        if (op <= 0.0f)
            return;

        for (uint32_t i = 0; i < pixelCount; ++i) {
            uint32_t idx = i * TILE_CHANNELS;
            const float maskScale = sourceMaskScale(finalSourceMask, tileKey, i);
            if (maskScale <= 0.0f)
                continue;

            if (useAlphaCap) {
                const float capF = std::clamp(maskScale, 0.0f, 1.0f);
                const float dstAF = static_cast<float>(dp[idx + 3]) / 255.0f;
                if (dstAF > capF) {
                    // Pre-existing alpha already above the soft-mask cap; preserve.
                    continue;
                }
                const float srcAF = (static_cast<float>(sp[idx + 3]) / 255.0f) * op * maskScale;
                if (srcAF <= 0.0f)
                    continue;
                const float inv = 1.0f - srcAF;
                const float srcR = static_cast<float>(sp[idx + 0]) * op * maskScale;
                const float srcG = static_cast<float>(sp[idx + 1]) * op * maskScale;
                const float srcB = static_cast<float>(sp[idx + 2]) * op * maskScale;
                float coR = srcR + static_cast<float>(dp[idx + 0]) * inv;
                float coG = srcG + static_cast<float>(dp[idx + 1]) * inv;
                float coB = srcB + static_cast<float>(dp[idx + 2]) * inv;
                float aoF = srcAF + dstAF * inv;
                if (aoF > capF) {
                    // Clamp result alpha to cap; scale RGB to keep visible color stable.
                    const float scale = (aoF > 0.0f) ? (capF / aoF) : 0.0f;
                    coR *= scale;
                    coG *= scale;
                    coB *= scale;
                    aoF = capF;
                }
                const float capPx = capF * 255.0f;
                dp[idx + 0] = static_cast<uint8_t>(std::clamp(coR, 0.0f, capPx));
                dp[idx + 1] = static_cast<uint8_t>(std::clamp(coG, 0.0f, capPx));
                dp[idx + 2] = static_cast<uint8_t>(std::clamp(coB, 0.0f, capPx));
                dp[idx + 3] = static_cast<uint8_t>(std::clamp(aoF * 255.0f, 0.0f, 255.0f));
                continue;
            }

            const float srcAF = (static_cast<float>(sp[idx + 3]) / 255.0f) * op * maskScale;
            uint8_t sa = static_cast<uint8_t>(std::clamp(srcAF * 255.0f, 0.0f, 255.0f));
            if (sa == 0)
                continue;

            float saF = static_cast<float>(sa) / 255.0f;
            float inv = 1.0f - saF;
            const float srcR = static_cast<float>(sp[idx + 0]) * op * maskScale;
            const float srcG = static_cast<float>(sp[idx + 1]) * op * maskScale;
            const float srcB = static_cast<float>(sp[idx + 2]) * op * maskScale;

            dp[idx + 0] = static_cast<uint8_t>(std::min(255.0f, srcR + dp[idx + 0] * inv));
            dp[idx + 1] = static_cast<uint8_t>(std::min(255.0f, srcG + dp[idx + 1] * inv));
            dp[idx + 2] = static_cast<uint8_t>(std::min(255.0f, srcB + dp[idx + 2] * inv));
            dp[idx + 3] = static_cast<uint8_t>(
                std::min(255.0f, static_cast<float>(sa) + dp[idx + 3] * inv));
        }
    }

    /// Src-over color blend while preserving destination alpha (alpha lock).
    /// Source RGB is expected to already include any lock factor.
    static void flattenTileAlphaLocked(const TileData& src, TileData& dst, float opacity)
    {
        const uint8_t* sp = src.pixels();
        uint8_t* dp = dst.pixels();
        constexpr uint32_t pixelCount = TILE_SIZE * TILE_SIZE;
        const float op = std::clamp(opacity, 0.0f, 1.0f);
        if (op <= 0.0f)
            return;

        for (uint32_t i = 0; i < pixelCount; ++i) {
            uint32_t idx = i * TILE_CHANNELS;
            const float srcAF = (static_cast<float>(sp[idx + 3]) / 255.0f) * op;
            uint8_t sa = static_cast<uint8_t>(std::clamp(srcAF * 255.0f, 0.0f, 255.0f));
            if (sa == 0)
                continue;

            float saF = static_cast<float>(sa) / 255.0f;
            float inv = 1.0f - saF;
            const float srcR = static_cast<float>(sp[idx + 0]) * op;
            const float srcG = static_cast<float>(sp[idx + 1]) * op;
            const float srcB = static_cast<float>(sp[idx + 2]) * op;

            dp[idx + 0] = static_cast<uint8_t>(std::min(255.0f, srcR + dp[idx + 0] * inv));
            dp[idx + 1] = static_cast<uint8_t>(std::min(255.0f, srcG + dp[idx + 1] * inv));
            dp[idx + 2] = static_cast<uint8_t>(std::min(255.0f, srcB + dp[idx + 2] * inv));
            // Keep destination alpha unchanged.
        }
    }

    static void flattenTileWithBlendMode(const TileData& src, const TileData* blendBase,
        TileData& dst, float opacity, ruwa::core::layers::BlendMode mode, bool alphaLock,
        const TileKey& tileKey, const Color& backdropColor = Color::transparent(),
        const TileGrid* finalSourceMask = nullptr)
    {
        const uint8_t* sp = src.pixels();
        uint8_t* dp = dst.pixels();
        constexpr uint32_t pixelCount = TILE_SIZE * TILE_SIZE;
        const float op = std::clamp(opacity, 0.0f, 1.0f);
        if (op <= 0.0f)
            return;

        for (uint32_t i = 0; i < pixelCount; ++i) {
            const uint32_t idx = i * TILE_CHANNELS;
            const float asRaw = static_cast<float>(sp[idx + 3]) / 255.0f;
            if (asRaw <= 0.0f)
                continue;

            const float maskScale = sourceMaskScale(finalSourceMask, tileKey, i);
            if (maskScale <= 0.0f)
                continue;
            float as = asRaw * op * maskScale;
            if (mode == ruwa::core::layers::BlendMode::Dissolve) {
                const int32_t localX = static_cast<int32_t>(i % TILE_SIZE);
                const int32_t localY = static_cast<int32_t>(i / TILE_SIZE);
                const int32_t worldX = tileKey.x * static_cast<int32_t>(TILE_SIZE) + localX;
                const int32_t worldY = tileKey.y * static_cast<int32_t>(TILE_SIZE) + localY;
                as = dissolveBlendAlpha(as, worldX, worldY);
            }
            if (as <= 0.0f)
                continue;

            const uint8_t* bp = blendBase ? blendBase->pixels() : dp;
            const float ab = static_cast<float>(bp[idx + 3]) / 255.0f;
            const float backdropAlpha = std::clamp(backdropColor.a, 0.0f, 1.0f);
            const float dstAlpha = static_cast<float>(dp[idx + 3]) / 255.0f;
            const Float3 srcColor { static_cast<float>(sp[idx + 0]) / 255.0f / asRaw,
                static_cast<float>(sp[idx + 1]) / 255.0f / asRaw,
                static_cast<float>(sp[idx + 2]) / 255.0f / asRaw };
            const Float3 basePremul = (ab > 0.0f)
                ? Float3 { static_cast<float>(bp[idx + 0]) / 255.0f,
                      static_cast<float>(bp[idx + 1]) / 255.0f,
                      static_cast<float>(bp[idx + 2]) / 255.0f }
                : Float3 { 0.0f, 0.0f, 0.0f };
            const Float3 backdropPremul { std::clamp(backdropColor.r, 0.0f, 1.0f) * backdropAlpha,
                std::clamp(backdropColor.g, 0.0f, 1.0f) * backdropAlpha,
                std::clamp(backdropColor.b, 0.0f, 1.0f) * backdropAlpha };
            const float visibleBaseAlpha = ab + backdropAlpha * (1.0f - ab);
            const Float3 visibleBasePremul { basePremul[0] + backdropPremul[0] * (1.0f - ab),
                basePremul[1] + backdropPremul[1] * (1.0f - ab),
                basePremul[2] + backdropPremul[2] * (1.0f - ab) };
            const Float3 baseColor = (visibleBaseAlpha > 0.0f)
                ? Float3 { visibleBasePremul[0] / visibleBaseAlpha,
                      visibleBasePremul[1] / visibleBaseAlpha,
                      visibleBasePremul[2] / visibleBaseAlpha }
                : Float3 { 0.0f, 0.0f, 0.0f };
            const Float3 dstColor = (dstAlpha > 0.0f)
                ? Float3 { static_cast<float>(dp[idx + 0]) / 255.0f / dstAlpha,
                      static_cast<float>(dp[idx + 1]) / 255.0f / dstAlpha,
                      static_cast<float>(dp[idx + 2]) / 255.0f / dstAlpha }
                : Float3 { 0.0f, 0.0f, 0.0f };
            const Float3 blended = blendModeColor(baseColor, srcColor, mode);
            const Float3 blendedOverBackdrop { (1.0f - visibleBaseAlpha) * srcColor[0]
                    + visibleBaseAlpha * blended[0],
                (1.0f - visibleBaseAlpha) * srcColor[1] + visibleBaseAlpha * blended[1],
                (1.0f - visibleBaseAlpha) * srcColor[2] + visibleBaseAlpha * blended[2] };

            if (alphaLock) {
                const Float3 outColor { dstAlpha
                        * (as * blendedOverBackdrop[0] + (1.0f - as) * dstColor[0]),
                    dstAlpha * (as * blendedOverBackdrop[1] + (1.0f - as) * dstColor[1]),
                    dstAlpha * (as * blendedOverBackdrop[2] + (1.0f - as) * dstColor[2]) };
                dp[idx + 0] = static_cast<uint8_t>(
                    std::lround(std::clamp(outColor[0], 0.0f, dstAlpha) * 255.0f));
                dp[idx + 1] = static_cast<uint8_t>(
                    std::lround(std::clamp(outColor[1], 0.0f, dstAlpha) * 255.0f));
                dp[idx + 2] = static_cast<uint8_t>(
                    std::lround(std::clamp(outColor[2], 0.0f, dstAlpha) * 255.0f));
                continue;
            }

            const float ao = as + dstAlpha * (1.0f - as);
            const Float3 outColor { as * blendedOverBackdrop[0]
                    + (1.0f - as) * static_cast<float>(dp[idx + 0]) / 255.0f,
                as * blendedOverBackdrop[1]
                    + (1.0f - as) * static_cast<float>(dp[idx + 1]) / 255.0f,
                as * blendedOverBackdrop[2]
                    + (1.0f - as) * static_cast<float>(dp[idx + 2]) / 255.0f };

            dp[idx + 0]
                = static_cast<uint8_t>(std::lround(std::clamp(outColor[0], 0.0f, ao) * 255.0f));
            dp[idx + 1]
                = static_cast<uint8_t>(std::lround(std::clamp(outColor[1], 0.0f, ao) * 255.0f));
            dp[idx + 2]
                = static_cast<uint8_t>(std::lround(std::clamp(outColor[2], 0.0f, ao) * 255.0f));
            dp[idx + 3] = static_cast<uint8_t>(std::lround(std::clamp(ao, 0.0f, 1.0f) * 255.0f));
        }
    }

    /// Erase blend: subtract stroke alpha from layer (destination-out)
    /// Both src and dst are premultiplied RGBA.
    static void eraseFlattenTile(const TileData& src, TileData& dst, float opacity,
        const TileKey& tileKey, const TileGrid* finalSourceMask)
    {
        const uint8_t* sp = src.pixels();
        uint8_t* dp = dst.pixels();
        constexpr uint32_t pixelCount = TILE_SIZE * TILE_SIZE;
        const float op = std::clamp(opacity, 0.0f, 1.0f);
        if (op <= 0.0f)
            return;

        for (uint32_t i = 0; i < pixelCount; ++i) {
            uint32_t idx = i * TILE_CHANNELS;
            const float maskScale = sourceMaskScale(finalSourceMask, tileKey, i);
            if (maskScale <= 0.0f)
                continue;
            const float srcAF = (static_cast<float>(sp[idx + 3]) / 255.0f) * op * maskScale;
            uint8_t sa = static_cast<uint8_t>(std::clamp(srcAF * 255.0f, 0.0f, 255.0f));
            if (sa == 0)
                continue;

            float keep = 1.0f - static_cast<float>(sa) / 255.0f;

            dp[idx + 0] = static_cast<uint8_t>(dp[idx + 0] * keep);
            dp[idx + 1] = static_cast<uint8_t>(dp[idx + 1] * keep);
            dp[idx + 2] = static_cast<uint8_t>(dp[idx + 2] * keep);
            dp[idx + 3] = static_cast<uint8_t>(dp[idx + 3] * keep);
        }
    }

    uint8_t m_r = 255, m_g = 255, m_b = 255, m_a = 255;
    float m_radius = 8.0f;
    float m_hardness = 0.7f;
    float m_spacing = 0.25f; // fraction of radius between stamps
    float m_flow = 1.0f; // 0..1 alpha multiplier, independent of color alpha
    int m_flowBlendMode = ruwa::core::brushes::BrushSettingsData::FlowBlendMax;
    float m_textureAmount = 0.0f;
    float m_textureScale = 1.0f;
    float m_textureContrast = 0.5f;
    float m_textureDepth = 1.0f;
    float m_textureBlend = 0.5f;
    float m_textureEdgeBoost = 0.0f;
    int m_textureType = 0; // 0=Procedural, 1=Noise, 2=Perlin
    int m_dabType = 0; // 0..5 — dab shape/type (0=circle, 1-5=PNG)
    std::vector<uint8_t> m_dabShapeAlpha; // PNG alpha mask for dabType 1-5
    std::vector<uint8_t> m_dabShapeSoftAlpha; // prefiltered alpha for custom-dab hardness
    int m_dabShapeW = 0;
    int m_dabShapeH = 0;
    QString m_dabCustomImagePath;
    float m_dabXScale = 1.0f;
    float m_dabYScale = 1.0f;
    float m_dabRotation = 0.0f;
    float m_dabThreshold = 0.5f;
    float m_dabCompression = 1.0f;
    int m_dabInterpolation = 0; // 0 = bilinear, 1 = nearest
    uint32_t m_textureRevision = 1;
    float m_scatterPosition = 0.0f;
    bool m_brushFeather = false; // small edge softening (not pixel-perfect)
    float m_roundness = 1.0f; // 0..1, 1 = circle
    float m_angleDegrees = 0.0f;
    bool m_eraseMode = false;
    bool m_blurMode = false;
    bool m_smudgeMode = false;
    bool m_liquifyMode = false;
    int m_liquifyToolMode = 0;
    float m_wetMix = 0.5f;
    float m_colorBlending = 0.0f;
    float m_colorDilution = 0.0f;
    float m_colorSpread = 0.0f;
    float m_colorLength = 0.0f;
    float m_colorWetFlow = 0.75f;
    float m_colorDryRate = 0.0f;
    float m_colorBuildup = 0.0f;
    ruwa::core::layers::BlendMode m_strokeBlendMode = ruwa::core::layers::BlendMode::Normal;
    bool m_selectionMaskAffectsAlpha = false;

    // Pen pressure state (updated per-event by CanvasPanel)
    float m_pressure = 1.0f; // 0..1, 1.0 = full pressure (mouse default)
    float m_strokeElapsedSeconds = 0.0f;
    bool m_strokeTimeAvailable = false;
    bool m_sizePressureEnabled = false;
    bool m_opacityPressureEnabled = true;
    float m_sizePressureMin = 0.0f;
    float m_sizePressureMax = 1.0f;
    float m_opacityPressureMin = 0.0f;
    float m_opacityPressureMax = 1.0f;
    float m_flowPressureMin = 1.0f;
    float m_flowPressureMax = 1.0f;
    float m_startTaper = 0.0f;
    float m_endTaper = 0.0f;
    float m_postCorrection = 0.0f;
    float m_stabilization = 0.0f;
    bool m_adjustCorrectionBySpeed = false;
    bool m_startCorrectionEnabled = false;
    float m_startCorrectionLength = 0.0f;
    bool m_endCorrectionEnabled = false;
    float m_endCorrectionLength = 0.0f;

    // Stroke buffer — accumulates per-stroke paint mask before final flatten.
    TileGrid m_strokeBuffer;
    mutable std::unordered_map<TileKey, ProceduralTextureTile, TileKeyHash>
        m_proceduralTextureTiles;
    std::vector<DabPoint> m_strokeDabs;
    float m_spacingDistanceSinceLastDab = 0.0f;
    bool m_strokeActive = false;
    // Stroke direction is smoothed across segments to avoid per-event
    // angular noise at low pointer speeds (short segments give a noisy
    // atan2(dy,dx)). EMA on the unit direction vector, length-weighted:
    // alpha = clamp(segLen / m_radius, 0, 1). Fast strokes (segLen >= radius)
    // adopt the new direction immediately; slow strokes accumulate smoothly.
    // Smoothed direction vector exposed for cursor preview (unit-length).
    float m_strokeDirCos = 1.0f;
    float m_strokeDirSin = 0.0f;
    // Raw leaky integrator over delta vectors. Longer segments contribute
    // proportionally more, so sub-pixel jitter at stroke start can't lock
    // in a wrong heading.
    float m_strokeDirSumX = 0.0f;
    float m_strokeDirSumY = 0.0f;
    // Total distance traveled since stroke start. Used to defer dab placement
    // through the first few pixels where device jitter dominates over real
    // motion — otherwise an angle-bound brush latches onto that noise and
    // produces a visibly mis-oriented head before settling.
    float m_strokeTravelTotal = 0.0f;
    bool m_strokeDirInitialized = false;
    ruwa::core::brushes::BrushSettingsData m_brushSettingsModel {};
    bool m_useBrushSettingsModel = false;
    // Bitmask of setting indices whose dynamics slot has an *active* binding
    // to BrushInputSourceKey::RandomValue. Recomputed in setBrushSettings.
    // 22 setting keys → fits easily in uint32_t.
    uint32_t m_randomBoundSettingMask = 0u;
};

} // namespace aether

#endif // RUWA_CORE_TILES_TILEBRUSH_H
