// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_BRUSHENGINE_BRUSHSTROKEREPLAY_H
#define RUWA_CORE_BRUSHENGINE_BRUSHSTROKEREPLAY_H

#include "shared/tiles/TileTypes.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ruwa::core::brushes {

struct BrushStrokeReplayPoint {
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
    float radius = 0.0f;
    float baseRadius = 0.0f;
    float hardness = 1.0f;
    float roundness = 1.0f;
    float angleDegrees = 0.0f;
    bool useMaxBlend = true;
    bool strokeTimeAvailable = false;
    uint8_t colorR = 255;
    uint8_t colorG = 255;
    uint8_t colorB = 255;
    uint8_t alpha = 0;
    uint8_t baseAlpha = 0;
};

class IBrushStrokeReplayData {
public:
    virtual ~IBrushStrokeReplayData() = default;

    virtual bool empty() const = 0;
    virtual size_t size() const = 0;
    virtual std::vector<BrushStrokeReplayPoint> points() const = 0;
    virtual std::shared_ptr<IBrushStrokeReplayData> clone() const = 0;
};

class IEditableBrushStrokeReplayData : public IBrushStrokeReplayData {
public:
    ~IEditableBrushStrokeReplayData() override = default;

    virtual bool replacePoints(const std::vector<BrushStrokeReplayPoint>& points) = 0;
    virtual bool translate(float dx, float dy) = 0;
};

} // namespace ruwa::core::brushes

#endif // RUWA_CORE_BRUSHENGINE_BRUSHSTROKEREPLAY_H
