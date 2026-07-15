// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   Q U I C K   S H A P E   M O R P H
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_QUICK_SHAPE_QUICKSHAPEMORPH_H
#define RUWA_FEATURES_CANVAS_QUICK_SHAPE_QUICKSHAPEMORPH_H

#include "features/canvas/quick-shape/QuickShapeDetector.h"
#include "features/brush/engine/BrushStrokeReplay.h"
#include "shared/tiles/TileTypes.h"

#include <QObject>
#include <QTimer>
#include <array>
#include <functional>
#include <memory>
#include <unordered_set>
#include <vector>

namespace aether {

class TileGrid;

// ==========================================================================
//   Q U I C K   S H A P E   M O R P H
// ==========================================================================

class QuickShapeMorph : public QObject {
    Q_OBJECT

public:
    enum class Kind {
        None,
        Line,
        Circle,
        Square,
        TriangleUp,
        TriangleDown,
        TriangleLeft,
        TriangleRight
    };

    struct State {
        bool active = false;
        Kind kind = Kind::None;
        float progress = 0.0f;
        float anchorX = 0.0f;
        float anchorY = 0.0f;
        float cursorX = 0.0f;
        float cursorY = 0.0f;
        float cursorTargetX = 0.0f;
        float cursorTargetY = 0.0f;
        float circleCenterX = 0.0f;
        float circleCenterY = 0.0f;
        float circleAngleDirection = 1.0f;
        float squareHalfExtent = 0.0f;
        float triangleHalfWidth = 0.0f;
        float triangleHalfHeight = 0.0f;
        float trianglePrimaryAxisSign = 1.0f;
        std::array<int, 3> triangleVertexOrder { 0, 1, 2 };
        std::array<float, 3> triangleVertexParams { 0.0f, 0.33f, 0.66f };
        bool triangleMappingValid = false;
        bool cursorDirty = false;
        std::vector<ruwa::core::brushes::BrushStrokeReplayPoint> sourceDabs;
        std::vector<ruwa::core::brushes::BrushStrokeReplayPoint> targetDabs;
        std::vector<float> shapeParams;
    };

    struct Callbacks {
        std::function<std::shared_ptr<ruwa::core::brushes::IEditableBrushStrokeReplayData>()>
            getStrokeReplayData;
        std::function<bool()> isDrawing;
        std::function<TileGrid*()> activeLayerTileGrid;
        std::function<TileGrid*()> getSelectionMask;
        std::function<float()> getBrushEffectiveRadius;
        std::function<float()> getBrushSpacing;
        std::function<std::pair<float, float>()> getLastStrokePosition;
        std::function<void()> rebuildPreview;
        std::function<void()> onMorphApplied;
    };

    explicit QuickShapeMorph(QObject* parent, Callbacks callbacks);

    bool isActive() const { return m_state.active; }
    const State& state() const { return m_state; }

    void restartHoldTimer();
    void stopHoldTimer();
    void stop();

    bool onHoldTimeout();
    void onMorphTick(float lastStrokeX, float lastStrokeY);

    /// Call from input handler when cursor moves while morph is active.
    void updateCursorTarget(float x, float y);

    /// Translate the morph shape (e.g. when user pans the stroke).
    void translate(float dx, float dy);

signals:
    void strokeModified();

private:
    bool start();
    void apply(float easedT, bool rebuildPreview);
    static Kind kindFromTriangleDirection(QuickTriangleDirection direction);

    Callbacks m_callbacks;
    State m_state;
    QTimer m_holdTimer;
    QTimer m_morphTimer;
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_QUICK_SHAPE_QUICKSHAPEMORPH_H
