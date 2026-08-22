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
#include "features/canvas/rendering/CanvasBackdropRenderer.h"
#include "features/canvas/rendering/LayerScreenSourceCache.h"
#include "features/canvas/rendering/SmartContentCompositor.h"
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
#include "features/canvas/overlays/ToolCursorOverlayGL.h"
#include "features/canvas/overlays/LassoOverlay.h"
#include "features/canvas/overlays/LassoFillOverlay.h"
#include "features/canvas/overlays/TextEditOverlayGL.h"
#include "features/canvas/selection/PolygonClipUtils.h"
#include "features/canvas/selection/SelectionMaskOps.h"
#include "features/canvas/scene/CanvasDisplayTransforms.h"
#include "features/transform/TransformApplicator.h"
#include "features/transform/TransformGeometry.h"
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
#include "shared/undo/LayerRemoveCommand.h"
#include "features/layers/smart/SmartDocument.h"
#include "shared/clipboard/EditClipboard.h"
#include "shared/style/AnimationPolicy.h"
#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TileGridClone.h"
#include "shared/tiles/TilePixelAccess.h"
#include "shared/types/GeometryHelpers.h"
#include "shared/widgets/DotGridLoadingIndicator.h"
#include "features/canvas/undo/DrawCommand.h"
#include "features/canvas/undo/ApplyMaskCommand.h"
#include "features/canvas/undo/ApplyLayerEffectsCommand.h"
#include "features/canvas/undo/LayerContentSwapCommand.h"
#include "features/canvas/undo/SmartContentSwapCommand.h"
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
#include <QFutureWatcher>
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
#include <QShowEvent>
#include <QSet>
#include <QThread>
#include <QtConcurrent>

#include "platform/windows/WindowsInkFeedback.h"
#include "features/canvas/rendering/LayerCompositingBuilder.h"
#include "features/canvas/selection/CanvasSelectionController.h"
#include "features/canvas/ui/CanvasMetricLabelOverlay.h"
#include "services/input/StylusInputManager.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace anim = ruwa::ui::core::anim;

namespace {

/// Display-pyramid rebuilds a frame may spend on tiles that already hold
/// content, while a continuous preview is running — see
/// OpenGLCanvasWidget::displayPyramidDeferrableBudget, which is the only thing
/// that applies this, and never to a discrete edit. Absent tiles are never
/// counted — see DisplayPyramidPacing.
///
/// This is the LEVEL-1 allowance: the pyramid shrinks it by four per level and
/// exempts the levels the display samples, because the work shrinks the same
/// way and a shared cap is otherwise spent entirely on the bottom of the
/// cascade (see DisplayPyramid::UpdateRequest::deferrableBudget).
///
/// A rebuild is one 258x258 draw with four taps, so the GPU side is noise; the
/// cost that matters is ~20 GL calls each, and this caps that at the same order
/// as one frame's compositing. A normal stroke dirties 2-6 level-zero tiles and
/// so asks for well under ten rebuilds — the budget only ever bites on a
/// full-canvas event (a fill preview, a transform over a big layer, a stroke
/// under a layer effect whose coverage expansion rings every dab), which is
/// exactly where a couple of frames of content lag in the corners of the screen
/// is preferable to spending the frame.
constexpr uint32_t kDisplayPyramidDeferrableBudget = 64;

bool selectionStateMatches(const aether::SelectionState& lhs, const aether::SelectionState& rhs)
{
    return lhs.layer.primaryId == rhs.layer.primaryId
        && lhs.layer.selectedIds == rhs.layer.selectedIds && lhs.lasso.regions == rhs.lasso.regions
        && lhs.lasso.canvasWidth == rhs.lasso.canvasWidth
        && lhs.lasso.canvasHeight == rhs.lasso.canvasHeight
        && lhs.lasso.maskTiles == rhs.lasso.maskTiles
        && lhs.lasso.maskHasSoftAlpha == rhs.lasso.maskHasSoftAlpha;
}

inline int32_t floorDiv(int32_t a, int32_t b)
{
    return (a >= 0) ? (a / b) : ((a - b + 1) / b);
}

/// Flatten a document-space region of a tile grid into an image, for handing the
/// copied selection to the system clipboard. Tile pixels are premultiplied, and
/// float formats are clamped to 8 bit — the full-fidelity copy stays in the tile
/// grid on the edit clipboard.
QImage imageFromTileGridRegion(const aether::TileGrid& grid, const QRect& bounds)
{
    if (bounds.isEmpty()) {
        return {};
    }

    QImage image(bounds.width(), bounds.height(), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        return {};
    }
    image.fill(Qt::transparent);

    const auto toByte
        = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };

    for (const auto& [key, tile] : grid.tiles()) {
        const int tileOriginX = key.x * static_cast<int>(aether::TILE_SIZE);
        const int tileOriginY = key.y * static_cast<int>(aether::TILE_SIZE);
        for (uint32_t localY = 0; localY < aether::TILE_SIZE; ++localY) {
            const int docY = tileOriginY + static_cast<int>(localY);
            if (docY < bounds.top() || docY > bounds.bottom()) {
                continue;
            }
            auto* scanLine = reinterpret_cast<QRgb*>(image.scanLine(docY - bounds.top()));
            for (uint32_t localX = 0; localX < aether::TILE_SIZE; ++localX) {
                const int docX = tileOriginX + static_cast<int>(localX);
                if (docX < bounds.left() || docX > bounds.right()) {
                    continue;
                }
                float c[4];
                aether::readTilePixelF(tile, localX, localY, c);
                if (c[3] <= 0.0f) {
                    continue;
                }
                scanLine[docX - bounds.left()]
                    = qRgba(toByte(c[0]), toByte(c[1]), toByte(c[2]), toByte(c[3]));
            }
        }
    }

    return image.convertToFormat(QImage::Format_ARGB32);
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

    if (layer->isIsolatedPixelLayer() && layer->hasSmartContent()) {
        const aether::Rect sourceBounds = layer->smartContentBounds();
        if (sourceBounds.width <= 0.0f || sourceBounds.height <= 0.0f) {
            return false;
        }

        aether::Vector2 sourcePos;
        const aether::TransformState state
            = aether::transformStateWithSourceBounds(layer->smartTransform, sourceBounds);
        return state.tryInverseTransformPoint(worldPos, sourcePos)
            && gridPixelAlphaAt(layer->smartGrid(), sourcePos) > 0.0f;
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

        const std::optional<aether::Rect> bounds = aether::transformBoundsForLayer(layer);
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
        return aether::transformStateWithSourceBounds(layer->textData->transform, sourceBounds);
    }
    if (layer->isIsolatedPixelLayer() && layer->hasSmartContent()) {
        return aether::transformStateWithSourceBounds(
            layer->smartTransform, layer->smartContentBounds());
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
constexpr qint64 kCanvasCornerIdleDelayMs = 1000;
constexpr qint64 kCanvasCornerInteractionCooldownMs = 160;
constexpr qint64 kCanvasCornerFrameDelayMs = 16;
constexpr float kCanvasCornerVisibilityMarginPx = 0.5f;
constexpr float kCanvasCornerMaxScreenRadiusPx = 12.0f;
constexpr float kCanvasCornerAnimationSpeed = 14.0f;
constexpr qint64 kClassicFillWaitPopupDelayMs = 2000;
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

const QUuid& lassoPreviewSelectionMaskCacheId()
{
    static const QUuid id(QStringLiteral("{5b4ce0bb-14e7-4bc5-9ed4-bcb954cf6989}"));
    return id;
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
        m_opacityAnim->setDuration(anim::duration(120));
        m_opacityAnim->setStartValue(m_opacityEffect->opacity());
        m_opacityAnim->setEndValue(1.0);

        m_posAnim->stop();
        m_posAnim->setDuration(anim::duration(120));
        m_posAnim->setStartValue(startPos);
        m_posAnim->setEndValue(topLeft);

        anim::start(m_opacityAnim);
        anim::start(m_posAnim);
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
        m_opacityAnim->setDuration(anim::duration(180));
        m_opacityAnim->setStartValue(m_opacityEffect->opacity());
        m_opacityAnim->setEndValue(0.0);

        m_posAnim->stop();
        m_posAnim->setDuration(anim::duration(180));
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

        // The opacity animation owns the completion (it hides the popup), so
        // start it last: with animations disabled it finishes inside the call.
        anim::start(m_posAnim);
        anim::start(m_opacityAnim);
    }

    void startMorph(const QRect& startGeometry, const QRect& targetGeometry, int token)
    {
        m_posAnim->stop();
        m_geometryAnim->stop();
        m_geometryAnim->setDuration(anim::duration(150));
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

        anim::start(m_geometryAnim);
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

        m_label->setFont(theme.font(ruwa::ui::core::ThemeFontRole::Body, QFont::Medium));
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

    // Raw snapshots are sized for the grid's own pixel format; imported content
    // may remain RGBA8 inside a 16F/32F document, while masks are always RGBA8.
    // tile->pixels() returns tileByteSize(format) bytes, so copying that many
    // keeps every representation intact.
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
// the composited result of surrounding tiles, so those neighbours have to
// recomposite as well. Every layer's chain is considered, not just the edited
// one: an adjustment layer or a group effect above the edit reads the composite
// BELOW it, so its bleed is driven by content it does not own.
//
// The reach comes from the same resolver the compositor asks for a layer's
// output coverage (effectOutputKeysForGrid), rather than a square ring derived
// from the neighbourhood pad. Two things follow from that, and both of them
// matter:
//
//   * the shape is the effect's own. A directional drop shadow used to ring the
//     edit in all four directions because the pad is a scalar; now it reaches
//     only where it actually draws. The dirty set feeds the display pyramid's
//     rebuild budget, so an expansion wider than the truth is not free — see
//     DisplayPyramid::UpdateRequest::deferrableBudget.
//
//   * the trigger is expandsBounds, the capability that actually describes
//     bleed. The pad only counts effects that additionally declare
//     requiresNeighborTiles; one that expands its bounds without asking for
//     neighbour tiles (a plugin is free to) bled into tiles nothing ever marked
//     dirty, and they stayed stale until something else recomposited them.
//
// Cheap no-op when no visible layer carries a bounds-expanding effect.
std::unordered_set<aether::TileKey, aether::TileKeyHash> expandDirtyKeysByLayerEffects(
    const ruwa::core::layers::LayerModel* model, const std::vector<aether::TileKey>& keys)
{
    std::unordered_set<aether::TileKey, aether::TileKeyHash> result(keys.begin(), keys.end());
    if (!model || keys.empty()) {
        return result;
    }

    const std::unordered_set<aether::TileKey, aether::TileKeyHash> sourceKeys(
        keys.begin(), keys.end());
    model->forEach([&](ruwa::core::layers::LayerData* layer) {
        // A hidden layer composites nothing, so it cannot bleed anywhere, and a
        // chain that does not expand its bounds (every colour effect) would only
        // hand back the set it was given.
        if (!layer || !layer->visible || layer->effects.isEmpty()
            || !ruwa::core::effects::EffectCoverageResolver::chainExpandsBounds(layer->effects)) {
            return;
        }
        const auto expanded = ruwa::core::effects::EffectCoverageResolver::expandedDocumentCoverage(
            sourceKeys, layer->effects);
        result.insert(expanded.begin(), expanded.end());
    });
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
    const aether::VisibleWorldBounds visible
        = aether::visibleWorldBounds(viewport, canvasWidth, canvasHeight, flipH, flipV);
    const aether::Rect visibleBounds { visible.minX, visible.minY, visible.maxX - visible.minX,
        visible.maxY - visible.minY };
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

/**
 * Whether an edit about to be performed has to rasterize the layer first.
 *
 * Same question as layerRequiresRasterizationForPixelEdits, asked at the point
 * of an actual edit: when the layer's MASK is the active target the edit lands
 * in a plain RGBA8 mask grid and the isolated content is never touched, so a
 * smart layer needs no conversion. The raw query above stays untouched — the
 * explicit "Rasterize" command must keep working while a mask is focused.
 */
bool pixelEditsRequireRasterization(const ruwa::core::layers::LayerData* layer)
{
    return layerRequiresRasterizationForPixelEdits(layer) && !layer->maskIsEditTarget();
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
        TilePixelFormat contentFormat = kDefaultTileFormat;
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
                  request->canvasBounds.height, request->contentFormat)
            : floodFillRawTiles(request->layerSnapshotTiles, request->origin.x, request->origin.y,
                  request->color.r, request->color.g, request->color.b, request->color.a,
                  request->selectionMaskTiles, request->canvasBounds.width,
                  request->canvasBounds.height, request->contentFormat);

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

std::optional<Vector2> OpenGLCanvasWidget::displayPyramidFocusPoint() const
{
    // The brush cursor is the one pointer position the widget already keeps in
    // viewport pixels every frame, and it is where the user is looking whenever
    // content is being changed. Anything else (a transform drag, a fill preview)
    // gets the region centre, which is no worse than the arbitrary hash order
    // the rebuild used before.
    if (!m_cursorOverlayState.brushVisible) {
        return std::nullopt;
    }
    return documentWorldFromScreen(
        { m_cursorOverlayState.brushCenterX, m_cursorOverlayState.brushCenterY });
}

uint32_t OpenGLCanvasWidget::displayPyramidDeferrableBudget() const
{
    // The budget buys a cheaper frame with content lag as the currency, and that
    // is only ever a good trade while the content is going to move again next
    // frame anyway: a stroke, a live fill or transform preview. There the dirty
    // set is small and the cap barely bites; when it does, one frame of lag in
    // the corners is invisible against the next dab.
    //
    // A DISCRETE change — undo, a visibility toggle, an effect edit — is the
    // opposite case. Its dirty set can cover the whole viewport, the compositor
    // recomposites all of it with no budget at all, and no further frame is
    // coming to hide the difference. Pacing the pyramid there just publishes the
    // lag: the level-zero tap updates instantly while the level the frame is
    // mostly showing arrives several frames later, tile by tile, cursor-outwards
    // — i.e. the edges of the screen last. Finish the cascade instead.
    const bool continuousPreview = (m_brush && m_brush->hasActiveStroke())
        || hasPendingStrokeFinalization() || isFillPreviewActive() || m_lassoFillPreview.active
        || m_lassoFillViewportPreview.active || m_transformController.isActive()
        || m_pendingTransform.active || m_autoApplyingTransform;
    return continuousPreview ? kDisplayPyramidDeferrableBudget : 0u;
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
                if (!pending.selectionRestoreCaptured) {
                    SelectionRestoreContext selRestore;
                    selRestore.layerSelection
                        = m_layerModel ? m_layerModel->selectionManager() : nullptr;
                    selRestore.lassoSelection = m_selectionController
                        ? &m_selectionController->lassoSelection()
                        : nullptr;
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
                }

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
    stopFillPreview();
    shutdownFillWorker();
    makeCurrent();
    flushPendingFillPreviewTextureDeletes();
    if (m_selectionController) {
        m_selectionController->shutdown(m_selectionRenderer.get());
    }
    flushPendingStrokeFinalization();
    flushPendingTransformFinalization();
    m_sceneFboManager.releaseSceneFbo(this);
    if (m_backdropRenderer) {
        m_backdropRenderer->shutdown();
        m_backdropRenderer.reset();
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
    if (m_fillPreview.active) {
        ++m_fillPreview.viewportRevision;
        m_fillPreview.finalCompositeDirty = true;
        if (m_layerScreenSourceCache) {
            m_layerScreenSourceCache->invalidateByViewport();
        }
    }
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
    stopFillPreview();
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
        connect(m_layerModel, &ruwa::core::layers::LayerModel::layerEffectResultChanged, this,
            &OpenGLCanvasWidget::onLayerEffectResultChanged);
        connect(m_layerModel, &ruwa::core::layers::LayerModel::layerEffectsChanged, this,
            [this](const QUuid& id, quint64) {
                if (m_layerScreenSourceCache) {
                    m_layerScreenSourceCache->invalidateByLayer(id);
                }
                // A content-space filter is baked into the projected content, not
                // run by the compositor, so a chain change has to rebuild that
                // projection — and so does the removal of the last one, which is
                // what the tracking set answers. This is the one case where
                // LayerModel::markLayerEffectsChanged's "never rebuild a
                // projection for a slider value" rule cannot hold: here the
                // projection IS the effect result. It stays gated on the layer
                // actually having such a filter, so every other chain edit is as
                // cheap as it was.
                auto* layer = m_layerModel ? m_layerModel->layerById(id) : nullptr;
                if (layer
                    && (layer->hasContentSpaceEffects()
                        || m_smartContentEffectProjections.contains(id))) {
                    rebuildSmartProjectionCacheForLayer(id);
                    m_canvas.dirtyManager().onStructureChanged();
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
    m_smartContentEffectProjections.clear();
    m_layerHadBoundsEffect.clear();
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
    stopFillPreview();
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

bool OpenGLCanvasWidget::updateBoundsEffectInvalidationState(
    const QUuid& id, const ruwa::core::layers::LayerData* layer)
{
    if (!layer) {
        return m_layerHadBoundsEffect.value(id, false);
    }

    // Bounds-expanding effects bleed beyond the layer's own tile positions.
    // Remember both sides of an on/off transition so disabling or removing the
    // effect also clears the previously rendered neighbour tiles.
    const bool nowBoundsEffect
        = ruwa::core::effects::EffectCoverageResolver::neighborhoodPadPixels(layer->effects) > 0;
    const bool boundsInvalidate = nowBoundsEffect || m_layerHadBoundsEffect.value(id, false);
    m_layerHadBoundsEffect.insert(id, nowBoundsEffect);
    return boundsInvalidate;
}

void OpenGLCanvasWidget::dirtyClippedLayerDependents(const QUuid& id)
{
    if (!m_layerModel) {
        return;
    }

    // Layers clipped to this one are composited as part of its clip group and
    // follow its result. Their own tiles may extend past the base layer.
    const auto clippedLayers = m_layerModel->layersClippedTo(id);
    for (auto* clipped : clippedLayers) {
        if (!clipped) {
            continue;
        }
        m_canvas.dirtyManager().onLayerPropertyChanged(clipped->id);
        if (!clipped->hasChildren()) {
            continue;
        }
        QList<ruwa::core::layers::LayerData*> descendants;
        clipped->flatten(descendants, false);
        for (auto* descendant : descendants) {
            if (descendant) {
                m_canvas.dirtyManager().onLayerPropertyChanged(descendant->id);
            }
        }
    }
}

void OpenGLCanvasWidget::onLayerDataChanged(const QUuid& id)
{
    cancelPendingLassoFillCommit(id);
    if (m_fillPreview.active && m_fillPreview.targetLayerId == id) {
        stopFillPreview();
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
            const bool boundsInvalidate = updateBoundsEffectInvalidationState(id, layer);
            if (layer->isIsolatedPixelLayer()) {
                // The projection is a function of the content grid and the smart
                // transform. A mask-target transform commit changes neither, yet it
                // notifies this layer while its own readback is still pending — so
                // rebuilding here would be pure work at the worst possible moment.
                const bool maskOnlyChange = m_pendingTransform.active
                    && m_pendingTransform.maskTarget && m_pendingTransform.layerId == id;
                if (!maskOnlyChange) {
                    rebuildSmartProjectionCacheForLayer(id);
                }
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
            // Layers clipped to this one are composited as part of its clip group
            // and follow its visibility (a hidden base hides the whole group), so
            // their own tiles must recomposite as well — they can reach past the
            // base's tiles and would otherwise keep stale pixels.
            dirtyClippedLayerDependents(id);
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

void OpenGLCanvasWidget::onLayerEffectResultChanged(const QUuid& id, quint64 revision)
{
    Q_UNUSED(revision);

    if (m_layerScreenSourceCache) {
        m_layerScreenSourceCache->invalidateByLayer(id);
    }
    invalidateCachedLayerStacks();

    if (!m_layerModel) {
        m_canvas.dirtyManager().onLayerPropertyChanged(id);
        requestRender();
        return;
    }

    auto* layer = m_layerModel->layerById(id);
    if (!layer) {
        m_canvas.dirtyManager().onStructureChanged();
        requestRender();
        return;
    }

    const bool boundsInvalidate = updateBoundsEffectInvalidationState(id, layer);
    if (boundsInvalidate || layer->isBackground()) {
        // Covers both active expanded output and the first frame after disabling
        // or removing it, when the old bleed still has to be cleared.
        m_canvas.compositionCache().markAllDirty();
    } else if (layer->isGroup() || layer->isAdjustment()) {
        // These layers have no source tile positions of their own.
        m_canvas.dirtyManager().onStructureChanged();
    } else {
        // Raster, smart, board, text and retained-content layers already have a
        // stable position index. In particular, an effect consumes the smart
        // projection but never mutates its pixels or transform.
        m_canvas.dirtyManager().onLayerPropertyChanged(id);
    }

    dirtyClippedLayerDependents(id);
    if (layerAffectsBoardComposition(layer) || m_boardCompositionLayerIds.contains(id)) {
        invalidateBoardCompositionCache();
    }
    requestRender();
}

void OpenGLCanvasWidget::onLayerRemoved(const QUuid& id)
{
    cancelPendingLassoFillCommit(id);
    if (m_fillPreview.active && m_fillPreview.targetLayerId == id) {
        stopFillPreview();
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
    if (m_fillPreview.active) {
        m_fillPreview.finalCompositeDirty = true;
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
    if (m_transformController.isActive()
        && isBoardCompositionLayerId(m_transformController.layerId())) {
        invalidateBoardCompositionCache();
    }
}

void OpenGLCanvasWidget::rebuildSmartProjectionCacheForLayer(const QUuid& layerId)
{
    invalidateCachedLayerStacks();
    // Whatever this rebuild produces re-arms it (see buildContentSpaceEffectedGrid);
    // every path that ends without a projection must leave it cleared.
    m_smartContentEffectProjections.remove(layerId);
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

    const auto* sourceGrid = layer->smartGrid();
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

std::shared_ptr<TileGrid> OpenGLCanvasWidget::buildContentSpaceEffectedGrid(
    const ruwa::core::layers::LayerData* layer)
{
    if (!layer) {
        return nullptr;
    }
    // The set tracks which cached projections have filters baked into them, so
    // that REMOVING the last content-space filter still rebuilds the projection
    // that had it. Cleared first, re-armed only if a bake actually lands.
    m_smartContentEffectProjections.remove(layer->id);

    const auto contentEffects = layer->contentSpaceEffects();
    const TileGrid* sourceGrid = layer->smartGrid();
    if (contentEffects.isEmpty() || !sourceGrid || sourceGrid->empty() || !m_renderer) {
        return nullptr;
    }

    GLCompositor* compositor = m_renderer->compositor();
    GLTileRenderer* tileRenderer = m_renderer->tileRenderer();
    if (!compositor || !tileRenderer) {
        return nullptr;
    }

    // A throwaway clone: the object's own content must never be modified by a
    // filter — that is the difference between a smart filter and Apply Effects.
    auto effected = cloneTileGrid(sourceGrid);
    if (!effected || effected->empty()) {
        return nullptr;
    }

    std::vector<TileKey> touched;
    makeCurrent();
    const bool baked = compositor->bakeEffectsIntoGrid(
        *effected, contentEffects, tileRenderer, /*beforeTileWrite=*/nullptr, touched);
    // Cross-batch effect caches are keyed by grid address, and this grid is a
    // fresh allocation that may land on a freed one's address.
    compositor->dropWholeLayerCacheEntry(effected.get());
    // Projection sources stay CPU-backed, like the projected grids themselves.
    for (auto& [key, tile] : effected->tiles()) {
        if (tile.hasTexture()) {
            tileRenderer->destroyTileTexture(tile);
        }
    }
    doneCurrent();

    if (!baked) {
        return nullptr;
    }
    m_smartContentEffectProjections.insert(layer->id);
    return effected;
}

std::shared_ptr<TileGrid> OpenGLCanvasWidget::buildSmartProjectedGrid(
    const ruwa::core::layers::LayerData* layer)
{
    if (!layer || !layer->isIsolatedPixelLayer()) {
        return nullptr;
    }
    const TileGrid* sourceGrid = layer->smartGrid();
    if (!sourceGrid) {
        return nullptr;
    }

    // Content-space smart filters run HERE, on the content, before the placement
    // — that is what makes them rotate, scale and deform with the object instead
    // of sitting in document space on top of it. The result becomes the source of
    // the projection; everything downstream (compositor, masks, thumbnails) sees
    // it as the object's pixels, and LayerData::documentSpaceEffects keeps the
    // compositor from running the same filters a second time.
    //
    // The placement box stays anchored to the RAW content bounds, the way every
    // other measurement of the layer is: a bounds-expanding filter (blur, shadow)
    // adds pixels around the content and they travel with it, rather than
    // re-fitting the object every time such a filter's radius changes.
    std::shared_ptr<TileGrid> contentEffected = buildContentSpaceEffectedGrid(layer);
    if (contentEffected) {
        sourceGrid = contentEffected.get();
    }
    TileGrid* sourceGridMutable = const_cast<TileGrid*>(sourceGrid);

    // Re-anchored to the CURRENT content bounds, exactly like hit-testing, the
    // transform overlay and the row preview already do
    // (transformStateWithSourceBounds keeps the placement identical while moving
    // the pivot). It matters now that contents can be edited: their bounds change
    // without the placement being refitted, and a projection built against the
    // stored bounds would then disagree with everything that measures the layer —
    // visibly so for a quad/deform placement, whose uv mapping IS the bounds.
    const TransformState projectionState = aether::transformStateWithSourceBounds(
        layer->smartTransform, layer->smartContentBounds());

    // Identity transform can be rendered from source grid directly — unless the
    // content-space chain already produced different pixels, which are then the
    // whole point of having a projection at all.
    if (projectionState.isIdentity()) {
        return contentEffected;
    }

    // Prefer GPU transform for smart-layer projection rebuild when available.
    //
    // NOT while a transform readback is still in flight: GLTransformRenderer owns a
    // single source atlas and a single readback PBO, so building a projection here
    // would overwrite both before tryFinalizeTransform() gets to read them, and the
    // pending transform would collect THIS layer's content instead of its own tiles.
    // That is reachable from a mask transform on a smart layer, whose commit
    // notifies the layer (-> projection rebuild) while its own readback is pending.
    // Flushing the pending transform instead is not an option here: it would push
    // the TransformCommand ahead of the copy-move add command that the commit path
    // pushes right after the notification, inverting the undo stack.
    const bool canUseGpu = m_initialized && m_renderer && m_renderer->transformRenderer()
        && m_renderer->tileRenderer() && !sourceGrid->empty() && !m_pendingTransform.active;
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
        if (contentEffected) {
            // Same rule for the throwaway effected source: uploadDirtyTiles just
            // gave its tiles textures, and nothing else will ever free them.
            for (auto& [key, tile] : contentEffected->tiles()) {
                if (tile.hasTexture()) {
                    tileRenderer->destroyTileTexture(tile);
                }
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
    m_layerHadBoundsEffect.clear();
    if (!m_layerModel) {
        m_smartProjectedGrids.clear();
        m_smartContentEffectProjections.clear();
        return;
    }

    QSet<QUuid> aliveIsolatedIds;

    m_layerModel->forEach([this, &aliveIsolatedIds](ruwa::core::layers::LayerData* layer) {
        if (!layer) {
            return;
        }

        const bool hasBoundsEffect
            = ruwa::core::effects::EffectCoverageResolver::neighborhoodPadPixels(layer->effects)
            > 0;
        m_layerHadBoundsEffect.insert(layer->id, hasBoundsEffect);

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
    if (!layer || !pixelEditsRequireRasterization(layer)) {
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

namespace {

/**
 * Move every field describing HOW a layer stores its content out of the layer,
 * leaving it ready to receive a different representation. What comes back is
 * exactly what LayerContentSwapCommand swaps back on undo, so a conversion only
 * has to install the new representation — the old one is already parked.
 */
LayerContentState takeLayerContentState(ruwa::core::layers::LayerData* layer)
{
    LayerContentState state;
    state.type = layer->type;
    state.tileGrid = std::move(layer->tileGrid);
    state.smartContent = std::move(layer->smartContent);
    state.smartTransform = layer->smartTransform;
    state.textData = std::move(layer->textData);
    state.runtimeVisualBackend = layer->runtimeVisualBackend;
    state.runtimeRetainedPayload = std::move(layer->runtimeRetainedPayload);
    state.runtimeRetainedPayloadKey = std::move(layer->runtimeRetainedPayloadKey);
    return state;
}

/**
 * Re-fit a placement onto content of a different size.
 *
 * The layer's placement box is what the user arranged, so the new content is
 * fitted into it rather than the other way round (this is what Photoshop's
 * Replace Contents does). For a free quad or a deform mesh the box is already
 * expressed in document space and the content is addressed through normalized
 * coordinates, so writing the new bounds is the whole job. For the affine case
 * the scale absorbs the size ratio and the translation is rebuilt so the
 * content's centre stays where it was.
 */
void refitTransformToContentBounds(TransformState& transform, const Rect& newBounds)
{
    const Rect oldBounds = transform.contentBounds;
    if (newBounds.width <= 0.0f || newBounds.height <= 0.0f) {
        transform.contentBounds = newBounds;
        return;
    }

    if (transform.hasFreeQuad() || transform.hasDeformMesh()) {
        transform.contentBounds = newBounds;
        return;
    }

    if (oldBounds.width <= 0.0f || oldBounds.height <= 0.0f) {
        transform.contentBounds = newBounds;
        transform.pivot = newBounds.center();
        return;
    }

    const Vector2 oldCentreInDoc = transform.transformPoint(oldBounds.center());
    transform.scale.x *= oldBounds.width / newBounds.width;
    transform.scale.y *= oldBounds.height / newBounds.height;
    transform.contentBounds = newBounds;
    transform.pivot = newBounds.center();
    // With the pivot at the content centre, the centre maps to pivot + translation.
    transform.translation
        = { oldCentreInDoc.x - transform.pivot.x, oldCentreInDoc.y - transform.pivot.y };
}

} // namespace

void OpenGLCanvasWidget::rasterizeSmartLayer(ruwa::core::layers::LayerData* layer)
{
    if (!layer || !layerRequiresRasterizationForPixelEdits(layer))
        return;

    std::unique_ptr<TileGrid> rasterGrid;

    if (layer->isText()) {
        rasterGrid = rasterizeTextLayerToGrid(layer);
    } else if (auto projected = buildSmartProjectedGrid(layer); projected) {
        rasterGrid = std::make_unique<TileGrid>(std::move(*projected));
    } else if (auto clonedSource = cloneTileGrid(layer->smartGrid()); clonedSource) {
        rasterGrid = std::make_unique<TileGrid>(std::move(*clonedSource));
    } else {
        rasterGrid = std::make_unique<TileGrid>();
    }

    LayerContentState replacedState = takeLayerContentState(layer);

    layer->type = ruwa::core::layers::LayerType::Raster;
    layer->tileGrid = std::move(rasterGrid);
    layer->smartTransform.reset();
    layer->runtimeRetainedPayloadKey.clear();
    layer->runtimeVisualBackend = LayerVisualBackend::RasterTiles;

    finishLayerContentSwap(layer, std::move(replacedState), QStringLiteral("Rasterize Layer"));
}

bool OpenGLCanvasWidget::convertLayerToSmartObject(ruwa::core::layers::LayerData* layer)
{
    if (!layer || !layer->canConvertToSmartObject()) {
        return false;
    }

    // A group carries a whole layer stack, so it becomes a smart object with a
    // nested DOCUMENT rather than a flattened grid — the contents stay editable.
    if (layer->isGroup()) {
        return convertGroupToSmartObject(layer);
    }

    // Text is a model, not pixels: it too becomes a nested document, so the
    // glyphs stay editable instead of being baked.
    if (layer->isText() && layer->textData) {
        return convertTextToSmartObject(layer);
    }

    // Content space starts out identical to document space: the smart object
    // shows exactly the pixels the layer showed, placed by an identity
    // transform.
    std::unique_ptr<TileGrid> contentGrid;
    if (auto cloned = cloneTileGrid(layer->tileGrid.get()); cloned) {
        // The original grid belongs to the undo state, so the content takes a
        // copy. Rasterization pays the same price in the other direction.
        contentGrid = std::make_unique<TileGrid>(std::move(*cloned));
    }
    if (!contentGrid) {
        contentGrid = std::make_unique<TileGrid>();
    }

    LayerContentState replacedState = takeLayerContentState(layer);

    layer->type = ruwa::core::layers::LayerType::Smart;
    layer->smartContent
        = std::make_shared<ruwa::core::layers::SmartContent>(std::move(contentGrid));
    layer->smartTransform = TransformState();
    layer->smartTransform.contentBounds = layer->smartContentBounds();
    layer->smartTransform.pivot = layer->smartTransform.contentBounds.center();
    layer->runtimeRetainedPayloadKey.clear();
    layer->runtimeVisualBackend = LayerVisualBackend::RasterTiles;

    // The layer's effect chain and mask deliberately stay put: both are applied
    // to the layer's finished document-space grid, which is now the projected
    // content, so they keep meaning exactly what they meant before (Photoshop
    // likewise keeps effects as smart filters over the new object).

    finishLayerContentSwap(
        layer, std::move(replacedState), QStringLiteral("Convert to Smart Object"));
    return true;
}

bool OpenGLCanvasWidget::convertGroupToSmartObject(ruwa::core::layers::LayerData* layer)
{
    using ruwa::core::layers::LayerData;
    using ruwa::core::layers::LayerId;

    if (!layer || !layer->isGroup() || !m_layerModel) {
        return false;
    }

    // A Background cannot leave the model (removeLayers refuses it), and half a
    // group moved into an object is worse than no conversion at all.
    const std::function<bool(const LayerData*)> holdsBackground = [&](const LayerData* node) {
        if (!node) {
            return false;
        }
        if (node->isBackground()) {
            return true;
        }
        for (const auto& child : node->children) {
            if (holdsBackground(child.get())) {
                return true;
            }
        }
        return false;
    };
    for (const auto& child : layer->children) {
        if (holdsBackground(child.get())) {
            return false;
        }
    }

    // The children themselves BECOME the contents; the undo state gets clones,
    // exactly the way deleting layers does. So the two halves of the conversion
    // never share a layer object, and an undo cannot reach into the document.
    QList<std::shared_ptr<LayerData>> contentRoots;
    QList<std::shared_ptr<LayerData>> undoClones;
    QList<std::pair<LayerId, int>> restorePositions;
    QList<LayerId> childIds;
    for (int i = 0; i < layer->children.size(); ++i) {
        const auto& child = layer->children[i];
        if (!child) {
            continue;
        }
        auto clone = ruwa::core::layers::LayerModel::cloneLayerTree(child.get(),
            /*preserveIds=*/true);
        if (!clone) {
            continue;
        }
        contentRoots.append(child);
        undoClones.append(std::move(clone));
        restorePositions.append({ layer->id, i });
        childIds.append(child->id);
    }

    // Content space starts out identical to document space (identity placement),
    // so the nested canvas is this canvas and the contents keep the document's
    // authoring precision.
    auto document = std::make_shared<ruwa::core::layers::SmartDocument>();
    document->size = QSize(static_cast<int>(m_canvas.width()), static_cast<int>(m_canvas.height()));
    document->format = m_layerModel->documentTileFormat();

    // One undo step: a redo that replayed the content swap without the removal
    // would leave the children in the model AND inside the object.
    auto& undoManager = m_canvas.undoManager();
    undoManager.beginTransaction(QStringLiteral("Convert to Smart Object"));

    if (!childIds.isEmpty()) {
        m_layerModel->removeLayers(childIds);
        // Detached from the tree by hand: the layers live on in `contentRoots`,
        // and a document root with a parent still pointing at the (now smart)
        // layer would be walked as if it were nested.
        for (const auto& root : contentRoots) {
            root->parent = nullptr;
            root->depth = 0;
            root->updateChildrenDepth();
        }
        undoManager.push(std::make_unique<aether::LayerRemoveCommand>(
            m_layerModel, std::move(undoClones), std::move(restorePositions),
            [this]() { requestRender(); }, [this]() { notifyCanvasInteraction(true); }));
    }
    document->roots = std::move(contentRoots);

    LayerContentState replacedState = takeLayerContentState(layer);

    layer->type = ruwa::core::layers::LayerType::Smart;
    layer->smartContent = std::make_shared<ruwa::core::layers::SmartContent>();
    // No composite yet, so the cache is declared stale on purpose: the flatten
    // below (or the deferred sweep, if GL is not up) is what produces the pixels.
    layer->smartContent->setDocument(document);
    layer->smartTransform = TransformState();
    layer->runtimeRetainedPayloadKey.clear();
    layer->runtimeVisualBackend = LayerVisualBackend::RasterTiles;

    // The group's own mask and effect chain stay on the layer, as they do for a
    // raster conversion: they were applied to the group's finished result, which
    // is exactly what the smart object now shows. What the conversion does change
    // is pass-through blending — a flattened object is isolated by nature, the
    // same trade Photoshop makes.

    // Flatten now so the object has pixels before anything asks for them. A
    // refusal (no GL context yet) is not a failure: the content is stale and the
    // deferred sweep composites it on the first frame that has a renderer.
    recompositeSmartContent(layer->smartContent);
    layer->smartTransform.contentBounds = layer->smartContentBounds();
    layer->smartTransform.pivot = layer->smartTransform.contentBounds.center();

    finishLayerContentSwap(
        layer, std::move(replacedState), QStringLiteral("Convert to Smart Object"));

    undoManager.endTransaction();
    return true;
}

bool OpenGLCanvasWidget::convertTextToSmartObject(ruwa::core::layers::LayerData* layer)
{
    using ruwa::core::layers::LayerData;

    if (!layer || !layer->isText() || !layer->textData) {
        return false;
    }

    // Where the glyphs actually land in document space. Read off the retained
    // payload — that is the geometry the compositor draws, so nothing can be
    // outside it — with the layout box put through the text transform as the
    // fallback for a layer whose payload cannot be built (no glyphs yet).
    Rect worldBounds {};
    if (ensureTextRetainedPayload(layer) && layer->runtimeRetainedPayload
        && !layer->runtimeRetainedPayload->empty()) {
        worldBounds = layer->runtimeRetainedPayload->worldBounds;
    } else {
        worldBounds = aether::transformStateWithSourceBounds(
            layer->textData->transform, computeTextLayoutSourceBounds(*layer->textData))
                          .transformedAABB();
    }

    // The nested canvas is the text's box, not "the document origin out to the
    // text": deriving it from the pixels alone (what a flat content does, via
    // SmartContent::contentSpaceSize) anchors content space at (0,0) and gives
    // text placed at 800,600 an 800×600 margin of nothing inside its own object.
    // The glyphs move to the content origin instead and the layer's placement
    // carries the offset back, so the object stays exactly where the text was.
    //
    // The margin is the same slack collectCompositeLayerKeys gives a transformed
    // layer: a glyph's antialiased edge (and an italic's overhang) can reach
    // just past the layout box it was measured from.
    constexpr float kGlyphMargin = 2.0f;
    const int left = static_cast<int>(std::floor(worldBounds.left() - kGlyphMargin));
    const int top = static_cast<int>(std::floor(worldBounds.top() - kGlyphMargin));
    const int right = static_cast<int>(std::ceil(worldBounds.right() + kGlyphMargin));
    const int bottom = static_cast<int>(std::ceil(worldBounds.bottom() + kGlyphMargin));
    const QSize documentSize(std::max(1, right - left), std::max(1, bottom - top));

    // The text MODEL is what moves inside, not a rasterization of it: the
    // contents tab opens on a real text layer. The original layer keeps its id
    // and becomes the smart object, so the nested one is a copy — and the layer's
    // own textData is parked in the undo state below, never shared with it.
    auto textChild = LayerData::create(ruwa::core::layers::LayerType::Text, layer->name);
    textChild->nameIsCustom = layer->nameIsCustom;
    textChild->textData = std::make_unique<ruwa::core::layers::TextLayerData>(*layer->textData);
    textChild->textData->transform.shiftForCanvasResize(left, top);

    auto document = std::make_shared<ruwa::core::layers::SmartDocument>();
    document->size = documentSize;
    document->format
        = m_layerModel ? m_layerModel->documentTileFormat() : aether::kDefaultTileFormat;
    document->roots.append(std::move(textChild));

    LayerContentState replacedState = takeLayerContentState(layer);

    layer->type = ruwa::core::layers::LayerType::Smart;
    layer->smartContent = std::make_shared<ruwa::core::layers::SmartContent>();
    // Stale on purpose, exactly as in the group conversion: the flatten below
    // (or the deferred sweep, when GL is not up yet) produces the pixels.
    layer->smartContent->setDocument(document);
    layer->smartTransform = TransformState();
    // Content point c lands at c + translation while scale is 1 and rotation 0,
    // which is precisely the offset the glyphs were moved by.
    layer->smartTransform.translation = { static_cast<float>(left), static_cast<float>(top) };
    layer->runtimeRetainedPayload.reset();
    layer->runtimeRetainedPayloadKey.clear();
    layer->runtimeVisualBackend = LayerVisualBackend::RasterTiles;

    // The layer's mask and effect chain stay put, as they do for every other
    // conversion: they were applied to the text's finished document-space
    // result, which is what the object now shows.

    recompositeSmartContent(layer->smartContent);
    // Re-fitting the placement box onto the composited pixels leaves the mapping
    // alone (the pivot cancels out at scale 1 / rotation 0) and gives the
    // transform handles something to hug.
    layer->smartTransform.contentBounds = layer->smartContentBounds();
    layer->smartTransform.pivot = layer->smartTransform.contentBounds.center();

    finishLayerContentSwap(
        layer, std::move(replacedState), QStringLiteral("Convert to Smart Object"));
    return true;
}

void OpenGLCanvasWidget::finishLayerContentSwap(
    ruwa::core::layers::LayerData* layer, LayerContentState replacedState, const QString& text)
{
    m_smartProjectedGrids.remove(layer->id);
    rebuildLayerProjectionCaches();
    m_canvas.dirtyManager().onStructureChanged();
    if (m_layerModel) {
        // A conversion hands the layer a different content (or none), which
        // changes how many layers share the old one — the remaining instances'
        // badges have to follow. This is a layer-data change, not a structural
        // one, so the model's own layersChanged refresh does not cover it.
        m_layerModel->refreshSmartInstanceCounts();
        m_layerModel->notifyLayerDataChanged(layer->id);
    }

    auto command = std::make_unique<LayerContentSwapCommand>(m_layerModel, layer->id,
        std::move(replacedState), text, [this](const ruwa::core::layers::LayerId& changedLayerId) {
            rebuildLayerProjectionCaches();
            m_canvas.dirtyManager().onStructureChanged();
            if (m_layerModel) {
                m_layerModel->refreshSmartInstanceCounts();
                m_layerModel->notifyLayerDataChanged(changedLayerId);
            }
        });
    m_canvas.undoManager().push(std::move(command));
    requestRender();
}

void OpenGLCanvasWidget::refreshLayersForSmartContent(const QUuid& contentId, bool refitPlacement)
{
    if (!m_layerModel || contentId.isNull()) {
        return;
    }

    QList<ruwa::core::layers::LayerId> affectedLayers;
    m_layerModel->forEach([&](ruwa::core::layers::LayerData* layer) {
        if (!layer || !layer->smartContent || layer->smartContent->contentId != contentId) {
            return;
        }
        // The placement is per-layer, so each instance re-fits the new content
        // into its own box; without this the layer keeps projecting through the
        // previous content's bounds. Editing the object's own contents is the
        // one case that must NOT refit — see the header.
        if (refitPlacement) {
            refitTransformToContentBounds(layer->smartTransform, layer->smartContentBounds());
        }
        layer->thumbnailDirty = true;
        m_smartProjectedGrids.remove(layer->id);
        affectedLayers.append(layer->id);
    });

    if (affectedLayers.isEmpty()) {
        return;
    }

    rebuildLayerProjectionCaches();
    m_canvas.dirtyManager().onStructureChanged();
    for (const ruwa::core::layers::LayerId& layerId : affectedLayers) {
        m_layerModel->notifyLayerDataChanged(layerId);
    }
    requestRender();
}

void OpenGLCanvasWidget::refreshSmartContentComposites()
{
    if (!m_layerModel) {
        return;
    }

    // Collect first: the sweep must not walk the model while the refresh below
    // rebuilds projections and emits model signals.
    QList<std::shared_ptr<ruwa::core::layers::SmartContent>> staleContents;
    QSet<QUuid> seenContents;
    m_layerModel->forEach([&](ruwa::core::layers::LayerData* layer) {
        if (!layer || !layer->smartContent || !layer->smartContent->document) {
            return;
        }
        const QUuid contentId = layer->smartContent->contentId;
        // Instances share one content; compositing it once is the whole point.
        if (layer->smartContent->compositeUpToDate() || seenContents.contains(contentId)) {
            return;
        }
        seenContents.insert(contentId);
        staleContents.append(layer->smartContent);
    });

    if (staleContents.isEmpty()) {
        m_smartCompositeRefreshPending = false;
        return;
    }

    bool anyStillStale = false;
    for (const auto& content : staleContents) {
        recompositeSmartContent(content);
        // Asked per content rather than inferred from the return value: a
        // composite that was refused for good (a nesting cycle, a driver that
        // produced nothing) settles the cache itself, and only what is genuinely
        // left stale should bring the sweep back.
        anyStillStale = anyStillStale || !content->compositeUpToDate();
    }

    m_smartCompositeRefreshPending = anyStillStale;
}

bool OpenGLCanvasWidget::recompositeSmartContent(
    const std::shared_ptr<ruwa::core::layers::SmartContent>& content)
{
    if (!content || !content->document || content->compositeUpToDate()) {
        return false;
    }
    if (!m_initialized || !m_renderer || !m_layerCompositingBuilder) {
        // No GL yet: the content keeps its last valid pixels and the sweep is
        // re-armed for the first frame that has a renderer.
        m_smartCompositeRefreshPending = true;
        return false;
    }

    makeCurrent();
    SmartContentCompositor compositor(m_renderer.get(), m_layerCompositingBuilder.get());
    const bool recomposited = compositor.ensureComposite(*content);
    doneCurrent();

    if (recomposited) {
        // Editing an object's contents must not move the object: the placement
        // stays exactly as the user left it, only the pixels behind it change.
        refreshLayersForSmartContent(content->contentId, /*refitPlacement=*/false);
    }
    return recomposited;
}

bool OpenGLCanvasWidget::applySmartContentDocument(
    const QUuid& contentId, std::shared_ptr<ruwa::core::layers::SmartDocument> document)
{
    if (!m_layerModel || contentId.isNull() || !document) {
        return false;
    }

    std::shared_ptr<ruwa::core::layers::SmartContent> content;
    m_layerModel->forEach([&](ruwa::core::layers::LayerData* layer) {
        if (content || !layer || !layer->smartContent
            || layer->smartContent->contentId != contentId) {
            return;
        }
        content = layer->smartContent;
    });
    if (!content) {
        // Nothing in this document shows those contents any more (the layer was
        // deleted while its contents tab was open). There is nothing to commit
        // into; the session reconcile is already closing that tab.
        return false;
    }

    notifyCanvasInteraction(true);

    // The old composite is COPIED for the undo state rather than moved out: the
    // content must keep valid pixels the whole way through, because the
    // recomposite below can decline (no GPU) and the commit then has to unwind
    // to exactly what was there before.
    SmartContentState replacedState;
    replacedState.grid = content->grid ? aether::cloneGridWithSolids(*content->grid)
                                       : std::make_unique<TileGrid>();
    replacedState.document = content->document;
    replacedState.compositeRevision = content->compositeRevision;
    replacedState.sourcePath = content->sourcePath;
    replacedState.sourceKind = content->sourceKind;
    replacedState.sourceHash = content->sourceHash;

    auto previousDocument = content->document;
    const quint64 previousCompositeRevision = content->compositeRevision;

    content->setDocument(std::move(document));
    if (!recompositeSmartContent(content)) {
        // The pixels could not be produced, so there is nothing to show for this
        // commit — put the previous contents back instead of leaving the object
        // pointing at layers its pixels do not match. The caller reports the
        // failure and the editing tab stays open with its edits intact.
        content->adoptDocument(std::move(previousDocument), previousCompositeRevision);
        return false;
    }

    // Untranslated like every other undo label here (see "Rasterize Layer" /
    // "Replace Contents"): the undo stack is not localized in this codebase.
    auto command = std::make_unique<SmartContentSwapCommand>(content, std::move(replacedState),
        QStringLiteral("Edit Contents"), [this](const QUuid& changedContentId) {
            // Undo/redo swaps back a composite that already matches its document,
            // so only the placement-preserving re-projection is needed.
            refreshLayersForSmartContent(changedContentId, /*refitPlacement=*/false);
        });
    m_canvas.undoManager().push(std::move(command));
    requestRender();
    return true;
}

bool OpenGLCanvasWidget::confirmRasterizeForSelectionTransform(
    ruwa::core::layers::LayerData* layer, bool hasSelection)
{
    if (!layer || !pixelEditsRequireRasterization(layer) || !hasSelection) {
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
    if (!layer || !pixelEditsRequireRasterization(layer) || !hasSelection) {
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
        if (pixelEditsRequireRasterization(layer)) {
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
        if (pixelEditsRequireRasterization(layer)) {
            rasterizeSmartLayer(layer);
            rasterizedAny = true;
        }
    }

    if (rasterizedAny) {
        m_transformTargetSet
            = buildTransformTargetSet(*m_layerModel, aether::transformBoundsForLayer);
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

void OpenGLCanvasWidget::beginStroke(float worldX, float worldY, float pressure,
    BrushStrokeHost::StrokeInputDevice inputDevice, bool axisConstraint)
{
    if (m_transformController.isActive())
        return;
    if (!ensurePaintableActiveLayer())
        return;

    auto* layer = activeLayer();
    if (!isLayerCanvasEditable(layer))
        return;

    if (m_strokeHost) {
        m_strokeHost->beginStroke(worldX, worldY, pressure, inputDevice, axisConstraint);
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

void OpenGLCanvasWidget::queueStrokeAtElapsed(float worldX, float worldY, float pressure,
    float strokeElapsedSeconds, BrushStrokeHost::StrokeInputDevice inputDevice)
{
    if (m_strokeHost) {
        m_strokeHost->queueStrokeAtElapsed(
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

    FloodFillResult result = fillMaskTiles(*layer->tileGrid, screenMaskTiles, fillR, fillG, fillB,
        fillA, selectionMask, layer->alphaLock);

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

    // The polygon is handed to the preview OPEN and UNCLIPPED. Closing it and
    // clipping it to the canvas used to happen here, per added point: both
    // renumber the vertices, which is exactly what an incrementally accumulated
    // mask cannot survive. The mask closes the polygon implicitly (its parity fan
    // is pivoted on the first point) and clips to the canvas with a stencil gate,
    // so the shape is unchanged while the work per point stays O(1).
    const Rect bounds = retainedPolygonBounds(m_lassoFillPoints);
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
    // Points are only ever appended within a stroke, so the count is a complete
    // change test — and unlike comparing the polygons it does not grow with it.
    const bool polygonChanged
        = !m_lassoFillPreview.active || m_lassoFillPreview.pointCount != m_lassoFillPoints.size();

    if (!(targetChanged || colorChanged || polygonChanged)) {
        return;
    }

    m_lassoFillPreview.active = true;
    m_lassoFillPreview.targetLayerId = layer->id;
    m_lassoFillPreview.revision += 1;
    m_lassoFillPreview.bounds = bounds;
    m_lassoFillPreview.pointCount = m_lassoFillPoints.size();
    m_lassoFillPreview.color = previewColor;

    auto& session = m_lassoFillViewportPreview;
    session.active = true;
    session.targetLayerId = layer->id;
    if (session.polygonWorld.size() > m_lassoFillPoints.size()) {
        // Not a continuation of what the session holds — start over.
        session.polygonWorld.clear();
        session.polygonScreen.clear();
        session.screenBoundsValid = false;
    }
    session.polygonWorld.insert(session.polygonWorld.end(),
        m_lassoFillPoints.begin() + static_cast<std::ptrdiff_t>(session.polygonWorld.size()),
        m_lassoFillPoints.end());
    if (targetChanged) {
        session.screenSourcesDirty = true;
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
    m_lassoFillPreview.pointCount = 0;
    m_lassoFillPreview.color = {};
    m_lassoFillViewportPreview = {};
    // The mask accumulates in a stencil buffer across frames. Dropping the
    // session is not enough: a new stroke that happened to start at the very same
    // point would otherwise be taken for a continuation of this one. Touches no
    // GL object, so it is safe from the non-paint callers of this function.
    if (m_renderer) {
        if (auto* maskRenderer = m_renderer->lassoMaskRenderer()) {
            maskRenderer->invalidateAccumulation();
        }
    }
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

bool OpenGLCanvasWidget::performMagicWandSelection(
    int worldX, int worldY, bool addSelection, bool subtractSelection)
{
    if (!m_selectionController) {
        return false;
    }

    const quint64 requestSequence = ++m_magicWandRequestSequence;

    SelectionState before;
    before.layer = captureLayerSelection(m_layerModel ? m_layerModel->selectionManager() : nullptr);
    before.lasso = captureLassoSelection(&m_selectionController->lassoSelection(),
        effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());

    auto request = m_selectionController->prepareMagicWandSelection(
        worldX, worldY, addSelection, subtractSelection);
    if (!request) {
        return false;
    }

    const LassoSelectionMode mode = request->mode;
    const uint32_t canvasWidth = request->canvasWidth;
    const uint32_t canvasHeight = request->canvasHeight;
    const QUuid sourceLayerId = request->sourceLayerId;
    auto* watcher = new QFutureWatcher<MaskTileSnapshot>(this);
    connect(watcher, &QFutureWatcher<MaskTileSnapshot>::finished, this,
        [this, watcher, requestSequence, before = std::move(before), mode, canvasWidth,
            canvasHeight, sourceLayerId]() mutable {
            QFuture<MaskTileSnapshot> future = watcher->future();
            MaskTileSnapshot wandMask = future.takeResult();
            watcher->deleteLater();

            if (requestSequence != m_magicWandRequestSequence || !m_selectionController) {
                return;
            }
            const auto* currentLayer = activeLayer();
            if (!currentLayer || currentLayer->id != sourceLayerId) {
                return;
            }

            SelectionState current;
            current.layer
                = captureLayerSelection(m_layerModel ? m_layerModel->selectionManager() : nullptr);
            current.lasso = captureLassoSelection(&m_selectionController->lassoSelection(),
                effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
            if (!selectionStateMatches(current, before)) {
                return;
            }

            if (!m_selectionController->applyMagicWandSelection(
                    wandMask, mode, canvasWidth, canvasHeight)) {
                return;
            }

            SelectionState after;
            after.layer = before.layer;
            after.lasso = captureLassoSelection(&m_selectionController->lassoSelection(),
                effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
            m_ignoreSelectionChange = true;
            pushSelectionCommand(before, after);
            m_ignoreSelectionChange = false;
        });

    watcher->setFuture(QtConcurrent::run([request = std::move(*request)]() mutable {
        return CanvasSelectionController::computeMagicWandSelection(std::move(request));
    }));
    return true;
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

bool OpenGLCanvasWidget::selectAll()
{
    if (!m_selectionController) {
        return false;
    }
    SelectionState before;
    before.layer = captureLayerSelection(m_layerModel ? m_layerModel->selectionManager() : nullptr);
    before.lasso = captureLassoSelection(&m_selectionController->lassoSelection(),
        effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
    if (!m_selectionController->selectAll()) {
        return false;
    }
    SelectionState after;
    after.layer = before.layer;
    after.lasso = captureLassoSelection(&m_selectionController->lassoSelection(),
        effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
    m_ignoreSelectionChange = true;
    pushSelectionCommand(before, after);
    m_ignoreSelectionChange = false;
    return true;
}

bool OpenGLCanvasWidget::invertSelection()
{
    if (!m_selectionController) {
        return false;
    }
    SelectionState before;
    before.layer = captureLayerSelection(m_layerModel ? m_layerModel->selectionManager() : nullptr);
    before.lasso = captureLassoSelection(&m_selectionController->lassoSelection(),
        effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
    if (!m_selectionController->invertSelection()) {
        return false;
    }
    SelectionState after;
    after.layer = before.layer;
    after.lasso = captureLassoSelection(&m_selectionController->lassoSelection(),
        effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
    m_ignoreSelectionChange = true;
    pushSelectionCommand(before, after);
    m_ignoreSelectionChange = false;
    return true;
}

bool OpenGLCanvasWidget::canReselect() const
{
    if (!m_selectionController || m_reselectState.isEmpty() || !m_reselectState.maskTiles) {
        return false;
    }
    // A resize remaps the undo stack but not this snapshot, so a mask captured
    // against different document bounds is dropped rather than pasted askew.
    return m_reselectState.canvasWidth == effectiveDocumentBoundsWidth()
        && m_reselectState.canvasHeight == effectiveDocumentBoundsHeight();
}

bool OpenGLCanvasWidget::reselect()
{
    if (!canReselect()) {
        return false;
    }
    SelectionState before;
    before.layer = captureLayerSelection(m_layerModel ? m_layerModel->selectionManager() : nullptr);
    before.lasso = captureLassoSelection(&m_selectionController->lassoSelection(),
        effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
    if (!m_selectionController->applyRestoredSelectionMask(m_reselectState.maskTiles,
            m_reselectState.regions, m_reselectState.maskHasSoftAlpha, m_reselectState.canvasWidth,
            m_reselectState.canvasHeight)) {
        return false;
    }
    SelectionState after;
    after.layer = before.layer;
    after.lasso = captureLassoSelection(&m_selectionController->lassoSelection(),
        effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
    m_ignoreSelectionChange = true;
    pushSelectionCommand(before, after);
    m_ignoreSelectionChange = false;
    return true;
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

void OpenGLCanvasWidget::selectActiveLayerMask()
{
    if (m_selectionController) {
        SelectionState before;
        before.layer
            = captureLayerSelection(m_layerModel ? m_layerModel->selectionManager() : nullptr);
        before.lasso = captureLassoSelection(&m_selectionController->lassoSelection(),
            effectiveDocumentBoundsWidth(), effectiveDocumentBoundsHeight());
        m_selectionController->selectActiveLayerMask();
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
    // Filling the MASK works on any mask-capable layer (the mask is a plain grid);
    // filling the pixels still requires a raster layer.
    if (!isLayerCanvasEditable(layer) || (!layer->isRaster() && !layer->maskIsEditTarget()))
        return false;
    const bool maskTarget = layer->maskIsEditTarget();
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
    // Straight (unpremultiplied) fill color for the alpha-locked path, which
    // recolors existing pixels instead of compositing new coverage on top.
    const float fillRF = static_cast<float>(fillR) / 255.0f;
    const float fillGF = static_cast<float>(fillG) / 255.0f;
    const float fillBF = static_cast<float>(fillB) / 255.0f;
    // Alpha lock applies to the layer's pixels only; a mask target has none.
    // (A mask's alpha is its own painted coverage, not a silhouette.)
    const bool preserveDestinationAlpha = !maskTarget && layer->alphaLock;
    // Partial selection coverage is not a plain opacity multiplier: it is the
    // fill's per-pixel alpha CEILING, and the source strength depends on what
    // is underneath (FloodFill.h :: fillSelectionSourceStrength /
    // fillSelectionAlphaCeiling, the same rule the fill tools and the brush
    // commit use). Multiplying the fill by coverage on its own is what made a
    // fill through a content-derived selection stack a second layer of alpha
    // over the very pixels whose coverage defined that selection. Fully covered
    // pixels are untouched by all of this, so hard selections keep plain
    // src-over.
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
                if (!dstTile && (fillA == 0 || preserveDestinationAlpha))
                    continue;
                if (!dstTile) {
                    dstTile = &targetGrid->getOrCreateTile(key);
                    snapshot.createdTiles.insert(key);
                }

                float d[4];
                aether::readTilePixelF(*dstTile, localX, localY, d);
                if (preserveDestinationAlpha && d[3] <= 0.0f)
                    continue;

                // Source strength = min(1, coverage / destination alpha), see
                // FloodFill.h :: fillSelectionSourceStrength. Where the
                // destination is denser than the selection, coverage describes
                // an EDGE across existing content and scales the fill down so
                // the boundary blends; where it is not (an empty pixel, or a
                // content selection tracing this layer's own soft alpha) the
                // fill goes down at full strength and the ceiling below trims
                // it back to the selected coverage. Deliberately a ratio and
                // not a branch on `d[3] > cov`: the mask is a quantized copy of
                // the alpha it traces, so the two cross repeatedly along a soft
                // gradient and a branch posterizes the fill into contour bands.
                float strength = 1.0f;
                if (preserveDestinationAlpha) {
                    // Alpha lock keeps the silhouette, so coverage is free to
                    // act as a plain opacity on the recolor (matching
                    // FloodFill.cpp's alpha-locked fill).
                    strength = cov;
                } else if (d[3] > cov && d[3] > 0.0f) {
                    strength = std::min(1.0f, cov / d[3]);
                }
                const float srcA = fillAF * strength;
                // Ceiling: the fill may pull alpha from where it is towards the
                // coverage, by its own alpha, and only upwards. With an opaque
                // fill that is max(cov, d), and with coverage 1 it is exactly
                // the plain src-over alpha, so the clamp cannot fire on a hard
                // selection. See FloodFill.h :: fillSelectionAlphaCeiling.
                const float alphaCeiling = d[3] + fillAF * std::max(0.0f, cov - d[3]);

                if (!capturedBefore && hadTile) {
                    auto& before = snapshot.beforeTiles[key];
                    before.resize(contentTileBytes);
                    std::memcpy(before.data(), dstTile->pixels(), contentTileBytes);
                    capturedBefore = true;
                }

                const float inv = 1.0f - srcA;
                float out[4];
                if (preserveDestinationAlpha) {
                    // Alpha lock: recolor what is already there, leave the
                    // silhouette untouched. Premultiplied storage, so the mixed
                    // straight color is re-multiplied by the kept alpha.
                    out[0] = fillRF * srcA * d[3] + d[0] * inv;
                    out[1] = fillGF * srcA * d[3] + d[1] * inv;
                    out[2] = fillBF * srcA * d[3] + d[2] * inv;
                    out[3] = d[3];
                } else {
                    out[0] = fillPRF * strength + d[0] * inv;
                    out[1] = fillPGF * strength + d[1] * inv;
                    out[2] = fillPBF * strength + d[2] * inv;
                    out[3] = srcA + d[3] * inv;
                    if (out[3] > alphaCeiling + kFillEps) {
                        // Clamp to the coverage ceiling, scaling RGB so the
                        // visible color stays stable under premultiplied storage.
                        const float scale = (out[3] > 0.0f) ? (alphaCeiling / out[3]) : 0.0f;
                        out[0] *= scale;
                        out[1] *= scale;
                        out[2] *= scale;
                        out[3] = alphaCeiling;
                    }
                }

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

    // writeTilePixelF() dirties TileData, but existing tiles are not added to
    // TileGrid::dirtyTiles() by that per-tile flag alone. Register every changed
    // key with the grid so the renderer uploads pre-existing tiles too and
    // whole-grid caches observe the new content version.
    for (const TileKey& key : dirtyKeys) {
        targetGrid->markDirty(key);
    }

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

bool OpenGLCanvasWidget::copySelectionPixelsToClipboard(QImage* outFlattenedImage)
{
    if (!m_selectionController || !hasSelectionMask())
        return false;
    // Reading the layer's own tiles means only plain raster layers qualify: a
    // generated pixel layer (smart/board/text) would need its transform baked in
    // first, which a copy has no business doing.
    auto* layer = activeLayer();
    if (!layer || !layer->isRaster() || !layer->tileGrid || layer->tileGrid->empty())
        return false;

    // Content pixels, even when the mask is the active paint target: what paste
    // produces is a layer, not a mask.
    const auto& sourceGrid = *layer->tileGrid;
    const auto& selectionMask = m_selectionController->lassoSelection().mask();
    const bool clipToCanvas = hasFiniteDocumentBounds();
    const int canvasW = static_cast<int>(m_canvas.width());
    const int canvasH = static_cast<int>(m_canvas.height());

    auto copied = std::make_shared<TileGrid>();
    copied->setFormat(sourceGrid.format());

    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();
    int maxY = std::numeric_limits<int>::min();

    for (const auto& [key, maskTile] : selectionMask.tiles()) {
        const TileData* srcTile = sourceGrid.getTile(key);
        if (!srcTile)
            continue;

        TileData* dstTile = nullptr;
        for (uint32_t localY = 0; localY < TILE_SIZE; ++localY) {
            const int worldY = key.y * static_cast<int>(TILE_SIZE) + static_cast<int>(localY);
            if (clipToCanvas && (worldY < 0 || worldY >= canvasH))
                continue;
            for (uint32_t localX = 0; localX < TILE_SIZE; ++localX) {
                const int worldX = key.x * static_cast<int>(TILE_SIZE) + static_cast<int>(localX);
                if (clipToCanvas && (worldX < 0 || worldX >= canvasW))
                    continue;

                float coverage[4];
                aether::readTilePixelF(maskTile, localX, localY, coverage);
                if (coverage[3] <= 0.0f)
                    continue;

                float src[4];
                aether::readTilePixelF(*srcTile, localX, localY, src);
                if (src[3] <= 0.0f && src[0] <= 0.0f && src[1] <= 0.0f && src[2] <= 0.0f)
                    continue;

                // Premultiplied throughout, so partial coverage is a plain scale.
                const float out[4] = { src[0] * coverage[3], src[1] * coverage[3],
                    src[2] * coverage[3], src[3] * coverage[3] };
                if (out[3] <= 0.0f)
                    continue;

                if (!dstTile)
                    dstTile = &copied->getOrCreateTile(key);
                aether::writeTilePixelF(*dstTile, localX, localY, out);

                minX = std::min(minX, worldX);
                minY = std::min(minY, worldY);
                maxX = std::max(maxX, worldX);
                maxY = std::max(maxY, worldY);
            }
        }

        if (dstTile)
            copied->markDirty(key);
    }

    copied->pruneEmpty();
    if (copied->empty() || minX > maxX || minY > maxY)
        return false;

    const QRect bounds(QPoint(minX, minY), QPoint(maxX, maxY));
    if (outFlattenedImage)
        *outFlattenedImage = imageFromTileGridRegion(*copied, bounds);

    ruwa::shared::clipboard::EditClipboard::instance().setPixels(std::move(copied), bounds);
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

bool OpenGLCanvasWidget::convertLayerToSmartObjectById(const QUuid& layerId)
{
    if (!m_layerModel || layerId.isNull()) {
        return false;
    }
    auto* layer = m_layerModel->layerById(layerId);
    if (!layer || !layer->canConvertToSmartObject()) {
        return false;
    }

    notifyCanvasInteraction(true);
    return convertLayerToSmartObject(layer);
}

bool OpenGLCanvasWidget::replaceSmartLayerContents(const QUuid& layerId,
    std::unique_ptr<TileGrid> contentGrid, const QString& sourcePath, const QByteArray& sourceHash)
{
    if (!m_layerModel || layerId.isNull() || !contentGrid) {
        return false;
    }
    auto* layer = m_layerModel->layerById(layerId);
    if (!layer || !layer->isSmart()) {
        return false;
    }

    // Deliberately NOT LayerData::setSmartGrid(): that detaches a shared content
    // first, because installing pixels into one layer must not reach through to
    // its instances. Replacing an object's CONTENTS is the opposite statement —
    // the object itself is now something else — so this writes through the
    // shared content and every instance follows.
    auto content = layer->smartContent;
    if (!content) {
        layer->ensureSmartContent();
        content = layer->smartContent;
    }

    notifyCanvasInteraction(true);

    SmartContentState replacedState;
    replacedState.grid = std::move(content->grid);
    // The nested document (if this object had one) is parked with the pixels it
    // produced: the new contents replace the layers too, and setGrid() below is
    // what drops them. Undo hands both halves back together.
    replacedState.document = content->document;
    replacedState.compositeRevision = content->compositeRevision;
    replacedState.sourcePath = content->sourcePath;
    replacedState.sourceKind = content->sourceKind;
    replacedState.sourceHash = content->sourceHash;

    content->setGrid(std::move(contentGrid));
    content->sourcePath = sourcePath;
    // Still Embedded: the pixels live in the document and a .rwf stays openable
    // without the source file. The path only says where they can be refreshed
    // from, which is exactly what a later Place Linked will reuse.
    content->sourceKind = ruwa::core::layers::SmartSourceKind::Embedded;
    content->sourceHash = sourceHash;

    refreshLayersForSmartContent(content->contentId);

    auto command = std::make_unique<SmartContentSwapCommand>(std::move(content),
        std::move(replacedState), QStringLiteral("Replace Contents"),
        [this](const QUuid& changedContentId) { refreshLayersForSmartContent(changedContentId); });
    m_canvas.undoManager().push(std::move(command));
    requestRender();
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
    // Type-agnostic: inverting only rewrites the mask grid, which every
    // mask-capable layer (raster or smart) carries in the same form.
    if (!layer || !layer->hasMask() || !layer->canHostMask()) {
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

bool OpenGLCanvasWidget::canFlipActiveTransform() const
{
    return m_transformController.isActive() && !m_transformController.isDragging()
        && !m_moveOnlyTransform && !m_autoApplyingTransform
        && m_transformController.canAnimateFlip();
}

bool OpenGLCanvasWidget::flipActiveTransformHorizontally()
{
    return flipActiveTransform(true, false);
}

bool OpenGLCanvasWidget::flipActiveTransformVertically()
{
    return flipActiveTransform(false, true);
}

bool OpenGLCanvasWidget::flipActiveTransform(bool flipHorizontal, bool flipVertical)
{
    if ((!flipHorizontal && !flipVertical) || !canFlipActiveTransform()) {
        return false;
    }

    const TransformState before = m_transformController.state();
    const TransformInteractionMode mode = m_transformController.interactionMode();

    bool started = false;
    if (flipHorizontal) {
        started = m_transformController.animateFlipHorizontal() || started;
    }
    if (flipVertical) {
        started = m_transformController.animateFlipVertical() || started;
    }
    if (!started) {
        return false;
    }

    // Record the flip as its own undo step, but build the command by hand rather
    // than going through begin/commitTransformUndoStep: committing finalizes the
    // pending animation, which would snap the mirror into place instead of
    // playing it. The endpoint is already known, so undo/redo jump between the
    // two states while the eased flip keeps running on screen.
    if (m_transformUndoManager) {
        TransformState after = before;
        after.scale = m_transformController.animatedTargetScale();
        if (!transformStatesNearlyEqual(before, after)) {
            m_transformUndoManager->push(
                std::make_unique<TransformSessionCommand>(&m_transformController, before, mode,
                    after, mode, [this]() { onTransformUndoStateRestored(); }));
        }
    }

    m_prevTransformDirtyValid = false;
    invalidateTransformViewportPreviewTransform();
    m_canvas.dirtyManager().onStructureChanged();
    requestRender();
    return true;
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

    // Mirrors the transform's own scale animation, which rides the canvas
    // animation policy — this wait has to shrink with it.
    const int flipSettleMs
        = anim::canvasEnabled() ? qMax(1, qRound(kAutoFlipAnimationDurationMs / anim::speed())) : 0;
    QTimer::singleShot(flipSettleMs, this, [this, sequence]() {
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
    const bool hadPreview = m_fillPreview.active || !m_fillPreview.affectedKeys.empty()
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
    auto* layer = m_layerModel ? m_layerModel->layerById(kickoff.layerId) : nullptr;
    if (!isLayerCanvasEditable(layer) || (!layer->isRaster() && !kickoff.maskTarget)) {
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

    FillPreviewRawTileMap layerSnapshotTiles = snapshotRawTiles<FillPreviewRawTileMap>(*targetGrid);

    FillPreviewRawTileMap selectionMaskTiles;
    if (selectionMask) {
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
        targetGrid->format(), kickoff.maskTarget, kickoff.forceFinalResultOnly, useFillWorker);

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
    request->contentFormat = targetGrid->format();
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
    m_activeFillWorkerRequest = 0;
    m_fillWorkerCancelState.reset();

    auto* layer = m_layerModel ? m_layerModel->layerById(layerId) : nullptr;
    if (!isLayerCanvasEditable(layer) || (!layer->isRaster() && !m_fillPreview.maskTarget)) {
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

    if (m_fillPreview.job) {
        m_fillPreview.job->cancelled.store(true, std::memory_order_release);
        m_fillPreview.job.reset();
    }

    m_fillPreview.selectionRestore = std::move(selectionRestore);
    m_fillPreview.queuedBatches.clear();

    const float readyRadiusAtResult = m_fillPreview.readyRadius;
    auto job = std::make_shared<FillPreviewState::AsyncJob>();
    m_fillPreview.awaitingResult = true;
    m_fillPreview.pendingResult = {};
    m_fillPreview.job = job;

    std::thread([job, result = std::move(result), originX, originY, readyRadiusAtResult]() mutable {
        if (job->cancelled.load(std::memory_order_acquire)) {
            return;
        }

        std::deque<FillPreviewState::ProgressBatch> preparedBatches
            = OpenGLCanvasWidget::buildFillPreviewBatches(
                result.afterTiles, result.fillMaskTiles, originX, originY, readyRadiusAtResult);
        if (job->cancelled.load(std::memory_order_acquire)) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(job->resultMutex);
            while (!preparedBatches.empty()) {
                job->pendingBatches.push_back(std::move(preparedBatches.front()));
                preparedBatches.pop_front();
            }
            job->result = std::move(result);
        }
        job->done.store(true, std::memory_order_release);
    }).detach();

    if (!m_fillPreview.previewActive) {
        beginFillPreviewAnimation(FloodFillResult {});
    }
    requestRender();
}

void OpenGLCanvasWidget::startAsyncFillSession(const QUuid& layerId, FillAlgorithm algorithm,
    FillPreviewRawTileMap layerSnapshotTiles, FillPreviewRawTileMap selectionMaskTiles,
    FillPreviewRawTileMap initialPreviewTiles, FillPreviewRawTileMap initialMaskTiles,
    FloodFillResult initialPendingResult, SelectionRestoreContext selectionRestore,
    FillOrigin origin, FillColor color, FillCanvasBounds canvasBounds,
    aether::TilePixelFormat contentFormat, bool maskTarget, bool forceFinalResultOnly,
    bool waitForExternalResultOnly)
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
    stopFillPreview();
    const bool finalResultOnly = waitForExternalResultOnly || forceFinalResultOnly
        || algorithm == FillAlgorithm::Classic
        || (algorithm == FillAlgorithm::Smart && !selectionMaskTiles.empty());
    // The caller captures the target grid's actual format alongside the raw
    // snapshot. It can differ from the document default for imported content.
    m_fillPreview.active = true;
    m_fillPreview.algorithm = algorithm;
    m_fillPreview.finalResultOnly = finalResultOnly;
    m_fillPreview.maskTarget = maskTarget;
    m_fillPreview.contentFormat = contentFormat;
    m_fillPreview.previewActive = false;
    m_fillPreview.easeActive = false;
    m_fillPreview.targetLayerId = layerId;
    m_fillPreview.previewContentGrid.reset();
    m_fillPreview.fillMaskGrid.reset();
    m_fillPreview.affectedKeys.clear();
    m_fillPreview.queuedBatches.clear();
    m_fillPreview.sourceCacheId = QUuid::createUuid();
    m_fillPreview.contentRevision = 1;
    m_fillPreview.viewportRevision = 1;
    m_fillPreview.finalCompositeDirty = true;
    m_fillPreview.gpuPipelineFailed = false;
    m_fillPreview.affectedDocumentBounds = {};
    m_fillPreview.viewportWidth = 0;
    m_fillPreview.viewportHeight = 0;
    m_fillPreview.cameraPosition = {};
    m_fillPreview.cameraZoom = 0.0f;
    m_fillPreview.cameraRotation = 0.0f;
    m_fillPreview.flipH = false;
    m_fillPreview.flipV = false;
    m_fillPreview.origin
        = Vector2 { static_cast<float>(originX) + 0.5f, static_cast<float>(originY) + 0.5f };
    m_fillPreview.readyRadius = 0.0f;
    m_fillPreview.displayRadius = 0.0f;
    m_fillPreview.revealSpeedPxPerMs = 0.0f;
    m_fillPreview.easeStartRadius = 0.0f;
    m_fillPreview.easeTargetRadius = 0.0f;
    resetFillPreviewMetrics();
    m_fillPreview.timer.restart();
    m_fillPreview.lastAnimationMs = 0;
    m_fillPreview.easeStartMs = 0;
    m_fillPreview.pendingResult = {};
    m_fillPreview.selectionRestore = std::move(selectionRestore);
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
        applyFillPreviewBatchBudget(kFillPreviewStartBatchBudget, kFillPreviewStartBatchBudgetMs);
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
                int selectionOffsetX = 0;
                int selectionOffsetY = 0;
                int selectionCanvasW = localCanvasW;
                int selectionCanvasH = localCanvasH;
                if (!aether::computeRawMaskPixelBounds(*selectionTilesForFill, localCanvasW,
                        localCanvasH, selectionOffsetX, selectionOffsetY, selectionCanvasW,
                        selectionCanvasH)) {
                    return;
                }

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

                        // Selection coverage caps the previewed pixel so the
                        // progressive preview matches the capped commit.
                        const uint8_t previewCap = selectionMaskTiles.empty()
                            ? 255
                            : aether::sampleRawAlpha(selectionMaskTiles, x, seedY,
                                  aether::TilePixelFormat::RGBA8);
                        aether::writeProgressiveFillPixel(layerSnapshotTiles, previewInteriorResult,
                            x, seedY, fillR, fillG, fillB, fillA, false, previewCap, contentFormat);

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
                    finalResult.fillMaskTiles, originX, originY, previousReadyRadius);
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
    const FillPreviewRawTileMap& maskTiles, int originX, int originY, float readyRadius)
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

    float previousReadyRadius = readyRadius;
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
        = OpenGLCanvasWidget::buildFillPreviewBatches(
            previewTiles, maskTiles, originX, originY, m_fillPreview.readyRadius);
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

    if (m_fillPreview.job) {
        std::lock_guard<std::mutex> lock(m_fillPreview.job->resultMutex);
        while (!m_fillPreview.job->pendingBatches.empty()) {
            m_fillPreview.queuedBatches.push_back(
                std::move(m_fillPreview.job->pendingBatches.front()));
            m_fillPreview.job->pendingBatches.pop_front();
        }
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

    m_fillPreview.affectedKeys.insert(batch.keys.begin(), batch.keys.end());
    const QRect batchDocumentBounds(batch.minTileX * static_cast<int>(TILE_SIZE),
        batch.minTileY * static_cast<int>(TILE_SIZE),
        (batch.maxTileX - batch.minTileX + 1) * static_cast<int>(TILE_SIZE),
        (batch.maxTileY - batch.minTileY + 1) * static_cast<int>(TILE_SIZE));
    m_fillPreview.affectedDocumentBounds = m_fillPreview.affectedDocumentBounds.isEmpty()
        ? batchDocumentBounds
        : m_fillPreview.affectedDocumentBounds.united(batchDocumentBounds);
    ++m_fillPreview.contentRevision;
    m_fillPreview.finalCompositeDirty = true;
    if (m_layerScreenSourceCache && !m_fillPreview.sourceCacheId.isNull()) {
        m_layerScreenSourceCache->invalidateByLayer(m_fillPreview.sourceCacheId);
    }

    if (overwroteMaskTiles) {
        m_fillPreview.metricsDirty = true;
    }
    if (!m_fillPreview.fillMaskGrid->empty()) {
        updateFillPreviewMetricsFromBatch(batch);
    }

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
    {
        std::lock_guard<std::mutex> lock(m_fillPreview.job->resultMutex);
        while (!m_fillPreview.job->pendingBatches.empty()) {
            m_fillPreview.queuedBatches.push_back(
                std::move(m_fillPreview.job->pendingBatches.front()));
            m_fillPreview.job->pendingBatches.pop_front();
        }
        result = std::move(m_fillPreview.job->result);
    }

    m_fillPreview.awaitingResult = false;
    m_fillPreview.job.reset();

    if (result.pixelsFilled <= 0 || result.fillMaskTiles.empty()) {
        stopFillPreview(false);
        return;
    }

    m_fillPreview.pendingResult = std::move(result);
    if (m_fillPreview.gpuPipelineFailed) {
        failFillPreviewGpuPipeline();
        return;
    }
    if (m_fillPreview.appliedPixelCount > 0
        && m_fillPreview.appliedMaxRadius > m_fillPreview.readyRadius + 0.01f) {
        retargetFillPreviewReveal(m_fillPreview.appliedMaxRadius);
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

    // Keep progressive batches hidden until the final result is complete. Starting
    // from a partial readyRadius produces a small eased reveal followed by a pause
    // and a fast linear catch-up when the remaining batches arrive.
    if (m_fillPreview.awaitingResult || m_fillPreview.pendingResult.pixelsFilled <= 0) {
        return;
    }

    applyFillPreviewBatchBudget(kFillPreviewStartBatchBudget, kFillPreviewStartBatchBudgetMs);

    if (m_fillPreview.readyRadius <= 0.0f && m_fillPreview.pendingResult.pixelsFilled > 0
        && !m_fillPreview.pendingResult.fillMaskTiles.empty()) {
        enqueueFillPreviewBatches(m_fillPreview.pendingResult.afterTiles,
            m_fillPreview.pendingResult.fillMaskTiles,
            static_cast<int>(std::floor(m_fillPreview.origin.x)),
            static_cast<int>(std::floor(m_fillPreview.origin.y)));
        applyFillPreviewBatchBudget(kFillPreviewStartBatchBudget, kFillPreviewStartBatchBudgetMs);
    }

    if (!m_fillPreview.queuedBatches.empty() || m_fillPreview.readyRadius <= 0.0f) {
        return;
    }

    m_fillPreview.timer.restart();
    m_fillPreview.previewActive = true;
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
    const float clampedReadyRadius = std::max(newReadyRadius, m_fillPreview.displayRadius);
    if (clampedReadyRadius <= m_fillPreview.readyRadius + 0.01f) {
        return;
    }

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
    auto* layer = m_layerModel ? m_layerModel->layerById(layerId) : nullptr;
    if (!isLayerCanvasEditable(layer) || (!layer->isRaster() && !maskTarget)) {
        return false;
    }
    TileGrid* targetGrid = maskTarget ? layer->maskTileGrid() : layer->tileGrid.get();
    if (!targetGrid) {
        return false;
    }

    auto& grid = *targetGrid;
    std::unordered_set<TileKey, TileKeyHash> affectedKeys;
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
        // Bump the grid's whole-grid content version too (not just the per-tile
        // dirty flag). Overwriting a PRE-EXISTING tile via getOrCreateTile does
        // not bump contentVersion on its own, so the layer-effect caches
        // (LayerEffectTileCacheEntry / WholeLayerCacheEntry, both keyed on
        // TileGrid::contentVersion) would score a false hit and re-serve the
        // stale pre-fill effect output for that tile — the tile visibly reverts
        // to its cached composite until a brush stroke bumps the version. Mirror
        // the DrawCommand redo path, which does both.
        grid.markDirty(key);
    }

    std::vector<TileKey> dirtyVec(affectedKeys.begin(), affectedKeys.end());
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
    snapshot.layerId = layer->id;
    snapshot.maskTarget = maskTarget;
    snapshot.beforeTiles = std::move(result.beforeTiles);
    snapshot.afterTiles = std::move(result.afterTiles);
    snapshot.createdTiles = std::move(result.createdTiles);
    snapshot.removedTiles = std::move(result.removedTiles);
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

void OpenGLCanvasWidget::stopFillPreview(bool cancelWorker, bool hidePopup)
{
    if (!m_fillPreview.active && m_fillPreview.affectedKeys.empty() && !m_fillPreview.job
        && !m_pendingFillKickoff.pending && m_activeFillWorkerRequest == 0) {
        return;
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
    m_fillPreview.easeActive = false;
    m_fillPreview.finalResultOnly = false;
    m_fillPreview.maskTarget = false;
    m_fillPreview.targetLayerId = QUuid();
    releaseFillPreviewGpuResources();
    m_fillPreview.previewContentGrid.reset();
    m_fillPreview.fillMaskGrid.reset();
    m_fillPreview.affectedKeys.clear();
    m_fillPreview.queuedBatches.clear();
    m_fillPreview.readyRadius = 0.0f;
    m_fillPreview.displayRadius = 0.0f;
    m_fillPreview.revealSpeedPxPerMs = 0.0f;
    m_fillPreview.easeStartRadius = 0.0f;
    m_fillPreview.easeTargetRadius = 0.0f;
    resetFillPreviewMetrics();
    m_fillPreview.lastAnimationMs = 0;
    m_fillPreview.easeStartMs = 0;
    m_fillPreview.pendingResult = {};
    m_fillPreview.selectionRestore = {};
    m_fillPreview.job.reset();
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

void OpenGLCanvasWidget::releaseFillPreviewGpuResources()
{
    if (m_layerScreenSourceCache && !m_fillPreview.sourceCacheId.isNull()) {
        m_layerScreenSourceCache->invalidateByLayer(m_fillPreview.sourceCacheId);
    }
    m_fillPreview.sourceCacheId = QUuid();
    if (m_fillPreview.finalCompositeTexture) {
        m_pendingFillPreviewTextureDeletes.push_back(m_fillPreview.finalCompositeTexture);
        m_fillPreview.finalCompositeTexture = 0;
    }
    m_fillPreview.finalCompositeWidth = 0;
    m_fillPreview.finalCompositeHeight = 0;
    m_fillPreview.affectedDocumentBounds = {};
    m_fillPreview.contentRevision = 0;
    m_fillPreview.viewportRevision = 0;
    m_fillPreview.finalCompositeDirty = true;
    m_fillPreview.gpuPipelineFailed = false;
}

void OpenGLCanvasWidget::flushPendingFillPreviewTextureDeletes()
{
    if (m_pendingFillPreviewTextureDeletes.empty()
        || QOpenGLContext::currentContext() != context()) {
        return;
    }
    glDeleteTextures(static_cast<GLsizei>(m_pendingFillPreviewTextureDeletes.size()),
        m_pendingFillPreviewTextureDeletes.data());
    m_pendingFillPreviewTextureDeletes.clear();
}

void OpenGLCanvasWidget::failFillPreviewGpuPipeline()
{
    if (!m_fillPreview.active) {
        return;
    }
    m_fillPreview.gpuPipelineFailed = true;
    m_fillPreview.previewActive = false;
    if (m_fillPreview.pendingResult.pixelsFilled > 0) {
        commitFillPreviewResult();
        stopFillPreview(m_fillPreview.awaitingResult);
    }
}

bool OpenGLCanvasWidget::updateFillPreviewAnimationState()
{
    if (!m_fillPreview.active) {
        return false;
    }

    applyFillPreviewBatchBudget(kFillPreviewFrameBatchBudget, kFillPreviewFrameBatchBudgetMs);
    if (m_fillPreview.gpuPipelineFailed) {
        if (m_fillPreview.awaitingResult) {
            adoptCompletedFillResult();
        }
        failFillPreviewGpuPipeline();
        return m_fillPreview.active;
    }

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

    if (m_fillPreview.awaitingResult) {
        adoptCompletedFillResult();
        if (!m_fillPreview.active) {
            return false;
        }
    }

    const bool hasQueuedBatches = !m_fillPreview.queuedBatches.empty();
    const bool hasCommitReadyResult = m_fillPreview.pendingResult.pixelsFilled > 0;
    if (!m_fillPreview.awaitingResult && m_fillPreview.appliedPixelCount > 0
        && m_fillPreview.appliedMaxRadius > m_fillPreview.readyRadius + 0.01f) {
        retargetFillPreviewReveal(m_fillPreview.appliedMaxRadius);
    }
    const bool animationCaughtUp = m_fillPreview.displayRadius + 0.5f >= m_fillPreview.readyRadius;
    if (!hasQueuedBatches && animationCaughtUp
        && (!m_fillPreview.awaitingResult || hasCommitReadyResult)) {
        commitFillPreviewResult();
        stopFillPreview(m_fillPreview.awaitingResult);
        return false;
    }

    return m_fillPreview.active;
}

bool OpenGLCanvasWidget::performFill(int worldX, int worldY)
{
    if (m_transformController.isActive())
        return false;

    auto* layer = activeLayer();
    if (!isLayerCanvasEditable(layer) || (!layer->isRaster() && !layer->maskIsEditTarget())) {
        return false;
    }
    const bool maskTarget = layer->maskIsEditTarget();
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

    stopFillPreview();

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
    if (!isLayerCanvasEditable(layer) || (!layer->isRaster() && !layer->maskIsEditTarget())) {
        return false;
    }
    const bool maskTarget = layer->maskIsEditTarget();
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

void OpenGLCanvasWidget::flushPendingStrokeFinalization()
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
        && before.lasso.regions == after.lasso.regions
        && before.lasso.maskTiles == after.lasso.maskTiles) {
        return;
    }
    // Every selection change funnels through here, so this is where Reselect
    // learns its target: the last mask that went from something to nothing.
    // Replacing one selection with another deliberately does not arm it —
    // Reselect is meant to undo a Deselect, not to walk backwards through
    // selection history.
    if (!before.lasso.isEmpty() && before.lasso.maskTiles && after.lasso.isEmpty()) {
        m_reselectState = before.lasso;
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

    if (m_fillPreview.active) {
        auto* selectedLayer = activeLayer();
        if (!selectedLayer || selectedLayer->id != m_fillPreview.targetLayerId) {
            stopFillPreview();
            requestRender();
        }
    }

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

void OpenGLCanvasWidget::setToolCursorState(bool visible, float centerX, float centerY,
    ToolCursorStyle style, const QString& toolIconResource)
{
    m_cursorOverlayState.toolCursorVisible = visible;
    m_cursorOverlayState.toolCursorCenterX = centerX;
    m_cursorOverlayState.toolCursorCenterY = centerY;
    m_cursorOverlayState.toolCursorStyle = style;
    if (!toolIconResource.isEmpty()) {
        m_cursorOverlayState.toolCursorIcon = toolIconResource;
    }
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

    // Export, thumbnails and the navigator read the composition, so a smart
    // object whose contents changed must be flattened FIRST. It runs before the
    // makeCurrent below on purpose: the sweep releases the context when it is
    // done, and everything after this line needs it current.
    refreshSmartContentComposites();

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
    // An overview is minified by definition, so it needs the pyramid as much as
    // the interactive frame does — and more of it: the request is built from
    // THIS viewport, which sees the whole document at once. Unlimited budget,
    // because a half-built export is not a frame that gets a second chance.
    m_renderer->syncDisplayPyramid(DisplayPyramidSlot::Document, m_canvas.compositionCache(),
        overviewViewport, static_cast<float>(m_canvas.width()),
        static_cast<float>(m_canvas.height()), false, false);
    m_renderer->drawTiles(m_canvas.compositionGrid(), overviewViewport, tileClipWidth,
        tileClipHeight, 0.0f, false, false, false, Color::transparent(), true,
        DisplayPyramidSlot::Document);

    if (m_layerCompositingBuilder && !m_exportPreviewHideBoardLayers) {
        const auto& boardLayerStack = m_layerCompositingBuilder->buildBoardLayerStack();
        if (!boardLayerStack.empty()) {
            std::unordered_set<TileKey, TileKeyHash> boardKeys;
            collectCompositeLayerKeys(boardLayerStack, boardKeys);
            if (!boardKeys.empty()) {
                CompositionCache boardCache;
                boardCache.markDirty(boardKeys);
                m_renderer->compositeAllDirty(boardLayerStack, boardCache);
                // This cache lives and dies inside this call, so the board slot
                // starts from nothing both here and on the next interactive
                // frame: the slot's staleness check is pointer identity and a
                // local object can reuse an address.
                m_renderer->resetDisplayPyramid(DisplayPyramidSlot::Board);
                m_renderer->syncDisplayPyramid(DisplayPyramidSlot::Board, boardCache,
                    overviewViewport, static_cast<float>(m_canvas.width()),
                    static_cast<float>(m_canvas.height()), false, false);
                m_renderer->drawTiles(boardCache.grid(), overviewViewport, 0u, 0u, 0.0f, false,
                    false, false, Color::transparent(), true, DisplayPyramidSlot::Board);
                m_renderer->resetDisplayPyramid(DisplayPyramidSlot::Board);
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

    // Render canvas to FBO. Strictly 1:1, so no minification and no pyramid.
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

bool OpenGLCanvasWidget::latchSelectionCopyMoveTransformIfNeeded(const Vector2& worldPos)
{
    if (m_selectionCopyMoveTransform || m_layerCopyMoveTransform) {
        return false;
    }
    // Copy is a drag-start decision. A later Alt press belongs to the active
    // transform and only suppresses auto snap.
    const Qt::KeyboardModifiers required = Qt::ControlModifier | Qt::AltModifier;
    if (!m_transformController.moveDragStartedWithModifiers(required)) {
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

    m_transformTargetSet = buildTransformTargetSet(*m_layerModel, aether::transformBoundsForLayer);
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

void OpenGLCanvasWidget::beginTransformSnapSession()
{
    if (!m_transformController.isActive() || !m_transformController.isDragging()) {
        m_transformController.endSnapSession();
        m_transformDragStartCorners.reset();
        return;
    }
    // Called right after every successful transform mousePress, so this is the
    // reference frame the live drag readout measures against.
    m_transformDragStartCorners = m_transformController.state().transformedCorners();

    const auto& editor = ruwa::core::SettingsManager::instance().settings().editor;
    SnapSettings settings;
    settings.canvasEnabled = editor.autoSnapCanvasEnabled;
    settings.layersEnabled = editor.autoSnapLayersEnabled;
    settings.equalSpacingEnabled = editor.autoSnapEqualSpacingEnabled;
    settings.pixelAlignRasterMovesEnabled = editor.pixelAlignRasterMovesEnabled;

    SnapScene scene;
    scene.canvasSize = m_canvas.size();
    scene.finiteCanvas = hasFiniteDocumentBounds();

    std::optional<QUuid> sourceParentId;
    bool rootsShareParent = true;
    if (m_layerModel) {
        for (const QUuid& rootId : m_transformTargetSet.rootLayerIds) {
            const auto* source = m_layerModel->layerById(rootId);
            const QUuid parentId = source && source->parent ? source->parent->id : QUuid {};
            if (!sourceParentId.has_value()) {
                sourceParentId = parentId;
            } else if (*sourceParentId != parentId) {
                rootsShareParent = false;
                break;
            }
        }
    }
    if (!rootsShareParent) {
        sourceParentId.reset();
    }

    auto belongsToMovingHierarchy = [this](const ruwa::core::layers::LayerData* layer) {
        if (!layer || !m_layerModel) {
            return false;
        }
        if (m_transformTargetSet.visualTargetIds.find(layer->id)
            != m_transformTargetSet.visualTargetIds.end()) {
            return true;
        }
        for (const QUuid& rootId : m_transformTargetSet.rootLayerIds) {
            const auto* root = m_layerModel->layerById(rootId);
            if (root && (layer == root || layer->isAncestorOf(root) || root->isAncestorOf(layer))) {
                return true;
            }
        }
        return false;
    };

    auto effectivelyVisible = [](const ruwa::core::layers::LayerData* layer) {
        for (auto* current = layer; current; current = current->parent) {
            if (!current->visible || current->opacity <= 0.0 || current->isBackground()) {
                return false;
            }
        }
        return layer != nullptr;
    };

    std::function<std::optional<Rect>(const ruwa::core::layers::LayerData*)> visibleBounds;
    visibleBounds = [&](const ruwa::core::layers::LayerData* layer) -> std::optional<Rect> {
        if (!effectivelyVisible(layer)) {
            return std::nullopt;
        }
        Rect bounds {};
        if (const auto ownBounds = aether::transformBoundsForLayer(layer)) {
            bounds = *ownBounds;
        }
        for (const auto& child : layer->children) {
            if (const auto childBounds = visibleBounds(child.get())) {
                bounds = unionTransformBounds(bounds, *childBounds);
            }
        }
        return bounds.width > 0.0f && bounds.height > 0.0f ? std::optional<Rect>(bounds)
                                                           : std::nullopt;
    };

    if (m_layerModel && settings.layersEnabled) {
        const QList<ruwa::core::layers::LayerData*> layers = m_layerModel->allLayersFlattened();
        scene.targets.reserve(static_cast<size_t>(layers.size()));
        for (const auto* layer : layers) {
            if (!effectivelyVisible(layer) || belongsToMovingHierarchy(layer)
                || (!transformIsVisualTarget(layer) && !layer->isGroup())) {
                continue;
            }
            const auto bounds
                = layer->isGroup() ? visibleBounds(layer) : aether::transformBoundsForLayer(layer);
            if (!bounds) {
                continue;
            }
            SnapTarget target;
            target.bounds = *bounds;
            target.id = layer->id;
            target.parentId = layer->parent ? layer->parent->id : QUuid {};
            target.type = layer->isGroup()      ? SnapTargetType::Group
                : layer->isText()               ? SnapTargetType::Text
                : layer->isIsolatedPixelLayer() ? SnapTargetType::IsolatedPixel
                                                : SnapTargetType::Raster;
            scene.targets.push_back(std::move(target));
        }
    }

    bool rasterOnly = !m_transformTargetSet.visualTargets.empty();
    for (const TransformTargetInfo& target : m_transformTargetSet.visualTargets) {
        rasterOnly = rasterOnly && target.kind == TransformTargetInfo::Kind::Raster;
    }
    const SnapCoordinatePolicy policy = settings.pixelAlignRasterMovesEnabled && rasterOnly
        ? SnapCoordinatePolicy::PixelAligned
        : SnapCoordinatePolicy::Continuous;
    m_transformController.beginSnapSession(
        std::move(settings), std::move(scene), policy, sourceParentId);
}

void OpenGLCanvasWidget::syncTransformMetricOverlays()
{
    syncTransformSnapMetricLabels();
    syncTransformDragMetricLabel();
}

void OpenGLCanvasWidget::syncTransformSnapMetricLabels()
{
    const auto& labels = m_transformController.snapVisualState().labels;
    while (m_transformSnapMetricLabels.size() < labels.size()) {
        m_transformSnapMetricLabels.push_back(
            new ruwa::ui::widgets::CanvasMetricLabelOverlay(this));
    }

    for (size_t i = 0; i < labels.size(); ++i) {
        const SnapMetricLabel& label = labels[i];
        const Vector2 screen = screenFromDocumentWorld(label.position);
        m_transformSnapMetricLabels[i]->presentAtPoint(label.text, QPointF(screen.x, screen.y));
    }
    for (size_t i = labels.size(); i < m_transformSnapMetricLabels.size(); ++i) {
        m_transformSnapMetricLabels[i]->dismiss();
    }
}

void OpenGLCanvasWidget::syncTransformDragMetricLabel()
{
    // Measure against the corners latched at drag start rather than the raw
    // translation/rotation/scale fields: those are bypassed in free-quad and
    // mesh modes, while the corners are meaningful in every mode.
    using ruwa::ui::widgets::MetricSegment;
    QList<MetricSegment> segments;
    if (m_transformController.isActive() && m_transformController.isDragging()
        && m_transformDragStartCorners.has_value()) {
        const auto& start = *m_transformDragStartCorners;
        const std::array<Vector2, 4> now = m_transformController.state().transformedCorners();
        const auto edge = [](const std::array<Vector2, 4>& q, int from, int to) {
            return Vector2 { q[to].x - q[from].x, q[to].y - q[from].y };
        };
        const auto length = [](const Vector2& v) {
            return std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y);
        };

        switch (m_transformController.activeDragKind()) {
        case TransformDragKind::Move: {
            const auto centroid = [](const std::array<Vector2, 4>& q) {
                return Vector2 { (q[0].x + q[1].x + q[2].x + q[3].x) * 0.25f,
                    (q[0].y + q[1].y + q[2].y + q[3].y) * 0.25f };
            };
            const Vector2 from = centroid(start);
            const Vector2 to = centroid(now);
            const int dx = qRound(to.x - from.x);
            const int dy = qRound(to.y - from.y);
            // The arrows carry the direction, so the values stay unsigned. Both
            // icons point their default way (left / down); each flips once the
            // content travels against it.
            const QString offsetTemplate = QStringLiteral("8888");
            segments.append(MetricSegment { QStringLiteral(":/icons/TransformLeft"), dx > 0, false,
                QString::number(qAbs(dx)), offsetTemplate });
            segments.append(MetricSegment { QStringLiteral(":/icons/TransformDown"), false, dy < 0,
                QString::number(qAbs(dy)), offsetTemplate });
            break;
        }
        case TransformDragKind::Rotate: {
            const Vector2 before = edge(start, 0, 1);
            const Vector2 after = edge(now, 0, 1);
            if (length(before) > 1.0e-4 && length(after) > 1.0e-4) {
                constexpr double kRadiansToDegrees = 57.29577951308232;
                double degrees = (std::atan2(after.y, after.x) - std::atan2(before.y, before.x))
                    * kRadiansToDegrees;
                // Report the shortest signed rotation, matching what the drag reads as.
                degrees = std::fmod(degrees + 540.0, 360.0) - 180.0;
                // The rotation glyph is never mirrored, so the sign is the only
                // thing left to tell the two directions apart — keep it.
                segments.append(MetricSegment { QStringLiteral(":/icons/TransformRotation"), false,
                    false, QString::number(degrees, 'f', 1) + QStringLiteral("°"),
                    QStringLiteral("-888.8°") });
            }
            break;
        }
        case TransformDragKind::Scale: {
            const double startWidth = length(edge(start, 0, 1));
            const double startHeight = length(edge(start, 0, 3));
            if (startWidth > 1.0e-4 && startHeight > 1.0e-4) {
                const double widthPercent = length(edge(now, 0, 1)) / startWidth * 100.0;
                const double heightPercent = length(edge(now, 0, 3)) / startHeight * 100.0;
                const bool uniform = std::abs(widthPercent - heightPercent) < 0.05;
                const QString value = uniform
                    ? QStringLiteral("%1%").arg(QString::number(widthPercent, 'f', 1))
                    : QStringLiteral("%1% × %2%")
                          .arg(QString::number(widthPercent, 'f', 1),
                              QString::number(heightPercent, 'f', 1));
                // Non-uniform scales can grow one axis and shrink the other;
                // the icon follows the area, which is what the eye reads.
                const bool grew = widthPercent * heightPercent >= 100.0 * 100.0;
                segments.append(MetricSegment { grew ? QStringLiteral(":/icons/TransformBigger")
                                                     : QStringLiteral(":/icons/TransformSmaller"),
                    false, false, value,
                    uniform ? QStringLiteral("888.8%") : QStringLiteral("888.8% × 888.8%") });
            }
            break;
        }
        default:
            break;
        }
    }

    if (segments.isEmpty()) {
        if (m_transformDragMetricLabel) {
            m_transformDragMetricLabel->dismiss();
        }
        return;
    }

    if (!m_transformDragMetricLabel) {
        m_transformDragMetricLabel = new ruwa::ui::widgets::CanvasMetricLabelOverlay(this);
    }

    m_transformDragMetricLabel->presentAtCursor(segments, QPointF(mapFromGlobal(QCursor::pos())));
}

void OpenGLCanvasWidget::refreshTransformDragMetricAnchor()
{
    // Sampled per frame off the OS cursor rather than off move events, the same
    // way pan sampling is: the readout then tracks the pointer at the display
    // refresh rate even when Qt coalesces or delays the moves behind it.
    if (!m_transformDragMetricLabel || !m_transformController.isActive()
        || !m_transformController.isDragging()) {
        return;
    }
    m_transformDragMetricLabel->refreshAtCursor(QPointF(mapFromGlobal(QCursor::pos())));
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
    m_transformTargetSet = buildTransformTargetSet(*m_layerModel, aether::transformBoundsForLayer);
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
                m_transformController.state() = aether::transformStateWithSourceBounds(
                    layer->textData->transform, sourceBounds);
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
                    = aether::transformStateWithSourceBounds(layer->smartTransform, sourceBounds);
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

void OpenGLCanvasWidget::beginInteractiveContentMove()
{
    if (m_interactiveContentMove) {
        return;
    }
    m_interactiveContentMove = true;
    // The session is not opened here: whether one is needed at all depends on
    // the first delta actually arriving, and entering move-only mode on an
    // empty layer would fail for a gesture that may never move anything.
    m_interactiveContentMoveOwnsSession = false;
}

void OpenGLCanvasWidget::endInteractiveContentMove()
{
    if (!m_interactiveContentMove) {
        return;
    }
    const bool ownedSession = m_interactiveContentMoveOwnsSession;
    m_interactiveContentMove = false;
    m_interactiveContentMoveOwnsSession = false;

    if (!m_transformController.isActive()) {
        return; // the drag never moved anything
    }
    if (ownedSession) {
        // Bakes the pixels and pushes the ordinary transform undo command, so
        // the whole drag lands in history as one move.
        confirmTransform();
        return;
    }
    // The user's own transform session stays open; the drag is recorded as a
    // single step inside it, the way one nudge would have been.
    commitTransformUndoStep();
    m_prevTransformDirtyValid = false;
    invalidateTransformViewportPreviewTransform();
    m_canvas.dirtyManager().onStructureChanged();
    requestRender();
}

bool OpenGLCanvasWidget::moveSelectedContentBy(const Vector2& delta)
{
    if (qAbs(delta.x) < 1e-4f && qAbs(delta.y) < 1e-4f) {
        return false;
    }
    // The transform renderer owns a single atlas and a single readback PBO, so
    // it must not be re-entered while an apply is still in flight.
    if (m_autoApplyingTransform || m_pendingTransform.active) {
        return false;
    }
    if (m_strokeHost && m_strokeHost->isDrawing()) {
        return false;
    }

    const auto applyDelta = [this, &delta]() {
        m_transformController.state().translateBy(delta.x, delta.y);
    };

    // A transform the user already has open is nudged in place and recorded as
    // its own step, the way a mode switch or a flip is — closing their session
    // out from under them would lose the rest of their edit.
    if (m_transformController.isActive()) {
        if (m_transformController.isDragging()) {
            return false;
        }
        // A live move session records one step for the whole gesture, opened on
        // its first delta and closed by endInteractiveContentMove().
        const bool liveMove = m_interactiveContentMove;
        if (!liveMove) {
            beginTransformUndoStep();
        } else if (!m_interactiveContentMoveOwnsSession && !m_transformUndoStepBefore.has_value()) {
            beginTransformUndoStep();
        }
        applyDelta();
        if (!liveMove) {
            commitTransformUndoStep();
        }

        m_prevTransformDirtyValid = false;
        invalidateTransformViewportPreviewTransform();
        m_canvas.dirtyManager().onStructureChanged();
        requestRender();
        return true;
    }

    if (!enterMoveOnlyTransformMode()) {
        return false;
    }
    applyDelta();
    if (m_interactiveContentMove) {
        // The pixels stay on the GPU as a transform preview until the gesture
        // ends: baking every intermediate delta would cost a readback per
        // mouse move and fill the history with them.
        m_interactiveContentMoveOwnsSession = true;
        m_prevTransformDirtyValid = false;
        invalidateTransformViewportPreviewTransform();
        m_canvas.dirtyManager().onStructureChanged();
        requestRender();
        return true;
    }
    // Bakes the pixels and pushes the ordinary transform undo command, so the
    // move lands in history looking like any other move.
    confirmTransform();
    return true;
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
        syncTransformMetricOverlays();
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
        syncTransformMetricOverlays();
        destroyTransformUndoStack();
        const Rect cacheBounds
            = unionTransformBounds(m_transformTargetSet.contentBounds, stateCopy.transformedAABB());
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
        syncTransformMetricOverlays();
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
        syncTransformMetricOverlays();
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
        syncTransformMetricOverlays();
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
    syncTransformMetricOverlays();
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

    // Canvas motion (camera inertia, transform easing) is a rate, not a duration:
    // a faster policy means a faster decay, which is exactly a longer time step.
    // With canvas animations off there is nothing to interpolate — both the
    // camera and the transform jump to the state they were heading for.
    if (anim::canvasEnabled()) {
        const float animatedDt = dt * static_cast<float>(anim::speed());
        m_viewport.camera().update(animatedDt);
        if (m_transformController.isActive() && m_transformController.updateAnimation(animatedDt)) {
            update();
        }
    } else {
        m_viewport.camera().finishAnimation();
        if (m_transformController.isActive() && m_transformController.hasPendingAnimation()) {
            m_transformController.finalizePendingAnimation();
            update();
        }
    }
    m_cameraWasAnimatingLastFrame
        = m_viewport.camera().isAnimating() || m_transformController.hasPendingAnimation();

    updateFillProgressPopupPosition();
    refreshTransformDragMetricAnchor();
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
    if (!m_renderer || !m_renderer->compositor() || !m_renderer->tileRenderer()) {
        return;
    }
    // Every path below can decide there is nothing to draw and return without
    // syncing the board slot. Whatever it still owed died with the board, and
    // leaving the flag set would keep asking paintGL for a catch-up frame that
    // can never clear it. The sync, when it happens, sets the flag again.
    m_renderer->clearDisplayPyramidPending(DisplayPyramidSlot::Board);
    if (m_exportPreviewHideBoardLayers) {
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

    // The board gets its own pyramid: level 0 of a pyramid IS one grid, and this
    // is a different grid from the document's cache. Without it the board would
    // be the one aliased thing in a smooth zoomed-out frame.
    //
    // Board tiles are composited only where visible (see above), so a level tile
    // straddling the window edge box-filters transparency in from the neighbour
    // that was culled. That is confined to the outermost texel of the frame and
    // heals as soon as panning composites the tile.
    DisplayPyramidPacing boardPacing;
    boardPacing.deferrableTileBudget = displayPyramidDeferrableBudget();
    boardPacing.focusPoint = displayPyramidFocusPoint();
    m_renderer->syncDisplayPyramid(DisplayPyramidSlot::Board, m_boardCompositionCache, m_viewport,
        static_cast<float>(m_canvas.width()), static_cast<float>(m_canvas.height()),
        effectiveContentFlipH(), effectiveContentFlipV(), boardPacing);
    m_renderer->drawTiles(m_boardCompositionCache.grid(), m_viewport, m_canvas.width(),
        m_canvas.height(), 0.0f, effectiveContentFlipH(), effectiveContentFlipV(), false,
        Color::transparent(), false, DisplayPyramidSlot::Board);
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
    // Bring the display pyramid up to date with whatever the composite above
    // just changed, then draw the frame from it. Unlike the per-tile mip chains
    // this replaces, the update is NOT suspended during a stroke: it costs about
    // a third of the compositing that already happened this frame, and
    // suspending it is precisely what used to flip the whole canvas to aliased
    // for the length of a stroke. The budget below only ever bounds HOW MUCH is
    // refreshed per frame, never whether the pyramid is used.
    DisplayPyramidPacing documentPacing;
    documentPacing.deferrableTileBudget = displayPyramidDeferrableBudget();
    documentPacing.focusPoint = displayPyramidFocusPoint();
    m_renderer->syncDisplayPyramid(DisplayPyramidSlot::Document, m_canvas.compositionCache(),
        m_viewport, static_cast<float>(m_canvas.width()), static_cast<float>(m_canvas.height()),
        effectiveContentFlipH(), effectiveContentFlipV(), documentPacing);
    m_renderer->drawTiles(m_canvas.compositionGrid(), m_viewport, tileClipWidth, tileClipHeight,
        tileCornerRadiusCanvasPx, effectiveContentFlipH(), effectiveContentFlipV(),
        compositeRoundedEdgesOverViewportBackground, m_backgroundColor, true,
        DisplayPyramidSlot::Document);
    renderBoardLayers(boardLayerStack);
    if (renderToSceneFbo) {
        m_sceneFboManager.blitToDefaultFbo(this, defaultFbo, surfaceWidth, surfaceHeight);
    }
}

void OpenGLCanvasWidget::setBackdropRegionProvider(BackdropRegionProvider provider)
{
    m_backdropRegionProvider = std::move(provider);
    requestRender();
}

void OpenGLCanvasWidget::paintGL_renderBackdrop(GLint defaultFbo)
{
    if (!m_backdropRegionProvider) {
        return;
    }
    const std::vector<CanvasBackdropRegion> regions = m_backdropRegionProvider();
    if (regions.empty()) {
        return;
    }
    if (!m_backdropRenderer) {
        m_backdropRenderer = std::make_unique<CanvasBackdropRenderer>(
            static_cast<QOpenGLFunctions_4_5_Core*>(this));
        auto initResult = m_backdropRenderer->initialize(m_fillShaderDir);
        if (!initResult) {
            m_backdropRenderer.reset();
            return;
        }
    }
    const QSize surf = currentSurfacePixelSize(this);
    // QWidget geometry is expressed in logical pixels. resizeGL() and the
    // internal viewport currently use those same units, whereas the native
    // devicePixelRatio can be greater than one. Derive the conversion from the
    // actual render surface instead of applying DPR a second time.
    const qreal scaleX
        = width() > 0 ? static_cast<qreal>(surf.width()) / static_cast<qreal>(width()) : 1.0;
    const qreal scaleY
        = height() > 0 ? static_cast<qreal>(surf.height()) / static_cast<qreal>(height()) : 1.0;
    // Source and destination are the same framebuffer: the blur samples exactly
    // what the frame has already drawn (scene, screen-space previews, chrome).
    // No feedback loop — every region is blitted into its own reduction targets
    // before anything is composited back.
    const GLuint target = static_cast<GLuint>(defaultFbo);
    const bool available = m_backdropRenderer->render(
        target, target, surf.width(), surf.height(), scaleX, scaleY, regions);
    if (available != m_backdropRendererAvailable) {
        m_backdropRendererAvailable = available;
        emit backdropAvailabilityChanged();
    }
}

bool OpenGLCanvasWidget::backdropAvailable() const
{
    return m_backdropRendererAvailable;
}

void OpenGLCanvasWidget::requestBackdropUpdate()
{
    requestRender();
}

void OpenGLCanvasWidget::paintGL_renderOverlays(GLuint sceneTarget)
{
    const QSize surfaceSize = currentSurfacePixelSize(this);
    const int surfaceWidth = surfaceSize.width();
    const int surfaceHeight = surfaceSize.height();
    auto* transformOverlay = m_overlayManager ? m_overlayManager->transformOverlay() : nullptr;
    auto* canvasResizeOverlay
        = m_overlayManager ? m_overlayManager->canvasResizeOverlay() : nullptr;
    auto* textEditOverlay = m_overlayManager ? m_overlayManager->textEditOverlay() : nullptr;
    const bool moveAxisGuideActive = m_transformController.moveAxisGuideActive();
    const auto& autoSnapGuideState = m_transformController.snapVisualState();
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
    const TransformAutoSnapGuides* autoSnapGuidesPtr = nullptr;
    if (autoSnapGuideActive) {
        autoSnapGuidesPtr = &autoSnapGuideState;
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
}

// A GL cursor is drawn inside the canvas frame, so it needs a position that is
// as fresh as the frame — not the one carried by the last MouseMove that made
// it through. Runs before anything reads the cursor state this frame.
void OpenGLCanvasWidget::paintGL_syncCursorToLivePointer()
{
    if (m_skipCursorOverlays || m_cursorPositionPinned) {
        // No cursor in this frame at all (export grab), or the brush-size drag
        // is parking the ring on its anchor while the pointer moves away.
        return;
    }
    if (!m_cursorOverlayState.brushVisible && !m_cursorOverlayState.eyedropperVisible
        && !m_cursorOverlayState.toolCursorVisible) {
        return;
    }
    if (!isActiveWindow()) {
        // The GL cursor belongs to Ruwa's own canvas interaction; while another
        // window is in front, the pointer is not ours to follow. Same rule as
        // CanvasCursorManager::isOverCanvas.
        return;
    }

    // Same source of truth as CanvasCursorManager: the direct WinTab position
    // while native routing owns the stylus, the system pointer otherwise.
    const auto nativePos
        = ruwa::services::input::StylusInputManager::instance().nativeCursorPosition();
    const QPoint globalPos = nativePos.value_or(QCursor::pos());
    const QPoint localPos = mapFromGlobal(globalPos);
    if (!rect().contains(localPos)) {
        // Off the canvas. Visibility is the cursor manager's call, not this
        // frame's — leave the last position alone and let it hide the cursor.
        return;
    }

    const qreal scaleX
        = width() > 0 ? static_cast<qreal>(m_viewport.width()) / static_cast<qreal>(width()) : 1.0;
    const qreal scaleY = height() > 0
        ? static_cast<qreal>(m_viewport.height()) / static_cast<qreal>(height())
        : 1.0;
    const float centerX = static_cast<float>(static_cast<qreal>(localPos.x()) * scaleX);
    const float centerY = static_cast<float>(static_cast<qreal>(localPos.y()) * scaleY);

    // A moved pointer always earns one more frame. Normally the MouseMove that
    // carried it would have asked for that frame; when input is starved nothing
    // does, and the cursor would sit still until some unrelated repaint came
    // along. Self-limiting: once a frame has drawn the pointer where it now is,
    // the positions match and no further frame is requested. Both sides are
    // derived the same way from integer widget coordinates, so they compare
    // exactly — a tolerance here would drop slow single-pixel movement.
    if (centerX != m_lastSyncedCursorX || centerY != m_lastSyncedCursorY) {
        m_lastSyncedCursorX = centerX;
        m_lastSyncedCursorY = centerY;
        update();
    }

    if (m_cursorOverlayState.brushVisible) {
        m_cursorOverlayState.brushCenterX = centerX;
        m_cursorOverlayState.brushCenterY = centerY;
    }
    if (m_cursorOverlayState.eyedropperVisible) {
        m_cursorOverlayState.eyedropperCenterX = centerX;
        m_cursorOverlayState.eyedropperCenterY = centerY;
    }
    if (m_cursorOverlayState.toolCursorVisible) {
        m_cursorOverlayState.toolCursorCenterX = centerX;
        m_cursorOverlayState.toolCursorCenterY = centerY;
    }
}

// The cursor overlays are the topmost thing on the canvas, so they run after
// every pass that writes canvas pixels. The lasso fill preview in particular
// re-renders the viewport into the scene FBO and blits it over the whole
// surface, which would bury a cursor drawn with the other overlays.
void OpenGLCanvasWidget::paintGL_renderCursorOverlays()
{
    if (m_skipCursorOverlays || !m_overlayManager || !m_sceneFboManager.sceneTexture()) {
        return;
    }

    const QSize surfaceSize = currentSurfacePixelSize(this);
    const int surfaceWidth = surfaceSize.width();
    const int surfaceHeight = surfaceSize.height();

    auto* brushCursorOverlay = m_overlayManager->brushCursorOverlay();
    auto* eyedropperCursorOverlay = m_overlayManager->eyedropperCursorOverlay();
    auto* toolCursorOverlay = m_overlayManager->toolCursorOverlay();

    const bool wantBrushCursor = brushCursorOverlay && m_cursorOverlayState.brushVisible
        && m_cursorOverlayState.brushRadius > 0.5f;
    const bool wantEyedropperCursor
        = eyedropperCursorOverlay && m_cursorOverlayState.eyedropperVisible;
    const bool wantToolCursor = toolCursorOverlay && m_cursorOverlayState.toolCursorVisible;

    if (wantBrushCursor) {
        ensureCursorOverlayInitialized(brushCursorOverlay, "brush cursor overlay");
    }
    if (wantEyedropperCursor) {
        ensureCursorOverlayInitialized(eyedropperCursorOverlay, "eyedropper cursor overlay");
    }
    if (wantToolCursor) {
        ensureCursorOverlayInitialized(toolCursorOverlay, "tool cursor overlay");
    }

    if (wantBrushCursor && brushCursorOverlay->isInitialized()) {
        const float cursorRotation = m_brush ? m_brush->previewDabRotationDeltaRadians() : 0.0f;
        brushCursorOverlay->render(m_cursorOverlayState.brushCenterX,
            m_cursorOverlayState.brushCenterY, m_cursorOverlayState.brushRadius, surfaceWidth,
            surfaceHeight, m_sceneFboManager.sceneTexture(), cursorRotation);
    }
    if (wantEyedropperCursor && eyedropperCursorOverlay->isInitialized()) {
        const QColor selectedColor = QColor::fromRgbF(m_cursorOverlayState.eyedropperSelectedR,
            m_cursorOverlayState.eyedropperSelectedG, m_cursorOverlayState.eyedropperSelectedB,
            m_cursorOverlayState.eyedropperSelectedA);
        eyedropperCursorOverlay->render(m_cursorOverlayState.eyedropperCenterX,
            m_cursorOverlayState.eyedropperCenterY, surfaceWidth, surfaceHeight,
            m_sceneFboManager.sceneTexture(), selectedColor);
    }
    if (wantToolCursor && toolCursorOverlay->isInitialized()) {
        toolCursorOverlay->render(m_cursorOverlayState.toolCursorCenterX,
            m_cursorOverlayState.toolCursorCenterY, surfaceWidth, surfaceHeight,
            m_sceneFboManager.sceneTexture(), m_cursorOverlayState.toolCursorStyle,
            m_cursorOverlayState.toolCursorIcon);
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
        // The latched source-overscan pad is expressed in screen px at the previous
        // camera, and every cached source is dropped below anyway, so re-derive it
        // from scratch for the new viewport instead of carrying a stale maximum.
        session.sourceOverscanPadX = 0;
        session.sourceOverscanPadY = 0;
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

    // The pad a transform source needs grows continuously with the drag offset (for a
    // pure move it is offset * zoom). LayerScreenSourceCache allocates its entry at the
    // source viewport size, so a pad that follows the offset px-for-px destroyed and
    // re-created the texture — and re-rendered the layer in full — for every transform
    // target on every drag frame. Rounding the pad up to a coarse quantum makes the
    // source size change only once per quantum of travel, so the cached source (and its
    // render) is reused for the frames in between.
    constexpr uint32_t kSourcePadQuantum = 256;
    auto quantizeSourcePad = [](float needed, uint32_t maxPad) -> uint32_t {
        if (needed <= 0.0f) {
            return 0u;
        }
        const uint32_t raw = static_cast<uint32_t>(std::ceil(needed + 8.0f));
        const uint32_t quantized
            = ((raw + kSourcePadQuantum - 1u) / kSourcePadQuantum) * kSourcePadQuantum;
        return std::min(quantized, maxPad);
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
        // Latch the running maximum on top of the quantised requirement: dragging back
        // toward the origin would otherwise shrink the source and throw away the larger
        // texture that the outbound half of the same drag already rendered. The latch is
        // cleared whenever the camera/viewport changes (which drops the cache anyway).
        const uint32_t neededPadX = quantizeSourcePad(sourcePadXNeeded, maxSourcePadX);
        const uint32_t neededPadY = quantizeSourcePad(sourcePadYNeeded, maxSourcePadY);
        overscan.padX = std::min(std::max(session.sourceOverscanPadX, neededPadX), maxSourcePadX);
        overscan.padY = std::min(std::max(session.sourceOverscanPadY, neededPadY), maxSourcePadY);
        session.sourceOverscanPadX = overscan.padX;
        session.sourceOverscanPadY = overscan.padY;
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
        // Quantised for the same reason as the plain source overscan above. No session
        // latch here: this pad is relative to a per-layer effect-overscan OUTPUT size,
        // so the maximum is not shared across layers and would over-allocate.
        overscan.padX = quantizeSourcePad(padXNeeded, maxPadX);
        overscan.padY = quantizeSourcePad(padYNeeded, maxPadY);
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
        // The content stays put, so it must be the grid the document compositor
        // would draw: for a smart layer that is the PROJECTED grid. Its raw
        // pixelGrid() lives at the content origin and would snap the object into
        // the top-left corner for the duration of the mask transform.
        fixedContentInfo.tileGrid = m_layerCompositingBuilder
            ? m_layerCompositingBuilder->compositingGridForLayer(targetLayer)
            : targetLayer->pixelGrid();
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

void OpenGLCanvasWidget::paintGL_renderFillPreviewOverlay(
    const std::vector<CompositeLayerInfo>& layerStack, GLuint sceneTarget, GLint defaultFbo)
{
    if (!m_fillPreview.active || !m_fillPreview.previewActive || !m_fillPreview.previewContentGrid
        || !m_fillPreview.fillMaskGrid || m_fillPreview.fillMaskGrid->empty() || !m_renderer
        || !m_layerScreenSourceCache) {
        return;
    }

    const QSize surfaceSize = currentSurfacePixelSize(this);
    const int viewportWidth = surfaceSize.width();
    const int viewportHeight = surfaceSize.height();
    auto* viewportCompositor = m_renderer->viewportCompositor();
    auto* targetPreviewPass = m_renderer->targetLayerPreviewPass();
    if (viewportWidth <= 0 || viewportHeight <= 0 || !viewportCompositor
        || !viewportCompositor->isInitialized() || !targetPreviewPass
        || !targetPreviewPass->isInitialized()) {
        failFillPreviewGpuPipeline();
        return;
    }

    auto* targetLayer
        = m_layerModel ? m_layerModel->layerById(m_fillPreview.targetLayerId) : nullptr;
    if (!targetLayer || (!targetLayer->isRaster() && !m_fillPreview.maskTarget)
        || (m_fillPreview.maskTarget && !targetLayer->maskTileGrid())) {
        stopFillPreview();
        return;
    }

    const Vector2 cameraPosition = m_viewport.camera().position();
    const float cameraZoom = m_viewport.camera().zoom();
    const float cameraRotation = m_viewport.camera().rotation();
    const bool flipH = effectiveContentFlipH();
    const bool flipV = effectiveContentFlipV();
    const bool viewportChanged = m_viewport.camera().isAnimating()
        || m_fillPreview.viewportWidth != static_cast<uint32_t>(viewportWidth)
        || m_fillPreview.viewportHeight != static_cast<uint32_t>(viewportHeight)
        || m_fillPreview.flipH != flipH || m_fillPreview.flipV != flipV
        || !nearlyEqualPoint(m_fillPreview.cameraPosition, cameraPosition)
        || !nearlyEqualFloat(m_fillPreview.cameraZoom, cameraZoom)
        || !nearlyEqualFloat(m_fillPreview.cameraRotation, cameraRotation);
    if (viewportChanged) {
        m_fillPreview.viewportWidth = static_cast<uint32_t>(viewportWidth);
        m_fillPreview.viewportHeight = static_cast<uint32_t>(viewportHeight);
        m_fillPreview.cameraPosition = cameraPosition;
        m_fillPreview.cameraZoom = cameraZoom;
        m_fillPreview.cameraRotation = cameraRotation;
        m_fillPreview.flipH = flipH;
        m_fillPreview.flipV = flipV;
        ++m_fillPreview.viewportRevision;
        m_fillPreview.finalCompositeDirty = true;
        m_layerScreenSourceCache->invalidateByViewport();
    }
    if (m_fillPreview.viewportRevision == 0) {
        m_fillPreview.viewportRevision = 1;
    }

    const CompositeLayerInfo* targetLayerInfo = nullptr;
    std::function<void(const std::vector<CompositeLayerInfo>&)> findTarget
        = [&](const std::vector<CompositeLayerInfo>& layers) {
              for (const CompositeLayerInfo& info : layers) {
                  if (targetLayerInfo) {
                      return;
                  }
                  if (info.isGroup) {
                      findTarget(info.children);
                  } else if (info.id == m_fillPreview.targetLayerId) {
                      targetLayerInfo = &info;
                  }
              }
          };
    findTarget(layerStack);
    if (!targetLayerInfo) {
        // Export-excluded board layers are composed by a different surface and
        // cannot be represented exactly by this document viewport stack.
        failFillPreviewGpuPipeline();
        return;
    }

    const bool finiteDocumentBounds = hasFiniteDocumentBounds();
    const uint32_t canvasWidth = finiteDocumentBounds ? m_canvas.width() : 0u;
    const uint32_t canvasHeight = finiteDocumentBounds ? m_canvas.height() : 0u;

    if (m_fillPreview.finalCompositeDirty || !m_fillPreview.finalCompositeTexture) {
        CompositeLayerInfo afterInfo;
        afterInfo.id = m_fillPreview.sourceCacheId;
        afterInfo.tileGrid = m_fillPreview.previewContentGrid.get();
        afterInfo.opacity = 1.0f;
        afterInfo.blendMode = 0;
        afterInfo.visible = true;
        const GLuint afterTexture = m_layerScreenSourceCache->acquireLayerTexture(afterInfo,
            *m_renderer, m_viewport, canvasWidth, canvasHeight, flipH, flipV,
            m_fillPreview.viewportRevision, LayerScreenSourceCache::SourceKind::LayerColor,
            ruwa::core::effects::LayerSourcePurpose::RawContent, m_fillPreview.contentRevision);

        CompositeLayerInfo coverageInfo;
        coverageInfo.id = m_fillPreview.sourceCacheId;
        coverageInfo.tileGrid = m_fillPreview.fillMaskGrid.get();
        coverageInfo.opacity = 1.0f;
        coverageInfo.blendMode = 0;
        coverageInfo.visible = true;
        const GLuint coverageTexture = m_layerScreenSourceCache->acquireLayerTexture(coverageInfo,
            *m_renderer, m_viewport, 0u, 0u, flipH, flipV, m_fillPreview.viewportRevision,
            LayerScreenSourceCache::SourceKind::AlphaMask,
            ruwa::core::effects::LayerSourcePurpose::MaskColor, m_fillPreview.contentRevision);

        GLuint replacementBaseTexture = 0;
        if (m_fillPreview.maskTarget) {
            CompositeLayerInfo committedMaskInfo;
            committedMaskInfo.id = targetLayer->id;
            committedMaskInfo.effectChainRevision = targetLayer->effectChainRevision;
            committedMaskInfo.tileGrid = targetLayer->maskTileGrid();
            committedMaskInfo.opacity = 1.0f;
            committedMaskInfo.blendMode = 0;
            committedMaskInfo.visible = true;
            replacementBaseTexture = m_layerScreenSourceCache->acquireLayerTexture(
                committedMaskInfo, *m_renderer, m_viewport, canvasWidth, canvasHeight, flipH, flipV,
                m_fillPreview.viewportRevision, LayerScreenSourceCache::SourceKind::LayerMask,
                ruwa::core::effects::LayerSourcePurpose::MaskColor);
        } else {
            CompositeLayerInfo targetBaseInfo = *targetLayerInfo;
            targetBaseInfo.effectChainRevision = targetLayer->effectChainRevision;
            targetBaseInfo.externalClipMaskGrid = nullptr;
            targetBaseInfo.clipMaskLuminanceReveal = false;
            targetBaseInfo.clipMaskGrid2 = nullptr;
            replacementBaseTexture = m_layerScreenSourceCache->acquireLayerTexture(targetBaseInfo,
                *m_renderer, m_viewport, canvasWidth, canvasHeight, flipH, flipV,
                m_fillPreview.viewportRevision, LayerScreenSourceCache::SourceKind::LayerColor,
                ruwa::core::effects::LayerSourcePurpose::RawContent);
        }

        const GLuint replacedTexture = targetPreviewPass->renderTextureReplacement(
            replacementBaseTexture, afterTexture, coverageTexture,
            static_cast<uint32_t>(viewportWidth), static_cast<uint32_t>(viewportHeight));
        if (!replacedTexture) {
            failFillPreviewGpuPipeline();
            return;
        }

        viewportCompositor->beginFrame(
            static_cast<uint32_t>(viewportWidth), static_cast<uint32_t>(viewportHeight));
        Color canvasBackground;
        const bool hasCanvasBackground
            = m_layerCompositingBuilder->resolveCanvasBackgroundColor(canvasBackground);
        const GLuint transientComposite = viewportCompositor->compositeLayers(
            layerStack,
            [&](const CompositeLayerInfo& info) -> GLuint {
                if (!m_fillPreview.maskTarget && !info.isGroup
                    && info.id == m_fillPreview.targetLayerId) {
                    return replacedTexture;
                }
                return m_layerScreenSourceCache->acquireLayerTexture(info, *m_renderer, m_viewport,
                    canvasWidth, canvasHeight, flipH, flipV, m_fillPreview.viewportRevision,
                    LayerScreenSourceCache::SourceKind::LayerColor,
                    ruwa::core::effects::LayerSourcePurpose::RawContent);
            },
            hasCanvasBackground ? canvasBackground : Color::transparent(), cameraZoom,
            buildViewportEffectRegion(m_viewport, static_cast<float>(canvasWidth),
                static_cast<float>(canvasHeight), flipH, flipV, viewportWidth, viewportHeight),
            [&](const CompositeLayerInfo& info, int padX,
                int padY) -> GLViewportCompositor::OverscanLayerSource {
                if (info.isGroup || info.id == m_fillPreview.targetLayerId) {
                    return {};
                }
                return resolveOverscanRasterSource(info, padX, padY, *m_layerScreenSourceCache,
                    *m_renderer, m_viewport, viewportWidth, viewportHeight, canvasWidth,
                    canvasHeight, flipH, flipV, m_fillPreview.viewportRevision);
            },
            [&](const CompositeLayerInfo& info) -> GLuint {
                if (m_fillPreview.maskTarget && !info.isGroup
                    && info.id == m_fillPreview.targetLayerId) {
                    return replacedTexture;
                }
                return acquireLayerMaskTextureForPreview(
                    info, flipH, flipV, m_fillPreview.viewportRevision);
            });
        if (!transientComposite
            || !viewportCompositor->saveTexture(transientComposite,
                m_fillPreview.finalCompositeTexture, m_fillPreview.finalCompositeWidth,
                m_fillPreview.finalCompositeHeight)) {
            failFillPreviewGpuPipeline();
            return;
        }
        m_fillPreview.finalCompositeDirty = false;
    }

    if (!m_fillPreview.finalCompositeTexture || m_fillPreview.affectedDocumentBounds.isEmpty()) {
        return;
    }

    QRect affectedBounds = m_fillPreview.affectedDocumentBounds;
    int effectPadDocument = 0;
    if (m_layerModel) {
        m_layerModel->forEach([&](ruwa::core::layers::LayerData* layer) {
            if (layer) {
                effectPadDocument = std::max(effectPadDocument,
                    ruwa::core::effects::EffectCoverageResolver::neighborhoodPadPixels(
                        layer->effects, /*realtimeOnly=*/true));
            }
        });
    }
    affectedBounds.adjust(
        -effectPadDocument, -effectPadDocument, effectPadDocument, effectPadDocument);
    const std::array<Vector2, 4> documentCorners { Vector2 {
                                                       static_cast<float>(affectedBounds.left()),
                                                       static_cast<float>(affectedBounds.top()) },
        Vector2 { static_cast<float>(affectedBounds.right() + 1),
            static_cast<float>(affectedBounds.top()) },
        Vector2 { static_cast<float>(affectedBounds.right() + 1),
            static_cast<float>(affectedBounds.bottom() + 1) },
        Vector2 { static_cast<float>(affectedBounds.left()),
            static_cast<float>(affectedBounds.bottom() + 1) } };
    float minScreenX = std::numeric_limits<float>::max();
    float minScreenY = std::numeric_limits<float>::max();
    float maxScreenX = std::numeric_limits<float>::lowest();
    float maxScreenY = std::numeric_limits<float>::lowest();
    for (const Vector2& corner : documentCorners) {
        const Vector2 screen = screenFromDocumentWorld(corner);
        minScreenX = std::min(minScreenX, screen.x);
        minScreenY = std::min(minScreenY, screen.y);
        maxScreenX = std::max(maxScreenX, screen.x);
        maxScreenY = std::max(maxScreenY, screen.y);
    }
    constexpr int kFillPreviewScissorPadding = 2;
    const QRect drawBounds(static_cast<int>(std::floor(minScreenX)) - kFillPreviewScissorPadding,
        static_cast<int>(std::floor(minScreenY)) - kFillPreviewScissorPadding,
        static_cast<int>(std::ceil(maxScreenX) - std::floor(minScreenX))
            + kFillPreviewScissorPadding * 2,
        static_cast<int>(std::ceil(maxScreenY) - std::floor(minScreenY))
            + kFillPreviewScissorPadding * 2);
    const QRect clippedDrawBounds
        = drawBounds.intersected(QRect(0, 0, viewportWidth, viewportHeight));
    if (clippedDrawBounds.isEmpty()) {
        return;
    }

    GLViewportCompositor::CanvasClipParams clipParams;
    if (finiteDocumentBounds) {
        clipParams.enabled = true;
        clipParams.cameraPosition = cameraPosition;
        clipParams.cameraZoom = cameraZoom;
        clipParams.cameraRotation = cameraRotation;
        clipParams.canvasWidth = static_cast<float>(canvasWidth);
        clipParams.canvasHeight = static_cast<float>(canvasHeight);
        clipParams.canvasCornerRadius = canvasCornerRadiusCanvasPx();
    }
    GLViewportCompositor::RadialRevealParams radialReveal;
    radialReveal.enabled = true;
    radialReveal.documentOrigin = m_fillPreview.origin;
    radialReveal.radius = std::max(1.0f, m_fillPreview.displayRadius);
    radialReveal.feather = m_fillPreview.feather;
    radialReveal.flipH = flipH;
    radialReveal.flipV = flipV;
    GLViewportCompositor::CheckerBackdropParams checkerBackdrop;
    checkerBackdrop.enabled = true;
    checkerBackdrop.documentSpace = finiteDocumentBounds;
    checkerBackdrop.color1 = m_checkerColor1;
    checkerBackdrop.color2 = m_checkerColor2;
    checkerBackdrop.viewportColor = m_backgroundColor;
    checkerBackdrop.size = m_checkerSize;

    const GLuint targetFbo = sceneTarget == m_sceneFboManager.sceneFbo()
        ? sceneTarget
        : static_cast<GLuint>(defaultFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, viewportWidth, viewportHeight);

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
    glScissor(clippedDrawBounds.x(),
        viewportHeight - clippedDrawBounds.y() - clippedDrawBounds.height(),
        clippedDrawBounds.width(), clippedDrawBounds.height());
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    viewportCompositor->drawTexture(m_fillPreview.finalCompositeTexture, clipParams,
        GLViewportCompositor::LassoMaskParams {}, /*replaceWithCoverage=*/true, radialReveal,
        checkerBackdrop);
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

    if (targetFbo == m_sceneFboManager.sceneFbo()) {
        m_sceneFboManager.blitToDefaultFbo(this, defaultFbo, viewportWidth, viewportHeight);
    }
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
        session.screenSourcesDirty = true;
        // Screen coordinates are camera-derived, so a camera move is the one
        // thing that invalidates the whole projected polygon (and with it the
        // accumulated mask, via forceRebuild below).
        session.polygonScreen.clear();
        session.screenBoundsValid = false;
        session.maskRebuildPending = true;
        m_layerScreenSourceCache->invalidateByViewport();
    }
    if (session.viewportRevision == 0) {
        session.viewportRevision = 1;
    }

    if (session.polygonScreen.size() > session.polygonWorld.size()) {
        session.polygonScreen.clear();
        session.screenBoundsValid = false;
        session.maskRebuildPending = true;
    }
    // Project only what is new. Everything before this index was projected on an
    // earlier frame under the same camera and cannot have moved.
    for (std::size_t i = session.polygonScreen.size(); i < session.polygonWorld.size(); ++i) {
        const Vector2 point = screenFromDocumentWorld(session.polygonWorld[i]);
        session.polygonScreen.push_back(point);
        if (!session.screenBoundsValid) {
            session.screenMinX = point.x;
            session.screenMaxX = point.x;
            session.screenMinY = point.y;
            session.screenMaxY = point.y;
            session.screenBoundsValid = true;
        } else {
            session.screenMinX = std::min(session.screenMinX, point.x);
            session.screenMinY = std::min(session.screenMinY, point.y);
            session.screenMaxX = std::max(session.screenMaxX, point.x);
            session.screenMaxY = std::max(session.screenMaxY, point.y);
        }
    }

    if (session.polygonScreen.size() < 3 || !session.screenBoundsValid) {
        session.clippedScreenBounds = {};
        return;
    }

    // The polygon is clipped to the canvas on the GPU, as a stencil gate on the
    // mask, rather than geometrically on the point list — geometric clipping
    // inserts and drops vertices, which would break the append-only accumulation
    // for no visual difference (the pad along a clip edge falls inside the fill).
    const bool finiteDocumentBounds = hasFiniteDocumentBounds();
    GLLassoMaskRenderer::CanvasGate canvasGate;
    if (finiteDocumentBounds && m_canvas.width() > 0 && m_canvas.height() > 0) {
        const auto documentWidth = static_cast<float>(m_canvas.width());
        const auto documentHeight = static_cast<float>(m_canvas.height());
        canvasGate.enabled = true;
        canvasGate.corners = { { screenFromDocumentWorld({ 0.0f, 0.0f }),
            screenFromDocumentWorld({ documentWidth, 0.0f }),
            screenFromDocumentWorld({ documentWidth, documentHeight }),
            screenFromDocumentWorld({ 0.0f, documentHeight }) } };
    }

    {
        constexpr int kLassoFillMaskPaddingPx = 2;
        const QRect viewportRect(0, 0, viewportWidth, viewportHeight);
        const int floorX = static_cast<int>(std::floor(session.screenMinX));
        const int floorY = static_cast<int>(std::floor(session.screenMinY));
        const QRect polygonRect(floorX - kLassoFillMaskPaddingPx, floorY - kLassoFillMaskPaddingPx,
            std::max(1,
                static_cast<int>(std::ceil(session.screenMaxX)) - floorX + 1
                    + kLassoFillMaskPaddingPx * 2),
            std::max(1,
                static_cast<int>(std::ceil(session.screenMaxY)) - floorY + 1
                    + kLassoFillMaskPaddingPx * 2));
        session.clippedScreenBounds = polygonRect.intersected(viewportRect);
        if (canvasGate.enabled) {
            // The gate zeroes the mask outside the canvas, so nothing beyond the
            // document's screen box can differ from the scene. Keeps the redrawn
            // region as tight as the geometric clip used to make it.
            float gateMinX = canvasGate.corners[0].x;
            float gateMinY = canvasGate.corners[0].y;
            float gateMaxX = gateMinX;
            float gateMaxY = gateMinY;
            for (const Vector2& corner : canvasGate.corners) {
                gateMinX = std::min(gateMinX, corner.x);
                gateMinY = std::min(gateMinY, corner.y);
                gateMaxX = std::max(gateMaxX, corner.x);
                gateMaxY = std::max(gateMaxY, corner.y);
            }
            const QRect gateRect(static_cast<int>(std::floor(gateMinX)),
                static_cast<int>(std::floor(gateMinY)),
                std::max(1,
                    static_cast<int>(std::ceil(gateMaxX)) - static_cast<int>(std::floor(gateMinX))
                        + 1),
                std::max(1,
                    static_cast<int>(std::ceil(gateMaxY)) - static_cast<int>(std::floor(gateMinY))
                        + 1));
            session.clippedScreenBounds = session.clippedScreenBounds.intersected(gateRect);
        }
    }

    if (!session.clippedScreenBounds.isValid() || session.clippedScreenBounds.isEmpty()) {
        return;
    }

    const auto maskResult = maskRenderer->accumulate(session.polygonScreen,
        QSize(viewportWidth, viewportHeight), canvasGate, session.maskRebuildPending);
    if (!maskResult.isValid()) {
        session.screenMaskTexture = 0;
        session.screenMaskBounds = {};
        return;
    }
    session.maskRebuildPending = false;
    session.screenMaskTexture = maskResult.texture;
    session.screenMaskBounds = maskResult.bounds;

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

    // The region to redraw is the lasso's own box, NOT the mask texture's bounds:
    // the mask spans the whole viewport so that it can be accumulated in place.
    QRect maskBounds = session.clippedScreenBounds;
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
    const auto& lassoSelection = m_selectionController->lassoSelection();
    const auto& edges = lassoSelection.edges();
    const std::vector<LassoEdgeSegment>* edgesToRender = &edges;
    std::vector<LassoEdgeSegment> transformedEdges;
    uint64_t edgesRevision = lassoSelection.edgesRevision();
    if (const TransformState* displayState = selectionDisplayTransformState();
        displayState && !edges.empty()) {
        transformedEdges.reserve(edges.size());
        for (const auto& seg : edges) {
            transformedEdges.push_back(
                { displayState->transformPoint(seg.a), displayState->transformPoint(seg.b) });
        }
        edgesToRender = &transformedEdges;
        // The transform preview changes without rebuilding the selection mask.
        // Revision zero tells the overlay that these transient endpoints must
        // be uploaded again for this frame.
        edgesRevision = 0;
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
    lassoOverlay->render(m_viewport, lassoPoints, activeClosed, *edgesToRender, edgesRevision,
        edgesAlpha, addPathMaskTexture, maskOriginX, maskOriginY, maskWidth, maskHeight,
        pathAlphaInside, pathAlphaOutside, &contentVp);
    if (lassoOverlay->isAnimating())
        update();
}

void OpenGLCanvasWidget::paintGL()
{
    if (!m_initialized || !m_renderer)
        return;

    // The GL cursor is part of this frame, so it can only be as fresh as the
    // frame is. Take the pointer position from the OS/tablet here instead of
    // relying on a MouseMove having been delivered: mouse messages are the
    // lowest-priority thing in the Windows queue and a busy posted-event queue
    // (an undo/redo burst is exactly that) starves them, while timers and
    // repaints — the navigator's animation, this very frame — keep running.
    paintGL_syncCursorToLivePointer();

    // Service the stroke input queue before anything reads the document. A frame
    // is the deadline the queue is scheduled against: everything the pen has
    // produced since the previous frame is rasterized now, so the layer stack
    // built below already contains it and the oldest unconsumed sample can never
    // be older than one frame. Doing this from a timer instead made the queue's
    // service rate depend on the repaint it asked for, which is how a fast
    // tablet accumulated a second of drawing lag.
    if (m_strokeHost) {
        m_strokeHost->drainStrokeInputForFrame();
    }

    if (m_smartCompositeRefreshPending) {
        // A smart object's contents changed while there was no renderer to
        // flatten them. There is one now — but a composite is its own batch and
        // this one makes/releases the context, so it runs from the event loop
        // rather than inside this frame. One frame of last-good pixels.
        m_smartCompositeRefreshPending = false;
        QTimer::singleShot(0, this, [this]() { refreshSmartContentComposites(); });
    }

    flushPendingFillPreviewTextureDeletes();
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
    auto* toolCursorOverlay = m_overlayManager ? m_overlayManager->toolCursorOverlay() : nullptr;
    auto* textEditOverlay = m_overlayManager ? m_overlayManager->textEditOverlay() : nullptr;
    const bool wantBrushCursor = !m_skipCursorOverlays && brushCursorOverlay
        && m_cursorOverlayState.brushVisible && m_cursorOverlayState.brushRadius > 0.5f;
    const bool wantEyedropperCursor = !m_skipCursorOverlays && eyedropperCursorOverlay
        && m_cursorOverlayState.eyedropperVisible;
    const bool wantToolCursor
        = !m_skipCursorOverlays && toolCursorOverlay && m_cursorOverlayState.toolCursorVisible;
    if (wantBrushCursor) {
        ensureCursorOverlayInitialized(brushCursorOverlay, "brush cursor overlay");
    }
    if (wantEyedropperCursor) {
        ensureCursorOverlayInitialized(eyedropperCursorOverlay, "eyedropper cursor overlay");
    }
    if (wantToolCursor) {
        ensureCursorOverlayInitialized(toolCursorOverlay, "tool cursor overlay");
    }
    const bool moveAxisGuideActivePre = m_transformController.moveAxisGuideActive();
    const bool autoSnapGuideActivePre = m_transformController.snapVisualState().active();
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
    const bool drawToolCursor
        = wantToolCursor && toolCursorOverlay && toolCursorOverlay->isInitialized();
    // Most overlays genuinely consume the whole scene texture. A cursor does
    // not: it inverts what is under itself, so it only ever samples the small
    // rectangle it draws into and can work from a local copy taken after the
    // scene has rendered directly to the target. This avoids a full-surface
    // offscreen render + blit on every cursor frame, which is especially costly
    // at maximized-window resolutions. The brush ring already worked this way;
    // the tool and eyedropper cursors used to force the full-scene path.
    const bool needFullSceneForOverlay
        = drawTransformOverlay || drawCanvasResizeOverlay || drawTextEditOverlay;
    std::array<CursorCaptureRect, 3> cursorCaptureRects {};
    int cursorCaptureRectCount = 0;
    if (!needFullSceneForOverlay) {
        if (drawBrushCursor) {
            // The cursor shader uses linear sampling and its contour is two
            // pixels wide. Keep a small guard band so edge texels never sample
            // stale content from outside the copied rectangle.
            constexpr float kCursorCapturePaddingPx = 3.0f;
            const float r = m_cursorOverlayState.brushRadius + kCursorCapturePaddingPx;
            cursorCaptureRects[cursorCaptureRectCount++]
                = CursorCaptureRect { m_cursorOverlayState.brushCenterX - r,
                      m_cursorOverlayState.brushCenterY - r, m_cursorOverlayState.brushCenterX + r,
                      m_cursorOverlayState.brushCenterY + r };
        }
        if (drawEyedropperCursor) {
            cursorCaptureRects[cursorCaptureRectCount++] = EyedropperCursorOverlayGL::captureRect(
                m_cursorOverlayState.eyedropperCenterX, m_cursorOverlayState.eyedropperCenterY);
        }
        if (drawToolCursor) {
            cursorCaptureRects[cursorCaptureRectCount++]
                = ToolCursorOverlayGL::captureRect(m_cursorOverlayState.toolCursorCenterX,
                    m_cursorOverlayState.toolCursorCenterY, m_cursorOverlayState.toolCursorStyle);
        }
    }

    GLint defaultFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &defaultFbo);
    GLuint sceneTarget = 0;
    paintGL_renderSceneAndBlit(sceneTarget, defaultFbo, needFullSceneForOverlay, boardLayerStack);

    paintGL_renderFillPreviewOverlay(layerStack, sceneTarget, defaultFbo);

    if (cursorCaptureRectCount > 0) {
        const QSize surfaceSize = currentSurfacePixelSize(this);
        const int surfaceWidth = surfaceSize.width();
        const int surfaceHeight = surfaceSize.height();
        m_sceneFboManager.ensureSceneFbo(this, surfaceWidth, surfaceHeight);
        if (m_sceneFboManager.sceneFbo() && m_sceneFboManager.sceneTexture()) {
            for (int i = 0; i < cursorCaptureRectCount; ++i) {
                const CursorCaptureRect& rect = cursorCaptureRects[i];
                const int left
                    = std::clamp(static_cast<int>(std::floor(rect.left)), 0, surfaceWidth);
                const int right
                    = std::clamp(static_cast<int>(std::ceil(rect.right)), 0, surfaceWidth);
                const int top
                    = std::clamp(static_cast<int>(std::floor(rect.top)), 0, surfaceHeight);
                const int bottom
                    = std::clamp(static_cast<int>(std::ceil(rect.bottom)), 0, surfaceHeight);
                if (right <= left || bottom <= top) {
                    continue;
                }
                const int glBottom = surfaceHeight - bottom;
                m_sceneFboManager.copyRegionFromDefaultFbo(
                    this, defaultFbo, left, glBottom, right - left, bottom - top);
                sceneTarget = m_sceneFboManager.sceneFbo();
            }
        }
    }

    paintGL_renderTransformViewportPreview(layerStack, boardLayerStack, defaultFbo);

    paintGL_renderOverlays(sceneTarget);

    paintGL_processSelectionReadback();

    paintGL_renderLassoFillOverlay(layerStack, boardLayerStack, defaultFbo);
    paintGL_renderLassoOverlay();

    paintGL_renderCursorOverlays();

    // Same-frame ROI blur for visible canvas widgets. It must run AFTER every
    // pass that writes canvas pixels: the screen-space previews (transform,
    // lasso fill) re-render the viewport into the scene FBO and blit it over the
    // whole surface, which erased a blur drawn earlier in the frame. Reading the
    // default framebuffer here (instead of the scene FBO) keeps the source in
    // sync with what those passes actually left on screen, including the passes
    // that draw straight to the default target.
    paintGL_renderBackdrop(defaultFbo);

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
    // A pyramid tile that missed its rebuild draws stale rather than aliased,
    // so this is a catch-up frame, not a quality fallback being undone.
    if (m_renderer->hasPendingDisplayPyramidWork())
        update();
    // The GL cursor exists only inside this frame, and a held-down undo/redo
    // drains its queue through posted events, which Windows serves ahead of
    // mouse input: no MouseMove arrives to ask for a frame. Overlays that
    // animate (marching ants) or tools that repaint for their own reasons (the
    // brush ring) hide this by keeping frames coming; the plain tool cursor has
    // no such motor and was redrawn once per undo step, which reads as frozen.
    // So keep the canvas ticking for a moment after each step — every frame
    // re-samples the live pointer, so that is what makes the cursor move.
    // The pending count cannot drive this: one key repeat enqueues one step and
    // it is consumed at once, so the queue is empty most of the burst. Both
    // clocks are needed, and the window runs from whichever is newer: requests
    // cover the steps that apply nothing (the first one while its command is
    // still being prepared on a worker, and every repeat after the stack runs
    // out), applications cover a preparation that outlasts the window.
    constexpr qint64 kUndoActivityFrameWindowMs = 250;
    const auto& undoManager = m_canvas.undoManager();
    const qint64 msSinceUndoRequest = undoManager.msSinceLastRequestedOperation();
    const qint64 msSinceUndoApplied = undoManager.msSinceLastAppliedOperation();
    const bool undoActivityRecent
        = (msSinceUndoRequest >= 0 && msSinceUndoRequest < kUndoActivityFrameWindowMs)
        || (msSinceUndoApplied >= 0 && msSinceUndoApplied < kUndoActivityFrameWindowMs);
    if (undoManager.hasPendingOperations() || undoActivityRecent)
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
