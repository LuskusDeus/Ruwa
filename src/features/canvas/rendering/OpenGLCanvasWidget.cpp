// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   O P E N G L   C A N V A S   W I D G E T
// ==========================================================================

#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/canvas/rendering/PaintGLCameraFrameState.h"
#include "features/canvas/rendering/CompositeLayerKeys.h"
#include "features/canvas/rendering/ExportContentBoundsCalc.h"
#include "features/canvas/rendering/GLRenderer.h"
#include "features/canvas/rendering/GLCompositor.h"
#include "features/canvas/rendering/GLRetainedRenderer.h"
#include "features/canvas/rendering/GLTileRenderer.h"
#include "features/canvas/rendering/GLLassoMaskRenderer.h"
#include "features/canvas/rendering/GLTargetLayerPreviewPass.h"
#include "features/canvas/rendering/GLTransformViewportPreviewPass.h"
#include "features/canvas/rendering/GLViewportCompositor.h"
#include "features/canvas/rendering/CanvasBackdropCapture.h"
#include "features/canvas/rendering/LayerScreenSourceCache.h"
#include "features/canvas/rendering/TextRetainedPayloadBuilder.h"
#include "features/brush/rendering/DabShapeCache.h"
#include "features/canvas/rendering/BrushCursorContourBuilder.h"
#include "features/brush/rendering/GLBrushRenderer.h"
#include "features/transform/GLTransformRenderer.h"
#include "features/selection/GLSelectionRenderer.h"
#include "features/brush/engine/BrushEngine.h"
#include "features/brush/engine/BrushEngineRegistry.h"
#include "features/brush/engine/PixelBrushModule.h"
#include "features/canvas/overlays/CanvasOverlayManager.h"
#include "features/canvas/overlays/TransformOverlay.h"
#include "features/canvas/overlays/CanvasResizeOverlayGL.h"
#include "features/canvas/overlays/BrushCursorOverlayGL.h"
#include "features/canvas/overlays/EyedropperCursorOverlayGL.h"
#include "features/canvas/overlays/LassoOverlay.h"
#include "features/canvas/overlays/LassoFillOverlay.h"
#include "features/canvas/overlays/TextEditOverlayGL.h"
#include "features/canvas/selection/PolygonClipUtils.h"
#include "features/canvas/selection/SelectionMaskOps.h"
#include "features/canvas/scene/CanvasDisplayTransforms.h"
#include "features/transform/TransformApplicator.h"
#include "features/transform/TransformSessionCommand.h"

#include <QDebug>

#include "features/layers/model/LayerModel.h"
#include "features/layers/model/LayerData.h"
#include "features/effects/EffectCoverageResolver.h"
#include "features/settings/SettingsManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "features/theme/manager/ThemeManager.h"

#include "shared/undo/UndoManager.h"
#include "shared/undo/SelectionState.h"
#include "shared/undo/SelectionCommand.h"
#include "shared/undo/LayerAddCommand.h"
#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TilePixelAccess.h"
#include "shared/types/GeometryHelpers.h"
#include "shared/widgets/DotGridLoadingIndicator.h"
#include "features/canvas/undo/DrawCommand.h"
#include "features/canvas/undo/ApplyMaskCommand.h"
#include "features/canvas/undo/ApplyLayerEffectsCommand.h"
#include "features/canvas/undo/RasterizeLayerCommand.h"
#include "shared/undo/InvertMaskCommand.h"
#include "features/fill/FloodFill.h"
#include "features/fill/FillRawTileOps.h"
#include "features/fill/FillProgressivePolicy.h"
#include "features/fill/GLFillRenderer.h"
#include "shared/rendering/ShaderDirectoryResolver.h"

#include <QCoreApplication>
#include <QCursor>
#include <QScreen>
#include <QWindow>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLinearGradient>
#include <QMessageBox>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QOpenGLVersionFunctionsFactory>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPropertyAnimation>
#include <QStringList>
#include <QShowEvent>
#include <QSet>
#include <QThread>

#include "platform/windows/WindowsInkFeedback.h"
#include "features/canvas/rendering/LayerCompositingBuilder.h"
#include "features/canvas/selection/CanvasSelectionController.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
inline int32_t floorDiv(int32_t a, int32_t b)
{
    return (a >= 0) ? (a / b) : ((a - b + 1) / b);
}

constexpr int kAutoFlipAnimationDurationMs
    = static_cast<int>(aether::TransformController::SCALE_ANIMATION_DURATION * 1000.0f) + 16;

std::unordered_set<aether::TileKey, aether::TileKeyHash> retainedTextTileKeys(
    ruwa::core::layers::LayerData* layer)
{
    if (!layer || !layer->isText() || !aether::ensureTextRetainedPayload(layer)
        || !layer->runtimeRetainedPayload) {
        return {};
    }
    return aether::retainedCoverageTileKeys(layer->runtimeRetainedPayload->worldBounds);
}

aether::TransformState stateWithSourceBounds(
    const aether::TransformState& storedState, const aether::Rect& sourceBounds)
{
    aether::TransformState state = storedState;
    if (state.contentBounds.width <= 0.0f || state.contentBounds.height <= 0.0f) {
        state.contentBounds = sourceBounds;
        state.pivot = sourceBounds.center();
        state.reset();
        return state;
    }

    state.contentBounds = sourceBounds;
    if (!state.hasFreeQuad() && !state.hasDeformMesh()) {
        const aether::Vector2 oldPivot = state.pivot;
        const aether::Vector2 newPivot = sourceBounds.center();
        const float dpx = newPivot.x - oldPivot.x;
        const float dpy = newPivot.y - oldPivot.y;
        const float sdx = dpx * state.scale.x;
        const float sdy = dpy * state.scale.y;
        const float cosR = std::cos(state.rotation);
        const float sinR = std::sin(state.rotation);
        state.translation.x += (sdx * cosR - sdy * sinR) - dpx;
        state.translation.y += (sdx * sinR + sdy * cosR) - dpy;
        state.pivot = newPivot;
    }
    return state;
}

bool nearlyEqual(float a, float b, float epsilon = 0.0001f)
{
    return std::abs(a - b) <= epsilon;
}

bool vectorNearlyEqual(const aether::Vector2& a, const aether::Vector2& b)
{
    return nearlyEqual(a.x, b.x) && nearlyEqual(a.y, b.y);
}

bool rectNearlyEqual(const aether::Rect& a, const aether::Rect& b)
{
    return nearlyEqual(a.x, b.x) && nearlyEqual(a.y, b.y) && nearlyEqual(a.width, b.width)
        && nearlyEqual(a.height, b.height);
}

bool freeCornersNearlyEqual(const std::optional<std::array<aether::Vector2, 4>>& a,
    const std::optional<std::array<aether::Vector2, 4>>& b)
{
    if (a.has_value() != b.has_value()) {
        return false;
    }
    if (!a.has_value()) {
        return true;
    }
    for (size_t i = 0; i < a->size(); ++i) {
        if (!vectorNearlyEqual((*a)[i], (*b)[i])) {
            return false;
        }
    }
    return true;
}

bool deformMeshesNearlyEqual(const std::optional<aether::TransformState::DeformMesh>& a,
    const std::optional<aether::TransformState::DeformMesh>& b)
{
    if (a.has_value() != b.has_value()) {
        return false;
    }
    if (!a.has_value()) {
        return true;
    }
    if (a->rows != b->rows || a->cols != b->cols || a->vertices.size() != b->vertices.size()) {
        return false;
    }
    for (size_t i = 0; i < a->vertices.size(); ++i) {
        if (!vectorNearlyEqual(a->vertices[i].source, b->vertices[i].source)
            || !vectorNearlyEqual(a->vertices[i].target, b->vertices[i].target)) {
            return false;
        }
    }
    return true;
}

bool transformStatesNearlyEqual(const aether::TransformState& a, const aether::TransformState& b)
{
    return rectNearlyEqual(a.contentBounds, b.contentBounds)
        && vectorNearlyEqual(a.translation, b.translation) && nearlyEqual(a.rotation, b.rotation)
        && vectorNearlyEqual(a.scale, b.scale) && vectorNearlyEqual(a.pivot, b.pivot)
        && freeCornersNearlyEqual(a.freeCorners, b.freeCorners)
        && deformMeshesNearlyEqual(a.deformMesh, b.deformMesh);
}

std::optional<aether::Rect> transformBoundsForLayer(const ruwa::core::layers::LayerData* layer)
{
    if (!layer) {
        return std::nullopt;
    }
    if (layer->isRaster()) {
        const auto* grid = layer->pixelGrid();
        if (!grid || grid->empty()) {
            return std::nullopt;
        }
        return aether::TransformState::computeContentBounds(*grid);
    }
    if (layer->isIsolatedPixelLayer()) {
        const auto* grid = layer->smartContentGrid.get();
        if (!grid || grid->empty()) {
            return std::nullopt;
        }
        const aether::Rect sourceBounds = aether::TransformState::computeContentBounds(*grid);
        if (sourceBounds.width <= 0.0f || sourceBounds.height <= 0.0f) {
            return std::nullopt;
        }
        return stateWithSourceBounds(layer->smartTransform, sourceBounds).transformedAABB();
    }
    if (layer->isText() && layer->textData) {
        const aether::Rect sourceBounds = aether::computeTextLayoutSourceBounds(*layer->textData);
        if (sourceBounds.width <= 0.0f || sourceBounds.height <= 0.0f) {
            return std::nullopt;
        }
        return stateWithSourceBounds(layer->textData->transform, sourceBounds).transformedAABB();
    }
    return std::nullopt;
}

struct MoveToolContentHit {
    bool blocksBelow = false;
    QUuid targetLayerId;
};

bool gridPixelAt(const aether::TileGrid* grid, const aether::Vector2& position, float (&pixel)[4])
{
    if (!grid) {
        return false;
    }

    const int32_t x = static_cast<int32_t>(std::floor(position.x));
    const int32_t y = static_cast<int32_t>(std::floor(position.y));
    const int32_t tileSize = static_cast<int32_t>(aether::TILE_SIZE);
    const aether::TileKey key { floorDiv(x, tileSize), floorDiv(y, tileSize) };
    const auto* tile = grid->getTile(key);
    if (!tile) {
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        uint8_t a = 0;
        grid->defaultFill(r, g, b, a);
        pixel[0] = static_cast<float>(r) / 255.0f;
        pixel[1] = static_cast<float>(g) / 255.0f;
        pixel[2] = static_cast<float>(b) / 255.0f;
        pixel[3] = static_cast<float>(a) / 255.0f;
        return true;
    }

    const uint32_t localX = static_cast<uint32_t>(x - key.x * tileSize);
    const uint32_t localY = static_cast<uint32_t>(y - key.y * tileSize);
    aether::readTilePixelF(*tile, localX, localY, pixel);
    return true;
}

float gridPixelAlphaAt(const aether::TileGrid* grid, const aether::Vector2& position)
{
    float pixel[4] {};
    return gridPixelAt(grid, position, pixel) ? pixel[3] : 0.0f;
}

bool layerMaskRevealsPosition(
    const ruwa::core::layers::LayerData* layer, const aether::Vector2& worldPos)
{
    if (!layer || !layer->maskAffectsCompositing()) {
        return true;
    }

    float pixel[4] {};
    if (!gridPixelAt(layer->maskTileGrid(), worldPos, pixel)) {
        return true;
    }

    // Keep this identical to the compositor's luminance-reveal mask rule.
    const float reveal
        = 0.299f * pixel[0] + 0.587f * pixel[1] + 0.114f * pixel[2] + (1.0f - pixel[3]);
    return reveal > 0.0f;
}

bool retainedPayloadHasPixelAt(
    const aether::RetainedRenderPayload& payload, const aether::Vector2& worldPos)
{
    const aether::TileKey key = aether::worldToTile(worldPos.x, worldPos.y);
    const QImage tileImage = aether::GLRetainedRenderer::renderPayloadTileImage(payload, key);
    if (tileImage.isNull()) {
        return false;
    }

    float tileX = 0.0f;
    float tileY = 0.0f;
    aether::tileWorldOrigin(key, tileX, tileY);
    const int localX = static_cast<int>(std::floor(worldPos.x - tileX));
    const int localY = static_cast<int>(std::floor(worldPos.y - tileY));
    if (localX < 0 || localY < 0 || localX >= tileImage.width() || localY >= tileImage.height()) {
        return false;
    }
    return tileImage.constScanLine(localY)[localX * 4 + 3] != 0;
}

bool layerHasPixelAt(ruwa::core::layers::LayerData* layer, const aether::Vector2& worldPos)
{
    if (!layer) {
        return false;
    }

    if (layer->isRaster()) {
        return gridPixelAlphaAt(layer->pixelGrid(), worldPos) > 0.0f;
    }

    if (layer->isIsolatedPixelLayer() && layer->smartContentGrid) {
        const aether::Rect sourceBounds
            = aether::TransformState::computeContentBounds(*layer->smartContentGrid);
        if (sourceBounds.width <= 0.0f || sourceBounds.height <= 0.0f) {
            return false;
        }

        aether::Vector2 sourcePos;
        const aether::TransformState state
            = stateWithSourceBounds(layer->smartTransform, sourceBounds);
        return state.tryInverseTransformPoint(worldPos, sourcePos)
            && gridPixelAlphaAt(layer->smartContentGrid.get(), sourcePos) > 0.0f;
    }

    if (layer->isText() && aether::ensureTextRetainedPayload(layer)
        && layer->runtimeRetainedPayload) {
        return retainedPayloadHasPixelAt(*layer->runtimeRetainedPayload, worldPos);
    }

    return false;
}

MoveToolContentHit hitTestMoveToolContentLayerList(
    const QList<std::shared_ptr<ruwa::core::layers::LayerData>>& layers,
    const aether::Vector2& worldPos)
{
    for (const auto& layerPtr : layers) {
        auto* layer = layerPtr.get();
        if (!layer || !layer->visible || layer->opacity <= 0.0) {
            continue;
        }
        if (!layerMaskRevealsPosition(layer, worldPos)) {
            continue;
        }

        if (layer->hasChildren()) {
            MoveToolContentHit childHit
                = hitTestMoveToolContentLayerList(layer->children, worldPos);
            if (childHit.blocksBelow || !childHit.targetLayerId.isNull()) {
                return childHit;
            }
        }

        if (layer->isBackground() || !aether::transformIsVisualTarget(layer)) {
            continue;
        }

        const std::optional<aether::Rect> bounds = transformBoundsForLayer(layer);
        if (!bounds.has_value() || bounds->width <= 0.0f || bounds->height <= 0.0f
            || !bounds->contains(worldPos) || !layerHasPixelAt(layer, worldPos)) {
            continue;
        }

        MoveToolContentHit hit;
        hit.blocksBelow = true;
        if (aether::transformLayerHierarchyEditable(layer)) {
            hit.targetLayerId = layer->id;
        }
        return hit;
    }
    return {};
}

aether::TransformState currentNonRasterTransformState(const ruwa::core::layers::LayerData* layer)
{
    if (!layer) {
        return {};
    }
    if (layer->isText() && layer->textData) {
        const aether::Rect sourceBounds = aether::computeTextLayoutSourceBounds(*layer->textData);
        return stateWithSourceBounds(layer->textData->transform, sourceBounds);
    }
    if (layer->isIsolatedPixelLayer() && layer->smartContentGrid) {
        const aether::Rect sourceBounds
            = aether::TransformState::computeContentBounds(*layer->smartContentGrid);
        return stateWithSourceBounds(layer->smartTransform, sourceBounds);
    }
    return {};
}

aether::TransformState composeLayerTransform(
    const aether::TransformState& before, const aether::TransformState& sessionState)
{
    aether::TransformState after = before;
    after.reset();
    after.pivot = after.contentBounds.center();

    if (before.hasDeformMesh() || sessionState.hasDeformMesh()) {
        aether::TransformState::DeformMesh mesh {};
        mesh.rows = aether::TransformState::DEFORM_MESH_ROWS;
        mesh.cols = aether::TransformState::DEFORM_MESH_COLS;
        mesh.vertices.reserve(mesh.rows * mesh.cols);
        for (int row = 0; row < mesh.rows; ++row) {
            const float v = (mesh.rows > 1) ? static_cast<float>(row) / (mesh.rows - 1) : 0.5f;
            for (int col = 0; col < mesh.cols; ++col) {
                const float u = (mesh.cols > 1) ? static_cast<float>(col) / (mesh.cols - 1) : 0.5f;
                aether::Vector2 source { before.contentBounds.left()
                        + u * before.contentBounds.width,
                    before.contentBounds.top() + v * before.contentBounds.height };
                const aether::Vector2 target
                    = sessionState.transformPoint(before.transformPoint(source));
                mesh.vertices.push_back({ source, target });
            }
        }
        after.deformMesh = std::move(mesh);
        return after;
    }

    std::array<aether::Vector2, 4> corners = before.transformedCorners();
    for (auto& corner : corners) {
        corner = sessionState.transformPoint(corner);
    }
    after.freeCorners = corners;
    return after;
}

std::unordered_map<aether::TileKey, std::vector<uint8_t>, aether::TileKeyHash> snapshotGridTiles(
    const aether::TileGrid& grid)
{
    std::unordered_map<aether::TileKey, std::vector<uint8_t>, aether::TileKeyHash> snapshot;
    snapshot.reserve(grid.tiles().size());
    for (const auto& [key, tile] : grid.tiles()) {
        auto& buffer = snapshot[key];
        const int tileBytes = static_cast<int>(aether::tileByteSize(tile.format()));
        buffer.resize(tileBytes);
        if (tile.isSolid()) {
            // const pixels() returns zeros for a solid tile; expand the uniform
            // color into the buffer so undo restores the real reveal.
            uint8_t r, g, b, a;
            tile.solidColor(r, g, b, a);
            aether::fillTileSolid(buffer.data(), tile.format(), r, g, b, a);
        } else {
            std::memcpy(buffer.data(), tile.pixels(), tileBytes);
        }
    }
    return snapshot;
}

inline uint32_t floorMod(int32_t a, int32_t b)
{
    int32_t m = a % b;
    return static_cast<uint32_t>(m < 0 ? m + b : m);
}

constexpr float kQuickLineMovementEpsilon = 0.05f;
constexpr double kRealtimePreviewSamplingEnableRateHz = 140.0;
constexpr double kRealtimePreviewSamplingTargetHz = 90.0;
constexpr size_t kRealtimePreviewSamplingMinDabs = 48;
constexpr size_t kRealtimePreviewSamplingMaxDabs = 768;
constexpr size_t kStrokeInputBatchMaxSamples = 24;
constexpr qint64 kStrokeInputBatchBudgetMs = 4;
constexpr qint64 kCanvasCornerIdleDelayMs = 1000;
constexpr qint64 kCanvasCornerInteractionCooldownMs = 160;
constexpr qint64 kCanvasCornerFrameDelayMs = 16;
constexpr float kCanvasCornerVisibilityMarginPx = 0.5f;
constexpr float kCanvasCornerMaxScreenRadiusPx = 12.0f;
constexpr float kCanvasCornerAnimationSpeed = 14.0f;
constexpr qint64 kFillPreviewStartDelayMs = 1000;
constexpr qint64 kClassicFillWaitPopupDelayMs = 2000;
constexpr float kFillPreviewPauseRadiusPx = 1000.0f;
constexpr int kFillProgressPopupMargin = 8;
constexpr int kFillProgressPopupOffsetY = 18;

std::unique_ptr<ruwa::core::brushes::IBrushEngineSession> createDefaultBrushSession()
{
    ruwa::core::brushes::BrushSessionConfig config;
    config.engineVersion = ruwa::core::brushes::kPixelBrushEngineVersion;
    if (const auto* module = ruwa::core::brushes::BrushEngineRegistry::instance().pixelModule()) {
        config.settings = module->defaultSettings();
        if (auto session = module->createSession(config)) {
            return session;
        }
    }

    config.settings = ruwa::core::brushes::PixelBrushModule {}.defaultSettings();
    return std::make_unique<ruwa::core::brushes::PixelBrushSession>(config);
}

aether::TileBrush* pixelBrushFromSession(ruwa::core::brushes::IBrushEngineSession* session)
{
    auto* pixelSession = dynamic_cast<ruwa::core::brushes::PixelBrushSession*>(session);
    return pixelSession ? &pixelSession->brush() : nullptr;
}

std::shared_ptr<ruwa::core::brushes::IEditableBrushStrokeReplayData>
activeStrokeReplayDataFromSession(ruwa::core::brushes::IBrushEngineSession* session)
{
    return session ? session->activeStrokeReplayData() : nullptr;
}

aether::TileGrid makeTechnicalWarmupGrid(uint8_t alpha)
{
    aether::TileGrid grid;
    auto& tile = grid.getOrCreateTile(aether::TileKey { 0, 0 });
    const uint32_t center = aether::TILE_SIZE / 2;
    tile.setPixel(center, center, alpha, alpha, alpha, alpha);
    return grid;
}

double elapsedMs(const QElapsedTimer& timer)
{
    return static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
}

const QUuid& lassoPreviewSelectionMaskCacheId()
{
    static const QUuid id(QStringLiteral("{5b4ce0bb-14e7-4bc5-9ed4-bcb954cf6989}"));
    return id;
}

QString formatTilePayload(size_t tileCount)
{
    const double mib = static_cast<double>(tileCount) * static_cast<double>(aether::TILE_BYTE_SIZE)
        / (1024.0 * 1024.0);
    return QStringLiteral("%1 tiles (~%2 MiB)")
        .arg(static_cast<qulonglong>(tileCount))
        .arg(mib, 0, 'f', 1);
}

} // namespace

namespace aether {

namespace {

template <typename Overlay>
void ensureCursorOverlayInitialized(Overlay* overlay, const char* overlayName)
{
    if (!overlay) {
        return;
    }

    const auto result = overlay->initialize();
    if (!result) { }
}

QSize currentSurfacePixelSize(const aether::OpenGLCanvasWidget* widget)
{
    if (!widget) {
        return QSize(1, 1);
    }

    const int viewportWidth = static_cast<int>(widget->viewport().width());
    const int viewportHeight = static_cast<int>(widget->viewport().height());
    if (viewportWidth > 0 && viewportHeight > 0) {
        return QSize(viewportWidth, viewportHeight);
    }

    const qreal dpr = widget->devicePixelRatioF();
    return QSize(std::max(1, qRound(static_cast<qreal>(widget->width()) * dpr)),
        std::max(1, qRound(static_cast<qreal>(widget->height()) * dpr)));
}

} // namespace

class FillProgressPopupWidget final : public QWidget {
public:
    static constexpr int ProcessingTextWidth = 400;
    static constexpr int CompactProcessingTextWidth = 120;
    static constexpr int DoneTextWidth = 96;
    static constexpr int ProcessingIndicatorSize = 22;
    static constexpr int DoneIndicatorSize = 16;

    explicit FillProgressPopupWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_ShowWithoutActivating);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(12, 8, 12, 8);
        layout->setSpacing(8);

        m_indicator = new ruwa::ui::widgets::DotGridLoadingIndicator(this);
        m_indicator->setFixedSize(16, 16);
        layout->addWidget(m_indicator, 0, Qt::AlignVCenter);

        m_label = new QLabel(this);
        m_label->setWordWrap(true);
        m_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        m_label->setMinimumWidth(ProcessingTextWidth);
        m_label->setMaximumWidth(ProcessingTextWidth);
        layout->addWidget(m_label, 1);

        m_opacityEffect = new QGraphicsOpacityEffect(this);
        m_opacityEffect->setOpacity(0.0);
        setGraphicsEffect(m_opacityEffect);

        m_opacityAnim = new QPropertyAnimation(m_opacityEffect, "opacity", this);
        m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);

        m_posAnim = new QPropertyAnimation(this, "pos", this);
        m_posAnim->setEasingCurve(QEasingCurve::OutCubic);

        m_geometryAnim = new QPropertyAnimation(this, "geometry", this);
        m_geometryAnim->setEasingCurve(QEasingCurve::OutCubic);

        connect(&ruwa::ui::core::ThemeManager::instance(),
            &ruwa::ui::core::ThemeManager::themeChanged, this, [this]() {
                updateTheme();
                updateGeometry();
                update();
            });

        updateTheme();
        hide();
    }

    void showProcessingAt(const QPoint& anchorPoint)
    {
        showProcessingAt(anchorPoint,
            QCoreApplication::translate(
                "OpenGLCanvasWidget", "Filling the area. Live preview is paused. Please wait."),
            ProcessingTextWidth);
    }

    void showProcessingAt(const QPoint& anchorPoint, const QString& text, int textWidth)
    {
        ++m_transitionToken;
        m_state = State::Processing;
        m_processingTextWidth = std::max(1, textWidth);
        m_label->setText(text);
        m_indicator->show();
        m_indicator->start();
        applyStateSizing();
        updateTheme();
        if (layout()) {
            layout()->activate();
        }
        const QSize targetSize = sizeHint();
        resize(targetSize);
        startShow(popupTopLeftForAnchor(anchorPoint, targetSize));
    }

    void showDoneAt(const QPoint& anchorPoint)
    {
        const int token = ++m_transitionToken;
        const bool morphFromProcessing = isVisible() && !m_isHiding && m_state == State::Processing;
        const QRect currentGeometry = geometry();

        m_state = State::Done;
        m_label->setText(QCoreApplication::translate("OpenGLCanvasWidget", "Done!"));
        m_indicator->stop();
        m_indicator->hide();
        applyStateSizing();
        updateTheme();
        if (layout()) {
            layout()->activate();
        }

        const QSize targetSize = sizeHint();
        const QRect targetGeometry(popupTopLeftForAnchor(anchorPoint, targetSize), targetSize);

        if (morphFromProcessing) {
            startMorph(currentGeometry, targetGeometry, token);
        } else {
            resize(targetSize);
            startShow(targetGeometry.topLeft());
            scheduleDoneHide(token);
        }
    }

    void updateAnchor(const QPoint& topLeft)
    {
        if (!isVisible() || m_isHiding) {
            return;
        }

        if (m_posAnim->state() == QAbstractAnimation::Running) {
            m_posAnim->setEndValue(topLeft);
        } else if (pos() != topLeft) {
            move(topLeft);
        }
    }

    void hideImmediate()
    {
        ++m_transitionToken;
        m_state = State::Hidden;
        m_isHiding = false;
        m_indicator->stop();
        m_indicator->hide();
        m_opacityAnim->stop();
        m_posAnim->stop();
        m_geometryAnim->stop();
        m_opacityEffect->setOpacity(0.0);
        hide();
    }

    bool isProcessingVisible() const
    {
        return isVisible() && !m_isHiding && m_state == State::Processing;
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        QRectF rect = this->rect().adjusted(0.5, 0.5, -0.5, -0.5);
        constexpr qreal radius = 8.0;

        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.surfaceElevated());
        painter.drawRoundedRect(rect, radius, radius);

        QPainterPath borderPath;
        QRectF borderRect = rect.adjusted(0.5, 0.5, -0.5, -0.5);
        borderPath.addRoundedRect(borderRect, radius - 0.5, radius - 0.5);

        QLinearGradient borderGradient(borderRect.topLeft(), borderRect.bottomLeft());
        QColor borderTop = colors.borderSubtle();
        QColor borderBottom
            = ruwa::ui::core::ThemeColors::withAlpha(borderTop, borderTop.alpha() / 2);
        borderGradient.setColorAt(0.0, borderTop);
        borderGradient.setColorAt(1.0, borderBottom);

        QPen borderPen;
        borderPen.setBrush(borderGradient);
        borderPen.setWidth(1);
        borderPen.setCosmetic(true);

        painter.setPen(borderPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(borderPath);
    }

private:
    enum class State { Hidden, Processing, Done };

    void startShow(const QPoint& topLeft)
    {
        const QPoint startPos = topLeft + QPoint(0, 10);

        m_isHiding = false;
        disconnect(m_opacityAnim, &QPropertyAnimation::finished, this, nullptr);
        m_geometryAnim->stop();
        if (pos() != startPos) {
            move(startPos);
        }
        show();
        raise();

        m_opacityAnim->stop();
        m_opacityAnim->setDuration(120);
        m_opacityAnim->setStartValue(m_opacityEffect->opacity());
        m_opacityAnim->setEndValue(1.0);

        m_posAnim->stop();
        m_posAnim->setDuration(120);
        m_posAnim->setStartValue(startPos);
        m_posAnim->setEndValue(topLeft);

        m_opacityAnim->start();
        m_posAnim->start();
    }

    void startHide()
    {
        if (!isVisible() || m_isHiding) {
            return;
        }

        m_isHiding = true;
        m_indicator->stop();

        const QPoint currentPos = pos();

        m_opacityAnim->stop();
        m_opacityAnim->setDuration(180);
        m_opacityAnim->setStartValue(m_opacityEffect->opacity());
        m_opacityAnim->setEndValue(0.0);

        m_posAnim->stop();
        m_posAnim->setDuration(180);
        m_posAnim->setStartValue(currentPos);
        m_posAnim->setEndValue(currentPos - QPoint(0, 10));

        m_geometryAnim->stop();

        disconnect(m_opacityAnim, &QPropertyAnimation::finished, this, nullptr);
        connect(m_opacityAnim, &QPropertyAnimation::finished, this, [this]() {
            if (!m_isHiding) {
                return;
            }
            m_state = State::Hidden;
            m_isHiding = false;
            m_indicator->hide();
            hide();
        });

        m_opacityAnim->start();
        m_posAnim->start();
    }

    void startMorph(const QRect& startGeometry, const QRect& targetGeometry, int token)
    {
        m_posAnim->stop();
        m_geometryAnim->stop();
        m_geometryAnim->setDuration(150);
        m_geometryAnim->setStartValue(startGeometry);
        m_geometryAnim->setEndValue(targetGeometry);
        setGeometry(startGeometry);

        disconnect(m_geometryAnim, &QPropertyAnimation::finished, this, nullptr);
        connect(m_geometryAnim, &QPropertyAnimation::finished, this, [this, token]() {
            if (token != m_transitionToken || m_state != State::Done || m_isHiding) {
                return;
            }
            scheduleDoneHide(token);
        });

        m_geometryAnim->start();
    }

    void scheduleDoneHide(int token)
    {
        QTimer::singleShot(500, this, [this, token]() {
            if (token != m_transitionToken || m_state != State::Done || m_isHiding) {
                return;
            }
            startHide();
        });
    }

    void updateTheme()
    {
        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const auto& colors = theme.colors();

        QFont font = colors.fonts.getUIFont(theme.scaledFontSize(9));
        font.setWeight(QFont::Medium);
        m_label->setFont(font);
        m_label->setStyleSheet(
            QString("QLabel { background: transparent; color: %1; }").arg(colors.text.name()));

        const int indicatorBaseSize
            = (m_state == State::Processing) ? ProcessingIndicatorSize : DoneIndicatorSize;
        const int indicatorSize = theme.scaled(indicatorBaseSize);
        m_indicator->setFixedSize(indicatorSize, indicatorSize);
        m_indicator->setAccentColor(colors.primary);

        const int textWidth
            = theme.scaled((m_state == State::Processing) ? m_processingTextWidth : DoneTextWidth);
        m_label->setMinimumWidth(textWidth);
        m_label->setMaximumWidth(textWidth);

        if (auto* layout = qobject_cast<QHBoxLayout*>(this->layout())) {
            const int verticalPadding = (m_state == State::Processing) ? 6 : 8;
            layout->setContentsMargins(theme.scaled(12), theme.scaled(verticalPadding),
                theme.scaled(12), theme.scaled(verticalPadding));
            layout->setSpacing(theme.scaled(8));
        }
    }

    void applyStateSizing()
    {
        if (m_state == State::Processing) {
            m_label->setMinimumWidth(m_processingTextWidth);
            m_label->setMaximumWidth(m_processingTextWidth);
            m_label->setWordWrap(true);
        } else {
            m_label->setMinimumWidth(DoneTextWidth);
            m_label->setMaximumWidth(DoneTextWidth);
            m_label->setWordWrap(false);
        }
    }

    QPoint popupTopLeftForAnchor(const QPoint& anchorPoint, const QSize& popupSize) const
    {
        constexpr int popupMargin = 8;
        constexpr int popupOffsetY = 18;

        int x = anchorPoint.x() - popupSize.width() / 2;
        int y = anchorPoint.y() - popupSize.height() - popupOffsetY;

        if (auto* parent = parentWidget()) {
            x = qBound(popupMargin, x,
                qMax(popupMargin, parent->width() - popupSize.width() - popupMargin));
            y = qBound(popupMargin, y,
                qMax(popupMargin, parent->height() - popupSize.height() - popupMargin));
        }

        return QPoint(x, y);
    }

    ruwa::ui::widgets::DotGridLoadingIndicator* m_indicator = nullptr;
    QLabel* m_label = nullptr;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QPropertyAnimation* m_opacityAnim = nullptr;
    QPropertyAnimation* m_posAnim = nullptr;
    QPropertyAnimation* m_geometryAnim = nullptr;
    int m_processingTextWidth = ProcessingTextWidth;
    State m_state = State::Hidden;
    bool m_isHiding = false;
    int m_transitionToken = 0;
};

} // namespace aether

namespace {

template <typename RawTileMap> RawTileMap snapshotRawTiles(const aether::TileGrid& grid)
{
    RawTileMap tiles;
    const auto& sourceTiles = grid.tiles();
    tiles.reserve(sourceTiles.size());
    if (sourceTiles.empty()) {
        return tiles;
    }

    // Raw snapshots are sized for the grid's own pixel format (document content
    // grids use the per-document format; layer-mask grids are RGBA8). tile->pixels()
    // returns tileByteSize(format) bytes, so copying that many keeps wider
    // formats intact instead of truncating to the 8-bit size.
    const uint32_t tileBytes = aether::tileByteSize(grid.format());

    using SnapshotEntry = std::pair<aether::TileKey, const aether::TileData*>;
    std::vector<SnapshotEntry> entries;
    entries.reserve(sourceTiles.size());
    for (const auto& [key, tile] : sourceTiles) {
        entries.emplace_back(key, &tile);
    }

    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    const size_t workerCount = std::min(entries.size(),
        hardwareThreads > 1 ? static_cast<size_t>(hardwareThreads - 1) : static_cast<size_t>(1));
    if (workerCount <= 1 || entries.size() < workerCount * 8) {
        for (const auto& [key, tile] : entries) {
            std::vector<uint8_t> raw(tileBytes);
            std::memcpy(raw.data(), tile->pixels(), tileBytes);
            tiles.emplace(key, std::move(raw));
        }
        return tiles;
    }

    std::vector<RawTileMap> localMaps(workerCount);
    std::vector<std::thread> workers;
    workers.reserve(workerCount - 1);
    const size_t chunkSize = (entries.size() + workerCount - 1) / workerCount;

    const auto snapshotChunk = [&](size_t workerIndex, size_t begin, size_t end) {
        auto& local = localMaps[workerIndex];
        local.reserve(end - begin);
        for (size_t i = begin; i < end; ++i) {
            const auto& [key, tile] = entries[i];
            std::vector<uint8_t> raw(tileBytes);
            std::memcpy(raw.data(), tile->pixels(), tileBytes);
            local.emplace(key, std::move(raw));
        }
    };

    for (size_t workerIndex = 1; workerIndex < workerCount; ++workerIndex) {
        const size_t begin = workerIndex * chunkSize;
        const size_t end = std::min(begin + chunkSize, entries.size());
        if (begin >= end) {
            break;
        }

        workers.emplace_back(snapshotChunk, workerIndex, begin, end);
    }

    snapshotChunk(0, 0, std::min(chunkSize, entries.size()));

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    for (auto& local : localMaps) {
        for (auto& [key, raw] : local) {
            tiles.emplace(key, std::move(raw));
        }
    }

    return tiles;
}

struct FillWorkRect {
    int originX = 0;
    int originY = 0;
    int width = 0;
    int height = 0;
    bool forceFinalResultOnly = false;
};

struct PolygonFillWorkArea {
    int originX = 0;
    int originY = 0;
    int width = 0;
    int height = 0;
    std::vector<aether::Vector2> polygon;
};

QRect worldRectFromTileKeys(const std::vector<aether::TileKey>& keys)
{
    if (keys.empty()) {
        return {};
    }

    int minTileX = std::numeric_limits<int>::max();
    int minTileY = std::numeric_limits<int>::max();
    int maxTileX = std::numeric_limits<int>::min();
    int maxTileY = std::numeric_limits<int>::min();
    for (const auto& key : keys) {
        minTileX = std::min(minTileX, key.x);
        minTileY = std::min(minTileY, key.y);
        maxTileX = std::max(maxTileX, key.x);
        maxTileY = std::max(maxTileY, key.y);
    }

    const int tileSize = static_cast<int>(aether::TILE_SIZE);
    const int minX = minTileX * tileSize;
    const int minY = minTileY * tileSize;
    const int maxX = (maxTileX + 1) * tileSize - 1;
    const int maxY = (maxTileY + 1) * tileSize - 1;
    return QRect(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

QList<QPoint> qPointsFromTileKeys(const std::vector<aether::TileKey>& keys)
{
    QList<QPoint> points;
    points.reserve(static_cast<qsizetype>(keys.size()));
    for (const auto& key : keys) {
        points.append(QPoint(key.x, key.y));
    }
    return points;
}

QList<QPoint> qPointsFromTileKeys(
    const std::unordered_set<aether::TileKey, aether::TileKeyHash>& keys)
{
    QList<QPoint> points;
    points.reserve(static_cast<qsizetype>(keys.size()));
    for (const auto& key : keys) {
        points.append(QPoint(key.x, key.y));
    }
    return points;
}

FillWorkRect computeFillWorkRect(const aether::TileGrid* layerGrid,
    const aether::TileGrid* selectionMask, int seedX, int seedY, bool hasFiniteDocumentBounds,
    int canvasW, int canvasH)
{
    if (hasFiniteDocumentBounds) {
        return { 0, 0, canvasW, canvasH, false };
    }

    constexpr int kFillLocalMarginPx = static_cast<int>(aether::TILE_SIZE);
    constexpr int kFillEmptySpanPx = static_cast<int>(aether::TILE_SIZE) * 4;

    bool hasBounds = false;
    int minX = seedX;
    int minY = seedY;
    int maxX = seedX;
    int maxY = seedY;

    auto includeBounds = [&](int bx0, int by0, int bx1, int by1) {
        if (!hasBounds) {
            minX = bx0;
            minY = by0;
            maxX = bx1;
            maxY = by1;
            hasBounds = true;
            return;
        }
        minX = std::min(minX, bx0);
        minY = std::min(minY, by0);
        maxX = std::max(maxX, bx1);
        maxY = std::max(maxY, by1);
    };

    int boundsMinX = 0;
    int boundsMinY = 0;
    int boundsMaxX = 0;
    int boundsMaxY = 0;
    if (aether::computeTileGridContentBounds(
            layerGrid, boundsMinX, boundsMinY, boundsMaxX, boundsMaxY)) {
        includeBounds(boundsMinX, boundsMinY, boundsMaxX, boundsMaxY);
    }

    int selectionWidth = 0;
    int selectionHeight = 0;
    if (selectionMask
        && aether::computeRawMaskPixelBounds(
            snapshotRawTiles<aether::FloodFillResult::RawTileMap>(*selectionMask), 0, 0, boundsMinX,
            boundsMinY, selectionWidth, selectionHeight)) {
        includeBounds(boundsMinX, boundsMinY, boundsMinX + selectionWidth - 1,
            boundsMinY + selectionHeight - 1);
    }

    if (!hasBounds) {
        return { seedX - kFillEmptySpanPx / 2, seedY - kFillEmptySpanPx / 2, kFillEmptySpanPx,
            kFillEmptySpanPx, true };
    }

    includeBounds(seedX, seedY, seedX, seedY);
    minX -= kFillLocalMarginPx;
    minY -= kFillLocalMarginPx;
    maxX += kFillLocalMarginPx;
    maxY += kFillLocalMarginPx;

    return { minX, minY, std::max(1, maxX - minX + 1), std::max(1, maxY - minY + 1), true };
}

PolygonFillWorkArea computePolygonFillWorkArea(const std::vector<aether::Vector2>& polygon,
    bool hasFiniteDocumentBounds, int canvasW, int canvasH)
{
    PolygonFillWorkArea workArea;
    if (polygon.size() < 3) {
        return workArea;
    }

    if (hasFiniteDocumentBounds) {
        workArea.polygon = aether::clipPolygonToCanvas(
            polygon, static_cast<float>(canvasW), static_cast<float>(canvasH));
        if (workArea.polygon.size() < 3 || canvasW <= 0 || canvasH <= 0) {
            workArea.polygon.clear();
            return workArea;
        }

        workArea.width = canvasW;
        workArea.height = canvasH;
        return workArea;
    }

    float minX = polygon.front().x;
    float minY = polygon.front().y;
    float maxX = polygon.front().x;
    float maxY = polygon.front().y;
    for (const aether::Vector2& point : polygon) {
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }

    const int originX = static_cast<int>(std::floor(minX));
    const int originY = static_cast<int>(std::floor(minY));
    const int maxPixelX = static_cast<int>(std::ceil(maxX));
    const int maxPixelY = static_cast<int>(std::ceil(maxY));
    const int width = std::max(1, maxPixelX - originX + 1);
    const int height = std::max(1, maxPixelY - originY + 1);

    workArea.originX = originX;
    workArea.originY = originY;
    workArea.width = width;
    workArea.height = height;
    workArea.polygon.reserve(polygon.size());
    for (const aether::Vector2& point : polygon) {
        workArea.polygon.push_back(
            { point.x - static_cast<float>(originX), point.y - static_cast<float>(originY) });
    }

    return workArea;
}

std::vector<aether::Vector2> buildClosedPolygon(const std::vector<aether::Vector2>& points)
{
    if (points.size() < 3) {
        return {};
    }

    std::vector<aether::Vector2> closed = points;
    const aether::Vector2& first = closed.front();
    const aether::Vector2& last = closed.back();
    const float dx = first.x - last.x;
    const float dy = first.y - last.y;
    if ((dx * dx + dy * dy) > 0.01f) {
        closed.push_back(first);
    }
    return closed;
}

std::vector<aether::TileKey> collectVisibleDirtyKeys(const aether::Viewport& viewport,
    const aether::CompositionCache& cache, float canvasWidth, float canvasHeight, bool flipH,
    bool flipV)
{
    const auto& dirtyPositions = cache.dirtyPositions();
    std::vector<aether::TileKey> keys;
    if (dirtyPositions.empty()) {
        return keys;
    }

    const aether::VisibleTileKeyBounds visibleBounds
        = aether::visibleTileKeyBounds(viewport, canvasWidth, canvasHeight, flipH, flipV);

    keys.reserve(dirtyPositions.size());
    for (const aether::TileKey& key : dirtyPositions) {
        if (!aether::isTileKeyVisible(key, visibleBounds)) {
            continue;
        }
        keys.push_back(key);
    }
    return keys;
}

// Builds the fragment->document affine (EffectRegionFrame) of a viewport
// preview's screen texture, so positional / distortion layer effects render live
// there. Derives the mapping from the proven camera transform (screenToWorld)
// rather than rebuilding matrices: the three UV corners are mapped to document
// pixels, giving an affine that carries rotation / zoom / pan for free. GL
// framebuffer v=1 is the TOP of the screen (the projection flips Y), so a UV maps
// to the Qt (top-left, Y-down) pixel below. Content-flip (flipH/flipV) is a model
// mirror about the canvas applied before the camera, so it is inverted here to
// recover the true document coordinate; without finite canvas bounds it cannot be
// inverted, so the frame is left invalid (effects then pass through) when a flip
// is active but bounds are unknown.
ruwa::core::effects::EffectRegionFrame buildViewportEffectRegion(const aether::Viewport& viewport,
    float canvasWidth, float canvasHeight, bool flipH, bool flipV, uint32_t viewportWidth,
    uint32_t viewportHeight)
{
    ruwa::core::effects::EffectRegionFrame frame;
    if (viewportWidth == 0 || viewportHeight == 0) {
        return frame;
    }
    if ((flipH && canvasWidth <= 0.0f) || (flipV && canvasHeight <= 0.0f)) {
        return frame;
    }

    const float w = static_cast<float>(viewportWidth);
    const float h = static_cast<float>(viewportHeight);
    auto uvToDoc = [&](float u, float v) -> aether::Vector2 {
        const aether::Vector2 screenPx { u * w, (1.0f - v) * h };
        aether::Vector2 world = viewport.screenToWorld(screenPx);
        if (flipH) {
            world.x = canvasWidth - world.x;
        }
        if (flipV) {
            world.y = canvasHeight - world.y;
        }
        return world;
    };

    const aether::Vector2 o = uvToDoc(0.0f, 0.0f);
    const aether::Vector2 ux = uvToDoc(1.0f, 0.0f);
    const aether::Vector2 uy = uvToDoc(0.0f, 1.0f);
    frame.valid = true;
    frame.useAffine = true;
    frame.originX = o.x;
    frame.originY = o.y;
    frame.basisXx = ux.x - o.x;
    frame.basisXy = ux.y - o.y;
    frame.basisYx = uy.x - o.x;
    frame.basisYy = uy.y - o.y;
    return frame;
}

// Renders a raster layer's RAW screen source (pre-effect, matching the normal
// viewport source resolvers) at the viewport enlarged by (padX, padY) on every
// side, and pairs it with the fragment->document affine of that enlarged texture.
// Feeds GLViewportCompositor's distortion-reach path so a twirl / pinch / ripple
// can sample content that falls outside the visible viewport when zoomed in,
// without leaving screen space (the source is viewport+2*pad, not the whole layer
// in document resolution). Same camera as the base viewport, so the enlarged
// surface is centre-anchored and the compositor's centre crop realigns with the
// normal viewport source. Returns a zero texture to decline (groups, or a failed
// render) — the compositor then keeps the plain viewport-sized effect path.
aether::GLViewportCompositor::OverscanLayerSource resolveOverscanRasterSource(
    const aether::CompositeLayerInfo& info, int padX, int padY,
    aether::LayerScreenSourceCache& cache, aether::GLRenderer& renderer,
    const aether::Viewport& baseViewport, uint32_t viewportWidth, uint32_t viewportHeight,
    uint32_t canvasWidth, uint32_t canvasHeight, bool flipH, bool flipV, uint64_t viewportRevision,
    aether::LayerScreenSourceCache::SourceKind sourceKind
    = aether::LayerScreenSourceCache::SourceKind::LayerColor)
{
    aether::GLViewportCompositor::OverscanLayerSource out;
    if (info.isGroup || padX <= 0 || padY <= 0 || viewportWidth == 0 || viewportHeight == 0) {
        return out;
    }
    const uint32_t overscanWidth = viewportWidth + static_cast<uint32_t>(padX) * 2u;
    const uint32_t overscanHeight = viewportHeight + static_cast<uint32_t>(padY) * 2u;

    aether::Viewport overscanViewport = baseViewport;
    overscanViewport.resize(overscanWidth, overscanHeight);

    const GLuint texture = cache.acquireLayerTexture(info, renderer, overscanViewport, canvasWidth,
        canvasHeight, flipH, flipV, viewportRevision, sourceKind,
        ruwa::core::effects::LayerSourcePurpose::RawContent);
    if (!texture) {
        return out;
    }
    out.texture = texture;
    out.region = buildViewportEffectRegion(overscanViewport, static_cast<float>(canvasWidth),
        static_cast<float>(canvasHeight), flipH, flipV, overscanWidth, overscanHeight);
    return out;
}

// Bounds-expanding layer effects (blur/shadow) make an edit in one tile affect
// the composited result of surrounding tiles. Grow a set of edited tile keys by
// the largest neighbourhood ring any layer's effect chain needs, so those
// neighbour composite tiles are recomposited too. Cheap no-op when no layer has
// such an effect.
std::unordered_set<aether::TileKey, aether::TileKeyHash> expandDirtyKeysByLayerEffects(
    const ruwa::core::layers::LayerModel* model, const std::vector<aether::TileKey>& keys)
{
    std::unordered_set<aether::TileKey, aether::TileKeyHash> result(keys.begin(), keys.end());
    if (!model || keys.empty()) {
        return result;
    }

    int maxRing = 0;
    model->forEach([&](ruwa::core::layers::LayerData* layer) {
        if (!layer || layer->effects.isEmpty()) {
            return;
        }
        const int pad
            = ruwa::core::effects::EffectCoverageResolver::neighborhoodPadPixels(layer->effects);
        if (pad > 0) {
            const int ring = (pad + static_cast<int>(aether::TILE_SIZE) - 1)
                / static_cast<int>(aether::TILE_SIZE);
            maxRing = std::max(maxRing, ring);
        }
    });
    if (maxRing <= 0) {
        return result;
    }

    for (const aether::TileKey& key : keys) {
        for (int dy = -maxRing; dy <= maxRing; ++dy) {
            for (int dx = -maxRing; dx <= maxRing; ++dx) {
                result.insert(aether::TileKey { key.x + dx, key.y + dy });
            }
        }
    }
    return result;
}

std::unordered_set<aether::TileKey, aether::TileKeyHash> expandLayerCoverageByEffects(
    const ruwa::core::layers::LayerData* layer,
    const std::unordered_set<aether::TileKey, aether::TileKeyHash>& keys)
{
    if (!layer || keys.empty() || layer->effects.isEmpty()) {
        return keys;
    }
    return ruwa::core::effects::EffectCoverageResolver::expandedDocumentCoverage(
        keys, layer->effects);
}

void insertLayerEffectExpandedCoverage(const ruwa::core::layers::LayerData* layer,
    const std::unordered_set<aether::TileKey, aether::TileKeyHash>& keys,
    std::unordered_set<aether::TileKey, aether::TileKeyHash>& outKeys)
{
    const auto expanded = expandLayerCoverageByEffects(layer, keys);
    outKeys.insert(expanded.begin(), expanded.end());
}

std::vector<aether::TileKey> collectVisibleUncachedKeys(
    const std::vector<aether::CompositeLayerInfo>& layers, const aether::Viewport& viewport,
    const aether::CompositionCache& cache, float canvasWidth, float canvasHeight, bool flipH,
    bool flipV)
{
    std::unordered_set<aether::TileKey, aether::TileKeyHash> layerKeys;
    aether::collectVisibleCompositeLayerKeys(layers, layerKeys);

    std::vector<aether::TileKey> keys;
    if (layerKeys.empty()) {
        return keys;
    }

    const aether::VisibleTileKeyBounds visibleBounds
        = aether::visibleTileKeyBounds(viewport, canvasWidth, canvasHeight, flipH, flipV);
    keys.reserve(layerKeys.size());
    for (const aether::TileKey& key : layerKeys) {
        if (!aether::isTileKeyVisible(key, visibleBounds) || cache.grid().hasTile(key)) {
            continue;
        }
        keys.push_back(key);
    }
    return keys;
}

aether::Rect unionRects(const aether::Rect& a, const aether::Rect& b)
{
    if (!aether::rectHasArea(a)) {
        return b;
    }
    if (!aether::rectHasArea(b)) {
        return a;
    }

    const float minX = std::min(a.x, b.x);
    const float minY = std::min(a.y, b.y);
    const float maxX = std::max(a.x + a.width, b.x + b.width);
    const float maxY = std::max(a.y + a.height, b.y + b.height);
    return aether::Rect { minX, minY, maxX - minX, maxY - minY };
}

aether::Rect intersectRects(const aether::Rect& a, const aether::Rect& b)
{
    if (!aether::rectHasArea(a) || !aether::rectHasArea(b) || !a.intersects(b)) {
        return {};
    }

    const float minX = std::max(a.x, b.x);
    const float minY = std::max(a.y, b.y);
    const float maxX = std::min(a.x + a.width, b.x + b.width);
    const float maxY = std::min(a.y + a.height, b.y + b.height);
    if (maxX <= minX || maxY <= minY) {
        return {};
    }
    return aether::Rect { minX, minY, maxX - minX, maxY - minY };
}

aether::Rect incrementalLassoFillDirtyBounds(const std::vector<aether::Vector2>& previousPolygon,
    const std::vector<aether::Vector2>& currentPolygon)
{
    if (previousPolygon.size() < 4 || currentPolygon.size() < 4) {
        return {};
    }
    if (currentPolygon.size() != previousPolygon.size() + 1) {
        return {};
    }

    const aether::Vector2& previousAnchor = previousPolygon.front();
    const aether::Vector2& currentAnchor = currentPolygon.front();
    if (!aether::nearlyEqualPoint(previousAnchor, currentAnchor)
        || !aether::nearlyEqualPoint(previousPolygon.back(), previousAnchor)
        || !aether::nearlyEqualPoint(currentPolygon.back(), currentAnchor)) {
        return {};
    }

    const aether::Vector2& previousTip = previousPolygon[previousPolygon.size() - 2];
    const aether::Vector2& currentTip = currentPolygon[currentPolygon.size() - 2];
    if (aether::nearlyEqualPoint(previousTip, currentTip)) {
        return {};
    }

    return aether::retainedPolygonBounds({ currentAnchor, previousTip, currentTip });
}

aether::Rect visibleWorldBounds(
    const aether::Viewport& viewport, float canvasWidth, float canvasHeight, bool flipH, bool flipV)
{
    const auto& camera = viewport.camera();
    const aether::Vector2 viewportSize = viewport.size();
    const aether::Vector2 p0
        = aether::mirrorWorldInCanvas(camera.screenToWorld({ 0.0f, 0.0f }, viewportSize),
            canvasWidth, canvasHeight, flipH, flipV);
    const aether::Vector2 p1
        = aether::mirrorWorldInCanvas(camera.screenToWorld({ viewportSize.x, 0.0f }, viewportSize),
            canvasWidth, canvasHeight, flipH, flipV);
    const aether::Vector2 p2
        = aether::mirrorWorldInCanvas(camera.screenToWorld({ 0.0f, viewportSize.y }, viewportSize),
            canvasWidth, canvasHeight, flipH, flipV);
    const aether::Vector2 p3 = aether::mirrorWorldInCanvas(
        camera.screenToWorld({ viewportSize.x, viewportSize.y }, viewportSize), canvasWidth,
        canvasHeight, flipH, flipV);

    const float minX = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
    const float minY = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
    const float maxX = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
    const float maxY = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));
    return aether::Rect { minX, minY, maxX - minX, maxY - minY };
}

std::vector<aether::TileKey> tileKeysForRect(const aether::Rect& rect)
{
    std::vector<aether::TileKey> keys;
    if (!aether::rectHasArea(rect)) {
        return keys;
    }

    const float maxX = rect.x + rect.width;
    const float maxY = rect.y + rect.height;
    const aether::TileKey minKey = aether::worldToTile(rect.x, rect.y);
    const aether::TileKey maxKey
        = aether::worldToTile(std::nextafter(maxX, rect.x), std::nextafter(maxY, rect.y));
    keys.reserve(static_cast<size_t>(std::max(0, maxKey.x - minKey.x + 1))
        * static_cast<size_t>(std::max(0, maxKey.y - minKey.y + 1)));
    for (int32_t y = minKey.y; y <= maxKey.y; ++y) {
        for (int32_t x = minKey.x; x <= maxKey.x; ++x) {
            keys.push_back(aether::TileKey { x, y });
        }
    }
    return keys;
}

std::vector<aether::TileKey> collectVisibleKeysForBounds(const aether::Rect& bounds,
    const aether::Viewport& viewport, float canvasWidth, float canvasHeight, bool flipH, bool flipV)
{
    const aether::Rect visibleBounds
        = visibleWorldBounds(viewport, canvasWidth, canvasHeight, flipH, flipV);
    return tileKeysForRect(intersectRects(bounds, visibleBounds));
}

aether::FloodFillResult::RawTileMap extractRawTilesRegion(
    const aether::FloodFillResult::RawTileMap& sourceTiles, int offsetX, int offsetY, int width,
    int height, bool alphaOnly = false, aether::TilePixelFormat fmt = aether::kDefaultTileFormat)
{
    aether::FloodFillResult::RawTileMap regionTiles;
    if (width <= 0 || height <= 0) {
        return regionTiles;
    }

    for (const auto& [key, tile] : sourceTiles) {
        if (tile.size() != aether::tileByteSize(fmt)) {
            continue;
        }

        const int baseX = key.x * static_cast<int>(aether::TILE_SIZE);
        const int baseY = key.y * static_cast<int>(aether::TILE_SIZE);
        for (uint32_t localY = 0; localY < aether::TILE_SIZE; ++localY) {
            const int srcY = baseY + static_cast<int>(localY);
            if (srcY < offsetY || srcY >= offsetY + height) {
                continue;
            }

            const int dstY = srcY - offsetY;
            for (uint32_t localX = 0; localX < aether::TILE_SIZE; ++localX) {
                const int srcX = baseX + static_cast<int>(localX);
                if (srcX < offsetX || srcX >= offsetX + width) {
                    continue;
                }

                float f[4];
                aether::readTilePixelF(tile.data(), fmt, localX, localY, f);
                const uint8_t r = aether::fillQuantizeChannel(f[0]);
                const uint8_t g = aether::fillQuantizeChannel(f[1]);
                const uint8_t b = aether::fillQuantizeChannel(f[2]);
                const uint8_t a = aether::fillQuantizeChannel(f[3]);
                if (alphaOnly) {
                    if (a == 0) {
                        continue;
                    }
                } else if (r == 0 && g == 0 && b == 0 && a == 0) {
                    continue;
                }

                const int dstX = srcX - offsetX;
                const aether::TileKey dstKey { dstX / static_cast<int>(aether::TILE_SIZE),
                    dstY / static_cast<int>(aether::TILE_SIZE) };
                const uint32_t dstLocalX
                    = static_cast<uint32_t>(dstX % static_cast<int>(aether::TILE_SIZE));
                const uint32_t dstLocalY
                    = static_cast<uint32_t>(dstY % static_cast<int>(aether::TILE_SIZE));
                std::vector<uint8_t>& dstTile = aether::ensureRawTile(regionTiles, dstKey, fmt);
                aether::setRawPixel(dstTile, dstLocalX, dstLocalY, r, g, b, a, fmt);
            }
        }
    }

    return regionTiles;
}

template <typename RawTileMap>
RawTileMap snapshotRawTilesRegion(const aether::TileGrid& grid, int offsetX, int offsetY, int width,
    int height, bool alphaOnly = false)
{
    RawTileMap tiles;
    if (width <= 0 || height <= 0) {
        return tiles;
    }

    using SnapshotEntry = std::pair<aether::TileKey, const aether::TileData*>;
    std::vector<SnapshotEntry> entries;
    entries.reserve(grid.tiles().size());

    const int regionMaxX = offsetX + width;
    const int regionMaxY = offsetY + height;
    for (const auto& [key, tile] : grid.tiles()) {
        const int tileMinX = key.x * static_cast<int>(aether::TILE_SIZE);
        const int tileMinY = key.y * static_cast<int>(aether::TILE_SIZE);
        const int tileMaxX = tileMinX + static_cast<int>(aether::TILE_SIZE);
        const int tileMaxY = tileMinY + static_cast<int>(aether::TILE_SIZE);
        if (tileMaxX <= offsetX || tileMaxY <= offsetY || tileMinX >= regionMaxX
            || tileMinY >= regionMaxY) {
            continue;
        }

        entries.emplace_back(key, &tile);
    }

    if (entries.empty()) {
        return tiles;
    }

    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    const size_t workerCount = std::min(entries.size(),
        hardwareThreads > 1 ? static_cast<size_t>(hardwareThreads - 1) : static_cast<size_t>(1));

    if (workerCount <= 1 || entries.size() < workerCount * 4) {
        for (const auto& [key, tile] : entries) {
            const int tileMinX = key.x * static_cast<int>(aether::TILE_SIZE);
            const int tileMinY = key.y * static_cast<int>(aether::TILE_SIZE);
            const int srcMinX = std::max(tileMinX, offsetX);
            const int srcMinY = std::max(tileMinY, offsetY);
            const int srcMaxX
                = std::min(tileMinX + static_cast<int>(aether::TILE_SIZE), regionMaxX);
            const int srcMaxY
                = std::min(tileMinY + static_cast<int>(aether::TILE_SIZE), regionMaxY);
            const uint8_t* pixels = tile->pixels();

            for (int srcY = srcMinY; srcY < srcMaxY; ++srcY) {
                const uint32_t localY = static_cast<uint32_t>(srcY - tileMinY);
                const int dstY = srcY - offsetY;
                for (int srcX = srcMinX; srcX < srcMaxX; ++srcX) {
                    const uint32_t localX = static_cast<uint32_t>(srcX - tileMinX);
                    const uint32_t idx = aether::rawPixelIndex(localX, localY);
                    const uint8_t r = pixels[idx + 0];
                    const uint8_t g = pixels[idx + 1];
                    const uint8_t b = pixels[idx + 2];
                    const uint8_t a = pixels[idx + 3];
                    if (alphaOnly) {
                        if (a == 0) {
                            continue;
                        }
                    } else if (r == 0 && g == 0 && b == 0 && a == 0) {
                        continue;
                    }

                    const int dstX = srcX - offsetX;
                    const aether::TileKey dstKey { dstX / static_cast<int>(aether::TILE_SIZE),
                        dstY / static_cast<int>(aether::TILE_SIZE) };
                    const uint32_t dstLocalX
                        = static_cast<uint32_t>(dstX % static_cast<int>(aether::TILE_SIZE));
                    const uint32_t dstLocalY
                        = static_cast<uint32_t>(dstY % static_cast<int>(aether::TILE_SIZE));
                    std::vector<uint8_t>& dstTile = aether::ensureRawTile(tiles, dstKey);
                    aether::setRawPixel(dstTile, dstLocalX, dstLocalY, r, g, b, a);
                }
            }
        }

        return tiles;
    }

    std::vector<RawTileMap> localMaps(workerCount);
    std::vector<std::thread> workers;
    workers.reserve(workerCount - 1);
    const size_t chunkSize = (entries.size() + workerCount - 1) / workerCount;

    const auto snapshotChunk = [&](size_t workerIndex, size_t begin, size_t end) {
        auto& local = localMaps[workerIndex];
        local.reserve(end - begin);
        for (size_t i = begin; i < end; ++i) {
            const auto& [key, tile] = entries[i];
            const int tileMinX = key.x * static_cast<int>(aether::TILE_SIZE);
            const int tileMinY = key.y * static_cast<int>(aether::TILE_SIZE);
            const int srcMinX = std::max(tileMinX, offsetX);
            const int srcMinY = std::max(tileMinY, offsetY);
            const int srcMaxX
                = std::min(tileMinX + static_cast<int>(aether::TILE_SIZE), regionMaxX);
            const int srcMaxY
                = std::min(tileMinY + static_cast<int>(aether::TILE_SIZE), regionMaxY);
            const uint8_t* pixels = tile->pixels();

            for (int srcY = srcMinY; srcY < srcMaxY; ++srcY) {
                const uint32_t localY = static_cast<uint32_t>(srcY - tileMinY);
                const int dstY = srcY - offsetY;
                for (int srcX = srcMinX; srcX < srcMaxX; ++srcX) {
                    const uint32_t localX = static_cast<uint32_t>(srcX - tileMinX);
                    const uint32_t idx = aether::rawPixelIndex(localX, localY);
                    const uint8_t r = pixels[idx + 0];
                    const uint8_t g = pixels[idx + 1];
                    const uint8_t b = pixels[idx + 2];
                    const uint8_t a = pixels[idx + 3];
                    if (alphaOnly) {
                        if (a == 0) {
                            continue;
                        }
                    } else if (r == 0 && g == 0 && b == 0 && a == 0) {
                        continue;
                    }

                    const int dstX = srcX - offsetX;
                    const aether::TileKey dstKey { dstX / static_cast<int>(aether::TILE_SIZE),
                        dstY / static_cast<int>(aether::TILE_SIZE) };
                    const uint32_t dstLocalX
                        = static_cast<uint32_t>(dstX % static_cast<int>(aether::TILE_SIZE));
                    const uint32_t dstLocalY
                        = static_cast<uint32_t>(dstY % static_cast<int>(aether::TILE_SIZE));
                    std::vector<uint8_t>& dstTile = aether::ensureRawTile(local, dstKey);
                    aether::setRawPixel(dstTile, dstLocalX, dstLocalY, r, g, b, a);
                }
            }
        }
    };

    for (size_t workerIndex = 1; workerIndex < workerCount; ++workerIndex) {
        const size_t begin = workerIndex * chunkSize;
        const size_t end = std::min(begin + chunkSize, entries.size());
        if (begin >= end) {
            break;
        }

        workers.emplace_back(snapshotChunk, workerIndex, begin, end);
    }

    snapshotChunk(0, 0, std::min(chunkSize, entries.size()));

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    for (auto& local : localMaps) {
        for (auto& [key, raw] : local) {
            tiles.emplace(key, std::move(raw));
        }
    }

    return tiles;
}

uint64_t progressiveSeedKey(int x, int y)
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 32) | static_cast<uint32_t>(x);
}

int progressiveTileRadius(int x, int y, int seedTileX, int seedTileY)
{
    const int tileX = x / static_cast<int>(aether::TILE_SIZE);
    const int tileY = y / static_cast<int>(aether::TILE_SIZE);
    return std::max(std::abs(tileX - seedTileX), std::abs(tileY - seedTileY));
}

aether::Vector2 midpoint(const aether::Vector2& a, const aether::Vector2& b)
{
    return { (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f };
}

aether::Vector2 quadraticPoint(const aether::Vector2& start, const aether::Vector2& control,
    const aether::Vector2& end, float t)
{
    const float u = 1.0f - t;
    const float uu = u * u;
    const float tt = t * t;
    return { uu * start.x + 2.0f * u * t * control.x + tt * end.x,
        uu * start.y + 2.0f * u * t * control.y + tt * end.y };
}

} // namespace
#ifndef SHADER_DIR
#define SHADER_DIR "shaders"
#endif

namespace aether {

namespace {
std::shared_ptr<TileGrid> cloneTileGrid(const TileGrid* source)
{
    if (!source) {
        return nullptr;
    }

    auto cloned = std::make_shared<TileGrid>();
    // Preserve the source content format (may be 16F/32F under per-document
    // format) so tiles are not truncated to a 256 KB RGBA8 slice.
    cloned->setFormat(source->format());
    const size_t tileBytes = aether::tileByteSize(source->format());
    for (const auto& [key, tile] : source->tiles()) {
        TileData& dst = cloned->getOrCreateTile(key);
        std::memcpy(dst.pixels(), tile.pixels(), tileBytes);
        dst.markDirty();
        cloned->markDirty(key);
    }
    return cloned;
}

bool layerRequiresRasterizationForPixelEdits(const ruwa::core::layers::LayerData* layer)
{
    return layer && (layer->isIsolatedPixelLayer() || layer->isText());
}

std::unique_ptr<TileGrid> rasterizeTextLayerToGrid(ruwa::core::layers::LayerData* layer)
{
    auto rasterGrid = std::make_unique<TileGrid>();
    // The retained-text renderer produces 8-bit premultiplied QImage tiles and
    // the copies below write raw RGBA8 bytes, so pin the raster grid to RGBA8
    // (a deliberate fixed format). Otherwise it would default to
    // kDefaultTileFormat and the 8-bit writes would corrupt a 16F/32F buffer.
    rasterGrid->setFormat(aether::TilePixelFormat::RGBA8);
    if (!layer || !layer->isText() || !ensureTextRetainedPayload(layer)
        || !layer->runtimeRetainedPayload) {
        return rasterGrid;
    }

    const auto tileKeys = retainedCoverageTileKeys(layer->runtimeRetainedPayload->worldBounds);
    for (const TileKey& key : tileKeys) {
        QImage tileImage
            = GLRetainedRenderer::renderPayloadTileImage(*layer->runtimeRetainedPayload, key);
        if (tileImage.isNull()) {
            continue;
        }
        if (tileImage.format() != QImage::Format_RGBA8888_Premultiplied) {
            tileImage = tileImage.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        }

        TileData& tile = rasterGrid->getOrCreateTile(key);
        if (tileImage.bytesPerLine() == static_cast<int>(TILE_SIZE * TILE_CHANNELS)) {
            std::memcpy(tile.pixels(), tileImage.constBits(), TILE_BYTE_SIZE);
        } else {
            auto* dst = tile.pixels();
            const int rowBytes = static_cast<int>(TILE_SIZE * TILE_CHANNELS);
            for (int y = 0; y < static_cast<int>(TILE_SIZE); ++y) {
                std::memcpy(dst + y * rowBytes, tileImage.constScanLine(y), rowBytes);
            }
        }
        tile.markDirty();
        rasterGrid->markDirty(key);
    }
    return rasterGrid;
}

QString isolatedLayerKindLabel(const ruwa::core::layers::LayerData* layer)
{
    if (!layer) {
        return QObject::tr("Imported object");
    }
    if (layer->isBoard()) {
        return QObject::tr("Board layer");
    }
    if (layer->isSmart()) {
        return QObject::tr("Smart object");
    }
    if (layer->isText()) {
        return QObject::tr("Text layer");
    }
    return QObject::tr("Imported object");
}

QString isolatedLayerConvertTitle(const ruwa::core::layers::LayerData* layer)
{
    if (layer && layer->isBoard()) {
        return QObject::tr("Convert Board Layer");
    }
    if (layer && layer->isText()) {
        return QObject::tr("Convert Text Layer");
    }
    return QObject::tr("Convert Smart Object");
}

} // namespace

class FillWorker final : public QObject {
public:
    using RawTileMap = FloodFillResult::RawTileMap;

    struct Request {
        uint64_t sequence = 0;
        QUuid layerId;
        OpenGLCanvasWidget::FillAlgorithm algorithm = OpenGLCanvasWidget::FillAlgorithm::Smart;
        SelectionRestoreContext selectionRestore {};
        RawTileMap layerSnapshotTiles;
        RawTileMap selectionMaskTiles;
        FillOrigin origin;
        FillColor color;
        FillCanvasBounds canvasBounds;
        std::shared_ptr<std::atomic<bool>> cancelState;
    };

    FillWorker(QOpenGLContext* shareContext, QOffscreenSurface* surface, QString shaderDir,
        OpenGLCanvasWidget* owner)
        : m_shareContext(shareContext)
        , m_surface(surface)
        , m_shaderDir(std::move(shaderDir))
        , m_owner(owner)
    {
    }

    ~FillWorker() override { shutdownGl(); }

    void warmUp(int canvasW, int canvasH)
    {
        Q_UNUSED(canvasW);
        Q_UNUSED(canvasH);
    }

    void process(const std::shared_ptr<Request>& request)
    {
        if (!request || isCancelled(*request)) {
            return;
        }

        QElapsedTimer fillTimer;
        fillTimer.start();

        // Keep fill computation off the UI thread, but avoid the shared-GL
        // preview path here. Legacy project layers can destabilize the shared
        // OpenGL context; the CPU fill remains the authoritative, stable path.
        FloodFillResult result = request->algorithm == OpenGLCanvasWidget::FillAlgorithm::Classic
            ? classicFloodFillRawTiles(request->layerSnapshotTiles, request->origin.x,
                  request->origin.y, request->color.r, request->color.g, request->color.b,
                  request->color.a, request->selectionMaskTiles, request->canvasBounds.width,
                  request->canvasBounds.height)
            : floodFillRawTiles(request->layerSnapshotTiles, request->origin.x, request->origin.y,
                  request->color.r, request->color.g, request->color.b, request->color.a,
                  request->selectionMaskTiles, request->canvasBounds.width,
                  request->canvasBounds.height);

        if (isCancelled(*request)) {
            return;
        }

        QPointer<OpenGLCanvasWidget> owner(m_owner);
        if (!owner) {
            return;
        }

        QMetaObject::invokeMethod(
            owner.data(),
            [owner, request, result = std::move(result)]() mutable {
                if (!owner) {
                    return;
                }

                owner->handleFillWorkerResult(request->sequence, request->layerId,
                    std::move(request->selectionRestore), std::move(result), request->origin,
                    request->color, request->canvasBounds);
            },
            Qt::QueuedConnection);
    }

private:
    bool isCancelled(const Request& request) const
    {
        return request.cancelState && request.cancelState->load(std::memory_order_acquire);
    }

    bool ensureGlReady()
    {
        if (!m_surface || !m_shareContext) {
            return false;
        }

        if (!m_context) {
            m_context = std::make_unique<QOpenGLContext>();
            m_context->setFormat(m_shareContext->format());
            m_context->setShareContext(m_shareContext);
            if (!m_context->create()) {
                m_context.reset();
                return false;
            }
        }

        if (!m_context->makeCurrent(m_surface)) {
            return false;
        }

        if (!m_gl) {
            m_gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_5_Core>(m_context.get());
            if (!m_gl) {
                m_context->doneCurrent();
                return false;
            }
        }

        if (!m_fillRenderer) {
            m_tileRenderer = std::make_unique<GLTileRenderer>(m_gl);
            m_fillRenderer = std::make_unique<GLFillRenderer>(m_gl);
            auto initResult = m_fillRenderer->initialize(m_shaderDir);
            if (!initResult || !m_fillRenderer->isInitialized()) {
                m_fillRenderer.reset();
                m_tileRenderer.reset();
                m_context->doneCurrent();
                return false;
            }
        }

        return true;
    }

    void shutdownGl()
    {
        if (m_context && m_surface && m_context->makeCurrent(m_surface)) {
            if (m_fillRenderer) {
                m_fillRenderer->shutdown();
            }
            m_fillRenderer.reset();
            m_tileRenderer.reset();
            m_context->doneCurrent();
        } else {
            m_fillRenderer.reset();
            m_tileRenderer.reset();
        }

        m_gl = nullptr;
        m_context.reset();
    }

    void doneCurrentIfNeeded()
    {
        if (m_context && QOpenGLContext::currentContext() == m_context.get()) {
            m_context->doneCurrent();
        }
    }

    QOpenGLContext* m_shareContext = nullptr;
    QOffscreenSurface* m_surface = nullptr;
    QString m_shaderDir;
    OpenGLCanvasWidget* m_owner = nullptr;
    std::unique_ptr<QOpenGLContext> m_context;
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    std::unique_ptr<GLTileRenderer> m_tileRenderer;
    std::unique_ptr<GLFillRenderer> m_fillRenderer;
};

// ==========================================================================
//   C A N V A S   C O N T E N T   M I R R O R   ( D I S P L A Y   O N L Y )
// ==========================================================================

void OpenGLCanvasWidget::setCanvasContentFlipHorizontal(bool flip)
{
    m_canvasContentFlipHorizontal = flip;
    requestRender();
}

void OpenGLCanvasWidget::setCanvasContentFlipVertical(bool flip)
{
    m_canvasContentFlipVertical = flip;
    requestRender();
}

void OpenGLCanvasWidget::toggleCanvasContentFlipHorizontal()
{
    m_canvasContentFlipHorizontal = !m_canvasContentFlipHorizontal;
    requestRender();
}

void OpenGLCanvasWidget::toggleCanvasContentFlipVertical()
{
    m_canvasContentFlipVertical = !m_canvasContentFlipVertical;
    requestRender();
}

bool OpenGLCanvasWidget::effectiveContentFlipH() const
{
    return m_canvasContentFlipHorizontal && !m_exportPreviewSuppressContentMirror;
}

bool OpenGLCanvasWidget::effectiveContentFlipV() const
{
    return m_canvasContentFlipVertical && !m_exportPreviewSuppressContentMirror;
}

void OpenGLCanvasWidget::setExportPreviewSuppressContentMirror(bool suppress)
{
    if (m_exportPreviewSuppressContentMirror == suppress) {
        return;
    }
    m_exportPreviewSuppressContentMirror = suppress;
    requestRender();
}

void OpenGLCanvasWidget::setExportPreviewHideBoardLayers(bool hide)
{
    if (m_exportPreviewHideBoardLayers == hide) {
        return;
    }
    m_exportPreviewHideBoardLayers = hide;
    requestRender();
}

Vector2 OpenGLCanvasWidget::documentWorldFromScreen(const Vector2& screenPx) const
{
    Vector2 w = m_viewport.screenToWorld(screenPx);
    const float cw = static_cast<float>(m_canvas.width());
    const float ch = static_cast<float>(m_canvas.height());
    if (cw <= 0.0f || ch <= 0.0f) {
        return w;
    }
    return mirrorWorldInCanvas(w, cw, ch, effectiveContentFlipH(), effectiveContentFlipV());
}

Vector2 OpenGLCanvasWidget::screenFromDocumentWorld(const Vector2& documentWorld) const
{
    const float cw = static_cast<float>(m_canvas.width());
    const float ch = static_cast<float>(m_canvas.height());
    Vector2 w = documentWorld;
    if (cw > 0.0f && ch > 0.0f) {
        w = mirrorWorldInCanvas(
            documentWorld, cw, ch, effectiveContentFlipH(), effectiveContentFlipV());
    }
    return m_viewport.worldToScreen(w);
}

std::array<float, 16> OpenGLCanvasWidget::canvasContentViewProjectionMatrix() const
{
    std::array<float, 16> vp = m_viewport.viewProjectionMatrix();
    const float cw = static_cast<float>(m_canvas.width());
    const float ch = static_cast<float>(m_canvas.height());
    if ((!effectiveContentFlipH() && !effectiveContentFlipV()) || cw <= 0.0f || ch <= 0.0f) {
        return vp;
    }
    const auto m
        = canvasContentMirrorMatrix4(cw, ch, effectiveContentFlipH(), effectiveContentFlipV());
    return multiplyMat4ColMajor(vp, m);
}

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

OpenGLCanvasWidget::OpenGLCanvasWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , m_canvas(1920, 1080)
{
    QSurfaceFormat format;
    format.setVersion(4, 5);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(1);
    format.setSamples(0); // Disable MSAA â€” canvas uses tile-based rendering, not geometry
    format.setAlphaBufferSize(0); // Prevent desktop bleed-through in frameless windows
    setFormat(format);

    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    m_brushSession = createDefaultBrushSession();
    m_brush = pixelBrushFromSession(m_brushSession.get());
    if (!m_brush) {
        m_brushSession = std::make_unique<ruwa::core::brushes::PixelBrushSession>(
            ruwa::core::brushes::BrushSessionConfig {});
        m_brush = pixelBrushFromSession(m_brushSession.get());
    }

    // Default brush
    m_brush->setColor(255, 255, 255, 255);
    m_brush->setRadius(8.0f);
    m_brush->setHardness(0.7f);

    // Repaint after undo/redo
    connect(&m_canvas.undoManager(), &UndoManager::indexChanged, this, [this]() {
        cancelFillPreview();
        requestRender();
    });
    connect(&m_canvas.undoManager(), &UndoManager::commandApplied, this,
        [this](const QList<QPoint>& tilePositions) {
            if (tilePositions.isEmpty()) {
                invalidateBoardCompositionCache();
                return;
            }

            std::vector<TileKey> affectedKeys;
            affectedKeys.reserve(static_cast<size_t>(tilePositions.size()));
            for (const QPoint& pos : tilePositions) {
                affectedKeys.push_back(TileKey { pos.x(), pos.y() });
            }

            // Undo/redo can restore or remove tiles without going through the
            // usual immediate-edit path, so refresh both composition caches
            // from the final affected set reported by the command itself.
            m_canvas.compositionCache().markDirty(
                expandDirtyKeysByLayerEffects(m_layerModel, affectedKeys));
            invalidateBoardCompositionCache();
        });

    m_stabilizerCatchupTimer.setSingleShot(false);
    m_stabilizerCatchupTimer.setInterval(8);
    m_stabilizerCatchupTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_stabilizerCatchupTimer, &QTimer::timeout, this,
        &OpenGLCanvasWidget::processStabilizerCatchup);

    m_canvasCornerEffectTimer.setSingleShot(true);
    m_canvasCornerEffectTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_canvasCornerEffectTimer, &QTimer::timeout, this, [this]() { requestRender(); });
    m_canvasCornerEffectClock.start();

    m_cameraAnimationFrameTimer.setSingleShot(true);
    m_cameraAnimationFrameTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_cameraAnimationFrameTimer, &QTimer::timeout, this, [this]() { update(); });

    // Layer compositing builder (must be created before selection controller)
    LayerCompositingContext layerCtx;
    layerCtx.getActiveLayer = [this]() { return activeLayer(); };
    layerCtx.getBrushHasActiveStroke
        = [this]() { return m_brush->hasActiveStroke() && !m_brush->strokeBuffer().empty(); };
    layerCtx.getBrushStrokeBuffer = [this]() { return &m_brush->strokeBuffer(); };
    layerCtx.getBrushStrokeOpacity = [this]() { return m_brush->strokeOpacity(); };
    layerCtx.getBrushStrokeBlendMode
        = [this]() { return static_cast<int>(m_brush->strokeBlendMode()); };
    layerCtx.getBrushIsEraseMode = [this]() { return m_brush->isEraseMode(); };
    layerCtx.getBrushIsBlurMode = [this]() { return m_brush->isBlurMode(); };
    layerCtx.getBrushIsSmudgeMode = [this]() { return m_brush->isSmudgeMode(); };
    layerCtx.getBrushIsWetMode = [this]() { return m_brush->isWetMode(); };
    layerCtx.getBrushIsLiquifyMode = [this]() { return m_brush->isLiquifyMode(); };
    layerCtx.getSelectionMaskGrid = [this]() -> const TileGrid* {
        return (m_selectionController && m_selectionController->lassoSelection().hasSelection())
            ? &m_selectionController->lassoSelection().mask()
            : nullptr;
    };
    layerCtx.getSelectionMaskHasSoftAlpha = [this]() {
        return m_selectionController && m_selectionController->lassoSelection().maskHasSoftAlpha();
    };
    layerCtx.shouldPreserveAlphaForPaintMask
        = [this](const ruwa::core::layers::LayerData* layer, const TileGrid* paintMask) {
              return shouldPreserveAlphaForPaintMask(layer, paintMask);
          };
    layerCtx.getFillPreview = [this]() { return currentFillPreviewState(); };
    layerCtx.useViewportTransformPreview = [this]() {
        return m_transformViewportPreview.active && m_transformViewportPreview.viewportPathEnabled;
    };
    layerCtx.getTransformPreserveMaskedSource = [this]() { return m_selectionCopyMoveTransform; };
    layerCtx.getTransformController = [this]() { return &m_transformController; };
    layerCtx.getRenderer = [this]() { return m_renderer.get(); };
    m_layerCompositingBuilder
        = std::make_unique<LayerCompositingBuilder>(&m_layerModel, m_smartProjectedGrids, layerCtx);

    // Selection controller (must be created before QuickShapeMorph)
    CanvasSelectionContext selCtx;
    selCtx.getCanvas = [this]() -> const Canvas& { return m_canvas; };
    selCtx.getCanvasForEdit = [this]() { return &m_canvas; };
    selCtx.getZoom = [this]() { return static_cast<float>(m_viewport.camera().zoom()); };
    selCtx.getTileRenderer = [this]() { return m_renderer ? m_renderer->tileRenderer() : nullptr; };
    selCtx.getSelectionRenderer = [this]() { return m_selectionRenderer.get(); };
    selCtx.getRenderer = [this]() { return m_renderer.get(); };
    selCtx.getActiveLayer = [this]() { return activeLayer(); };
    selCtx.getLayerModel = [this]() { return m_layerModel; };
    selCtx.getCompositingGridForLayer = [this](const ruwa::core::layers::LayerData* layer) {
        return m_layerCompositingBuilder->compositingGridForLayer(layer);
    };
    selCtx.getEffectShapedGrid = [this](const ruwa::core::layers::LayerData* layer) {
        return buildEffectShapedSelectionGrid(layer);
    };
    selCtx.isTransformActive = [this]() { return m_transformController.isActive(); };
    selCtx.requestRender = [this]() { requestRender(); };
    selCtx.startSelectionTick = [this]() {
        if (!m_selectionTick.isActive())
            m_selectionTick.start();
    };
    selCtx.isSelectionTickActive = [this]() { return m_selectionTick.isActive(); };
    selCtx.executeFillWithColor = [this](const QColor& c) { return doFillSelectionWithColor(c); };
    selCtx.executeClearSelectionContent = [this]() { return doClearSelectionContent(); };
    m_selectionController = std::make_unique<CanvasSelectionController>(selCtx);

    m_strokeHost = std::make_unique<BrushStrokeHost>(this,
        BrushStrokeHost::Callbacks { [this]() { return m_brush; },
            [this]() { return activeLayerTileGrid(); }, [this]() { return activeLayer(); },
            [this](ruwa::core::layers::LayerData* layer, TileGrid* grid) {
                return getEffectivePaintMask(layer, grid);
            },
            [this](const ruwa::core::layers::LayerData* layer, const TileGrid* paintMask) {
                return shouldPreserveAlphaForPaintMask(layer, paintMask);
            },
            [this]() { return m_renderer ? m_renderer->brushExecutionBackend() : nullptr; },
            [this]() { return m_quickShapeMorph.get(); },
            [this]() { return effectiveDocumentBoundsWidth(); },
            [this]() { return effectiveDocumentBoundsHeight(); },
            [this]() { return m_initialized; },
            [this](bool edited) { notifyCanvasInteraction(edited); }, [this]() { requestRender(); },
            [this](const std::vector<TileKey>& dirtyKeys) {
                m_canvas.compositionCache().markDirty(
                    expandDirtyKeysByLayerEffects(m_layerModel, dirtyKeys));
            },
            [this]() { cleanupStrokeTextures(); }, [this]() { makeCurrent(); },
            [this]() { doneCurrent(); },
            [this]() { return QOpenGLContext::currentContext() == context(); },
            [this]() {
                SelectionState state;
                state.layer = captureLayerSelection(
                    m_layerModel ? m_layerModel->selectionManager() : nullptr);
                state.lasso = captureLassoSelection(
                    m_selectionController ? &m_selectionController->lassoSelection() : nullptr,
                    effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
                return state;
            },
            [this]() { return activeStrokeReplayDataFromSession(m_brushSession.get()); },
            [this](const QUuid& layerId,
                const std::unordered_set<TileKey, TileKeyHash>& flattenedKeys, bool eraseMode,
                float strokeOpacity) {
                if (flattenedKeys.empty()) {
                    return;
                }
                for (const auto& key : flattenedKeys) {
                    m_canvas.tilePositionIndex().addEntry(key, layerId);
                }
                const std::vector<TileKey> dirtyVec(flattenedKeys.begin(), flattenedKeys.end());
                const auto expandedDirty = expandDirtyKeysByLayerEffects(m_layerModel, dirtyVec);
                const std::vector<TileKey> expandedDirtyVec(
                    expandedDirty.begin(), expandedDirty.end());
                m_canvas.dirtyManager().onTilesDirtied(layerId, expandedDirtyVec);
                markBoardCompositionTilesDirty(layerId, expandedDirtyVec);
                emit contentRegionChanged(worldRectFromTileKeys(expandedDirtyVec));
                emit contentTilesChanged(qPointsFromTileKeys(expandedDirtyVec));
            },
            [this](const QUuid& layerId, const std::unordered_set<TileKey, TileKeyHash>& strokeKeys)
                -> std::shared_ptr<TileGrid> {
                if (layerId.isNull() || strokeKeys.empty() || !m_initialized || !m_renderer
                    || !m_layerCompositingBuilder) {
                    return nullptr;
                }

                std::vector<TileKey> keys(strokeKeys.begin(), strokeKeys.end());
                auto stack = m_layerCompositingBuilder->buildStackThroughLayer(layerId);
                if (stack.empty()) {
                    return nullptr;
                }
                CompositionCache backdropCache;
                Color canvasBackdrop = Color::transparent();
                m_layerCompositingBuilder->resolveCanvasBackgroundColor(canvasBackdrop);

                makeCurrent();
                m_renderer->compositeDirtyKeys(stack, backdropCache, keys, canvasBackdrop);
                auto backdropGrid = std::make_shared<TileGrid>(std::move(backdropCache.grid()));
                if (auto* backend = m_renderer->brushExecutionBackend()) {
                    GLsync fence = backend->startAsyncReadback(*backdropGrid, keys, true);
                    if (fence) {
                        backend->finishReadback(fence, *backdropGrid, keys, true);
                    }
                }
                doneCurrent();
                return backdropGrid;
            },
            [this]() {
                Color canvasBackdrop = Color::transparent();
                if (m_layerCompositingBuilder) {
                    m_layerCompositingBuilder->resolveCanvasBackgroundColor(canvasBackdrop);
                }
                return canvasBackdrop;
            },
            [this](BrushStrokeHost::SyncCommit&& commit) {
                if (!commit.flattenedKeys.empty()) {
                    if (!commit.eraseMode) {
                        emit strokePainted();
                    }
                    for (const auto& key : commit.flattenedKeys) {
                        if (!commit.snapshot.removedTiles.count(key)) {
                            m_canvas.tilePositionIndex().addEntry(key, commit.layerId);
                        }
                    }
                    const std::vector<TileKey> dirtyVec(
                        commit.flattenedKeys.begin(), commit.flattenedKeys.end());
                    const auto expandedDirty
                        = expandDirtyKeysByLayerEffects(m_layerModel, dirtyVec);
                    const std::vector<TileKey> expandedDirtyVec(
                        expandedDirty.begin(), expandedDirty.end());
                    m_canvas.dirtyManager().onTilesDirtied(commit.layerId, expandedDirtyVec);
                    markBoardCompositionTilesDirty(commit.layerId, expandedDirtyVec);
                    emit contentRegionChanged(worldRectFromTileKeys(expandedDirtyVec));
                    emit contentTilesChanged(qPointsFromTileKeys(expandedDirtyVec));

                    auto selRestore = buildCurrentSelectionRestore();
                    selRestore.before = commit.selectionBefore;
                    auto cmd = std::make_unique<DrawCommand>(
                        &m_canvas, m_layerModel, std::move(commit.snapshot), std::move(selRestore));
                    m_canvas.undoManager().push(std::move(cmd));
                }
            },
            [this](PendingStrokeFinalization& pending, const SelectionState& selectionBefore,
                bool emitPainted) {
                StrokeFinalizationController::Context ctx;
                ctx.getActiveLayerGrid = [this]() { return activeLayerTileGrid(); };
                ctx.getBrushExecutionBackend = [this]() {
                    return m_renderer ? m_renderer->brushExecutionBackend() : nullptr;
                };
                ctx.makeCurrent = [this]() { makeCurrent(); };
                ctx.doneCurrent = [this]() { doneCurrent(); };
                ctx.canvas = &m_canvas;
                ctx.layerModel = m_layerModel;
                SelectionRestoreContext selRestore;
                selRestore.layerSelection
                    = m_layerModel ? m_layerModel->selectionManager() : nullptr;
                selRestore.lassoSelection
                    = m_selectionController ? &m_selectionController->lassoSelection() : nullptr;
                selRestore.canvas = &m_canvas;
                selRestore.before = selectionBefore;
                selRestore.after.layer = captureLayerSelection(selRestore.layerSelection);
                selRestore.after.lasso = captureLassoSelection(selRestore.lassoSelection,
                    effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
                selRestore.layerExists = [this](const ruwa::core::layers::LayerId& id) {
                    return m_layerModel && m_layerModel->contains(id);
                };
                selRestore.requestRender = [this]() { requestRender(); };
                ctx.selectionRestore = std::move(selRestore);

                if (emitPainted) {
                    emit strokePainted();
                }
                StrokeFinalizationController::finalize(pending, ctx);
            },
            [this]() { return m_viewport.camera().zoom(); } });

    // Hold-to-shape quick morph (line / circle / triangle / square)
    m_quickShapeMorph = std::make_unique<QuickShapeMorph>(this,
        QuickShapeMorph::Callbacks {
            [this]() { return activeStrokeReplayDataFromSession(m_brushSession.get()); },
            [this]() { return m_strokeHost && m_strokeHost->isDrawing(); },
            [this]() { return activeLayerTileGrid(); },
            [this]() {
                auto* layer = activeLayer();
                TileGrid* grid = activeLayerTileGrid();
                return getEffectivePaintMask(layer, grid);
            },
            [this]() { return m_brush->effectiveRadius(); },
            [this]() { return m_brush->spacing(); },
            [this]() {
                return m_strokeHost ? m_strokeHost->lastStrokePosition()
                                    : std::make_pair(0.0f, 0.0f);
            },
            [this]() {
                if (m_strokeHost)
                    m_strokeHost->rebuildPreviewFromCurrentDabs();
            },
            [this]() {
                if (m_strokeHost)
                    m_strokeHost->notifyQuickShapePreviewModified();
            } });

    // Deferred transform finalization timer
    m_transformFinalizeTimer.setSingleShot(true);
    m_transformFinalizeTimer.setInterval(0);
    connect(
        &m_transformFinalizeTimer, &QTimer::timeout, this, &OpenGLCanvasWidget::finalizeTransform);

    // Selection tick (drives GPU mask updates without camera movement)
    m_selectionTick.setSingleShot(false);
    m_selectionTick.setInterval(16);
    connect(&m_selectionTick, &QTimer::timeout, this, [this]() { update(); });
}

bool OpenGLCanvasWidget::event(QEvent* event)
{
    return QOpenGLWidget::event(event);
}

void OpenGLCanvasWidget::showEvent(QShowEvent* event)
{
    QOpenGLWidget::showEvent(event);
#if defined(Q_OS_WIN)
    aether::platform::configureWindowsInkFeedback(reinterpret_cast<void*>(effectiveWinId()));
#endif
}

#if defined(Q_OS_WIN)
bool OpenGLCanvasWidget::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (aether::platform::handleWindowsInkNativeEvent(message, result)) {
        return true;
    }
    return QOpenGLWidget::nativeEvent(eventType, message, result);
}
#endif

OpenGLCanvasWidget::~OpenGLCanvasWidget()
{
    cancelPendingLassoFillCommit();
    stopFillPreview(false);
    shutdownFillWorker();
    makeCurrent();
    if (m_selectionController) {
        m_selectionController->shutdown(m_selectionRenderer.get());
    }
    flushPendingFinalization();
    flushPendingTransformFinalization();
    m_sceneFboManager.releaseSceneFbo(this);
    if (m_backdropCapture) {
        m_backdropCapture->shutdown();
        m_backdropCapture.reset();
    }
    m_overlayManager.reset();
    m_selectionRenderer.reset();
    m_layerScreenSourceCache.reset();
    m_renderer.reset();
    doneCurrent();
    paintGLCompositeContexts().erase(this);
    paintGLCameraFrameStates().erase(this);
}

// ==========================================================================
//   C A N V A S   &   V I E W P O R T
// ==========================================================================

void OpenGLCanvasWidget::setCanvas(uint32_t width, uint32_t height)
{
    m_canvas.setSize(width, height);
    update();
}

void OpenGLCanvasWidget::setBackgroundColor(const Color& color)
{
    m_backgroundColor = color;
    update();
}

void OpenGLCanvasWidget::setCheckerColors(const Color& color1, const Color& color2)
{
    m_checkerColor1 = color1;
    m_checkerColor2 = color2;
    update();
}

void OpenGLCanvasWidget::setCheckerSize(float size)
{
    m_checkerSize = size;
    update();
}

void OpenGLCanvasWidget::requestRender()
{
    update();
}

void OpenGLCanvasWidget::beginPanSampling()
{
    m_panSamplingActive = true;
    m_panSamplingLastGlobalPos = QCursor::pos();
    update();
}

void OpenGLCanvasWidget::endPanSampling()
{
    m_panSamplingActive = false;
}

void OpenGLCanvasWidget::notifyCanvasInteraction(bool canvasEdited)
{
    if (!m_canvasCornerEffectClock.isValid()) {
        m_canvasCornerEffectClock.start();
    }

    const qint64 now = m_canvasCornerEffectClock.elapsed();
    m_lastCanvasInteractionMs = now;
    if (canvasEdited) {
        m_lastCanvasEditMs = now;
    }

    const bool shouldInterruptAnimation
        = m_canvasCornerRadiusScreenPx > 0.01f || m_canvasCornerTargetScreenPx > 0.01f;
    m_canvasCornerTargetScreenPx = 0.0f;

    if (shouldInterruptAnimation) {
        requestRender();
    }

    const qint64 remainingEditMs
        = std::max<qint64>(0, kCanvasCornerIdleDelayMs - (now - m_lastCanvasEditMs));
    const qint64 remainingCooldownMs = canvasEdited ? 0 : kCanvasCornerInteractionCooldownMs;
    scheduleCanvasCornerEffectUpdate(std::max(remainingEditMs, remainingCooldownMs));
}

void OpenGLCanvasWidget::scheduleCanvasCornerEffectUpdate(qint64 delayMs)
{
    const qint64 clampedDelay = std::max<qint64>(0, delayMs);
    if (clampedDelay == 0) {
        requestRender();
        return;
    }

    const int delay
        = static_cast<int>(std::min<qint64>(clampedDelay, std::numeric_limits<int>::max()));
    if (!m_canvasCornerEffectTimer.isActive()
        || m_canvasCornerEffectTimer.remainingTime() > delay) {
        m_canvasCornerEffectTimer.start(delay);
    }
}

bool OpenGLCanvasWidget::isCanvasFullyVisible(float marginPx) const
{
    if (!hasFiniteDocumentBounds()) {
        return false;
    }

    if (width() <= 0 || height() <= 0 || m_canvas.width() == 0 || m_canvas.height() == 0) {
        return false;
    }

    const float canvasWidth = static_cast<float>(m_canvas.width());
    const float canvasHeight = static_cast<float>(m_canvas.height());
    const Vector2 p0 = screenFromDocumentWorld({ 0.0f, 0.0f });
    const Vector2 p1 = screenFromDocumentWorld({ canvasWidth, 0.0f });
    const Vector2 p2 = screenFromDocumentWorld({ canvasWidth, canvasHeight });
    const Vector2 p3 = screenFromDocumentWorld({ 0.0f, canvasHeight });
    const float left = std::min({ p0.x, p1.x, p2.x, p3.x });
    const float right = std::max({ p0.x, p1.x, p2.x, p3.x });
    const float top = std::min({ p0.y, p1.y, p2.y, p3.y });
    const float bottom = std::max({ p0.y, p1.y, p2.y, p3.y });

    return left >= marginPx && top >= marginPx && right <= static_cast<float>(width()) - marginPx
        && bottom <= static_cast<float>(height()) - marginPx;
}

float OpenGLCanvasWidget::canvasCornerRadiusCanvasPx() const
{
    if (!hasFiniteDocumentBounds() || m_exportPreviewSuppressContentMirror) {
        return 0.0f;
    }

    if (m_canvasCornerRadiusScreenPx <= 0.0f) {
        return 0.0f;
    }

    const float zoom = m_viewport.camera().zoom();
    if (zoom <= std::numeric_limits<float>::epsilon()) {
        return 0.0f;
    }

    const float maxRadius = 0.5f
        * std::min(static_cast<float>(m_canvas.width()), static_cast<float>(m_canvas.height()));
    return std::clamp(m_canvasCornerRadiusScreenPx / zoom, 0.0f, maxRadius);
}

bool OpenGLCanvasWidget::updateCanvasCornerEffectState()
{
    if (!hasFiniteDocumentBounds()) {
        m_canvasCornerTargetScreenPx = 0.0f;
        m_canvasCornerRadiusScreenPx = 0.0f;
        return false;
    }

    if (!m_canvasCornerEffectClock.isValid()) {
        m_canvasCornerEffectClock.start();
    }

    const qint64 now = m_canvasCornerEffectClock.elapsed();
    const qint64 previousTickMs = m_canvasCornerLastTickMs;
    const float deltaSeconds = previousTickMs > 0
        ? std::clamp(static_cast<float>(now - previousTickMs) / 1000.0f, 0.0f, 0.05f)
        : 0.0f;
    m_canvasCornerLastTickMs = now;

    const bool fullyVisible = isCanvasFullyVisible(kCanvasCornerVisibilityMarginPx);
    const qint64 idleSinceEditMs = now - m_lastCanvasEditMs;
    const qint64 idleSinceInteractionMs = now - m_lastCanvasInteractionMs;
    const bool editIdleEnough = idleSinceEditMs >= kCanvasCornerIdleDelayMs;
    const bool interactionCoolingDown = idleSinceInteractionMs < kCanvasCornerInteractionCooldownMs;
    const bool interactionActive = (m_strokeHost && m_strokeHost->isDrawing())
        || m_transformController.isActive() || m_lassoFillActive || isLassoActive()
        || isRectSelectionActive() || isCircleSelectionActive();
    const float targetRadius
        = (!interactionActive && fullyVisible && editIdleEnough && !interactionCoolingDown)
        ? kCanvasCornerMaxScreenRadiusPx
        : 0.0f;
    m_canvasCornerTargetScreenPx = targetRadius;

    const float radiusDelta = m_canvasCornerTargetScreenPx - m_canvasCornerRadiusScreenPx;
    if (std::abs(radiusDelta) > 0.01f) {
        const float blend = deltaSeconds > 0.0f
            ? (1.0f - std::exp(-kCanvasCornerAnimationSpeed * deltaSeconds))
            : 0.0f;
        m_canvasCornerRadiusScreenPx += radiusDelta * blend;
        if (std::abs(m_canvasCornerTargetScreenPx - m_canvasCornerRadiusScreenPx) <= 0.05f) {
            m_canvasCornerRadiusScreenPx = m_canvasCornerTargetScreenPx;
        }
        scheduleCanvasCornerEffectUpdate(kCanvasCornerFrameDelayMs);
        return true;
    }

    m_canvasCornerRadiusScreenPx = m_canvasCornerTargetScreenPx;
    if (!interactionActive && fullyVisible && targetRadius <= 0.0f) {
        const qint64 remainingEditMs
            = std::max<qint64>(0, kCanvasCornerIdleDelayMs - idleSinceEditMs);
        const qint64 remainingCooldownMs
            = std::max<qint64>(0, kCanvasCornerInteractionCooldownMs - idleSinceInteractionMs);
        const qint64 wakeDelayMs = std::max(remainingEditMs, remainingCooldownMs);
        if (wakeDelayMs > 0) {
            scheduleCanvasCornerEffectUpdate(wakeDelayMs);
        }
    }

    return false;
}

void OpenGLCanvasWidget::ensureFillProgressPopup()
{
    if (m_fillProgressPopup) {
        return;
    }

    m_fillProgressPopup = new FillProgressPopupWidget(this);
    m_fillProgressPopup->hide();
}

QPoint OpenGLCanvasWidget::fillProgressPopupTopLeft() const
{
    if (!m_fillProgressPopup) {
        return QPoint(kFillProgressPopupMargin, kFillProgressPopupMargin);
    }

    const QPoint anchorPoint = fillProgressPopupAnchorPoint();
    const QSize popupSize = m_fillProgressPopup->isVisible() ? m_fillProgressPopup->size()
                                                             : m_fillProgressPopup->sizeHint();

    int x = anchorPoint.x() - popupSize.width() / 2;
    int y = anchorPoint.y() - popupSize.height() - kFillProgressPopupOffsetY;

    x = qBound(kFillProgressPopupMargin, x,
        qMax(kFillProgressPopupMargin, width() - popupSize.width() - kFillProgressPopupMargin));
    y = qBound(kFillProgressPopupMargin, y,
        qMax(kFillProgressPopupMargin, height() - popupSize.height() - kFillProgressPopupMargin));
    return QPoint(x, y);
}

QPoint OpenGLCanvasWidget::fillProgressPopupAnchorPoint() const
{
    const Vector2 screenPos = screenFromDocumentWorld(m_fillPreview.origin);
    return QPoint(
        static_cast<int>(std::round(screenPos.x)), static_cast<int>(std::round(screenPos.y)));
}

void OpenGLCanvasWidget::updateFillProgressPopupPosition()
{
    if (!m_fillProgressPopup || !m_fillProgressPopup->isVisible() || !m_fillPreview.active) {
        return;
    }

    m_fillProgressPopup->updateAnchor(fillProgressPopupTopLeft());
}

void OpenGLCanvasWidget::showFillProgressPopupProcessing()
{
    ensureFillProgressPopup();
    m_fillProgressPopup->showProcessingAt(fillProgressPopupAnchorPoint());
    m_fillProgressPopup->updateAnchor(fillProgressPopupTopLeft());
}

void OpenGLCanvasWidget::showClassicFillWaitPopup()
{
    ensureFillProgressPopup();
    m_fillProgressPopup->showProcessingAt(fillProgressPopupAnchorPoint(),
        QCoreApplication::translate("OpenGLCanvasWidget", "please wait"),
        FillProgressPopupWidget::CompactProcessingTextWidth);
    m_fillProgressPopup->updateAnchor(fillProgressPopupTopLeft());
}

void OpenGLCanvasWidget::showFillProgressPopupDone(const QPoint& anchorPoint)
{
    ensureFillProgressPopup();
    m_fillProgressPopup->showDoneAt(anchorPoint);
}

void OpenGLCanvasWidget::hideFillProgressPopupImmediate()
{
    if (m_fillProgressPopup) {
        m_fillProgressPopup->hideImmediate();
    }
}

void OpenGLCanvasWidget::setCanvasResizeOverlayState(
    bool active, const QRectF& selectionWorldRect, bool selectingOrMoving)
{
    // Keep last valid rect while fading out to avoid flash/jump.
    if (active || !selectionWorldRect.isEmpty()) {
        m_canvasResizeSelectionWorld = selectionWorldRect.normalized();
    }
    m_canvasResizeOverlaySelecting = selectingOrMoving;

    if (active != m_canvasResizeOverlayActive) {
        m_canvasResizeOverlayActive = active;
        if (auto* overlay = m_overlayManager ? m_overlayManager->canvasResizeOverlay() : nullptr) {
            if (overlay->isInitialized()) {
                if (active) {
                    overlay->onModeEntered();
                } else {
                    overlay->onModeExited();
                }
            }
        }
    }

    if (auto* overlay = m_overlayManager ? m_overlayManager->canvasResizeOverlay() : nullptr) {
        if (overlay->isInitialized()) {
            overlay->setSelectionRect(m_canvasResizeSelectionWorld);
            overlay->setSelecting(m_canvasResizeOverlaySelecting);
        }
    }

    requestRender();
}

void OpenGLCanvasWidget::setTextEditOverlayState(const TextEditOverlayState& state)
{
    if (auto* overlay = m_overlayManager ? m_overlayManager->textEditOverlay() : nullptr) {
        overlay->setState(state);
    }
    requestRender();
}

// ==========================================================================
//   L A Y E R   M O D E L   I N T E G R A T I O N
// ==========================================================================

void OpenGLCanvasWidget::setLayerModel(ruwa::core::layers::LayerModel* model)
{
    stopFillPreview(false);
    clearLassoFillPreview(false);
    // Disconnect old model
    if (m_layerModel) {
        QObject::disconnect(m_layerModel, nullptr, this, nullptr);
    }

    m_layerModel = model;
    m_canvas.setLayerModel(model);
    invalidateCachedLayerStacks();
    clearBoardCompositionCache();
    invalidateBoardCompositionCache();

    // Connect new model signals for dirty tracking
    if (m_layerModel) {
        connect(m_layerModel, &ruwa::core::layers::LayerModel::layersChanged, this,
            &OpenGLCanvasWidget::onLayersChanged);
        connect(m_layerModel, &ruwa::core::layers::LayerModel::layerDataChanged, this,
            &OpenGLCanvasWidget::onLayerDataChanged);
        connect(m_layerModel, &ruwa::core::layers::LayerModel::layerEffectsChanged, this,
            [this](const QUuid& id, quint64) {
                if (m_layerScreenSourceCache) {
                    m_layerScreenSourceCache->invalidateByLayer(id);
                }
                invalidateCachedLayerStacks();
                requestRender();
            });
        connect(m_layerModel, &ruwa::core::layers::LayerModel::layerRemoved, this,
            &OpenGLCanvasWidget::onLayerRemoved);
        connect(m_layerModel, &ruwa::core::layers::LayerModel::selectionChanged, this,
            &OpenGLCanvasWidget::onLayerSelectionChanged);

        m_lastSelectionState.layer = captureLayerSelection(m_layerModel->selectionManager());
        m_lastSelectionState.lasso = captureLassoSelection(
            m_selectionController ? &m_selectionController->lassoSelection() : nullptr,
            effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());

        // Force full index rebuild + dirty propagation immediately.
        // This covers cases where model already contains loaded tiles
        // before any new layersChanged signal is emitted.
        onLayersChanged();
        return;
    }

    m_smartProjectedGrids.clear();
    requestRender();
}

void OpenGLCanvasWidget::setRasterizationConfirmCallback(
    std::function<bool(const QString&, const QString&)> fn)
{
    m_rasterizationConfirmCallback = std::move(fn);
}

void OpenGLCanvasWidget::onLayersChanged()
{
    // Structure changed (add/remove/reorder) â€” rebuild projection/index and dirty all
    cancelPendingLassoFillCommit();
    stopFillPreview(false);
    clearLassoFillPreview(false);
    if (m_layerScreenSourceCache) {
        m_layerScreenSourceCache->clear();
    }
    rebuildLayerProjectionCaches();
    invalidateCachedLayerStacks();
    invalidateBoardCompositionCache();

    // Drop composited tiles at positions no longer covered by any layer.
    // layerRemoved fires only for removal roots, so deleting a group never
    // removes the children's tile keys from index/cache before the rebuild
    // above wipes them from the index — leaving ghost tiles in the cache.
    purgeStaleCompositionCacheTiles();

    m_canvas.dirtyManager().onStructureChanged();
    requestRender();
}

void OpenGLCanvasWidget::purgeStaleCompositionCacheTiles()
{
    std::unordered_set<TileKey, TileKeyHash> aliveKeys;
    for (const TileKey& key : m_canvas.tilePositionIndex().allTileKeys()) {
        aliveKeys.insert(key);
    }
    std::vector<TileKey> staleKeys;
    staleKeys.reserve(m_canvas.compositionCache().grid().tiles().size());
    for (const auto& [key, tile] : m_canvas.compositionCache().grid().tiles()) {
        Q_UNUSED(tile);
        if (!aliveKeys.count(key)) {
            staleKeys.push_back(key);
        }
    }
    for (const TileKey& key : staleKeys) {
        m_canvas.compositionCache().removeTile(key);
    }
}

void OpenGLCanvasWidget::onLayerDataChanged(const QUuid& id)
{
    cancelPendingLassoFillCommit(id);
    if (m_fillPreview.active && m_fillPreview.targetLayerId == id) {
        stopFillPreview(false);
    }
    if (m_layerScreenSourceCache) {
        m_layerScreenSourceCache->invalidateByLayer(id);
    }
    if (m_lassoFillPreview.active && m_lassoFillPreview.targetLayerId == id) {
        m_lassoFillViewportPreview.screenSourcesDirty = true;
        refreshLassoFillPreview();
    }
    if (m_transformViewportPreview.active
        && (m_transformViewportPreview.targetLayerId == id
            || m_transformViewportPreview.sourceLayerId == id)) {
        invalidateTransformViewportPreviewSource();
    }
    invalidateCachedLayerStacks();

    // Raster properties dirty only that layer positions.
    // Background changes must refresh all existing cached tiles
    // (do not generate new tile positions).
    // Other non-raster/vector/background properties affect compositing, so
    // recompose all known raster positions.
    if (m_layerModel) {
        if (auto* layer = m_layerModel->layerById(id); layer) {
            // A bounds-expanding effect (blur/shadow) bleeds beyond the layer's own
            // tiles. Disabling/removing it drops the pad to 0, so an own-tiles-only
            // invalidation would strand the old bleed ("ghost") on the expanded
            // neighbour tiles. Track whether the layer HAD such an effect and force
            // a full (viewport-culled) cache invalidation across the on→off / add→
            // remove transition as well, not only while the effect is active.
            const bool nowBoundsEffect
                = ruwa::core::effects::EffectCoverageResolver::neighborhoodPadPixels(layer->effects)
                > 0;
            const bool boundsInvalidate
                = nowBoundsEffect || m_layerHadBoundsEffect.value(id, false);
            m_layerHadBoundsEffect.insert(id, nowBoundsEffect);
            if (layer->isIsolatedPixelLayer()) {
                rebuildSmartProjectionCacheForLayer(id);
                m_canvas.dirtyManager().onStructureChanged();
                // Remove stale composited tiles that no longer exist in any layer.
                // Fast text edits can otherwise leave one-frame "ghost" cache tiles.
                purgeStaleCompositionCacheTiles();
            } else if (layer->isText()) {
                std::unordered_set<TileKey, TileKeyHash> affectedKeys
                    = m_canvas.tilePositionIndex().tileKeysForLayer(id);
                const auto keys = retainedTextTileKeys(layer);
                m_canvas.tilePositionIndex().removeLayer(id);
                for (const TileKey& key : keys) {
                    m_canvas.tilePositionIndex().addEntry(key, id);
                    affectedKeys.insert(key);
                }
                if (!affectedKeys.empty()) {
                    m_canvas.dirtyManager().onTilesDirtied(id, affectedKeys);
                    markBoardCompositionTilesDirty(id, affectedKeys);
                } else {
                    m_canvas.dirtyManager().onStructureChanged();
                }
            } else if (layer->isPixelLayer()) {
                if (boundsInvalidate) {
                    // A bounds-expanding effect (blur/shadow) bleeds beyond this
                    // layer's own tiles. Editing, disabling or removing it must
                    // clear the old bleed, so invalidate the whole (viewport-culled)
                    // cache rather than just this layer's tiles.
                    m_canvas.compositionCache().markAllDirty();
                } else {
                    m_canvas.dirtyManager().onLayerPropertyChanged(id);
                }
            } else if (layer->isBackground()) {
                m_canvas.compositionCache().markAllDirty();
            } else if ((layer->isAdjustment() || layer->isGroup()) && boundsInvalidate) {
                // An adjustment/group with a bounds-expanding effect (blur) bleeds
                // into tiles with no content of their own (not in the tile index),
                // so onStructureChanged() — which only dirties index keys — would
                // leave that bleed stale on an edit/disable/remove. Invalidate the
                // whole (viewport-culled) cache instead.
                m_canvas.compositionCache().markAllDirty();
            } else {
                m_canvas.dirtyManager().onStructureChanged();
            }
            if (layerAffectsBoardComposition(layer) || m_boardCompositionLayerIds.contains(id)) {
                invalidateBoardCompositionCache();
            }
        } else {
            m_canvas.dirtyManager().onStructureChanged();
            if (m_boardCompositionLayerIds.contains(id)) {
                invalidateBoardCompositionCache();
            }
        }
    } else {
        m_canvas.dirtyManager().onLayerPropertyChanged(id);
        if (m_boardCompositionLayerIds.contains(id)) {
            invalidateBoardCompositionCache();
        }
    }
    requestRender();
}

void OpenGLCanvasWidget::onLayerRemoved(const QUuid& id)
{
    cancelPendingLassoFillCommit(id);
    if (m_fillPreview.active && m_fillPreview.targetLayerId == id) {
        stopFillPreview(false);
    }
    if (m_layerScreenSourceCache) {
        m_layerScreenSourceCache->invalidateByLayer(id);
    }
    if (m_lassoFillPreview.active && m_lassoFillPreview.targetLayerId == id) {
        clearLassoFillPreview(false);
    }
    if (m_transformViewportPreview.active
        && (m_transformViewportPreview.targetLayerId == id
            || m_transformViewportPreview.sourceLayerId == id)) {
        clearTransformViewportPreview();
    }
    m_canvas.dirtyManager().onLayerRemoved(id);
    m_smartProjectedGrids.remove(id);
    m_layerHadBoundsEffect.remove(id);
    invalidateCachedLayerStacks();
    if (m_boardCompositionLayerIds.contains(id)) {
        invalidateBoardCompositionCache();
    }
    requestRender();
}

void OpenGLCanvasWidget::invalidateCachedLayerStacks()
{
    if (m_layerCompositingBuilder) {
        m_layerCompositingBuilder->invalidateCaches();
    }
}

void OpenGLCanvasWidget::invalidateBoardCompositionCache()
{
    m_boardCompositionCacheDirty = true;
}

void OpenGLCanvasWidget::clearBoardCompositionCache()
{
    m_boardCompositionCache.clear();
    m_boardCompositionCacheDirty = true;
    m_boardCompositionKeys.clear();
    m_boardCompositionLayerIds.clear();
}

bool OpenGLCanvasWidget::layerAffectsBoardComposition(
    const ruwa::core::layers::LayerData* layer) const
{
    return layer && (layer->isGroup() || layer->isExportExcluded());
}

bool OpenGLCanvasWidget::isBoardCompositionLayerId(const QUuid& id) const
{
    if (id.isNull()) {
        return false;
    }
    if (m_boardCompositionLayerIds.contains(id)) {
        return true;
    }
    return m_layerModel && layerAffectsBoardComposition(m_layerModel->layerById(id));
}

void OpenGLCanvasWidget::markBoardCompositionTilesDirty(
    const QUuid& layerId, const std::vector<TileKey>& keys)
{
    if (keys.empty() || !isBoardCompositionLayerId(layerId)) {
        return;
    }
    m_boardCompositionCache.markDirty(keys);
    for (const TileKey& key : keys) {
        m_boardCompositionKeys.insert(key);
    }
}

void OpenGLCanvasWidget::markBoardCompositionTilesDirty(
    const QUuid& layerId, const std::unordered_set<TileKey, TileKeyHash>& keys)
{
    if (keys.empty() || !isBoardCompositionLayerId(layerId)) {
        return;
    }
    m_boardCompositionCache.markDirty(keys);
    for (const TileKey& key : keys) {
        m_boardCompositionKeys.insert(key);
    }
}

void OpenGLCanvasWidget::updateBoardCompositionTransientDirty()
{
    if (m_brush->hasActiveStroke()) {
        if (auto* layer = activeLayer(); layer && isBoardCompositionLayerId(layer->id)) {
            invalidateBoardCompositionCache();
        }
    }
    if (m_fillPreview.active && isBoardCompositionLayerId(m_fillPreview.targetLayerId)) {
        invalidateBoardCompositionCache();
    }
    if (m_transformController.isActive()
        && isBoardCompositionLayerId(m_transformController.layerId())) {
        invalidateBoardCompositionCache();
    }
}

void OpenGLCanvasWidget::rebuildSmartProjectionCacheForLayer(const QUuid& layerId)
{
    invalidateCachedLayerStacks();
    if (!m_layerModel) {
        m_smartProjectedGrids.remove(layerId);
        m_canvas.tilePositionIndex().removeLayer(layerId);
        return;
    }

    auto* layer = m_layerModel->layerById(layerId);
    if (!layer || !layer->isIsolatedPixelLayer()) {
        m_smartProjectedGrids.remove(layerId);
        m_canvas.tilePositionIndex().removeLayer(layerId);
        return;
    }

    const auto* sourceGrid = layer->smartContentGrid.get();
    if (!sourceGrid) {
        m_smartProjectedGrids.remove(layerId);
        m_canvas.tilePositionIndex().removeLayer(layerId);
        return;
    }

    auto projected = buildSmartProjectedGrid(layer);
    if (projected) {
        m_smartProjectedGrids.insert(layerId, projected);
        m_canvas.tilePositionIndex().rebuildForLayer(layerId, projected->tiles());
    } else {
        m_smartProjectedGrids.remove(layerId);
        m_canvas.tilePositionIndex().rebuildForLayer(layerId, sourceGrid->tiles());
    }
}

std::shared_ptr<TileGrid> OpenGLCanvasWidget::buildSmartProjectedGrid(
    const ruwa::core::layers::LayerData* layer)
{
    if (!layer || !layer->isIsolatedPixelLayer()) {
        return nullptr;
    }
    const TileGrid* sourceGrid = layer->smartContentGrid.get();
    if (!sourceGrid) {
        return nullptr;
    }
    TileGrid* sourceGridMutable = const_cast<TileGrid*>(sourceGrid);

    TransformState projectionState = layer->smartTransform;
    if (projectionState.contentBounds.width <= 0.0f
        || projectionState.contentBounds.height <= 0.0f) {
        projectionState.contentBounds = TransformState::computeContentBounds(*sourceGrid);
        projectionState.pivot = projectionState.contentBounds.center();
    }

    // Identity transform can be rendered from source grid directly.
    if (projectionState.isIdentity()) {
        return nullptr;
    }

    // Prefer GPU transform for smart-layer projection rebuild when available.
    const bool canUseGpu = m_initialized && m_renderer && m_renderer->transformRenderer()
        && m_renderer->tileRenderer() && !sourceGrid->empty();
    if (canUseGpu) {
        makeCurrent();

        auto* transformRenderer = m_renderer->transformRenderer();
        auto* tileRenderer = m_renderer->tileRenderer();

        m_renderer->uploadDirtyTiles(*sourceGridMutable);
        transformRenderer->buildSourceAtlas(*sourceGrid, tileRenderer);

        auto projected = std::make_shared<TileGrid>();
        auto resultKeys = transformRenderer->applyGPU(projectionState, *projected, tileRenderer);
        if (!resultKeys.empty()) {
            std::vector<TileKey> readbackKeys(resultKeys.begin(), resultKeys.end());
            GLsync fence = transformRenderer->startAsyncReadback(*projected, readbackKeys);
            if (fence) {
                transformRenderer->finishReadback(fence, *projected, readbackKeys);
            }
            projected->pruneEmpty();
        }

        // Projected caches should stay CPU-backed only.
        for (auto& [key, tile] : projected->tiles()) {
            if (tile.hasTexture()) {
                tileRenderer->destroyTileTexture(tile);
            }
        }
        transformRenderer->destroySourceAtlas();
        doneCurrent();

        if (projected->empty()) {
            return nullptr;
        }
        return projected;
    }

    auto projected = cloneTileGrid(sourceGrid);
    if (!projected) {
        return nullptr;
    }

    if (!projected->empty()) {
        TransformApplicator::apply(*projected, projectionState);
    }
    return projected;
}

void OpenGLCanvasWidget::rebuildLayerProjectionCaches()
{
    m_canvas.tilePositionIndex().clear();
    if (!m_layerModel) {
        m_smartProjectedGrids.clear();
        return;
    }

    QSet<QUuid> aliveIsolatedIds;

    m_layerModel->forEach([this, &aliveIsolatedIds](ruwa::core::layers::LayerData* layer) {
        if (!layer) {
            return;
        }

        if (layer->isIsolatedPixelLayer()) {
            aliveIsolatedIds.insert(layer->id);
            rebuildSmartProjectionCacheForLayer(layer->id);
            return;
        }

        if (layer->isText()) {
            const auto keys = retainedTextTileKeys(layer);
            m_canvas.tilePositionIndex().removeLayer(layer->id);
            for (const TileKey& key : keys) {
                m_canvas.tilePositionIndex().addEntry(key, layer->id);
            }
            return;
        }

        if (const auto* grid = layer->pixelGrid(); grid) {
            m_canvas.tilePositionIndex().rebuildForLayer(layer->id, grid->tiles());
        }
    });

    for (auto it = m_smartProjectedGrids.begin(); it != m_smartProjectedGrids.end();) {
        if (!aliveIsolatedIds.contains(it.key())) {
            it = m_smartProjectedGrids.erase(it);
        } else {
            ++it;
        }
    }
}

// ==========================================================================
//   D R A W I N G   S T R O K E
// ==========================================================================

ruwa::core::layers::LayerData* OpenGLCanvasWidget::activeLayer() const
{
    if (!m_layerModel)
        return nullptr;
    return m_layerModel->selectedLayer();
}

bool OpenGLCanvasWidget::ensurePaintableActiveLayer()
{
    if (!m_layerModel) {
        return true;
    }

    auto* layer = m_layerModel->selectedLayer();
    if (!layer || !layerRequiresRasterizationForPixelEdits(layer)) {
        return true;
    }

    const QString layerKind = isolatedLayerKindLabel(layer);
    const QString title = isolatedLayerConvertTitle(layer);
    const QString message = tr("%1 must be rasterized before painting.\n"
                               "Convert the selected layer to a raster layer?")
                                .arg(layerKind);

    bool confirmed = false;
    if (m_rasterizationConfirmCallback) {
        confirmed = m_rasterizationConfirmCallback(title, message);
    } else {
        const auto reply = QMessageBox::question(
            this, title, message, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        confirmed = (reply == QMessageBox::Yes);
    }

    if (!confirmed) {
        return false;
    }

    rasterizeSmartLayer(layer);

    // Conversion is complete, but current input should not paint yet.
    // User must start a new stroke explicitly.
    return false;
}

void OpenGLCanvasWidget::rasterizeSmartLayer(ruwa::core::layers::LayerData* layer)
{
    if (!layer || !layerRequiresRasterizationForPixelEdits(layer))
        return;

    std::unique_ptr<TileGrid> rasterGrid;

    if (layer->isText()) {
        rasterGrid = rasterizeTextLayerToGrid(layer);
    } else if (auto projected = buildSmartProjectedGrid(layer); projected) {
        rasterGrid = std::make_unique<TileGrid>(std::move(*projected));
    } else if (auto clonedSource = cloneTileGrid(layer->smartContentGrid.get()); clonedSource) {
        rasterGrid = std::make_unique<TileGrid>(std::move(*clonedSource));
    } else {
        rasterGrid = std::make_unique<TileGrid>();
    }

    RasterizedLayerState replacedState;
    replacedState.type = layer->type;
    replacedState.tileGrid = std::move(layer->tileGrid);
    replacedState.smartContentGrid = std::move(layer->smartContentGrid);
    replacedState.smartTransform = layer->smartTransform;
    replacedState.textData = std::move(layer->textData);
    replacedState.runtimeVisualBackend = layer->runtimeVisualBackend;
    replacedState.runtimeRetainedPayload = std::move(layer->runtimeRetainedPayload);
    replacedState.runtimeRetainedPayloadKey = std::move(layer->runtimeRetainedPayloadKey);

    layer->type = ruwa::core::layers::LayerType::Raster;
    layer->tileGrid = std::move(rasterGrid);
    layer->smartTransform.reset();
    layer->runtimeRetainedPayloadKey.clear();
    layer->runtimeVisualBackend = LayerVisualBackend::RasterTiles;

    m_smartProjectedGrids.remove(layer->id);
    rebuildLayerProjectionCaches();
    m_canvas.dirtyManager().onStructureChanged();
    if (m_layerModel) {
        m_layerModel->notifyLayerDataChanged(layer->id);
    }

    auto command = std::make_unique<RasterizeLayerCommand>(m_layerModel, layer->id,
        std::move(replacedState), [this](const ruwa::core::layers::LayerId& changedLayerId) {
            rebuildLayerProjectionCaches();
            m_canvas.dirtyManager().onStructureChanged();
            if (m_layerModel) {
                m_layerModel->notifyLayerDataChanged(changedLayerId);
            }
        });
    m_canvas.undoManager().push(std::move(command));
}

bool OpenGLCanvasWidget::confirmRasterizeForSelectionTransform(
    ruwa::core::layers::LayerData* layer, bool hasSelection)
{
    if (!layer || !layerRequiresRasterizationForPixelEdits(layer) || !hasSelection) {
        return true;
    }

    const QString title = isolatedLayerConvertTitle(layer);
    const QString message = tr("%1 does not support transforming a selection.\n"
                               "Rasterize the layer to transform the selection?")
                                .arg(isolatedLayerKindLabel(layer));

    bool confirmed = false;
    if (m_rasterizationConfirmCallback) {
        confirmed = m_rasterizationConfirmCallback(title, message);
    } else {
        const auto reply = QMessageBox::question(
            this, title, message, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        confirmed = (reply == QMessageBox::Yes);
    }

    if (!confirmed) {
        return false;
    }

    return true;
}

bool OpenGLCanvasWidget::offerRasterizeForSelectionTransform(
    ruwa::core::layers::LayerData* layer, bool hasSelection)
{
    if (!confirmRasterizeForSelectionTransform(layer, hasSelection)) {
        return false;
    }
    if (!layer || !layerRequiresRasterizationForPixelEdits(layer) || !hasSelection) {
        return true;
    }

    rasterizeSmartLayer(layer);
    return true;
}

bool OpenGLCanvasWidget::offerRasterizeForSelectionTransformTargets(bool hasSelection)
{
    if (!hasSelection || !m_layerModel) {
        return true;
    }

    std::vector<QUuid> rasterizeLayerIds;
    rasterizeLayerIds.reserve(m_transformTargetSet.visualTargets.size());
    for (const TransformTargetInfo& target : m_transformTargetSet.visualTargets) {
        auto* layer = m_layerModel->layerById(target.layerId);
        if (layerRequiresRasterizationForPixelEdits(layer)) {
            rasterizeLayerIds.push_back(target.layerId);
        }
    }
    if (rasterizeLayerIds.empty()) {
        return true;
    }

    for (const QUuid& layerId : rasterizeLayerIds) {
        if (!confirmRasterizeForSelectionTransform(
                m_layerModel->layerById(layerId), hasSelection)) {
            return false;
        }
    }

    bool rasterizedAny = false;
    for (const QUuid& layerId : rasterizeLayerIds) {
        auto* layer = m_layerModel->layerById(layerId);
        if (layerRequiresRasterizationForPixelEdits(layer)) {
            rasterizeSmartLayer(layer);
            rasterizedAny = true;
        }
    }

    if (rasterizedAny) {
        m_transformTargetSet = buildTransformTargetSet(*m_layerModel, transformBoundsForLayer);
    }
    return true;
}

TileGrid* OpenGLCanvasWidget::activeLayerTileGrid() const
{
    auto* layer = activeLayer();
    if (!isLayerCanvasEditable(layer))
        return nullptr;
    // When the layer's mask is the active edit target, brush strokes paint into
    // the mask grid (alpha = hide coverage) instead of the layer pixels.
    if (layer->maskEditActive && layer->maskGrid) {
        return layer->maskGrid.get();
    }
    if (!layer->isRaster() || !layer->tileGrid)
        return nullptr;
    return layer->tileGrid.get();
}

TileGrid* OpenGLCanvasWidget::getEffectivePaintMask(
    ruwa::core::layers::LayerData* layer, TileGrid* grid) const
{
    // const_cast: getEffectivePaintMask must return TileGrid* because the brush
    // stroke pipeline does per-tile GPU texture sync (ensureTileTexture / uploadTileData)
    // through this pointer. Selection-mask pixel data itself stays read-only — pixel
    // mutation is funneled through LassoSelectionManager::MaskMutationScope.
    TileGrid* selectionMask
        = (m_selectionController && m_selectionController->lassoSelection().hasSelection())
        ? const_cast<TileGrid*>(&m_selectionController->lassoSelection().mask())
        : nullptr;

    const bool useAlphaLock = layer && layer->alphaLock && layer->isPixelLayer();

    if (!useAlphaLock) {
        return selectionMask;
    }

    TileGrid* layerAlphaGrid = m_layerCompositingBuilder
        ? m_layerCompositingBuilder->compositingGridForLayer(layer)
        : nullptr;
    if (!layerAlphaGrid || layerAlphaGrid->empty()) {
        return selectionMask;
    }

    if (!selectionMask) {
        return layerAlphaGrid;
    }

    buildAlphaLockCombinedMask(selectionMask, layerAlphaGrid);
    return &m_alphaLockMaskGrid;
}

bool OpenGLCanvasWidget::shouldPreserveAlphaForPaintMask(
    const ruwa::core::layers::LayerData* layer, const TileGrid* paintMask) const
{
    if (!layer || !layer->isPixelLayer()) {
        return false;
    }
    if (layer->alphaLock) {
        return true;
    }
    if (!paintMask) {
        return false;
    }
    if (!m_selectionController) {
        return false;
    }

    const auto& selectionMask = m_selectionController->lassoSelection().mask();
    const bool paintMaskHasSoftAlpha = (paintMask == &selectionMask)
        ? m_selectionController->lassoSelection().maskHasSoftAlpha()
        : LayerCompositingBuilder::hasSoftMaskAlpha(paintMask);
    if (!paintMaskHasSoftAlpha) {
        return false;
    }

    const QUuid sourceLayerId = m_selectionController->contentSelectionSourceLayerId();
    return !sourceLayerId.isNull() && sourceLayerId == layer->id;
}

void OpenGLCanvasWidget::buildAlphaLockCombinedMask(
    const TileGrid* selectionMask, const TileGrid* layerAlphaGrid) const
{
    m_alphaLockMaskGrid.clear();
    const int canvasW = static_cast<int>(m_canvas.width());
    const int canvasH = static_cast<int>(m_canvas.height());
    if (canvasW <= 0 || canvasH <= 0)
        return;

    for (const auto& [key, selTile] : selectionMask->tiles()) {
        const int baseX = key.x * static_cast<int>(TILE_SIZE);
        const int baseY = key.y * static_cast<int>(TILE_SIZE);
        const uint8_t* selPixels = selTile.pixels();

        TileData* dstTile = nullptr;
        for (uint32_t localY = 0; localY < TILE_SIZE; ++localY) {
            const int y = baseY + static_cast<int>(localY);
            if (y < 0 || y >= canvasH)
                continue;
            for (uint32_t localX = 0; localX < TILE_SIZE; ++localX) {
                const int x = baseX + static_cast<int>(localX);
                if (x < 0 || x >= canvasW)
                    continue;
                const uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
                const uint8_t selA = selPixels[idx + 3];
                if (selA == 0)
                    continue;

                const TileData* layerTile = layerAlphaGrid->getTile(key);
                uint8_t layerA = 0;
                if (layerTile) {
                    // Content grid may be RGBA8/16F/32F — read alpha format-aware
                    // (the raw [idx*4+3] byte is only the alpha for RGBA8).
                    float lp[4];
                    aether::readTilePixelF(*layerTile, localX, localY, lp);
                    layerA = static_cast<uint8_t>(std::clamp(lp[3], 0.0f, 1.0f) * 255.0f + 0.5f);
                }
                const uint8_t combined = static_cast<uint8_t>(std::min(selA, layerA));
                if (combined == 0)
                    continue;

                if (!dstTile)
                    dstTile = &m_alphaLockMaskGrid.getOrCreateTile(key);
                dstTile->setPixel(localX, localY, combined, combined, combined, combined);
            }
        }
    }
}

void OpenGLCanvasWidget::beginStroke(
    float worldX, float worldY, float pressure, BrushStrokeHost::StrokeInputDevice inputDevice)
{
    if (m_transformController.isActive())
        return;
    if (!ensurePaintableActiveLayer())
        return;

    auto* layer = activeLayer();
    if (!isLayerCanvasEditable(layer))
        return;

    if (m_strokeHost) {
        m_strokeHost->beginStroke(worldX, worldY, pressure, inputDevice);
    }
}

void OpenGLCanvasWidget::continueStroke(
    float worldX, float worldY, float pressure, BrushStrokeHost::StrokeInputDevice inputDevice)
{
    if (m_strokeHost) {
        m_strokeHost->continueStroke(worldX, worldY, pressure, inputDevice);
    }
}

void OpenGLCanvasWidget::continueStrokeAtElapsed(float worldX, float worldY, float pressure,
    float strokeElapsedSeconds, BrushStrokeHost::StrokeInputDevice inputDevice)
{
    if (m_strokeHost) {
        m_strokeHost->continueStrokeAtElapsed(
            worldX, worldY, pressure, strokeElapsedSeconds, inputDevice);
    }
}

float OpenGLCanvasWidget::strokeElapsedSecondsNow() const
{
    return m_strokeHost ? m_strokeHost->strokeElapsedSecondsNow() : 0.0f;
}

uint32_t OpenGLCanvasWidget::effectiveDocumentBoundsWidth() const
{
    return hasFiniteDocumentBounds() ? m_canvas.width() : 0;
}

uint32_t OpenGLCanvasWidget::effectiveDocumentBoundsHeight() const
{
    return hasFiniteDocumentBounds() ? m_canvas.height() : 0;
}

void OpenGLCanvasWidget::translateActiveStroke(float dx, float dy)
{
    if (m_strokeHost) {
        m_strokeHost->translateActiveStroke(dx, dy);
    }
}

void OpenGLCanvasWidget::endStroke()
{
    if (m_strokeHost) {
        m_strokeHost->endStroke();
    }
}

void OpenGLCanvasWidget::beginLasso(
    float worldX, float worldY, bool addSelection, bool subtractSelection)
{
    resetSelectionPathStabilizer();
    m_lastStrokeTargetX = worldX;
    m_lastStrokeTargetY = worldY;
    if (m_selectionController) {
        m_selectionAtLassoBegin.layer
            = captureLayerSelection(m_layerModel ? m_layerModel->selectionManager() : nullptr);
        m_selectionAtLassoBegin.lasso
            = captureLassoSelection(&m_selectionController->lassoSelection(),
                effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
        m_stabilizerElapsedTimer.start();
        const float lagMs = ruwa::core::brushes::stabilizationTauMs(m_lassoStabilization);
        const auto stabOut = ruwa::core::brushes::sampleStrokeStabilizer(
            m_lassoStabilizerState, worldX, worldY, lagMs, 0.0, true);
        m_selectionController->beginLasso(stabOut.x, stabOut.y, addSelection, subtractSelection);
    }
    updateStabilizerCatchupTimer();
}

void OpenGLCanvasWidget::updateLasso(float worldX, float worldY)
{
    m_lastStrokeTargetX = worldX;
    m_lastStrokeTargetY = worldY;
    if (m_selectionController) {
        const float lagMs = ruwa::core::brushes::stabilizationTauMs(m_lassoStabilization);
        const double nowMs = static_cast<double>(m_stabilizerElapsedTimer.elapsed());
        ruwa::core::brushes::sampleStrokeStabilizerPath(m_lassoStabilizerState, worldX, worldY,
            lagMs, nowMs, false,
            [this](const ruwa::core::brushes::StrokeStabilizerPoint& pt, double) {
                m_selectionController->updateLasso(pt.x, pt.y);
            });
    }
    updateStabilizerCatchupTimer();
}

void OpenGLCanvasWidget::endLasso(bool addSelection, bool subtractSelection)
{
    if (m_selectionController) {
        m_selectionController->endLasso(addSelection, subtractSelection);
        SelectionState after;
        after.layer
            = captureLayerSelection(m_layerModel ? m_layerModel->selectionManager() : nullptr);
        after.lasso = captureLassoSelection(&m_selectionController->lassoSelection(),
            effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
        m_ignoreSelectionChange = true;
        pushSelectionCommand(m_selectionAtLassoBegin, after);
        m_ignoreSelectionChange = false;
    }
    resetSelectionPathStabilizer();
}

void OpenGLCanvasWidget::beginLassoFill(float worldX, float worldY)
{
    m_lassoFillActive = true;
    m_lassoFillPreviewRefreshQueued = false;
    m_lassoFillPoints.clear();
    resetSelectionPathStabilizer();
    m_lastStrokeTargetX = worldX;
    m_lastStrokeTargetY = worldY;
    m_stabilizerElapsedTimer.start();
    const float lagMs = ruwa::core::brushes::stabilizationTauMs(m_lassoFillStabilization);
    const auto stabOut = ruwa::core::brushes::sampleStrokeStabilizer(
        m_lassoStabilizerState, worldX, worldY, lagMs, 0.0, true);
    m_lassoFillPoints.emplace_back(stabOut.x, stabOut.y);
    clearLassoFillPreview(false);
    updateStabilizerCatchupTimer();
    requestRender();
}

void OpenGLCanvasWidget::updateLassoFill(float worldX, float worldY)
{
    if (!m_lassoFillActive)
        return;
    m_lastStrokeTargetX = worldX;
    m_lastStrokeTargetY = worldY;
    const float lagMs = ruwa::core::brushes::stabilizationTauMs(m_lassoFillStabilization);
    const double nowMs = static_cast<double>(m_stabilizerElapsedTimer.elapsed());
    const float zoom = m_viewport.camera().zoom();
    const float minDistSq = (2.0f / zoom) * (2.0f / zoom);
    bool addedPoint = false;
    ruwa::core::brushes::sampleStrokeStabilizerPath(m_lassoStabilizerState, worldX, worldY, lagMs,
        nowMs, false,
        [this, minDistSq, &addedPoint](
            const ruwa::core::brushes::StrokeStabilizerPoint& pt, double) {
            if (m_lassoFillPoints.empty()) {
                m_lassoFillPoints.emplace_back(pt.x, pt.y);
                addedPoint = true;
            } else {
                const Vector2& last = m_lassoFillPoints.back();
                const float dx = pt.x - last.x;
                const float dy = pt.y - last.y;
                if ((dx * dx + dy * dy) >= minDistSq) {
                    m_lassoFillPoints.emplace_back(pt.x, pt.y);
                    addedPoint = true;
                }
            }
        });
    if (addedPoint) {
        updateStabilizerCatchupTimer();
        scheduleLassoFillPreviewRefresh();
    } else {
        updateStabilizerCatchupTimer();
    }
}

void OpenGLCanvasWidget::endLassoFill()
{
    if (!m_lassoFillActive)
        return;
    m_lassoFillActive = false;
    m_lassoFillPreviewRefreshQueued = false;
    clearLassoFillPreview();

    if (m_lassoFillPoints.size() < 3) {
        m_lassoFillPoints.clear();
        resetSelectionPathStabilizer();
        requestRender();
        return;
    }

    const Vector2& first = m_lassoFillPoints.front();
    const Vector2& last = m_lassoFillPoints.back();
    float dx = first.x - last.x;
    float dy = first.y - last.y;
    if ((dx * dx + dy * dy) > 0.01f) {
        m_lassoFillPoints.push_back(first);
    }

    const std::vector<Vector2> commitPolygon = m_lassoFillPoints;
    m_lassoFillPoints.clear();

    const PolygonFillWorkArea workArea
        = computePolygonFillWorkArea(commitPolygon, hasFiniteDocumentBounds(),
            static_cast<int>(m_canvas.width()), static_cast<int>(m_canvas.height()));
    if (workArea.polygon.size() < 3 || workArea.width <= 0 || workArea.height <= 0) {
        resetSelectionPathStabilizer();
        requestRender();
        return;
    }

    performLassoFill(commitPolygon);
    resetSelectionPathStabilizer();
    requestRender();
}

void OpenGLCanvasWidget::cancelLassoFill()
{
    m_lassoFillActive = false;
    m_lassoFillPreviewRefreshQueued = false;
    m_lassoFillPoints.clear();
    resetSelectionPathStabilizer();
    clearLassoFillPreview();
    requestRender();
}

FloodFillResult::RawTileMap OpenGLCanvasWidget::buildLassoFillScreenMask(
    const std::vector<Vector2>& polygon) const
{
    FloodFillResult::RawTileMap maskTiles;
    if (polygon.size() < 3) {
        return maskTiles;
    }

    float minX = polygon.front().x;
    float minY = polygon.front().y;
    float maxX = minX;
    float maxY = minY;
    for (const Vector2& point : polygon) {
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }

    const float zoom = std::max(m_viewport.camera().zoom(), 0.0001f);
    const float edgePadWorld = 0.75f / zoom;
    const bool finiteDocumentBounds = hasFiniteDocumentBounds();
    const int canvasW = static_cast<int>(m_canvas.width());
    const int canvasH = static_cast<int>(m_canvas.height());

    const int y0 = finiteDocumentBounds
        ? std::max(0, static_cast<int>(std::floor(minY - edgePadWorld)))
        : static_cast<int>(std::floor(minY - edgePadWorld));
    const int y1 = finiteDocumentBounds
        ? std::min(canvasH - 1, static_cast<int>(std::ceil(maxY + edgePadWorld)))
        : static_cast<int>(std::ceil(maxY + edgePadWorld));
    const int x0 = finiteDocumentBounds
        ? std::max(0, static_cast<int>(std::floor(minX - edgePadWorld)))
        : static_cast<int>(std::floor(minX - edgePadWorld));
    const int x1 = finiteDocumentBounds
        ? std::min(canvasW - 1, static_cast<int>(std::ceil(maxX + edgePadWorld)))
        : static_cast<int>(std::ceil(maxX + edgePadWorld));
    if (y1 < y0 || x1 < x0) {
        return maskTiles;
    }

    auto writeMaskSpan = [&](int y, int spanX0, int spanX1) {
        if (spanX1 < spanX0) {
            return;
        }

        const int tileSize = static_cast<int>(TILE_SIZE);
        const int32_t tileY = floorDiv(y, tileSize);
        const uint32_t localY = floorMod(y, tileSize);
        const int32_t tileX0 = floorDiv(spanX0, tileSize);
        const int32_t tileX1 = floorDiv(spanX1, tileSize);
        for (int32_t tileX = tileX0; tileX <= tileX1; ++tileX) {
            const int tileMinX = tileX * tileSize;
            const int segmentX0 = std::max(spanX0, tileMinX);
            const int segmentX1 = std::min(spanX1, tileMinX + tileSize - 1);
            const uint32_t localX0 = floorMod(segmentX0, tileSize);
            const uint32_t localX1 = floorMod(segmentX1, tileSize);
            std::vector<uint8_t>& tile = aether::ensureRawTile(maskTiles, TileKey { tileX, tileY });
            const uint32_t begin = aether::rawPixelIndex(localX0, localY);
            const size_t byteCount = static_cast<size_t>(localX1 - localX0 + 1) * TILE_CHANNELS;
            std::memset(tile.data() + begin, 255, byteCount);
        }
    };

    const size_t count = polygon.size();
    std::vector<float> intersections;
    intersections.reserve(count);
    for (int y = y0; y <= y1; ++y) {
        const float scanY = static_cast<float>(y) + 0.5f;
        intersections.clear();

        for (size_t i = 0, j = count - 1; i < count; j = i++) {
            const Vector2& a = polygon[j];
            const Vector2& b = polygon[i];
            if ((a.y <= scanY) == (b.y <= scanY)) {
                continue;
            }

            const float t = (scanY - a.y) / (b.y - a.y + 0.0000001f);
            intersections.push_back(a.x + t * (b.x - a.x));
        }

        if (intersections.size() < 2) {
            continue;
        }
        std::sort(intersections.begin(), intersections.end());
        if ((intersections.size() & 1U) != 0U) {
            intersections.pop_back();
        }

        for (size_t k = 0; k + 1 < intersections.size(); k += 2) {
            const int spanX0 = std::max(static_cast<int>(std::ceil(intersections[k] - 0.5f)), x0);
            const int spanX1
                = std::min(static_cast<int>(std::floor(intersections[k + 1] - 0.5f)), x1);
            writeMaskSpan(y, spanX0, spanX1);
        }
    }

    if (edgePadWorld > 0.0f) {
        const float edgePadSq = edgePadWorld * edgePadWorld;
        for (size_t i = 0, j = count - 1; i < count; j = i++) {
            const Vector2& a = polygon[j];
            const Vector2& b = polygon[i];
            const Vector2 ab { b.x - a.x, b.y - a.y };
            const float denom = std::max(ab.x * ab.x + ab.y * ab.y, 0.0000001f);
            const int edgeY0
                = std::max(y0, static_cast<int>(std::floor(std::min(a.y, b.y) - edgePadWorld)));
            const int edgeY1
                = std::min(y1, static_cast<int>(std::ceil(std::max(a.y, b.y) + edgePadWorld)));
            const int edgeX0
                = std::max(x0, static_cast<int>(std::floor(std::min(a.x, b.x) - edgePadWorld)));
            const int edgeX1
                = std::min(x1, static_cast<int>(std::ceil(std::max(a.x, b.x) + edgePadWorld)));

            for (int y = edgeY0; y <= edgeY1; ++y) {
                int spanStart = std::numeric_limits<int>::max();
                int spanEnd = std::numeric_limits<int>::min();
                for (int x = edgeX0; x <= edgeX1; ++x) {
                    const Vector2 p { static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f };
                    const float t
                        = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / denom, 0.0f, 1.0f);
                    const Vector2 closest { a.x + ab.x * t, a.y + ab.y * t };
                    const float dx = p.x - closest.x;
                    const float dy = p.y - closest.y;
                    if (dx * dx + dy * dy <= edgePadSq) {
                        spanStart = std::min(spanStart, x);
                        spanEnd = std::max(spanEnd, x);
                    } else if (spanStart != std::numeric_limits<int>::max()) {
                        writeMaskSpan(y, spanStart, spanEnd);
                        spanStart = std::numeric_limits<int>::max();
                        spanEnd = std::numeric_limits<int>::min();
                    }
                }
                if (spanStart != std::numeric_limits<int>::max()) {
                    writeMaskSpan(y, spanStart, spanEnd);
                }
            }
        }
    }

    return maskTiles;
}

bool OpenGLCanvasWidget::performLassoFill(const std::vector<Vector2>& polygon)
{
    if (m_transformController.isActive())
        return false;

    auto* layer = activeLayer();
    if (!isLayerCanvasEditable(layer) || !layer->isRaster() || !layer->tileGrid) {
        return false;
    }
    notifyCanvasInteraction(true);

    uint8_t fillR = m_brush->colorR();
    uint8_t fillG = m_brush->colorG();
    uint8_t fillB = m_brush->colorB();
    uint8_t fillA = m_brush->colorA();

    FloodFillResult::RawTileMap screenMaskTiles = buildLassoFillScreenMask(polygon);
    if (screenMaskTiles.empty()) {
        return false;
    }

    // Selection mask gates the fill output (per-pixel alpha cap).
    // nullptr = no active selection -> unrestricted polygon fill.
    const TileGrid* selectionMask = nullptr;
    if (m_selectionController && m_selectionController->lassoSelection().hasSelection()
        && !m_selectionController->lassoSelection().mask().empty()) {
        selectionMask = &m_selectionController->lassoSelection().mask();
    }

    FloodFillResult result = fillMaskTiles(
        *layer->tileGrid, screenMaskTiles, fillR, fillG, fillB, fillA, selectionMask);

    return applyFloodFillResult(layer->id, std::move(result), buildCurrentSelectionRestore());
}

void OpenGLCanvasWidget::cancelPendingLassoFillCommit(const QUuid& layerId)
{
    if (!m_lassoFillCommit.job) {
        return;
    }
    if (!layerId.isNull() && m_lassoFillCommit.targetLayerId != layerId) {
        return;
    }

    m_lassoFillCommit.job->cancelled.store(true, std::memory_order_release);
    m_lassoFillCommit.targetLayerId = QUuid();
    m_lassoFillCommit.job.reset();
    syncFillProcessingLayerSignal();
}

void OpenGLCanvasWidget::handlePendingLassoFillResult(uint64_t sequence, const QUuid& layerId,
    SelectionRestoreContext selectionRestore, FloodFillResult result)
{
    if (!m_lassoFillCommit.job || m_lassoFillCommit.sequence != sequence
        || m_lassoFillCommit.targetLayerId != layerId) {
        return;
    }

    const std::shared_ptr<LassoFillCommitState::AsyncJob> job = m_lassoFillCommit.job;
    m_lassoFillCommit.targetLayerId = QUuid();
    m_lassoFillCommit.job.reset();
    syncFillProcessingLayerSignal();

    if (!job || job->cancelled.load(std::memory_order_acquire)) {
        return;
    }
    if (result.pixelsFilled <= 0 || result.fillMaskTiles.empty()) {
        return;
    }

    if (applyFloodFillResult(layerId, std::move(result), std::move(selectionRestore))) {
        requestRender();
    }
}

void OpenGLCanvasWidget::scheduleLassoFillPreviewRefresh()
{
    if (m_lassoFillPreviewRefreshQueued) {
        return;
    }

    m_lassoFillPreviewRefreshQueued = true;
    const int refreshDelayMs = hasFiniteDocumentBounds() ? 0 : 16;
    QTimer::singleShot(refreshDelayMs, this, [this]() {
        m_lassoFillPreviewRefreshQueued = false;
        if (!m_lassoFillActive) {
            return;
        }

        refreshLassoFillPreview();
        requestRender();
    });
}

void OpenGLCanvasWidget::refreshLassoFillPreview()
{
    if (!m_lassoFillActive) {
        clearLassoFillPreview();
        return;
    }

    auto* layer = activeLayer();
    if (!isLayerCanvasEditable(layer) || !layer->isRaster() || !layer->tileGrid
        || m_lassoFillPoints.size() < 3) {
        clearLassoFillPreview();
        return;
    }

    const std::vector<Vector2> closedPoly = buildClosedPolygon(m_lassoFillPoints);
    if (closedPoly.size() < 3) {
        clearLassoFillPreview();
        return;
    }

    std::vector<Vector2> previewPolygon = closedPoly;
    if (hasFiniteDocumentBounds()) {
        const int canvasW = static_cast<int>(m_canvas.width());
        const int canvasH = static_cast<int>(m_canvas.height());
        if (canvasW <= 0 || canvasH <= 0) {
            clearLassoFillPreview();
            return;
        }

        previewPolygon = clipPolygonToCanvas(
            previewPolygon, static_cast<float>(canvasW), static_cast<float>(canvasH));
        if (previewPolygon.size() < 3) {
            clearLassoFillPreview();
            return;
        }
    }

    const Rect bounds = retainedPolygonBounds(previewPolygon);
    if (!rectHasArea(bounds)) {
        clearLassoFillPreview();
        return;
    }

    const Color previewColor = Color::fromRGB(
        m_brush->colorR(), m_brush->colorG(), m_brush->colorB(), m_brush->colorA());
    const bool targetChanged
        = !m_lassoFillPreview.active || m_lassoFillPreview.targetLayerId != layer->id;
    const bool colorChanged = !m_lassoFillPreview.active
        || m_lassoFillPreview.color.r != previewColor.r
        || m_lassoFillPreview.color.g != previewColor.g
        || m_lassoFillPreview.color.b != previewColor.b
        || m_lassoFillPreview.color.a != previewColor.a;
    const bool polygonChanged = !m_lassoFillPreview.active
        || !polygonsEquivalent(m_lassoFillPreview.polygon, previewPolygon);
    const bool boundsChanged = !m_lassoFillPreview.active || m_lassoFillPreview.bounds.x != bounds.x
        || m_lassoFillPreview.bounds.y != bounds.y
        || m_lassoFillPreview.bounds.width != bounds.width
        || m_lassoFillPreview.bounds.height != bounds.height;

    if (!(targetChanged || colorChanged || polygonChanged || boundsChanged)) {
        return;
    }

    m_lassoFillPreview.active = true;
    m_lassoFillPreview.targetLayerId = layer->id;
    m_lassoFillPreview.revision += 1;
    m_lassoFillPreview.bounds = bounds;
    m_lassoFillPreview.polygon = previewPolygon;
    m_lassoFillPreview.color = previewColor;
    m_lassoFillPreview.compositingState = {};

    m_lassoFillViewportPreview.active = true;
    m_lassoFillViewportPreview.targetLayerId = layer->id;
    m_lassoFillViewportPreview.polygonWorld = std::move(previewPolygon);
    m_lassoFillViewportPreview.screenPolygonDirty = true;
    m_lassoFillViewportPreview.screenMaskDirty = true;
    if (targetChanged) {
        m_lassoFillViewportPreview.screenSourcesDirty = true;
    }
}

void OpenGLCanvasWidget::clearLassoFillPreview(bool markDirtyTiles)
{
    Q_UNUSED(markDirtyTiles);
    if (!m_lassoFillPreview.active && !m_lassoFillViewportPreview.active) {
        return;
    }

    m_lassoFillPreview.active = false;
    m_lassoFillPreview.targetLayerId = QUuid();
    m_lassoFillPreview.revision = 0;
    m_lassoFillPreview.bounds = {};
    m_lassoFillPreview.polygon.clear();
    m_lassoFillPreview.color = {};
    m_lassoFillPreview.compositingState = {};
    m_lassoFillViewportPreview = {};
}

void OpenGLCanvasWidget::beginRectSelection(
    float worldX, float worldY, bool addSelection, bool subtractSelection)
{
    if (m_selectionController) {
        m_selectionAtLassoBegin.layer
            = captureLayerSelection(m_layerModel ? m_layerModel->selectionManager() : nullptr);
        m_selectionAtLassoBegin.lasso
            = captureLassoSelection(&m_selectionController->lassoSelection(),
                effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
        m_selectionController->beginRectSelection(worldX, worldY, addSelection, subtractSelection);
    }
}

void OpenGLCanvasWidget::updateRectSelection(float worldX, float worldY)
{
    if (m_selectionController)
        m_selectionController->updateRectSelection(worldX, worldY);
}

void OpenGLCanvasWidget::endRectSelection(bool addSelection, bool subtractSelection)
{
    if (m_selectionController) {
        m_selectionController->endRectSelection(addSelection, subtractSelection);
        SelectionState after;
        after.layer
            = captureLayerSelection(m_layerModel ? m_layerModel->selectionManager() : nullptr);
        after.lasso = captureLassoSelection(&m_selectionController->lassoSelection(),
            effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
        m_ignoreSelectionChange = true;
        pushSelectionCommand(m_selectionAtLassoBegin, after);
        m_ignoreSelectionChange = false;
    }
}

void OpenGLCanvasWidget::beginCircleSelection(
    float worldX, float worldY, bool addSelection, bool subtractSelection)
{
    if (m_selectionController) {
        m_selectionAtLassoBegin.layer
            = captureLayerSelection(m_layerModel ? m_layerModel->selectionManager() : nullptr);
        m_selectionAtLassoBegin.lasso
            = captureLassoSelection(&m_selectionController->lassoSelection(),
                effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
        m_selectionController->beginCircleSelection(
            worldX, worldY, addSelection, subtractSelection);
    }
}

void OpenGLCanvasWidget::updateCircleSelection(float worldX, float worldY)
{
    if (m_selectionController)
        m_selectionController->updateCircleSelection(worldX, worldY);
}

void OpenGLCanvasWidget::translateActiveSelection(float dx, float dy)
{
    if (m_selectionController)
        m_selectionController->translateActiveSelection(dx, dy);
}

void OpenGLCanvasWidget::endCircleSelection(bool addSelection, bool subtractSelection)
{
    if (m_selectionController) {
        m_selectionController->endCircleSelection(addSelection, subtractSelection);
        SelectionState after;
        after.layer
            = captureLayerSelection(m_layerModel ? m_layerModel->selectionManager() : nullptr);
        after.lasso = captureLassoSelection(&m_selectionController->lassoSelection(),
            effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
        m_ignoreSelectionChange = true;
        pushSelectionCommand(m_selectionAtLassoBegin, after);
        m_ignoreSelectionChange = false;
    }
}

void OpenGLCanvasWidget::clearSelectionMask()
{
    if (m_selectionController) {
        SelectionState before;
        before.layer
            = captureLayerSelection(m_layerModel ? m_layerModel->selectionManager() : nullptr);
        before.lasso = captureLassoSelection(&m_selectionController->lassoSelection(),
            effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
        if (!before.lasso.isEmpty()) {
            SelectionState after;
            after.layer = before.layer;
            after.lasso.canvasWidth = effectiveDocumentBoundsWidth();
            after.lasso.canvasHeight = effectiveDocumentBoundsHeight();
            m_ignoreSelectionChange = true;
            pushSelectionCommand(before, after);
            m_ignoreSelectionChange = false;
        }
        m_selectionController->clearSelectionMask();
    }
}

void OpenGLCanvasWidget::selectActiveLayerContent()
{
    if (m_selectionController) {
        SelectionState before;
        before.layer
            = captureLayerSelection(m_layerModel ? m_layerModel->selectionManager() : nullptr);
        before.lasso = captureLassoSelection(&m_selectionController->lassoSelection(),
            effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
        m_selectionController->selectActiveLayerContent();
        SelectionState after;
        after.layer = before.layer;
        after.lasso = captureLassoSelection(&m_selectionController->lassoSelection(),
            effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
        m_ignoreSelectionChange = true;
        pushSelectionCommand(before, after);
        m_ignoreSelectionChange = false;
    }
}

bool OpenGLCanvasWidget::doFillSelectionWithColor(const QColor& color)
{
    if (!m_selectionController)
        return false;
    auto* layer = activeLayer();
    if (!isLayerCanvasEditable(layer) || !layer->isRaster())
        return false;
    const bool maskTarget = layer->maskEditActive && layer->maskGrid != nullptr;
    TileGrid* targetGrid = maskTarget ? layer->maskGrid.get() : layer->tileGrid.get();
    if (!targetGrid)
        return false;
    notifyCanvasInteraction(true);

    const auto& selectionMask = m_selectionController->lassoSelection().mask();

    const uint8_t fillA = static_cast<uint8_t>(qBound(0, color.alpha(), 255));
    const uint8_t fillR = static_cast<uint8_t>(qBound(0, color.red(), 255));
    const uint8_t fillG = static_cast<uint8_t>(qBound(0, color.green(), 255));
    const uint8_t fillB = static_cast<uint8_t>(qBound(0, color.blue(), 255));
    const uint8_t fillPR
        = static_cast<uint8_t>((static_cast<int>(fillR) * static_cast<int>(fillA) + 127) / 255);
    const uint8_t fillPG
        = static_cast<uint8_t>((static_cast<int>(fillG) * static_cast<int>(fillA) + 127) / 255);
    const uint8_t fillPB
        = static_cast<uint8_t>((static_cast<int>(fillB) * static_cast<int>(fillA) + 127) / 255);
    // Normalized premultiplied fill for the format-aware blend below (content may
    // be RGBA8/16F/32F). Snapshots are sized by the target grid's own format so
    // the DrawCommand (which captures targetGrid->format()) reads them correctly.
    const float fillAF = static_cast<float>(fillA) / 255.0f;
    const float fillPRF = static_cast<float>(fillPR) / 255.0f;
    const float fillPGF = static_cast<float>(fillPG) / 255.0f;
    const float fillPBF = static_cast<float>(fillPB) / 255.0f;
    const size_t contentTileBytes = aether::tileByteSize(targetGrid->format());
    constexpr float kFillEps = 0.5f / 255.0f; // ~half an 8-bit step = "unchanged"
    const bool clipToCanvas = hasFiniteDocumentBounds();
    const int canvasW = static_cast<int>(m_canvas.width());
    const int canvasH = static_cast<int>(m_canvas.height());

    StrokeSnapshot snapshot;
    snapshot.layerId = layer->id;
    snapshot.maskTarget = maskTarget;
    std::unordered_set<TileKey, TileKeyHash> dirtyKeys;

    for (const auto& [key, maskTile] : selectionMask.tiles()) {
        const uint8_t* maskPixels = maskTile.pixels();
        TileData* dstTile = targetGrid->getTile(key);
        const bool hadTile = (dstTile != nullptr);
        bool tileTouched = false;
        bool capturedBefore = false;

        for (uint32_t localY = 0; localY < TILE_SIZE; ++localY) {
            const int worldY = key.y * static_cast<int>(TILE_SIZE) + static_cast<int>(localY);
            if (clipToCanvas && (worldY < 0 || worldY >= canvasH))
                continue;
            for (uint32_t localX = 0; localX < TILE_SIZE; ++localX) {
                const int worldX = key.x * static_cast<int>(TILE_SIZE) + static_cast<int>(localX);
                if (clipToCanvas && (worldX < 0 || worldX >= canvasW))
                    continue;

                const uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
                const uint8_t maskA = maskPixels[idx + 3]; // RGBA8 selection coverage
                if (maskA == 0)
                    continue;

                const float cov = static_cast<float>(maskA) / 255.0f;
                const float srcA = fillAF * cov; // masked source alpha (premult)
                if (!dstTile && srcA <= 0.0f)
                    continue;
                if (!dstTile) {
                    dstTile = &targetGrid->getOrCreateTile(key);
                    snapshot.createdTiles.insert(key);
                }

                if (!capturedBefore && hadTile) {
                    auto& before = snapshot.beforeTiles[key];
                    before.resize(contentTileBytes);
                    std::memcpy(before.data(), dstTile->pixels(), contentTileBytes);
                    capturedBefore = true;
                }

                float d[4];
                aether::readTilePixelF(*dstTile, localX, localY, d);
                const float srcPR = fillPRF * cov;
                const float srcPG = fillPGF * cov;
                const float srcPB = fillPBF * cov;
                const float inv = 1.0f - srcA;
                const float out[4] = { srcPR + d[0] * inv, srcPG + d[1] * inv, srcPB + d[2] * inv,
                    srcA + d[3] * inv };

                if (std::abs(out[0] - d[0]) < kFillEps && std::abs(out[1] - d[1]) < kFillEps
                    && std::abs(out[2] - d[2]) < kFillEps && std::abs(out[3] - d[3]) < kFillEps) {
                    continue;
                }
                aether::writeTilePixelF(*dstTile, localX, localY, out);
                tileTouched = true;
            }
        }

        if (!tileTouched) {
            if (!hadTile)
                snapshot.createdTiles.erase(key);
            continue;
        }
        if (dstTile && dstTile->isEmpty()) {
            if (hadTile)
                snapshot.removedTiles.insert(key);
            else
                snapshot.createdTiles.erase(key);
        }
        dirtyKeys.insert(key);
    }

    if (dirtyKeys.empty())
        return false;

    targetGrid->pruneEmpty();

    for (const auto& key : dirtyKeys) {
        if (snapshot.removedTiles.count(key)) {
            snapshot.afterTiles[key].assign(contentTileBytes, 0);
            continue;
        }
        const TileData* afterTile = targetGrid->getTile(key);
        if (!afterTile)
            continue;
        auto& after = snapshot.afterTiles[key];
        after.resize(contentTileBytes);
        std::memcpy(after.data(), afterTile->pixels(), contentTileBytes);
    }

    std::vector<TileKey> dirtyVec(dirtyKeys.begin(), dirtyKeys.end());
    m_canvas.dirtyManager().onTilesDirtied(layer->id, dirtyVec);
    markBoardCompositionTilesDirty(layer->id, dirtyVec);
    emit contentRegionChanged(worldRectFromTileKeys(dirtyVec));
    emit contentTilesChanged(qPointsFromTileKeys(dirtyVec));
    for (const auto& key : dirtyKeys) {
        if (!maskTarget && !snapshot.removedTiles.count(key))
            m_canvas.tilePositionIndex().addEntry(key, layer->id);
    }
    if (maskTarget) {
        layer->maskThumbnailDirty = true;
        invalidateCachedLayerStacks();
    }

    auto cmd = std::make_unique<DrawCommand>(
        &m_canvas, m_layerModel, std::move(snapshot), buildCurrentSelectionRestore());
    m_canvas.undoManager().push(std::move(cmd));
    if (m_layerModel)
        m_layerModel->notifyLayerDataChanged(layer->id);
    requestRender();
    return true;
}

bool OpenGLCanvasWidget::doClearSelectionContent()
{
    if (!m_selectionController)
        return false;
    auto* layer = activeLayer();
    if (!isLayerCanvasEditable(layer) || !layer->isRaster() || !layer->tileGrid)
        return false;
    notifyCanvasInteraction(true);

    auto& targetGrid = *layer->tileGrid;
    // Content snapshots sized by the grid's own pixel format (RGBA8/16F/32F) so
    // the DrawCommand (which captures grid.format()) reads them size-exact.
    const size_t contentTileBytes = aether::tileByteSize(targetGrid.format());
    constexpr float kClearEps = 0.5f / 255.0f;
    const auto& selectionMask = m_selectionController->lassoSelection().mask();
    const bool clipToCanvas = hasFiniteDocumentBounds();
    const int canvasW = static_cast<int>(m_canvas.width());
    const int canvasH = static_cast<int>(m_canvas.height());

    StrokeSnapshot snapshot;
    snapshot.layerId = layer->id;
    std::unordered_set<TileKey, TileKeyHash> dirtyKeys;

    for (const auto& [key, maskTile] : selectionMask.tiles()) {
        TileData* dstTile = targetGrid.getTile(key);
        if (!dstTile)
            continue;

        const uint8_t* maskPixels = maskTile.pixels();
        bool tileTouched = false;
        bool capturedBefore = false;

        for (uint32_t localY = 0; localY < TILE_SIZE; ++localY) {
            const int worldY = key.y * static_cast<int>(TILE_SIZE) + static_cast<int>(localY);
            if (clipToCanvas && (worldY < 0 || worldY >= canvasH))
                continue;
            for (uint32_t localX = 0; localX < TILE_SIZE; ++localX) {
                const int worldX = key.x * static_cast<int>(TILE_SIZE) + static_cast<int>(localX);
                if (clipToCanvas && (worldX < 0 || worldX >= canvasW))
                    continue;

                const uint32_t idx = (localY * TILE_SIZE + localX) * TILE_CHANNELS;
                const uint8_t maskA = maskPixels[idx + 3]; // RGBA8 selection coverage
                if (maskA == 0)
                    continue;

                float d[4];
                aether::readTilePixelF(*dstTile, localX, localY, d);
                if (d[0] <= 0.0f && d[1] <= 0.0f && d[2] <= 0.0f && d[3] <= 0.0f)
                    continue;

                if (!capturedBefore) {
                    auto& before = snapshot.beforeTiles[key];
                    before.resize(contentTileBytes);
                    std::memcpy(before.data(), dstTile->pixels(), contentTileBytes);
                    capturedBefore = true;
                }

                if (maskA == 255) {
                    const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    aether::writeTilePixelF(*dstTile, localX, localY, zero);
                    tileTouched = true;
                    continue;
                }

                const float keep = static_cast<float>(255 - maskA) / 255.0f;
                const float out[4] = { d[0] * keep, d[1] * keep, d[2] * keep, d[3] * keep };

                if (std::abs(out[0] - d[0]) < kClearEps && std::abs(out[1] - d[1]) < kClearEps
                    && std::abs(out[2] - d[2]) < kClearEps && std::abs(out[3] - d[3]) < kClearEps) {
                    continue;
                }
                aether::writeTilePixelF(*dstTile, localX, localY, out);
                tileTouched = true;
            }
        }

        if (!tileTouched)
            continue;
        if (dstTile->isEmpty())
            snapshot.removedTiles.insert(key);
        dirtyKeys.insert(key);
    }

    if (dirtyKeys.empty())
        return false;

    targetGrid.pruneEmpty();

    for (const auto& key : dirtyKeys) {
        if (snapshot.removedTiles.count(key)) {
            snapshot.afterTiles[key].assign(contentTileBytes, 0);
            continue;
        }
        const TileData* afterTile = targetGrid.getTile(key);
        if (!afterTile) {
            snapshot.afterTiles[key].assign(contentTileBytes, 0);
            snapshot.removedTiles.insert(key);
            continue;
        }
        auto& after = snapshot.afterTiles[key];
        after.resize(contentTileBytes);
        std::memcpy(after.data(), afterTile->pixels(), contentTileBytes);
    }

    std::vector<TileKey> dirtyVec(dirtyKeys.begin(), dirtyKeys.end());
    m_canvas.dirtyManager().onTilesDirtied(layer->id, dirtyVec);
    markBoardCompositionTilesDirty(layer->id, dirtyVec);
    emit contentRegionChanged(worldRectFromTileKeys(dirtyVec));
    emit contentTilesChanged(qPointsFromTileKeys(dirtyVec));
    for (const auto& key : dirtyKeys) {
        if (!snapshot.removedTiles.count(key))
            m_canvas.tilePositionIndex().addEntry(key, layer->id);
    }

    auto cmd = std::make_unique<DrawCommand>(
        &m_canvas, m_layerModel, std::move(snapshot), buildCurrentSelectionRestore());
    m_canvas.undoManager().push(std::move(cmd));
    if (m_layerModel)
        m_layerModel->notifyLayerDataChanged(layer->id);
    requestRender();
    return true;
}

bool OpenGLCanvasWidget::hasSelectionMask() const
{
    return m_selectionController && m_selectionController->hasSelectionMask();
}

bool OpenGLCanvasWidget::selectionBoundsWorld(QRectF& outBounds) const
{
    if (!m_selectionController || !m_selectionController->selectionBoundsWorld(outBounds)) {
        return false;
    }

    const TransformState* displayState = selectionDisplayTransformState();
    if (!displayState || outBounds.isEmpty()) {
        return true;
    }

    const Vector2 v1 = displayState->transformPoint(
        { static_cast<float>(outBounds.left()), static_cast<float>(outBounds.top()) });
    const Vector2 v2 = displayState->transformPoint(
        { static_cast<float>(outBounds.right()), static_cast<float>(outBounds.top()) });
    const Vector2 v3 = displayState->transformPoint(
        { static_cast<float>(outBounds.right()), static_cast<float>(outBounds.bottom()) });
    const Vector2 v4 = displayState->transformPoint(
        { static_cast<float>(outBounds.left()), static_cast<float>(outBounds.bottom()) });
    const QPointF p1(v1.x, v1.y);
    const QPointF p2(v2.x, v2.y);
    const QPointF p3(v3.x, v3.y);
    const QPointF p4(v4.x, v4.y);

    const qreal minX = qMin(qMin(p1.x(), p2.x()), qMin(p3.x(), p4.x()));
    const qreal maxX = qMax(qMax(p1.x(), p2.x()), qMax(p3.x(), p4.x()));
    const qreal minY = qMin(qMin(p1.y(), p2.y()), qMin(p3.y(), p4.y()));
    const qreal maxY = qMax(qMax(p1.y(), p2.y()), qMax(p3.y(), p4.y()));
    outBounds = QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).normalized();
    return true;
}

bool OpenGLCanvasWidget::fillSelectionWithColor(const QColor& color)
{
    return m_selectionController && m_selectionController->fillSelectionWithColor(color);
}

bool OpenGLCanvasWidget::clearSelectionContent()
{
    return m_selectionController && m_selectionController->clearSelectionContent();
}

bool OpenGLCanvasWidget::rasterizeSmartLayerById(const QUuid& layerId)
{
    if (!m_layerModel || layerId.isNull()) {
        return false;
    }
    auto* layer = m_layerModel->layerById(layerId);
    if (!layer || !layerRequiresRasterizationForPixelEdits(layer)) {
        return false;
    }

    notifyCanvasInteraction(true);
    rasterizeSmartLayer(layer);
    return true;
}

bool OpenGLCanvasWidget::clearLayerPixelContent(const QUuid& layerId)
{
    if (!m_layerModel || layerId.isNull()) {
        return false;
    }
    auto* layer = m_layerModel->layerById(layerId);
    if (!isLayerCanvasEditable(layer) || !layer->isRaster() || !layer->tileGrid) {
        return false;
    }
    notifyCanvasInteraction(true);

    auto& targetGrid = *layer->tileGrid;
    // Content snapshots sized by the grid's own format (RGBA8/16F/32F).
    const size_t contentTileBytes = aether::tileByteSize(targetGrid.format());
    const int cw = static_cast<int>(m_canvas.width());
    const int ch = static_cast<int>(m_canvas.height());
    if (cw <= 0 || ch <= 0) {
        return false;
    }

    StrokeSnapshot snapshot;
    snapshot.layerId = layer->id;
    std::unordered_set<TileKey, TileKeyHash> dirtyKeys;

    std::vector<TileKey> keys;
    keys.reserve(targetGrid.tiles().size());
    for (const auto& [key, _] : targetGrid.tiles()) {
        keys.push_back(key);
    }

    for (const TileKey& key : keys) {
        TileData* dstTile = targetGrid.getTile(key);
        if (!dstTile) {
            continue;
        }

        bool capturedBefore = false;
        bool tileTouched = false;

        for (uint32_t localY = 0; localY < TILE_SIZE; ++localY) {
            const int worldY = key.y * static_cast<int>(TILE_SIZE) + static_cast<int>(localY);
            if (worldY < 0 || worldY >= ch) {
                continue;
            }
            for (uint32_t localX = 0; localX < TILE_SIZE; ++localX) {
                const int worldX = key.x * static_cast<int>(TILE_SIZE) + static_cast<int>(localX);
                if (worldX < 0 || worldX >= cw) {
                    continue;
                }

                float d[4];
                aether::readTilePixelF(*dstTile, localX, localY, d);
                if (d[0] <= 0.0f && d[1] <= 0.0f && d[2] <= 0.0f && d[3] <= 0.0f) {
                    continue;
                }

                if (!capturedBefore) {
                    auto& before = snapshot.beforeTiles[key];
                    before.resize(contentTileBytes);
                    std::memcpy(before.data(), dstTile->pixels(), contentTileBytes);
                    capturedBefore = true;
                }

                const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                aether::writeTilePixelF(*dstTile, localX, localY, zero);
                tileTouched = true;
            }
        }

        if (!tileTouched) {
            continue;
        }

        dirtyKeys.insert(key);
        if (dstTile->isEmpty()) {
            snapshot.removedTiles.insert(key);
        }
    }

    if (dirtyKeys.empty()) {
        return false;
    }

    targetGrid.pruneEmpty();

    for (const auto& key : dirtyKeys) {
        if (snapshot.removedTiles.count(key)) {
            snapshot.afterTiles[key].assign(contentTileBytes, 0);
            continue;
        }
        const TileData* afterTile = targetGrid.getTile(key);
        if (!afterTile) {
            snapshot.afterTiles[key].assign(contentTileBytes, 0);
            snapshot.removedTiles.insert(key);
            continue;
        }
        auto& after = snapshot.afterTiles[key];
        after.resize(contentTileBytes);
        std::memcpy(after.data(), afterTile->pixels(), contentTileBytes);
    }

    std::vector<TileKey> dirtyVec(dirtyKeys.begin(), dirtyKeys.end());
    m_canvas.dirtyManager().onTilesDirtied(layer->id, dirtyVec);
    markBoardCompositionTilesDirty(layer->id, dirtyVec);
    emit contentRegionChanged(worldRectFromTileKeys(dirtyVec));
    emit contentTilesChanged(qPointsFromTileKeys(dirtyVec));
    for (const auto& key : dirtyKeys) {
        if (!snapshot.removedTiles.count(key)) {
            m_canvas.tilePositionIndex().addEntry(key, layer->id);
        }
    }

    auto cmd = std::make_unique<DrawCommand>(
        &m_canvas, m_layerModel, std::move(snapshot), buildCurrentSelectionRestore());
    m_canvas.undoManager().push(std::move(cmd));
    m_layerModel->notifyLayerDataChanged(layer->id);
    requestRender();
    return true;
}

bool OpenGLCanvasWidget::applyLayerMask(const QUuid& layerId)
{
    if (!m_layerModel || layerId.isNull()) {
        return false;
    }
    auto* layer = m_layerModel->layerById(layerId);
    if (!layer || !layer->hasMask() || !isLayerCanvasEditable(layer) || !layer->isRaster()
        || !layer->tileGrid) {
        return false;
    }

    const int cw = static_cast<int>(m_canvas.width());
    const int ch = static_cast<int>(m_canvas.height());
    if (cw <= 0 || ch <= 0) {
        return false;
    }

    notifyCanvasInteraction(true);

    TileGrid& pixels = *layer->tileGrid;
    // Content snapshots sized by the layer grid's own format (RGBA8/16F/32F);
    // the mask read in revealAt stays 8-bit getPixel (mask grids are RGBA8).
    const size_t contentTileBytes = aether::tileByteSize(pixels.format());
    const TileGrid* mask = layer->maskTileGrid();
    const int ts = static_cast<int>(TILE_SIZE);

    // Reveal of an absent mask tile = the grid's default-fill background reveal
    // (1.0 for a reveal-all mask, 0.0 for a hide-all/black background).
    float maskDefaultReveal = 1.0f;
    if (mask) {
        uint8_t dr = 0, dg = 0, db = 0, da = 0;
        mask->defaultFill(dr, dg, db, da);
        const float dlum = (0.299f * dr + 0.587f * dg + 0.114f * db) / 255.0f;
        maskDefaultReveal = qBound(0.0f, dlum + (1.0f - static_cast<float>(da) / 255.0f), 1.0f);
    }

    // Reveal at a (non-negative) world pixel, matching the compositor and the
    // mask thumbnail preview: reveal = clamp(lum(premult rgb) + (1 - cover), 0, 1).
    // A missing mask tile falls back to the grid background reveal.
    auto revealAt = [mask, ts, maskDefaultReveal](int worldX, int worldY) -> float {
        if (!mask) {
            return 1.0f;
        }
        const int tileX = worldX / ts;
        const int tileY = worldY / ts;
        const TileData* tile = mask->getTile(TileKey { tileX, tileY });
        if (!tile) {
            return maskDefaultReveal;
        }
        const uint32_t lx = static_cast<uint32_t>(worldX - tileX * ts);
        const uint32_t ly = static_cast<uint32_t>(worldY - tileY * ts);
        uint8_t pr = 0, pg = 0, pb = 0, a = 0;
        tile->getPixel(lx, ly, pr, pg, pb, a);
        const float lum = (0.299f * pr + 0.587f * pg + 0.114f * pb) / 255.0f;
        return qBound(0.0f, lum + (1.0f - static_cast<float>(a) / 255.0f), 1.0f);
    };

    // --- Bake the mask reveal into the layer pixels (premultiplied) ---
    StrokeSnapshot pixelSnapshot;
    pixelSnapshot.layerId = layer->id;
    pixelSnapshot.maskTarget = false;
    std::unordered_set<TileKey, TileKeyHash> dirtyKeys;

    std::vector<TileKey> keys;
    keys.reserve(pixels.tiles().size());
    for (const auto& [key, _] : pixels.tiles()) {
        keys.push_back(key);
    }

    for (const TileKey& key : keys) {
        TileData* dstTile = pixels.getTile(key);
        if (!dstTile) {
            continue;
        }

        bool capturedBefore = false;
        bool tileTouched = false;

        for (uint32_t localY = 0; localY < TILE_SIZE; ++localY) {
            const int worldY = key.y * ts + static_cast<int>(localY);
            if (worldY < 0 || worldY >= ch) {
                continue;
            }
            for (uint32_t localX = 0; localX < TILE_SIZE; ++localX) {
                const int worldX = key.x * ts + static_cast<int>(localX);
                if (worldX < 0 || worldX >= cw) {
                    continue;
                }

                float d[4];
                aether::readTilePixelF(*dstTile, localX, localY, d);
                if (d[0] <= 0.0f && d[1] <= 0.0f && d[2] <= 0.0f && d[3] <= 0.0f) {
                    continue;
                }

                const float reveal = revealAt(worldX, worldY);
                if (reveal >= 1.0f) {
                    continue; // fully revealed → pixel unchanged
                }

                if (!capturedBefore) {
                    auto& before = pixelSnapshot.beforeTiles[key];
                    before.resize(contentTileBytes);
                    std::memcpy(before.data(), dstTile->pixels(), contentTileBytes);
                    capturedBefore = true;
                }

                // Scale the premultiplied pixel by the mask reveal.
                const float out[4] = { d[0] * reveal, d[1] * reveal, d[2] * reveal, d[3] * reveal };
                aether::writeTilePixelF(*dstTile, localX, localY, out);
                tileTouched = true;
            }
        }

        if (!tileTouched) {
            continue;
        }

        dirtyKeys.insert(key);
        if (dstTile->isEmpty()) {
            pixelSnapshot.removedTiles.insert(key);
        }
    }

    pixels.pruneEmpty();

    for (const auto& key : dirtyKeys) {
        if (pixelSnapshot.removedTiles.count(key)) {
            pixelSnapshot.afterTiles[key].assign(contentTileBytes, 0);
            continue;
        }
        const TileData* afterTile = pixels.getTile(key);
        if (!afterTile) {
            pixelSnapshot.afterTiles[key].assign(contentTileBytes, 0);
            pixelSnapshot.removedTiles.insert(key);
            continue;
        }
        auto& after = pixelSnapshot.afterTiles[key];
        after.resize(contentTileBytes);
        std::memcpy(after.data(), afterTile->pixels(), contentTileBytes);
    }

    // --- Capture the mask tiles + flags, then detach the mask ---
    StrokeSnapshot maskSnapshot;
    maskSnapshot.layerId = layer->id;
    maskSnapshot.maskTarget = true;
    std::vector<TileKey> maskKeys;
    if (const TileGrid* mg = layer->maskTileGrid()) {
        maskKeys.reserve(mg->tiles().size());
        for (const auto& [key, tile] : mg->tiles()) {
            auto& before = maskSnapshot.beforeTiles[key];
            before.resize(TILE_BYTE_SIZE);
            std::memcpy(before.data(), tile.pixels(), TILE_BYTE_SIZE);
            maskSnapshot.removedTiles.insert(key);
            maskKeys.push_back(key);
        }
    }
    const bool maskEnabled = layer->maskEnabled;
    const bool maskLinked = layer->maskLinked;

    layer->clearMask();

    // Dirty/recomposite the union of changed pixel tiles and mask-covered
    // tiles (the latter change appearance now that the mask is gone).
    std::unordered_set<TileKey, TileKeyHash> allDirty = dirtyKeys;
    for (const TileKey& key : maskKeys) {
        allDirty.insert(key);
    }
    std::vector<TileKey> dirtyVec(allDirty.begin(), allDirty.end());
    if (!dirtyVec.empty()) {
        m_canvas.dirtyManager().onTilesDirtied(layer->id, dirtyVec);
        markBoardCompositionTilesDirty(layer->id, dirtyVec);
        emit contentRegionChanged(worldRectFromTileKeys(dirtyVec));
        emit contentTilesChanged(qPointsFromTileKeys(dirtyVec));
    }
    for (const auto& key : dirtyKeys) {
        if (!pixelSnapshot.removedTiles.count(key)) {
            m_canvas.tilePositionIndex().addEntry(key, layer->id);
        }
    }

    auto cmd = std::make_unique<ApplyMaskCommand>(&m_canvas, m_layerModel, layer->id,
        std::move(pixelSnapshot), std::move(maskSnapshot), maskEnabled, maskLinked);
    m_canvas.undoManager().push(std::move(cmd));
    m_layerModel->notifyLayerDataChanged(layer->id);
    requestRender();
    return true;
}

bool OpenGLCanvasWidget::invertLayerMask(const QUuid& layerId)
{
    if (!m_layerModel || layerId.isNull()) {
        return false;
    }
    auto* layer = m_layerModel->layerById(layerId);
    if (!layer || !layer->hasMask() || !layer->isRaster()) {
        return false;
    }
    TileGrid* mask = layer->maskTileGrid();
    if (!mask) {
        return false;
    }

    // Reveal of a mask texel: reveal = clamp(lum(premult rgb) + (1 - a), 0, 1).
    // Inverting the mask means reveal -> 1 - reveal everywhere, stored back as an
    // opaque gray (premult rgb = gray, a = 255) so reveal = gray value.
    auto invertedGray = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> uint8_t {
        const float reveal = qBound(0.0f,
            (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f
                + (1.0f - static_cast<float>(a) / 255.0f),
            1.0f);
        const float nv = qBound(0.0f, 1.0f - reveal, 1.0f);
        return static_cast<uint8_t>(nv * 255.0f + 0.5f);
    };

    auto snapshot = [](const TileGrid* grid) -> MaskGridState {
        MaskGridState s;
        s.defaultFill = grid->defaultFillPacked();
        for (const auto& [key, tile] : grid->tiles()) {
            MaskGridState::Tile t;
            t.key = key;
            if (tile.isSolid()) {
                t.solid = true;
                t.solidColor = tile.solidColorPacked();
            } else {
                t.bytes.resize(aether::TILE_BYTE_SIZE);
                std::memcpy(t.bytes.data(), tile.pixels(), aether::TILE_BYTE_SIZE);
            }
            s.tiles.push_back(std::move(t));
        }
        return s;
    };

    MaskGridState before = snapshot(mask);

    // Invert the infinite background.
    {
        uint8_t dr = 0, dg = 0, db = 0, da = 0;
        mask->defaultFill(dr, dg, db, da);
        const uint8_t nv = invertedGray(dr, dg, db, da);
        mask->setDefaultFill(nv, nv, nv, 255);
    }

    // Invert every existing tile in place (solids stay solid, painted tiles are
    // rewritten per pixel as opaque inverted gray).
    std::vector<TileKey> dirty;
    dirty.reserve(mask->tiles().size());
    for (auto& [key, tile] : mask->tiles()) {
        if (tile.isSolid()) {
            uint8_t sr = 0, sg = 0, sb = 0, sa = 0;
            tile.solidColor(sr, sg, sb, sa);
            const uint8_t nv = invertedGray(sr, sg, sb, sa);
            tile.setSolid(nv, nv, nv, 255);
        } else {
            uint8_t* px = tile.pixels();
            for (uint32_t i = 0; i < TILE_SIZE * TILE_SIZE; ++i) {
                const uint32_t idx = i * aether::TILE_CHANNELS;
                const uint8_t nv = invertedGray(px[idx + 0], px[idx + 1], px[idx + 2], px[idx + 3]);
                px[idx + 0] = nv;
                px[idx + 1] = nv;
                px[idx + 2] = nv;
                px[idx + 3] = 255;
            }
            tile.markDirty();
        }
        mask->markDirty(key);
        dirty.push_back(key);
    }

    MaskGridState after = snapshot(mask);

    layer->maskThumbnailDirty = true;

    auto cmd = std::make_unique<InvertMaskCommand>(m_layerModel, layer->id, std::move(before),
        std::move(after), [this]() { requestRender(); });
    m_canvas.undoManager().push(std::move(cmd));

    if (!dirty.empty()) {
        m_canvas.dirtyManager().onTilesDirtied(layer->id, dirty);
        markBoardCompositionTilesDirty(layer->id, dirty);
    }
    // Recomposite all content tiles (the background flip hides/reveals content
    // that has no painted mask tile) and refresh the panel row.
    m_layerModel->notifyLayerDataChanged(layer->id);
    requestRender();
    return true;
}

std::shared_ptr<TileGrid> OpenGLCanvasWidget::buildEffectShapedSelectionGrid(
    const ruwa::core::layers::LayerData* layer)
{
    if (!layer || !m_renderer) {
        return nullptr;
    }
    // Only raster layers carry a bakeable pixel grid + effect chain. Text and
    // smart/isolated layers keep the raw-content selection (their projected grid
    // is served by getCompositingGridForLayer as before).
    if (!layer->isRaster() || !layer->tileGrid || layer->effects.isEmpty()) {
        return nullptr;
    }

    GLCompositor* compositor = m_renderer->compositor();
    GLTileRenderer* tileRenderer = m_renderer->tileRenderer();
    if (!compositor || !tileRenderer) {
        return nullptr;
    }

    // Bake the effect chain into a throwaway clone of the raw content so the
    // layer's own pixels are never modified. The baked alpha matches what the
    // compositor renders for this layer, so a Ctrl+click content selection
    // traces the EFFECTED silhouette (twirl/blur/etc.) rather than the raw
    // pixels. Non-shape effects bake to the same alpha (harmless, just work).
    auto shaped = cloneTileGrid(layer->tileGrid.get());
    if (!shaped || shaped->empty()) {
        return nullptr;
    }

    std::vector<TileKey> touched;
    // The bake uploads tile textures, runs the chain on the GPU and reads the
    // result back — all needing this widget's GL context current (same as the
    // Apply Layer Effects menu path, which is likewise invoked outside paintGL).
    makeCurrent();
    const bool baked = compositor->bakeEffectsIntoGrid(
        *shaped, layer->effects, tileRenderer, /*beforeTileWrite=*/nullptr, touched);
    // The whole-layer distortion cache is cross-batch and keyed by grid address;
    // the clone is about to be freed, so drop its entry to avoid a stale key.
    compositor->dropWholeLayerCacheEntry(shaped.get());
    doneCurrent();
    if (!baked) {
        return nullptr;
    }
    return shaped;
}

bool OpenGLCanvasWidget::applyLayerEffects(const QUuid& layerId)
{
    if (!m_layerModel || layerId.isNull() || !m_renderer) {
        return false;
    }
    auto* layer = m_layerModel->layerById(layerId);
    if (!layer || !layer->isRaster() || !layer->tileGrid || layer->effects.isEmpty()) {
        return false;
    }

    GLCompositor* compositor = m_renderer->compositor();
    GLTileRenderer* tileRenderer = m_renderer->tileRenderer();
    if (!compositor || !tileRenderer) {
        return false;
    }

    notifyCanvasInteraction(true);

    TileGrid& grid = *layer->tileGrid;
    const size_t contentTileBytes = aether::tileByteSize(grid.format());

    StrokeSnapshot snapshot;
    snapshot.layerId = layer->id;

    auto beforeTileWrite = [&](const TileKey& key) {
        if (snapshot.beforeTiles.count(key)) {
            return;
        }
        auto& before = snapshot.beforeTiles[key];
        if (const TileData* tile = grid.getTile(key)) {
            before.resize(contentTileBytes);
            std::memcpy(before.data(), tile->pixels(), contentTileBytes);
        } else {
            before.assign(contentTileBytes, 0);
            snapshot.createdTiles.insert(key);
        }
    };

    const QList<ruwa::core::effects::LayerEffectState> beforeEffects = layer->effects;
    std::vector<TileKey> touched;

    // The bake uploads tile textures, runs the effect chain on the GPU and reads
    // the result back with glReadPixels — all of which need this widget's GL
    // context current. Unlike a live composite (which runs inside paintGL), this
    // is invoked straight from a menu action, so make the context current
    // explicitly. Without it every GL call is a no-op and the readback leaves the
    // zero-initialized staging buffer untouched, so each tile bakes to fully
    // transparent and pruneEmpty() then wipes the whole layer.
    makeCurrent();
    const bool baked = compositor->bakeEffectsIntoGrid(
        grid, beforeEffects, tileRenderer, beforeTileWrite, touched);
    doneCurrent();
    if (!baked) {
        return false;
    }

    std::unordered_set<TileKey, TileKeyHash> dirtyKeys(touched.begin(), touched.end());
    for (const TileKey& key : touched) {
        auto& after = snapshot.afterTiles[key];
        if (const TileData* tile = grid.getTile(key)) {
            after.resize(contentTileBytes);
            std::memcpy(after.data(), tile->pixels(), contentTileBytes);
        } else {
            after.assign(contentTileBytes, 0);
            snapshot.removedTiles.insert(key);
        }
    }

    std::vector<TileKey> dirtyVec(dirtyKeys.begin(), dirtyKeys.end());
    m_canvas.dirtyManager().onTilesDirtied(layer->id, dirtyVec);
    markBoardCompositionTilesDirty(layer->id, dirtyVec);
    emit contentRegionChanged(worldRectFromTileKeys(dirtyVec));
    emit contentTilesChanged(qPointsFromTileKeys(dirtyVec));
    for (const auto& key : dirtyKeys) {
        if (!snapshot.removedTiles.count(key)) {
            m_canvas.tilePositionIndex().addEntry(key, layer->id);
        }
    }

    // Clear the effect chain now that it is baked into the pixels above.
    m_layerModel->replaceLayerEffects(
        layer->id, QList<ruwa::core::effects::LayerEffectState>(), /*affectsDocumentResult=*/true);

    auto cmd = std::make_unique<ApplyLayerEffectsCommand>(
        &m_canvas, m_layerModel, layer->id, std::move(snapshot), beforeEffects);
    m_canvas.undoManager().push(std::move(cmd));

    requestRender();
    return true;
}

bool OpenGLCanvasWidget::fillLayerMaskFromActiveSelection(const QUuid& layerId)
{
    if (!m_layerModel || layerId.isNull() || !m_selectionController) {
        return false;
    }
    auto* layer = m_layerModel->layerById(layerId);
    if (!layer || !layer->hasMask()) {
        return false;
    }

    const auto& lasso = m_selectionController->lassoSelection();
    if (!lasso.hasSelection() || lasso.mask().empty()) {
        return false; // no selection → leave the mask reveal-all (plain add)
    }
    const TileGrid& sel = lasso.mask();
    TileGrid* mask = layer->maskTileGrid();
    if (!mask) {
        return false;
    }

    // Reveal = selection coverage. The mask stores premultiplied painted gray and
    // the compositor reads reveal = lum(rgb) + (1 - a); an opaque gray g gives
    // reveal = g. So write opaque gray = coverage: inside the selection (cov=255)
    // → white → reveal 1 (visible); soft edges become matching gray.
    //
    // Everything OUTSIDE the selection is hidden for free via the mask's hide-all
    // background (defaultFill = opaque black): every absent tile reads reveal 0, so
    // the infinite area beyond the selection costs no tiles at all, and only the
    // selection tiles carry painted coverage.
    mask->setDefaultFill(0, 0, 0, 255);

    std::vector<TileKey> dirty;
    dirty.reserve(static_cast<size_t>(sel.tiles().size()));
    std::vector<uint8_t> cov(static_cast<size_t>(TILE_SIZE) * TILE_SIZE);
    for (const auto& [key, selTile] : sel.tiles()) {
        // Gather this tile's coverage and detect a uniform value so a fully
        // covered (or fully empty) tile can be stored as a memory-free solid.
        uint8_t minCov = 255;
        uint8_t maxCov = 0;
        for (uint32_t ly = 0; ly < TILE_SIZE; ++ly) {
            for (uint32_t lx = 0; lx < TILE_SIZE; ++lx) {
                uint8_t sr = 0, sg = 0, sb = 0, sa = 0;
                selTile.getPixel(lx, ly, sr, sg, sb, sa);
                cov[ly * TILE_SIZE + lx] = sa; // coverage lives in the alpha channel
                minCov = std::min(minCov, sa);
                maxCov = std::max(maxCov, sa);
            }
        }

        if (maxCov == 0) {
            // Fully outside the selection — identical to the hide-all background,
            // so leave it absent (no tile, no memory).
            continue;
        }

        TileData& dst = mask->getOrCreateTile(key);
        if (minCov == maxCov) {
            // Uniform coverage → store as a solid tile (no 256 KB buffer).
            dst.setSolid(maxCov, maxCov, maxCov, 255);
        } else {
            for (uint32_t ly = 0; ly < TILE_SIZE; ++ly) {
                for (uint32_t lx = 0; lx < TILE_SIZE; ++lx) {
                    const uint8_t c = cov[ly * TILE_SIZE + lx];
                    dst.setPixel(lx, ly, c, c, c, 255);
                }
            }
        }
        dst.markDirty();
        mask->markDirty(key);
        dirty.push_back(key);
    }
    layer->maskThumbnailDirty = true;

    // Content lying outside the selection is recomposited (and thus hidden by the
    // black background) by the caller's notifyLayerDataChanged; here we only need
    // to dirty the painted selection tiles.
    if (!dirty.empty()) {
        m_canvas.dirtyManager().onTilesDirtied(layer->id, dirty);
        markBoardCompositionTilesDirty(layer->id, dirty);
        emit contentRegionChanged(worldRectFromTileKeys(dirty));
        emit contentTilesChanged(qPointsFromTileKeys(dirty));
    }
    return true;
}

bool OpenGLCanvasWidget::flipSelectionHorizontally()
{
    return startAnimatedSelectionFlip(true, false);
}

bool OpenGLCanvasWidget::flipSelectionVertically()
{
    return startAnimatedSelectionFlip(false, true);
}

bool OpenGLCanvasWidget::startAnimatedSelectionFlip(bool flipHorizontal, bool flipVertical)
{
    if (!flipHorizontal && !flipVertical)
        return false;
    if (m_autoApplyingTransform || m_pendingTransform.active)
        return false;
    if ((m_strokeHost && m_strokeHost->isDrawing()) || !m_selectionController)
        return false;

    const bool hasSelectionMask = m_selectionController->lassoSelection().hasSelection()
        && !m_selectionController->lassoSelection().mask().empty();
    if (!hasSelectionMask)
        return false;

    const uint64_t sequence = ++m_autoApplyTransformSequence;
    m_autoApplyingTransform = true;

    if (m_transformController.isActive()) {
        if (m_moveOnlyTransform || m_transformController.isDragging()) {
            m_autoApplyingTransform = false;
            return false;
        }
    } else {
        enterTransformMode();
        if (!m_transformController.isActive()) {
            m_autoApplyingTransform = false;
            return false;
        }
    }

    bool started = false;
    if (flipHorizontal) {
        started = m_transformController.animateFlipHorizontal() || started;
    }
    if (flipVertical) {
        started = m_transformController.animateFlipVertical() || started;
    }
    if (!started) {
        m_autoApplyingTransform = false;
        return false;
    }

    requestRender();

    QTimer::singleShot(kAutoFlipAnimationDurationMs, this, [this, sequence]() {
        if (!m_autoApplyingTransform || sequence != m_autoApplyTransformSequence) {
            return;
        }
        if (!m_transformController.isActive()) {
            m_autoApplyingTransform = false;
            return;
        }
        confirmTransform();
    });
    return true;
}

void OpenGLCanvasWidget::cancelFillPreview()
{
    const bool hadPreview = m_fillPreview.active || !m_fillPreview.dirtyKeys.empty()
        || m_activeFillWorkerRequest != 0;
    stopFillPreview();
    if (hadPreview) {
        requestRender();
    }
}

void OpenGLCanvasWidget::scheduleDeferredFillKickoff(const QUuid& layerId, FillAlgorithm algorithm,
    SelectionRestoreContext selectionRestore, FillOrigin origin, FillColor color,
    FillCanvasBounds canvasBounds, bool maskTarget, bool forceFinalResultOnly)
{
    if (m_pendingFillKickoff.pending) { }
    m_pendingFillKickoff.pending = true;
    ++m_pendingFillKickoff.sequence;
    m_pendingFillKickoff.layerId = layerId;
    m_pendingFillKickoff.algorithm = algorithm;
    m_pendingFillKickoff.selectionRestore = std::move(selectionRestore);
    m_pendingFillKickoff.origin = origin;
    m_pendingFillKickoff.color = color;
    m_pendingFillKickoff.canvasBounds = canvasBounds;
    m_pendingFillKickoff.maskTarget = maskTarget;
    m_pendingFillKickoff.forceFinalResultOnly = forceFinalResultOnly;
    syncFillProcessingLayerSignal();

    const uint64_t sequence = m_pendingFillKickoff.sequence;
    QTimer::singleShot(0, this, [this, sequence]() { executeDeferredFillKickoff(sequence); });
}

void OpenGLCanvasWidget::executeDeferredFillKickoff(uint64_t sequence)
{
    if (!m_pendingFillKickoff.pending || m_pendingFillKickoff.sequence != sequence) {
        return;
    }

    PendingFillKickoff kickoff = std::move(m_pendingFillKickoff);
    m_pendingFillKickoff = {};
    QElapsedTimer kickoffTimer;
    kickoffTimer.start();

    auto* layer = m_layerModel ? m_layerModel->layerById(kickoff.layerId) : nullptr;
    if (!isLayerCanvasEditable(layer) || !layer->isRaster()) {
        syncFillProcessingLayerSignal();
        return;
    }
    TileGrid* targetGrid = kickoff.maskTarget ? layer->maskTileGrid() : layer->tileGrid.get();
    if (!targetGrid) {
        syncFillProcessingLayerSignal();
        return;
    }

    const TileGrid* selectionMask
        = (m_selectionController && m_selectionController->lassoSelection().hasSelection()
              && !m_selectionController->lassoSelection().mask().empty())
        ? &m_selectionController->lassoSelection().mask()
        : nullptr;
    if (selectionMask && fillMaskAlphaAt(selectionMask, kickoff.origin.x, kickoff.origin.y) == 0) {
        syncFillProcessingLayerSignal();
        return;
    }

    QElapsedTimer layerSnapshotTimer;
    layerSnapshotTimer.start();
    FillPreviewRawTileMap layerSnapshotTiles = snapshotRawTiles<FillPreviewRawTileMap>(*targetGrid);

    FillPreviewRawTileMap selectionMaskTiles;
    if (selectionMask) {
        QElapsedTimer selectionSnapshotTimer;
        selectionSnapshotTimer.start();
        selectionMaskTiles = snapshotRawTiles<FillPreviewRawTileMap>(*selectionMask);
    }

    FillPreviewRawTileMap workerLayerSnapshotTiles;
    FillPreviewRawTileMap workerSelectionMaskTiles;
    SelectionRestoreContext workerSelectionRestore {};
    const bool useFillWorker = m_fillWorker && kickoff.algorithm != FillAlgorithm::Classic
        && selectionMaskTiles.empty() && !kickoff.maskTarget && !kickoff.forceFinalResultOnly;
    if (useFillWorker) {
        workerLayerSnapshotTiles = std::move(layerSnapshotTiles);
        workerSelectionMaskTiles = std::move(selectionMaskTiles);
        workerSelectionRestore = kickoff.selectionRestore;
    }

    startAsyncFillSession(layer->id, kickoff.algorithm,
        useFillWorker ? FillPreviewRawTileMap {} : std::move(layerSnapshotTiles),
        useFillWorker ? FillPreviewRawTileMap {} : std::move(selectionMaskTiles), {}, {}, {},
        std::move(kickoff.selectionRestore), kickoff.origin, kickoff.color, kickoff.canvasBounds,
        kickoff.maskTarget, kickoff.forceFinalResultOnly, useFillWorker);

    if (!useFillWorker) {
        return;
    }

    auto request = std::make_shared<FillWorker::Request>();
    request->sequence = ++m_fillWorkerRequestSequence;
    request->layerId = layer->id;
    request->algorithm = kickoff.algorithm;
    request->selectionRestore = std::move(workerSelectionRestore);
    request->layerSnapshotTiles = std::move(workerLayerSnapshotTiles);
    request->selectionMaskTiles = std::move(workerSelectionMaskTiles);
    request->origin = kickoff.origin;
    request->color = kickoff.color;
    request->canvasBounds = kickoff.canvasBounds;
    request->cancelState = std::make_shared<std::atomic<bool>>(false);

    m_fillWorkerCancelState = request->cancelState;
    m_activeFillWorkerRequest = request->sequence;
    syncFillProcessingLayerSignal();

    const bool submitted = QMetaObject::invokeMethod(
        m_fillWorker,
        [worker = m_fillWorker, request]() {
            if (worker) {
                worker->process(request);
            }
        },
        Qt::QueuedConnection);
    if (submitted) {
        return;
    }

    m_activeFillWorkerRequest = 0;
    m_fillWorkerCancelState.reset();
    syncFillProcessingLayerSignal();
}

void OpenGLCanvasWidget::initializeFillWorker()
{
    shutdownFillWorker();

    QOpenGLContext* shareContext = context();
    if (!shareContext || m_fillShaderDir.isEmpty()) {
        return;
    }

    auto surface = std::make_unique<QOffscreenSurface>();
    surface->setFormat(shareContext->format());
    surface->create();
    if (!surface->isValid()) {
        return;
    }

    auto thread = std::make_unique<QThread>();
    auto* worker = new FillWorker(shareContext, surface.get(), m_fillShaderDir, this);
    worker->moveToThread(thread.get());
    thread->start();

    m_fillWorkerSurface = std::move(surface);
    m_fillWorkerThread = std::move(thread);
    m_fillWorker = worker;
}

void OpenGLCanvasWidget::shutdownFillWorker()
{
    if (m_fillWorkerCancelState) {
        m_fillWorkerCancelState->store(true, std::memory_order_release);
    }
    m_fillWorkerCancelState.reset();
    m_activeFillWorkerRequest = 0;

    if (m_fillWorker) {
        if (m_fillWorkerThread && m_fillWorkerThread->isRunning()) {
            QMetaObject::invokeMethod(
                m_fillWorker, [worker = m_fillWorker]() { delete worker; },
                Qt::BlockingQueuedConnection);
        } else {
            delete m_fillWorker;
        }
        m_fillWorker = nullptr;
    }

    if (m_fillWorkerThread) {
        m_fillWorkerThread->quit();
        m_fillWorkerThread->wait();
        m_fillWorkerThread.reset();
    }

    m_fillWorkerSurface.reset();
}

void OpenGLCanvasWidget::prewarmOneTimeGpuPaths()
{
    if (!m_renderer) {
        return;
    }

    if (auto* fillRenderer = m_renderer->fillRenderer()) {
        fillRenderer->prewarmPreviewResources(std::max(1, static_cast<int>(m_canvas.width())),
            std::max(1, static_cast<int>(m_canvas.height())));
    }

    auto* tileRenderer = m_renderer->tileRenderer();
    auto* transformRenderer = m_renderer->transformRenderer();
    if (!tileRenderer || !transformRenderer) {
        return;
    }

    TileGrid sourceGrid = makeTechnicalWarmupGrid(255);
    m_renderer->uploadDirtyTiles(sourceGrid);
    transformRenderer->buildSourceAtlas(sourceGrid, tileRenderer);

    TileGrid maskGrid = makeTechnicalWarmupGrid(255);
    m_renderer->uploadDirtyTiles(maskGrid);
    transformRenderer->buildMaskAtlas(maskGrid, tileRenderer);
}

void OpenGLCanvasWidget::handleFillWorkerResult(uint64_t requestSequence, const QUuid& layerId,
    SelectionRestoreContext selectionRestore, FloodFillResult result, FillOrigin origin,
    FillColor color, FillCanvasBounds canvasBounds)
{
    const int originX = origin.x;
    const int originY = origin.y;
    Q_UNUSED(color);
    Q_UNUSED(canvasBounds);

    if (requestSequence == 0 || requestSequence != m_activeFillWorkerRequest) {
        return;
    }
    QElapsedTimer resultTimer;
    resultTimer.start();

    m_activeFillWorkerRequest = 0;
    m_fillWorkerCancelState.reset();

    auto* layer = m_layerModel ? m_layerModel->layerById(layerId) : nullptr;
    if (!isLayerCanvasEditable(layer) || !layer->isRaster()) {
        return;
    }
    TileGrid* targetGrid = m_fillPreview.maskTarget ? layer->maskTileGrid() : layer->tileGrid.get();
    if (!targetGrid) {
        return;
    }

    if (result.pixelsFilled <= 0 || result.fillMaskTiles.empty()) {
        return;
    }

    if (!m_fillPreview.active || m_fillPreview.targetLayerId != layerId) {
        return;
    }

    const int resultPixelsFilled = result.pixelsFilled;

    if (m_fillPreview.job) {
        m_fillPreview.job->cancelled.store(true, std::memory_order_release);
        m_fillPreview.job.reset();
    }

    m_fillPreview.selectionRestore = std::move(selectionRestore);
    m_fillPreview.queuedBatches.clear();
    m_fillPreview.queuedRevealSegments.clear();

    if (m_fillPreview.previewPaused) {
        const aether::FillPreviewRadiusRange resultRange
            = aether::computeFillPreviewRadiusRange(result.fillMaskTiles,
                Vector2 { static_cast<float>(originX) + 0.5f, static_cast<float>(originY) + 0.5f });
        m_fillPreview.radiusCap
            = resultRange.maxRadius > kFillPreviewPauseRadiusPx ? kFillPreviewPauseRadiusPx : 0.0f;
        if (m_fillPreview.radiusCap > 0.0f) {
            const float effectiveRadiusCap
                = std::max(m_fillPreview.radiusCap, m_fillPreview.displayRadius);
            m_fillPreview.readyRadius = std::min(m_fillPreview.readyRadius, effectiveRadiusCap);
            if (m_fillPreview.easeActive) {
                m_fillPreview.easeTargetRadius
                    = std::min(m_fillPreview.easeTargetRadius, effectiveRadiusCap);
            }
        }

        m_fillPreview.awaitingResult = false;
        m_fillPreview.pendingResult = std::move(result);
    } else {
        const float readyRadiusAtResult = m_fillPreview.readyRadius;
        auto job = std::make_shared<FillPreviewState::AsyncJob>();
        m_fillPreview.awaitingResult = true;
        m_fillPreview.pendingResult = {};
        m_fillPreview.job = job;

        std::thread([job, result = std::move(result), originX, originY,
                        readyRadiusAtResult]() mutable {
            if (job->cancelled.load(std::memory_order_acquire)) {
                return;
            }

            std::deque<FillPreviewState::ProgressBatch> preparedBatches
                = OpenGLCanvasWidget::buildFillPreviewBatches(result.afterTiles,
                    result.fillMaskTiles, originX, originY, readyRadiusAtResult, 0.0f);
            if (job->cancelled.load(std::memory_order_acquire)) {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(job->resultMutex);
                job->preparedRadiusCap = 0.0f;
                while (!preparedBatches.empty()) {
                    job->pendingBatches.push_back(std::move(preparedBatches.front()));
                    preparedBatches.pop_front();
                }
                job->result = std::move(result);
            }
            job->done.store(true, std::memory_order_release);
        }).detach();
    }

    if (!m_fillPreview.previewActive) {
        beginFillPreviewAnimation(FloodFillResult {});
    }
    requestRender();
}

void OpenGLCanvasWidget::startAsyncFillSession(const QUuid& layerId, FillAlgorithm algorithm,
    FillPreviewRawTileMap layerSnapshotTiles, FillPreviewRawTileMap selectionMaskTiles,
    FillPreviewRawTileMap initialPreviewTiles, FillPreviewRawTileMap initialMaskTiles,
    FloodFillResult initialPendingResult, SelectionRestoreContext selectionRestore,
    FillOrigin origin, FillColor color, FillCanvasBounds canvasBounds, bool maskTarget,
    bool forceFinalResultOnly, bool waitForExternalResultOnly)
{
    const int originX = origin.x;
    const int originY = origin.y;
    const uint8_t fillR = color.r;
    const uint8_t fillG = color.g;
    const uint8_t fillB = color.b;
    const uint8_t fillA = color.a;
    const int workOriginX = canvasBounds.workOriginX;
    const int workOriginY = canvasBounds.workOriginY;
    const int canvasW = canvasBounds.width;
    const int canvasH = canvasBounds.height;
    stopFillPreview(true);
    const bool finalResultOnly = waitForExternalResultOnly || forceFinalResultOnly
        || algorithm == FillAlgorithm::Classic
        || (algorithm == FillAlgorithm::Smart && !selectionMaskTiles.empty());
    // CONTENT tile format for this fill: document layers use the per-document
    // format; layer masks are RGBA8. Threaded into the worker so raw snapshot
    // tiles (sized by the grid format in snapshotRawTiles) are decoded correctly.
    // All content grids share the document format (Slice 2).
    const aether::TilePixelFormat contentFormat = maskTarget
        ? aether::TilePixelFormat::RGBA8
        : (m_layerModel ? m_layerModel->documentTileFormat() : aether::kDefaultTileFormat);
    m_fillPreview.active = true;
    m_fillPreview.algorithm = algorithm;
    m_fillPreview.finalResultOnly = finalResultOnly;
    m_fillPreview.maskTarget = maskTarget;
    m_fillPreview.contentFormat = contentFormat;
    m_fillPreview.previewActive = false;
    m_fillPreview.previewPaused = false;
    m_fillPreview.easeActive = false;
    m_fillPreview.targetLayerId = layerId;
    m_fillPreview.previewContentGrid.reset();
    m_fillPreview.fillMaskGrid.reset();
    m_fillPreview.dirtyKeys.clear();
    m_fillPreview.queuedBatches.clear();
    m_fillPreview.queuedRevealSegments.clear();
    m_fillPreview.origin
        = Vector2 { static_cast<float>(originX) + 0.5f, static_cast<float>(originY) + 0.5f };
    m_fillPreview.radiusCap = 0.0f;
    m_fillPreview.readyRadius = 0.0f;
    m_fillPreview.displayRadius = 0.0f;
    m_fillPreview.revealSpeedPxPerMs = 0.0f;
    m_fillPreview.easeStartRadius = 0.0f;
    m_fillPreview.easeTargetRadius = 0.0f;
    resetFillPreviewMetrics();
    m_fillPreview.timer.restart();
    m_fillPreview.lastAnimationMs = 0;
    m_fillPreview.easeStartMs = 0;
    m_fillPreview.lastPreviewGateLogMs = -1;
    m_fillPreview.lastPreviewGateReason = 0;
    m_fillPreview.pendingResult = {};
    m_fillPreview.selectionRestore = std::move(selectionRestore);
    m_fillPreview.compositingState = {};
    syncFillProcessingLayerSignal();

    const bool hasInitialPreview = !initialMaskTiles.empty();
    const bool hasInitialPendingResult = initialPendingResult.pixelsFilled > 0;
    m_fillPreview.awaitingResult = !hasInitialPendingResult;
    if (hasInitialPendingResult) {
        m_fillPreview.pendingResult = std::move(initialPendingResult);
    }
    m_fillPreview.job.reset();

    if (hasInitialPreview) {
        enqueueFillPreviewBatches(initialPreviewTiles, initialMaskTiles, originX, originY);
        applyPendingFillPreviewBatches();
    }

    if (hasInitialPendingResult) {
        requestRender();
        return;
    }

    if (waitForExternalResultOnly) {
        requestRender();
        return;
    }

    if (finalResultOnly) {
        auto job = std::make_shared<FillPreviewState::AsyncJob>();
        m_fillPreview.job = job;

        std::thread([job, layerSnapshotTiles = std::move(layerSnapshotTiles), algorithm,
                        selectionMaskTiles = std::move(selectionMaskTiles), originX, originY, fillR,
                        fillG, fillB, fillA, workOriginX, workOriginY, canvasW, canvasH,
                        contentFormat]() mutable {
            if (job->cancelled.load(std::memory_order_acquire)) {
                return;
            }

            QElapsedTimer totalTimer;
            totalTimer.start();

            const bool clipSmartResultToSelection
                = algorithm == FillAlgorithm::Smart && !selectionMaskTiles.empty();
            int localOffsetX = 0;
            int localOffsetY = 0;
            int localCanvasW = canvasW;
            int localCanvasH = canvasH;
            FillPreviewRawTileMap localLayerSnapshotTiles;
            FillPreviewRawTileMap localSelectionMaskTiles;
            const FillPreviewRawTileMap* layerTilesForFill = &layerSnapshotTiles;
            const FillPreviewRawTileMap* selectionTilesForFill = &selectionMaskTiles;
            if (workOriginX != 0 || workOriginY != 0) {
                localOffsetX = workOriginX;
                localOffsetY = workOriginY;
                localLayerSnapshotTiles = extractRawTilesRegion(layerSnapshotTiles, localOffsetX,
                    localOffsetY, localCanvasW, localCanvasH, false, contentFormat);
                if (!selectionMaskTiles.empty()) {
                    localSelectionMaskTiles
                        = extractRawTilesRegion(selectionMaskTiles, localOffsetX, localOffsetY,
                            localCanvasW, localCanvasH, true, aether::TilePixelFormat::RGBA8);
                }
                layerTilesForFill = &localLayerSnapshotTiles;
                selectionTilesForFill = &localSelectionMaskTiles;
            }
            if (clipSmartResultToSelection) {
                QElapsedTimer boundsTimer;
                boundsTimer.start();
                int selectionOffsetX = 0;
                int selectionOffsetY = 0;
                int selectionCanvasW = localCanvasW;
                int selectionCanvasH = localCanvasH;
                if (!aether::computeRawMaskPixelBounds(*selectionTilesForFill, localCanvasW,
                        localCanvasH, selectionOffsetX, selectionOffsetY, selectionCanvasW,
                        selectionCanvasH)) {
                    return;
                }

                QElapsedTimer extractTimer;
                extractTimer.start();
                localLayerSnapshotTiles
                    = extractRawTilesRegion(*layerTilesForFill, selectionOffsetX, selectionOffsetY,
                        selectionCanvasW, selectionCanvasH, false, contentFormat);
                localSelectionMaskTiles = extractRawTilesRegion(*selectionTilesForFill,
                    selectionOffsetX, selectionOffsetY, selectionCanvasW, selectionCanvasH, true,
                    aether::TilePixelFormat::RGBA8);
                localOffsetX += selectionOffsetX;
                localOffsetY += selectionOffsetY;
                localCanvasW = selectionCanvasW;
                localCanvasH = selectionCanvasH;
                layerTilesForFill = &localLayerSnapshotTiles;
                selectionTilesForFill = &localSelectionMaskTiles;
            }
            QElapsedTimer fillTimer;
            fillTimer.start();
            FloodFillResult result = algorithm == FillAlgorithm::Classic
                ? classicFloodFillRawTiles(*layerTilesForFill, originX - localOffsetX,
                      originY - localOffsetY, fillR, fillG, fillB, fillA, *selectionTilesForFill,
                      localCanvasW, localCanvasH, contentFormat)
                : floodFillRawTiles(*layerTilesForFill, originX - localOffsetX,
                      originY - localOffsetY, fillR, fillG, fillB, fillA,
                      clipSmartResultToSelection ? FloodFillResult::RawTileMap {}
                                                 : *selectionTilesForFill,
                      localCanvasW, localCanvasH, contentFormat);

            if (clipSmartResultToSelection && result.pixelsFilled > 0) {
                QElapsedTimer clipTimer;
                clipTimer.start();
                aether::clipFloodFillResultToSelectionMask(
                    *layerTilesForFill, *selectionTilesForFill, result, contentFormat);
                aether::translateFloodFillResultToWorld(
                    layerSnapshotTiles, localOffsetX, localOffsetY, result, contentFormat);
            } else if ((localOffsetX != 0 || localOffsetY != 0) && result.pixelsFilled > 0) {
                aether::translateFloodFillResultToWorld(
                    layerSnapshotTiles, localOffsetX, localOffsetY, result, contentFormat);
            }

            if (job->cancelled.load(std::memory_order_acquire)) {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(job->resultMutex);
                job->result = std::move(result);
            }
            job->done.store(true, std::memory_order_release);
        }).detach();

        requestRender();
        return;
    }

    auto job = std::make_shared<FillPreviewState::AsyncJob>();
    m_fillPreview.job = job;

    std::thread([job, layerSnapshotTiles = std::move(layerSnapshotTiles),
                    selectionMaskTiles = std::move(selectionMaskTiles), algorithm,
                    hasInitialPreview, originX, originY, fillR, fillG, fillB, fillA, canvasW,
                    canvasH, contentFormat]() mutable {
        if (job->cancelled.load(std::memory_order_acquire)) {
            return;
        }

        if (!aether::progressiveWithinCanvas(originX, originY, canvasW, canvasH)) {
            job->done.store(true, std::memory_order_release);
            return;
        }

        const int canvasMaxTileX = std::max(0, (canvasW - 1) / static_cast<int>(TILE_SIZE));
        const int canvasMaxTileY = std::max(0, (canvasH - 1) / static_cast<int>(TILE_SIZE));
        const int seedTileX = originX / static_cast<int>(TILE_SIZE);
        const int seedTileY = originY / static_cast<int>(TILE_SIZE);
        const int maxTileRadius = std::max(std::max(seedTileX, canvasMaxTileX - seedTileX),
            std::max(seedTileY, canvasMaxTileY - seedTileY));
        float previousReadyRadius = 0.0f;
        if (!hasInitialPreview) {
            const int startTileRadius = std::min(1, maxTileRadius);
            const aether::PremultPixel seedPixel
                = aether::sampleRawPixel(layerSnapshotTiles, originX, originY, contentFormat);

            FloodFillResult previewInteriorResult;
            std::vector<std::deque<std::pair<int, int>>> queuedSeedsByRadius(
                static_cast<size_t>(maxTileRadius + 1));
            std::unordered_set<uint64_t> seenSeedPoints;
            std::deque<std::pair<int, int>> activeSeeds;

            auto enqueueSeed = [&](int x, int y, int activeRadius) {
                if (!aether::canFillProgressivePixel(layerSnapshotTiles, selectionMaskTiles,
                        previewInteriorResult.fillMaskTiles, seedPixel, algorithm, x, y, canvasW,
                        canvasH, contentFormat)) {
                    return;
                }

                const uint64_t seedKey = progressiveSeedKey(x, y);
                if (!seenSeedPoints.insert(seedKey).second) {
                    return;
                }

                const int tileRadius = progressiveTileRadius(x, y, seedTileX, seedTileY);
                if (tileRadius > maxTileRadius) {
                    return;
                }

                if (tileRadius <= activeRadius) {
                    activeSeeds.emplace_back(x, y);
                } else {
                    queuedSeedsByRadius[static_cast<size_t>(tileRadius)].emplace_back(x, y);
                }
            };

            enqueueSeed(originX, originY, startTileRadius);
            int nextQueuedRadius = 0;

            for (int tileRadius = startTileRadius; tileRadius <= maxTileRadius; ++tileRadius) {
                if (job->cancelled.load(std::memory_order_acquire)) {
                    return;
                }

                while (nextQueuedRadius <= tileRadius && nextQueuedRadius <= maxTileRadius) {
                    auto& queued = queuedSeedsByRadius[static_cast<size_t>(nextQueuedRadius)];
                    while (!queued.empty()) {
                        activeSeeds.push_back(queued.front());
                        queued.pop_front();
                    }
                    ++nextQueuedRadius;
                }

                std::unordered_set<TileKey, TileKeyHash> ringTouchedKeys;
                ringTouchedKeys.reserve(16);

                auto processSeed = [&](int seedX, int seedY) {
                    if (!aether::canFillProgressivePixel(layerSnapshotTiles, selectionMaskTiles,
                            previewInteriorResult.fillMaskTiles, seedPixel, algorithm, seedX, seedY,
                            canvasW, canvasH, contentFormat)) {
                        return;
                    }

                    int left = seedX;
                    while (left > 0
                        && progressiveTileRadius(left - 1, seedY, seedTileX, seedTileY)
                            <= tileRadius
                        && aether::canFillProgressivePixel(layerSnapshotTiles, selectionMaskTiles,
                            previewInteriorResult.fillMaskTiles, seedPixel, algorithm, left - 1,
                            seedY, canvasW, canvasH, contentFormat)) {
                        --left;
                    }

                    int right = seedX;
                    while (right + 1 < canvasW
                        && progressiveTileRadius(right + 1, seedY, seedTileX, seedTileY)
                            <= tileRadius
                        && aether::canFillProgressivePixel(layerSnapshotTiles, selectionMaskTiles,
                            previewInteriorResult.fillMaskTiles, seedPixel, algorithm, right + 1,
                            seedY, canvasW, canvasH, contentFormat)) {
                        ++right;
                    }

                    for (int x = left; x <= right; ++x) {
                        if (!aether::canFillProgressivePixel(layerSnapshotTiles, selectionMaskTiles,
                                previewInteriorResult.fillMaskTiles, seedPixel, algorithm, x, seedY,
                                canvasW, canvasH, contentFormat)) {
                            continue;
                        }

                        aether::writeProgressiveFillPixel(layerSnapshotTiles, previewInteriorResult,
                            x, seedY, fillR, fillG, fillB, fillA, false, contentFormat);

                        ringTouchedKeys.insert(TileKey {
                            x / static_cast<int>(TILE_SIZE), seedY / static_cast<int>(TILE_SIZE) });
                    }

                    auto enqueueNeighborRow = [&](int rowY) {
                        if (rowY < 0 || rowY >= canvasH) {
                            return;
                        }

                        int x = std::max(0, left - 1);
                        const int scanRight = std::min(canvasW - 1, right + 1);
                        int previousRadiusBand = -1;
                        while (x <= scanRight) {
                            if (!aether::canFillProgressivePixel(layerSnapshotTiles,
                                    selectionMaskTiles, previewInteriorResult.fillMaskTiles,
                                    seedPixel, algorithm, x, rowY, canvasW, canvasH,
                                    contentFormat)) {
                                previousRadiusBand = -1;
                                ++x;
                                continue;
                            }

                            const int radiusBand
                                = progressiveTileRadius(x, rowY, seedTileX, seedTileY);
                            if (radiusBand != previousRadiusBand) {
                                enqueueSeed(x, rowY, tileRadius);
                                previousRadiusBand = radiusBand;
                            }
                            ++x;
                        }
                    };

                    enqueueNeighborRow(seedY - 1);
                    enqueueNeighborRow(seedY + 1);
                    enqueueSeed(left - 1, seedY, tileRadius);
                    enqueueSeed(right + 1, seedY, tileRadius);
                };

                while (!activeSeeds.empty()) {
                    const auto [seedX, seedY] = activeSeeds.front();
                    activeSeeds.pop_front();
                    processSeed(seedX, seedY);
                }

                if (!ringTouchedKeys.empty()) {
                    FillPreviewState::ProgressBatch batch;
                    batch.keys.reserve(ringTouchedKeys.size());
                    aether::progressiveBoundsFromKeys(ringTouchedKeys, batch.minTileX,
                        batch.minTileY, batch.maxTileX, batch.maxTileY);

                    for (const TileKey& key : ringTouchedKeys) {
                        batch.keys.push_back(key);
                        auto previewIt = previewInteriorResult.afterTiles.find(key);
                        if (previewIt != previewInteriorResult.afterTiles.end()) {
                            batch.previewTiles.emplace(key, previewIt->second);
                        }
                        auto maskIt = previewInteriorResult.fillMaskTiles.find(key);
                        if (maskIt != previewInteriorResult.fillMaskTiles.end()) {
                            batch.maskTiles.emplace(key, maskIt->second);
                        }
                    }

                    if (!batch.previewTiles.empty() || !batch.maskTiles.empty()) {
                        const Vector2 previewOrigin { static_cast<float>(originX) + 0.5f,
                            static_cast<float>(originY) + 0.5f };
                        const aether::FillPreviewRadiusRange batchRange
                            = aether::computeFillPreviewRadiusRange(batch.maskTiles, previewOrigin);

                        bool hasFutureSeeds = false;
                        int nextFutureRadius = -1;
                        for (int futureRadius = std::max(tileRadius + 1, nextQueuedRadius);
                            futureRadius <= maxTileRadius; ++futureRadius) {
                            if (!queuedSeedsByRadius[static_cast<size_t>(futureRadius)].empty()) {
                                hasFutureSeeds = true;
                                nextFutureRadius = futureRadius;
                                break;
                            }
                        }
                        const bool terminalPreviewBatch = !hasFutureSeeds;

                        batch.tileStartRadius
                            = aether::fillPreviewTileStartRadius(batch.keys, previewOrigin);
                        batch.contentMinRadius = batchRange.minRadius;
                        batch.contentMaxRadius = batchRange.maxRadius;
                        batch.pixelCount = batchRange.pixelCount;
                        if (algorithm == FillAlgorithm::Smart) {
                            batch.minRadius = std::max(previousReadyRadius, batch.tileStartRadius);

                            float batchTargetRadius = batch.contentMaxRadius;
                            if (!terminalPreviewBatch && nextFutureRadius >= 0) {
                                std::unordered_set<TileKey, TileKeyHash> frontierKeys;
                                for (const auto& seed :
                                    queuedSeedsByRadius[static_cast<size_t>(nextFutureRadius)]) {
                                    frontierKeys.insert(
                                        TileKey { seed.first / static_cast<int>(TILE_SIZE),
                                            seed.second / static_cast<int>(TILE_SIZE) });
                                }
                                if (!frontierKeys.empty()) {
                                    std::vector<TileKey> frontierKeyVec(
                                        frontierKeys.begin(), frontierKeys.end());
                                    batchTargetRadius = aether::fillPreviewTileStartRadius(
                                        frontierKeyVec, previewOrigin);
                                }
                            }
                            batch.maxRadius = std::max(batch.minRadius, batchTargetRadius);
                        } else {
                            batch.minRadius = std::max(previousReadyRadius, batchRange.minRadius);
                            batch.maxRadius = std::max(batch.minRadius, batchRange.maxRadius);
                        }

                        if (batch.maxRadius > batch.minRadius + 0.01f) {
                            std::lock_guard<std::mutex> lock(job->resultMutex);
                            previousReadyRadius = batch.maxRadius;
                            job->pendingBatches.push_back(std::move(batch));
                        }
                    }
                }

                bool hasFutureSeeds = false;
                for (int futureRadius = std::max(tileRadius + 1, nextQueuedRadius);
                    futureRadius <= maxTileRadius; ++futureRadius) {
                    if (!queuedSeedsByRadius[static_cast<size_t>(futureRadius)].empty()) {
                        hasFutureSeeds = true;
                        break;
                    }
                }

                if (!hasFutureSeeds && activeSeeds.empty()) {
                    break;
                }
            }
        }

        FloodFillResult finalResult = algorithm == FillAlgorithm::Classic
            ? classicFloodFillRawTiles(layerSnapshotTiles, originX, originY, fillR, fillG, fillB,
                  fillA, selectionMaskTiles, canvasW, canvasH, contentFormat)
            : floodFillRawTiles(layerSnapshotTiles, originX, originY, fillR, fillG, fillB, fillA,
                  selectionMaskTiles, canvasW, canvasH, contentFormat);

        if (job->cancelled.load(std::memory_order_acquire)) {
            return;
        }

        if (!finalResult.fillMaskTiles.empty()) {
            std::deque<FillPreviewState::ProgressBatch> preparedBatches
                = OpenGLCanvasWidget::buildFillPreviewBatches(finalResult.afterTiles,
                    finalResult.fillMaskTiles, originX, originY, previousReadyRadius, 0.0f);
            if (job->cancelled.load(std::memory_order_acquire)) {
                return;
            }

            std::lock_guard<std::mutex> lock(job->resultMutex);
            while (!preparedBatches.empty()) {
                previousReadyRadius = preparedBatches.front().maxRadius;
                job->pendingBatches.push_back(std::move(preparedBatches.front()));
                preparedBatches.pop_front();
            }
        }

        if (job->cancelled.load(std::memory_order_acquire)) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(job->resultMutex);
            job->result = std::move(finalResult);
        }
        job->done.store(true, std::memory_order_release);
    }).detach();

    requestRender();
}

std::deque<OpenGLCanvasWidget::FillPreviewState::ProgressBatch>
OpenGLCanvasWidget::buildFillPreviewBatches(const FillPreviewRawTileMap& previewTiles,
    const FillPreviewRawTileMap& maskTiles, int originX, int originY, float readyRadius,
    float radiusCap)
{
    std::deque<FillPreviewState::ProgressBatch> queuedBatches;
    if (maskTiles.empty()) {
        return queuedBatches;
    }

    const int seedTileX = originX / static_cast<int>(TILE_SIZE);
    const int seedTileY = originY / static_cast<int>(TILE_SIZE);
    std::unordered_map<int, std::vector<TileKey>> keysByRadius;
    keysByRadius.reserve(maskTiles.size());
    for (const auto& [key, _] : maskTiles) {
        const int tileRadius = std::max(std::abs(key.x - seedTileX), std::abs(key.y - seedTileY));
        keysByRadius[tileRadius].push_back(key);
    }
    if (keysByRadius.empty()) {
        return queuedBatches;
    }

    std::vector<int> sortedRadii;
    sortedRadii.reserve(keysByRadius.size());
    for (const auto& [tileRadius, keys] : keysByRadius) {
        if (!keys.empty()) {
            sortedRadii.push_back(tileRadius);
        }
    }
    if (sortedRadii.empty()) {
        return queuedBatches;
    }
    std::sort(sortedRadii.begin(), sortedRadii.end());

    const bool hasRadiusCap = radiusCap > 0.0f;
    float previousReadyRadius = hasRadiusCap ? std::min(readyRadius, radiusCap) : readyRadius;
    const Vector2 origin { static_cast<float>(originX) + 0.5f, static_cast<float>(originY) + 0.5f };
    const aether::FillPreviewRadiusRange finalRange
        = aether::computeFillPreviewRadiusRange(maskTiles, origin);

    std::vector<FillPreviewState::ProgressBatch> preparedBatches(sortedRadii.size());
    std::vector<uint8_t> validBatches(sortedRadii.size(), 0);

    aether::parallelForFillPreviewChunks(
        sortedRadii.size(), 1, [&](size_t, size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                const int tileRadius = sortedRadii[i];
                const auto keysIt = keysByRadius.find(tileRadius);
                if (keysIt == keysByRadius.end() || keysIt->second.empty()) {
                    continue;
                }

                FillPreviewState::ProgressBatch batch;
                batch.keys = keysIt->second;
                aether::progressiveBoundsFromKeys(
                    batch.keys, batch.minTileX, batch.minTileY, batch.maxTileX, batch.maxTileY);
                batch.tileStartRadius = aether::fillPreviewTileStartRadius(batch.keys, origin);
                batch.previewTiles.reserve(batch.keys.size());
                batch.maskTiles.reserve(batch.keys.size());

                for (const TileKey& key : batch.keys) {
                    auto previewIt = previewTiles.find(key);
                    if (previewIt != previewTiles.end()) {
                        batch.previewTiles.emplace(key, previewIt->second);
                    }
                    auto maskIt = maskTiles.find(key);
                    if (maskIt != maskTiles.end()) {
                        batch.maskTiles.emplace(key, maskIt->second);
                    }
                }

                if (batch.previewTiles.empty() && batch.maskTiles.empty()) {
                    continue;
                }

                const aether::FillPreviewRadiusRange batchRange
                    = aether::computeFillPreviewRadiusRange(batch.maskTiles, origin);
                if (batchRange.pixelCount <= 0) {
                    continue;
                }

                batch.contentMinRadius = batchRange.minRadius;
                batch.contentMaxRadius = batchRange.maxRadius;
                batch.pixelCount = batchRange.pixelCount;
                preparedBatches[i] = std::move(batch);
                validBatches[i] = 1;
            }
        });

    ptrdiff_t lastValidIndex = -1;
    for (ptrdiff_t i = static_cast<ptrdiff_t>(sortedRadii.size()) - 1; i >= 0; --i) {
        if (validBatches[static_cast<size_t>(i)] != 0) {
            lastValidIndex = i;
            break;
        }
    }
    if (lastValidIndex < 0) {
        return queuedBatches;
    }

    for (size_t i = 0; i < sortedRadii.size(); ++i) {
        if (validBatches[i] == 0) {
            continue;
        }

        FillPreviewState::ProgressBatch& batch = preparedBatches[i];
        const bool terminalBatch = static_cast<ptrdiff_t>(i) == lastValidIndex;
        batch.minRadius = std::max(previousReadyRadius, batch.tileStartRadius);
        float batchTargetRadius = finalRange.maxRadius;
        if (!terminalBatch) {
            for (size_t nextIndex = i + 1; nextIndex < sortedRadii.size(); ++nextIndex) {
                if (validBatches[nextIndex] != 0) {
                    batchTargetRadius = preparedBatches[nextIndex].tileStartRadius;
                    break;
                }
            }
        }
        if (hasRadiusCap) {
            batchTargetRadius = std::min(batchTargetRadius, radiusCap);
        }
        batch.maxRadius = std::max(batch.minRadius, batchTargetRadius);

        if (batch.maxRadius > batch.minRadius + 0.01f) {
            previousReadyRadius = batch.maxRadius;
            queuedBatches.push_back(std::move(batch));
        }
    }

    return queuedBatches;
}

void OpenGLCanvasWidget::resetFillPreviewMetrics()
{
    m_fillPreview.appliedPixelCount = 0;
    m_fillPreview.appliedMinRadius = 0.0f;
    m_fillPreview.appliedMaxRadius = 0.0f;
    m_fillPreview.minRevealRadius = 0.0f;
    m_fillPreview.feather = 0.0f;
    m_fillPreview.durationMs = 0;
    m_fillPreview.metricsDirty = false;
}

void OpenGLCanvasWidget::rebuildFillPreviewMetricsFromGrid()
{
    if (!m_fillPreview.fillMaskGrid || m_fillPreview.fillMaskGrid->empty()) {
        resetFillPreviewMetrics();
        return;
    }

    const aether::FillPreviewRadiusRange radiusRange
        = aether::computeFillPreviewRadiusRange(*m_fillPreview.fillMaskGrid, m_fillPreview.origin);
    const aether::FillPreviewMetrics metrics
        = aether::computeFillPreviewMetrics(*m_fillPreview.fillMaskGrid, m_fillPreview.origin);
    m_fillPreview.appliedPixelCount = metrics.pixelCount;
    m_fillPreview.appliedMinRadius = radiusRange.minRadius;
    m_fillPreview.appliedMaxRadius = metrics.maxRadius;
    m_fillPreview.minRevealRadius = radiusRange.minRadius;
    m_fillPreview.feather = aether::fillPreviewFeatherPx(metrics);
    m_fillPreview.durationMs = aether::fillPreviewDurationMs(metrics);
    m_fillPreview.metricsDirty = false;
}

bool OpenGLCanvasWidget::updateFillPreviewMetricsFromBatch(
    const FillPreviewState::ProgressBatch& batch)
{
    if (batch.pixelCount <= 0) {
        return false;
    }

    if (m_fillPreview.metricsDirty) {
        rebuildFillPreviewMetricsFromGrid();
        return true;
    }

    if (m_fillPreview.appliedPixelCount <= 0) {
        m_fillPreview.appliedPixelCount = batch.pixelCount;
        m_fillPreview.appliedMinRadius = batch.contentMinRadius;
        m_fillPreview.appliedMaxRadius = batch.contentMaxRadius;
    } else {
        m_fillPreview.appliedPixelCount += batch.pixelCount;
        m_fillPreview.appliedMinRadius
            = std::min(m_fillPreview.appliedMinRadius, batch.contentMinRadius);
        m_fillPreview.appliedMaxRadius
            = std::max(m_fillPreview.appliedMaxRadius, batch.contentMaxRadius);
    }

    const aether::FillPreviewMetrics metrics { m_fillPreview.appliedMaxRadius,
        m_fillPreview.appliedPixelCount };
    m_fillPreview.minRevealRadius = m_fillPreview.appliedMinRadius;
    m_fillPreview.feather = aether::fillPreviewFeatherPx(metrics);
    m_fillPreview.durationMs = aether::fillPreviewDurationMs(metrics);
    return true;
}

size_t OpenGLCanvasWidget::applyFillPreviewBatchBudget(size_t maxBatchCount, qint64 maxElapsedMs)
{
    if (maxBatchCount == 0) {
        return 0;
    }

    QElapsedTimer budgetTimer;
    if (maxElapsedMs > 0) {
        budgetTimer.start();
    }

    size_t appliedCount = 0;
    while (appliedCount < maxBatchCount) {
        if (!applyPendingFillPreviewBatches()) {
            break;
        }
        ++appliedCount;

        if (maxElapsedMs > 0 && budgetTimer.elapsed() >= maxElapsedMs) {
            break;
        }
    }

    return appliedCount;
}

void OpenGLCanvasWidget::enqueueFillPreviewBatches(const FillPreviewRawTileMap& previewTiles,
    const FillPreviewRawTileMap& maskTiles, int originX, int originY)
{
    if (maskTiles.empty()) {
        return;
    }

    const int canvasW = static_cast<int>(m_canvas.width());
    const int canvasH = static_cast<int>(m_canvas.height());
    if (canvasW <= 0 || canvasH <= 0) {
        return;
    }

    std::deque<FillPreviewState::ProgressBatch> preparedBatches
        = OpenGLCanvasWidget::buildFillPreviewBatches(previewTiles, maskTiles, originX, originY,
            m_fillPreview.readyRadius, m_fillPreview.radiusCap);
    while (!preparedBatches.empty()) {
        m_fillPreview.queuedBatches.push_back(std::move(preparedBatches.front()));
        preparedBatches.pop_front();
    }
}

bool OpenGLCanvasWidget::applyPendingFillPreviewBatches()
{
    if (!m_fillPreview.active) {
        return false;
    }

    if (m_fillPreview.previewPaused) {
        return false;
    }

    if (m_fillPreview.job) {
        std::lock_guard<std::mutex> lock(m_fillPreview.job->resultMutex);
        while (!m_fillPreview.job->pendingBatches.empty()) {
            m_fillPreview.queuedBatches.push_back(
                std::move(m_fillPreview.job->pendingBatches.front()));
            m_fillPreview.job->pendingBatches.pop_front();
        }
    }

    if (m_fillPreview.easeActive) {
        return false;
    }

    if (m_fillPreview.queuedBatches.empty()) {
        return false;
    }

    if (!m_fillPreview.previewContentGrid) {
        m_fillPreview.previewContentGrid = std::make_shared<TileGrid>();
        // Preview content mirrors the filled grid's format (document vs RGBA8 mask).
        m_fillPreview.previewContentGrid->setFormat(m_fillPreview.contentFormat);
    }
    if (!m_fillPreview.fillMaskGrid) {
        m_fillPreview.fillMaskGrid = std::make_shared<TileGrid>();
        // Coverage mask is always RGBA8.
        m_fillPreview.fillMaskGrid->setFormat(aether::TilePixelFormat::RGBA8);
    }

    const float batchUnlockMargin = 1.5f;
    if (m_fillPreview.previewActive
        && m_fillPreview.displayRadius + batchUnlockMargin
            < m_fillPreview.queuedBatches.front().minRadius) {
        // Bridge empty radial gaps between batches; otherwise the preview can
        // wait forever for a minRadius it is no longer able to reach.
        if (m_fillPreview.queuedBatches.front().minRadius > m_fillPreview.readyRadius + 0.01f) {
            retargetFillPreviewReveal(m_fillPreview.queuedBatches.front().minRadius);
        }
        return false;
    }

    const float readyRadiusBefore = m_fillPreview.readyRadius;
    const qulonglong queuedBefore = static_cast<qulonglong>(m_fillPreview.queuedBatches.size());
    QElapsedTimer applyBatchTimer;
    applyBatchTimer.start();
    FillPreviewState::ProgressBatch batch = std::move(m_fillPreview.queuedBatches.front());
    m_fillPreview.queuedBatches.pop_front();
    bool overwroteMaskTiles = false;
    for (const TileKey& key : batch.keys) {
        auto previewIt = batch.previewTiles.find(key);
        if (previewIt != batch.previewTiles.end()
            && previewIt->second.size() == aether::tileByteSize(m_fillPreview.contentFormat)) {
            // CONTENT (document layer format or RGBA8 mask).
            TileData& tile = m_fillPreview.previewContentGrid->getOrCreateTile(key);
            std::memcpy(tile.pixels(), previewIt->second.data(),
                aether::tileByteSize(m_fillPreview.contentFormat));
            tile.markDirty();
        }

        auto maskIt = batch.maskTiles.find(key);
        if (maskIt != batch.maskTiles.end() && maskIt->second.size() == TILE_BYTE_SIZE) {
            // MASK / coverage (RGBA8). fillMaskGrid is forced RGBA8 at creation.
            overwroteMaskTiles = overwroteMaskTiles || m_fillPreview.fillMaskGrid->hasTile(key);
            TileData& tile = m_fillPreview.fillMaskGrid->getOrCreateTile(key);
            std::memcpy(tile.pixels(), maskIt->second.data(), TILE_BYTE_SIZE);
            tile.markDirty();
        }
    }

    retargetFillPreviewReveal(batch.maxRadius);

    if (!m_fillPreview.queuedRevealSegments.empty()) {
        m_fillPreview.queuedRevealSegments.clear();
    }

    m_fillPreview.dirtyKeys.insert(batch.keys.begin(), batch.keys.end());

    if (overwroteMaskTiles) {
        m_fillPreview.metricsDirty = true;
    }
    if (!m_fillPreview.fillMaskGrid->empty()) {
        updateFillPreviewMetricsFromBatch(batch);
    }

    markFillPreviewDirtyTiles();
    if (readyRadiusBefore <= 0.0f && m_fillPreview.readyRadius > 0.0f) { }
    return true;
}

void OpenGLCanvasWidget::adoptCompletedFillResult()
{
    if (!m_fillPreview.active || !m_fillPreview.awaitingResult || !m_fillPreview.job) {
        return;
    }

    if (!m_fillPreview.job->done.load(std::memory_order_acquire)) {
        return;
    }

    FloodFillResult result;
    float preparedRadiusCap = 0.0f;
    QElapsedTimer adoptTimer;
    adoptTimer.start();
    {
        std::lock_guard<std::mutex> lock(m_fillPreview.job->resultMutex);
        preparedRadiusCap = m_fillPreview.job->preparedRadiusCap;
        while (!m_fillPreview.job->pendingBatches.empty()) {
            m_fillPreview.queuedBatches.push_back(
                std::move(m_fillPreview.job->pendingBatches.front()));
            m_fillPreview.job->pendingBatches.pop_front();
        }
        result = std::move(m_fillPreview.job->result);
    }

    m_fillPreview.awaitingResult = false;
    m_fillPreview.radiusCap = preparedRadiusCap;
    if (m_fillPreview.radiusCap > 0.0f) {
        const float effectiveRadiusCap
            = std::max(m_fillPreview.radiusCap, m_fillPreview.displayRadius);
        m_fillPreview.readyRadius = std::min(m_fillPreview.readyRadius, effectiveRadiusCap);
        if (m_fillPreview.easeActive) {
            m_fillPreview.easeTargetRadius
                = std::min(m_fillPreview.easeTargetRadius, effectiveRadiusCap);
        }
    }
    m_fillPreview.job.reset();

    if (result.pixelsFilled <= 0 || result.fillMaskTiles.empty()) {
        stopFillPreview(false, false);
        return;
    }

    m_fillPreview.pendingResult = std::move(result);
    if (!m_fillPreview.previewPaused && m_fillPreview.appliedPixelCount > 0
        && m_fillPreview.appliedMaxRadius > m_fillPreview.readyRadius + 0.01f) {
        retargetFillPreviewReveal(m_fillPreview.appliedMaxRadius);
    }
    if (m_fillPreview.previewPaused) {
        m_fillPreview.queuedBatches.clear();
        m_fillPreview.queuedRevealSegments.clear();
    }
    if (!m_fillPreview.previewActive) {
        beginFillPreviewAnimation(FloodFillResult {});
    }
}

void OpenGLCanvasWidget::beginFillPreviewAnimation(FloodFillResult&& result)
{
    Q_UNUSED(result);
    if (!m_fillPreview.active || m_fillPreview.previewActive) {
        return;
    }

    const qint64 waitElapsedMs = m_fillPreview.timer.isValid() ? m_fillPreview.timer.elapsed() : 0;
    if (m_fillPreview.awaitingResult && waitElapsedMs < kFillPreviewStartDelayMs) {
        return;
    }

    if (!m_fillPreview.awaitingResult) {
        QElapsedTimer applyExistingBatchesTimer;
        applyExistingBatchesTimer.start();
        const size_t appliedBatchCount = applyFillPreviewBatchBudget(
            kFillPreviewStartBatchBudget, kFillPreviewStartBatchBudgetMs);
    } else {
        QElapsedTimer applySinglePendingTimer;
        applySinglePendingTimer.start();
        applyPendingFillPreviewBatches();
    }

    if (m_fillPreview.readyRadius <= 0.0f && m_fillPreview.pendingResult.pixelsFilled > 0
        && !m_fillPreview.pendingResult.fillMaskTiles.empty()) {
        const float readyRadiusBeforeEnqueue = m_fillPreview.readyRadius;
        QElapsedTimer enqueuePendingResultTimer;
        enqueuePendingResultTimer.start();
        enqueueFillPreviewBatches(m_fillPreview.pendingResult.afterTiles,
            m_fillPreview.pendingResult.fillMaskTiles,
            static_cast<int>(std::floor(m_fillPreview.origin.x)),
            static_cast<int>(std::floor(m_fillPreview.origin.y)));
        if (!m_fillPreview.awaitingResult) {
            QElapsedTimer applyEnqueuedBatchesTimer;
            applyEnqueuedBatchesTimer.start();
            const size_t appliedBatchCount = applyFillPreviewBatchBudget(
                kFillPreviewStartBatchBudget, kFillPreviewStartBatchBudgetMs);
        } else {
            QElapsedTimer applyEnqueuedPendingTimer;
            applyEnqueuedPendingTimer.start();
            applyPendingFillPreviewBatches();
        }
        if (readyRadiusBeforeEnqueue <= 0.0f && m_fillPreview.readyRadius > 0.0f) { }
    }

    if (m_fillPreview.readyRadius <= 0.0f) {
        return;
    }

    m_fillPreview.lastPreviewGateReason = 0;
    m_fillPreview.lastPreviewGateLogMs = waitElapsedMs;

    m_fillPreview.timer.restart();
    m_fillPreview.previewActive = true;
    m_fillPreview.previewPaused = false;
    m_fillPreview.easeActive = true;
    const float animationStartRadius = std::clamp(std::max(1.0f, m_fillPreview.minRevealRadius),
        1.0f, std::max(1.0f, m_fillPreview.readyRadius));
    m_fillPreview.displayRadius = animationStartRadius;
    m_fillPreview.revealSpeedPxPerMs = 0.0f;
    m_fillPreview.easeStartRadius = m_fillPreview.displayRadius;
    m_fillPreview.easeTargetRadius
        = std::max(m_fillPreview.displayRadius, m_fillPreview.readyRadius);
    m_fillPreview.easeStartMs = 0;
    m_fillPreview.lastAnimationMs = 0;
}

void OpenGLCanvasWidget::retargetFillPreviewReveal(float newReadyRadius)
{
    float clampedReadyRadius = std::max(newReadyRadius, m_fillPreview.displayRadius);
    float revealRadiusCap = 0.0f;
    if (m_fillPreview.awaitingResult || m_fillPreview.previewPaused) {
        revealRadiusCap = kFillPreviewPauseRadiusPx;
    }
    if (m_fillPreview.radiusCap > 0.0f) {
        revealRadiusCap = revealRadiusCap > 0.0f
            ? std::min(revealRadiusCap, m_fillPreview.radiusCap)
            : m_fillPreview.radiusCap;
    }
    if (revealRadiusCap > 0.0f && clampedReadyRadius > revealRadiusCap) {
        clampedReadyRadius = std::max(revealRadiusCap, m_fillPreview.displayRadius);
    }
    if (clampedReadyRadius <= m_fillPreview.readyRadius + 0.01f) {
        return;
    }

    const qint64 elapsedMs = m_fillPreview.timer.isValid() ? m_fillPreview.timer.elapsed() : 0;
    m_fillPreview.lastAnimationMs = elapsedMs;

    const float segmentDistance = std::max(0.0f, clampedReadyRadius - m_fillPreview.displayRadius);
    const float distanceNorm = std::clamp(segmentDistance / 220.0f, 0.0f, 1.0f);
    const float segmentDurationMs = 48.0f + distanceNorm * 70.0f;
    m_fillPreview.revealSpeedPxPerMs = segmentDistance / std::max(segmentDurationMs, 1.0f);
    m_fillPreview.readyRadius = clampedReadyRadius;
}

bool OpenGLCanvasWidget::applyFloodFillResult(const QUuid& layerId, FloodFillResult&& result,
    std::optional<SelectionRestoreContext> selectionRestore, bool maskTarget)
{
    if (result.pixelsFilled <= 0) {
        return false;
    }
    QElapsedTimer applyTimer;
    applyTimer.start();

    auto* layer = m_layerModel ? m_layerModel->layerById(layerId) : nullptr;
    if (!isLayerCanvasEditable(layer) || !layer->isRaster()) {
        return false;
    }
    TileGrid* targetGrid = maskTarget ? layer->maskTileGrid() : layer->tileGrid.get();
    if (!targetGrid) {
        return false;
    }

    auto& grid = *targetGrid;
    std::unordered_set<TileKey, TileKeyHash> affectedKeys;
    QElapsedTimer collectTimer;
    collectTimer.start();
    affectedKeys.reserve(
        result.beforeTiles.size() + result.afterTiles.size() + result.removedTiles.size());
    for (const auto& [key, _] : result.beforeTiles) {
        affectedKeys.insert(key);
    }
    for (const auto& [key, _] : result.afterTiles) {
        affectedKeys.insert(key);
    }
    for (const TileKey& key : result.removedTiles) {
        affectedKeys.insert(key);
    }

    QElapsedTimer applyTilesTimer;
    applyTilesTimer.start();
    for (const TileKey& key : affectedKeys) {
        if (result.removedTiles.count(key) > 0) {
            grid.removeTile(key);
            continue;
        }

        auto afterIt = result.afterTiles.find(key);
        if (afterIt == result.afterTiles.end()
            || afterIt->second.size() != aether::tileByteSize(grid.format())) {
            continue;
        }

        TileData& tile = grid.getOrCreateTile(key);
        std::memcpy(tile.pixels(), afterIt->second.data(), aether::tileByteSize(grid.format()));
        tile.markDirty();
    }

    std::vector<TileKey> dirtyVec(affectedKeys.begin(), affectedKeys.end());
    QElapsedTimer dirtyTimer;
    dirtyTimer.start();
    if (!dirtyVec.empty()) {
        m_canvas.dirtyManager().onTilesDirtied(layer->id, dirtyVec);
        markBoardCompositionTilesDirty(layer->id, dirtyVec);
        emit contentRegionChanged(worldRectFromTileKeys(dirtyVec));
        emit contentTilesChanged(qPointsFromTileKeys(dirtyVec));
    }

    for (const TileKey& key : affectedKeys) {
        if (!maskTarget) {
            if (result.removedTiles.count(key) > 0) {
                m_canvas.tilePositionIndex().removeEntry(key, layer->id);
            } else {
                m_canvas.tilePositionIndex().addEntry(key, layer->id);
            }
        }
    }

    StrokeSnapshot snapshot;
    QElapsedTimer undoTimer;
    undoTimer.start();
    snapshot.layerId = layer->id;
    snapshot.maskTarget = maskTarget;
    snapshot.beforeTiles = std::move(result.beforeTiles);
    snapshot.afterTiles = std::move(result.afterTiles);
    snapshot.createdTiles = std::move(result.createdTiles);
    snapshot.removedTiles = std::move(result.removedTiles);
    const size_t undoBeforeCount = snapshot.beforeTiles.size();
    const size_t undoAfterCount = snapshot.afterTiles.size();

    auto cmd = std::make_unique<DrawCommand>(
        &m_canvas, m_layerModel, std::move(snapshot), std::move(selectionRestore));

    m_canvas.undoManager().push(std::move(cmd));

    if (m_layerModel) {
        if (maskTarget) {
            layer->maskThumbnailDirty = true;
            invalidateCachedLayerStacks();
        }
        m_layerModel->notifyLayerDataChanged(layer->id);
    }

    return true;
}

bool OpenGLCanvasWidget::commitFillPreviewResult()
{
    if (!m_fillPreview.active || m_fillPreview.pendingResult.pixelsFilled <= 0) {
        return false;
    }

    return applyFloodFillResult(m_fillPreview.targetLayerId, std::move(m_fillPreview.pendingResult),
        std::move(m_fillPreview.selectionRestore), m_fillPreview.maskTarget);
}

QUuid OpenGLCanvasWidget::currentFillProcessingLayerId() const
{
    if (m_pendingFillKickoff.pending && !m_pendingFillKickoff.layerId.isNull()) {
        return m_pendingFillKickoff.layerId;
    }
    if ((m_fillPreview.active || m_activeFillWorkerRequest != 0)
        && !m_fillPreview.targetLayerId.isNull()) {
        return m_fillPreview.targetLayerId;
    }
    if (m_lassoFillCommit.job && !m_lassoFillCommit.targetLayerId.isNull()) {
        return m_lassoFillCommit.targetLayerId;
    }
    return {};
}

void OpenGLCanvasWidget::syncFillProcessingLayerSignal()
{
    const QUuid currentLayerId = currentFillProcessingLayerId();
    if (currentLayerId == m_signaledFillProcessingLayerId) {
        return;
    }
    m_signaledFillProcessingLayerId = currentLayerId;
    emit fillProcessingLayerChanged(m_signaledFillProcessingLayerId);
}

void OpenGLCanvasWidget::stopFillPreview(bool markDirtyTiles, bool cancelWorker, bool hidePopup)
{
    if (!m_fillPreview.active && m_fillPreview.dirtyKeys.empty() && !m_fillPreview.job
        && !m_pendingFillKickoff.pending && m_activeFillWorkerRequest == 0) {
        return;
    }

    if (markDirtyTiles) {
        markFillPreviewDirtyTiles();
    }

    if (m_fillPreview.job) {
        m_fillPreview.job->cancelled.store(cancelWorker, std::memory_order_release);
    }
    if (cancelWorker && m_fillWorkerCancelState) {
        m_fillWorkerCancelState->store(true, std::memory_order_release);
    }

    m_fillPreview.active = false;
    m_fillPreview.previewActive = false;
    m_fillPreview.awaitingResult = false;
    m_fillPreview.previewPaused = false;
    m_fillPreview.easeActive = false;
    m_fillPreview.finalResultOnly = false;
    m_fillPreview.maskTarget = false;
    m_fillPreview.targetLayerId = QUuid();
    m_fillPreview.previewContentGrid.reset();
    m_fillPreview.fillMaskGrid.reset();
    m_fillPreview.dirtyKeys.clear();
    m_fillPreview.queuedBatches.clear();
    m_fillPreview.queuedRevealSegments.clear();
    m_fillPreview.radiusCap = 0.0f;
    m_fillPreview.readyRadius = 0.0f;
    m_fillPreview.displayRadius = 0.0f;
    m_fillPreview.revealSpeedPxPerMs = 0.0f;
    m_fillPreview.easeStartRadius = 0.0f;
    m_fillPreview.easeTargetRadius = 0.0f;
    resetFillPreviewMetrics();
    m_fillPreview.lastAnimationMs = 0;
    m_fillPreview.easeStartMs = 0;
    m_fillPreview.lastPreviewGateLogMs = -1;
    m_fillPreview.lastPreviewGateReason = 0;
    m_fillPreview.pendingResult = {};
    m_fillPreview.selectionRestore = {};
    m_fillPreview.job.reset();
    m_fillPreview.compositingState = {};
    m_pendingFillKickoff = {};
    if (cancelWorker) {
        m_fillWorkerCancelState.reset();
        m_activeFillWorkerRequest = 0;
    }
    if (hidePopup) {
        hideFillProgressPopupImmediate();
    }
    syncFillProcessingLayerSignal();
}

void OpenGLCanvasWidget::markFillPreviewDirtyTiles()
{
    if (!m_fillPreview.dirtyKeys.empty()) {
        m_canvas.compositionCache().markDirty(m_fillPreview.dirtyKeys);
    }
}

bool OpenGLCanvasWidget::updateFillPreviewAnimationState()
{
    if (!m_fillPreview.active) {
        return false;
    }

    applyPendingFillPreviewBatches();

    const bool finalResultOnlyAwaitingResult
        = m_fillPreview.finalResultOnly && m_fillPreview.awaitingResult;
    if (finalResultOnlyAwaitingResult) {
        const qint64 elapsedMs = m_fillPreview.timer.isValid() ? m_fillPreview.timer.elapsed() : 0;
        if (elapsedMs >= kClassicFillWaitPopupDelayMs) {
            if (!m_fillProgressPopup || !m_fillProgressPopup->isProcessingVisible()) {
                showClassicFillWaitPopup();
            } else {
                updateFillProgressPopupPosition();
            }
        }
    } else if (m_fillPreview.finalResultOnly && m_fillProgressPopup
        && m_fillProgressPopup->isProcessingVisible()) {
        hideFillProgressPopupImmediate();
    }

    if (!m_fillPreview.previewActive) {
        if (m_fillPreview.awaitingResult) {
            adoptCompletedFillResult();
        }
        if (m_fillPreview.finalResultOnly && m_fillProgressPopup
            && m_fillProgressPopup->isProcessingVisible() && !m_fillPreview.awaitingResult) {
            hideFillProgressPopupImmediate();
        }
        beginFillPreviewAnimation(FloodFillResult {});
        return m_fillPreview.active;
    }

    const qint64 elapsedMs = m_fillPreview.timer.isValid() ? m_fillPreview.timer.elapsed() : 0;
    const qint64 deltaMs = std::max<qint64>(0, elapsedMs - m_fillPreview.lastAnimationMs);
    m_fillPreview.lastAnimationMs = elapsedMs;

    if (m_fillPreview.easeActive) {
        const qint64 easeElapsedMs = std::max<qint64>(0, elapsedMs - m_fillPreview.easeStartMs);
        const float durationMs = static_cast<float>(std::max(m_fillPreview.durationMs, 1));
        const float t = std::clamp(static_cast<float>(easeElapsedMs) / durationMs, 0.0f, 1.0f);
        const float easedT = aether::fillPreviewRevealEase(t);
        m_fillPreview.displayRadius = m_fillPreview.easeStartRadius
            + (m_fillPreview.easeTargetRadius - m_fillPreview.easeStartRadius) * easedT;
        if (t >= 1.0f) {
            m_fillPreview.displayRadius = m_fillPreview.easeTargetRadius;
            m_fillPreview.easeActive = false;
            m_fillPreview.lastAnimationMs = elapsedMs;
        }
    } else if (deltaMs > 0 && m_fillPreview.revealSpeedPxPerMs > 0.0f) {
        m_fillPreview.displayRadius = std::min(m_fillPreview.readyRadius,
            m_fillPreview.displayRadius
                + m_fillPreview.revealSpeedPxPerMs * static_cast<float>(deltaMs));
    }

    if (!m_fillPreview.easeActive && !m_fillPreview.previewPaused && m_fillPreview.awaitingResult
        && m_fillPreview.displayRadius >= kFillPreviewPauseRadiusPx) {
        m_fillPreview.previewPaused = true;
        m_fillPreview.displayRadius = kFillPreviewPauseRadiusPx;
        m_fillPreview.readyRadius = kFillPreviewPauseRadiusPx;
        m_fillPreview.revealSpeedPxPerMs = 0.0f;
        m_fillPreview.queuedRevealSegments.clear();
        showFillProgressPopupProcessing();
    }

    if (!m_fillPreview.queuedRevealSegments.empty()) {
        const float preloadMargin = std::max(m_fillPreview.feather, 6.0f);
        while (!m_fillPreview.queuedRevealSegments.empty()
            && m_fillPreview.displayRadius + preloadMargin
                >= m_fillPreview.queuedRevealSegments.front().minRadius) {
            retargetFillPreviewReveal(m_fillPreview.queuedRevealSegments.front().maxRadius);
            m_fillPreview.queuedRevealSegments.pop_front();
        }
    }

    if (m_fillPreview.awaitingResult) {
        adoptCompletedFillResult();
        if (!m_fillPreview.active) {
            return false;
        }
    }

    if (m_fillPreview.previewPaused) {
        updateFillProgressPopupPosition();
        if (!m_fillPreview.awaitingResult || m_fillPreview.pendingResult.pixelsFilled > 0) {
            m_fillPreview.previewPaused = false;
            m_fillPreview.radiusCap = 0.0f;
            m_fillPreview.readyRadius
                = std::max(m_fillPreview.readyRadius, m_fillPreview.displayRadius);
            m_fillPreview.revealSpeedPxPerMs = 0.0f;
            m_fillPreview.queuedBatches.clear();
            m_fillPreview.queuedRevealSegments.clear();

            enqueueFillPreviewBatches(m_fillPreview.pendingResult.afterTiles,
                m_fillPreview.pendingResult.fillMaskTiles,
                static_cast<int>(std::floor(m_fillPreview.origin.x)),
                static_cast<int>(std::floor(m_fillPreview.origin.y)));
            applyPendingFillPreviewBatches();

            hideFillProgressPopupImmediate();
            markFillPreviewDirtyTiles();
            return m_fillPreview.active;
        }

        markFillPreviewDirtyTiles();
        return m_fillPreview.active;
    }

    const bool hasQueuedBatches = !m_fillPreview.queuedBatches.empty();
    const bool hasCommitReadyResult = m_fillPreview.pendingResult.pixelsFilled > 0;
    if (!m_fillPreview.awaitingResult && !m_fillPreview.previewPaused
        && m_fillPreview.appliedPixelCount > 0
        && m_fillPreview.appliedMaxRadius > m_fillPreview.readyRadius + 0.01f) {
        retargetFillPreviewReveal(m_fillPreview.appliedMaxRadius);
    }
    const bool animationCaughtUp = m_fillPreview.displayRadius + 0.5f >= m_fillPreview.readyRadius;
    if (!hasQueuedBatches && animationCaughtUp
        && (!m_fillPreview.awaitingResult || hasCommitReadyResult)) {
        commitFillPreviewResult();
        stopFillPreview(true, m_fillPreview.awaitingResult);
        return false;
    }

    markFillPreviewDirtyTiles();
    return m_fillPreview.active;
}

const FillPreviewCompositingState* OpenGLCanvasWidget::currentFillPreviewState() const
{
    if (m_fillPreview.active && m_fillPreview.previewActive && m_fillPreview.previewContentGrid
        && m_fillPreview.fillMaskGrid) {
        const qint64 elapsedMs = m_fillPreview.timer.isValid() ? m_fillPreview.timer.elapsed() : 0;
        Q_UNUSED(elapsedMs);

        m_fillPreview.compositingState.active = true;
        m_fillPreview.compositingState.targetLayerId = m_fillPreview.targetLayerId;
        m_fillPreview.compositingState.maskTarget = m_fillPreview.maskTarget;
        m_fillPreview.compositingState.previewContentGrid = m_fillPreview.previewContentGrid.get();
        m_fillPreview.compositingState.fillMaskGrid = m_fillPreview.fillMaskGrid.get();
        m_fillPreview.compositingState.retainedPayload = nullptr;
        m_fillPreview.compositingState.useSolidColor = false;
        m_fillPreview.compositingState.renderAboveLayerContent = true;
        m_fillPreview.compositingState.solidColor = {};
        m_fillPreview.compositingState.origin = m_fillPreview.origin;
        m_fillPreview.compositingState.radius = std::max(1.0f, m_fillPreview.displayRadius);
        m_fillPreview.compositingState.feather = m_fillPreview.feather;
        return &m_fillPreview.compositingState;
    }

    return nullptr;
}

bool OpenGLCanvasWidget::performFill(int worldX, int worldY)
{
    if (m_transformController.isActive())
        return false;

    auto* layer = activeLayer();
    if (!isLayerCanvasEditable(layer) || !layer->isRaster()) {
        return false;
    }
    const bool maskTarget = layer->maskEditActive && layer->maskGrid != nullptr;
    TileGrid* targetGrid = maskTarget ? layer->maskGrid.get() : layer->tileGrid.get();
    if (!targetGrid) {
        return false;
    }
    notifyCanvasInteraction(true);

    const TileGrid* selectionMask
        = (m_selectionController && m_selectionController->lassoSelection().hasSelection()
              && !m_selectionController->lassoSelection().mask().empty())
        ? &m_selectionController->lassoSelection().mask()
        : nullptr;

    const FillWorkRect workRect
        = computeFillWorkRect(targetGrid, selectionMask, worldX, worldY, hasFiniteDocumentBounds(),
            static_cast<int>(m_canvas.width()), static_cast<int>(m_canvas.height()));
    if (workRect.width <= 0 || workRect.height <= 0)
        return false;

    const uint8_t fillR = m_brush->colorR();
    const uint8_t fillG = m_brush->colorG();
    const uint8_t fillB = m_brush->colorB();
    const uint8_t fillA = m_brush->colorA();
    const uint8_t pr
        = static_cast<uint8_t>((static_cast<int>(fillR) * static_cast<int>(fillA) + 127) / 255);
    const uint8_t pg
        = static_cast<uint8_t>((static_cast<int>(fillG) * static_cast<int>(fillA) + 127) / 255);
    const uint8_t pb
        = static_cast<uint8_t>((static_cast<int>(fillB) * static_cast<int>(fillA) + 127) / 255);

    stopFillPreview(true);

    uint8_t seedR = 0;
    uint8_t seedG = 0;
    uint8_t seedB = 0;
    uint8_t seedA = 0;
    if (!samplePixelAt(targetGrid, worldX, worldY, seedR, seedG, seedB, seedA)) {
        return false;
    }
    if (selectionMask && fillMaskAlphaAt(selectionMask, worldX, worldY) == 0) {
        return false;
    }
    if (seedR == pr && seedG == pg && seedB == pb && seedA == fillA) {
        return false;
    }

    if (hasFiniteDocumentBounds()) {
        const float estimatedRadius
            = aether::estimateFillRadiusFromSeed(targetGrid, selectionMask, FillAlgorithm::Smart,
                worldX, worldY, workRect.width, workRect.height, kSmartFillMaxEstimatedRadiusPx);
        if (estimatedRadius >= kSmartFillMaxEstimatedRadiusPx) {
            aether::showFillRadiusLimitPopup(this, FillAlgorithm::Smart, estimatedRadius);
            return false;
        }
    }

    scheduleDeferredFillKickoff(layer->id, FillAlgorithm::Smart, buildCurrentSelectionRestore(),
        FillOrigin { worldX, worldY }, FillColor { pr, pg, pb, fillA },
        FillCanvasBounds { workRect.originX, workRect.originY, workRect.width, workRect.height },
        maskTarget, workRect.forceFinalResultOnly);
    return true;
}

bool OpenGLCanvasWidget::performClassicFill(int worldX, int worldY)
{
    if (m_transformController.isActive())
        return false;

    auto* layer = activeLayer();
    if (!isLayerCanvasEditable(layer) || !layer->isRaster()) {
        return false;
    }
    const bool maskTarget = layer->maskEditActive && layer->maskGrid != nullptr;
    TileGrid* targetGrid = maskTarget ? layer->maskGrid.get() : layer->tileGrid.get();
    if (!targetGrid) {
        return false;
    }
    notifyCanvasInteraction(true);

    const TileGrid* selectionMask
        = (m_selectionController && m_selectionController->lassoSelection().hasSelection()
              && !m_selectionController->lassoSelection().mask().empty())
        ? &m_selectionController->lassoSelection().mask()
        : nullptr;

    const FillWorkRect workRect
        = computeFillWorkRect(targetGrid, selectionMask, worldX, worldY, hasFiniteDocumentBounds(),
            static_cast<int>(m_canvas.width()), static_cast<int>(m_canvas.height()));
    if (workRect.width <= 0 || workRect.height <= 0)
        return false;

    const uint8_t fillR = m_brush->colorR();
    const uint8_t fillG = m_brush->colorG();
    const uint8_t fillB = m_brush->colorB();
    const uint8_t fillA = m_brush->colorA();
    const uint8_t pr
        = static_cast<uint8_t>((static_cast<int>(fillR) * static_cast<int>(fillA) + 127) / 255);
    const uint8_t pg
        = static_cast<uint8_t>((static_cast<int>(fillG) * static_cast<int>(fillA) + 127) / 255);
    const uint8_t pb
        = static_cast<uint8_t>((static_cast<int>(fillB) * static_cast<int>(fillA) + 127) / 255);

    uint8_t seedR = 0;
    uint8_t seedG = 0;
    uint8_t seedB = 0;
    uint8_t seedA = 0;
    if (!samplePixelAt(targetGrid, worldX, worldY, seedR, seedG, seedB, seedA)) {
        return false;
    }
    if (selectionMask && fillMaskAlphaAt(selectionMask, worldX, worldY) == 0) {
        return false;
    }
    if (seedR == pr && seedG == pg && seedB == pb && seedA == fillA) {
        return false;
    }

    scheduleDeferredFillKickoff(layer->id, FillAlgorithm::Classic, buildCurrentSelectionRestore(),
        FillOrigin { worldX, worldY }, FillColor { pr, pg, pb, fillA },
        FillCanvasBounds { workRect.originX, workRect.originY, workRect.width, workRect.height },
        maskTarget, true);
    return true;
}

void OpenGLCanvasWidget::updateTileIndex(const ruwa::core::layers::LayerData* layer,
    const std::unordered_set<TileKey, TileKeyHash>& dirtyKeys)
{
    if (!layer)
        return;
    for (const auto& key : dirtyKeys) {
        m_canvas.tilePositionIndex().addEntry(key, layer->id);
    }
}

void OpenGLCanvasWidget::cleanupStrokeTextures()
{
    if (!m_renderer || !m_brush->hasActiveStroke())
        return;

    auto* tileRenderer = m_renderer->tileRenderer();
    if (!tileRenderer)
        return;

    for (auto& [key, tile] : m_brush->strokeBuffer().tiles()) {
        if (tile.hasTexture()) {
            tileRenderer->destroyTileTexture(tile);
        }
    }
}

void OpenGLCanvasWidget::setLassoStabilization(float stabilization)
{
    m_lassoStabilization = std::clamp(stabilization, 0.0f, 1.0f);
}

void OpenGLCanvasWidget::setLassoFillStabilization(float stabilization)
{
    m_lassoFillStabilization = std::clamp(stabilization, 0.0f, 1.0f);
}

bool OpenGLCanvasWidget::selectionPathNeedsCatchup(
    float targetX, float targetY, float stabilization) const
{
    const float clampedStabilization = std::clamp(stabilization, 0.0f, 1.0f);
    if (clampedStabilization <= 0.0001f) {
        return false;
    }
    return ruwa::core::brushes::hasPendingStrokeStabilizer(
        m_lassoStabilizerState, targetX, targetY);
}

void OpenGLCanvasWidget::resetSelectionPathStabilizer()
{
    m_stabilizerCatchupTimer.stop();
    ruwa::core::brushes::clearStrokeStabilizer(m_lassoStabilizerState);
}

void OpenGLCanvasWidget::updateStabilizerCatchupTimer()
{
    const bool lassoCatchupActive = m_selectionController && m_selectionController->isLassoActive()
        && selectionPathNeedsCatchup(
            m_lastStrokeTargetX, m_lastStrokeTargetY, m_lassoStabilization);
    const bool lassoFillCatchupActive = m_lassoFillActive
        && selectionPathNeedsCatchup(
            m_lastStrokeTargetX, m_lastStrokeTargetY, m_lassoFillStabilization);

    if (lassoCatchupActive || lassoFillCatchupActive) {
        if (!m_stabilizerCatchupTimer.isActive()) {
            m_stabilizerCatchupTimer.start();
        }
        return;
    }

    m_stabilizerCatchupTimer.stop();
}

void OpenGLCanvasWidget::processStabilizerCatchup()
{
    if (m_selectionController && m_selectionController->isLassoActive()) {
        if (!selectionPathNeedsCatchup(
                m_lastStrokeTargetX, m_lastStrokeTargetY, m_lassoStabilization)) {
            m_stabilizerCatchupTimer.stop();
            return;
        }
        const float lagMs = ruwa::core::brushes::stabilizationTauMs(m_lassoStabilization);
        const double nowMs = static_cast<double>(m_stabilizerElapsedTimer.elapsed());
        ruwa::core::brushes::sampleStrokeStabilizerPath(m_lassoStabilizerState, m_lastStrokeTargetX,
            m_lastStrokeTargetY, lagMs, nowMs, false,
            [this](const ruwa::core::brushes::StrokeStabilizerPoint& pt, double) {
                m_selectionController->updateLasso(pt.x, pt.y);
            });
        updateStabilizerCatchupTimer();
        return;
    }

    if (m_lassoFillActive) {
        if (!selectionPathNeedsCatchup(
                m_lastStrokeTargetX, m_lastStrokeTargetY, m_lassoFillStabilization)) {
            m_stabilizerCatchupTimer.stop();
            return;
        }
        const float lagMs = ruwa::core::brushes::stabilizationTauMs(m_lassoFillStabilization);
        const double nowMs = static_cast<double>(m_stabilizerElapsedTimer.elapsed());
        bool addedPoint = false;
        ruwa::core::brushes::sampleStrokeStabilizerPath(m_lassoStabilizerState, m_lastStrokeTargetX,
            m_lastStrokeTargetY, lagMs, nowMs, false,
            [this, &addedPoint](const ruwa::core::brushes::StrokeStabilizerPoint& pt, double) {
                if (m_lassoFillPoints.empty()) {
                    m_lassoFillPoints.push_back({ pt.x, pt.y });
                    addedPoint = true;
                } else {
                    const Vector2& last = m_lassoFillPoints.back();
                    const float dx = pt.x - last.x;
                    const float dy = pt.y - last.y;
                    if ((dx * dx + dy * dy) >= 0.01f) {
                        m_lassoFillPoints.push_back({ pt.x, pt.y });
                        addedPoint = true;
                    }
                }
            });
        if (addedPoint) {
            scheduleLassoFillPreviewRefresh();
        } else {
            requestRender();
        }
        updateStabilizerCatchupTimer();
        return;
    }

    m_stabilizerCatchupTimer.stop();
}

void OpenGLCanvasWidget::flushPendingFinalization()
{
    if (m_strokeHost) {
        m_strokeHost->flushPendingFinalization();
    }
}

void OpenGLCanvasWidget::pushSelectionCommand(
    const SelectionState& before, const SelectionState& after)
{
    if (before.layer.primaryId == after.layer.primaryId
        && before.layer.selectedIds == after.layer.selectedIds
        && before.lasso.regions == after.lasso.regions) {
        return;
    }
    auto* layerSel = m_layerModel ? m_layerModel->selectionManager() : nullptr;
    auto* lassoSel = m_selectionController ? &m_selectionController->lassoSelection() : nullptr;
    auto cmd = std::make_unique<SelectionCommand>(
        layerSel, lassoSel, &m_canvas, before, after,
        [this](const ruwa::core::layers::LayerId& id) {
            return m_layerModel && m_layerModel->contains(id);
        },
        [this]() { requestRender(); });
    m_canvas.undoManager().push(std::move(cmd));
    m_lastSelectionState = after;
}

void OpenGLCanvasWidget::onLayerSelectionChanged(const ruwa::core::layers::LayerId&)
{
    if (m_ignoreSelectionChange) {
        m_ignoreSelectionChange = false;
    }
    SelectionState current;
    current.layer
        = captureLayerSelection(m_layerModel ? m_layerModel->selectionManager() : nullptr);
    current.lasso = captureLassoSelection(
        m_selectionController ? &m_selectionController->lassoSelection() : nullptr,
        effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
    m_lastSelectionState = current;

    if (m_layerScreenSourceCache) {
        m_layerScreenSourceCache->invalidateByLayer(lassoPreviewSelectionMaskCacheId());
    }

    if (m_lassoFillPreview.active) {
        m_lassoFillViewportPreview.screenSourcesDirty = true;
        refreshLassoFillPreview();
        requestRender();
    }
    if (m_transformController.isActive()) {
        invalidateTransformViewportPreviewSelectionMask();
        requestRender();
    }
}

SelectionRestoreContext OpenGLCanvasWidget::buildCurrentSelectionRestore()
{
    SelectionRestoreContext ctx;
    ctx.layerSelection = m_layerModel ? m_layerModel->selectionManager() : nullptr;
    ctx.lassoSelection = m_selectionController ? &m_selectionController->lassoSelection() : nullptr;
    ctx.canvas = &m_canvas;
    ctx.before.layer = captureLayerSelection(ctx.layerSelection);
    ctx.before.lasso = captureLassoSelection(
        ctx.lassoSelection, effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
    ctx.after = ctx.before;
    ctx.layerExists = [this](const ruwa::core::layers::LayerId& id) {
        return m_layerModel && m_layerModel->contains(id);
    };
    ctx.requestRender = [this]() { requestRender(); };
    ctx.onBeforeRestore = [this]() { m_ignoreSelectionChange = true; };
    ctx.onAfterRestore = [this]() {
        m_ignoreSelectionChange = false;
        m_lastSelectionState.layer
            = captureLayerSelection(m_layerModel ? m_layerModel->selectionManager() : nullptr);
        m_lastSelectionState.lasso = captureLassoSelection(
            m_selectionController ? &m_selectionController->lassoSelection() : nullptr,
            effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
    };
    return ctx;
}

// ==========================================================================
//   D E F E R R E D   T R A N S F O R M   F I N A L I Z A T I O N
// ==========================================================================

void OpenGLCanvasWidget::finalizeTransform()
{
    if (!tryFinalizeTransform(false)) {
        m_transformFinalizeTimer.start(1);
    }
}

bool OpenGLCanvasWidget::tryFinalizeTransform(bool forceWait)
{
    if (!m_pendingTransform.active)
        return true;

    auto* layer = m_layerModel ? m_layerModel->layerById(m_pendingTransform.layerId) : nullptr;
    TileGrid* grid = layer
        ? (m_pendingTransform.maskTarget ? layer->maskTileGrid() : layer->pixelGrid())
        : nullptr;

    if (!grid) {
        // Layer disappeared â€” clean up GL resources
        if ((m_pendingTransform.fence || m_pendingTransform.selectionFence) && m_renderer
            && m_renderer->transformRenderer()) {
            makeCurrent();
            if (m_pendingTransform.fence) {
                m_renderer->transformRenderer()->deleteFence(m_pendingTransform.fence);
            }
            if (m_pendingTransform.selectionFence) {
                m_renderer->transformRenderer()->deleteFence(m_pendingTransform.selectionFence);
            }
            doneCurrent();
        }
        m_pendingTransform = {};
        return true;
    }

    if (m_pendingTransform.fence && m_renderer && m_renderer->transformRenderer() && !forceWait) {
        makeCurrent();
        const bool readbackReady
            = m_renderer->transformRenderer()->isReadbackComplete(m_pendingTransform.fence);
        doneCurrent();
        if (!readbackReady) {
            return false;
        }
    }

    // 1. Finish PBO readback: wait for DMA, copy to CPU
    if (m_pendingTransform.fence && m_renderer && m_renderer->transformRenderer()) {
        makeCurrent();
        m_renderer->transformRenderer()->finishReadback(
            m_pendingTransform.fence, *grid, m_pendingTransform.readbackKeysOrdered);
        doneCurrent();
        m_pendingTransform.fence = nullptr;
    }

    // 1.5. GPU transform selection mask (if needed)
    if (m_pendingTransform.applySelectionMask && m_renderer && m_renderer->transformRenderer()
        && m_renderer->tileRenderer()) {
        auto* transformRenderer = m_renderer->transformRenderer();
        auto* tileRenderer = m_renderer->tileRenderer();
        LassoSelectionManager::MaskMutationScope maskScope(m_selectionController->lassoSelection());
        maskScope.disableSoftAlphaInvalidation();
        TileGrid& maskGrid = maskScope.grid();
        if (!maskGrid.empty()) {
            if (!m_pendingTransform.selectionFence
                && m_pendingTransform.selectionReadbackKeysOrdered.empty()) {
                makeCurrent();
                transformRenderer->buildSourceAtlas(maskGrid, tileRenderer, true);
                for (auto& [key, tile] : maskGrid.tiles()) {
                    if (tile.hasTexture()) {
                        tileRenderer->destroyTileTexture(tile);
                    }
                }
                maskGrid.clear();

                auto resultKeys = transformRenderer->applyGPU(
                    m_pendingTransform.selectionTransformState, maskGrid, tileRenderer);
                m_pendingTransform.selectionReadbackKeysOrdered.assign(
                    resultKeys.begin(), resultKeys.end());
                m_pendingTransform.selectionFence = transformRenderer->startAsyncReadback(
                    maskGrid, m_pendingTransform.selectionReadbackKeysOrdered);
                transformRenderer->destroySourceAtlas();
                doneCurrent();

                if (m_pendingTransform.selectionFence && !forceWait) {
                    return false;
                }
            }

            if (m_pendingTransform.selectionFence && !forceWait) {
                makeCurrent();
                const bool readbackReady
                    = transformRenderer->isReadbackComplete(m_pendingTransform.selectionFence);
                doneCurrent();
                if (!readbackReady) {
                    return false;
                }
            }

            if (m_pendingTransform.selectionFence) {
                makeCurrent();
                transformRenderer->finishReadback(m_pendingTransform.selectionFence, maskGrid,
                    m_pendingTransform.selectionReadbackKeysOrdered);
                doneCurrent();
                m_pendingTransform.selectionFence = nullptr;
            }

            // Binarize mask to eliminate any intermediate alpha from GPU resampling
            // (prevents semi-transparent outline on subsequent transforms)
            binarizeSelectionMask(maskGrid);
            if (hasFiniteDocumentBounds()) {
                clampSelectionMaskToCanvas(maskGrid, m_canvas.width(), m_canvas.height());
            }
            m_selectionController->lassoSelection().setMaskHasSoftAlpha(false);
            m_selectionController->lassoSelection().rebuildEdgesFromMask(
                effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
            // Sync m_regions so captureLassoSelection gets correct data (e.g. for
            // clearSelectionMask undo)
            LassoSelectionState afterLasso = transformLassoRegions(
                m_pendingTransform.selectionBefore, m_pendingTransform.selectionTransformState);
            m_selectionController->lassoSelection().setRegionsOnly(afterLasso.regions);
        }
        m_pendingTransform.selectionReadbackKeysOrdered.clear();
        m_pendingTransform.applySelectionMask = false;
    }

    // 2. Build TransformSnapshot for undo
    TransformSnapshot snapshot;
    snapshot.layerId = m_pendingTransform.layerId;
    snapshot.maskTarget = m_pendingTransform.maskTarget;
    snapshot.beforeTiles = std::move(m_pendingTransform.beforeTiles);
    snapshot.createdTiles = std::move(m_pendingTransform.createdTiles);
    snapshot.removedTiles = std::move(m_pendingTransform.removedTiles);

    // 3. Prune empty tiles
    grid->pruneEmpty();

    // 4. After-snapshots from freshly synced CPU pixels. Size by the target
    //    grid's own format (content = doc format, mask = RGBA8) so they match the
    //    format-sized before-snapshot and the TransformCommand consumer, which
    //    rejects any after-tile whose size != tileByteSize(grid->format()). A
    //    fixed TILE_BYTE_SIZE truncated 16F/32F content and broke transform redo.
    const size_t contentTileBytes = aether::tileByteSize(grid->format());
    for (const auto& key : m_pendingTransform.readbackKeysOrdered) {
        if (snapshot.removedTiles.count(key)) {
            snapshot.afterTiles[key].resize(contentTileBytes, 0);
            continue;
        }
        const TileData* tile = grid->getTile(key);
        if (tile) {
            auto& buf = snapshot.afterTiles[key];
            buf.resize(contentTileBytes);
            std::memcpy(buf.data(), tile->pixels(), contentTileBytes);
        } else {
            // Tile was pruned (empty after transform)
            snapshot.afterTiles[key].resize(contentTileBytes, 0);
            snapshot.removedTiles.insert(key);
        }
    }
    // Also add empty data for tiles that existed before but not in result
    for (const auto& key : m_pendingTransform.beforeKeys) {
        if (m_pendingTransform.resultKeys.find(key) == m_pendingTransform.resultKeys.end()) {
            if (snapshot.afterTiles.find(key) == snapshot.afterTiles.end()) {
                snapshot.afterTiles[key].resize(contentTileBytes, 0);
            }
        }
    }

    // 5. Build selection restore: when selection was transformed with content,
    //    before=original selection, after=transformed selection (atomic undo/redo).
    std::optional<SelectionRestoreContext> selRestore;
    if (!m_pendingTransform.selectionBefore.isEmpty()) {
        SelectionRestoreContext ctx;
        ctx.layerSelection = m_layerModel ? m_layerModel->selectionManager() : nullptr;
        ctx.lassoSelection
            = m_selectionController ? &m_selectionController->lassoSelection() : nullptr;
        ctx.canvas = &m_canvas;
        ctx.before.layer = captureLayerSelection(ctx.layerSelection);
        ctx.before.lasso = m_pendingTransform.selectionBefore;
        ctx.after.layer = ctx.before.layer;
        // Capture from the live manager so the after-state carries the actual
        // post-transform mask tile snapshot (and correct softAlpha flag), not
        // just transformed regions. Required for redo to faithfully restore
        // soft-alpha selections instead of replaying polygons at strength=255.
        ctx.after.lasso = captureLassoSelection(
            m_selectionController ? &m_selectionController->lassoSelection() : nullptr,
            effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
        ctx.layerExists = [this](const ruwa::core::layers::LayerId& id) {
            return m_layerModel && m_layerModel->contains(id);
        };
        ctx.requestRender = [this]() { requestRender(); };
        ctx.onBeforeRestore = [this]() { m_ignoreSelectionChange = true; };
        ctx.onAfterRestore = [this]() {
            m_ignoreSelectionChange = false;
            m_lastSelectionState.layer
                = captureLayerSelection(m_layerModel ? m_layerModel->selectionManager() : nullptr);
            m_lastSelectionState.lasso = captureLassoSelection(
                m_selectionController ? &m_selectionController->lassoSelection() : nullptr,
                effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
        };
        selRestore = std::move(ctx);
    } else {
        selRestore = buildCurrentSelectionRestore();
    }

    // 6. Push undo command (single command: content + selection together)
    auto cmd = std::make_unique<TransformCommand>(
        &m_canvas, m_layerModel, std::move(snapshot), std::move(selRestore));
    m_canvas.undoManager().push(std::move(cmd));

    m_pendingTransform = {};
    return true;
}

bool OpenGLCanvasWidget::hasPendingSelectionTransform() const
{
    return m_pendingTransform.active && m_pendingTransform.applySelectionMask
        && !m_pendingTransform.selectionBefore.isEmpty();
}

const TransformState* OpenGLCanvasWidget::selectionDisplayTransformState() const
{
    if (m_transformController.isActive()) {
        return &m_transformController.state();
    }
    if (hasPendingSelectionTransform()) {
        return &m_pendingTransform.selectionTransformState;
    }
    return nullptr;
}

void OpenGLCanvasWidget::flushPendingTransformFinalization()
{
    m_transformFinalizeTimer.stop();
    if (m_pendingTransform.active) {
        tryFinalizeTransform(true);
    }
}

// ==========================================================================
//   B R U S H   &   E Y E D R O P P E R   C U R S O R
// ==========================================================================

void OpenGLCanvasWidget::setBrushCursorState(
    bool visible, float centerX, float centerY, float radiusPx)
{
    m_cursorOverlayState.brushVisible = visible;
    m_cursorOverlayState.brushCenterX = centerX;
    m_cursorOverlayState.brushCenterY = centerY;
    m_cursorOverlayState.brushRadius = radiusPx;
    update();
}

void OpenGLCanvasWidget::setEyedropperCursorState(
    bool visible, float centerX, float centerY, const QColor& selectedColor)
{
    m_cursorOverlayState.eyedropperVisible = visible;
    m_cursorOverlayState.eyedropperCenterX = centerX;
    m_cursorOverlayState.eyedropperCenterY = centerY;
    m_cursorOverlayState.eyedropperSelectedR = static_cast<float>(selectedColor.redF());
    m_cursorOverlayState.eyedropperSelectedG = static_cast<float>(selectedColor.greenF());
    m_cursorOverlayState.eyedropperSelectedB = static_cast<float>(selectedColor.blueF());
    m_cursorOverlayState.eyedropperSelectedA = static_cast<float>(selectedColor.alphaF());
    update();
}

bool OpenGLCanvasWidget::sampleColorFromScene(float worldX, float worldY, QColor& out)
{
    if (!m_initialized || !m_sceneFboManager.sceneFbo() || !m_sceneFboManager.sceneTexture()
        || width() <= 0 || height() <= 0)
        return false;

    const Vector2 viewportSize(static_cast<float>(width()), static_cast<float>(height()));
    const Vector2 screenPos = screenFromDocumentWorld(Vector2(worldX, worldY));

    const int px = static_cast<int>(std::floor(screenPos.x));
    const int py = static_cast<int>(std::floor(screenPos.y));
    if (px < 0 || px >= width() || py < 0 || py >= height())
        return false;

    makeCurrent();

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFboManager.sceneFbo());

    // glReadPixels: origin bottom-left; screen coords are top-left
    const int readY = height() - 1 - py;
    uint8_t rgba[4];
    glReadPixels(px, readY, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));

    out = QColor(static_cast<int>(rgba[0]), static_cast<int>(rgba[1]), static_cast<int>(rgba[2]),
        static_cast<int>(rgba[3]));
    return true;
}

void OpenGLCanvasWidget::synchronizeCompositionForReadback()
{
    if (!m_initialized || !m_renderer || !m_layerCompositingBuilder) {
        return;
    }

    makeCurrent();
    const auto& layerStack = m_layerCompositingBuilder->buildLayerStack();
    if (layerStack.empty()) {
        return;
    }

    // The live paint path only composites tiles that are BOTH visible and
    // uncached (collectVisibleUncachedKeys) — an off-screen content tile that
    // has never entered the viewport at the current camera is therefore ABSENT
    // from the composition grid AND is not flagged dirty. compositeAllDirty
    // alone would skip those tiles, so a readback (export / clipboard / thumbs)
    // would come out cropped to whatever the user happened to have on screen.
    // Force every content key that isn't cached to recomposite so the whole
    // document is present before we read it back.
    auto& compositionCache = m_canvas.compositionCache();
    std::unordered_set<TileKey, TileKeyHash> contentKeys;
    collectCompositeLayerKeys(layerStack, contentKeys);
    for (const TileKey& key : contentKeys) {
        if (!compositionCache.grid().hasTile(key)) {
            compositionCache.markDirty(key);
        }
    }

    Color canvasBackdrop = Color::transparent();
    m_layerCompositingBuilder->resolveCanvasBackgroundColor(canvasBackdrop);
    m_renderer->compositeAllDirty(layerStack, compositionCache, canvasBackdrop);
}

bool OpenGLCanvasWidget::computeExportContentBounds(QRect& outBounds)
{
    if (!m_layerModel || !m_layerCompositingBuilder) {
        return false;
    }

    synchronizeCompositionForReadback();

    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
    if (!computeExportLayerBoundsRecursive(m_layerModel->rootLayers(),
            m_layerCompositingBuilder.get(), true, minX, minY, maxX, maxY)) {
        return false;
    }

    outBounds = QRect(minX, minY, maxX - minX + 1, maxY - minY + 1);
    return outBounds.isValid() && !outBounds.isEmpty();
}

bool OpenGLCanvasWidget::computeNavigatorContentBounds(QRect& outBounds)
{
    if (!m_layerModel || !m_layerCompositingBuilder) {
        return false;
    }

    synchronizeCompositionForReadback();

    std::unordered_set<TileKey, TileKeyHash> keys;
    collectCompositeLayerKeys(m_layerCompositingBuilder->buildLayerStack(), keys);
    if (keys.empty()) {
        return false;
    }

    auto it = keys.begin();
    int minX = it->x;
    int minY = it->y;
    int maxX = it->x;
    int maxY = it->y;
    ++it;
    for (; it != keys.end(); ++it) {
        minX = std::min(minX, it->x);
        minY = std::min(minY, it->y);
        maxX = std::max(maxX, it->x);
        maxY = std::max(maxY, it->y);
    }

    const int tileSize = static_cast<int>(TILE_SIZE);
    minX *= tileSize;
    minY *= tileSize;
    maxX = (maxX + 1) * tileSize - 1;
    maxY = (maxY + 1) * tileSize - 1;

    outBounds = QRect(minX, minY, maxX - minX + 1, maxY - minY + 1);
    return outBounds.isValid() && !outBounds.isEmpty();
}

QImage OpenGLCanvasWidget::renderCompositedRegion(const QRect& worldRect, const QSize& targetSize)
{
    if (!m_initialized || !m_renderer || !targetSize.isValid() || targetSize.isEmpty()) {
        return {};
    }

    const QRect normalizedRect = worldRect.normalized();
    if (!normalizedRect.isValid() || normalizedRect.isEmpty()) {
        return {};
    }

    const uint32_t targetW = static_cast<uint32_t>(targetSize.width());
    const uint32_t targetH = static_cast<uint32_t>(targetSize.height());
    if (targetW == 0 || targetH == 0) {
        return {};
    }

    synchronizeCompositionForReadback();

    GLuint exportFbo = 0;
    GLuint exportTex = 0;
    glGenTextures(1, &exportTex);
    glBindTexture(GL_TEXTURE_2D, exportTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(targetW),
        static_cast<GLsizei>(targetH), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    GLint prevFbo = 0;
    GLint prevViewport[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    glGenFramebuffers(1, &exportFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, exportFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, exportTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        glDeleteFramebuffers(1, &exportFbo);
        glDeleteTextures(1, &exportTex);
        return {};
    }
    glViewport(0, 0, static_cast<GLsizei>(targetW), static_cast<GLsizei>(targetH));

    Viewport overviewViewport(targetW, targetH);
    const float zoomX = static_cast<float>(targetW) / static_cast<float>(normalizedRect.width());
    const float zoomY = static_cast<float>(targetH) / static_cast<float>(normalizedRect.height());
    overviewViewport.camera().setZoomLimits(0.001f, std::max(zoomX, zoomY));
    overviewViewport.camera().setPosition(static_cast<float>(normalizedRect.center().x()) + 0.5f,
        static_cast<float>(normalizedRect.center().y()) + 0.5f);
    overviewViewport.camera().setZoom(std::min(zoomX, zoomY));
    overviewViewport.camera().setRotation(0.0f);

    m_renderer->beginFrame(targetW, targetH);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    Color canvasBg;
    if (m_layerCompositingBuilder
        && m_layerCompositingBuilder->resolveCanvasBackgroundColor(canvasBg)) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_renderer->drawBackground(canvasBg);
        m_renderer->drawCanvas(m_canvas, overviewViewport, canvasBg, canvasBg, 1.0f);
        glDisable(GL_BLEND);
    }

    const bool clipTilesToDocumentBounds = hasFiniteDocumentBounds();
    const uint32_t tileClipWidth = clipTilesToDocumentBounds ? m_canvas.width() : 0u;
    const uint32_t tileClipHeight = clipTilesToDocumentBounds ? m_canvas.height() : 0u;
    m_renderer->drawTiles(
        m_canvas.compositionGrid(), overviewViewport, tileClipWidth, tileClipHeight);

    if (m_layerCompositingBuilder && !m_exportPreviewHideBoardLayers) {
        const auto& boardLayerStack = m_layerCompositingBuilder->buildBoardLayerStack();
        if (!boardLayerStack.empty()) {
            std::unordered_set<TileKey, TileKeyHash> boardKeys;
            collectCompositeLayerKeys(boardLayerStack, boardKeys);
            if (!boardKeys.empty()) {
                CompositionCache boardCache;
                boardCache.markDirty(boardKeys);
                m_renderer->compositeAllDirty(boardLayerStack, boardCache);
                m_renderer->drawTiles(boardCache.grid(), overviewViewport, 0u, 0u);
            }
        }
    }

    m_renderer->endFrame();

    std::vector<uint8_t> pixels(static_cast<size_t>(targetW) * targetH * 4);
    glReadPixels(0, 0, static_cast<GLsizei>(targetW), static_cast<GLsizei>(targetH), GL_RGBA,
        GL_UNSIGNED_BYTE, pixels.data());

    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glDeleteFramebuffers(1, &exportFbo);
    glDeleteTextures(1, &exportTex);

    QImage image(static_cast<int>(targetW), static_cast<int>(targetH), QImage::Format_RGBA8888);
    const int bytesPerLine = static_cast<int>(targetW) * 4;
    for (int y = static_cast<int>(targetH) - 1; y >= 0; --y) {
        const int srcRow = static_cast<int>(targetH) - 1 - y;
        std::memcpy(image.scanLine(y), pixels.data() + srcRow * bytesPerLine,
            static_cast<size_t>(bytesPerLine));
    }

    return image;
}

QImage OpenGLCanvasWidget::grabCanvasImage()
{
    return grabCanvasImage(
        QRect(0, 0, static_cast<int>(m_canvas.width()), static_cast<int>(m_canvas.height())));
}

QImage OpenGLCanvasWidget::grabCanvasImage(const QRect& worldRect)
{
    if (!m_initialized || !m_renderer) {
        return QImage();
    }

    const QRect normalizedRect = worldRect.normalized();
    if (!normalizedRect.isValid() || normalizedRect.isEmpty()) {
        return QImage();
    }

    const uint32_t cw = static_cast<uint32_t>(normalizedRect.width());
    const uint32_t ch = static_cast<uint32_t>(normalizedRect.height());
    if (cw == 0 || ch == 0) {
        return QImage();
    }

    synchronizeCompositionForReadback();

    // Create offscreen FBO for export
    GLuint exportFbo = 0;
    GLuint exportTex = 0;
    glGenTextures(1, &exportTex);
    glBindTexture(GL_TEXTURE_2D, exportTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(cw), static_cast<GLsizei>(ch), 0,
        GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &exportFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, exportFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, exportTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &exportFbo);
        glDeleteTextures(1, &exportTex);
        return QImage();
    }

    // Save viewport and camera state
    const uint32_t prevW = m_viewport.width();
    const uint32_t prevH = m_viewport.height();
    const Vector2 prevPos = m_viewport.camera().position();
    const float prevZoom = m_viewport.camera().zoom();
    const float prevRot = m_viewport.camera().rotation();
    const float prevMinZoom = m_viewport.camera().minZoom();
    const float prevMaxZoom = m_viewport.camera().maxZoom();
    const bool prevFlipH = m_canvasContentFlipHorizontal;
    const bool prevFlipV = m_canvasContentFlipVertical;

    // Set viewport for 1:1 canvas render. The interactive zoom limits are sized
    // to the on-screen viewport: a SMALL document shown in a large window has a
    // minimum zoom well above 1.0 (you cannot shrink a tiny canvas to nothing),
    // so a bare setZoom(1.0) would be clamped UP and the readback would come out
    // zoomed-in and cropped at the correct resolution. Relax the limits to admit
    // 1:1 for the duration of the grab, then restore them.
    m_viewport.resize(cw, ch);
    m_viewport.camera().setZoomLimits(0.001f, std::max(prevMaxZoom, 1.0f));
    m_viewport.camera().setPosition(
        static_cast<float>(normalizedRect.x()) + static_cast<float>(normalizedRect.width()) * 0.5f,
        static_cast<float>(normalizedRect.y())
            + static_cast<float>(normalizedRect.height()) * 0.5f);
    m_viewport.camera().setZoom(1.0f);
    m_viewport.camera().setRotation(0.0f);
    m_canvasContentFlipHorizontal = false;
    m_canvasContentFlipVertical = false;

    glViewport(0, 0, static_cast<GLsizei>(cw), static_cast<GLsizei>(ch));

    // Render canvas to FBO
    m_renderer->beginFrame(cw, ch);

    // Clear to transparent so hidden/transparent background exports correctly
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Canvas background: only when visible and not fully transparent
    Color canvasBg;
    if (m_layerCompositingBuilder->resolveCanvasBackgroundColor(canvasBg)) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_renderer->drawBackground(canvasBg);
        m_renderer->drawCanvas(m_canvas, m_viewport, canvasBg, canvasBg, 1.0f);
        glDisable(GL_BLEND);
    }
    // When false: background hidden or transparent â€” leave transparent, composition tiles only

    const bool clipTilesToDocumentBounds = hasFiniteDocumentBounds();
    const uint32_t tileClipWidth = clipTilesToDocumentBounds ? m_canvas.width() : 0u;
    const uint32_t tileClipHeight = clipTilesToDocumentBounds ? m_canvas.height() : 0u;
    m_renderer->drawTiles(m_canvas.compositionGrid(), m_viewport, tileClipWidth, tileClipHeight);
    m_renderer->endFrame();

    // Read pixels (glReadPixels origin is bottom-left; QImage row 0 is top)
    std::vector<uint8_t> pixels(static_cast<size_t>(cw) * ch * 4);
    glReadPixels(0, 0, static_cast<GLsizei>(cw), static_cast<GLsizei>(ch), GL_RGBA,
        GL_UNSIGNED_BYTE, pixels.data());

    // Restore viewport and camera (limits first, so prevZoom is not re-clamped).
    m_viewport.resize(prevW, prevH);
    m_viewport.camera().setZoomLimits(prevMinZoom, prevMaxZoom);
    m_viewport.camera().setPosition(prevPos.x, prevPos.y);
    m_viewport.camera().setZoom(prevZoom);
    m_viewport.camera().setRotation(prevRot);
    m_canvasContentFlipHorizontal = prevFlipH;
    m_canvasContentFlipVertical = prevFlipV;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &exportFbo);
    glDeleteTextures(1, &exportTex);

    // Build QImage (flip Y: glReadPixels row 0 = bottom, QImage row 0 = top).
    // The export FBO accumulates PREMULTIPLIED alpha: the tile shader emits
    // straight color, but the tile blend (GL_SRC_ALPHA / GL_ONE for alpha) folds
    // color back to C*a while keeping alpha = a. Label the buffer accordingly and
    // let Qt un-premultiply, otherwise semi-transparent pixels export too dark.
    QImage image(static_cast<int>(cw), static_cast<int>(ch), QImage::Format_RGBA8888_Premultiplied);
    const int bytesPerLine = static_cast<int>(cw) * 4;
    for (int y = static_cast<int>(ch) - 1; y >= 0; --y) {
        const int srcRow = static_cast<int>(ch) - 1 - y;
        std::memcpy(image.scanLine(y), pixels.data() + srcRow * bytesPerLine,
            static_cast<size_t>(bytesPerLine));
    }

    // Convert to straight alpha for export (PNG/JPEG/WebP store non-premultiplied).
    return image.convertToFormat(QImage::Format_RGBA8888);
}

void OpenGLCanvasWidget::updateBrushCursorStamp()
{
    if (!m_initialized || !m_overlayManager || !m_brush)
        return;
    auto* overlay = m_overlayManager->brushCursorOverlay();
    if (!overlay)
        return;

    if (!m_brushCursorContourBuilder) {
        m_brushCursorContourBuilder = std::make_unique<aether::BrushCursorContourBuilder>(this);
        connect(m_brushCursorContourBuilder.get(),
            &aether::BrushCursorContourBuilder::contoursReady, this,
            [this](std::vector<std::vector<aether::Vector2>> contours) {
                if (!m_overlayManager)
                    return;
                auto* o = m_overlayManager->brushCursorOverlay();
                if (!o)
                    return;
                o->setStampContours(contours);
                if (m_cursorOverlayState.brushVisible) {
                    update();
                }
            });
    }

    aether::BrushCursorContourBuilder::Request req;
    req.dabType = m_brush->dabType();
    req.roundness = m_brush->roundness();
    req.angleDegrees = m_brush->angleDegrees();
    req.dabXScale = m_brush->dabXScale();
    req.dabYScale = m_brush->dabYScale();
    req.dabRotation = m_brush->dabRotation();

    // Fetch alpha grid on the GUI thread (DabShapeCache is not thread-safe).
    if (req.dabType > 0) {
        auto& cache = aether::DabShapeCache::instance();
        aether::DabShapeCache::AlphaGrid grid;
        const QString& customPath = m_brush->dabCustomImagePath();
        if (!customPath.isEmpty()) {
            grid = cache.getCustomAlphaGrid(customPath, m_brush->dabThreshold(),
                m_brush->dabCompression(), m_brush->dabInterpolation());
        } else {
            grid = cache.getAlphaGrid(req.dabType);
        }
        req.alphaMask = std::move(grid.data);
        req.maskWidth = grid.width;
        req.maskHeight = grid.height;
    }

    m_brushCursorContourBuilder->submit(std::move(req));
}

// ==========================================================================
//   T R A N S F O R M   M O D E
// ==========================================================================

bool OpenGLCanvasWidget::transformViewportPreviewSupportsViewportPath(
    const TransformState& state) const
{
    Q_UNUSED(state);
    if (!m_transformTargetSet.empty() && !m_transformTargetSet.singleVisualTarget()) {
        return !m_transformTargetSet.previewBlocks.empty();
    }
    return true;
}

void OpenGLCanvasWidget::refreshTransformViewportPreviewCapabilities()
{
    if (!m_transformViewportPreview.active || !m_transformController.isActive()) {
        return;
    }

    const bool nextEnabled
        = transformViewportPreviewSupportsViewportPath(m_transformController.state());
    if (m_transformViewportPreview.viewportPathEnabled != nextEnabled) {
        m_transformViewportPreview.viewportPathEnabled = nextEnabled;
        m_transformViewportPreview.viewportDirty = true;
        m_transformViewportPreview.transformDirty = true;
        m_transformViewportPreview.sourceDirty = true;
        m_transformViewportPreview.selectionMaskDirty = true;
        m_canvas.dirtyManager().onStructureChanged();
        m_prevTransformDirtyValid = false;
    }
}

void OpenGLCanvasWidget::activateTransformViewportPreview(
    const QUuid& targetLayerId, const QUuid& sourceLayerId)
{
    m_transformViewportPreview = {};
    m_transformViewportPreview.active = true;
    m_transformViewportPreview.targetLayerId = targetLayerId;
    m_transformViewportPreview.sourceLayerId = sourceLayerId;
    m_transformViewportPreview.viewportPathEnabled = m_transformController.isActive()
        && transformViewportPreviewSupportsViewportPath(m_transformController.state());
    m_transformViewportPreview.viewportDirty = true;
    m_transformViewportPreview.transformDirty = true;
    m_transformViewportPreview.sourceDirty = true;
    m_transformViewportPreview.selectionMaskDirty = true;
}

void OpenGLCanvasWidget::clearTransformViewportPreview()
{
    m_transformViewportPreview = {};
}

void OpenGLCanvasWidget::clearTransformPreviewCacheTiles(const Rect& currentBounds)
{
    auto removeBounds = [this](Rect bounds) {
        if (bounds.width <= 0.0f || bounds.height <= 0.0f) {
            return;
        }

        constexpr float kTransformMargin = 2.0f;
        bounds.x -= kTransformMargin;
        bounds.y -= kTransformMargin;
        bounds.width += kTransformMargin * 2.0f;
        bounds.height += kTransformMargin * 2.0f;

        const int32_t minTX = static_cast<int32_t>(std::floor(bounds.left() / TILE_SIZE));
        const int32_t minTY = static_cast<int32_t>(std::floor(bounds.top() / TILE_SIZE));
        const int32_t maxTX = static_cast<int32_t>(std::floor(bounds.right() / TILE_SIZE));
        const int32_t maxTY = static_cast<int32_t>(std::floor(bounds.bottom() / TILE_SIZE));
        for (int32_t ty = minTY; ty <= maxTY; ++ty) {
            for (int32_t tx = minTX; tx <= maxTX; ++tx) {
                m_canvas.compositionCache().removeTile(TileKey { tx, ty });
            }
        }
    };

    removeBounds(currentBounds);
    if (m_prevTransformDirtyValid) {
        Rect previousBounds { static_cast<float>(m_prevTransformMinTX) * TILE_SIZE,
            static_cast<float>(m_prevTransformMinTY) * TILE_SIZE,
            static_cast<float>(m_prevTransformMaxTX - m_prevTransformMinTX + 1) * TILE_SIZE,
            static_cast<float>(m_prevTransformMaxTY - m_prevTransformMinTY + 1) * TILE_SIZE };
        removeBounds(previousBounds);
    }
    m_prevTransformDirtyValid = false;
}

void OpenGLCanvasWidget::invalidateTransformViewportPreviewTransform()
{
    if (!m_transformViewportPreview.active) {
        return;
    }
    refreshTransformViewportPreviewCapabilities();
    m_transformViewportPreview.transformDirty = true;
}

void OpenGLCanvasWidget::invalidateTransformViewportPreviewSource()
{
    if (!m_transformViewportPreview.active) {
        return;
    }
    refreshTransformViewportPreviewCapabilities();
    m_transformViewportPreview.sourceDirty = true;
}

void OpenGLCanvasWidget::invalidateTransformViewportPreviewSelectionMask()
{
    if (!m_transformViewportPreview.active) {
        return;
    }
    refreshTransformViewportPreviewCapabilities();
    m_transformViewportPreview.selectionMaskDirty = true;
}

bool OpenGLCanvasWidget::latchSelectionCopyMoveTransformIfNeeded(
    const Vector2& worldPos, Qt::KeyboardModifiers mods)
{
    if (m_selectionCopyMoveTransform || m_layerCopyMoveTransform) {
        return false;
    }
    const Qt::KeyboardModifiers required = Qt::ControlModifier | Qt::AltModifier;
    if ((mods & required) != required) {
        return false;
    }
    const bool hasSelectionMask = m_selectionController
        && m_selectionController->lassoSelection().hasSelection()
        && !m_selectionController->lassoSelection().mask().empty();
    if (!m_transformController.isActive() || !m_transformController.isDragging()
        || !m_moveOnlyTransform || m_transformController.hasChanges()
        || !m_transformController.moveDragHasNonzeroPixelAlignedOffset(worldPos)) {
        return false;
    }

    if (!hasSelectionMask) {
        return latchLayerCopyMoveTransform();
    }

    m_selectionCopyMoveTransform = true;
    invalidateCachedLayerStacks();
    invalidateTransformViewportPreviewTransform();
    return true;
}

bool OpenGLCanvasWidget::latchLayerCopyMoveTransform()
{
    if (!m_layerModel || m_transformTargetSet.empty()) {
        return false;
    }

    const Rect oldCacheBounds = m_transformTargetSet.contentBounds;
    const QList<ruwa::core::layers::LayerId> addedIds = m_layerModel->duplicateSelectedLayers();
    if (addedIds.isEmpty()) {
        return false;
    }

    const QList<ruwa::core::layers::LayerData*> flat = m_layerModel->allLayersFlattened();
    QList<ruwa::core::layers::LayerData*> addedRoots;
    for (ruwa::core::layers::LayerData* layer : flat) {
        if (!layer || !addedIds.contains(layer->id)) {
            continue;
        }
        bool hasAddedAncestor = false;
        for (ruwa::core::layers::LayerData* anc = layer->parent; anc; anc = anc->parent) {
            if (addedIds.contains(anc->id)) {
                hasAddedAncestor = true;
                break;
            }
        }
        if (!hasAddedAncestor) {
            addedRoots.append(layer);
        }
    }

    m_layerCopyMoveAddedLayers.clear();
    m_layerCopyMoveAddedPositions.clear();
    for (ruwa::core::layers::LayerData* root : addedRoots) {
        auto clone = ruwa::core::layers::LayerModel::cloneLayerTree(root, true);
        if (!clone) {
            continue;
        }

        ruwa::core::layers::LayerId parentId;
        int index = -1;
        if (root->parent) {
            parentId = root->parent->id;
            index = root->indexInParent();
        } else {
            const auto& roots = m_layerModel->rootLayers();
            for (int i = 0; i < roots.size(); ++i) {
                if (roots[i].get() == root) {
                    index = i;
                    break;
                }
            }
        }

        m_layerCopyMoveAddedLayers.append(clone);
        m_layerCopyMoveAddedPositions.append({ parentId, index });
    }

    if (m_layerCopyMoveAddedLayers.isEmpty()) {
        m_layerCopyMoveTransform = true;
        m_layerCopyMoveAddedIds = addedIds;
        discardLayerCopyMoveDuplicates();
        return false;
    }

    m_transformTargetSet = buildTransformTargetSet(*m_layerModel, transformBoundsForLayer);
    if (m_transformTargetSet.empty()) {
        m_layerCopyMoveTransform = true;
        m_layerCopyMoveAddedIds = addedIds;
        discardLayerCopyMoveDuplicates();
        return false;
    }

    m_layerCopyMoveTransform = true;
    m_layerCopyMoveAddedIds = addedIds;
    clearTransformPreviewCacheTiles(oldCacheBounds);
    if (m_transformTargetSet.singleVisualTarget()) {
        auto* layer = m_layerModel->layerById(m_transformTargetSet.visualTargets.front().layerId);
        if (layer && !layer->isText()) {
            activateTransformViewportPreview(layer->id, layer->id);
        } else {
            clearTransformViewportPreview();
        }
    } else if (!m_transformTargetSet.previewBlocks.empty()) {
        const QUuid insertionLayerId
            = m_transformTargetSet.previewBlocks.front().topInsertionLayerId;
        activateTransformViewportPreview(insertionLayerId, insertionLayerId);
    } else {
        clearTransformViewportPreview();
    }
    invalidateCachedLayerStacks();
    invalidateTransformViewportPreviewSource();
    invalidateTransformViewportPreviewTransform();
    m_canvas.dirtyManager().onStructureChanged();
    requestRender();
    return true;
}

void OpenGLCanvasWidget::commitLayerCopyMoveAddUndo()
{
    if (!m_layerCopyMoveTransform) {
        return;
    }

    if (m_layerModel && !m_layerCopyMoveAddedLayers.isEmpty()) {
        auto cmd = std::make_unique<LayerAddCommand>(
            m_layerModel, std::move(m_layerCopyMoveAddedLayers),
            std::move(m_layerCopyMoveAddedPositions), [this]() { requestRender(); },
            [this]() { notifyCanvasInteraction(true); });
        m_canvas.undoManager().push(std::move(cmd));
    }

    clearLayerCopyMoveState();
}

void OpenGLCanvasWidget::discardLayerCopyMoveDuplicates()
{
    if (m_layerCopyMoveTransform && m_layerModel && !m_layerCopyMoveAddedIds.isEmpty()) {
        QList<ruwa::core::layers::LayerId> idsToRemove;
        for (const auto& id : m_layerCopyMoveAddedIds) {
            if (m_layerModel->contains(id)) {
                idsToRemove.append(id);
            }
        }
        if (!idsToRemove.isEmpty()) {
            m_layerModel->removeLayers(idsToRemove);
        }
    }

    clearLayerCopyMoveState();
}

void OpenGLCanvasWidget::clearLayerCopyMoveState()
{
    m_layerCopyMoveTransform = false;
    m_layerCopyMoveAddedIds.clear();
    m_layerCopyMoveAddedLayers.clear();
    m_layerCopyMoveAddedPositions.clear();
}

UndoManager* OpenGLCanvasWidget::activeUndoManager()
{
    if (m_transformController.isActive()) {
        if (m_transformController.isDragging()) {
            return nullptr;
        }
        return m_transformUndoManager.get();
    }
    return &m_canvas.undoManager();
}

const UndoManager* OpenGLCanvasWidget::activeUndoManager() const
{
    if (m_transformController.isActive()) {
        if (m_transformController.isDragging()) {
            return nullptr;
        }
        return m_transformUndoManager.get();
    }
    return &m_canvas.undoManager();
}

void OpenGLCanvasWidget::createTransformUndoStack()
{
    m_transformUndoStepBefore.reset();
    m_transformUndoStepBeforeMode.reset();
    m_transformUndoManager = std::make_unique<UndoManager>(this);
    m_transformUndoManager->setMemoryLimit(m_canvas.undoManager().memoryLimit());
}

void OpenGLCanvasWidget::destroyTransformUndoStack()
{
    m_transformUndoStepBefore.reset();
    m_transformUndoStepBeforeMode.reset();
    m_transformUndoManager.reset();
}

void OpenGLCanvasWidget::beginTransformUndoStep()
{
    if (!m_transformController.isActive() || !m_transformUndoManager) {
        m_transformUndoStepBefore.reset();
        m_transformUndoStepBeforeMode.reset();
        return;
    }
    m_transformUndoStepBefore = m_transformController.state();
    m_transformUndoStepBeforeMode = m_transformController.interactionMode();
}

void OpenGLCanvasWidget::commitTransformUndoStep()
{
    if (!m_transformController.isActive() || !m_transformUndoManager
        || !m_transformUndoStepBefore.has_value() || !m_transformUndoStepBeforeMode.has_value()) {
        m_transformUndoStepBefore.reset();
        m_transformUndoStepBeforeMode.reset();
        return;
    }

    m_transformController.finalizePendingAnimation();
    const TransformState after = m_transformController.state();
    const TransformState before = *m_transformUndoStepBefore;
    const TransformInteractionMode afterMode = m_transformController.interactionMode();
    const TransformInteractionMode beforeMode = *m_transformUndoStepBeforeMode;
    m_transformUndoStepBefore.reset();
    m_transformUndoStepBeforeMode.reset();

    if (beforeMode == afterMode && transformStatesNearlyEqual(before, after)) {
        onTransformUndoStateRestored();
        return;
    }

    auto cmd = std::make_unique<TransformSessionCommand>(&m_transformController, before, beforeMode,
        after, afterMode, [this]() { onTransformUndoStateRestored(); });
    m_transformUndoManager->push(std::move(cmd));
    onTransformUndoStateRestored();
}

void OpenGLCanvasWidget::discardTransformUndoStep()
{
    m_transformUndoStepBefore.reset();
    m_transformUndoStepBeforeMode.reset();
}

void OpenGLCanvasWidget::onTransformUndoStateRestored()
{
    m_prevTransformDirtyValid = false;
    invalidateTransformViewportPreviewTransform();
    m_canvas.dirtyManager().onStructureChanged();
    requestRender();
}

bool OpenGLCanvasWidget::enterSelectedTransformMode(bool moveOnly)
{
    if (m_transformController.isActive())
        return false;
    if (m_strokeHost && m_strokeHost->isDrawing())
        return false;
    if (!m_layerModel)
        return false;

    m_selectionCopyMoveTransform = false;
    m_transformEditingMask = false;
    clearLayerCopyMoveState();
    m_transformTargetSet = buildTransformTargetSet(*m_layerModel, transformBoundsForLayer);
    if (m_transformTargetSet.empty() || m_transformTargetSet.contentBounds.width <= 0.0f
        || m_transformTargetSet.contentBounds.height <= 0.0f) {
        m_transformTargetSet.clear();
        m_selectionCopyMoveTransform = false;
        return false;
    }

    const bool hasSelectionMask = m_selectionController
        && m_selectionController->lassoSelection().hasSelection()
        && !m_selectionController->lassoSelection().mask().empty();
    const TileGrid* selectionMask
        = hasSelectionMask ? &m_selectionController->lassoSelection().mask() : nullptr;

    bool singleTarget = m_transformTargetSet.singleVisualTarget();
    auto* layer = singleTarget
        ? m_layerModel->layerById(m_transformTargetSet.visualTargets.front().layerId)
        : nullptr;

    if (!singleTarget) {
        if (!offerRasterizeForSelectionTransformTargets(hasSelectionMask)) {
            m_transformTargetSet.clear();
            m_selectionCopyMoveTransform = false;
            return false;
        }
        if (m_transformTargetSet.empty() || m_transformTargetSet.contentBounds.width <= 0.0f
            || m_transformTargetSet.contentBounds.height <= 0.0f) {
            m_transformTargetSet.clear();
            m_selectionCopyMoveTransform = false;
            return false;
        }
        singleTarget = m_transformTargetSet.singleVisualTarget();
        layer = singleTarget
            ? m_layerModel->layerById(m_transformTargetSet.visualTargets.front().layerId)
            : nullptr;
    }

    bool entered = false;
    if (singleTarget && layer) {
        if (!isLayerCanvasEditable(layer)) {
            m_transformTargetSet.clear();
            m_selectionCopyMoveTransform = false;
            return false;
        }

        if (layer->isText() && hasSelectionMask) {
            if (!offerRasterizeForSelectionTransform(layer, hasSelectionMask)) {
                m_transformTargetSet.clear();
                m_selectionCopyMoveTransform = false;
                return false;
            }
            layer = m_layerModel->layerById(m_transformTargetSet.visualTargets.front().layerId);
            if (!isLayerCanvasEditable(layer)) {
                m_transformTargetSet.clear();
                m_selectionCopyMoveTransform = false;
                return false;
            }
        }

        if (layer->isText()) {
            if (!layer->textData || !m_layerCompositingBuilder) {
                m_transformTargetSet.clear();
                m_selectionCopyMoveTransform = false;
                return false;
            }
            const Rect sourceBounds = computeTextLayoutSourceBounds(*layer->textData);
            if (sourceBounds.width <= 0.0f || sourceBounds.height <= 0.0f) {
                m_transformTargetSet.clear();
                m_selectionCopyMoveTransform = false;
                return false;
            }
            entered = m_transformController.enter(layer->id, sourceBounds, moveOnly);
            if (entered) {
                m_transformController.state()
                    = stateWithSourceBounds(layer->textData->transform, sourceBounds);
                m_transformController.syncAnimatedState();
                m_transformController.captureTransformModeEntryReference();
            }
        } else {
            if (!offerRasterizeForSelectionTransform(layer, hasSelectionMask)) {
                m_transformTargetSet.clear();
                m_selectionCopyMoveTransform = false;
                return false;
            }
            layer = m_layerModel->layerById(m_transformTargetSet.visualTargets.front().layerId);
            if (!isLayerCanvasEditable(layer)) {
                m_transformTargetSet.clear();
                m_selectionCopyMoveTransform = false;
                return false;
            }
            // When the layer's mask is the active paint target, transform warps
            // the mask grid (the pixels stay fixed); otherwise the content grid.
            const bool editingMask = layer->maskEditActive && layer->maskTileGrid();
            TileGrid* transformGrid = editingMask ? layer->maskTileGrid() : layer->pixelGrid();
            if (!transformGrid || transformGrid->empty()) {
                m_transformTargetSet.clear();
                m_selectionCopyMoveTransform = false;
                return false;
            }
            m_transformEditingMask = editingMask;

            entered = editingMask
                ? m_transformController.enter(layer->id, transformGrid, selectionMask, moveOnly)
                : m_transformController.enter(layer, selectionMask, moveOnly);
            if (entered && !editingMask && layer->isIsolatedPixelLayer()) {
                const Rect sourceBounds = m_transformController.state().contentBounds;
                m_transformController.state()
                    = stateWithSourceBounds(layer->smartTransform, sourceBounds);
                m_transformController.syncAnimatedState();
                m_transformController.captureTransformModeEntryReference();
            }
        }
    } else {
        const QUuid sessionLayerId = !m_transformTargetSet.rootLayerIds.empty()
            ? m_transformTargetSet.rootLayerIds.front()
            : m_transformTargetSet.visualTargets.front().layerId;
        const Rect transformContentBounds = selectionMask
            ? TransformState::computeContentBounds(*selectionMask)
            : m_transformTargetSet.contentBounds;
        if (transformContentBounds.width <= 0.0f || transformContentBounds.height <= 0.0f) {
            m_transformTargetSet.clear();
            m_selectionCopyMoveTransform = false;
            return false;
        }
        entered = m_transformController.enter(
            sessionLayerId, transformContentBounds, selectionMask, moveOnly);
    }

    if (!entered) {
        m_transformTargetSet.clear();
        m_selectionCopyMoveTransform = false;
        return false;
    }

    createTransformUndoStack();
    m_moveOnlyTransform = moveOnly;
    m_prevTransformDirtyValid = false;

    if (singleTarget && layer && !layer->isText()) {
        activateTransformViewportPreview(layer->id, layer->id);
    } else if (!singleTarget && !m_transformTargetSet.previewBlocks.empty()) {
        const QUuid insertionLayerId
            = m_transformTargetSet.previewBlocks.front().topInsertionLayerId;
        activateTransformViewportPreview(insertionLayerId, insertionLayerId);
    } else {
        clearTransformViewportPreview();
        invalidateCachedLayerStacks();
    }

    m_canvas.dirtyManager().onStructureChanged();

    auto* overlay = m_overlayManager ? m_overlayManager->transformOverlay() : nullptr;
    if (!moveOnly && !m_autoApplyingTransform && overlay) {
        overlay->onTransformModeEntered();
    }
    emit transformModeEntered();
    requestRender();
    return true;
}

void OpenGLCanvasWidget::enterTransformMode()
{
    enterSelectedTransformMode(false);
}

bool OpenGLCanvasWidget::enterMoveOnlyTransformMode()
{
    return enterSelectedTransformMode(true);
}

QUuid OpenGLCanvasWidget::moveToolContentLayerAt(const Vector2& worldPos) const
{
    if (!m_layerModel) {
        return {};
    }
    return hitTestMoveToolContentLayerList(m_layerModel->rootLayers(), worldPos).targetLayerId;
}

void OpenGLCanvasWidget::setTransformInteractionMode(aether::TransformInteractionMode mode)
{
    if (m_transformController.interactionMode() == mode) {
        return;
    }

    const bool recordUndoStep = m_transformController.isActive()
        && !m_transformController.isDragging() && m_transformUndoManager != nullptr;
    if (recordUndoStep) {
        beginTransformUndoStep();
    }

    m_transformController.setInteractionMode(mode);

    if (recordUndoStep) {
        commitTransformUndoStep();
    }
    invalidateTransformViewportPreviewTransform();
    requestRender();
}

void OpenGLCanvasWidget::rebuildTransformAtlas()
{
    if (!m_transformController.isActive())
        return;
    if (!m_initialized || !m_renderer || !m_renderer->transformRenderer()
        || !m_renderer->tileRenderer())
        return;

    auto* layer = activeLayer();
    if (!layer)
        return;
    if (layer->isText()) {
        invalidateCachedLayerStacks();
        m_prevTransformDirtyValid = false;
        m_canvas.dirtyManager().onStructureChanged();
        return;
    }
    TileGrid* grid = nullptr;
    grid = (m_transformEditingMask && layer->maskTileGrid()) ? layer->maskTileGrid()
                                                             : layer->pixelGrid();
    if (!grid || grid->empty())
        return;

    makeCurrent();
    m_renderer->uploadDirtyTiles(*grid);
    m_renderer->transformRenderer()->buildSourceAtlas(*grid, m_renderer->tileRenderer());
    // Rebuild mask atlas too since buildSourceAtlas destroys it
    m_renderer->transformRenderer()->buildMaskAtlas(
        m_selectionController->lassoSelection().mask(), m_renderer->tileRenderer());
    doneCurrent();
    invalidateTransformViewportPreviewSource();

    m_canvas.dirtyManager().onStructureChanged();
}

void OpenGLCanvasWidget::confirmTransform()
{
    if (!m_transformController.isActive())
        return;

    const bool suppressTransformUi = m_autoApplyingTransform;
    const bool wasMoveOnlyTransform = m_moveOnlyTransform;
    const bool selectionCopyMoveTransform = m_selectionCopyMoveTransform;
    const bool wasEditingMask = m_transformEditingMask;
    m_autoApplyingTransform = false;
    m_moveOnlyTransform = false;
    m_selectionCopyMoveTransform = false;
    m_transformEditingMask = false;
    m_transformController.finalizePendingAnimation();

    const bool multiTargetTransform
        = !m_transformTargetSet.empty() && !m_transformTargetSet.singleVisualTarget();
    auto* layer = activeLayer();
    if (!multiTargetTransform && !m_transformTargetSet.empty()
        && m_transformTargetSet.singleVisualTarget() && m_layerModel) {
        layer = m_layerModel->layerById(m_transformTargetSet.visualTargets.front().layerId);
    }
    if (!layer) {
        cancelTransform(wasMoveOnlyTransform);
        return;
    }

    const TransformState stateCopy = m_transformController.state();

    if (!m_transformController.hasChanges()) {
        // No changes
        m_transformController.cancelAndExit();
        destroyTransformUndoStack();
        m_transformTargetSet.clear();
        m_selectionCopyMoveTransform = false;
        clearTransformViewportPreview();
        clearTransformPreviewCacheTiles(stateCopy.transformedAABB());
        invalidateCachedLayerStacks();
        if (m_renderer && m_renderer->transformRenderer()) {
            makeCurrent();
            m_renderer->transformRenderer()->destroySourceAtlas();
            doneCurrent();
        }
        discardLayerCopyMoveDuplicates();
        auto* overlay = m_overlayManager ? m_overlayManager->transformOverlay() : nullptr;
        if (!suppressTransformUi && overlay) {
            overlay->onTransformModeExited(!wasMoveOnlyTransform);
        }
        emit transformModeExited(true);
        requestRender();
        return;
    }

    if (multiTargetTransform) {
        const bool hadSelectionTransform = m_selectionController
            && m_selectionController->lassoSelection().hasSelection()
            && !m_selectionController->lassoSelection().mask().empty();
        LassoSelectionState selectionBefore;
        if (hadSelectionTransform) {
            selectionBefore = captureLassoSelection(&m_selectionController->lassoSelection(),
                effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
        }

        const bool gpuTransformAvailable = m_initialized && m_renderer
            && m_renderer->transformRenderer() && m_renderer->tileRenderer();
        if (!gpuTransformAvailable) {
            for (const TransformTargetInfo& target : m_transformTargetSet.visualTargets) {
                if (target.kind == TransformTargetInfo::Kind::Raster) {
                    cancelTransform(wasMoveOnlyTransform);
                    return;
                }
            }
        }

        std::vector<TransformSnapshot> snapshots;
        snapshots.reserve(m_transformTargetSet.visualTargets.size());
        std::unordered_set<TileKey, TileKeyHash> allAffected;
        std::vector<QUuid> changedLayers;
        changedLayers.reserve(m_transformTargetSet.visualTargets.size());

        for (const TransformTargetInfo& target : m_transformTargetSet.visualTargets) {
            auto* targetLayer = m_layerModel ? m_layerModel->layerById(target.layerId) : nullptr;
            if (!targetLayer || !isLayerCanvasEditable(targetLayer)) {
                continue;
            }

            TransformSnapshot snapshot;
            snapshot.layerId = targetLayer->id;

            if (target.kind == TransformTargetInfo::Kind::Raster) {
                TileGrid* grid = targetLayer->pixelGrid();
                if (!grid) {
                    continue;
                }
                snapshot.beforeTiles = snapshotGridTiles(*grid);

                std::unordered_set<TileKey, TileKeyHash> beforeKeys;
                beforeKeys.reserve(snapshot.beforeTiles.size());
                for (const auto& [key, _] : snapshot.beforeTiles) {
                    beforeKeys.insert(key);
                }

                auto* transformRenderer = m_renderer->transformRenderer();
                auto* tileRenderer = m_renderer->tileRenderer();
                makeCurrent();
                m_renderer->uploadDirtyTiles(*grid);
                transformRenderer->buildSourceAtlas(*grid, tileRenderer);
                if (hadSelectionTransform) {
                    transformRenderer->buildMaskAtlas(
                        m_selectionController->lassoSelection().mask(), tileRenderer);
                }
                for (auto& [key, tile] : grid->tiles()) {
                    if (tile.hasTexture()) {
                        tileRenderer->destroyTileTexture(tile);
                    }
                }
                grid->clear();
                auto resultKeys = transformRenderer->applyGPU(
                    stateCopy, *grid, tileRenderer, selectionCopyMoveTransform);
                std::vector<TileKey> readbackKeys(resultKeys.begin(), resultKeys.end());
                GLsync fence = transformRenderer->startAsyncReadback(*grid, readbackKeys);
                transformRenderer->finishReadback(fence, *grid, readbackKeys);
                transformRenderer->destroySourceAtlas();
                doneCurrent();

                grid->pruneEmpty();

                const int contentTileBytes = static_cast<int>(aether::tileByteSize(grid->format()));
                for (const TileKey& key : readbackKeys) {
                    const TileData* tile = grid->getTile(key);
                    if (tile) {
                        auto& buf = snapshot.afterTiles[key];
                        buf.resize(contentTileBytes);
                        std::memcpy(buf.data(), tile->pixels(), contentTileBytes);
                    } else {
                        snapshot.afterTiles[key].resize(contentTileBytes, 0);
                        snapshot.removedTiles.insert(key);
                    }
                }
                for (const TileKey& key : readbackKeys) {
                    if (beforeKeys.find(key) == beforeKeys.end()) {
                        snapshot.createdTiles.insert(key);
                    }
                }
                for (const TileKey& key : beforeKeys) {
                    if (resultKeys.find(key) == resultKeys.end()) {
                        snapshot.removedTiles.insert(key);
                        if (snapshot.afterTiles.find(key) == snapshot.afterTiles.end()) {
                            snapshot.afterTiles[key].resize(contentTileBytes, 0);
                        }
                    }
                }

                m_canvas.tilePositionIndex().rebuildForLayer(targetLayer->id, grid->tiles());

                std::unordered_set<TileKey, TileKeyHash> layerAffected;
                layerAffected.reserve(snapshot.beforeTiles.size() + snapshot.afterTiles.size());
                for (const auto& [key, _] : snapshot.beforeTiles)
                    layerAffected.insert(key);
                for (const auto& [key, _] : snapshot.afterTiles)
                    layerAffected.insert(key);
                insertLayerEffectExpandedCoverage(targetLayer, layerAffected, allAffected);
            } else if (target.kind == TransformTargetInfo::Kind::IsolatedPixel) {
                if (!targetLayer->isIsolatedPixelLayer()) {
                    continue;
                }
                std::unordered_set<TileKey, TileKeyHash> oldKeys;
                if (auto* oldGrid = m_layerCompositingBuilder
                        ? m_layerCompositingBuilder->compositingGridForLayer(targetLayer)
                        : nullptr) {
                    for (const auto& [key, _] : oldGrid->tiles())
                        oldKeys.insert(key);
                }

                snapshot.isSmartTransform = true;
                snapshot.beforeSmartTransform = targetLayer->smartTransform;
                const TransformState beforeState = currentNonRasterTransformState(targetLayer);
                targetLayer->smartTransform = composeLayerTransform(beforeState, stateCopy);
                snapshot.afterSmartTransform = targetLayer->smartTransform;

                rebuildSmartProjectionCacheForLayer(targetLayer->id);

                std::unordered_set<TileKey, TileKeyHash> layerAffected = oldKeys;
                if (auto* newGrid = m_layerCompositingBuilder
                        ? m_layerCompositingBuilder->compositingGridForLayer(targetLayer)
                        : nullptr) {
                    for (const auto& [key, _] : newGrid->tiles())
                        layerAffected.insert(key);
                }
                insertLayerEffectExpandedCoverage(targetLayer, layerAffected, allAffected);
            } else if (target.kind == TransformTargetInfo::Kind::Text) {
                if (!targetLayer->isText() || !targetLayer->textData) {
                    continue;
                }
                std::unordered_set<TileKey, TileKeyHash> oldKeys
                    = retainedTextTileKeys(targetLayer);
                if (oldKeys.empty()) {
                    oldKeys = m_canvas.tilePositionIndex().tileKeysForLayer(targetLayer->id);
                }

                snapshot.isSmartTransform = true;
                snapshot.beforeSmartTransform = targetLayer->textData->transform;
                const TransformState beforeState = currentNonRasterTransformState(targetLayer);
                targetLayer->textData->transform = composeLayerTransform(beforeState, stateCopy);
                targetLayer->runtimeRetainedPayload.reset();
                targetLayer->runtimeRetainedPayloadKey.clear();
                snapshot.afterSmartTransform = targetLayer->textData->transform;

                std::unordered_set<TileKey, TileKeyHash> newKeys
                    = retainedTextTileKeys(targetLayer);
                m_canvas.tilePositionIndex().removeLayer(targetLayer->id);
                for (const TileKey& key : newKeys) {
                    m_canvas.tilePositionIndex().addEntry(key, targetLayer->id);
                }
                oldKeys.insert(newKeys.begin(), newKeys.end());
                insertLayerEffectExpandedCoverage(targetLayer, oldKeys, allAffected);
            }

            snapshots.push_back(std::move(snapshot));
            changedLayers.push_back(targetLayer->id);
        }

        if (hadSelectionTransform && gpuTransformAvailable) {
            LassoSelectionManager::MaskMutationScope maskScope(
                m_selectionController->lassoSelection());
            maskScope.disableSoftAlphaInvalidation();
            TileGrid& maskGrid = maskScope.grid();
            if (!maskGrid.empty()) {
                auto* transformRenderer = m_renderer->transformRenderer();
                auto* tileRenderer = m_renderer->tileRenderer();
                makeCurrent();
                transformRenderer->buildSourceAtlas(maskGrid, tileRenderer, true);
                for (auto& [key, tile] : maskGrid.tiles()) {
                    if (tile.hasTexture()) {
                        tileRenderer->destroyTileTexture(tile);
                    }
                }
                maskGrid.clear();
                auto resultKeys = transformRenderer->applyGPU(stateCopy, maskGrid, tileRenderer);
                std::vector<TileKey> readbackKeys(resultKeys.begin(), resultKeys.end());
                GLsync fence = transformRenderer->startAsyncReadback(maskGrid, readbackKeys);
                transformRenderer->finishReadback(fence, maskGrid, readbackKeys);
                transformRenderer->destroySourceAtlas();
                doneCurrent();
            }
            binarizeSelectionMask(maskGrid);
            if (hasFiniteDocumentBounds()) {
                clampSelectionMaskToCanvas(maskGrid, m_canvas.width(), m_canvas.height());
            }
            m_selectionController->lassoSelection().setMaskHasSoftAlpha(false);
            m_selectionController->lassoSelection().rebuildEdgesFromMask(
                effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
            LassoSelectionState afterLasso = transformLassoRegions(selectionBefore, stateCopy);
            m_selectionController->lassoSelection().setRegionsOnly(afterLasso.regions);
        }

        m_transformController.cancelAndExit();
        destroyTransformUndoStack();
        const Rect cacheBounds
            = unionTransformRects(m_transformTargetSet.contentBounds, stateCopy.transformedAABB());
        m_transformTargetSet.clear();
        m_selectionCopyMoveTransform = false;
        clearTransformViewportPreview();
        clearTransformPreviewCacheTiles(cacheBounds);
        invalidateCachedLayerStacks();
        if (m_renderer && m_renderer->transformRenderer()) {
            makeCurrent();
            m_renderer->transformRenderer()->destroySourceAtlas();
            doneCurrent();
        }

        if (!allAffected.empty()) {
            for (const QUuid& layerId : changedLayers) {
                m_canvas.dirtyManager().onTilesDirtied(layerId, allAffected);
                markBoardCompositionTilesDirty(layerId, allAffected);
            }
            emit contentRegionChanged(worldRectFromTileKeys(
                std::vector<TileKey>(allAffected.begin(), allAffected.end())));
            emit contentTilesChanged(qPointsFromTileKeys(allAffected));
        } else {
            m_canvas.dirtyManager().onStructureChanged();
        }

        std::optional<SelectionRestoreContext> selRestore;
        if (!selectionBefore.isEmpty()) {
            SelectionRestoreContext ctx;
            ctx.layerSelection = m_layerModel ? m_layerModel->selectionManager() : nullptr;
            ctx.lassoSelection
                = m_selectionController ? &m_selectionController->lassoSelection() : nullptr;
            ctx.canvas = &m_canvas;
            ctx.before.layer = captureLayerSelection(ctx.layerSelection);
            ctx.before.lasso = selectionBefore;
            ctx.after.layer = ctx.before.layer;
            ctx.after.lasso = captureLassoSelection(ctx.lassoSelection,
                effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
            ctx.layerExists = [this](const ruwa::core::layers::LayerId& id) {
                return m_layerModel && m_layerModel->contains(id);
            };
            ctx.requestRender = [this]() { requestRender(); };
            ctx.onBeforeRestore = [this]() { m_ignoreSelectionChange = true; };
            ctx.onAfterRestore = [this]() {
                m_ignoreSelectionChange = false;
                m_lastSelectionState.layer = captureLayerSelection(
                    m_layerModel ? m_layerModel->selectionManager() : nullptr);
                m_lastSelectionState.lasso = captureLassoSelection(
                    m_selectionController ? &m_selectionController->lassoSelection() : nullptr,
                    effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
            };
            selRestore = std::move(ctx);
        } else {
            selRestore = buildCurrentSelectionRestore();
        }

        commitLayerCopyMoveAddUndo();
        if (!snapshots.empty()) {
            auto cmd = std::make_unique<MultiTransformCommand>(
                &m_canvas, m_layerModel, std::move(snapshots), std::move(selRestore));
            m_canvas.undoManager().push(std::move(cmd));
        }

        for (const QUuid& layerId : changedLayers) {
            if (m_layerModel) {
                m_layerModel->notifyLayerDataChanged(layerId);
            }
        }
        auto* overlay = m_overlayManager ? m_overlayManager->transformOverlay() : nullptr;
        if (!suppressTransformUi && overlay) {
            overlay->onTransformModeExited(!wasMoveOnlyTransform);
        }
        emit transformModeExited(true);
        requestRender();
        return;
    }

    if (layer->isText()) {
        if (!layer->textData || !m_layerCompositingBuilder) {
            cancelTransform(wasMoveOnlyTransform);
            return;
        }

        std::unordered_set<TileKey, TileKeyHash> oldKeys = retainedTextTileKeys(layer);
        if (oldKeys.empty()) {
            oldKeys = m_canvas.tilePositionIndex().tileKeysForLayer(layer->id);
        }
        const TransformState beforeState = layer->textData->transform;

        layer->textData->transform = stateCopy;
        layer->runtimeRetainedPayload.reset();
        layer->runtimeRetainedPayloadKey.clear();

        m_transformController.cancelAndExit();
        destroyTransformUndoStack();
        m_transformTargetSet.clear();
        m_selectionCopyMoveTransform = false;
        clearTransformViewportPreview();
        clearTransformPreviewCacheTiles(stateCopy.transformedAABB());
        invalidateCachedLayerStacks();
        if (m_renderer && m_renderer->transformRenderer()) {
            makeCurrent();
            m_renderer->transformRenderer()->destroySourceAtlas();
            doneCurrent();
        }

        std::unordered_set<TileKey, TileKeyHash> allAffected = oldKeys;
        const auto newKeys = retainedTextTileKeys(layer);
        m_canvas.tilePositionIndex().removeLayer(layer->id);
        for (const TileKey& key : newKeys) {
            m_canvas.tilePositionIndex().addEntry(key, layer->id);
            allAffected.insert(key);
        }
        allAffected = expandLayerCoverageByEffects(layer, allAffected);
        if (!allAffected.empty()) {
            m_canvas.dirtyManager().onTilesDirtied(layer->id, allAffected);
            markBoardCompositionTilesDirty(layer->id, allAffected);
            emit contentRegionChanged(worldRectFromTileKeys(
                std::vector<TileKey>(allAffected.begin(), allAffected.end())));
            emit contentTilesChanged(qPointsFromTileKeys(allAffected));
        } else {
            m_canvas.dirtyManager().onStructureChanged();
        }

        TransformSnapshot snapshot;
        snapshot.layerId = layer->id;
        snapshot.isSmartTransform = true;
        snapshot.beforeSmartTransform = beforeState;
        snapshot.afterSmartTransform = layer->textData->transform;
        commitLayerCopyMoveAddUndo();
        auto cmd = std::make_unique<TransformCommand>(
            &m_canvas, m_layerModel, std::move(snapshot), buildCurrentSelectionRestore());
        m_canvas.undoManager().push(std::move(cmd));

        if (m_layerModel) {
            m_layerModel->notifyLayerDataChanged(layer->id);
        }
        auto* overlay = m_overlayManager ? m_overlayManager->transformOverlay() : nullptr;
        if (!suppressTransformUi && overlay) {
            overlay->onTransformModeExited(!wasMoveOnlyTransform);
        }
        emit transformModeExited(true);
        requestRender();
        return;
    }

    TileGrid* transformTargetGrid
        = (wasEditingMask && layer->maskTileGrid()) ? layer->maskTileGrid() : layer->pixelGrid();
    if (!transformTargetGrid) {
        cancelTransform(wasMoveOnlyTransform);
        return;
    }

    if (!wasEditingMask && layer->isIsolatedPixelLayer()) {
        const TransformState beforeState = layer->smartTransform;

        std::unordered_set<TileKey, TileKeyHash> oldKeys;
        if (auto* oldGrid = m_layerCompositingBuilder->compositingGridForLayer(layer)) {
            for (const auto& [key, tile] : oldGrid->tiles()) {
                oldKeys.insert(key);
            }
        }

        layer->smartTransform = stateCopy;

        m_transformController.cancelAndExit();
        destroyTransformUndoStack();
        m_transformTargetSet.clear();
        m_selectionCopyMoveTransform = false;
        clearTransformViewportPreview();
        clearTransformPreviewCacheTiles(stateCopy.transformedAABB());
        if (m_renderer && m_renderer->transformRenderer()) {
            makeCurrent();
            m_renderer->transformRenderer()->destroySourceAtlas();
            doneCurrent();
        }

        rebuildSmartProjectionCacheForLayer(layer->id);

        std::unordered_set<TileKey, TileKeyHash> allAffected = oldKeys;
        if (auto* newGrid = m_layerCompositingBuilder->compositingGridForLayer(layer)) {
            for (const auto& [key, tile] : newGrid->tiles()) {
                allAffected.insert(key);
            }
        }
        allAffected = expandLayerCoverageByEffects(layer, allAffected);
        if (!allAffected.empty()) {
            m_canvas.dirtyManager().onTilesDirtied(layer->id, allAffected);
            markBoardCompositionTilesDirty(layer->id, allAffected);
            emit contentRegionChanged(worldRectFromTileKeys(
                std::vector<TileKey>(allAffected.begin(), allAffected.end())));
            emit contentTilesChanged(qPointsFromTileKeys(allAffected));
        } else {
            m_canvas.dirtyManager().onStructureChanged();
        }

        TransformSnapshot snapshot;
        snapshot.layerId = layer->id;
        snapshot.isSmartTransform = true;
        snapshot.beforeSmartTransform = beforeState;
        snapshot.afterSmartTransform = layer->smartTransform;
        commitLayerCopyMoveAddUndo();
        auto cmd = std::make_unique<TransformCommand>(
            &m_canvas, m_layerModel, std::move(snapshot), buildCurrentSelectionRestore());
        m_canvas.undoManager().push(std::move(cmd));

        if (m_layerModel) {
            m_layerModel->notifyLayerDataChanged(layer->id);
        }
        auto* overlay = m_overlayManager ? m_overlayManager->transformOverlay() : nullptr;
        if (!suppressTransformUi && overlay) {
            overlay->onTransformModeExited(!wasMoveOnlyTransform);
        }
        emit transformModeExited(true);
        requestRender();
        return;
    }

    // ---- GPU Apply Path ----
    bool useGPU = m_initialized && m_renderer && m_renderer->transformRenderer()
        && m_renderer->tileRenderer();

    if (useGPU) {
        makeCurrent();

        auto* transformRenderer = m_renderer->transformRenderer();
        auto* tileRenderer = m_renderer->tileRenderer();
        const auto& state = m_transformController.state();

        // 1. Save before-snapshot (from TransformController)
        m_pendingTransform.layerId = layer->id;
        m_pendingTransform.maskTarget = wasEditingMask;
        m_pendingTransform.beforeTiles = m_transformController.takeBeforeSnapshot();
        m_pendingTransform.beforeKeys = m_transformController.takeBeforeKeys();

        m_renderer->uploadDirtyTiles(*transformTargetGrid);
        transformRenderer->buildSourceAtlas(*transformTargetGrid, tileRenderer);
        if (m_selectionController && m_selectionController->lassoSelection().hasSelection()
            && !m_selectionController->lassoSelection().mask().empty()) {
            transformRenderer->buildMaskAtlas(
                m_selectionController->lassoSelection().mask(), tileRenderer);
        }

        // 2. Clear the grid â€” GPU will rebuild it
        //    Destroy old tile textures first
        for (auto& [key, tile] : transformTargetGrid->tiles()) {
            if (tile.hasTexture()) {
                tileRenderer->destroyTileTexture(tile);
            }
        }
        transformTargetGrid->clear();

        // 3. GPU apply â€” render all destination tiles
        auto resultKeys = transformRenderer->applyGPU(
            state, *transformTargetGrid, tileRenderer, selectionCopyMoveTransform);

        // 4. Start async PBO readback
        std::vector<TileKey> readbackKeys(resultKeys.begin(), resultKeys.end());
        GLsync fence = transformRenderer->startAsyncReadback(*transformTargetGrid, readbackKeys);

        // 5. Destroy atlas (no longer needed)
        transformRenderer->destroySourceAtlas();

        doneCurrent();

        // 6. Exit transform mode (without CPU apply â€” already done on GPU)
        m_transformController.cancelAndExit();
        destroyTransformUndoStack();
        m_transformTargetSet.clear();
        m_selectionCopyMoveTransform = false;
        clearTransformViewportPreview();
        clearTransformPreviewCacheTiles(stateCopy.transformedAABB());

        // 7. Determine created/removed tiles
        m_pendingTransform.resultKeys = std::move(resultKeys);
        m_pendingTransform.readbackKeysOrdered = std::move(readbackKeys);
        m_pendingTransform.fence = fence;
        m_pendingTransform.active = true;
        m_pendingTransform.applySelectionMask = m_selectionController
            && m_selectionController->lassoSelection().hasSelection()
            && !m_selectionController->lassoSelection().mask().empty();
        m_pendingTransform.selectionTransformState = stateCopy;
        if (m_pendingTransform.applySelectionMask) {
            m_pendingTransform.selectionBefore
                = captureLassoSelection(&m_selectionController->lassoSelection(),
                    effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
        }

        // Compute created/removed
        for (const auto& key : m_pendingTransform.readbackKeysOrdered) {
            if (m_pendingTransform.beforeKeys.find(key) == m_pendingTransform.beforeKeys.end()) {
                m_pendingTransform.createdTiles.insert(key);
            }
        }
        for (const auto& key : m_pendingTransform.beforeKeys) {
            if (m_pendingTransform.resultKeys.find(key) == m_pendingTransform.resultKeys.end()) {
                m_pendingTransform.removedTiles.insert(key);
            }
        }

        // 8. Update tile position index + dirty NOW.
        //    Mask tiles are not layer content, so the content position index is
        //    left untouched (the pixels did not move); only the mask grid changed.
        if (!wasEditingMask) {
            m_canvas.tilePositionIndex().rebuildForLayer(layer->id, transformTargetGrid->tiles());
        }

        std::unordered_set<TileKey, TileKeyHash> allAffected;
        for (const auto& key : m_pendingTransform.readbackKeysOrdered)
            allAffected.insert(key);
        for (const auto& key : m_pendingTransform.beforeKeys)
            allAffected.insert(key);
        allAffected = expandLayerCoverageByEffects(layer, allAffected);
        m_canvas.dirtyManager().onTilesDirtied(layer->id, allAffected);
        markBoardCompositionTilesDirty(layer->id, allAffected);
        emit contentRegionChanged(
            worldRectFromTileKeys(std::vector<TileKey>(allAffected.begin(), allAffected.end())));
        emit contentTilesChanged(qPointsFromTileKeys(allAffected));

        if (wasEditingMask && m_layerModel) {
            // The warped mask now gates compositing — refresh cached layer stacks
            // and the mask thumbnail so the result is shown (the dirty tiles above
            // recomposite the affected region against the mutated mask grid).
            invalidateCachedLayerStacks();
            m_layerModel->notifyLayerDataChanged(layer->id);
        }

        // 9. Schedule deferred finalization
        commitLayerCopyMoveAddUndo();
        m_transformFinalizeTimer.start();

    } else {
        cancelTransform(wasMoveOnlyTransform);
        return;
    }

    auto* overlay = m_overlayManager ? m_overlayManager->transformOverlay() : nullptr;
    m_transformTargetSet.clear();
    m_selectionCopyMoveTransform = false;
    if (!suppressTransformUi && overlay) {
        overlay->onTransformModeExited(!wasMoveOnlyTransform);
    }
    emit transformModeExited(true);
    requestRender();
}

void OpenGLCanvasWidget::cancelTransform(std::optional<bool> moveOnlyStateForOverlay)
{
    if (!m_transformController.isActive())
        return;

    const bool suppressTransformUi = m_autoApplyingTransform;
    const bool wasMoveOnlyTransform
        = moveOnlyStateForOverlay.has_value() ? *moveOnlyStateForOverlay : m_moveOnlyTransform;
    m_autoApplyingTransform = false;
    m_moveOnlyTransform = false;
    m_transformEditingMask = false;

    // Capture current transform bounds before exit to clear preview cache
    Rect transformedAABB = m_transformController.state().transformedAABB();

    m_transformController.cancelAndExit();
    destroyTransformUndoStack();
    m_transformTargetSet.clear();
    m_selectionCopyMoveTransform = false;
    clearTransformViewportPreview();

    // Destroy GPU atlas
    if (m_renderer && m_renderer->transformRenderer()) {
        makeCurrent();
        m_renderer->transformRenderer()->destroySourceAtlas();
        doneCurrent();
    }

    discardLayerCopyMoveDuplicates();
    clearTransformPreviewCacheTiles(transformedAABB);
    invalidateCachedLayerStacks();

    // Force full recomposite to restore original appearance
    m_canvas.dirtyManager().onStructureChanged();

    auto* overlay = m_overlayManager ? m_overlayManager->transformOverlay() : nullptr;
    if (!suppressTransformUi && overlay) {
        overlay->onTransformModeExited(!wasMoveOnlyTransform);
    }
    emit transformModeExited(false);
    requestRender();
}

// ==========================================================================
//   O P E N G L   W I D G E T   I N T E R F A C E
// ==========================================================================

void OpenGLCanvasWidget::initializeGL()
{
    if (!initializeOpenGLFunctions()) {
        return;
    }

    shutdownFillWorker();

    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));

    const QSurfaceFormat actualFmt = context()->format();
    if (QScreen* scr = this->screen()) { }
    if (QWindow* win = window() ? window()->windowHandle() : nullptr) {
        if (QScreen* ws = win->screen()) { }
    }

    m_renderer = std::make_unique<GLRenderer>(static_cast<QOpenGLFunctions_4_5_Core*>(this));

    const auto showShaderDirectoryError = [this](const QString& message) {
        QMessageBox::critical(this, tr("Shader Loading Error"), message);
    };

    auto shaderDirResult = resolveRuntimeShaderDirectory();
    if (!shaderDirResult) {
        showShaderDirectoryError(QString::fromStdString(shaderDirResult.error().message));
        return;
    }

    const QString finalShaderDir = shaderDirResult.value();
    m_fillShaderDir = finalShaderDir;

    QElapsedTimer shaderInitTimer;
    shaderInitTimer.start();

    auto result = m_renderer->initialize(finalShaderDir);
    if (!result) {
        qCritical().noquote() << "OpenGL renderer initialization failed:"
                              << QString::fromStdString(result.error().message);
        return;
    }

    m_layerScreenSourceCache
        = std::make_unique<LayerScreenSourceCache>(static_cast<QOpenGLFunctions_4_5_Core*>(this));

    const QSize surfaceSize = currentSurfacePixelSize(this);
    m_viewport.resize(
        static_cast<uint32_t>(surfaceSize.width()), static_cast<uint32_t>(surfaceSize.height()));

    // Initialize canvas overlay manager (owns all GL overlays)
    m_overlayManager = std::make_unique<CanvasOverlayManager>();
    auto overlayResult
        = m_overlayManager->initialize(static_cast<QOpenGLFunctions_4_5_Core*>(this));
    if (!overlayResult) { }

    // Initialize selection renderer (GPU mask)
    m_selectionRenderer
        = std::make_unique<GLSelectionRenderer>(static_cast<QOpenGLFunctions_4_5_Core*>(this));
    auto selResult = m_selectionRenderer->initialize();
    if (!selResult) { }

    initializeFillWorker();
    if (m_fillWorker) {
        const int canvasW = std::max(1, static_cast<int>(m_canvas.width()));
        const int canvasH = std::max(1, static_cast<int>(m_canvas.height()));
        QMetaObject::invokeMethod(
            m_fillWorker,
            [worker = m_fillWorker, canvasW, canvasH]() {
                if (worker) {
                    worker->warmUp(canvasW, canvasH);
                }
            },
            Qt::QueuedConnection);
    }

    prewarmOneTimeGpuPaths();

    m_initialized = true;
    emit initialized();
}

void OpenGLCanvasWidget::resizeGL(int w, int h)
{
    if (w > 0 && h > 0) {
        m_viewport.resize(w, h);
        emit surfaceResized(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    }
}

void OpenGLCanvasWidget::paintGL_updateCameraAndEmitSignals()
{
    // Any paint satisfies a pending camera-animation frame request. Restart the
    // cadence from this frame so an unrelated repaint cannot produce a second
    // animation frame inside the 120 FPS interval.
    m_cameraAnimationFrameTimer.stop();

    const float dt = (m_cameraWasAnimatingLastFrame && m_cameraFrameTimer.isValid())
        ? static_cast<float>(qBound<qint64>(qint64(1), m_cameraFrameTimer.elapsed(), qint64(100)))
            / 1000.0f
        : 0.016f;
    m_cameraFrameTimer.restart();

    // VSync-synchronous pan: sample OS cursor position directly here, so one
    // delta is applied per paint regardless of how mouse events were scheduled
    // by Qt's event loop. Eliminates beat-pattern judder on high-refresh
    // displays where mouse poll rate and VSync rate don't divide evenly.
    if (m_panSamplingActive) {
        const QPointF currentGlobal = QCursor::pos();
        if (currentGlobal != m_panSamplingLastGlobalPos) {
            auto& cam = m_viewport.camera();
            const QPointF prevInGl = mapFromGlobal(m_panSamplingLastGlobalPos);
            const QPointF currInGl = mapFromGlobal(currentGlobal);
            const aether::Vector2 viewportSize = m_viewport.size();
            const aether::Vector2 prevScreen(
                static_cast<float>(prevInGl.x()), static_cast<float>(prevInGl.y()));
            const aether::Vector2 currScreen(
                static_cast<float>(currInGl.x()), static_cast<float>(currInGl.y()));
            const aether::Vector2 worldPrev = cam.screenToWorld(prevScreen, viewportSize);
            const aether::Vector2 worldCurr = cam.screenToWorld(currScreen, viewportSize);
            cam.move(worldPrev - worldCurr);
            m_panSamplingLastGlobalPos = currentGlobal;
        }
    }

    m_viewport.camera().update(dt);
    if (m_transformController.isActive() && m_transformController.updateAnimation(dt)) {
        update();
    }
    m_cameraWasAnimatingLastFrame
        = m_viewport.camera().isAnimating() || m_transformController.hasPendingAnimation();

    updateFillProgressPopupPosition();
    const float zoom = m_viewport.camera().zoom();
    if (zoom != m_lastEmittedZoom) {
        m_lastEmittedZoom = zoom;
        emit cameraZoomChanged(static_cast<qreal>(zoom));
    }
    const float rotation = m_viewport.camera().rotation();
    if (rotation != m_lastEmittedRotation) {
        m_lastEmittedRotation = rotation;
        emit cameraRotationChanged(static_cast<qreal>(rotation));
    }
}

void OpenGLCanvasWidget::paintGL_markTransformDirty()
{
    const auto* transformLayer
        = m_layerModel ? m_layerModel->layerById(m_transformController.layerId()) : nullptr;
    const bool isTextTransform = transformLayer && transformLayer->isText();
    const bool hasTransformAtlas = m_renderer && m_renderer->transformRenderer()
        && m_renderer->transformRenderer()->hasAtlas();
    const bool viewportTransformPreviewActive
        = m_transformViewportPreview.active && m_transformViewportPreview.viewportPathEnabled;
    if (!m_transformController.isActive() || !m_renderer
        || (!isTextTransform && !hasTransformAtlas && !viewportTransformPreviewActive)) {
        m_prevTransformDirtyValid = false;
        return;
    }
    invalidateTransformViewportPreviewTransform();
    if (viewportTransformPreviewActive) {
        m_prevTransformDirtyValid = false;
        return;
    }
    if (m_prevTransformDirtyValid) {
        for (int32_t ty = m_prevTransformMinTY; ty <= m_prevTransformMaxTY; ++ty)
            for (int32_t tx = m_prevTransformMinTX; tx <= m_prevTransformMaxTX; ++tx)
                m_canvas.compositionCache().markDirty(TileKey { tx, ty });
    }
    const auto& state = m_transformController.state();
    Rect previewAABB = state.transformedAABB();
    if (m_transformController.usesSelectionMask()) {
        previewAABB.x = std::min(previewAABB.left(), state.contentBounds.left());
        previewAABB.y = std::min(previewAABB.top(), state.contentBounds.top());
        const float previewRight = std::max(previewAABB.right(), state.contentBounds.right());
        const float previewBottom = std::max(previewAABB.bottom(), state.contentBounds.bottom());
        previewAABB.width = previewRight - previewAABB.x;
        previewAABB.height = previewBottom - previewAABB.y;
    }
    int32_t pMinTX = static_cast<int32_t>(std::floor(previewAABB.left() / TILE_SIZE));
    int32_t pMinTY = static_cast<int32_t>(std::floor(previewAABB.top() / TILE_SIZE));
    int32_t pMaxTX = static_cast<int32_t>(std::floor(previewAABB.right() / TILE_SIZE));
    int32_t pMaxTY = static_cast<int32_t>(std::floor(previewAABB.bottom() / TILE_SIZE));
    for (int32_t ty = pMinTY; ty <= pMaxTY; ++ty)
        for (int32_t tx = pMinTX; tx <= pMaxTX; ++tx)
            m_canvas.compositionCache().markDirty(TileKey { tx, ty });
    m_prevTransformMinTX = pMinTX;
    m_prevTransformMinTY = pMinTY;
    m_prevTransformMaxTX = pMaxTX;
    m_prevTransformMaxTY = pMaxTY;
    m_prevTransformDirtyValid = true;
}

void OpenGLCanvasWidget::paintGL_runComposite(const std::vector<CompositeLayerInfo>& layerStack)
{
    if (layerStack.empty())
        return;

    const float canvasWidth = static_cast<float>(m_canvas.width());
    const float canvasHeight = static_cast<float>(m_canvas.height());
    const bool flipH = effectiveContentFlipH();
    const bool flipV = effectiveContentFlipV();
    auto& compositionCache = m_canvas.compositionCache();
    auto* compositor = m_renderer ? m_renderer->compositor() : nullptr;

    // A stroke on a layer carrying a preview-disabled effect: drop that effect
    // across the whole layer for the duration of the stroke. This is the point of
    // "preview off" — the (potentially expensive) effect is NOT computed while
    // drawing, so interaction stays cheap; it is restored on commit. The builder
    // renders the active layer raw in this case; here we invalidate the composite
    // cache ONCE on each suppress-state transition so tiles recomposite raw
    // (suppressed) or effected (restored). compositeDirtyKeys is viewport-culled,
    // so markAllDirty only recomposites visible tiles per frame and leaves
    // off-screen ones pending — this also covers tiles made raw then panned out
    // of view mid-stroke. Keying off the state transition — rather than the async
    // stroke begin/commit — means every exit path (commit, cancel, interruption)
    // restores the effect. Applies to ALL brushes (additive too), not just the
    // replace-mode tools, so disabling the preview always reduces stroke cost.
    {
        const bool activeStroke
            = m_brush && m_brush->hasActiveStroke() && !m_brush->strokeBuffer().empty();
        bool suppress = false;
        if (activeStroke) {
            const auto hasPreviewOffEffect = [](const ruwa::core::layers::LayerData* l) {
                if (!l) {
                    return false;
                }
                for (const auto& fx : l->effects) {
                    if (fx.enabled && !fx.realtimePreviewEnabled) {
                        return true;
                    }
                }
                return false;
            };
            // Any layer (active or not) whose chain the builder reduces for the
            // duration of the stroke — the whole stack recomposites the stroke's
            // dirty tiles, so a preview-OFF effect on ANY covering layer is heavy.
            suppress = hasPreviewOffEffect(activeLayer());
            if (!suppress && m_layerModel) {
                m_layerModel->forEach([&](ruwa::core::layers::LayerData* l) {
                    if (suppress) {
                        return;
                    }
                    if (hasPreviewOffEffect(l)) {
                        suppress = true;
                    }
                });
            }
        }
        if (suppress != m_strokeEffectSuppressed) {
            m_strokeEffectSuppressed = suppress;
            compositionCache.markAllDirty();
        }
    }

    const auto compositeContextIt = paintGLCompositeContexts().find(this);
    const PaintGLCompositeContext compositeContext
        = compositeContextIt != paintGLCompositeContexts().end() ? compositeContextIt->second
                                                                 : PaintGLCompositeContext {};

    if (compositeContext.pureCameraFrame && compositionCache.hasDirtyPositions()) {
        const size_t totalDirtyCount = compositionCache.dirtyPositions().size();
        std::vector<TileKey> cachedCameraDirtyKeys;
        cachedCameraDirtyKeys.reserve(totalDirtyCount);
        for (const TileKey& key : compositionCache.dirtyPositions()) {
            if (compositionCache.grid().hasTile(key)) {
                cachedCameraDirtyKeys.push_back(key);
            }
        }
        // NOTE: Do NOT clear dirty positions here. Undo/redo can complete
        // between frames, leaving contentMutationActive=false while tiles
        // were genuinely modified. Clearing would discard valid dirty flags
        // and cause stale GPU textures (tile garbage after undo during zoom).
    }

    std::vector<TileKey> keysToComposite = collectVisibleUncachedKeys(
        layerStack, m_viewport, compositionCache, canvasWidth, canvasHeight, flipH, flipV);
    std::unordered_set<TileKey, TileKeyHash> queuedKeys(
        keysToComposite.begin(), keysToComposite.end());

    if (compositionCache.hasDirtyPositions()) {
        const auto visibleDirtyKeys = collectVisibleDirtyKeys(
            m_viewport, compositionCache, canvasWidth, canvasHeight, flipH, flipV);
        for (const TileKey& key : visibleDirtyKeys) {
            if (queuedKeys.insert(key).second) {
                keysToComposite.push_back(key);
            }
        }
    }

    if (!keysToComposite.empty()) {
        Color canvasBackdrop = Color::transparent();
        m_layerCompositingBuilder->resolveCanvasBackgroundColor(canvasBackdrop);
        m_renderer->compositeDirtyKeys(
            layerStack, compositionCache, keysToComposite, canvasBackdrop);
    } else if (compositor) {
        compositor->resetFrameStats();
    }
}

void OpenGLCanvasWidget::renderBoardLayers(const std::vector<CompositeLayerInfo>& boardLayerStack)
{
    if (m_exportPreviewHideBoardLayers || !m_renderer || !m_renderer->compositor()
        || !m_renderer->tileRenderer()) {
        return;
    }
    if (boardLayerStack.empty()) {
        if (!m_boardCompositionKeys.empty() || !m_boardCompositionCache.grid().empty()) {
            clearBoardCompositionCache();
        }
        return;
    }

    updateBoardCompositionTransientDirty();

    if (m_boardCompositionCacheDirty) {
        std::unordered_set<TileKey, TileKeyHash> boardKeys;
        collectCompositeLayerKeys(boardLayerStack, boardKeys);
        if (boardKeys.empty()) {
            clearBoardCompositionCache();
            return;
        }

        // Structure/appearance of the board stack changed, so cached composite
        // tiles are potentially stale: drop tiles that no longer belong to the
        // board, and mark the rest dirty to be recomposited IN PLACE.
        //
        // Do NOT clear() the cache here. clear() frees every composite tile's
        // GPU texture, forcing a full reallocation + re-upload on the next
        // composite. When this rebuild fires every frame (e.g. transforming a
        // board layer triggers updateBoardCompositionTransientDirty each frame)
        // that churn cost ~190 us/tile (ensureTileTexture) and dominated the
        // frame — ~100 ms for a large board. Reusing the textures keeps the
        // recomposite at ~8 us/tile.
        {
            std::vector<TileKey> staleBoardKeys;
            for (const auto& [key, tile] : m_boardCompositionCache.grid().tiles()) {
                Q_UNUSED(tile);
                if (!boardKeys.count(key)) {
                    staleBoardKeys.push_back(key);
                }
            }
            for (const TileKey& key : staleBoardKeys) {
                m_boardCompositionCache.removeTile(key);
            }
            m_boardCompositionCache.markAllDirty();
        }
        m_boardCompositionKeys = std::move(boardKeys);
        m_boardCompositionLayerIds.clear();
        collectCompositeLayerIds(boardLayerStack, m_boardCompositionLayerIds);
        m_boardCompositionCacheDirty = false;
    }

    // Viewport-culled compositing: only (re)composite board tiles that are
    // actually visible. Off-screen uncached/dirty tiles stay pending and are
    // composited lazily when they scroll into view. This bounds per-frame board
    // composite cost to the visible region instead of the whole board.
    //
    // IMPORTANT: iterate the precomputed m_boardCompositionKeys set (maintained
    // on structural change) rather than rebuilding the key set from the layer
    // stack every frame. The latter allocates and hashes a fresh set over every
    // board tile each frame, which becomes a CPU bottleneck during continuous
    // repaint (panning/transform) with large boards. Board tiles are not clipped
    // to the document, but their visibility still uses the real canvas center so
    // mirrored culling matches mirrored rendering.
    {
        const bool boardFlipH = effectiveContentFlipH();
        const bool boardFlipV = effectiveContentFlipV();
        const aether::VisibleTileKeyBounds visibleBounds
            = aether::visibleTileKeyBounds(m_viewport, static_cast<float>(m_canvas.width()),
                static_cast<float>(m_canvas.height()), boardFlipH, boardFlipV);

        const auto& cacheGrid = m_boardCompositionCache.grid();
        const auto& dirtyPositions = m_boardCompositionCache.dirtyPositions();
        const bool hasDirty = !dirtyPositions.empty();

        std::vector<TileKey> keysToComposite;
        for (const TileKey& key : m_boardCompositionKeys) {
            if (!aether::isTileKeyVisible(key, visibleBounds)) {
                continue;
            }
            // Recomposite when not yet cached, or when marked dirty.
            if (!cacheGrid.hasTile(key) || (hasDirty && dirtyPositions.count(key))) {
                keysToComposite.push_back(key);
            }
        }

        if (!keysToComposite.empty()) {
            m_renderer->compositeDirtyKeys(
                boardLayerStack, m_boardCompositionCache, keysToComposite);
        }
    }

    if (m_boardCompositionCache.grid().empty()) {
        return;
    }

    m_renderer->drawTiles(m_boardCompositionCache.grid(), m_viewport, m_canvas.width(),
        m_canvas.height(), 0.0f, effectiveContentFlipH(), effectiveContentFlipV(), false,
        Color::transparent(), false);
}

void OpenGLCanvasWidget::paintGL_renderSceneAndBlit(GLuint& outSceneTarget, GLint defaultFbo,
    bool needSceneForOverlay, const std::vector<CompositeLayerInfo>& boardLayerStack)
{
    const QSize surfaceSize = currentSurfacePixelSize(this);
    const int surfaceWidth = surfaceSize.width();
    const int surfaceHeight = surfaceSize.height();
    const float cornerRadiusCanvasPx = canvasCornerRadiusCanvasPx();
    const bool finiteDocumentBounds = hasFiniteDocumentBounds();
    const uint32_t tileClipWidth = finiteDocumentBounds ? m_canvas.width() : 0u;
    const uint32_t tileClipHeight = finiteDocumentBounds ? m_canvas.height() : 0u;
    const float tileCornerRadiusCanvasPx = finiteDocumentBounds ? cornerRadiusCanvasPx : 0.0f;
    bool renderToSceneFbo = false;
    if (needSceneForOverlay && surfaceWidth > 0 && surfaceHeight > 0) {
        m_sceneFboManager.ensureSceneFbo(this, surfaceWidth, surfaceHeight);
        if (m_sceneFboManager.sceneFbo()) {
            glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFboManager.sceneFbo());
            outSceneTarget = m_sceneFboManager.sceneFbo();
            renderToSceneFbo = true;
        }
    }
    if (!renderToSceneFbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(defaultFbo));
        outSceneTarget = static_cast<GLuint>(defaultFbo);
    }
    glViewport(0, 0, surfaceWidth, surfaceHeight);
    m_renderer->beginFrame(
        static_cast<uint32_t>(surfaceWidth), static_cast<uint32_t>(surfaceHeight));
    Color canvasBackground;
    const bool hasCanvasBackground
        = m_layerCompositingBuilder->resolveCanvasBackgroundColor(canvasBackground);
    if (!finiteDocumentBounds) {
        m_renderer->drawViewportChecker(m_checkerColor1, m_checkerColor2, m_checkerSize);
        if (hasCanvasBackground) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            m_renderer->drawBackground(canvasBackground);
            glDisable(GL_BLEND);
        }
    } else {
        m_renderer->drawBackground(m_backgroundColor);
    }
    if (finiteDocumentBounds) {
        m_renderer->drawCanvas(m_canvas, m_viewport, m_checkerColor1, m_checkerColor2,
            m_checkerSize, cornerRadiusCanvasPx, effectiveContentFlipH(), effectiveContentFlipV());
    }
    if (hasCanvasBackground && finiteDocumentBounds) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_renderer->drawCanvas(m_canvas, m_viewport, canvasBackground, canvasBackground, 1.0f,
            cornerRadiusCanvasPx, effectiveContentFlipH(), effectiveContentFlipV());
        glDisable(GL_BLEND);
    }
    // The rounded clip must affect the final canvas color once. For opaque
    // document backgrounds, edge tiles replace the pre-drawn background with
    // content already composited against the viewport background; otherwise the
    // anti-aliased edge blends layer content over the white background first.
    const bool compositeRoundedEdgesOverViewportBackground = hasCanvasBackground
        && canvasBackground.a >= 0.999f && finiteDocumentBounds && cornerRadiusCanvasPx > 0.0f;
    m_renderer->drawTiles(m_canvas.compositionGrid(), m_viewport, tileClipWidth, tileClipHeight,
        tileCornerRadiusCanvasPx, effectiveContentFlipH(), effectiveContentFlipV(),
        compositeRoundedEdgesOverViewportBackground, m_backgroundColor);
    renderBoardLayers(boardLayerStack);
    if (renderToSceneFbo) {
        m_sceneFboManager.blitToDefaultFbo(this, defaultFbo, surfaceWidth, surfaceHeight);
    }
}

void OpenGLCanvasWidget::paintGL_captureBackdrop(GLuint sceneTexture)
{
    if (!sceneTexture)
        return;
    if (!m_backdropCapture) {
        m_backdropCapture = std::make_unique<CanvasBackdropCapture>(
            static_cast<QOpenGLFunctions_4_5_Core*>(this));
        auto initResult = m_backdropCapture->initialize(m_fillShaderDir);
        if (!initResult) {
            m_backdropCapture.reset();
            return;
        }
    }
    const QSize surf = currentSurfacePixelSize(this);
    m_backdropCapture->capture(sceneTexture, surf.width(), surf.height());
}

bool OpenGLCanvasWidget::backdropAvailable() const
{
    return m_backdropCapture && !m_backdropCapture->snapshot().isNull();
}

void OpenGLCanvasWidget::addBackdropConsumer()
{
    ++m_backdropConsumers;
    // Idle canvases don't repaint on their own — kick one frame so the first
    // snapshot is produced soon after a consumer attaches.
    requestRender();
}

void OpenGLCanvasWidget::removeBackdropConsumer()
{
    if (m_backdropConsumers > 0) {
        --m_backdropConsumers;
    }
}

QImage OpenGLCanvasWidget::sampleBackdrop(const QRect& globalRect, const QSize& targetSize) const
{
    if (!m_backdropCapture || targetSize.isEmpty()) {
        return QImage();
    }
    const QImage& snap = m_backdropCapture->snapshot();
    if (snap.isNull()) {
        return QImage();
    }
    // global (logical) -> this widget local (logical) -> device px -> snapshot px.
    const qreal dpr = devicePixelRatioF();
    const qreal toSnap = dpr / static_cast<qreal>(m_backdropCapture->scale());
    const QPoint localTopLeft = mapFromGlobal(globalRect.topLeft());
    const QRectF snapRectF(localTopLeft.x() * toSnap, localTopLeft.y() * toSnap,
        globalRect.width() * toSnap, globalRect.height() * toSnap);
    const QRect src = snapRectF.toAlignedRect().intersected(snap.rect());
    if (src.isEmpty()) {
        return QImage();
    }
    return snap.copy(src).scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void OpenGLCanvasWidget::paintGL_renderOverlays(GLuint sceneTarget)
{
    const QSize surfaceSize = currentSurfacePixelSize(this);
    const int surfaceWidth = surfaceSize.width();
    const int surfaceHeight = surfaceSize.height();
    auto* transformOverlay = m_overlayManager ? m_overlayManager->transformOverlay() : nullptr;
    auto* canvasResizeOverlay
        = m_overlayManager ? m_overlayManager->canvasResizeOverlay() : nullptr;
    auto* brushCursorOverlay = m_overlayManager ? m_overlayManager->brushCursorOverlay() : nullptr;
    auto* eyedropperCursorOverlay
        = m_overlayManager ? m_overlayManager->eyedropperCursorOverlay() : nullptr;
    auto* textEditOverlay = m_overlayManager ? m_overlayManager->textEditOverlay() : nullptr;
    const bool wantBrushCursor = !m_skipCursorOverlays && brushCursorOverlay
        && m_cursorOverlayState.brushVisible && m_cursorOverlayState.brushRadius > 0.5f;
    const bool wantEyedropperCursor = !m_skipCursorOverlays && eyedropperCursorOverlay
        && m_cursorOverlayState.eyedropperVisible;
    if (wantBrushCursor) {
        ensureCursorOverlayInitialized(brushCursorOverlay, "brush cursor overlay");
    }
    if (wantEyedropperCursor) {
        ensureCursorOverlayInitialized(eyedropperCursorOverlay, "eyedropper cursor overlay");
    }
    const bool moveAxisGuideActive = m_transformController.moveAxisGuideActive();
    const auto& autoSnapGuideState = m_transformController.autoSnapGuideState();
    const bool autoSnapGuideActive = autoSnapGuideState.active();
    const bool drawTransformChrome = !m_moveOnlyTransform;
    const bool drawTransformOverlay
        = (transformOverlay && transformOverlay->isInitialized() && !m_autoApplyingTransform
            && (m_transformController.isActive() || transformOverlay->isAnimating())
            && (drawTransformChrome || moveAxisGuideActive || autoSnapGuideActive));
    const bool drawCanvasResizeOverlay
        = (canvasResizeOverlay && canvasResizeOverlay->isInitialized()
            && (m_canvasResizeOverlayActive || canvasResizeOverlay->isAnimating()));
    const bool drawTextEditOverlay
        = textEditOverlay && textEditOverlay->isInitialized() && textEditOverlay->isActive();
    const bool drawBrushCursor
        = wantBrushCursor && brushCursorOverlay && brushCursorOverlay->isInitialized();
    const bool drawEyedropperCursor = wantEyedropperCursor && eyedropperCursorOverlay
        && eyedropperCursorOverlay->isInitialized();
    GLuint sceneTex
        = (sceneTarget == m_sceneFboManager.sceneFbo() && m_sceneFboManager.sceneTexture())
        ? m_sceneFboManager.sceneTexture()
        : 0;
    const auto contentVp = canvasContentViewProjectionMatrix();
    TransformMoveAxisGuide moveAxisGuide;
    const TransformMoveAxisGuide* moveAxisGuidePtr = nullptr;
    if (moveAxisGuideActive) {
        moveAxisGuide.originWorld = m_transformController.moveAxisGuideOriginWorld();
        moveAxisGuide.axisDirWorld = m_transformController.moveAxisGuideAxisDirWorld();
        moveAxisGuide.opacity = m_transformController.moveAxisGuideOpacity();
        moveAxisGuidePtr = &moveAxisGuide;
    }
    TransformAutoSnapGuides autoSnapGuides;
    const TransformAutoSnapGuides* autoSnapGuidesPtr = nullptr;
    if (autoSnapGuideActive) {
        autoSnapGuides.hasVertical = autoSnapGuideState.hasVertical;
        autoSnapGuides.hasSecondVertical = autoSnapGuideState.hasSecondVertical;
        autoSnapGuides.hasHorizontal = autoSnapGuideState.hasHorizontal;
        autoSnapGuides.hasSecondHorizontal = autoSnapGuideState.hasSecondHorizontal;
        autoSnapGuides.verticalX = autoSnapGuideState.verticalX;
        autoSnapGuides.secondVerticalX = autoSnapGuideState.secondVerticalX;
        autoSnapGuides.horizontalY = autoSnapGuideState.horizontalY;
        autoSnapGuides.secondHorizontalY = autoSnapGuideState.secondHorizontalY;
        autoSnapGuides.opacity = 1.0f;
        autoSnapGuidesPtr = &autoSnapGuides;
    }
    std::function<Vector2(const Vector2&)> docWorldFromScreenFn
        = [this](const Vector2& s) { return documentWorldFromScreen(s); };
    const std::function<Vector2(const Vector2&)>* docWorldFn
        = (moveAxisGuidePtr || autoSnapGuidesPtr) ? &docWorldFromScreenFn : nullptr;
    if (drawTransformOverlay && transformOverlay) {
        if (m_transformController.isActive())
            transformOverlay->render(m_transformController.state(), m_viewport, sceneTex,
                &contentVp, moveAxisGuidePtr, drawTransformChrome, docWorldFn,
                m_transformController.cornersActAsRotationHandles(), autoSnapGuidesPtr);
        else if (transformOverlay->isAnimating())
            transformOverlay->render(m_viewport, sceneTex, &contentVp);
        if (transformOverlay->isAnimating())
            update();
    }
    if (drawCanvasResizeOverlay && canvasResizeOverlay) {
        canvasResizeOverlay->setSelectionRect(m_canvasResizeSelectionWorld);
        canvasResizeOverlay->setSelecting(m_canvasResizeOverlaySelecting);
        std::function<Vector2(Vector2)> docToScreenFn
            = [this](Vector2 w) { return screenFromDocumentWorld(w); };
        canvasResizeOverlay->render(
            m_viewport, sceneTex, surfaceWidth, surfaceHeight, &docToScreenFn);
        if (canvasResizeOverlay->isAnimating())
            update();
    }
    if (drawTextEditOverlay && textEditOverlay) {
        textEditOverlay->render(m_viewport, sceneTex, surfaceWidth, surfaceHeight, &contentVp);
        update();
    }
    if (drawBrushCursor && brushCursorOverlay && m_sceneFboManager.sceneTexture()) {
        const float cursorRotation = m_brush ? m_brush->previewDabRotationDeltaRadians() : 0.0f;
        brushCursorOverlay->render(m_cursorOverlayState.brushCenterX,
            m_cursorOverlayState.brushCenterY, m_cursorOverlayState.brushRadius, surfaceWidth,
            surfaceHeight, m_sceneFboManager.sceneTexture(), cursorRotation);
    }
    if (drawEyedropperCursor && eyedropperCursorOverlay && m_sceneFboManager.sceneTexture()) {
        const QColor selectedColor = QColor::fromRgbF(m_cursorOverlayState.eyedropperSelectedR,
            m_cursorOverlayState.eyedropperSelectedG, m_cursorOverlayState.eyedropperSelectedB,
            m_cursorOverlayState.eyedropperSelectedA);
        eyedropperCursorOverlay->render(m_cursorOverlayState.eyedropperCenterX,
            m_cursorOverlayState.eyedropperCenterY, surfaceWidth, surfaceHeight,
            m_sceneFboManager.sceneTexture(), selectedColor);
    }
}

void OpenGLCanvasWidget::paintGL_processSelectionReadback()
{
    if (m_selectionController && m_selectionController->processSelectionReadbackFrame()) {
        update();
    }
    if (m_selectionController && !m_selectionController->pendingSelectionJob().active
        && !m_selectionController->pendingSelectionReadback().active
        && m_selectionTick.isActive()) {
        m_selectionTick.stop();
    }
}

GLuint OpenGLCanvasWidget::acquireLayerMaskTextureForPreview(
    const CompositeLayerInfo& layer, bool flipH, bool flipV, uint64_t viewportRevision)
{
    if (!layer.clipMaskLuminanceReveal || !layer.externalClipMaskGrid || !m_renderer
        || !m_layerScreenSourceCache) {
        return 0;
    }

    // Render the layer mask grid (premultiplied grayscale) to a cached screen-space
    // texture at the STANDARD viewport — i.e. at the mask's fixed canvas position,
    // independent of the preview transform. This matches commit semantics: the
    // transform path warps content (and the selection mask) but never the layer
    // maskGrid, so the mask must gate the previewed result in place.
    CompositeLayerInfo maskInfo;
    maskInfo.id = layer.id;
    maskInfo.effectChainRevision = layer.effectChainRevision;
    maskInfo.tileGrid = layer.externalClipMaskGrid;
    maskInfo.opacity = 1.0f;
    maskInfo.blendMode = 0;
    maskInfo.visible = true;
    return m_layerScreenSourceCache->acquireLayerTexture(maskInfo, *m_renderer, m_viewport,
        m_canvas.width(), m_canvas.height(), flipH, flipV, viewportRevision,
        LayerScreenSourceCache::SourceKind::LayerMask,
        ruwa::core::effects::LayerSourcePurpose::MaskColor);
}

void OpenGLCanvasWidget::paintGL_renderTransformViewportPreview(
    const std::vector<CompositeLayerInfo>& layerStack,
    const std::vector<CompositeLayerInfo>& boardLayerStack, GLint defaultFbo)
{
    if (!m_transformViewportPreview.active || !m_transformViewportPreview.viewportPathEnabled
        || !m_transformController.isActive() || !m_renderer || !m_layerScreenSourceCache
        || width() <= 0 || height() <= 0) {
        return;
    }

    auto* viewportCompositor = m_renderer->viewportCompositor();
    auto* transformPreviewPass = m_renderer->transformViewportPreviewPass();
    auto* transformRenderer = m_renderer->transformRenderer();
    if (!viewportCompositor || !transformPreviewPass || !transformPreviewPass->isInitialized()
        || !transformRenderer) {
        return;
    }

    auto& session = m_transformViewportPreview;

    const Vector2 cameraPosition = m_viewport.camera().position();
    const float cameraZoom = m_viewport.camera().zoom();
    const float cameraRotation = m_viewport.camera().rotation();
    const bool cameraAnimating = m_viewport.camera().isAnimating();
    const float cornerRadiusCanvasPx = canvasCornerRadiusCanvasPx();
    const bool flipH = effectiveContentFlipH();
    const bool flipV = effectiveContentFlipV();
    const uint32_t viewportWidth = static_cast<uint32_t>(width());
    const uint32_t viewportHeight = static_cast<uint32_t>(height());
    const bool viewportChanged = cameraAnimating || session.viewportWidth != viewportWidth
        || session.viewportHeight != viewportHeight || session.flipH != flipH
        || session.flipV != flipV || !nearlyEqualPoint(session.cameraPosition, cameraPosition)
        || !nearlyEqualFloat(session.cameraZoom, cameraZoom)
        || !nearlyEqualFloat(session.cameraRotation, cameraRotation);
    if (viewportChanged) {
        session.viewportWidth = viewportWidth;
        session.viewportHeight = viewportHeight;
        session.cameraPosition = cameraPosition;
        session.cameraZoom = cameraZoom;
        session.cameraRotation = cameraRotation;
        session.flipH = flipH;
        session.flipV = flipV;
        session.viewportRevision += 1;
        session.viewportDirty = true;
        if (!m_transformTargetSet.empty() && !m_transformTargetSet.singleVisualTarget()) {
            session.sourceDirty = true;
        }
        m_layerScreenSourceCache->invalidateByViewport();
    }
    if (session.viewportRevision == 0) {
        session.viewportRevision = 1;
    }

    const bool finiteDocumentBounds = hasFiniteDocumentBounds();
    const uint32_t canvasWidth = finiteDocumentBounds ? m_canvas.width() : 0u;
    const uint32_t canvasHeight = finiteDocumentBounds ? m_canvas.height() : 0u;
    const bool multiTargetPreview
        = !m_transformTargetSet.empty() && !m_transformTargetSet.singleVisualTarget();

    if (multiTargetPreview && session.selectionMaskDirty && m_renderer->tileRenderer()) {
        if (m_selectionController && m_selectionController->lassoSelection().hasSelection()
            && !m_selectionController->lassoSelection().mask().empty()) {
            transformRenderer->buildMaskAtlas(
                m_selectionController->lassoSelection().mask(), m_renderer->tileRenderer());
        } else {
            TileGrid emptyMaskGrid;
            transformRenderer->buildMaskAtlas(emptyMaskGrid, m_renderer->tileRenderer());
        }
        session.selectionMaskDirty = false;
    }

    struct TransformPreviewSourceOverscan {
        uint32_t padX = 0;
        uint32_t padY = 0;
        uint32_t viewportWidth = 0;
        uint32_t viewportHeight = 0;
    };

    auto computeSourceOverscan = [&]() -> TransformPreviewSourceOverscan {
        const auto& transformState = m_transformController.state();
        float minSourceScreenX = static_cast<float>(viewportWidth);
        float minSourceScreenY = static_cast<float>(viewportHeight);
        float maxSourceScreenX = 0.0f;
        float maxSourceScreenY = 0.0f;
        for (int sy = 0; sy <= 4; ++sy) {
            for (int sx = 0; sx <= 4; ++sx) {
                const Vector2 destScreen { static_cast<float>(viewportWidth) * 0.25f
                        * static_cast<float>(sx),
                    static_cast<float>(viewportHeight) * 0.25f * static_cast<float>(sy) };
                const Vector2 destWorld = m_viewport.screenToWorld(destScreen);
                const Vector2 sourceWorld = transformState.inverseTransformPoint(destWorld);
                const Vector2 sourceScreen = m_viewport.worldToScreen(sourceWorld);
                minSourceScreenX = std::min(minSourceScreenX, sourceScreen.x);
                minSourceScreenY = std::min(minSourceScreenY, sourceScreen.y);
                maxSourceScreenX = std::max(maxSourceScreenX, sourceScreen.x);
                maxSourceScreenY = std::max(maxSourceScreenY, sourceScreen.y);
            }
        }

        constexpr uint32_t kMaxTransformPreviewSourceSize = 4096;
        const float sourcePadLeft = std::max(0.0f, -minSourceScreenX);
        const float sourcePadTop = std::max(0.0f, -minSourceScreenY);
        const float sourcePadRight
            = std::max(0.0f, maxSourceScreenX - static_cast<float>(viewportWidth));
        const float sourcePadBottom
            = std::max(0.0f, maxSourceScreenY - static_cast<float>(viewportHeight));
        const uint32_t maxSourcePadX = kMaxTransformPreviewSourceSize > viewportWidth
            ? (kMaxTransformPreviewSourceSize - viewportWidth) / 2u
            : 0u;
        const uint32_t maxSourcePadY = kMaxTransformPreviewSourceSize > viewportHeight
            ? (kMaxTransformPreviewSourceSize - viewportHeight) / 2u
            : 0u;
        const float sourcePadXNeeded = std::max(sourcePadLeft, sourcePadRight);
        const float sourcePadYNeeded = std::max(sourcePadTop, sourcePadBottom);

        TransformPreviewSourceOverscan overscan;
        overscan.padX = sourcePadXNeeded > 0.0f
            ? std::min(static_cast<uint32_t>(std::ceil(sourcePadXNeeded + 8.0f)), maxSourcePadX)
            : 0u;
        overscan.padY = sourcePadYNeeded > 0.0f
            ? std::min(static_cast<uint32_t>(std::ceil(sourcePadYNeeded + 8.0f)), maxSourcePadY)
            : 0u;
        overscan.viewportWidth = viewportWidth + overscan.padX * 2u;
        overscan.viewportHeight = viewportHeight + overscan.padY * 2u;
        return overscan;
    };

    // Same source-overscan computation, but for a transform OUTPUT that is itself
    // enlarged by (effPadX, effPadY) screen px on each side (the distortion-reach
    // overscan). Returns the source overscan RELATIVE TO that enlarged output:
    // padX/padY = extra source px beyond each edge of the enlarged output (so
    // sourceScreenOffset = padX, matching the pass's (sourceSize-outputSize)/2
    // convention), viewportWidth/Height = the full source texture dimensions
    // (enlargedOutput + 2*pad). Used when a transform TARGET carries a distortion,
    // so its warped content is materialised with reach past the visible viewport.
    auto computeSourceOverscanForOutput
        = [&](int effPadX, int effPadY) -> TransformPreviewSourceOverscan {
        const auto& transformState = m_transformController.state();
        const float outLeft = -static_cast<float>(effPadX);
        const float outTop = -static_cast<float>(effPadY);
        const float outW = static_cast<float>(viewportWidth) + 2.0f * static_cast<float>(effPadX);
        const float outH = static_cast<float>(viewportHeight) + 2.0f * static_cast<float>(effPadY);
        float minSourceScreenX = outLeft + outW;
        float minSourceScreenY = outTop + outH;
        float maxSourceScreenX = outLeft;
        float maxSourceScreenY = outTop;
        for (int sy = 0; sy <= 4; ++sy) {
            for (int sx = 0; sx <= 4; ++sx) {
                const Vector2 destScreen { outLeft + outW * 0.25f * static_cast<float>(sx),
                    outTop + outH * 0.25f * static_cast<float>(sy) };
                const Vector2 destWorld = m_viewport.screenToWorld(destScreen);
                const Vector2 sourceWorld = transformState.inverseTransformPoint(destWorld);
                const Vector2 sourceScreen = m_viewport.worldToScreen(sourceWorld);
                minSourceScreenX = std::min(minSourceScreenX, sourceScreen.x);
                minSourceScreenY = std::min(minSourceScreenY, sourceScreen.y);
                maxSourceScreenX = std::max(maxSourceScreenX, sourceScreen.x);
                maxSourceScreenY = std::max(maxSourceScreenY, sourceScreen.y);
            }
        }

        constexpr uint32_t kMaxOverscanSourceSize = 8192;
        const float padLeft = std::max(0.0f, outLeft - minSourceScreenX);
        const float padTop = std::max(0.0f, outTop - minSourceScreenY);
        const float padRight = std::max(0.0f, maxSourceScreenX - (outLeft + outW));
        const float padBottom = std::max(0.0f, maxSourceScreenY - (outTop + outH));
        const float padXNeeded = std::max(padLeft, padRight);
        const float padYNeeded = std::max(padTop, padBottom);
        const uint32_t outWU = static_cast<uint32_t>(outW);
        const uint32_t outHU = static_cast<uint32_t>(outH);
        const uint32_t maxPadX
            = kMaxOverscanSourceSize > outWU ? (kMaxOverscanSourceSize - outWU) / 2u : 0u;
        const uint32_t maxPadY
            = kMaxOverscanSourceSize > outHU ? (kMaxOverscanSourceSize - outHU) / 2u : 0u;

        TransformPreviewSourceOverscan overscan;
        overscan.padX = padXNeeded > 0.0f
            ? std::min(static_cast<uint32_t>(std::ceil(padXNeeded + 8.0f)), maxPadX)
            : 0u;
        overscan.padY = padYNeeded > 0.0f
            ? std::min(static_cast<uint32_t>(std::ceil(padYNeeded + 8.0f)), maxPadY)
            : 0u;
        overscan.viewportWidth = outWU + overscan.padX * 2u;
        overscan.viewportHeight = outHU + overscan.padY * 2u;
        return overscan;
    };
    if (multiTargetPreview) {
        if (m_transformTargetSet.previewBlocks.empty()) {
            return;
        }
        const TransformPreviewSourceOverscan sourceOverscan = computeSourceOverscan();

        Color canvasBackground;
        const bool hasCanvasBackground
            = m_layerCompositingBuilder->resolveCanvasBackgroundColor(canvasBackground);

        viewportCompositor->beginFrame(viewportWidth, viewportHeight);
        const GLuint previewContentTexture = layerStack.empty()
            ? 0
            : viewportCompositor->compositeLayers(
                  layerStack,
                  [&](const CompositeLayerInfo& info) -> GLuint {
                      if (info.isGroup) {
                          return 0;
                      }

                      if (m_transformTargetSet.containsVisualTarget(info.id)) {
                          Viewport sourceViewport = m_viewport;
                          if (sourceOverscan.padX > 0u || sourceOverscan.padY > 0u) {
                              sourceViewport.resize(
                                  sourceOverscan.viewportWidth, sourceOverscan.viewportHeight);
                          }

                          // Keep every target as an independent compositor source. Flattening a
                          // block here loses the backdrop each layer's blend mode must evaluate
                          // against, then applies only the insertion layer's mode to the result.
                          CompositeLayerInfo sourceInfo = info;
                          sourceInfo.opacity = 1.0f;
                          sourceInfo.blendMode = 0;
                          sourceInfo.clippedToBelow = false;
                          sourceInfo.externalClipMaskGrid = nullptr;
                          sourceInfo.clipMaskLuminanceReveal = false;
                          sourceInfo.clipMaskGrid2 = nullptr;

                          const GLuint sourceTexture
                              = m_layerScreenSourceCache->acquireLayerTexture(sourceInfo,
                                  *m_renderer, sourceViewport, canvasWidth, canvasHeight, flipH,
                                  flipV, session.viewportRevision,
                                  LayerScreenSourceCache::SourceKind::LayerColor,
                                  ruwa::core::effects::LayerSourcePurpose::RawContent);
                          if (!sourceTexture) {
                              return 0;
                          }

                          const GLuint transformedTexture
                              = transformPreviewPass->renderFromScreenSource(sourceTexture,
                                  sourceTexture, transformRenderer->maskAtlasTexture(),
                                  transformRenderer->maskAtlasMinTX(),
                                  transformRenderer->maskAtlasMinTY(),
                                  static_cast<uint32_t>(transformRenderer->maskAtlasWidth()),
                                  static_cast<uint32_t>(transformRenderer->maskAtlasHeight()),
                                  m_transformController.state(), viewportWidth, viewportHeight,
                                  cameraPosition, cameraZoom, cameraRotation, canvasWidth,
                                  canvasHeight, cornerRadiusCanvasPx, flipH, flipV,
                                  m_selectionCopyMoveTransform, sourceOverscan.viewportWidth,
                                  sourceOverscan.viewportHeight,
                                  { static_cast<float>(sourceOverscan.padX),
                                      static_cast<float>(sourceOverscan.padY) },
                                  sourceOverscan.viewportWidth, sourceOverscan.viewportHeight,
                                  { static_cast<float>(sourceOverscan.padX),
                                      static_cast<float>(sourceOverscan.padY) });
                          if (!transformedTexture) {
                              return 0;
                          }

                          return transformedTexture;
                      }

                      return m_layerScreenSourceCache->acquireLayerTexture(info, *m_renderer,
                          m_viewport, canvasWidth, canvasHeight, flipH, flipV,
                          session.viewportRevision, LayerScreenSourceCache::SourceKind::LayerColor,
                          ruwa::core::effects::LayerSourcePurpose::RawContent);
                  },
                  hasCanvasBackground ? canvasBackground : Color::transparent(), cameraZoom,
                  buildViewportEffectRegion(m_viewport, static_cast<float>(canvasWidth),
                      static_cast<float>(canvasHeight), flipH, flipV, viewportWidth,
                      viewportHeight),
                  [&](const CompositeLayerInfo& info, int padX,
                      int padY) -> GLViewportCompositor::OverscanLayerSource {
                      if (info.isGroup) {
                          return {};
                      }
                      if (m_transformTargetSet.containsVisualTarget(info.id)) {
                          // A transformed target is sourced from the warped preview
                          // pass, not a plain layer render. Re-run its block transform
                          // at the compositor's effect-overscan OUTPUT size so the
                          // target's own distortion can sample the warped content beyond
                          // the viewport edge; the transform's own source overscan is
                          // recomputed for that enlarged output.
                          if (padX <= 0 || padY <= 0) {
                              return {};
                          }
                          const uint32_t outW = viewportWidth + static_cast<uint32_t>(padX) * 2u;
                          const uint32_t outH = viewportHeight + static_cast<uint32_t>(padY) * 2u;
                          const TransformPreviewSourceOverscan srcOver
                              = computeSourceOverscanForOutput(padX, padY);
                          Viewport sourceViewport = m_viewport;
                          sourceViewport.resize(srcOver.viewportWidth, srcOver.viewportHeight);

                          // Mirror the normal source resolver: keep each target an
                          // independent source (strip blend/clip so its own mode is
                          // evaluated by the compositor, not baked here).
                          CompositeLayerInfo sourceInfo = info;
                          sourceInfo.opacity = 1.0f;
                          sourceInfo.blendMode = 0;
                          sourceInfo.clippedToBelow = false;
                          sourceInfo.externalClipMaskGrid = nullptr;
                          sourceInfo.clipMaskLuminanceReveal = false;
                          sourceInfo.clipMaskGrid2 = nullptr;
                          const GLuint sourceTexture
                              = m_layerScreenSourceCache->acquireLayerTexture(sourceInfo,
                                  *m_renderer, sourceViewport, canvasWidth, canvasHeight, flipH,
                                  flipV, session.viewportRevision,
                                  LayerScreenSourceCache::SourceKind::LayerColor,
                                  ruwa::core::effects::LayerSourcePurpose::RawContent);
                          if (!sourceTexture) {
                              return {};
                          }
                          const GLuint warped = transformPreviewPass->renderFromScreenSource(
                              sourceTexture, sourceTexture, transformRenderer->maskAtlasTexture(),
                              transformRenderer->maskAtlasMinTX(),
                              transformRenderer->maskAtlasMinTY(),
                              static_cast<uint32_t>(transformRenderer->maskAtlasWidth()),
                              static_cast<uint32_t>(transformRenderer->maskAtlasHeight()),
                              m_transformController.state(), outW, outH, cameraPosition, cameraZoom,
                              cameraRotation, canvasWidth, canvasHeight, cornerRadiusCanvasPx,
                              flipH, flipV, m_selectionCopyMoveTransform, srcOver.viewportWidth,
                              srcOver.viewportHeight,
                              { static_cast<float>(srcOver.padX),
                                  static_cast<float>(srcOver.padY) },
                              srcOver.viewportWidth, srcOver.viewportHeight,
                              { static_cast<float>(srcOver.padX),
                                  static_cast<float>(srcOver.padY) });
                          if (!warped) {
                              return {};
                          }
                          Viewport outputViewport = m_viewport;
                          outputViewport.resize(outW, outH);
                          GLViewportCompositor::OverscanLayerSource out;
                          out.texture = warped;
                          out.region = buildViewportEffectRegion(outputViewport,
                              static_cast<float>(canvasWidth), static_cast<float>(canvasHeight),
                              flipH, flipV, outW, outH);
                          if (!out.region.valid) {
                              return {};
                          }
                          return out;
                      }
                      return resolveOverscanRasterSource(info, padX, padY,
                          *m_layerScreenSourceCache, *m_renderer, m_viewport, viewportWidth,
                          viewportHeight, canvasWidth, canvasHeight, flipH, flipV,
                          session.viewportRevision);
                  },
                  [&](const CompositeLayerInfo& info) {
                      return acquireLayerMaskTextureForPreview(
                          info, flipH, flipV, session.viewportRevision);
                  });
        if (!previewContentTexture) {
            return;
        }

        glBindFramebuffer(GL_FRAMEBUFFER,
            m_sceneFboManager.sceneFbo() ? m_sceneFboManager.sceneFbo()
                                         : static_cast<GLuint>(defaultFbo));
        glViewport(0, 0, width(), height());
        m_renderer->beginFrame(viewportWidth, viewportHeight);

        if (!finiteDocumentBounds) {
            m_renderer->drawViewportChecker(m_checkerColor1, m_checkerColor2, m_checkerSize);
            if (hasCanvasBackground) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                m_renderer->drawBackground(canvasBackground);
                glDisable(GL_BLEND);
            }
        } else {
            m_renderer->drawBackground(m_backgroundColor);
            m_renderer->drawCanvas(m_canvas, m_viewport, m_checkerColor1, m_checkerColor2,
                m_checkerSize, cornerRadiusCanvasPx, flipH, flipV);
            if (hasCanvasBackground) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                m_renderer->drawCanvas(m_canvas, m_viewport, canvasBackground, canvasBackground,
                    1.0f, cornerRadiusCanvasPx, flipH, flipV);
                glDisable(GL_BLEND);
            }
        }

        GLViewportCompositor::CanvasClipParams previewClipParams;
        if (finiteDocumentBounds) {
            previewClipParams.enabled = true;
            previewClipParams.cameraPosition = cameraPosition;
            previewClipParams.cameraZoom = cameraZoom;
            previewClipParams.cameraRotation = cameraRotation;
            previewClipParams.canvasWidth = static_cast<float>(canvasWidth);
            previewClipParams.canvasHeight = static_cast<float>(canvasHeight);
            previewClipParams.canvasCornerRadius = cornerRadiusCanvasPx;
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        viewportCompositor->drawTexture(previewContentTexture, previewClipParams);
        glDisable(GL_BLEND);
        renderBoardLayers(boardLayerStack);
        m_renderer->endFrame();

        if (m_sceneFboManager.sceneFbo()) {
            m_sceneFboManager.blitToDefaultFbo(this, defaultFbo, width(), height());
        }

        session.viewportDirty = false;
        session.transformDirty = false;
        session.sourceDirty = false;
        session.selectionMaskDirty = false;
        return;
    }

    auto* targetLayer = m_layerModel ? m_layerModel->layerById(session.targetLayerId) : nullptr;
    if (!targetLayer || !targetLayer->pixelGrid()) {
        return;
    }

    // When the mask is the transform target, the mask grid is the warped source
    // (the pixels stay fixed and are re-applied through the mask further below).
    const bool editingMask = m_transformEditingMask && targetLayer->maskTileGrid();

    TileGrid* sourceGrid = nullptr;
    if (session.sourceLayerId == session.targetLayerId) {
        sourceGrid = editingMask ? targetLayer->maskTileGrid() : targetLayer->pixelGrid();
    }
    if (!sourceGrid) {
        return;
    }

    const bool targetIsBoard = targetLayer->isBoard();
    bool sourceAtlasRebuilt = false;
    if ((session.sourceDirty || !transformRenderer->hasAtlas()) && m_renderer->tileRenderer()) {
        m_renderer->uploadDirtyTiles(*sourceGrid);
        transformRenderer->buildSourceAtlas(*sourceGrid, m_renderer->tileRenderer());
        sourceAtlasRebuilt = true;
    }
    if (!transformRenderer->hasAtlas()) {
        return;
    }
    if ((session.selectionMaskDirty || sourceAtlasRebuilt) && m_renderer->tileRenderer()) {
        if (m_selectionController && m_selectionController->lassoSelection().hasSelection()
            && !m_selectionController->lassoSelection().mask().empty()) {
            transformRenderer->buildMaskAtlas(
                m_selectionController->lassoSelection().mask(), m_renderer->tileRenderer());
        } else {
            TileGrid emptyMaskGrid;
            transformRenderer->buildMaskAtlas(emptyMaskGrid, m_renderer->tileRenderer());
        }
        session.selectionMaskDirty = false;
    }

    CompositeLayerInfo targetBaseInfo;
    // Board layers render outside document bounds, so their viewport preview
    // must not reuse the canvas-clipped LayerColor cache entry. Use a dedicated
    // BoardPreviewColor cache kind (rendered unclipped but mirrored around the
    // real canvas center). The untransformed board source stays cached across
    // drag frames instead of being re-rendered every frame; only the transform
    // matrix changes during the preview.
    // In mask mode the carve base is the mask rendered at its fixed position, so a
    // selection-masked mask transform composites the warped selection over the rest
    // of the (untransformed) mask — exactly matching commit. A board is never a
    // mask target, so editingMask and targetIsBoard never coincide.
    targetBaseInfo.id = session.targetLayerId;
    targetBaseInfo.effectChainRevision = targetLayer->effectChainRevision;
    targetBaseInfo.tileGrid = editingMask ? targetLayer->maskTileGrid() : targetLayer->pixelGrid();
    targetBaseInfo.opacity = 1.0f;
    targetBaseInfo.blendMode = 0;
    targetBaseInfo.visible = true;

    const GLuint targetBaseTexture = m_layerScreenSourceCache->acquireLayerTexture(targetBaseInfo,
        *m_renderer, m_viewport, targetIsBoard ? m_canvas.width() : canvasWidth,
        targetIsBoard ? m_canvas.height() : canvasHeight, flipH, flipV, session.viewportRevision,
        editingMask ? LayerScreenSourceCache::SourceKind::LayerMask
                    : (targetIsBoard ? LayerScreenSourceCache::SourceKind::BoardPreviewColor
                                     : LayerScreenSourceCache::SourceKind::LayerColor),
        editingMask ? ruwa::core::effects::LayerSourcePurpose::MaskColor
                    : (targetIsBoard ? ruwa::core::effects::LayerSourcePurpose::BoardRawContent
                                     : ruwa::core::effects::LayerSourcePurpose::RawContent));
    if (!targetBaseTexture) {
        return;
    }

    // When the mask itself is being transformed, the warped source grid is the
    // mask, whose absent/solid tiles carry a default-fill background (e.g. a
    // hide-all mask's opaque black). Pass it so the preview fills the area the
    // mask doesn't cover with that background instead of transparent (= revealed),
    // matching commit. Content transforms keep a transparent background (no-op).
    Color sourceBackgroundColor = Color::transparent();
    if (editingMask && sourceGrid) {
        uint8_t br = 0, bg = 0, bb = 0, ba = 0;
        sourceGrid->defaultFill(br, bg, bb, ba);
        sourceBackgroundColor = Color(br / 255.0f, bg / 255.0f, bb / 255.0f, ba / 255.0f);
    }

    const GLuint previewedTargetTexture = transformPreviewPass->render(
        transformRenderer->atlasTexture(), transformRenderer->atlasMinTX(),
        transformRenderer->atlasMinTY(), static_cast<uint32_t>(transformRenderer->atlasWidth()),
        static_cast<uint32_t>(transformRenderer->atlasHeight()), targetBaseTexture,
        transformRenderer->maskAtlasTexture(), transformRenderer->maskAtlasMinTX(),
        transformRenderer->maskAtlasMinTY(),
        static_cast<uint32_t>(transformRenderer->maskAtlasWidth()),
        static_cast<uint32_t>(transformRenderer->maskAtlasHeight()), m_transformController.state(),
        viewportWidth, viewportHeight, cameraPosition, cameraZoom, cameraRotation,
        targetIsBoard ? m_canvas.width() : canvasWidth,
        targetIsBoard ? m_canvas.height() : canvasHeight,
        targetIsBoard ? 0.0f : cornerRadiusCanvasPx, flipH, flipV, m_selectionCopyMoveTransform,
        sourceBackgroundColor, !targetIsBoard);
    if (!previewedTargetTexture) {
        return;
    }

    Color canvasBackground;
    const bool hasCanvasBackground
        = m_layerCompositingBuilder->resolveCanvasBackgroundColor(canvasBackground);

    viewportCompositor->beginFrame(viewportWidth, viewportHeight);

    // Content transforms feed warped raw color to the compositor. Mask transforms
    // feed fixed raw color and resolve previewedTargetTexture as the final mask.
    // In both cases effects run on raw color and the resolved mask gates the final
    // blend, matching the document-tile compositor.
    GLuint targetColorTexture = previewedTargetTexture;
    if (editingMask) {
        CompositeLayerInfo fixedContentInfo;
        fixedContentInfo.id = session.targetLayerId;
        fixedContentInfo.effectChainRevision = targetLayer->effectChainRevision;
        fixedContentInfo.tileGrid = targetLayer->pixelGrid();
        fixedContentInfo.opacity = 1.0f;
        fixedContentInfo.blendMode = 0;
        fixedContentInfo.visible = true;
        const GLuint fixedContentTexture = m_layerScreenSourceCache->acquireLayerTexture(
            fixedContentInfo, *m_renderer, m_viewport, canvasWidth, canvasHeight, flipH, flipV,
            session.viewportRevision, LayerScreenSourceCache::SourceKind::LayerColor,
            ruwa::core::effects::LayerSourcePurpose::RawContent);
        if (fixedContentTexture) {
            targetColorTexture = fixedContentTexture;
        }
    }

    const GLuint previewContentTexture = layerStack.empty()
        ? 0
        : viewportCompositor->compositeLayers(
              layerStack,
              [&](const CompositeLayerInfo& info) -> GLuint {
                  if (!info.isGroup && info.id == session.targetLayerId) {
                      return targetColorTexture;
                  }
                  return m_layerScreenSourceCache->acquireLayerTexture(info, *m_renderer,
                      m_viewport, canvasWidth, canvasHeight, flipH, flipV, session.viewportRevision,
                      LayerScreenSourceCache::SourceKind::LayerColor,
                      ruwa::core::effects::LayerSourcePurpose::RawContent);
              },
              hasCanvasBackground ? canvasBackground : Color::transparent(), cameraZoom,
              buildViewportEffectRegion(m_viewport, static_cast<float>(canvasWidth),
                  static_cast<float>(canvasHeight), flipH, flipV, viewportWidth, viewportHeight),
              [&](const CompositeLayerInfo& info, int padX,
                  int padY) -> GLViewportCompositor::OverscanLayerSource {
                  if (info.isGroup) {
                      return {};
                  }
                  if (info.id == session.targetLayerId) {
                      // The transform target's source is the WARPED content from the
                      // preview pass, not a plain layer render. Re-run that pass at the
                      // overscan size the compositor asked for so the target's own
                      // distortion can sample the warped content beyond the viewport
                      // edge (the reported zoom-in bug). Only content transforms on a
                      // finite canvas: mask transforms feed fixed content + a warped
                      // mask, and boards have no canvas-relative document frame.
                      if (editingMask || targetIsBoard || padX <= 0 || padY <= 0) {
                          return {};
                      }
                      const uint32_t overscanWidth
                          = viewportWidth + static_cast<uint32_t>(padX) * 2u;
                      const uint32_t overscanHeight
                          = viewportHeight + static_cast<uint32_t>(padY) * 2u;
                      Viewport overscanViewport = m_viewport;
                      overscanViewport.resize(overscanWidth, overscanHeight);
                      // Overscan base so the pass's screen-space base sampling stays
                      // aligned with the enlarged surface.
                      const GLuint overscanBase = m_layerScreenSourceCache->acquireLayerTexture(
                          targetBaseInfo, *m_renderer, overscanViewport, canvasWidth, canvasHeight,
                          flipH, flipV, session.viewportRevision,
                          LayerScreenSourceCache::SourceKind::LayerColor,
                          ruwa::core::effects::LayerSourcePurpose::RawContent);
                      if (!overscanBase) {
                          return {};
                      }
                      // Renders into the pass's (now overscan-sized) output texture,
                      // clobbering previewedTargetTexture — safe because when this
                      // returns a valid source the compositor takes the reach path and
                      // never reads the target's plain fallback source.
                      const GLuint warpedOverscan = transformPreviewPass->render(
                          transformRenderer->atlasTexture(), transformRenderer->atlasMinTX(),
                          transformRenderer->atlasMinTY(),
                          static_cast<uint32_t>(transformRenderer->atlasWidth()),
                          static_cast<uint32_t>(transformRenderer->atlasHeight()), overscanBase,
                          transformRenderer->maskAtlasTexture(),
                          transformRenderer->maskAtlasMinTX(), transformRenderer->maskAtlasMinTY(),
                          static_cast<uint32_t>(transformRenderer->maskAtlasWidth()),
                          static_cast<uint32_t>(transformRenderer->maskAtlasHeight()),
                          m_transformController.state(), overscanWidth, overscanHeight,
                          cameraPosition, cameraZoom, cameraRotation, canvasWidth, canvasHeight,
                          cornerRadiusCanvasPx, flipH, flipV, m_selectionCopyMoveTransform,
                          sourceBackgroundColor);
                      if (!warpedOverscan) {
                          return {};
                      }
                      GLViewportCompositor::OverscanLayerSource out;
                      out.texture = warpedOverscan;
                      out.region = buildViewportEffectRegion(overscanViewport,
                          static_cast<float>(canvasWidth), static_cast<float>(canvasHeight), flipH,
                          flipV, overscanWidth, overscanHeight);
                      if (!out.region.valid) {
                          return {};
                      }
                      return out;
                  }
                  return resolveOverscanRasterSource(info, padX, padY, *m_layerScreenSourceCache,
                      *m_renderer, m_viewport, viewportWidth, viewportHeight, canvasWidth,
                      canvasHeight, flipH, flipV, session.viewportRevision);
              },
              [&](const CompositeLayerInfo& info) -> GLuint {
                  if (editingMask && !info.isGroup && info.id == session.targetLayerId) {
                      return previewedTargetTexture;
                  }
                  return acquireLayerMaskTextureForPreview(
                      info, flipH, flipV, session.viewportRevision);
              });
    if (!targetIsBoard && !previewContentTexture) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER,
        m_sceneFboManager.sceneFbo() ? m_sceneFboManager.sceneFbo()
                                     : static_cast<GLuint>(defaultFbo));
    glViewport(0, 0, width(), height());
    m_renderer->beginFrame(viewportWidth, viewportHeight);

    if (!finiteDocumentBounds) {
        m_renderer->drawViewportChecker(m_checkerColor1, m_checkerColor2, m_checkerSize);
        if (hasCanvasBackground) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            m_renderer->drawBackground(canvasBackground);
            glDisable(GL_BLEND);
        }
    } else {
        m_renderer->drawBackground(m_backgroundColor);
    }

    if (finiteDocumentBounds) {
        m_renderer->drawCanvas(m_canvas, m_viewport, m_checkerColor1, m_checkerColor2,
            m_checkerSize, cornerRadiusCanvasPx, flipH, flipV);
    }

    if (hasCanvasBackground && finiteDocumentBounds) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_renderer->drawCanvas(m_canvas, m_viewport, canvasBackground, canvasBackground, 1.0f,
            cornerRadiusCanvasPx, flipH, flipV);
        glDisable(GL_BLEND);
    }

    GLViewportCompositor::CanvasClipParams previewClipParams;
    if (finiteDocumentBounds) {
        previewClipParams.enabled = true;
        previewClipParams.cameraPosition = cameraPosition;
        previewClipParams.cameraZoom = cameraZoom;
        previewClipParams.cameraRotation = cameraRotation;
        previewClipParams.canvasWidth = static_cast<float>(canvasWidth);
        previewClipParams.canvasHeight = static_cast<float>(canvasHeight);
        previewClipParams.canvasCornerRadius = cornerRadiusCanvasPx;
    }

    auto drawViewportTexture = [&](GLuint texture, bool clipToCanvas) {
        if (!texture) {
            return;
        }
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        viewportCompositor->drawTexture(
            texture, clipToCanvas ? previewClipParams : GLViewportCompositor::CanvasClipParams {});
        glDisable(GL_BLEND);
    };

    drawViewportTexture(previewContentTexture, finiteDocumentBounds);

    bool renderedBoardPreview = false;
    if (targetIsBoard && !m_exportPreviewHideBoardLayers && !boardLayerStack.empty()) {
        const GLuint boardPreviewTexture = viewportCompositor->compositeLayers(
            boardLayerStack,
            [&](const CompositeLayerInfo& info) -> GLuint {
                if (!info.isGroup && info.id == session.targetLayerId) {
                    return targetColorTexture;
                }
                return m_layerScreenSourceCache->acquireLayerTexture(info, *m_renderer, m_viewport,
                    m_canvas.width(), m_canvas.height(), flipH, flipV, session.viewportRevision,
                    LayerScreenSourceCache::SourceKind::BoardPreviewColor,
                    ruwa::core::effects::LayerSourcePurpose::RawContent);
            },
            Color::transparent(), cameraZoom,
            buildViewportEffectRegion(m_viewport, static_cast<float>(m_canvas.width()),
                static_cast<float>(m_canvas.height()), flipH, flipV, viewportWidth, viewportHeight),
            [&](const CompositeLayerInfo& info, int padX,
                int padY) -> GLViewportCompositor::OverscanLayerSource {
                if (info.isGroup || info.id == session.targetLayerId) {
                    return {};
                }
                return resolveOverscanRasterSource(info, padX, padY, *m_layerScreenSourceCache,
                    *m_renderer, m_viewport, viewportWidth, viewportHeight, m_canvas.width(),
                    m_canvas.height(), flipH, flipV, session.viewportRevision,
                    LayerScreenSourceCache::SourceKind::BoardPreviewColor);
            },
            [&](const CompositeLayerInfo& info) -> GLuint {
                if (editingMask && !info.isGroup && info.id == session.targetLayerId) {
                    return previewedTargetTexture;
                }
                return acquireLayerMaskTextureForPreview(
                    info, flipH, flipV, session.viewportRevision);
            });
        if (boardPreviewTexture) {
            drawViewportTexture(boardPreviewTexture, false);
            renderedBoardPreview = true;
        }
    }

    if (!renderedBoardPreview) {
        renderBoardLayers(boardLayerStack);
    }
    m_renderer->endFrame();

    if (m_sceneFboManager.sceneFbo()) {
        m_sceneFboManager.blitToDefaultFbo(this, defaultFbo, width(), height());
    }

    session.viewportDirty = false;
    session.transformDirty = false;
    session.sourceDirty = false;
    session.selectionMaskDirty = false;
}

void OpenGLCanvasWidget::paintGL_renderLassoFillOverlay(
    const std::vector<CompositeLayerInfo>& layerStack,
    const std::vector<CompositeLayerInfo>& boardLayerStack, GLint defaultFbo)
{
    Q_UNUSED(boardLayerStack);

    if (!m_lassoFillPreview.active || !m_lassoFillViewportPreview.active || !m_renderer
        || !m_layerScreenSourceCache) {
        return;
    }

    const QSize surfaceSize = currentSurfacePixelSize(this);
    const int viewportWidth = surfaceSize.width();
    const int viewportHeight = surfaceSize.height();
    if (viewportWidth <= 0 || viewportHeight <= 0) {
        return;
    }

    auto* viewportCompositor = m_renderer->viewportCompositor();
    auto* maskRenderer = m_renderer->lassoMaskRenderer();
    auto* targetPreviewPass = m_renderer->targetLayerPreviewPass();
    if (!viewportCompositor || !maskRenderer || !targetPreviewPass) {
        return;
    }

    auto* targetLayer = activeLayer();
    if (!targetLayer || targetLayer->id != m_lassoFillPreview.targetLayerId
        || !targetLayer->pixelGrid()) {
        return;
    }

    auto& session = m_lassoFillViewportPreview;
    const Vector2 cameraPosition = m_viewport.camera().position();
    const float cameraZoom = m_viewport.camera().zoom();
    const float cameraRotation = m_viewport.camera().rotation();
    const bool cameraAnimating = m_viewport.camera().isAnimating();
    const bool flipH = effectiveContentFlipH();
    const bool flipV = effectiveContentFlipV();
    const bool viewportChanged = cameraAnimating
        || session.viewportWidth != static_cast<uint32_t>(viewportWidth)
        || session.viewportHeight != static_cast<uint32_t>(viewportHeight) || session.flipH != flipH
        || session.flipV != flipV || !nearlyEqualPoint(session.cameraPosition, cameraPosition)
        || !nearlyEqualFloat(session.cameraZoom, cameraZoom)
        || !nearlyEqualFloat(session.cameraRotation, cameraRotation);
    if (viewportChanged) {
        session.viewportWidth = static_cast<uint32_t>(viewportWidth);
        session.viewportHeight = static_cast<uint32_t>(viewportHeight);
        session.cameraPosition = cameraPosition;
        session.cameraZoom = cameraZoom;
        session.cameraRotation = cameraRotation;
        session.flipH = flipH;
        session.flipV = flipV;
        session.viewportRevision += 1;
        session.screenPolygonDirty = true;
        session.screenMaskDirty = true;
        session.screenSourcesDirty = true;
        m_layerScreenSourceCache->invalidateByViewport();
    }
    if (session.viewportRevision == 0) {
        session.viewportRevision = 1;
    }

    if (session.screenPolygonDirty) {
        session.polygonScreen.clear();
        session.polygonScreen.reserve(session.polygonWorld.size());
        for (const Vector2& point : session.polygonWorld) {
            session.polygonScreen.push_back(screenFromDocumentWorld(point));
        }

        if (session.polygonScreen.size() < 3) {
            session.clippedScreenBounds = {};
            return;
        }

        float minX = session.polygonScreen.front().x;
        float minY = session.polygonScreen.front().y;
        float maxX = minX;
        float maxY = minY;
        for (const Vector2& point : session.polygonScreen) {
            minX = std::min(minX, point.x);
            minY = std::min(minY, point.y);
            maxX = std::max(maxX, point.x);
            maxY = std::max(maxY, point.y);
        }

        constexpr int kLassoFillMaskPaddingPx = 2;
        const QRect viewportRect(0, 0, viewportWidth, viewportHeight);
        const QRect polygonRect(static_cast<int>(std::floor(minX)) - kLassoFillMaskPaddingPx,
            static_cast<int>(std::floor(minY)) - kLassoFillMaskPaddingPx,
            std::max(1,
                static_cast<int>(std::ceil(maxX)) - static_cast<int>(std::floor(minX)) + 1
                    + kLassoFillMaskPaddingPx * 2),
            std::max(1,
                static_cast<int>(std::ceil(maxY)) - static_cast<int>(std::floor(minY)) + 1
                    + kLassoFillMaskPaddingPx * 2));
        session.clippedScreenBounds = polygonRect.intersected(viewportRect);
        session.screenPolygonDirty = false;
        session.screenMaskDirty = true;
    }

    if (!session.clippedScreenBounds.isValid() || session.clippedScreenBounds.isEmpty()) {
        return;
    }

    if (session.screenMaskDirty || !session.screenMaskTexture) {
        std::vector<Vector2> localScreenPolygon;
        localScreenPolygon.reserve(session.polygonScreen.size());
        for (const Vector2& point : session.polygonScreen) {
            localScreenPolygon.push_back(
                { point.x - static_cast<float>(session.clippedScreenBounds.x()),
                    point.y - static_cast<float>(session.clippedScreenBounds.y()) });
        }

        const auto maskResult
            = maskRenderer->renderMask(localScreenPolygon, session.clippedScreenBounds);
        if (!maskResult.isValid()) {
            session.screenMaskTexture = 0;
            session.screenMaskBounds = {};
            return;
        }

        session.screenMaskTexture = maskResult.texture;
        session.screenMaskBounds = maskResult.bounds;
        session.screenMaskDirty = false;
    }

    if (!session.screenMaskTexture || !session.screenMaskBounds.isValid()
        || session.screenMaskBounds.isEmpty()) {
        return;
    }

    const CompositeLayerInfo* targetLayerInfo = nullptr;
    std::function<void(const std::vector<CompositeLayerInfo>&)> findTarget
        = [&](const std::vector<CompositeLayerInfo>& layers) {
              for (const auto& info : layers) {
                  if (targetLayerInfo) {
                      return;
                  }
                  if (info.isGroup) {
                      findTarget(info.children);
                      continue;
                  }
                  if (info.id == session.targetLayerId) {
                      targetLayerInfo = &info;
                      return;
                  }
              }
          };
    findTarget(layerStack);
    if (!targetLayerInfo) {
        return;
    }

    const bool finiteDocumentBounds = hasFiniteDocumentBounds();
    const uint32_t canvasWidth = finiteDocumentBounds ? m_canvas.width() : 0u;
    const uint32_t canvasHeight = finiteDocumentBounds ? m_canvas.height() : 0u;
    // Acquire the target's RAW (unmasked) color: the fill is applied on top by
    // targetPreviewPass, then the compositor gates the effected result once in its
    // final blend — exactly mirroring the transform target. Baking the mask into the
    // base would both change the effect input and double soft mask edges. Clearing the
    // clip-mask fields routes this acquire through the direct (raw) render path. The
    // target's LayerColor cache entry is consumed only here (the composite resolver
    // returns the previewed texture for the target), so storing it unmasked is safe.
    CompositeLayerInfo targetBaseInfo = *targetLayerInfo;
    targetBaseInfo.effectChainRevision = targetLayer->effectChainRevision;
    targetBaseInfo.externalClipMaskGrid = nullptr;
    targetBaseInfo.clipMaskLuminanceReveal = false;
    targetBaseInfo.clipMaskGrid2 = nullptr;
    const GLuint targetLayerBaseTexture = m_layerScreenSourceCache->acquireLayerTexture(
        targetBaseInfo, *m_renderer, m_viewport, canvasWidth, canvasHeight, flipH, flipV,
        session.viewportRevision, LayerScreenSourceCache::SourceKind::LayerColor,
        ruwa::core::effects::LayerSourcePurpose::RawContent);
    if (!targetLayerBaseTexture) {
        return;
    }

    GLuint selectionMaskTexture = 0;
    if (m_selectionController && m_selectionController->lassoSelection().hasSelection()
        && !m_selectionController->lassoSelection().mask().empty()) {
        CompositeLayerInfo selectionMaskInfo;
        selectionMaskInfo.id = lassoPreviewSelectionMaskCacheId();
        // const_cast: CompositeLayerInfo::tileGrid is non-const for legacy compositor
        // GPU-sync paths. Pixel data is treated as read-only here; mutations to the
        // selection mask must go through LassoSelectionManager::MaskMutationScope.
        selectionMaskInfo.tileGrid
            = const_cast<TileGrid*>(&m_selectionController->lassoSelection().mask());
        selectionMaskInfo.opacity = 1.0f;
        selectionMaskInfo.blendMode = 0;
        selectionMaskInfo.visible = true;
        selectionMaskTexture = m_layerScreenSourceCache->acquireLayerTexture(selectionMaskInfo,
            *m_renderer, m_viewport, 0u, 0u, flipH, flipV, session.viewportRevision,
            LayerScreenSourceCache::SourceKind::AlphaMask);
    }

    const GLuint previewedTargetTexture = targetPreviewPass->render(targetLayerBaseTexture,
        session.screenMaskTexture, session.screenMaskBounds, static_cast<uint32_t>(viewportWidth),
        static_cast<uint32_t>(viewportHeight), m_lassoFillPreview.color, targetLayer->alphaLock,
        selectionMaskTexture);
    if (!previewedTargetTexture) {
        return;
    }

    viewportCompositor->beginFrame(
        static_cast<uint32_t>(viewportWidth), static_cast<uint32_t>(viewportHeight));

    // Keep the filled target raw here; its fixed-position layer mask is resolved for
    // the compositor below and applied after the target's realtime effect chain.
    // Bake the opaque canvas background (the special Background layer, drawn as a
    // backdrop and NOT part of layerStack) into previewContentTexture. Otherwise a
    // stack that is semi-transparent in the fill region (layer opacity < 100%) would
    // be composited over transparent here, and the replace-mode blit below would show
    // it over black instead of over the background — dark / wrong colours. With the
    // background baked in, previewContent is the true visible result and replace is
    // exact. When there is no opaque background (transparent canvas / checker) we keep
    // src-over so the checker still shows through the semi-transparent result.
    Color lassoCanvasBackground;
    const bool lassoHasCanvasBackground
        = m_layerCompositingBuilder->resolveCanvasBackgroundColor(lassoCanvasBackground);
    const GLuint previewContentTexture = viewportCompositor->compositeLayers(
        layerStack,
        [&](const CompositeLayerInfo& info) -> GLuint {
            if (!info.isGroup && info.id == session.targetLayerId) {
                return previewedTargetTexture;
            }
            return m_layerScreenSourceCache->acquireLayerTexture(info, *m_renderer, m_viewport,
                canvasWidth, canvasHeight, flipH, flipV, session.viewportRevision,
                LayerScreenSourceCache::SourceKind::LayerColor,
                ruwa::core::effects::LayerSourcePurpose::RawContent);
        },
        lassoHasCanvasBackground ? lassoCanvasBackground : Color::transparent(), cameraZoom,
        buildViewportEffectRegion(m_viewport, static_cast<float>(canvasWidth),
            static_cast<float>(canvasHeight), flipH, flipV, viewportWidth, viewportHeight),
        [&](const CompositeLayerInfo& info, int padX,
            int padY) -> GLViewportCompositor::OverscanLayerSource {
            if (info.isGroup || info.id == session.targetLayerId) {
                return {};
            }
            return resolveOverscanRasterSource(info, padX, padY, *m_layerScreenSourceCache,
                *m_renderer, m_viewport, viewportWidth, viewportHeight, canvasWidth, canvasHeight,
                flipH, flipV, session.viewportRevision);
        },
        [&](const CompositeLayerInfo& info) {
            return acquireLayerMaskTextureForPreview(info, flipH, flipV, session.viewportRevision);
        });
    if (!previewContentTexture) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(defaultFbo));
    glViewport(0, 0, viewportWidth, viewportHeight);

    const float cornerRadiusCanvasPx = canvasCornerRadiusCanvasPx();
    GLViewportCompositor::CanvasClipParams previewClipParams;
    if (finiteDocumentBounds) {
        previewClipParams.enabled = true;
        previewClipParams.cameraPosition = cameraPosition;
        previewClipParams.cameraZoom = cameraZoom;
        previewClipParams.cameraRotation = cameraRotation;
        previewClipParams.canvasWidth = static_cast<float>(canvasWidth);
        previewClipParams.canvasHeight = static_cast<float>(canvasHeight);
        previewClipParams.canvasCornerRadius = cornerRadiusCanvasPx;
    }

    // A bounds-expanding effect on the target (e.g. blur) makes the filled result
    // bleed OUTSIDE the lasso. The lasso scissor + mask below would clip that
    // bleed exactly at the selection edge (cutting half the blur). previewContent
    // is the full correct recomposite, so grow the preview region by the effect's
    // screen-space reach and drop the lasso-shape clip: where the bleed has decayed
    // (the expanded bbox edge) the preview equals the scene, so replacing the wider
    // region is seamless.
    int effectPadScreen = 0;
    if (targetLayer) {
        // realtimeOnly: a preview-disabled effect is NOT applied to this preview,
        // so it must not expand the region — otherwise the (effect-less) preview
        // would overwrite the surrounding cached effected background.
        const int effectPadDoc = ruwa::core::effects::EffectCoverageResolver::neighborhoodPadPixels(
            targetLayer->effects, /*realtimeOnly=*/true);
        if (effectPadDoc > 0) {
            effectPadScreen
                = static_cast<int>(std::ceil(static_cast<float>(effectPadDoc) * cameraZoom));
        }
    }

    QRect maskBounds = session.screenMaskBounds;
    if (effectPadScreen > 0) {
        maskBounds = maskBounds.adjusted(
            -effectPadScreen, -effectPadScreen, effectPadScreen, effectPadScreen);
    }
    const QRect drawBounds = maskBounds.intersected(QRect(0, 0, viewportWidth, viewportHeight));
    if (drawBounds.isEmpty()) {
        return;
    }

    const GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    GLint previousScissor[4] = {};
    GLint previousBlendSrcRgb = 0;
    GLint previousBlendDstRgb = 0;
    GLint previousBlendSrcAlpha = 0;
    GLint previousBlendDstAlpha = 0;
    glGetIntegerv(GL_SCISSOR_BOX, previousScissor);
    glGetIntegerv(GL_BLEND_SRC_RGB, &previousBlendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &previousBlendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &previousBlendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &previousBlendDstAlpha);

    glEnable(GL_SCISSOR_TEST);
    glScissor(drawBounds.x(), viewportHeight - drawBounds.y() - drawBounds.height(),
        drawBounds.width(), drawBounds.height());
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    GLViewportCompositor::LassoMaskParams previewLassoMask;
    if (effectPadScreen == 0) {
        // No bounds-expanding effect: clip to the lasso shape as before.
        previewLassoMask.maskTexture = session.screenMaskTexture;
        previewLassoMask.originX = session.screenMaskBounds.x();
        previewLassoMask.originY = session.screenMaskBounds.y();
        previewLassoMask.width = session.screenMaskBounds.width();
        previewLassoMask.height = session.screenMaskBounds.height();
    }
    // else: leave the mask empty (coverage 1 over the expanded scissor) so the
    // effect's bleed beyond the lasso is shown. The fill's own edge antialiasing
    // is already baked into previewContent via the selection mask, so dropping the
    // draw-time lasso clip does not harden the fill edge.
    // Replace (not src-over) inside the lasso region: previewContentTexture is the full
    // recomposite, and the scene already contains the target layer, so src-over would
    // double-composite a <100% opacity layer. Replace does dst = preview*cov + scene*(1-cov),
    // showing exactly the recomposited result. Only valid when the background was baked
    // into previewContent above (opaque canvas); otherwise the result is semi-transparent
    // and must src-over the scene so the checker shows through.
    viewportCompositor->drawTexture(previewContentTexture, previewClipParams, previewLassoMask,
        /*replaceWithCoverage=*/lassoHasCanvasBackground);
    glBlendFuncSeparate(static_cast<GLenum>(previousBlendSrcRgb),
        static_cast<GLenum>(previousBlendDstRgb), static_cast<GLenum>(previousBlendSrcAlpha),
        static_cast<GLenum>(previousBlendDstAlpha));
    if (blendWasEnabled) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    if (scissorWasEnabled) {
        glScissor(previousScissor[0], previousScissor[1], previousScissor[2], previousScissor[3]);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}

void OpenGLCanvasWidget::paintGL_renderLassoOverlay()
{
    // Note: previously this early-returned during lasso fill drag, which hid the
    // marching-ants overlay of the existing selection. The lasso fill tool does
    // not use the selection controller's drag state (m_lassoFillPoints is
    // separate from m_selectionController->lassoPoints()), so the new-drag
    // branches below stay inert during lasso fill — only the existing-selection
    // edges render, which is the desired behavior.
    auto* lassoOverlay = m_overlayManager ? m_overlayManager->lassoOverlay() : nullptr;
    if (!lassoOverlay || !lassoOverlay->isInitialized() || !m_selectionController)
        return;
    const auto& edges = m_selectionController->lassoSelection().edges();
    const std::vector<LassoEdgeSegment>* edgesToRender = &edges;
    std::vector<LassoEdgeSegment> transformedEdges;
    if (const TransformState* displayState = selectionDisplayTransformState();
        displayState && !edges.empty()) {
        transformedEdges.reserve(edges.size());
        for (const auto& seg : edges) {
            transformedEdges.push_back(
                { displayState->transformPoint(seg.a), displayState->transformPoint(seg.b) });
        }
        edgesToRender = &transformedEdges;
    }
    const auto& lassoPoints = m_selectionController->lassoPoints();
    bool activeClosed = (m_selectionController->isRectSelectionActive() && lassoPoints.size() >= 4)
        || (m_selectionController->isCircleSelectionActive() && lassoPoints.size() >= 3);
    bool drawingNewReplace = m_selectionController->selectionWillReplace()
        && (m_selectionController->isLassoActive() || m_selectionController->isRectSelectionActive()
            || m_selectionController->isCircleSelectionActive())
        && lassoPoints.size() >= 2 && !edgesToRender->empty();
    float edgesAlpha = drawingNewReplace ? 0.2f : 0.9f;

    GLuint addPathMaskTexture = 0;
    float maskOriginX = 0.0f, maskOriginY = 0.0f, maskWidth = 0.0f, maskHeight = 0.0f;
    float pathAlphaInside = 0.2f, pathAlphaOutside = 1.0f;
    const bool drawingNewAddOrSubtract
        = (m_selectionController->selectionIsAdd() || m_selectionController->selectionIsSubtract())
        && (m_selectionController->isLassoActive() || m_selectionController->isRectSelectionActive()
            || m_selectionController->isCircleSelectionActive())
        && lassoPoints.size() >= 2 && !edgesToRender->empty() && m_renderer
        && m_renderer->transformRenderer() && m_renderer->tileRenderer();
    if (drawingNewAddOrSubtract && !m_selectionController->lassoSelection().mask().empty()) {
        auto* transformRenderer = m_renderer->transformRenderer();
        auto* tileRenderer = m_renderer->tileRenderer();
        // GPU-sync only (texture upload), no pixel-data mutation.
        LassoSelectionManager::MaskMutationScope maskScope(m_selectionController->lassoSelection());
        maskScope.disableSoftAlphaInvalidation();
        TileGrid& maskGrid = maskScope.grid();
        for (auto& [key, tile] : maskGrid.tiles()) {
            if (!tile.hasTexture())
                tileRenderer->ensureTileTexture(tile);
            if (tile.isDirty())
                tileRenderer->uploadTileData(tile);
        }
        transformRenderer->buildMaskAtlas(maskGrid, tileRenderer);
        maskScope.disableSnapshotInvalidation();
        addPathMaskTexture = transformRenderer->maskAtlasTexture();
        if (addPathMaskTexture != 0) {
            maskOriginX = static_cast<float>(
                transformRenderer->maskAtlasMinTX() * static_cast<int>(TILE_SIZE));
            maskOriginY = static_cast<float>(
                transformRenderer->maskAtlasMinTY() * static_cast<int>(TILE_SIZE));
            maskWidth = static_cast<float>(transformRenderer->maskAtlasWidth());
            maskHeight = static_cast<float>(transformRenderer->maskAtlasHeight());
            if (m_selectionController->selectionIsSubtract()) {
                pathAlphaInside = 1.0f;
                pathAlphaOutside = 0.2f;
            }
        }
    }

    const auto contentVp = canvasContentViewProjectionMatrix();
    lassoOverlay->render(m_viewport, lassoPoints, activeClosed, *edgesToRender, edgesAlpha,
        addPathMaskTexture, maskOriginX, maskOriginY, maskWidth, maskHeight, pathAlphaInside,
        pathAlphaOutside, &contentVp);
    if (lassoOverlay->isAnimating())
        update();
}

void OpenGLCanvasWidget::paintGL()
{
    if (!m_initialized || !m_renderer)
        return;

    paintGL_updateCameraAndEmitSignals();
    const bool canvasCornerAnimating = updateCanvasCornerEffectState();
    const bool fillPreviewAnimating = updateFillPreviewAnimationState();
    const auto& layerStack = m_layerCompositingBuilder->buildLayerStack();
    const auto& boardLayerStack = m_layerCompositingBuilder->buildBoardLayerStack();

    const PaintGLCameraFrameState currentCameraFrameState = capturePaintGLCameraFrameState(this);
    const auto previousCameraFrameStateIt = paintGLCameraFrameStates().find(this);
    const PaintGLCameraFrameState previousCameraFrameState
        = previousCameraFrameStateIt != paintGLCameraFrameStates().end()
        ? previousCameraFrameStateIt->second
        : PaintGLCameraFrameState {};
    const bool selectionOperationActive = m_selectionController
        && (m_selectionController->isLassoActive() || m_selectionController->isRectSelectionActive()
            || m_selectionController->isCircleSelectionActive()
            || m_selectionController->pendingSelectionJob().active
            || m_selectionController->pendingSelectionReadback().active
            || m_selectionTick.isActive());
    const bool viewportTransformPreviewActive = m_transformController.isActive()
        && m_transformViewportPreview.active && m_transformViewportPreview.viewportPathEnabled;
    const bool contentMutationActive = m_brush->hasActiveStroke()
        || (m_strokeHost && m_strokeHost->hasPendingFinalization()) || isFillPreviewActive()
        || m_lassoFillPreview.active || m_lassoFillViewportPreview.active
        || selectionOperationActive
        || (m_transformController.isActive() && !viewportTransformPreviewActive)
        || m_pendingTransform.active || m_autoApplyingTransform
        || m_canvas.undoManager().isUndoRedoInProgress();
    paintGLCompositeContexts()[this] = PaintGLCompositeContext {
        paintGLCameraStateChanged(previousCameraFrameState, currentCameraFrameState)
            && !contentMutationActive,
        previousCameraFrameState.compositionCacheClean
    };
    paintGL_markTransformDirty();

    paintGL_runComposite(layerStack);

    auto* transformOverlay = m_overlayManager ? m_overlayManager->transformOverlay() : nullptr;
    auto* canvasResizeOverlay
        = m_overlayManager ? m_overlayManager->canvasResizeOverlay() : nullptr;
    auto* brushCursorOverlay = m_overlayManager ? m_overlayManager->brushCursorOverlay() : nullptr;
    auto* eyedropperCursorOverlay
        = m_overlayManager ? m_overlayManager->eyedropperCursorOverlay() : nullptr;
    auto* textEditOverlay = m_overlayManager ? m_overlayManager->textEditOverlay() : nullptr;
    const bool wantBrushCursor = !m_skipCursorOverlays && brushCursorOverlay
        && m_cursorOverlayState.brushVisible && m_cursorOverlayState.brushRadius > 0.5f;
    const bool wantEyedropperCursor = !m_skipCursorOverlays && eyedropperCursorOverlay
        && m_cursorOverlayState.eyedropperVisible;
    if (wantBrushCursor) {
        ensureCursorOverlayInitialized(brushCursorOverlay, "brush cursor overlay");
    }
    if (wantEyedropperCursor) {
        ensureCursorOverlayInitialized(eyedropperCursorOverlay, "eyedropper cursor overlay");
    }
    const bool moveAxisGuideActivePre = m_transformController.moveAxisGuideActive();
    const bool autoSnapGuideActivePre = m_transformController.autoSnapGuideState().active();
    const bool drawTransformChromePre = !m_moveOnlyTransform;
    const bool drawTransformOverlay
        = (transformOverlay && transformOverlay->isInitialized() && !m_autoApplyingTransform
            && (m_transformController.isActive() || transformOverlay->isAnimating())
            && (drawTransformChromePre || moveAxisGuideActivePre || autoSnapGuideActivePre));
    const bool drawCanvasResizeOverlay
        = (canvasResizeOverlay && canvasResizeOverlay->isInitialized()
            && (m_canvasResizeOverlayActive || canvasResizeOverlay->isAnimating()));
    const bool drawTextEditOverlay
        = textEditOverlay && textEditOverlay->isInitialized() && textEditOverlay->isActive();
    const bool drawBrushCursor
        = wantBrushCursor && brushCursorOverlay && brushCursorOverlay->isInitialized();
    const bool drawEyedropperCursor = wantEyedropperCursor && eyedropperCursorOverlay
        && eyedropperCursorOverlay->isInitialized();
    // Throttled frosted-glass backdrop capture for on-canvas overlays. Needs the
    // scene texture, so it forces the scene FBO on the frames it actually runs.
    const bool captureBackdropThisFrame
        = m_backdropConsumers > 0 && (m_backdropFrameCounter % kBackdropRefreshInterval == 0);
    ++m_backdropFrameCounter;

    const bool needSceneForOverlay = drawTransformOverlay || drawCanvasResizeOverlay
        || drawTextEditOverlay || drawBrushCursor || drawEyedropperCursor
        || captureBackdropThisFrame;

    GLint defaultFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &defaultFbo);
    GLuint sceneTarget = 0;
    paintGL_renderSceneAndBlit(sceneTarget, defaultFbo, needSceneForOverlay, boardLayerStack);

    if (captureBackdropThisFrame && sceneTarget == m_sceneFboManager.sceneFbo()
        && m_sceneFboManager.sceneTexture()) {
        paintGL_captureBackdrop(m_sceneFboManager.sceneTexture());
        // Capture rebinds its own FBOs/viewport — restore the default target.
        const QSize surf = currentSurfacePixelSize(this);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(defaultFbo));
        glViewport(0, 0, surf.width(), surf.height());
        emit backdropSnapshotUpdated();
    }

    paintGL_renderTransformViewportPreview(layerStack, boardLayerStack, defaultFbo);

    paintGL_renderOverlays(sceneTarget);

    paintGL_processSelectionReadback();

    paintGL_renderLassoFillOverlay(layerStack, boardLayerStack, defaultFbo);
    paintGL_renderLassoOverlay();

    PaintGLCameraFrameState completedFrameState = currentCameraFrameState;
    completedFrameState.compositionCacheClean = !m_canvas.compositionCache().hasDirtyPositions();
    paintGLCameraFrameStates()[this] = completedFrameState;

    m_renderer->endFrame();

    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    if (fillPreviewAnimating)
        update();
    if (canvasCornerAnimating)
        update();
    if (m_viewport.camera().isAnimating() && !m_cameraAnimationFrameTimer.isActive()) {
        constexpr qint64 kCameraFrameIntervalNs = 1000000000LL / 120;
        const qint64 elapsedNs
            = m_cameraFrameTimer.isValid() ? m_cameraFrameTimer.nsecsElapsed() : 0;
        const qint64 remainingNs = std::max<qint64>(0, kCameraFrameIntervalNs - elapsedNs);
        const int delayMs = static_cast<int>((remainingNs + 999999LL) / 1000000LL);
        m_cameraAnimationFrameTimer.start(delayMs);
    }
    if (m_panSamplingActive)
        update();
}

} // namespace aether
