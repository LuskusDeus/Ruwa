// SPDX-License-Identifier: MPL-2.0

// WorkspaceTab.cpp
#include "WorkspaceTab.h"

#include "app/Application.h"
#include "features/layers/model/LayerModel.h"
#include "features/effects/EffectCoverageResolver.h"
#include "features/transform/TransformState.h"
#include "platform/Platform.h"
#include "features/project/ProjectSerializer.h"
#include "features/project/RecentProjectsManager.h"
#include "features/project/ThumbnailCache.h"
#include "features/settings/SettingsManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/widgets/DotGridLoadingIndicator.h"
#include "shell/docking/Docking.h"
#include "shell/docking/state/DockLayoutPresetStore.h"
#include "features/layers/ui/LayersPanel.h"
#include "features/layers/ui/LayerPropertiesPanel.h"
#include "features/layers/ui/LayerEffectsPanel.h"
#include "shared/widgets/inputs/PositionInputField.h"
#include "features/tools/ToolsPanel.h"
#include "features/brush/ui/BrushesPanel.h"
#include "features/brush/ui/BrushSettingsPanel.h"
#include "features/color/ColorPanel.h"
#include "features/color/RecentColorsPersistence.h"
#include "features/canvas/ui/CanvasPanel.h"
#include "features/canvas/ui/CanvasPanelHelpers.h"
#include "features/canvas/ui/NavigatorPanel.h"
#include "features/canvas/ui/NavigatorWidget.h"
#include "shared/undo/UndoManager.h"
#include "shared/tiles/TileTypes.h"
#include "shared/tiles/TileFormat.h"
#include "shared/widgets/CapsuleButton.h"
#include "shell/tab-system/TabManager.h"
#include "shell/top-bar/MessagePopupManager.h"

#include <QCoreApplication>
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QGraphicsOpacityEffect>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QColor>
#include <QPalette>
#include <QSettings>
#include <QCursor>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QImage>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QPushButton>
#include <QSet>
#include <QStyle>
#include <QtConcurrent>

#include <cmath>
#include <memory>

namespace ruwa::ui::tabs {

using ruwa::core::effects::LayerEffectState;
using ruwa::core::layers::LayerData;
using ruwa::core::layers::LayerId;
using ruwa::core::layers::LayerType;

namespace {

/// Force a widget and its whole subtree to re-evaluate the (already updated)
/// global stylesheet and repaint with the new palette. Used by the theme
/// refresh path to bring a tab to a freshly-opened appearance WITHOUT tearing
/// down its OpenGL canvas (destroying a QOpenGLWidget mid-flight corrupts the
/// top-level composited surface and blacks out the whole window on Windows).
void repolishThemeRecursive(QWidget* widget)
{
    if (!widget) {
        return;
    }
    if (QStyle* style = widget->style()) {
        style->unpolish(widget);
        style->polish(widget);
    }
    widget->update();
    const QList<QWidget*> children = widget->findChildren<QWidget*>(Qt::FindDirectChildrenOnly);
    for (QWidget* child : children) {
        repolishThemeRecursive(child);
    }
}

QString colorDebugString(const QColor& color)
{
    return QStringLiteral("%1(0x%2)")
        .arg(color.name(QColor::HexArgb),
            QString::number(color.rgba(), 16).rightJustified(8, QLatin1Char('0')));
}

QString workspaceColorStateDebugString(const WorkspaceTab::WorkspaceColorState& state)
{
    return QStringLiteral("{fg=%1 bg=%2 slot=%3}")
        .arg(colorDebugString(state.foreground), colorDebugString(state.background),
            state.editingForeground ? QStringLiteral("fg") : QStringLiteral("bg"));
}

constexpr auto kWorkspaceDockLayoutKey = "Workspace/userDockLayout";
constexpr auto kWorkspaceBrushOverlayPosKey = "Workspace/brushControlOverlayPos";
constexpr auto kWorkspaceToolStateOverlayPosKey = "Workspace/toolStateOverlayPos";
constexpr auto kWorkspaceStylusJoystickPosKey = "Workspace/stylusJoystickPos";
constexpr auto kWorkspaceStylusJoystickAbovePanelKey = "Workspace/stylusJoystickAbovePanel";
/// QSettings key holding one canvas widget's visibility. The strings are part
/// of the on-disk settings format — keep them as they are.
const char* canvasWidgetVisibleKey(ruwa::ui::CanvasWidget widget)
{
    switch (widget) {
    case ruwa::ui::CanvasWidget::Joystick:
        return "Workspace/canvasWidgetsJoystickVisible";
    case ruwa::ui::CanvasWidget::BrushControl:
        return "Workspace/canvasWidgetsBrushControlVisible";
    case ruwa::ui::CanvasWidget::ToolState:
        return "Workspace/canvasWidgetsToolStateOverlayVisible";
    }
    return "";
}
constexpr auto kWorkspaceForegroundColorKey = "Workspace/foregroundColorRgba";
constexpr auto kWorkspaceBackgroundColorKey = "Workspace/backgroundColorRgba";
constexpr auto kWorkspaceEditingForegroundColorKey = "Workspace/editingForegroundColor";
// Persistent panel keys are part of the serialized dock-state format and must remain stable
// across user-facing renames. "composer" is the compatibility ID of the Navigator panel.
constexpr auto kNavigatorPanelPersistentKey = "composer";
constexpr auto kUiDragActiveProperty = "ruwa_ui_drag_active";
constexpr int kTileRestoreBatchBudgetMs = 6;
constexpr int kTileRestoreBatchMaxTiles = 96;

WorkspaceTab::NavigatorTrackedLayerState captureNavigatorTrackedLayerState(const LayerData* layer)
{
    WorkspaceTab::NavigatorTrackedLayerState state;
    if (!layer) {
        return state;
    }

    state.visible = layer->visible;
    state.opacity = layer->opacity;
    state.blendMode = static_cast<int>(layer->blendMode);
    state.groupCompositingMode = static_cast<int>(layer->groupCompositingMode);
    state.clippedToBelow = layer->clippedToBelow;
    state.backgroundTransparent = layer->backgroundTransparent;
    state.backgroundColor = layer->backgroundColor;
    state.isBackground = layer->isBackground();
    state.isGroup = layer->isGroup();
    state.isPixelLayer = layer->isPixelLayer();
    state.hasRetainedVisualContent = layer->hasRetainedVisualContent();
    state.effectChainRevision = layer->effectChainRevision;
    state.effects = layer->effects;
    return state;
}

bool affectsNavigatorOverview(const WorkspaceTab::NavigatorTrackedLayerState& lhs,
    const WorkspaceTab::NavigatorTrackedLayerState& rhs)
{
    return lhs.visible != rhs.visible || !qFuzzyCompare(lhs.opacity, rhs.opacity)
        || lhs.blendMode != rhs.blendMode || lhs.groupCompositingMode != rhs.groupCompositingMode
        || lhs.clippedToBelow != rhs.clippedToBelow
        || lhs.backgroundTransparent != rhs.backgroundTransparent
        || lhs.backgroundColor != rhs.backgroundColor || lhs.isBackground != rhs.isBackground
        || lhs.isGroup != rhs.isGroup || lhs.isPixelLayer != rhs.isPixelLayer
        || lhs.hasRetainedVisualContent != rhs.hasRetainedVisualContent;
}

bool effectResultAffectsNavigator(
    const QList<LayerEffectState>& lhs, const QList<LayerEffectState>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return true;
    }

    for (int i = 0; i < lhs.size(); ++i) {
        const LayerEffectState& before = lhs.at(i);
        const LayerEffectState& after = rhs.at(i);
        if (before.enabled != after.enabled || before.typeId != after.typeId
            || before.version != after.version || before.params != after.params) {
            return true;
        }
    }

    return false;
}

bool affectsNavigatorOverviewExceptOpacity(const WorkspaceTab::NavigatorTrackedLayerState& lhs,
    const WorkspaceTab::NavigatorTrackedLayerState& rhs)
{
    return lhs.visible != rhs.visible || lhs.blendMode != rhs.blendMode
        || lhs.groupCompositingMode != rhs.groupCompositingMode
        || lhs.clippedToBelow != rhs.clippedToBelow
        || lhs.backgroundTransparent != rhs.backgroundTransparent
        || lhs.backgroundColor != rhs.backgroundColor || lhs.isBackground != rhs.isBackground
        || lhs.isGroup != rhs.isGroup || lhs.isPixelLayer != rhs.isPixelLayer
        || lhs.hasRetainedVisualContent != rhs.hasRetainedVisualContent;
}

void collectLayerTilePositions(const LayerData* layer, QSet<QPoint>& out, bool recursive)
{
    if (!layer) {
        return;
    }

    if (layer->isIsolatedPixelLayer()) {
        const auto* pixelGrid = layer->smartContentGrid.get();
        if (pixelGrid && !pixelGrid->empty()) {
            aether::TransformState state = layer->smartTransform;
            if (state.contentBounds.width <= 0.0f || state.contentBounds.height <= 0.0f) {
                state.contentBounds = aether::TransformState::computeContentBounds(*pixelGrid);
                state.pivot = state.contentBounds.center();
            }

            aether::Rect bounds = state.transformedAABB();
            if (bounds.width > 0.0f && bounds.height > 0.0f) {
                constexpr float kTransformTileMargin = 2.0f;
                bounds.x -= kTransformTileMargin;
                bounds.y -= kTransformTileMargin;
                bounds.width += kTransformTileMargin * 2.0f;
                bounds.height += kTransformTileMargin * 2.0f;

                const float tileSize = static_cast<float>(aether::TILE_SIZE);
                const int minX = static_cast<int>(std::floor(bounds.left() / tileSize));
                const int minY = static_cast<int>(std::floor(bounds.top() / tileSize));
                const int maxX = static_cast<int>(std::ceil(bounds.right() / tileSize)) - 1;
                const int maxY = static_cast<int>(std::ceil(bounds.bottom() / tileSize)) - 1;

                for (int y = minY; y <= maxY; ++y) {
                    for (int x = minX; x <= maxX; ++x) {
                        out.insert(QPoint(x, y));
                    }
                }
            }
        }
    } else if (const auto* pixelGrid = layer->pixelGrid()) {
        for (const auto& [key, tile] : pixelGrid->tiles()) {
            Q_UNUSED(tile);
            out.insert(QPoint(key.x, key.y));
        }
    }

    if (!recursive) {
        return;
    }

    for (const auto& child : layer->children) {
        collectLayerTilePositions(child.get(), out, true);
    }
}

QList<QPoint> effectExpandedLayerTilePositions(
    const LayerData* layer, const QList<LayerEffectState>& effects, bool recursive)
{
    QSet<QPoint> positions;
    collectLayerTilePositions(layer, positions, recursive);
    if (positions.isEmpty() || effects.isEmpty()) {
        return positions.values();
    }

    ruwa::core::effects::EffectCoverageResolver::TileKeySet coverage;
    coverage.reserve(static_cast<size_t>(positions.size()));
    for (const QPoint& position : positions) {
        coverage.insert(aether::TileKey { position.x(), position.y() });
    }

    const auto expanded
        = ruwa::core::effects::EffectCoverageResolver::expandedDocumentCoverage(coverage, effects);
    QSet<QPoint> expandedPositions;
    expandedPositions.reserve(static_cast<int>(expanded.size()));
    for (const aether::TileKey& key : expanded) {
        expandedPositions.insert(QPoint(key.x, key.y));
    }
    return expandedPositions.values();
}

QList<QPoint> navigatorEffectChangedTilePositions(const LayerData* layer,
    const QList<LayerEffectState>& beforeEffects, const QList<LayerEffectState>& afterEffects)
{
    QSet<QPoint> positions;
    for (const QPoint& position : effectExpandedLayerTilePositions(layer, beforeEffects, true)) {
        positions.insert(position);
    }
    for (const QPoint& position : effectExpandedLayerTilePositions(layer, afterEffects, true)) {
        positions.insert(position);
    }
    return positions.values();
}

ruwa::core::serialization::LayerEntry::SerializedVec2 toSerializedVec2(const aether::Vector2& value)
{
    return { value.x, value.y };
}

ruwa::core::serialization::LayerEntry::SerializedRect toSerializedRect(const aether::Rect& value)
{
    return { value.x, value.y, value.width, value.height };
}

const aether::TransformState& serializedTransformForLayer(const LayerData* layer)
{
    if (layer && layer->isText() && layer->textData) {
        return layer->textData->transform;
    }
    return layer->smartTransform;
}

void writeLayerTransformFields(
    ruwa::core::serialization::LayerEntry& entry, const aether::TransformState& transform)
{
    entry.contentBounds = toSerializedRect(transform.contentBounds);
    entry.translation = toSerializedVec2(transform.translation);
    entry.rotation = transform.rotation;
    entry.scale = toSerializedVec2(transform.scale);
    entry.pivot = toSerializedVec2(transform.pivot);
    entry.hasFreeCorners = transform.freeCorners.has_value();
    if (entry.hasFreeCorners) {
        const auto& corners = *transform.freeCorners;
        for (int i = 0; i < 4; ++i) {
            entry.freeCorners[static_cast<size_t>(i)]
                = toSerializedVec2(corners[static_cast<size_t>(i)]);
        }
    }
    entry.hasDeformMesh = transform.deformMesh.has_value();
    if (entry.hasDeformMesh) {
        const auto& mesh = *transform.deformMesh;
        entry.deformLatticeRows = mesh.rows;
        entry.deformLatticeCols = mesh.cols;
        entry.deformVertices.reserve(static_cast<int>(mesh.vertices.size()));
        for (const auto& vertex : mesh.vertices) {
            entry.deformVertices.append(
                { toSerializedVec2(vertex.source), toSerializedVec2(vertex.target) });
        }
    }
}

void writeTextLayerFields(ruwa::core::serialization::LayerEntry& entry, const LayerData* layer)
{
    if (!layer || !layer->isText() || !layer->textData) {
        return;
    }

    entry.hasTextPayload = true;
    entry.text = layer->textData->text;
    entry.textFontFamily = layer->textData->fontFamily;
    entry.textFontSize = layer->textData->fontSize;
    entry.textColorRgba = layer->textData->color.rgba();
    entry.textAlignment = static_cast<int>(layer->textData->alignment);
    entry.textLineHeight = layer->textData->lineHeight;
    entry.textStyleRuns.reserve(layer->textData->styleRuns.size());
    for (const auto& run : layer->textData->styleRuns) {
        entry.textStyleRuns.append({ run.start, run.length, run.fontFamily, run.fontSize,
            run.color.rgba(), run.bold, run.italic, run.underline });
    }
}

ruwa::core::serialization::ProjectData::ExportFrame normalizedExportFrame(
    ruwa::core::serialization::ProjectData::ExportFrame frame, const QSize& legacyCanvasSize)
{
    if (!frame.isValid()) {
        frame.enabled = true;
        frame.rect = QRect(0, 0, legacyCanvasSize.width(), legacyCanvasSize.height());
    }
    return frame;
}

struct SaveProjectPayload {
    QString filePath;
    ruwa::core::serialization::ProjectData data;
    QImage thumbnail;
};

struct SnapshotSaveProjectPayload {
    QString filePath;
    WorkspaceTab::ProjectSaveSnapshot snapshot;
};

struct SaveProjectResult {
    bool success = false;
    QString errorMessage;
};

SaveProjectResult saveProjectInBackground(SaveProjectPayload payload)
{
    SaveProjectResult result;

    ruwa::core::serialization::ProjectSerializer serializer;
    if (!serializer.save(payload.filePath, payload.data)) {
        result.errorMessage = serializer.lastError();
        return result;
    }

    if (!payload.thumbnail.isNull()) {
        ruwa::core::serialization::ThumbnailCache::instance().save(
            payload.filePath, payload.thumbnail, 256);
    }

    result.success = true;
    return result;
}

ruwa::core::serialization::LayerEntry buildLayerEntryFromSnapshot(
    const std::shared_ptr<ruwa::core::layers::LayerData>& layer)
{
    using namespace ruwa::core::serialization;

    LayerEntry entry;
    if (!layer) {
        return entry;
    }

    entry.id = layer->id;
    entry.name = layer->name;
    entry.type = static_cast<int>(layer->type);
    entry.visible = layer->visible;
    entry.locked = layer->locked;
    entry.expanded = layer->expanded;
    entry.opacity = layer->opacity;
    entry.blendMode = static_cast<int>(layer->blendMode);
    entry.groupCompositingMode = static_cast<int>(layer->groupCompositingMode);
    entry.displayColorIndex = layer->displayColorIndex;
    entry.backgroundColorRgba = layer->backgroundColor.rgba();
    entry.backgroundTransparent = layer->backgroundTransparent;
    entry.clippedToBelow = layer->clippedToBelow;
    writeLayerTransformFields(entry, serializedTransformForLayer(layer.get()));
    writeTextLayerFields(entry, layer.get());

    if (const auto* pixelGrid = layer->pixelGrid(); pixelGrid) {
        const int contentTileBytes = static_cast<int>(aether::tileByteSize(pixelGrid->format()));
        entry.tiles.reserve(static_cast<int>(pixelGrid->tiles().size()));
        for (const auto& [key, tile] : pixelGrid->tiles()) {
            ruwa::core::serialization::TileEntry tileEntry;
            tileEntry.x = key.x;
            tileEntry.y = key.y;
            tileEntry.pixels
                = QByteArray(reinterpret_cast<const char*>(tile.pixels()), contentTileBytes);
            entry.tiles.append(std::move(tileEntry));
        }
    }

    if (layer->hasMask()) {
        entry.hasMask = true;
        entry.maskEnabled = layer->maskEnabled;
        entry.maskLinked = layer->maskLinked;
        if (const auto* maskGrid = layer->maskTileGrid()) {
            entry.maskDefaultFill = maskGrid->defaultFillPacked();
            entry.maskTiles.reserve(static_cast<int>(maskGrid->tiles().size()));
            for (const auto& [key, tile] : maskGrid->tiles()) {
                ruwa::core::serialization::TileEntry tileEntry;
                tileEntry.x = key.x;
                tileEntry.y = key.y;
                if (tile.isSolid()) {
                    tileEntry.solid = true;
                    tileEntry.solidColor = tile.solidColorPacked();
                } else {
                    tileEntry.pixels = QByteArray(reinterpret_cast<const char*>(tile.pixels()),
                        static_cast<int>(aether::TILE_BYTE_SIZE));
                }
                entry.maskTiles.append(std::move(tileEntry));
            }
        }
    }

    entry.children.reserve(layer->children.size());
    for (const auto& child : layer->children) {
        entry.children.append(buildLayerEntryFromSnapshot(child));
    }

    return entry;
}

ruwa::core::serialization::ProjectData buildProjectDataFromSnapshot(
    const WorkspaceTab::ProjectSaveSnapshot& snapshot)
{
    using namespace ruwa::core::serialization;

    ProjectData data;
    data.projectName = snapshot.projectName;
    data.tabTitle = snapshot.tabTitle;
    data.tabIconAlias = snapshot.tabIconAlias;
    data.canvasSize = snapshot.canvasSize;
    data.canvasBoundsMode = snapshot.canvasBoundsMode;
    data.exportFrame = normalizedExportFrame(snapshot.exportFrame, snapshot.canvasSize);
    data.contentTileFormat = snapshot.tileFormat;
    data.currentTool = snapshot.currentTool;
    data.brushToolState = snapshot.brushToolState;
    data.eraserToolState = snapshot.eraserToolState;
    data.blurToolState = snapshot.blurToolState;
    data.smudgeToolState = snapshot.smudgeToolState;
    data.lassoStabilization = snapshot.lassoStabilization;
    data.lassoFillStabilization = snapshot.lassoFillStabilization;
    data.lastUsedColorRgba = snapshot.lastUsedColorRgba;
    data.foregroundColorRgba = snapshot.foregroundColorRgba;
    data.backgroundColorRgba = snapshot.backgroundColorRgba;
    data.editingForegroundColor = snapshot.editingForegroundColor;
    data.dockLayoutState = snapshot.dockLayoutState;
    data.brushOverlayPosNormalized = snapshot.brushOverlayPosNormalized;
    data.toolStateOverlayPosNormalized = snapshot.toolStateOverlayPosNormalized;
    data.stylusJoystickPosNormalized = snapshot.stylusJoystickPosNormalized;
    data.stylusJoystickAbovePanel = snapshot.stylusJoystickAbovePanel;
    data.canvasWidgets = snapshot.canvasWidgets;
    data.selectedLayerId = snapshot.selectedLayerId;

    data.rootLayers.reserve(snapshot.rootLayers.size());
    for (const auto& root : snapshot.rootLayers) {
        data.rootLayers.append(buildLayerEntryFromSnapshot(root));
    }
    for (const auto& root : snapshot.rootLayers) {
        std::function<void(const std::shared_ptr<ruwa::core::layers::LayerData>&)> collectEffects;
        collectEffects = [&data, &collectEffects](
                             const std::shared_ptr<ruwa::core::layers::LayerData>& layer) {
            if (!layer) {
                return;
            }
            if (!layer->effects.isEmpty()) {
                data.layerEffects.append({ layer->id, layer->effects });
            }
            for (const auto& child : layer->children) {
                collectEffects(child);
            }
        };
        collectEffects(root);
    }

    return data;
}

SaveProjectResult saveProjectSnapshotInBackground(SnapshotSaveProjectPayload payload)
{
    SaveProjectResult result;

    ruwa::core::serialization::ProjectSerializer serializer;
    const auto data = buildProjectDataFromSnapshot(payload.snapshot);
    if (!serializer.save(payload.filePath, data)) {
        result.errorMessage = serializer.lastError();
        return result;
    }

    result.success = true;
    return result;
}

void setUiDragActive(bool active)
{
    if (!qApp) {
        return;
    }

    qApp->setProperty(kUiDragActiveProperty, active);
    if (active) {
        qApp->setOverrideCursor(Qt::SizeAllCursor);
    } else if (const QCursor* cursor = qApp->overrideCursor();
        cursor && cursor->shape() == Qt::SizeAllCursor) {
        qApp->restoreOverrideCursor();
    }
}

workspace::ToolsPanel::Tool toToolsPanelTool(workspace::CanvasPanel::ToolMode mode)
{
    switch (mode) {
    case workspace::CanvasPanel::ToolMode::Hand:
        return workspace::ToolsPanel::Tool::Hand;
    case workspace::CanvasPanel::ToolMode::Brush:
        return workspace::ToolsPanel::Tool::Brush;
    case workspace::CanvasPanel::ToolMode::Blur:
        return workspace::ToolsPanel::Tool::Blur;
    case workspace::CanvasPanel::ToolMode::Smudge:
        return workspace::ToolsPanel::Tool::Smudge;
    case workspace::CanvasPanel::ToolMode::Liquify:
        return workspace::ToolsPanel::Tool::Liquify;
    case workspace::CanvasPanel::ToolMode::Eraser:
        return workspace::ToolsPanel::Tool::Eraser;
    case workspace::CanvasPanel::ToolMode::Fill:
        return workspace::ToolsPanel::Tool::Fill;
    case workspace::CanvasPanel::ToolMode::ClassicFill:
        return workspace::ToolsPanel::Tool::ClassicFill;
    case workspace::CanvasPanel::ToolMode::Eyedropper:
        return workspace::ToolsPanel::Tool::Eyedropper;
    case workspace::CanvasPanel::ToolMode::Lasso:
        return workspace::ToolsPanel::Tool::Lasso;
    case workspace::CanvasPanel::ToolMode::LassoFill:
        return workspace::ToolsPanel::Tool::LassoFill;
    case workspace::CanvasPanel::ToolMode::SquareSelection:
        return workspace::ToolsPanel::Tool::SquareSelection;
    case workspace::CanvasPanel::ToolMode::CircleSelection:
        return workspace::ToolsPanel::Tool::CircleSelection;
    case workspace::CanvasPanel::ToolMode::Move:
        return workspace::ToolsPanel::Tool::Move;
    case workspace::CanvasPanel::ToolMode::RotateView:
        return workspace::ToolsPanel::Tool::RotateView;
    case workspace::CanvasPanel::ToolMode::CanvasResize:
        return workspace::ToolsPanel::Tool::CanvasResize;
    case workspace::CanvasPanel::ToolMode::Zoom:
        return workspace::ToolsPanel::Tool::Zoom;
    case workspace::CanvasPanel::ToolMode::Text:
        return workspace::ToolsPanel::Tool::Text;
    default:
        return workspace::ToolsPanel::Tool::Hand;
    }
}

} // namespace

workspace::NavigatorWidget* WorkspaceTab::navigatorWidget() const
{
    if (!m_navigatorPanel || !m_navigatorPanel->contentWidget()) {
        return nullptr;
    }
    return qobject_cast<workspace::NavigatorWidget*>(m_navigatorPanel->contentWidget());
}

void WorkspaceTab::invalidateNavigatorTiles(const QList<QPoint>& tilePositions)
{
    if (tilePositions.isEmpty()) {
        return;
    }
    if (auto* widget = navigatorWidget()) {
        widget->invalidateOverviewTiles(tilePositions);
    }
}

void WorkspaceTab::invalidateNavigatorOverview()
{
    if (auto* widget = navigatorWidget()) {
        widget->invalidateAllOverview();
    }
}

void WorkspaceTab::rebuildNavigatorTrackedLayerStates()
{
    m_navigatorTrackedLayerStates.clear();
    if (!m_layersPanel || !m_layersPanel->layerModel()) {
        return;
    }

    m_layersPanel->layerModel()->forEach([this](LayerData* layer) {
        if (!layer) {
            return;
        }
        m_navigatorTrackedLayerStates.insert(layer->id, captureNavigatorTrackedLayerState(layer));
    });
}

void WorkspaceTab::onNavigatorTrackedLayerChanged(const LayerId& id)
{
    if (!m_layersPanel || !m_layersPanel->layerModel()) {
        return;
    }

    auto* layer = m_layersPanel->layerModel()->layerById(id);
    if (!layer) {
        m_navigatorTrackedLayerStates.remove(id);
        return;
    }

    const NavigatorTrackedLayerState previous
        = m_navigatorTrackedLayerStates.value(id, captureNavigatorTrackedLayerState(layer));
    const NavigatorTrackedLayerState current = captureNavigatorTrackedLayerState(layer);
    m_navigatorTrackedLayerStates.insert(id, current);

    const bool effectChainChanged = previous.effectChainRevision != current.effectChainRevision;
    if (effectChainChanged && effectResultAffectsNavigator(previous.effects, current.effects)) {
        if (current.effects.size() < previous.effects.size()) {
            invalidateNavigatorOverview();
            return;
        }

        if (current.isBackground || previous.isBackground) {
            invalidateNavigatorOverview();
            return;
        }

        const QList<QPoint> tilePositions
            = navigatorEffectChangedTilePositions(layer, previous.effects, current.effects);
        if (!tilePositions.isEmpty()) {
            invalidateNavigatorTiles(tilePositions);
            return;
        }

        invalidateNavigatorOverview();
        return;
    }

    if (!affectsNavigatorOverview(previous, current)) {
        return;
    }

    if (m_pendingNavigatorOpacityCommitIds.contains(id)
        && !affectsNavigatorOverviewExceptOpacity(previous, current)) {
        return;
    }

    if (current.isBackground || previous.isBackground) {
        invalidateNavigatorOverview();
        return;
    }

    const QList<QPoint> tilePositions
        = effectExpandedLayerTilePositions(layer, current.effects, true);
    if (!tilePositions.isEmpty()) {
        invalidateNavigatorTiles(tilePositions);
        return;
    }

    invalidateNavigatorOverview();
}

void WorkspaceTab::onNavigatorTrackedLayerAboutToRemove(const LayerId& id)
{
    if (!m_layersPanel || !m_layersPanel->layerModel()) {
        return;
    }

    if (auto* layer = m_layersPanel->layerModel()->layerById(id)) {
        const QList<QPoint> tilePositions
            = effectExpandedLayerTilePositions(layer, layer->effects, false);
        for (const QPoint& tilePos : tilePositions) {
            m_pendingNavigatorRemovedTiles.insert(tilePos);
        }
        if (tilePositions.isEmpty()
            && (layer->hasRetainedVisualContent()
                || (!layer->isGroup() && !layer->isPixelLayer()))) {
            m_pendingNavigatorFullRefresh = true;
        }
        m_navigatorTrackedLayerStates.remove(id);
    }
}

void WorkspaceTab::onNavigatorOpacityEditStarted(const QUuid& id)
{
    if (!id.isNull()) {
        m_pendingNavigatorOpacityCommitIds.insert(id);
    }
}

void WorkspaceTab::onNavigatorOpacityEditFinished(const QUuid& id, bool changed)
{
    m_pendingNavigatorOpacityCommitIds.remove(id);
    if (!changed || id.isNull() || !m_layersPanel || !m_layersPanel->layerModel()) {
        return;
    }

    auto* layer = m_layersPanel->layerModel()->layerById(id);
    if (!layer) {
        return;
    }

    if (layer->isBackground()) {
        invalidateNavigatorOverview();
        return;
    }

    const QList<QPoint> tilePositions
        = effectExpandedLayerTilePositions(layer, layer->effects, true);
    if (!tilePositions.isEmpty()) {
        invalidateNavigatorTiles(tilePositions);
        return;
    }

    invalidateNavigatorOverview();
}

WorkspaceTab::WorkspaceTab(const ProjectSettings& settings, QWidget* parent)
    : WorkspaceTab(settings, QUuid(), parent)
{
}

WorkspaceTab::WorkspaceTab(const ProjectSettings& settings, const QUuid& id, QWidget* parent)
    : ruwa::core::BaseTab(id, parent)
    , m_theme(this)
    , m_settings(settings)
    , m_projectName(settings.name)
{
    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setSingleShot(false);
    connect(m_autoSaveTimer, &QTimer::timeout, this, [this]() {
        if (!m_saveInProgress && hasFilePath() && isModified()) {
            saveProjectAsync();
        }
    });
    connect(&ruwa::core::SettingsManager::instance(),
        &ruwa::core::SettingsManager::autoSaveIntervalChanged, this,
        [this](int) { startAutoSaveTimer(); });

    m_dockLayoutSaveTimer = new QTimer(this);
    m_dockLayoutSaveTimer->setSingleShot(true);
    m_dockLayoutSaveTimer->setInterval(300);
    connect(m_dockLayoutSaveTimer, &QTimer::timeout, this, [this]() { saveDockLayoutNow(); });

    m_workspaceStateFlushWatcher = new QFutureWatcher<void>(this);
    connect(m_workspaceStateFlushWatcher, &QFutureWatcher<void>::finished, this, [this]() {
        if (m_workspaceStateSyncPending) {
            flushWorkspaceStateSync();
        }
    });

    m_startupImageImportWatcher
        = new QFutureWatcher<ruwa::ui::workspace::detail::ImportedLayerBatch>(this);
    connect(m_startupImageImportWatcher,
        &QFutureWatcher<ruwa::ui::workspace::detail::ImportedLayerBatch>::finished, this, [this]() {
            if (!m_layersPanel) {
                m_pendingStartupImageImportPaths.clear();
                m_pendingStartupImageImports.clear();
                return;
            }

            auto* model = m_layersPanel->layerModel();
            if (!model) {
                m_pendingStartupImageImportPaths.clear();
                m_pendingStartupImageImports.clear();
                return;
            }

            ruwa::ui::workspace::detail::ImportedLayerBatch batch
                = m_startupImageImportWatcher->result();
            m_pendingStartupImageImportPaths.clear();
            m_pendingStartupImageImports.clear();

            m_layersPanel->setInsertAnimationsEnabled(false);
            bool layersRefreshed = false;

            if (!batch.isEmpty()) {
                auto importedLayers = ruwa::ui::workspace::detail::materializeImportedLayers(
                    std::move(batch.layers));
                if (!importedLayers.isEmpty()) {
                    ruwa::ui::workspace::detail::placeImportedSmartLayers(
                        importedLayers, effectiveDisplayFrame(), !isInfiniteCanvas());
                    const auto importedLayerId = importedLayers.constLast()->id;
                    model->addLayers(importedLayers, 0);
                    if (auto* backgroundLayer = model->backgroundLayer()) {
                        model->setLayerVisible(backgroundLayer->id, false);
                    }
                    if (!importedLayerId.isNull()) {
                        model->setSelectedLayer(importedLayerId);
                    }
                    m_layersPanel->refreshLayers();
                    layersRefreshed = true;
                }
            }

            if (!layersRefreshed && model->rootLayers().size() == 1 && model->backgroundLayer()) {
                auto* drawLayer = model->createLayer(tr("Draw here"), 0);
                model->setSelectedLayer(drawLayer->id);
                m_layersPanel->refreshLayers();
            }

            m_layersPanel->setInsertAnimationsEnabled(true);
        });
}

WorkspaceTab::~WorkspaceTab()
{
    clearLoadingShellFadeSnapshot();
    if (m_dockLayoutSaveTimer) {
        m_dockLayoutSaveTimer->stop();
    }
    saveDockLayoutNow();
    if (m_startupImageImportWatcher) {
        m_startupImageImportWatcher->waitForFinished();
    }
    if (m_workspaceStateFlushWatcher) {
        m_workspaceStateFlushWatcher->waitForFinished();
    }
    if (m_workspaceStateSyncPending) {
        writeWorkspaceStateSnapshot(m_pendingWorkspaceStateSnapshot);
        m_workspaceStateSyncPending = false;
    }
    // DockManager handles panel cleanup
    delete m_serializer;
}

QIcon WorkspaceTab::icon() const
{
    auto& icons = ruwa::ui::core::IconProvider::instance();
    if (!m_tabIconAlias.isEmpty()) {
        return icons.getIcon(m_tabIconAlias);
    }
    // Default workspace icon
    return icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::Brush);
}

QString WorkspaceTab::sanitizedRenameInput(const QString& input)
{
    QString t = input.trimmed();
    const QString marker = QStringLiteral(" *");
    while (t.endsWith(marker)) {
        t.chop(marker.size());
        t = t.trimmed();
    }
    return t;
}

QString WorkspaceTab::title() const
{
    const QString base = baseTitle();
    return isModified() ? (base + QStringLiteral(" *")) : base;
}

QRect WorkspaceTab::exportFrame() const
{
    return normalizedExportFrame(m_settings.exportFrame, m_settings.canvasSize).rect;
}

QRect WorkspaceTab::documentBoundsRect() const
{
    return QRect(0, 0, m_settings.canvasSize.width(), m_settings.canvasSize.height());
}

QRect WorkspaceTab::layerPreviewFrame() const
{
    return hasFiniteDocumentBounds() ? documentBoundsRect() : QRect();
}

QSize WorkspaceTab::exportFrameSize() const
{
    return exportFrame().size();
}

QRect WorkspaceTab::effectiveDisplayFrame() const
{
    return exportFrame();
}

QVariantMap WorkspaceTab::serialize() const
{
    QVariantMap data = ruwa::core::BaseTab::serialize();
    data[QStringLiteral("title")] = baseTitle();
    data[QStringLiteral("name")] = m_projectName;
    data[QStringLiteral("canvasSize")] = m_settings.canvasSize;
    data[QStringLiteral("canvasBoundsMode")] = static_cast<int>(m_settings.canvasBoundsMode);
    data[QStringLiteral("infiniteCanvasEnabled")] = isInfiniteCanvas();
    data[QStringLiteral("exportFrame")] = exportFrame();
    data[QStringLiteral("templateType")] = m_settings.templateType;
    data[QStringLiteral("backgroundColor")] = m_settings.backgroundColor;
    data[QStringLiteral("foregroundColor")] = m_workspaceColorState.foreground;
    data[QStringLiteral("workspaceBackgroundColor")] = m_workspaceColorState.background;
    data[QStringLiteral("editingForegroundColor")] = m_workspaceColorState.editingForeground;
    return data;
}

void WorkspaceTab::setCanvasBoundsMode(ruwa::core::canvas::CanvasBoundsMode mode)
{
    if (m_settings.canvasBoundsMode == mode) {
        return;
    }

    m_settings.canvasBoundsMode = mode;
    if (ruwa::core::canvas::hasFiniteDocumentBounds(mode)) {
        const QRect nextFrame = exportFrame();
        m_settings.canvasSize = nextFrame.size();
        m_settings.exportFrame = ruwa::core::serialization::ProjectData::ExportFrame { true,
            QRect(0, 0, nextFrame.width(), nextFrame.height()) };
    }

    if (m_canvasPanel) {
        if (ruwa::core::canvas::hasFiniteDocumentBounds(mode)) {
            m_canvasPanel->setCanvasSize(m_settings.canvasSize);
            m_canvasPanel->setExportFrame(m_settings.exportFrame.rect);
        } else {
            m_canvasPanel->setExportFrame(exportFrame());
        }
        m_canvasPanel->setCanvasBoundsMode(mode);
    }
    if (m_layersPanel) {
        m_layersPanel->setDisplayFrame(layerPreviewFrame());
    }

    refreshToolbarState();
    markProjectModified();
}

void WorkspaceTab::setWorkspaceColorState(const WorkspaceColorState& state)
{
    m_workspaceColorState = state;
    m_workspaceColorStateInitialized = true;
    m_workspaceColorStateSeededFromCanvasDefaults = false;
    syncColorPanelFromWorkspaceState();
    syncCanvasColorFromWorkspaceState();
    scheduleDockLayoutSave();
}

void WorkspaceTab::setWorkspaceColorSlot(bool isForeground)
{
    m_workspaceColorStateInitialized = true;
    if (m_workspaceColorState.editingForeground == isForeground) {
        return;
    }

    m_workspaceColorState.editingForeground = isForeground;
    m_workspaceColorStateSeededFromCanvasDefaults = false;
    syncColorPanelFromWorkspaceState();
    syncCanvasColorFromWorkspaceState();
    scheduleDockLayoutSave();
}

void WorkspaceTab::setWorkspaceColorForSlot(bool isForeground, const QColor& color)
{
    m_workspaceColorStateInitialized = true;
    QColor& target
        = isForeground ? m_workspaceColorState.foreground : m_workspaceColorState.background;
    if (target == color) {
        return;
    }

    target = color;
    m_workspaceColorStateSeededFromCanvasDefaults = false;
    syncColorPanelFromWorkspaceState();
    if (m_workspaceColorState.editingForeground == isForeground) {
        syncCanvasColorFromWorkspaceState();
    }
    scheduleDockLayoutSave();
}

void WorkspaceTab::syncColorPanelFromWorkspaceState()
{
    if (!m_colorPanel) {
        return;
    }

    m_syncingWorkspaceColorState = true;
    m_colorPanel->applyColorState(m_workspaceColorState.foreground,
        m_workspaceColorState.background, m_workspaceColorState.editingForeground);
    m_syncingWorkspaceColorState = false;
}

void WorkspaceTab::syncCanvasColorFromWorkspaceState()
{
    if (!m_canvasPanel) {
        return;
    }

    const QColor color = m_workspaceColorState.editingForeground ? m_workspaceColorState.foreground
                                                                 : m_workspaceColorState.background;
    m_canvasPanel->applyCurrentBrushColorPreservingOpacity(color);
}

bool WorkspaceTab::canClose()
{
    // Block close when modified; callers should use prepareWorkspaceTabForClose first
    return !isModified();
}

void WorkspaceTab::onInitialize()
{
    m_suppressModifiedChanges = true;

    const bool loadingExistingProject = m_waitingForAsyncProjectData || m_pendingProjectData;
    if (!m_suppressThemeRefreshLoadingShell) {
        setupLoadingShell();
        showLoadingShell(loadingExistingProject ? tr("Preparing workspace shell...")
                                                : tr("Creating workspace shell..."));
    }
    updateThemeColors();
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &WorkspaceTab::updateThemeColors);
    startAutoSaveTimer();
}

void WorkspaceTab::updateThemeColors()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, colors.background);
    setPalette(pal);

    if (m_loadingShell) {
        m_loadingShell->setStyleSheet(QString(R"(
            QWidget {
                background-color: %1;
            }
            QLabel#workspaceLoadingTitle {
                color: %2;
                font-size: 18px;
                font-weight: 600;
            }
            QLabel#workspaceLoadingStatus {
                color: %3;
                font-size: 12px;
            }
        )")
                .arg(colors.background.name(), colors.text.name(), colors.textMuted.name()));
    }
    if (m_loadingIndicator) {
        m_loadingIndicator->setAccentColor(colors.primary);
    }
}

void WorkspaceTab::onActivate()
{
    // TODO: Resume any paused operations
}

void WorkspaceTab::onDeactivate()
{
    // TODO: Pause any active operations
}

void WorkspaceTab::onApplyThemeRefresh(std::function<void()> finished, bool showLoading)
{
    Q_UNUSED(showLoading);

    // Non-destructive theme refresh.
    //
    // ThemeManager has already pushed the new palette + global stylesheet to the
    // whole application (qApp->setStyleSheet) and emitted themeChanged, to which
    // this tab and all of its panels are connected. So the heavy lifting is done;
    // here we only need to bring THIS tab's live widget tree to the exact
    // appearance of a freshly-opened tab.
    //
    // We deliberately do NOT recreate the tab or its OpenGLCanvasWidget: tearing
    // down / reparenting a QOpenGLWidget while the top-level window is composited
    // corrupts the backing store and blacks out the entire window (topbar
    // included) on Windows. Keeping the canvas alive also preserves the project
    // and undo history for free, with no save round-trip.

    QElapsedTimer profTimer;
    profTimer.start(); // [ThemeProfile]

    updateThemeColors();

    // Authoritatively push the current theme through the whole dock container
    // subtree (container background + dock areas + splitters + floating
    // containers + overlay). This is the workspace "edge" / gap background the
    // user sees around the panels; doing it here makes the refresh independent
    // of themeChanged slot ordering and of any frozen-repaint timing during the
    // global apply.
    if (m_dockContainer) {
        m_dockContainer->applyTheme(ruwa::ui::core::ThemeManager::instance().colors());
    }

    // Apply the theme work that each dock panel deferred while this tab was in
    // the background (see DockPanel::handleThemeChanged). This is the heavy part
    // we intentionally skipped at theme-apply time so that switching the theme
    // does not restyle every panel of every open project at once.
    if (m_dockManager) {
        const QList<docking::DockPanel*> panels = m_dockManager->panels();
        for (docking::DockPanel* panel : panels) {
            if (panel) {
                panel->flushPendingTheme();
            }
        }
    }

    // Re-run any visibility-gated theme handlers (heavy panel content that was
    // skipped while this tab was hidden — see ThemeManager::registerThemeHandler).
    ruwa::ui::core::ThemeManager::instance().flushThemeHandlers(this);

    const qint64 profBeforeRepolish = profTimer.elapsed(); // [ThemeProfile]

    // Force the whole subtree to re-evaluate the already-updated stylesheet +
    // repaint, covering any non-DockPanel widgets (toolbar, overlays).
    repolishThemeRecursive(this);

    // Give the event loop one turn so queued repaints land before the loading
    // overlay is dismissed, then report completion.
    QPointer<WorkspaceTab> guard(this);
    QTimer::singleShot(0, this, [guard, finished = std::move(finished)]() mutable {
        if (guard) {
            guard->update();
        }
        if (finished) {
            finished();
        }
    });
}

void WorkspaceTab::onTransitionFinishedImpl()
{
    m_transitionFinished = true;
    queuePostTransitionInitialization();
    return;

    // Heavy work: OpenGL widget, tile content, render
    const bool glContentCreated = m_canvasPanel && m_canvasPanel->createGLContent();

    auto* model = m_layersPanel ? m_layersPanel->layerModel() : nullptr;

    if (m_pendingProjectData) {
        fromProjectDataTiles(*m_pendingProjectData);
        m_pendingProjectData.reset();
    }

    if (m_canvasPanel && model) {
        m_canvasPanel->setLayerModel(model);
        m_canvasPanel->requestRender();
        // Play zoom-in animation when GL content was just created (first transition to workspace)
        // — both for new projects and when opening from file; not when switching back to existing
        // tab
        if (glContentCreated) {
            m_canvasPanel->scheduleNewProjectAppearanceAnimation();
        }
    }

    // Refresh navigator thumbnail as soon as GL content is ready
    if (glContentCreated && m_navigatorPanel && m_navigatorPanel->contentWidget()) {
        if (auto* w
            = qobject_cast<workspace::NavigatorWidget*>(m_navigatorPanel->contentWidget())) {
            w->refreshThumbnail();
        }
    }

    m_suppressModifiedChanges = false; // End of initial setup - user changes now mark as modified
}

void WorkspaceTab::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Dock system (fills rest of space)
    setupDockSystem();
    if (m_dockContainer) {
        mainLayout->addWidget(m_dockContainer, 1);
    }
}

void WorkspaceTab::resizeEvent(QResizeEvent* event)
{
    ruwa::core::BaseTab::resizeEvent(event);
    if (m_loadingShell) {
        m_loadingShell->setGeometry(rect());
    }
}

void WorkspaceTab::setupLoadingShell()
{
    if (m_loadingShell) {
        return;
    }

    m_loadingShell = new QWidget(this);
    m_loadingShell->setAttribute(Qt::WA_StyledBackground, true);
    m_loadingShell->setGeometry(rect());
    m_loadingShellOpacity = new QGraphicsOpacityEffect(m_loadingShell);
    m_loadingShellOpacity->setOpacity(1.0);
    m_loadingShell->setGraphicsEffect(m_loadingShellOpacity);

    auto* layout = new QVBoxLayout(m_loadingShell);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(10);
    layout->setAlignment(Qt::AlignCenter);

    m_loadingIndicator = new ruwa::ui::widgets::DotGridLoadingIndicator(m_loadingShell);
    m_loadingIndicator->setFixedSize(42, 42);

    m_loadingTitleLabel = new QLabel((m_waitingForAsyncProjectData || m_pendingProjectData)
            ? tr("Loading workspace")
            : tr("Creating workspace"),
        m_loadingShell);
    m_loadingTitleLabel->setObjectName(QStringLiteral("workspaceLoadingTitle"));
    m_loadingTitleLabel->setAlignment(Qt::AlignCenter);

    m_loadingStatusLabel = new QLabel(QString(), m_loadingShell);
    m_loadingStatusLabel->setObjectName(QStringLiteral("workspaceLoadingStatus"));
    m_loadingStatusLabel->setAlignment(Qt::AlignCenter);

    layout->addWidget(m_loadingIndicator, 0, Qt::AlignCenter);
    layout->addWidget(m_loadingTitleLabel, 0, Qt::AlignCenter);
    layout->addWidget(m_loadingStatusLabel, 0, Qt::AlignCenter);
}

void WorkspaceTab::showLoadingShell(const QString& statusText)
{
    if (!m_loadingShell) {
        return;
    }

    m_loadingShellHideContinuation = {};
    clearLoadingShellFadeSnapshot();

    if (m_loadingShellFadeAnimation) {
        m_loadingShellFadeAnimation->stop();
        m_loadingShellFadeAnimation->deleteLater();
        m_loadingShellFadeAnimation = nullptr;
    }

    if (m_loadingStatusLabel) {
        m_loadingStatusLabel->setText(statusText);
    }
    if (m_loadingIndicator && !m_loadingIndicator->isRunning()) {
        m_loadingIndicator->start();
    }
    if (m_dockContainer) {
        m_dockContainer->setEnabled(false);
    }
    if (m_loadingShellOpacity) {
        m_loadingShellOpacity->setOpacity(1.0);
    }
    m_loadingShell->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_loadingShell->setGeometry(rect());
    m_loadingShell->show();
    m_loadingShell->raise();
}

void WorkspaceTab::hideLoadingShell(std::function<void()> onFinished)
{
    m_loadingShellHideContinuation = std::move(onFinished);

    if (!m_loadingShell) {
        hideLoadingShellImmediately();
        return;
    }

    if (m_dockContainer) {
        m_dockContainer->setEnabled(true);
    }

    clearLoadingShellFadeSnapshot();

    if (m_loadingShellFadeAnimation) {
        m_loadingShellFadeAnimation->stop();
        m_loadingShellFadeAnimation->deleteLater();
        m_loadingShellFadeAnimation = nullptr;
    }

    const QPixmap snapshot = m_loadingShell->grab();
    if (snapshot.isNull()) {
        hideLoadingShellImmediately();
        return;
    }

    m_loadingShellFadeSnapshot = new QLabel(this);
    m_loadingShellFadeSnapshot->setObjectName(QStringLiteral("workspaceLoadingFadeSnapshot"));
    m_loadingShellFadeSnapshot->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_loadingShellFadeSnapshot->setPixmap(snapshot);
    m_loadingShellFadeSnapshot->setScaledContents(true);
    m_loadingShellFadeSnapshot->setGeometry(m_loadingShell->geometry());
    m_loadingShellFadeSnapshotOpacity = new QGraphicsOpacityEffect(m_loadingShellFadeSnapshot);
    m_loadingShellFadeSnapshotOpacity->setOpacity(1.0);
    m_loadingShellFadeSnapshot->setGraphicsEffect(m_loadingShellFadeSnapshotOpacity);
    m_loadingShellFadeSnapshot->show();
    m_loadingShellFadeSnapshot->raise();

    if (m_loadingIndicator && m_loadingIndicator->isRunning()) {
        m_loadingIndicator->stop();
    }
    if (m_loadingShellOpacity) {
        m_loadingShellOpacity->setOpacity(1.0);
    }
    m_loadingShell->hide();
    m_loadingShell->setAttribute(Qt::WA_TransparentForMouseEvents, false);

    m_loadingShellFadeAnimation
        = new QPropertyAnimation(m_loadingShellFadeSnapshotOpacity, "opacity", this);
    m_loadingShellFadeAnimation->setDuration(220);
    m_loadingShellFadeAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_loadingShellFadeAnimation->setStartValue(1.0);
    m_loadingShellFadeAnimation->setEndValue(0.0);
    connect(m_loadingShellFadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        clearLoadingShellFadeSnapshot();
        if (m_loadingShellFadeAnimation) {
            m_loadingShellFadeAnimation->deleteLater();
            m_loadingShellFadeAnimation = nullptr;
        }
        if (m_loadingShellHideContinuation) {
            auto continuation = std::move(m_loadingShellHideContinuation);
            m_loadingShellHideContinuation = {};
            continuation();
        }
    });
    m_loadingShellFadeAnimation->start();
}

void WorkspaceTab::hideLoadingShellImmediately()
{
    if (m_dockContainer) {
        m_dockContainer->setEnabled(true);
    }
    if (m_loadingIndicator && m_loadingIndicator->isRunning()) {
        m_loadingIndicator->stop();
    }
    if (m_loadingShellOpacity) {
        m_loadingShellOpacity->setOpacity(1.0);
    }
    if (m_loadingShell) {
        m_loadingShell->hide();
        m_loadingShell->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    }
    clearLoadingShellFadeSnapshot();
    if (m_loadingShellHideContinuation) {
        auto continuation = std::move(m_loadingShellHideContinuation);
        m_loadingShellHideContinuation = {};
        continuation();
    }
}

void WorkspaceTab::clearLoadingShellFadeSnapshot()
{
    if (m_loadingShellFadeSnapshot) {
        m_loadingShellFadeSnapshot->hide();
        m_loadingShellFadeSnapshot->deleteLater();
        m_loadingShellFadeSnapshot = nullptr;
    }
    m_loadingShellFadeSnapshotOpacity = nullptr;
}

void WorkspaceTab::buildWorkspaceUi()
{
    if (m_workspaceUiBuilt) {
        return;
    }

    setupUI();
    m_workspaceUiBuilt = true;
    m_canvasGlReady = false;

    if (m_layersPanel) {
        m_layersPanel->setInsertAnimationsEnabled(false);
    }

    if (m_pendingProjectData) {
        fromProjectDataStructure(*m_pendingProjectData);
    } else {
        initializeEmptyProject();
    }

    if (m_layersPanel) {
        m_layersPanel->setInsertAnimationsEnabled(true);
    }
}

void WorkspaceTab::initializeEmptyProject()
{
    if (!m_layersPanel) {
        return;
    }

    auto* model = m_layersPanel->layerModel();
    if (!model || !model->rootLayers().isEmpty()) {
        return;
    }

    auto backgroundLayer
        = ruwa::core::layers::LayerData::create(LayerType::Background, tr("Background"));
    backgroundLayer->backgroundColor = m_settings.backgroundColor;
    model->addLayer(backgroundLayer);

    if ((!m_pendingStartupImageImportPaths.isEmpty() || !m_pendingStartupImageImports.isEmpty())
        && m_startupImageImportWatcher && !m_startupImageImportWatcher->isRunning()) {
        const QStringList startupImportPaths = m_pendingStartupImageImportPaths;
        const QList<PendingStartupImageImport> startupImports = m_pendingStartupImageImports;
        m_startupImageImportWatcher->setFuture(
            QtConcurrent::run([startupImportPaths, startupImports]() {
                workspace::detail::ImportedLayerBatch batch
                    = workspace::detail::buildImportedRasterLayerBatch(startupImportPaths);

                for (const PendingStartupImageImport& import : startupImports) {
                    if (import.image.isNull()) {
                        continue;
                    }

                    workspace::detail::ImportedLayerBatch imageBatch
                        = workspace::detail::buildImportedRasterLayerBatchFromImage(import.image,
                            import.layerName.trimmed().isEmpty() ? QObject::tr("Dropped image")
                                                                 : import.layerName.trimmed());
                    for (auto& payload : imageBatch.layers) {
                        batch.layers.append(std::move(payload));
                    }
                    for (auto& payload : imageBatch.undoLayers) {
                        batch.undoLayers.append(std::move(payload));
                    }
                }

                return batch;
            }));
        return;
    }

    auto* drawLayer = model->createLayer(tr("Draw here"), 0);
    if (drawLayer) {
        model->setSelectedLayer(drawLayer->id);
    }

    m_layersPanel->refreshLayers();
}

void WorkspaceTab::seedStartupImageImportPaths(const QStringList& filePaths)
{
    if (filePaths.isEmpty()) {
        return;
    }
    m_pendingStartupImageImportPaths.append(filePaths);
}

void WorkspaceTab::seedStartupImageImport(const QImage& image, const QString& layerName)
{
    if (image.isNull()) {
        return;
    }

    PendingStartupImageImport import;
    import.image = image;
    import.layerName = layerName;
    m_pendingStartupImageImports.append(std::move(import));
}

void WorkspaceTab::queuePostTransitionInitialization()
{
    if (!m_transitionFinished || m_postTransitionInitializationQueued || m_asyncStartupCompleted) {
        return;
    }

    if (m_waitingForAsyncProjectData && !m_pendingProjectData) {
        return;
    }

    if (!m_workspaceUiBuilt) {
        if (m_suppressThemeRefreshLoadingShell) {
            buildWorkspaceUi();
            queuePostTransitionInitialization();
            return;
        }

        m_postTransitionInitializationQueued = true;
        QTimer::singleShot(0, this, [this]() {
            m_postTransitionInitializationQueued = false;
            showLoadingShell(m_pendingProjectData ? tr("Building workspace panels...")
                                                  : tr("Building workspace..."));
            buildWorkspaceUi();
            hideLoadingShell([this]() { queuePostTransitionInitialization(); });
        });
        return;
    }

    if (m_postTransitionInitializationStarted) {
        return;
    }

    m_postTransitionInitializationQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_postTransitionInitializationQueued = false;
        m_postTransitionInitializationStarted = true;

        if (!m_canvasPanel) {
            return;
        }

        const bool glContentCreated = m_canvasPanel->createGLContent();
        auto* model = m_layersPanel ? m_layersPanel->layerModel() : nullptr;
        if (m_canvasPanel && model) {
            m_canvasPanel->setLayerModel(model);
        }

        // createGLContent() ends with restoreToolState(), which seeds the GL brush
        // from the per-tool persisted color. That can disagree with the workspace
        // color state (what the Color panel shows) restored from the project/session,
        // and because GL creation finishes after the dock-layout restore timer, the
        // per-tool color wins — so the first stroke paints with the previous color
        // until the user re-picks one. Re-assert the workspace color here, after the
        // tool-state restore, so the canvas matches what the panel displays.
        if (glContentCreated) {
            syncColorPanelFromWorkspaceState();
            syncCanvasColorFromWorkspaceState();
        }

        if (m_pendingProjectData) {
            if (glContentCreated && m_canvasPanel) {
                m_canvasPanel->setDeferredAppearanceAnimation(true);
            }
            m_pendingCanvasAppearanceAnimation = glContentCreated;
            startDeferredTileRestore();
        } else {
            if (m_canvasPanel && model) {
                m_canvasPanel->requestRender();
                if (glContentCreated) {
                    m_canvasPanel->scheduleNewProjectAppearanceAnimation();
                }
            }
            tryFinishAsyncStartup();
        }
    });
}

void WorkspaceTab::startDeferredTileRestore()
{
    if (m_layersPanel) {
        m_layersPanel->setThumbnailLoadingMode(true);
    }

    m_pendingTileLoads.clear();
    m_pendingTileLoadIndex = 0;
    m_pendingTileIndex = 0;
    m_tileRestoreLoadedAnyContent = false;
    m_tileRestoreScheduled = false;
    m_tileRestoreInProgress = false;

    if (!m_pendingProjectData) {
        if (m_layersPanel) {
            m_layersPanel->setThumbnailLoadingMode(false);
        }
        tryFinishAsyncStartup();
        return;
    }

    enqueuePendingTileLoads(m_pendingProjectData->rootLayers);
    if (m_pendingTileLoads.empty()) {
        m_pendingProjectData.reset();
        if (m_layersPanel) {
            m_layersPanel->setThumbnailLoadingMode(false);
        }
        tryFinishAsyncStartup();
        return;
    }

    m_tileRestoreInProgress = true;
    scheduleNextTileRestoreBatch();
}

void WorkspaceTab::enqueuePendingTileLoads(
    const QList<ruwa::core::serialization::LayerEntry>& entries)
{
    for (const auto& entry : entries) {
        if (!entry.tiles.isEmpty()) {
            m_pendingTileLoads.push_back(PendingTileLayerLoad { entry.id, &entry });
        }
        if (!entry.children.isEmpty()) {
            enqueuePendingTileLoads(entry.children);
        }
    }
}

void WorkspaceTab::scheduleNextTileRestoreBatch()
{
    if (m_tileRestoreScheduled || !m_tileRestoreInProgress) {
        return;
    }

    m_tileRestoreScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_tileRestoreScheduled = false;
        processPendingTileRestoreBatch();
    });
}

void WorkspaceTab::processPendingTileRestoreBatch()
{
    if (!m_tileRestoreInProgress || !m_layersPanel) {
        return;
    }

    auto* model = m_layersPanel->layerModel();
    if (!model) {
        m_tileRestoreInProgress = false;
        return;
    }

    QElapsedTimer timer;
    timer.start();

    int processedTiles = 0;
    QSet<QUuid> touchedLayerIds;
    while (m_pendingTileLoadIndex < static_cast<int>(m_pendingTileLoads.size())) {
        const auto& pending = m_pendingTileLoads[static_cast<std::size_t>(m_pendingTileLoadIndex)];
        const auto* entry = pending.entry;
        if (!entry) {
            ++m_pendingTileLoadIndex;
            m_pendingTileIndex = 0;
            continue;
        }

        auto* layer = model->layerById(pending.layerId);
        auto* pixelGrid = layer ? layer->pixelGrid() : nullptr;
        const auto& tiles = entry->tiles;

        const int contentTileBytes
            = pixelGrid ? static_cast<int>(aether::tileByteSize(pixelGrid->format())) : 0;
        while (m_pendingTileIndex < tiles.size()) {
            const auto& tile = tiles.at(m_pendingTileIndex++);
            if (pixelGrid && tile.pixels.size() == contentTileBytes) {
                aether::TileData& dst
                    = pixelGrid->getOrCreateTile(aether::TileKey { tile.x, tile.y });
                std::memcpy(
                    dst.pixels(), tile.pixels.constData(), static_cast<size_t>(contentTileBytes));
                dst.markDirty();
                touchedLayerIds.insert(pending.layerId);
                m_tileRestoreLoadedAnyContent = true;
            }

            ++processedTiles;
            if (processedTiles >= kTileRestoreBatchMaxTiles
                || timer.elapsed() >= kTileRestoreBatchBudgetMs) {
                break;
            }
        }

        if (m_pendingTileIndex >= tiles.size()) {
            m_pendingTileIndex = 0;
            ++m_pendingTileLoadIndex;
        }

        if (processedTiles >= kTileRestoreBatchMaxTiles
            || timer.elapsed() >= kTileRestoreBatchBudgetMs) {
            break;
        }
    }

    if (!touchedLayerIds.isEmpty() && m_layersPanel) {
        m_layersPanel->invalidateLayerThumbnails(touchedLayerIds.values());
    }

    if (m_canvasPanel && m_canvasGlReady) {
        m_canvasPanel->requestRender();
    }

    if (m_pendingTileLoadIndex >= static_cast<int>(m_pendingTileLoads.size())) {
        m_tileRestoreInProgress = false;
        m_pendingTileLoads.clear();
        if (m_tileRestoreLoadedAnyContent) {
            model->notifyBulkLayerContentChanged();
            m_tileRestoreLoadedAnyContent = false;
        }
        m_pendingProjectData.reset();
        if (m_layersPanel) {
            m_layersPanel->setThumbnailLoadingMode(false);
        }
        if (m_canvasPanel) {
            m_canvasPanel->requestRender();
        }
        tryFinishAsyncStartup();
        return;
    }

    scheduleNextTileRestoreBatch();
}

void WorkspaceTab::scheduleRecentProjectsThumbnailCapture()
{
    if (!hasFilePath() || m_initialThumbnailCaptureScheduled) {
        return;
    }

    m_initialThumbnailCaptureScheduled = true;
    QPointer<WorkspaceTab> tabGuard(this);
    const QString path = m_filePath;
    QTimer::singleShot(600, this, [tabGuard, path]() {
        if (!tabGuard || !tabGuard->canvasPanel()) {
            return;
        }
        const QImage thumb = tabGuard->canvasPanel()->getFullCanvasThumbnail(256);
        if (thumb.isNull()) {
            return;
        }
        ruwa::core::serialization::ThumbnailCache::instance().save(path, thumb, 256);
        ruwa::core::serialization::RecentProjectsManager::instance().notifyThumbnailsUpdated();
    });
}

void WorkspaceTab::tryFinishAsyncStartup()
{
    if (m_asyncStartupCompleted) {
        return;
    }
    if (!m_workspaceUiBuilt || !m_transitionFinished || !m_canvasGlReady) {
        return;
    }
    if (m_tileRestoreInProgress || m_tileRestoreScheduled) {
        return;
    }

    if (m_canvasPanel && m_layersPanel) {
        m_canvasPanel->setLayerModel(m_layersPanel->layerModel());
        m_canvasPanel->requestRender();
    }

    flushPendingStartupImageImport();

    if (!m_pendingPromptImportPaths.isEmpty()) {
        QStringList paths;
        paths.swap(m_pendingPromptImportPaths);
        if (m_canvasPanel) {
            m_canvasPanel->promptImportImageFiles(paths);
        }
    }

    if (m_pendingCanvasAppearanceAnimation && m_canvasPanel) {
        m_pendingCanvasAppearanceAnimation = false;
        m_canvasPanel->setDeferredAppearanceAnimation(false);
        m_canvasPanel->scheduleNewProjectAppearanceAnimation();
    }

    if (m_layersPanel) {
        m_layersPanel->setThumbnailLoadingMode(false);
    }

    if (m_navigatorPanel && m_navigatorPanel->contentWidget()) {
        if (auto* w
            = qobject_cast<workspace::NavigatorWidget*>(m_navigatorPanel->contentWidget())) {
            w->refreshThumbnail();
        }
    }

    scheduleRecentProjectsThumbnailCapture();

    m_asyncStartupCompleted = true;
    m_suppressModifiedChanges = false;
    emit startupCompleted();
}

void WorkspaceTab::setupDockSystem()
{
    using namespace docking;

    // Create dock container (replaces QMainWindow)
    m_dockContainer = new DockContainerWidget(this);

    // Create dock manager
    m_dockManager = new DockManager(this);
    m_dockManager->setContainer(m_dockContainer);

    // Create serializer for state persistence
    m_serializer = new DockStateSerializer(m_dockManager);

    // Setup panels
    setupPanels();

    // Apply default layout
    setupDefaultLayout();

    // Apply saved user layout over defaults (user choices have priority).
    // Defer so dock tree/panels are fully settled and initial layout has run.
    QTimer::singleShot(50, this, [this]() { restoreUserDockLayout(); });

    // Connect signals
    connect(m_dockManager, &docking::DockManager::layoutChanged, this,
        [this]() { scheduleDockLayoutSave(); });
    connectPanelSignals();
}

void WorkspaceTab::setupPanels()
{
    constexpr int kDefaultRightPanelWidth = 320;
    m_settings.exportFrame = normalizedExportFrame(m_settings.exportFrame, m_settings.canvasSize);

    // Create all panels
    m_canvasPanel = new workspace::CanvasPanel(m_settings.canvasSize, m_settings.exportFrame.rect);
    m_canvasPanel->setCanvasBoundsMode(m_settings.canvasBoundsMode);
    m_canvasPanel->setLoadingOverlayDecorationsVisible(
        m_waitingForAsyncProjectData || static_cast<bool>(m_pendingProjectData));
    m_toolsPanel = new workspace::ToolsPanel();
    m_brushesPanel = new workspace::BrushesPanel();
    m_brushSettingsPanel = new workspace::BrushSettingsPanel();
    m_layersPanel = new workspace::LayersPanel();
    // Stamp the per-document tile format before any layer/grid is created so new
    // content grids adopt it (masks stay RGBA8). For loaded projects m_settings
    // .tileFormat was already set from the file's contentTileFormat.
    m_layersPanel->layerModel()->setDocumentTileFormat(m_settings.tileFormat);
    m_layersPanel->setDisplayFrame(layerPreviewFrame());
    m_layerPropertiesPanel = new workspace::LayerPropertiesPanel();
    m_layerPropertiesPanel->setLayerModel(m_layersPanel->layerModel());
    m_layerEffectsPanel = new workspace::LayerEffectsPanel();
    m_layerEffectsPanel->setLayerModel(m_layersPanel->layerModel());
    m_layerEffectsPanel->setCanvasSizeProvider([this]() -> std::optional<QSize> {
        if (!m_canvasPanel
            || ruwa::core::canvas::isInfiniteCanvas(m_canvasPanel->canvasBoundsMode())) {
            return std::nullopt;
        }
        return QSize(static_cast<int>(m_canvasPanel->canvas().width()),
            static_cast<int>(m_canvasPanel->canvas().height()));
    });
    m_toolsPanel->setRelatedPanels(m_canvasPanel, m_layersPanel);

    m_layersPanel->setPushUndoFn([this](std::unique_ptr<aether::IUndoCommand> cmd) {
        if (m_canvasPanel) {
            m_canvasPanel->createGLContent();
            m_canvasPanel->canvas().undoManager().push(std::move(cmd));
        }
    });
    m_layersPanel->setUndoCallbacks(
        [this]() {
            if (m_canvasPanel)
                m_canvasPanel->requestRender();
        },
        [this]() {
            if (m_canvasPanel) {
                m_canvasPanel->notifyContentChanged();
            } else {
                markProjectModified();
            }
        });
    m_layersPanel->setFillMaskFromSelectionFn([this](const ruwa::core::layers::LayerId& id) {
        return m_canvasPanel && m_canvasPanel->fillLayerMaskFromActiveSelection(id);
    });
    m_layerEffectsPanel->setPushUndoFn([this](std::unique_ptr<aether::IUndoCommand> cmd) {
        if (m_canvasPanel) {
            m_canvasPanel->createGLContent();
            m_canvasPanel->canvas().undoManager().push(std::move(cmd));
        }
    });
    m_layerEffectsPanel->setUndoCallbacks(
        [this]() {
            if (m_canvasPanel)
                m_canvasPanel->requestRender();
        },
        [this]() {
            if (m_canvasPanel) {
                m_canvasPanel->notifyContentChanged();
            } else {
                markProjectModified();
            }
        });

    m_colorPanel = new workspace::ColorPanel();
    m_navigatorPanel = new workspace::NavigatorPanel();

    m_canvasPanel->setPersistentKey(QStringLiteral("canvas"));
    m_toolsPanel->setPersistentKey(QStringLiteral("tools"));
    m_brushesPanel->setPersistentKey(QStringLiteral("brushes"));
    m_brushSettingsPanel->setPersistentKey(QStringLiteral("brush-settings"));
    m_layersPanel->setPersistentKey(QStringLiteral("layers"));
    m_layerPropertiesPanel->setPersistentKey(QStringLiteral("layer-properties"));
    m_layerEffectsPanel->setPersistentKey(QStringLiteral("layer-effects"));
    m_colorPanel->setPersistentKey(QStringLiteral("color"));
    m_navigatorPanel->setPersistentKey(QString::fromLatin1(kNavigatorPanelPersistentKey));

    m_navigatorPanel->setCanvasPanel(m_canvasPanel);
    m_brushesPanel->setCanvasPanel(m_canvasPanel);
    m_brushSettingsPanel->setCanvasPanel(m_canvasPanel);
    connect(m_brushSettingsPanel, &workspace::BrushSettingsPanel::brushEditorRequested,
        m_brushesPanel, &workspace::BrushesPanel::openBrushEditorForBrush);
    {
        m_toolsPanel->setCurrentTool(toToolsPanelTool(m_canvasPanel->toolMode()));
    }
    m_brushesPanel->setUserHorizontalDockedWidth(220);
    m_brushesPanel->setUserVerticalDockedHeight(234);
    m_brushSettingsPanel->setUserHorizontalDockedWidth(220);
    m_brushSettingsPanel->setUserVerticalDockedHeight(280);
    if (!m_workspaceColorStateInitialized) {
        m_workspaceColorState.foreground = m_canvasPanel->currentBrushColor();
        m_workspaceColorState.background = m_settings.backgroundColor;
        m_workspaceColorState.editingForeground = true;
        m_workspaceColorStateInitialized = true;
        m_workspaceColorStateSeededFromCanvasDefaults = true;
    }
    syncColorPanelFromWorkspaceState();

    // Keep right-side column width stable on startup.
    // Canvas then receives all remaining horizontal space.
    m_layersPanel->setUserHorizontalDockedWidth(kDefaultRightPanelWidth);
    m_layerPropertiesPanel->setUserHorizontalDockedWidth(kDefaultRightPanelWidth);
    m_layerEffectsPanel->setUserHorizontalDockedWidth(kDefaultRightPanelWidth);
    m_colorPanel->setUserHorizontalDockedWidth(kDefaultRightPanelWidth);
    // Color panel: compact height (240px) so Layers gets most of the right column
    m_colorPanel->setUserVerticalDockedHeight(kDefaultRightPanelWidth * 3 / 4);
    m_navigatorPanel->setUserHorizontalDockedWidth(220);

    // Register with dock manager (manager takes ownership)
    m_dockManager->registerPanel(m_canvasPanel);
    m_dockManager->registerPanel(m_toolsPanel);
    m_dockManager->registerPanel(m_brushesPanel);
    m_dockManager->registerPanel(m_brushSettingsPanel);
    m_dockManager->registerPanel(m_layersPanel);
    m_dockManager->registerPanel(m_layerPropertiesPanel);
    m_dockManager->registerPanel(m_layerEffectsPanel);
    m_dockManager->registerPanel(m_colorPanel);
    m_dockManager->registerPanel(m_navigatorPanel);
}

void WorkspaceTab::setupDefaultLayout()
{
    using namespace docking;

    // Default layout:
    // ┌──────┬─────────────────────┬────────┐
    // │      │                     │ Color  │  (3:4 aspect, above layers)
    // │Tools │      Canvas         ├────────┤
    // │      │                     │ Layers │
    // │      │                     │        │
    // │      │                     │        │
    // │      │                     │        │
    // └──────┴─────────────────────┴────────┘
    // Layer Properties and Navigator are hidden by default.

    // Add canvas first (center)
    m_dockManager->addPanel(m_canvasPanel, DockPosition::Center);

    // Add tools on left
    m_dockManager->addPanel(m_toolsPanel, DockPosition::Left);

    // Add brushes under tools
    m_dockManager->addPanelRelativeTo(m_brushesPanel, m_toolsPanel, DockPosition::Bottom);

    // Add favorite settings under the brush list.
    m_dockManager->addPanelRelativeTo(m_brushSettingsPanel, m_brushesPanel, DockPosition::Bottom);

    // Add layers on right
    m_dockManager->addPanel(m_layersPanel, DockPosition::Right);

    // Add color above layers (3:4 aspect ratio set in setupPanels)
    m_dockManager->addPanelRelativeTo(m_colorPanel, m_layersPanel, DockPosition::Top);

    // Navigator, Layer Properties and Layer Effects are not added to layout by default.
    // Mark as hidden so visibility getters return false.
    // User can show them via View → Panels.
    m_dockManager->closePanel(m_navigatorPanel);
    m_dockManager->closePanel(m_layerPropertiesPanel);
    m_dockManager->closePanel(m_layerEffectsPanel);
}

void WorkspaceTab::restoreUserDockLayout()
{
    m_restoringWorkspaceUiState = true;
    const auto finishRestore = [this]() {
        syncColorPanelFromWorkspaceState();
        syncCanvasColorFromWorkspaceState();
        m_restoringWorkspaceUiState = false;
    };

    QSettings settings;
    const bool useSerializedWorkspaceState = m_hasSerializedWorkspaceState;
    const QByteArray savedLayout = useSerializedWorkspaceState
        ? m_serializedDockLayoutState
        : settings.value(kWorkspaceDockLayoutKey).toByteArray();
    const bool hasStoredWorkspaceColors = settings.contains(kWorkspaceForegroundColorKey)
        || settings.contains(kWorkspaceBackgroundColorKey)
        || settings.contains(kWorkspaceEditingForegroundColorKey);
    const docking::DockLayoutPreset defaultPreset = docking::DockLayoutPreset::defaultWorkspace();

    const auto applyDefaultPreset = [this, &defaultPreset]() -> bool {
        if (!m_serializer) {
            return false;
        }

        const bool hadAnimations = m_dockContainer ? m_dockContainer->animationsEnabled() : true;
        m_restoringDockLayout = true;
        if (m_dockContainer) {
            m_dockContainer->setAnimationsEnabled(false);
        }

        const bool restored = defaultPreset.hasSerializedDockState()
            ? restoreDockState(defaultPreset.dockState)
            : m_serializer->applyPreset(defaultPreset);

        if (m_dockContainer) {
            m_dockContainer->setAnimationsEnabled(hadAnimations);
        }
        m_restoringDockLayout = false;

        if (restored) {
            applyPresetCanvasWidgetState(defaultPreset);
            emit panelsVisibilityChanged();
        }

        return restored;
    };

    if (m_canvasPanel) {
        if (!useSerializedWorkspaceState && hasStoredWorkspaceColors
            && (!m_workspaceColorStateInitialized
                || m_workspaceColorStateSeededFromCanvasDefaults)) {
            const QColor foreground = QColor::fromRgba(
                settings.value(kWorkspaceForegroundColorKey, QColor(Qt::black).rgba()).toUInt());
            const QColor background = QColor::fromRgba(
                settings.value(kWorkspaceBackgroundColorKey, QColor(Qt::white).rgba()).toUInt());
            const bool editingForeground
                = settings.value(kWorkspaceEditingForegroundColorKey, true).toBool();

            m_workspaceColorState.foreground = foreground;
            m_workspaceColorState.background = background;
            m_workspaceColorState.editingForeground = editingForeground;
            m_workspaceColorStateInitialized = true;
            m_workspaceColorStateSeededFromCanvasDefaults = false;
            syncColorPanelFromWorkspaceState();
            syncCanvasColorFromWorkspaceState();
        }

        if (savedLayout.isEmpty()) {
            applyPresetCanvasWidgetState(defaultPreset);
        } else {
            // Restore overlay state from settings
            for (const ruwa::ui::CanvasWidget widget : ruwa::ui::kCanvasWidgets) {
                const bool visible = useSerializedWorkspaceState
                    ? m_serializedCanvasWidgets[widget]
                    : settings.value(canvasWidgetVisibleKey(widget), true).toBool();
                m_canvasPanel->setCanvasWidgetVisible(widget, visible);
            }

            const QPointF brushNorm = useSerializedWorkspaceState
                ? m_serializedBrushOverlayPosNormalized
                : settings.value(kWorkspaceBrushOverlayPosKey).toPointF();
            if (brushNorm.x() >= 0 && brushNorm.y() >= 0 && brushNorm.x() <= 1
                && brushNorm.y() <= 1) {
                m_canvasPanel->setPendingBrushOverlayPositionNormalized(brushNorm);
            } else if (!useSerializedWorkspaceState) {
                const QVariant brushV = settings.value(kWorkspaceBrushOverlayPosKey);
                if (brushV.canConvert<QPointF>()) {
                    const QPointF norm = brushV.toPointF();
                    if (norm.x() >= 0 && norm.y() >= 0 && norm.x() <= 1 && norm.y() <= 1) {
                        m_canvasPanel->setPendingBrushOverlayPositionNormalized(norm);
                    }
                } else {
                    const QPoint p = brushV.toPoint();
                    if (p.x() >= 0 && p.y() >= 0) {
                        m_canvasPanel->setPendingBrushOverlayPosition(p);
                    }
                }
            }

            const QPointF toolStateNorm = useSerializedWorkspaceState
                ? m_serializedToolStateOverlayPosNormalized
                : settings.value(kWorkspaceToolStateOverlayPosKey).toPointF();
            if (toolStateNorm.x() >= 0 && toolStateNorm.y() >= 0 && toolStateNorm.x() <= 1
                && toolStateNorm.y() <= 1) {
                m_canvasPanel->setPendingToolStateOverlayPositionNormalized(toolStateNorm);
            } else if (!useSerializedWorkspaceState) {
                const QVariant toolV = settings.value(kWorkspaceToolStateOverlayPosKey);
                if (toolV.canConvert<QPointF>()) {
                    const QPointF norm = toolV.toPointF();
                    if (norm.x() >= 0 && norm.y() >= 0 && norm.x() <= 1 && norm.y() <= 1) {
                        m_canvasPanel->setPendingToolStateOverlayPositionNormalized(norm);
                    }
                } else {
                    const QPoint p = toolV.toPoint();
                    if (p.x() >= 0 && p.y() >= 0) {
                        m_canvasPanel->setPendingToolStateOverlayPosition(p);
                    }
                }
            }

            const QPointF joystickNorm = useSerializedWorkspaceState
                ? m_serializedStylusJoystickPosNormalized
                : settings.value(kWorkspaceStylusJoystickPosKey).toPointF();
            if (joystickNorm.x() >= 0 && joystickNorm.y() >= 0 && joystickNorm.x() <= 1
                && joystickNorm.y() <= 1) {
                m_canvasPanel->setPendingStylusJoystickPositionNormalized(joystickNorm);
            } else if (!useSerializedWorkspaceState) {
                const QVariant joystickV = settings.value(kWorkspaceStylusJoystickPosKey);
                if (joystickV.canConvert<QPointF>()) {
                    const QPointF norm = joystickV.toPointF();
                    if (norm.x() >= 0 && norm.y() >= 0 && norm.x() <= 1 && norm.y() <= 1) {
                        m_canvasPanel->setPendingStylusJoystickPositionNormalized(norm);
                    }
                } else {
                    const QPoint p = joystickV.toPoint();
                    if (p.x() >= 0 && p.y() >= 0) {
                        m_canvasPanel->setPendingStylusJoystickPosition(p);
                    }
                }
            }

            if (useSerializedWorkspaceState) {
                m_canvasPanel->setPendingStylusJoystickAbovePanel(
                    m_serializedStylusJoystickAbovePanel);
            } else if (settings.contains(kWorkspaceStylusJoystickAbovePanelKey)) {
                m_canvasPanel->setPendingStylusJoystickAbovePanel(
                    settings.value(kWorkspaceStylusJoystickAbovePanelKey, true).toBool());
            }
        }
    }

    if (!m_serializer) {
        finishRestore();
        return;
    }
    if (savedLayout.isEmpty()) {
        applyDefaultPreset();
        finishRestore();
        return;
    }

    const bool hadAnimations = m_dockContainer ? m_dockContainer->animationsEnabled() : true;

    m_restoringDockLayout = true;
    if (m_dockContainer) {
        m_dockContainer->setAnimationsEnabled(false);
    }
    const bool restored = restoreDockState(savedLayout);
    if (m_dockContainer) {
        m_dockContainer->setAnimationsEnabled(hadAnimations);
    }
    m_restoringDockLayout = false;

    if (restored) {
        emit panelsVisibilityChanged();
    }

    if (!restored) {
        discardPendingWorkspaceStateSync();
        QSettings settings;
        settings.remove(kWorkspaceDockLayoutKey);
        settings.sync();
        applyDefaultPreset();
        finishRestore();
        return;
    }

    // Sanity check: workspace must keep at least one visible docked/floating panel.
    const bool hasLayoutContent = (m_dockContainer
        && (!m_dockContainer->dockedPanels().isEmpty()
            || !m_dockContainer->floatingPanels().isEmpty()));
    const bool hasSuspiciousDockedCount = (m_dockContainer && m_dockManager
        && m_dockContainer->dockedPanels().size() > m_dockManager->panels().size());

    if (!hasLayoutContent || hasSuspiciousDockedCount) {
        discardPendingWorkspaceStateSync();
        QSettings settings;
        settings.remove(kWorkspaceDockLayoutKey);
        settings.sync();
        applyDefaultPreset();
    }

    finishRestore();
}

void WorkspaceTab::scheduleDockLayoutSave()
{
    if (!m_dockLayoutSaveTimer || m_restoringDockLayout) {
        return;
    }
    m_workspaceStateDirty = true;
    m_dockLayoutSaveTimer->start();
}

void WorkspaceTab::saveDockLayoutNow()
{
    if (ruwa::Application::isFactoryResetRestartInProgress()) {
        return;
    }

    if (m_restoringDockLayout) {
        return;
    }

    if (!m_workspaceStateDirty) {
        return;
    }

    const WorkspaceStateSnapshot snapshot = captureWorkspaceStateSnapshot();
    m_serializedDockLayoutState = snapshot.dockLayoutState;
    if (snapshot.hasBrushOverlayPos) {
        m_serializedBrushOverlayPosNormalized = snapshot.brushOverlayPosNormalized;
    }
    if (snapshot.hasToolStateOverlayPos) {
        m_serializedToolStateOverlayPosNormalized = snapshot.toolStateOverlayPosNormalized;
    }
    if (snapshot.hasStylusJoystickPos) {
        m_serializedStylusJoystickPosNormalized = snapshot.stylusJoystickPosNormalized;
    }
    m_serializedStylusJoystickAbovePanel = snapshot.stylusJoystickAbovePanel;
    m_serializedCanvasWidgets = snapshot.canvasWidgets;
    m_hasSerializedWorkspaceState = !m_serializedDockLayoutState.isEmpty()
        || (m_serializedBrushOverlayPosNormalized.x() >= 0.0
            && m_serializedBrushOverlayPosNormalized.y() >= 0.0)
        || (m_serializedToolStateOverlayPosNormalized.x() >= 0.0
            && m_serializedToolStateOverlayPosNormalized.y() >= 0.0)
        || (m_serializedStylusJoystickPosNormalized.x() >= 0.0
            && m_serializedStylusJoystickPosNormalized.y() >= 0.0)
        || !m_serializedStylusJoystickAbovePanel || !m_serializedCanvasWidgets.allVisible();
    m_workspaceStateDirty = false;
    m_pendingWorkspaceStateSnapshot = snapshot;
    m_workspaceStateSyncPending = true;
    flushWorkspaceStateSync();
}

void WorkspaceTab::setupToolbar()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    m_toolbar = new QWidget(this);
    m_toolbar->setFixedHeight(40);
    m_toolbar->setStyleSheet(QString(R"(
        QWidget {
            background: %1;
            border-bottom: 1px solid %2;
        }
    )")
            .arg(colors.surface.name(), colors.border.name()));

    QHBoxLayout* layout = new QHBoxLayout(m_toolbar);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(8);

    // Project name
    QLabel* nameLabel = new QLabel(m_projectName, m_toolbar);
    QFont font = nameLabel->font();
    font.setPointSize(10);
    font.setBold(true);
    nameLabel->setFont(font);
    nameLabel->setStyleSheet(
        QString("QLabel { color: %1; border: none; }").arg(colors.text.name()));
    layout->addWidget(nameLabel);

    // Canvas size info
    m_toolbarSizeLabel = new QLabel(
        QString("%1 × %2").arg(exportFrameSize().width()).arg(exportFrameSize().height()),
        m_toolbar);
    m_toolbarSizeLabel->setStyleSheet(
        QString("QLabel { color: %1; border: none; }").arg(colors.textMuted.name()));
    layout->addWidget(m_toolbarSizeLabel);

    m_toolbarModeLabel = new QLabel(m_toolbar);
    m_toolbarModeLabel->setStyleSheet(
        QString("QLabel { color: %1; border: none; }").arg(colors.textMuted.name()));
    layout->addWidget(m_toolbarModeLabel);

    m_toolbarModeToggleButton = new widgets::CapsuleButton(
        QString(), widgets::CapsuleButton::Variant::Secondary, m_toolbar);
    m_toolbarModeToggleButton->setBaseMinimumWidth(138);
    m_toolbarModeToggleButton->setBannerBaseHeight(28);
    m_toolbarModeToggleButton->setSizeScale(0.74);
    m_toolbarModeToggleButton->setCursor(Qt::PointingHandCursor);
    connect(m_toolbarModeToggleButton, &QPushButton::clicked, this, [this]() {
        setCanvasBoundsMode(isInfiniteCanvas() ? ruwa::core::canvas::CanvasBoundsMode::Bounded
                                               : ruwa::core::canvas::CanvasBoundsMode::Infinite);
    });
    layout->addWidget(m_toolbarModeToggleButton);

    layout->addStretch();

    // TODO: Add tool options, zoom controls, etc.
    refreshToolbarState();
}

void WorkspaceTab::connectPanelSignals()
{
    // Connect layer model to canvas (so canvas knows where to draw)
    m_canvasPanel->setLayerModel(m_layersPanel->layerModel());
    connect(m_canvasPanel, &workspace::CanvasPanel::glContentReady, this, [this]() {
        m_canvasGlReady = true;
        tryFinishAsyncStartup();
    });
    connect(m_canvasPanel, &workspace::CanvasPanel::exportFrameChanged, this,
        [this](const QRect& frame) {
            const auto normalized = normalizedExportFrame(
                ruwa::core::serialization::ProjectData::ExportFrame { true, frame }, frame.size());
            if (m_settings.exportFrame.rect == normalized.rect
                && m_settings.exportFrame.enabled == normalized.enabled) {
                return;
            }
            m_settings.exportFrame = normalized;
            refreshToolbarState();
            markProjectModified();
        });
    connect(
        m_canvasPanel, &workspace::CanvasPanel::canvasSizeChanged, this, [this](const QSize& size) {
            if (m_settings.canvasSize == size) {
                return;
            }
            m_settings.canvasSize = size;
            if (hasFiniteDocumentBounds()) {
                m_settings.exportFrame = ruwa::core::serialization::ProjectData::ExportFrame { true,
                    QRect(0, 0, size.width(), size.height()) };
            }
            refreshToolbarState();
            markProjectModified();
            if (m_layersPanel) {
                m_layersPanel->setDisplayFrame(layerPreviewFrame());
            }
        });
    connect(m_canvasPanel, &workspace::CanvasPanel::canvasBoundsModeChanged, this,
        [this](ruwa::core::canvas::CanvasBoundsMode mode) {
            if (m_settings.canvasBoundsMode == mode) {
                return;
            }
            m_settings.canvasBoundsMode = mode;
            refreshToolbarState();
            markProjectModified();
            if (m_layersPanel) {
                m_layersPanel->setDisplayFrame(layerPreviewFrame());
            }
        });
    connect(m_canvasPanel, &workspace::CanvasPanel::canvasContentChanged, this, [this]() {
        if (!m_layersPanel) {
            return;
        }
        // Layer thumbnails rebuild on the UI thread and may walk large
        // tile grids. Defer them until the interactive stroke/transform
        // finishes so input stays responsive on large layers.
        if (m_canvasPanel
            && (m_canvasPanel->isDrawingActive() || m_canvasPanel->isTransformActive())) {
            return;
        }
        m_layersPanel->scheduleThumbnailRefresh();
    });
    connect(m_canvasPanel, &workspace::CanvasPanel::fillProcessingLayerChanged, m_layersPanel,
        &workspace::LayersPanel::setFillProcessingLayer);
    connect(m_canvasPanel, &workspace::CanvasPanel::canvasContentChanged, this,
        [this]() { markProjectModified(); });
    // Refresh navigator thumbnail only when user has stopped interacting (not during
    // drawing/transform)
    m_navigatorThumbnailRefreshTimer = new QTimer(this);
    m_navigatorThumbnailRefreshTimer->setSingleShot(true);
    m_navigatorThumbnailRefreshTimer->setInterval(800);
    connect(m_navigatorThumbnailRefreshTimer, &QTimer::timeout, this, [this]() {
        if (m_canvasPanel && !m_canvasPanel->isDrawingActive()
            && !m_canvasPanel->isTransformActive() && m_navigatorPanel
            && m_navigatorPanel->contentWidget() && m_navigatorPanel->isVisible()) {
            if (auto* w
                = qobject_cast<workspace::NavigatorWidget*>(m_navigatorPanel->contentWidget())) {
                w->refreshThumbnail();
            }
        }
    });
    connect(m_canvasPanel, &workspace::CanvasPanel::canvasContentChanged, this, [this]() {
        if (m_navigatorPanel && m_navigatorPanel->contentWidget()
            && m_navigatorPanel->isVisible()) {
            m_navigatorThumbnailRefreshTimer->start();
        }
    });
    connect(m_canvasPanel, &workspace::CanvasPanel::zoomChanged, this, [this]() {
        if (m_navigatorPanel && m_navigatorPanel->contentWidget()) {
            m_navigatorPanel->contentWidget()->update();
        }
    });
    connect(m_canvasPanel, &workspace::CanvasPanel::brushOverlayPositionChanged, this,
        [this](const QPoint&) { scheduleDockLayoutSave(); });
    connect(m_canvasPanel, &workspace::CanvasPanel::toolStateOverlayPositionChanged, this,
        [this](const QPoint&) { scheduleDockLayoutSave(); });
    connect(m_canvasPanel, &workspace::CanvasPanel::stylusJoystickPositionChanged, this,
        [this](const QPoint&) { scheduleDockLayoutSave(); });
    connect(m_canvasPanel, &workspace::CanvasPanel::toolModeChanged, this,
        [this](workspace::CanvasPanel::ToolMode tool) {
            if (m_toolsPanel) {
                m_toolsPanel->setCurrentTool(toToolsPanelTool(tool));
            }
            syncCanvasColorFromWorkspaceState();
        });
    connect(m_canvasPanel, &workspace::CanvasPanel::canvasWidgetsVisibilityChanged, this, [this]() {
        scheduleDockLayoutSave();
        emit panelsVisibilityChanged();
    });
    connect(m_brushesPanel, &workspace::BrushesPanel::panelStateChanged, this,
        [this]() { scheduleDockLayoutSave(); });
    connect(m_layersPanel->layerModel(), &ruwa::core::layers::LayerModel::layersChanged, this,
        [this]() {
            if (!m_pendingNavigatorRemovedTiles.isEmpty()) {
                invalidateNavigatorTiles(m_pendingNavigatorRemovedTiles.values());
                m_pendingNavigatorRemovedTiles.clear();
            }
            if (m_pendingNavigatorFullRefresh) {
                invalidateNavigatorOverview();
                m_pendingNavigatorFullRefresh = false;
            }
            rebuildNavigatorTrackedLayerStates();
            markProjectModified();
        });
    connect(m_layersPanel->layerModel(), &ruwa::core::layers::LayerModel::layerDataChanged, this,
        [this](const LayerId& id) {
            onNavigatorTrackedLayerChanged(id);
            markProjectModified();
        });
    connect(m_layersPanel->layerModel(), &ruwa::core::layers::LayerModel::layerEffectsChanged, this,
        [this](const LayerId& id, quint64 revision) {
            const auto it = m_navigatorTrackedLayerStates.constFind(id);
            if (it == m_navigatorTrackedLayerStates.constEnd()
                || it.value().effectChainRevision != revision) {
                onNavigatorTrackedLayerChanged(id);
                markProjectModified();
            }
        });
    connect(m_layersPanel->layerModel(), &ruwa::core::layers::LayerModel::layerAboutToBeRemoved,
        this, &WorkspaceTab::onNavigatorTrackedLayerAboutToRemove);
    connect(m_layersPanel->layerModel(), &ruwa::core::layers::LayerModel::layerRemoved, this,
        [this](const LayerId&) {
            if (m_pendingNavigatorRemovedTiles.isEmpty()) {
                if (!m_pendingNavigatorFullRefresh) {
                    return;
                }
            }
            if (!m_pendingNavigatorRemovedTiles.isEmpty()) {
                invalidateNavigatorTiles(m_pendingNavigatorRemovedTiles.values());
                m_pendingNavigatorRemovedTiles.clear();
            }
            if (m_pendingNavigatorFullRefresh) {
                invalidateNavigatorOverview();
                m_pendingNavigatorFullRefresh = false;
            }
        });
    connect(m_layersPanel, &workspace::LayersPanel::layerOpacityEditStarted, this,
        &WorkspaceTab::onNavigatorOpacityEditStarted);
    connect(m_layersPanel, &workspace::LayersPanel::layerOpacityEditFinished, this,
        &WorkspaceTab::onNavigatorOpacityEditFinished);
    rebuildNavigatorTrackedLayerStates();
    connect(m_dockManager, &docking::DockManager::dragStarted, this,
        [](docking::DockPanel*) { setUiDragActive(true); });
    connect(m_dockManager, &docking::DockManager::dragFinished, this,
        [](docking::DockPanel*, bool) { setUiDragActive(false); });
    connect(m_layerPropertiesPanel, &workspace::LayerPropertiesPanel::colorPickerRequested, this,
        [this](const QColor& color, QWidget* sourceButton) {
            emit colorPickerRequested(color, sourceButton);
        });
    connect(m_layerEffectsPanel, &workspace::LayerEffectsPanel::colorPickerRequested, this,
        [this](const QColor& color, QWidget* sourceButton) {
            emit colorPickerRequested(color, sourceButton);
        });
    connect(m_layerEffectsPanel, &workspace::LayerEffectsPanel::positionPickerRequested, this,
        [this](QWidget* sourceField, const QPointF& currentPosition) {
            auto* field = qobject_cast<ruwa::ui::widgets::PositionInputField*>(sourceField);
            if (!field || !m_canvasPanel) {
                return;
            }
            QPointer<ruwa::ui::widgets::PositionInputField> guard(field);
            field->setActive(true);
            m_canvasPanel->beginPositionPicking(
                currentPosition,
                [guard](const QPointF& docPos) {
                    if (guard) {
                        guard->setActive(false);
                        guard->setPosition(docPos);
                    }
                },
                [guard]() {
                    if (guard) {
                        guard->setActive(false);
                    }
                });
        });

    // Tool selection
    connect(m_toolsPanel, &workspace::ToolsPanel::toolChanged, this,
        [this](workspace::ToolsPanel::Tool tool) {
            using CanvasTool = workspace::CanvasPanel::ToolMode;
            switch (tool) {
            case workspace::ToolsPanel::Tool::Hand:
                m_canvasPanel->setToolMode(CanvasTool::Hand);
                break;
            case workspace::ToolsPanel::Tool::Brush:
                m_canvasPanel->setToolMode(CanvasTool::Brush);
                break;
            case workspace::ToolsPanel::Tool::Blur:
                m_canvasPanel->setToolMode(CanvasTool::Blur);
                break;
            case workspace::ToolsPanel::Tool::Smudge:
                m_canvasPanel->setToolMode(CanvasTool::Smudge);
                break;
            case workspace::ToolsPanel::Tool::Liquify:
                m_canvasPanel->setToolMode(CanvasTool::Liquify);
                break;
            case workspace::ToolsPanel::Tool::Eraser:
                m_canvasPanel->setToolMode(CanvasTool::Eraser);
                break;
            case workspace::ToolsPanel::Tool::Fill:
                m_canvasPanel->setToolMode(CanvasTool::Fill);
                break;
            case workspace::ToolsPanel::Tool::ClassicFill:
                m_canvasPanel->setToolMode(CanvasTool::ClassicFill);
                break;
            case workspace::ToolsPanel::Tool::Eyedropper:
                m_canvasPanel->setToolMode(CanvasTool::Eyedropper);
                break;
            case workspace::ToolsPanel::Tool::Lasso:
                m_canvasPanel->setToolMode(CanvasTool::Lasso);
                break;
            case workspace::ToolsPanel::Tool::LassoFill:
                m_canvasPanel->setToolMode(CanvasTool::LassoFill);
                break;
            case workspace::ToolsPanel::Tool::SquareSelection:
                m_canvasPanel->setToolMode(CanvasTool::SquareSelection);
                break;
            case workspace::ToolsPanel::Tool::CircleSelection:
                m_canvasPanel->setToolMode(CanvasTool::CircleSelection);
                break;
            case workspace::ToolsPanel::Tool::Move:
                m_canvasPanel->setToolMode(CanvasTool::Move);
                break;
            case workspace::ToolsPanel::Tool::RotateView:
                m_canvasPanel->setToolMode(CanvasTool::RotateView);
                break;
            case workspace::ToolsPanel::Tool::CanvasResize:
                m_canvasPanel->setToolMode(CanvasTool::CanvasResize);
                break;
            case workspace::ToolsPanel::Tool::Zoom:
                m_canvasPanel->setToolMode(CanvasTool::Zoom);
                break;
            case workspace::ToolsPanel::Tool::Text:
                m_canvasPanel->setToolMode(CanvasTool::Text);
                break;
            }
        });

    // Action tools (Camera: copy canvas to clipboard)
    connect(m_toolsPanel, &workspace::ToolsPanel::actionToolActivated, this,
        [this](workspace::ToolsPanel::Tool tool) {
            if (tool == workspace::ToolsPanel::Tool::Camera && m_canvasPanel) {
                const QImage image = m_canvasPanel->exportCanvasImage();
                if (!image.isNull()) {
                    constexpr int max8K = 7680;
                    if (image.width() > max8K || image.height() > max8K) {
                        const QString errMsg = QCoreApplication::translate("MessagePopupManager",
                            "Image resolution exceeds 8K (7680×4320). Maximum dimension: 7680 px.");
                        ruwa::ui::widgets::MessagePopupManager::show(this, errMsg,
                            { { QCoreApplication::translate("MessagePopupManager", "OK"), false,
                                []() { } } },
                            320);
                    } else {
                        std::unique_ptr<ruwa::platform::Platform> platform(
                            ruwa::platform::Platform::create());
                        if (platform) {
                            platform->copyImageToClipboard(image);
                        }
                        ruwa::ui::widgets::MessagePopupManager::showImageCopied(this, image);
                    }
                }
            }
        });

    // Finish pending canvas edits before an incompatible layer operation mutates the document.
    connect(m_layersPanel, &workspace::LayersPanel::aboutToPerformTransformIncompatibleEdit,
        m_canvasPanel, &workspace::CanvasPanel::commitTransformBeforeDocumentMutation);
    connect(m_layersPanel, &workspace::LayersPanel::layerContentSelectionRequested, m_canvasPanel,
        &workspace::CanvasPanel::selectLayerContent);
    connect(m_layersPanel, &workspace::LayersPanel::layerTextEditRequested, m_canvasPanel,
        &workspace::CanvasPanel::startTextLayerEditing);
    connect(m_layersPanel, &workspace::LayersPanel::deleteLayerRequested, this, [this]() {
        if (m_canvasPanel) {
            m_canvasPanel->notifyContentChanged();
        }
    });
    connect(m_layersPanel, &workspace::LayersPanel::layerClearPixelContentRequested, this,
        [this](const ruwa::core::layers::LayerId& id) {
            if (m_canvasPanel) {
                m_canvasPanel->clearLayerPixelContent(id);
            }
        });
    connect(m_layersPanel, &workspace::LayersPanel::layerRasterizeSmartRequested, this,
        [this](const ruwa::core::layers::LayerId& id) {
            if (m_canvasPanel) {
                m_canvasPanel->rasterizeSmartLayer(id);
            }
        });
    connect(m_layersPanel, &workspace::LayersPanel::layerApplyMaskRequested, this,
        [this](const ruwa::core::layers::LayerId& id) {
            if (m_canvasPanel) {
                m_canvasPanel->applyLayerMask(id);
            }
        });
    connect(m_layersPanel, &workspace::LayersPanel::layerInvertMaskRequested, this,
        [this](const ruwa::core::layers::LayerId& id) {
            if (m_canvasPanel) {
                m_canvasPanel->invertLayerMask(id);
            }
        });
    connect(m_layersPanel, &workspace::LayersPanel::layerApplyEffectsRequested, this,
        [this](const ruwa::core::layers::LayerId& id) {
            if (m_canvasPanel) {
                m_canvasPanel->applyLayerEffects(id);
            }
        });

    // Layer selection (new model: LayerId)
    connect(m_layersPanel, &workspace::LayersPanel::layerSelected, this,
        [](const ruwa::core::layers::LayerId& id) {
            // TODO: Handle layer selection (e.g. canvas, properties)
            Q_UNUSED(id);
        });

    // Painting on a mask fades the color panel to grayscale (purely visual: a
    // mask only reacts to luminance), and back to color when leaving the mask.
    connect(m_layersPanel, &workspace::LayersPanel::maskEditTargetChanged, m_colorPanel,
        &workspace::ColorPanel::setMaskEditMode);

    // Color changes — forward active FG/BG slot color to brush
    connect(m_colorPanel, &workspace::ColorPanel::foregroundColorChanged, this,
        [this](const QColor& color) {
            if (m_syncingWorkspaceColorState || m_restoringWorkspaceUiState) {
                return;
            }
            m_workspaceColorStateInitialized = true;
            m_workspaceColorStateSeededFromCanvasDefaults = false;
            m_workspaceColorState.foreground = color;
            markProjectModified();
            scheduleDockLayoutSave();
        });
    connect(m_colorPanel, &workspace::ColorPanel::backgroundColorChanged, this,
        [this](const QColor& color) {
            if (m_syncingWorkspaceColorState || m_restoringWorkspaceUiState) {
                return;
            }
            m_workspaceColorStateInitialized = true;
            m_workspaceColorStateSeededFromCanvasDefaults = false;
            m_workspaceColorState.background = color;
            markProjectModified();
            scheduleDockLayoutSave();
        });
    connect(m_colorPanel, &workspace::ColorPanel::activeColorSlotChanged, this,
        [this](bool isForeground) {
            if (m_syncingWorkspaceColorState || m_restoringWorkspaceUiState) {
                return;
            }
            m_workspaceColorStateInitialized = true;
            m_workspaceColorStateSeededFromCanvasDefaults = false;
            m_workspaceColorState.editingForeground = isForeground;
            syncCanvasColorFromWorkspaceState();
            markProjectModified();
            scheduleDockLayoutSave();
        });
    connect(m_colorPanel, &workspace::ColorPanel::activeColorChanged, this, [this](const QColor&) {
        if (m_syncingWorkspaceColorState || m_restoringWorkspaceUiState) {
            return;
        }
        syncCanvasColorFromWorkspaceState();
    });

    connect(m_canvasPanel, &workspace::CanvasPanel::colorPicked, this, [this](const QColor& color) {
        if (m_restoringWorkspaceUiState) {
            return;
        }
        setWorkspaceColorForSlot(m_workspaceColorState.editingForeground, color);
        markProjectModified();
    });

    // Recent colors: add brush color when user draws (paint stroke completed)
    connect(m_canvasPanel, &workspace::CanvasPanel::strokePainted, this,
        [this]() { m_colorPanel->addColorToRecent(m_canvasPanel->currentBrushColor()); });
    connect(m_colorPanel, &workspace::ColorPanel::recentColorsChanged, this,
        [this]() { RecentColorsPersistence::save(m_colorPanel->recentColors()); });
    connect(m_colorPanel, &workspace::ColorPanel::pickerModeChanged, this,
        [this]() { scheduleDockLayoutSave(); });
    connect(m_colorPanel, &workspace::ColorPanel::channelModeChanged, this,
        [this]() { scheduleDockLayoutSave(); });
}

// ============================================================================
// Project Serialization
// ============================================================================

void WorkspaceTab::setTabTitle(const QString& tabTitle)
{
    if (m_tabTitle == tabTitle)
        return;
    m_tabTitle = tabTitle;
    markProjectModified();
    emit titleChanged(title());
}

void WorkspaceTab::setTabIconAlias(const QString& alias)
{
    if (m_tabIconAlias == alias)
        return;
    m_tabIconAlias = alias;
    setIcon(icon()); // Update BaseTab's cached icon + emit iconChanged
}

ruwa::core::serialization::ProjectData WorkspaceTab::toProjectData() const
{
    using namespace ruwa::core::serialization;

    ProjectData data;
    data.projectName = m_projectName;
    data.tabTitle = m_tabTitle;
    data.tabIconAlias = m_tabIconAlias;
    data.canvasSize = m_settings.canvasSize;
    data.canvasBoundsMode = m_settings.canvasBoundsMode;
    data.exportFrame = normalizedExportFrame(m_settings.exportFrame, m_settings.canvasSize);
    data.contentTileFormat = m_settings.tileFormat;

    // Serialize layers from model
    if (m_layersPanel) {
        const auto* model = m_layersPanel->layerModel();
        data.selectedLayerId = model->selectedLayerId();
        const auto& roots = model->rootLayers();

        std::function<LayerEntry(const std::shared_ptr<ruwa::core::layers::LayerData>&)> convert;
        convert
            = [&convert](const std::shared_ptr<ruwa::core::layers::LayerData>& ld) -> LayerEntry {
            LayerEntry entry;
            entry.id = ld->id;
            entry.name = ld->name;
            entry.type = static_cast<int>(ld->type);
            entry.visible = ld->visible;
            entry.locked = ld->locked;
            entry.expanded = ld->expanded;
            entry.opacity = ld->opacity;
            entry.blendMode = static_cast<int>(ld->blendMode);
            entry.groupCompositingMode = static_cast<int>(ld->groupCompositingMode);
            entry.displayColorIndex = ld->displayColorIndex;
            entry.backgroundColorRgba = ld->backgroundColor.rgba();
            entry.backgroundTransparent = ld->backgroundTransparent;
            entry.clippedToBelow = ld->clippedToBelow;
            writeLayerTransformFields(entry, serializedTransformForLayer(ld.get()));
            writeTextLayerFields(entry, ld.get());

            if (const auto* pixelGrid = ld->pixelGrid(); pixelGrid) {
                const int contentTileBytes
                    = static_cast<int>(aether::tileByteSize(pixelGrid->format()));
                entry.tiles.reserve(static_cast<int>(pixelGrid->tiles().size()));
                for (const auto& [key, tile] : pixelGrid->tiles()) {
                    TileEntry t;
                    t.x = key.x;
                    t.y = key.y;
                    t.pixels = QByteArray(
                        reinterpret_cast<const char*>(tile.pixels()), contentTileBytes);
                    entry.tiles.append(std::move(t));
                }
            }

            if (ld->hasMask()) {
                entry.hasMask = true;
                entry.maskEnabled = ld->maskEnabled;
                entry.maskLinked = ld->maskLinked;
                if (const auto* maskGrid = ld->maskTileGrid()) {
                    entry.maskDefaultFill = maskGrid->defaultFillPacked();
                    entry.maskTiles.reserve(static_cast<int>(maskGrid->tiles().size()));
                    for (const auto& [key, tile] : maskGrid->tiles()) {
                        TileEntry t;
                        t.x = key.x;
                        t.y = key.y;
                        if (tile.isSolid()) {
                            t.solid = true;
                            t.solidColor = tile.solidColorPacked();
                        } else {
                            t.pixels = QByteArray(reinterpret_cast<const char*>(tile.pixels()),
                                static_cast<int>(aether::TILE_BYTE_SIZE));
                        }
                        entry.maskTiles.append(std::move(t));
                    }
                }
            }

            for (const auto& child : ld->children) {
                entry.children.append(convert(child));
            }
            return entry;
        };

        for (const auto& root : roots) {
            data.rootLayers.append(convert(root));
        }
        data.layerEffects = model->toLayerEffectsEntries();
    }

    if (m_canvasPanel) {
        data.currentTool = static_cast<int>(m_canvasPanel->toolMode());
        const auto brushState
            = m_canvasPanel->persistedToolState(workspace::CanvasPanel::ToolMode::Brush);
        data.brushToolState.brushId = brushState.brushId;
        data.brushToolState.brushSize = brushState.brushSize;
        data.brushToolState.brushOpacity = brushState.brushOpacity;
        data.brushToolState.colorRgba = brushState.color.rgba();
        data.brushToolState.valid = brushState.valid;

        const auto eraserState
            = m_canvasPanel->persistedToolState(workspace::CanvasPanel::ToolMode::Eraser);
        data.eraserToolState.brushId = eraserState.brushId;
        data.eraserToolState.brushSize = eraserState.brushSize;
        data.eraserToolState.brushOpacity = eraserState.brushOpacity;
        data.eraserToolState.colorRgba = eraserState.color.rgba();
        data.eraserToolState.valid = eraserState.valid;

        const auto blurState
            = m_canvasPanel->persistedToolState(workspace::CanvasPanel::ToolMode::Blur);
        data.blurToolState.brushId = blurState.brushId;
        data.blurToolState.brushSize = blurState.brushSize;
        data.blurToolState.brushOpacity = blurState.brushOpacity;
        data.blurToolState.colorRgba = blurState.color.rgba();
        data.blurToolState.valid = blurState.valid;

        const auto smudgeState
            = m_canvasPanel->persistedToolState(workspace::CanvasPanel::ToolMode::Smudge);
        data.smudgeToolState.brushId = smudgeState.brushId;
        data.smudgeToolState.brushSize = smudgeState.brushSize;
        data.smudgeToolState.brushOpacity = smudgeState.brushOpacity;
        data.smudgeToolState.colorRgba = smudgeState.color.rgba();
        data.smudgeToolState.valid = smudgeState.valid;

        data.lassoStabilization = m_canvasPanel->lassoStabilization();
        data.lassoFillStabilization = m_canvasPanel->lassoFillStabilization();
        data.lastUsedColorRgba = m_canvasPanel->currentBrushColor().rgba();
        data.brushOverlayPosNormalized = m_canvasPanel->brushOverlayPositionNormalized();
        data.toolStateOverlayPosNormalized = m_canvasPanel->toolStateOverlayPositionNormalized();
        data.stylusJoystickPosNormalized = m_canvasPanel->stylusJoystickPositionNormalized();
        data.stylusJoystickAbovePanel = m_canvasPanel->stylusJoystickAbovePanel();
        data.canvasWidgets = m_canvasPanel->canvasWidgetVisibility();
    }

    data.foregroundColorRgba = m_workspaceColorState.foreground.rgba();
    data.backgroundColorRgba = m_workspaceColorState.background.rgba();
    data.editingForegroundColor = m_workspaceColorState.editingForeground;

    if (m_serializer) {
        data.dockLayoutState = saveDockState();
    }

    return data;
}

bool WorkspaceTab::fromProjectData(const ruwa::core::serialization::ProjectData& data)
{
    fromProjectDataStructure(data);
    fromProjectDataTiles(data);
    if (m_canvasPanel && m_layersPanel) {
        m_canvasPanel->setLayerModel(m_layersPanel->layerModel());
        m_canvasPanel->requestRender();
    }
    return true;
}

bool WorkspaceTab::fromProjectDataStructure(const ruwa::core::serialization::ProjectData& data)
{
    using namespace ruwa::core::serialization;
    using namespace ruwa::core::layers;

    m_projectName = data.projectName;
    m_tabTitle = data.tabTitle;
    m_tabIconAlias = data.tabIconAlias;
    m_settings.exportFrame = normalizedExportFrame(data.exportFrame, data.canvasSize);
    m_settings.canvasSize = data.canvasSize;
    m_settings.canvasBoundsMode = data.canvasBoundsMode;
    m_settings.tileFormat = data.contentTileFormat;
    m_serializedDockLayoutState = data.dockLayoutState;
    m_serializedBrushOverlayPosNormalized = data.brushOverlayPosNormalized;
    m_serializedToolStateOverlayPosNormalized = data.toolStateOverlayPosNormalized;
    m_serializedStylusJoystickPosNormalized = data.stylusJoystickPosNormalized;
    m_serializedStylusJoystickAbovePanel = data.stylusJoystickAbovePanel;
    m_serializedCanvasWidgets = data.canvasWidgets;
    m_workspaceColorState.foreground = QColor::fromRgba(data.foregroundColorRgba);
    m_workspaceColorState.background = QColor::fromRgba(data.backgroundColorRgba);
    m_workspaceColorState.editingForeground = data.editingForegroundColor;
    m_workspaceColorStateInitialized = true;
    m_workspaceColorStateSeededFromCanvasDefaults = false;
    m_hasSerializedWorkspaceState = !m_serializedDockLayoutState.isEmpty()
        || (m_serializedBrushOverlayPosNormalized.x() >= 0.0
            && m_serializedBrushOverlayPosNormalized.y() >= 0.0)
        || (m_serializedToolStateOverlayPosNormalized.x() >= 0.0
            && m_serializedToolStateOverlayPosNormalized.y() >= 0.0)
        || (m_serializedStylusJoystickPosNormalized.x() >= 0.0
            && m_serializedStylusJoystickPosNormalized.y() >= 0.0)
        || !m_serializedStylusJoystickAbovePanel || !m_serializedCanvasWidgets.allVisible();
    if (isInitialized()) {
        restoreUserDockLayout();
    }

    if (!m_layersPanel) {
        return false;
    }

    auto* model = m_layersPanel->layerModel();
    // Ensure the model stamps the file's format on freshly created content grids
    // (entryToLayerData reads documentTileFormat) before the tree is built.
    model->setDocumentTileFormat(data.contentTileFormat);
    m_layersPanel->setDisplayFrame(effectiveDisplayFrame());
    m_layersPanel->setThumbnailLoadingMode(true);
    // Use loadFromEntries to build the full tree at once, then refreshClippingConsistency.
    // Adding layers one-by-one via addLayerTo would call refreshClippingConsistency after
    // each add; at that point layers below are not yet present, so clippedToBelow gets
    // incorrectly reset for all layers.
    model->loadFromEntries(data.rootLayers);
    model->applyLayerEffectsEntries(data.layerEffects);
    if (auto* backgroundLayer = model->backgroundLayer()) {
        m_settings.backgroundColor = backgroundLayer->backgroundColor;
    }

    if (!data.selectedLayerId.isNull() && model->contains(data.selectedLayerId)) {
        model->setSelectedLayer(data.selectedLayerId);
    } else if (!model->rootLayers().isEmpty()) {
        model->setSelectedLayer(model->rootLayers().first()->id);
    }

    m_layersPanel->refreshLayers();
    if (m_canvasPanel) {
        m_canvasPanel->setCanvasBoundsMode(m_settings.canvasBoundsMode);
        m_canvasPanel->setCanvasSize(m_settings.canvasSize);
        m_canvasPanel->setExportFrame(exportFrame());
        refreshToolbarState();
        workspace::CanvasPanel::PersistedToolState brushState;
        brushState.brushId = data.brushToolState.brushId;
        brushState.brushSize = data.brushToolState.brushSize;
        brushState.brushOpacity = data.brushToolState.brushOpacity;
        brushState.color = QColor::fromRgba(data.brushToolState.colorRgba);
        brushState.valid = data.brushToolState.valid;
        m_canvasPanel->setPersistedToolState(workspace::CanvasPanel::ToolMode::Brush, brushState);

        workspace::CanvasPanel::PersistedToolState eraserState;
        eraserState.brushId = data.eraserToolState.brushId;
        eraserState.brushSize = data.eraserToolState.brushSize;
        eraserState.brushOpacity = data.eraserToolState.brushOpacity;
        eraserState.color = QColor::fromRgba(data.eraserToolState.colorRgba);
        eraserState.valid = data.eraserToolState.valid;
        m_canvasPanel->setPersistedToolState(workspace::CanvasPanel::ToolMode::Eraser, eraserState);

        workspace::CanvasPanel::PersistedToolState blurState;
        blurState.brushId = data.blurToolState.brushId;
        blurState.brushSize = data.blurToolState.brushSize;
        blurState.brushOpacity = data.blurToolState.brushOpacity;
        blurState.color = QColor::fromRgba(data.blurToolState.colorRgba);
        blurState.valid = data.blurToolState.valid;
        m_canvasPanel->setPersistedToolState(workspace::CanvasPanel::ToolMode::Blur, blurState);

        workspace::CanvasPanel::PersistedToolState smudgeState;
        smudgeState.brushId = data.smudgeToolState.brushId;
        smudgeState.brushSize = data.smudgeToolState.brushSize;
        smudgeState.brushOpacity = data.smudgeToolState.brushOpacity;
        smudgeState.color = QColor::fromRgba(data.smudgeToolState.colorRgba);
        smudgeState.valid = data.smudgeToolState.valid;
        m_canvasPanel->setPersistedToolState(workspace::CanvasPanel::ToolMode::Smudge, smudgeState);

        if (data.version >= 19) {
            m_canvasPanel->setLassoStabilization(data.lassoStabilization);
            m_canvasPanel->setLassoFillStabilization(data.lassoFillStabilization);
        }
    }
    if (m_toolsPanel) {
        int canvasTool = qBound(
            0, data.currentTool, static_cast<int>(workspace::CanvasPanel::ToolMode::Smudge));
        if (m_canvasPanel) {
            m_canvasPanel->setToolMode(static_cast<workspace::CanvasPanel::ToolMode>(canvasTool));
        }
        m_toolsPanel->setCurrentTool(
            toToolsPanelTool(static_cast<workspace::CanvasPanel::ToolMode>(canvasTool)));
    }
    if (m_canvasPanel) {
        // Project files saved before per-tool color support may not have valid
        // tool colors. In that case, restore from legacy last-used color first.
        const bool hasPerToolColor = data.brushToolState.valid || data.eraserToolState.valid
            || data.blurToolState.valid || data.smudgeToolState.valid;
        if (!hasPerToolColor) {
            const QColor loadedColor = QColor::fromRgba(data.lastUsedColorRgba);
            m_canvasPanel->setBrushColor(static_cast<uint8_t>(loadedColor.red()),
                static_cast<uint8_t>(loadedColor.green()), static_cast<uint8_t>(loadedColor.blue()),
                static_cast<uint8_t>(loadedColor.alpha()));
        }
        m_canvasPanel->reapplyCurrentToolState();
    }
    syncColorPanelFromWorkspaceState();
    syncCanvasColorFromWorkspaceState();
    emit titleChanged(title());
    emit iconChanged(icon());
    return true;
}

bool WorkspaceTab::fromProjectDataTiles(const ruwa::core::serialization::ProjectData& data)
{
    using namespace ruwa::core::serialization;
    using namespace ruwa::core::layers;

    if (!m_layersPanel)
        return false;

    auto* model = m_layersPanel->layerModel();
    bool loadedAnyTiles = false;

    std::function<void(const QList<LayerEntry>&)> loadTiles;
    loadTiles = [&](const QList<LayerEntry>& entries) {
        for (const auto& entry : entries) {
            if (!entry.tiles.isEmpty()) {
                LayerData* layer = model->layerById(entry.id);
                if (layer) {
                    auto* pixelGrid = layer->pixelGrid();
                    if (!pixelGrid) {
                        continue;
                    }
                    const int contentTileBytes
                        = static_cast<int>(aether::tileByteSize(pixelGrid->format()));
                    for (const auto& t : entry.tiles) {
                        if (t.pixels.size() != contentTileBytes) {
                            continue;
                        }
                        aether::TileData& dst
                            = pixelGrid->getOrCreateTile(aether::TileKey { t.x, t.y });
                        std::memcpy(dst.pixels(), t.pixels.constData(),
                            static_cast<size_t>(contentTileBytes));
                        dst.markDirty();
                        loadedAnyTiles = true;
                    }
                }
            }
            if (!entry.children.isEmpty()) {
                loadTiles(entry.children);
            }
        }
    };

    loadTiles(data.rootLayers);
    if (loadedAnyTiles) {
        model->notifyBulkLayerContentChanged();
    }
    if (m_toolsPanel) {
        int canvasTool = qBound(
            0, data.currentTool, static_cast<int>(workspace::CanvasPanel::ToolMode::Smudge));
        if (m_canvasPanel) {
            m_canvasPanel->setToolMode(static_cast<workspace::CanvasPanel::ToolMode>(canvasTool));
        }
        m_toolsPanel->setCurrentTool(
            toToolsPanelTool(static_cast<workspace::CanvasPanel::ToolMode>(canvasTool)));
    }
    if (m_canvasPanel) {
        m_canvasPanel->reapplyCurrentToolState();
    }
    syncColorPanelFromWorkspaceState();
    syncCanvasColorFromWorkspaceState();
    return true;
}

void WorkspaceTab::markProjectModified()
{
    if (m_suppressModifiedChanges) {
        return;
    }

    ++m_projectChangeRevision;
    setModified(true);
}

bool WorkspaceTab::waitForSaveToFinish()
{
    if (!m_saveInProgress) {
        return true;
    }

    bool success = false;
    QEventLoop loop;
    connect(
        this, &WorkspaceTab::saveFinished, &loop,
        [&success, &loop](bool ok) {
            success = ok;
            loop.quit();
        },
        Qt::SingleShotConnection);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    return success;
}

bool WorkspaceTab::saveProject()
{
    if (m_filePath.isEmpty()) {
        return false; // Caller should use saveProjectAs() with a dialog
    }
    return saveProjectAs(m_filePath);
}

bool WorkspaceTab::saveProjectAs(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return false;
    }

    if (m_saveInProgress) {
        waitForSaveToFinish();
        if (!isModified() && m_filePath == filePath) {
            return true;
        }
    }

    return performSave(filePath, true);
}

bool WorkspaceTab::saveProjectAsync()
{
    if (m_filePath.isEmpty()) {
        return false;
    }

    return saveProjectAsAsync(m_filePath);
}

bool WorkspaceTab::saveProjectAsAsync(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return false;
    }

    if (m_saveInProgress) {
        return false;
    }

    return startAsyncSave(filePath);
}

bool WorkspaceTab::performSave(const QString& filePath, bool allowUserInputWhileWaiting)
{
    m_saveInProgress = true;

    SnapshotSaveProjectPayload payload;
    payload.filePath = filePath;
    payload.snapshot = captureProjectSaveSnapshot();
    const quint64 savedRevision = m_projectChangeRevision;

    QFutureWatcher<SaveProjectResult> watcher;
    QEventLoop loop;
    SaveProjectResult result;

    connect(
        &watcher, &QFutureWatcher<SaveProjectResult>::finished, this, [&watcher, &result, &loop]() {
            result = watcher.result();
            loop.quit();
        });
    watcher.setFuture(QtConcurrent::run(saveProjectSnapshotInBackground, std::move(payload)));
    if (allowUserInputWhileWaiting) {
        loop.exec();
    } else {
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    m_saveInProgress = false;

    if (!result.success) {
        finalizeFailedSave(filePath, result.errorMessage);
        emit saveFinished(false);
        return false;
    }

    finalizeSuccessfulSave(filePath, savedRevision);
    emit saveFinished(true);
    return true;
}

bool WorkspaceTab::startAsyncSave(const QString& filePath)
{
    m_saveInProgress = true;

    SaveProjectPayload payload;
    payload.filePath = filePath;
    payload.data = toProjectData();
    payload.thumbnail = captureProjectThumbnail(256);
    const quint64 savedRevision = m_projectChangeRevision;

    auto* watcher = new QFutureWatcher<SaveProjectResult>(this);
    connect(watcher, &QFutureWatcher<SaveProjectResult>::finished, this,
        [this, watcher, filePath, savedRevision]() {
            const SaveProjectResult result = watcher->result();
            watcher->deleteLater();
            m_saveInProgress = false;

            if (!result.success) {
                finalizeFailedSave(filePath, result.errorMessage);
                emit saveFinished(false);
                return;
            }

            finalizeSuccessfulSave(filePath, savedRevision);
            emit saveFinished(true);
        });
    watcher->setFuture(QtConcurrent::run(saveProjectInBackground, std::move(payload)));
    return true;
}

QImage WorkspaceTab::captureProjectThumbnail(int maxSize) const
{
    if (!m_canvasPanel) {
        return QImage();
    }

    const QPixmap thumb = m_canvasPanel->grabCanvasThumbnail(maxSize);
    return thumb.isNull() ? QImage() : thumb.toImage();
}

WorkspaceTab::ProjectSaveSnapshot WorkspaceTab::captureProjectSaveSnapshot() const
{
    ProjectSaveSnapshot snapshot;
    snapshot.projectName = m_projectName;
    snapshot.tabTitle = m_tabTitle;
    snapshot.tabIconAlias = m_tabIconAlias;
    snapshot.canvasSize = m_settings.canvasSize;
    snapshot.canvasBoundsMode = m_settings.canvasBoundsMode;
    snapshot.exportFrame = normalizedExportFrame(m_settings.exportFrame, m_settings.canvasSize);
    snapshot.tileFormat = m_settings.tileFormat;

    if (m_layersPanel) {
        const auto* model = m_layersPanel->layerModel();
        snapshot.selectedLayerId = model->selectedLayerId();
        snapshot.rootLayers = model->rootLayers();
    }

    if (m_canvasPanel) {
        snapshot.currentTool = static_cast<int>(m_canvasPanel->toolMode());

        const auto brushState
            = m_canvasPanel->persistedToolState(workspace::CanvasPanel::ToolMode::Brush);
        snapshot.brushToolState.brushId = brushState.brushId;
        snapshot.brushToolState.brushSize = brushState.brushSize;
        snapshot.brushToolState.brushOpacity = brushState.brushOpacity;
        snapshot.brushToolState.colorRgba = brushState.color.rgba();
        snapshot.brushToolState.valid = brushState.valid;

        const auto eraserState
            = m_canvasPanel->persistedToolState(workspace::CanvasPanel::ToolMode::Eraser);
        snapshot.eraserToolState.brushId = eraserState.brushId;
        snapshot.eraserToolState.brushSize = eraserState.brushSize;
        snapshot.eraserToolState.brushOpacity = eraserState.brushOpacity;
        snapshot.eraserToolState.colorRgba = eraserState.color.rgba();
        snapshot.eraserToolState.valid = eraserState.valid;

        const auto blurState
            = m_canvasPanel->persistedToolState(workspace::CanvasPanel::ToolMode::Blur);
        snapshot.blurToolState.brushId = blurState.brushId;
        snapshot.blurToolState.brushSize = blurState.brushSize;
        snapshot.blurToolState.brushOpacity = blurState.brushOpacity;
        snapshot.blurToolState.colorRgba = blurState.color.rgba();
        snapshot.blurToolState.valid = blurState.valid;

        const auto smudgeStateSnap
            = m_canvasPanel->persistedToolState(workspace::CanvasPanel::ToolMode::Smudge);
        snapshot.smudgeToolState.brushId = smudgeStateSnap.brushId;
        snapshot.smudgeToolState.brushSize = smudgeStateSnap.brushSize;
        snapshot.smudgeToolState.brushOpacity = smudgeStateSnap.brushOpacity;
        snapshot.smudgeToolState.colorRgba = smudgeStateSnap.color.rgba();
        snapshot.smudgeToolState.valid = smudgeStateSnap.valid;

        snapshot.lassoStabilization = m_canvasPanel->lassoStabilization();
        snapshot.lassoFillStabilization = m_canvasPanel->lassoFillStabilization();
        snapshot.lastUsedColorRgba = m_canvasPanel->currentBrushColor().rgba();
        snapshot.brushOverlayPosNormalized = m_canvasPanel->brushOverlayPositionNormalized();
        snapshot.toolStateOverlayPosNormalized
            = m_canvasPanel->toolStateOverlayPositionNormalized();
        snapshot.stylusJoystickPosNormalized = m_canvasPanel->stylusJoystickPositionNormalized();
        snapshot.stylusJoystickAbovePanel = m_canvasPanel->stylusJoystickAbovePanel();
        snapshot.canvasWidgets = m_canvasPanel->canvasWidgetVisibility();
    }

    snapshot.foregroundColorRgba = m_workspaceColorState.foreground.rgba();
    snapshot.backgroundColorRgba = m_workspaceColorState.background.rgba();
    snapshot.editingForegroundColor = m_workspaceColorState.editingForeground;
    if (m_serializer) {
        snapshot.dockLayoutState = saveDockState();
    }

    return snapshot;
}

WorkspaceTab::WorkspaceStateSnapshot WorkspaceTab::captureWorkspaceStateSnapshot() const
{
    WorkspaceStateSnapshot snapshot;
    snapshot.foregroundColorRgba = m_workspaceColorState.foreground.rgba();
    snapshot.backgroundColorRgba = m_workspaceColorState.background.rgba();
    snapshot.editingForegroundColor = m_workspaceColorState.editingForeground;

    if (m_serializer) {
        snapshot.dockLayoutState = saveDockState();
    }

    if (!m_canvasPanel) {
        return snapshot;
    }

    const QPointF brushNorm = m_canvasPanel->brushOverlayPositionNormalized();
    if (brushNorm.x() >= 0.0 && brushNorm.y() >= 0.0 && brushNorm.x() <= 1.0
        && brushNorm.y() <= 1.0) {
        snapshot.brushOverlayPosNormalized = brushNorm;
        snapshot.hasBrushOverlayPos = true;
    }

    const QPointF toolStateNorm = m_canvasPanel->toolStateOverlayPositionNormalized();
    if (toolStateNorm.x() >= 0.0 && toolStateNorm.y() >= 0.0 && toolStateNorm.x() <= 1.0
        && toolStateNorm.y() <= 1.0) {
        snapshot.toolStateOverlayPosNormalized = toolStateNorm;
        snapshot.hasToolStateOverlayPos = true;
    }

    const QPointF joystickNorm = m_canvasPanel->stylusJoystickPositionNormalized();
    if (joystickNorm.x() >= 0.0 && joystickNorm.y() >= 0.0 && joystickNorm.x() <= 1.0
        && joystickNorm.y() <= 1.0) {
        snapshot.stylusJoystickPosNormalized = joystickNorm;
        snapshot.hasStylusJoystickPos = true;
    }

    snapshot.stylusJoystickAbovePanel = m_canvasPanel->stylusJoystickAbovePanel();
    snapshot.canvasWidgets = m_canvasPanel->canvasWidgetVisibility();
    return snapshot;
}

void WorkspaceTab::applyPresetCanvasWidgetState(const docking::DockLayoutPreset& preset)
{
    if (!m_canvasPanel) {
        return;
    }

    const bool hasExplicitOverlayPositions
        = preset.hasBrushOverlayPos || preset.hasToolStateOverlayPos || preset.hasStylusJoystickPos;
    if (!preset.hasSerializedDockState() && !hasExplicitOverlayPositions) {
        m_canvasPanel->resetCanvasOverlaysToDefault();
    }

    m_canvasPanel->setCanvasWidgetVisibility(preset.canvasWidgets);
    m_canvasPanel->setPendingStylusJoystickAbovePanel(preset.stylusJoystickAbovePanel);

    if (preset.hasBrushOverlayPos) {
        m_canvasPanel->setBrushOverlayPositionFromNormalized(preset.brushOverlayPosNormalized);
    }
    if (preset.hasToolStateOverlayPos) {
        m_canvasPanel->setToolStateOverlayPositionFromNormalized(
            preset.toolStateOverlayPosNormalized);
    }
    if (preset.hasStylusJoystickPos) {
        m_canvasPanel->setStylusJoystickPositionFromNormalized(preset.stylusJoystickPosNormalized);
    }
}

void WorkspaceTab::writeWorkspaceStateSnapshot(const WorkspaceStateSnapshot& snapshot)
{
    QSettings settings;
    if (!snapshot.dockLayoutState.isEmpty()) {
        settings.setValue(kWorkspaceDockLayoutKey, snapshot.dockLayoutState);
    }
    if (snapshot.hasBrushOverlayPos) {
        settings.setValue(kWorkspaceBrushOverlayPosKey, snapshot.brushOverlayPosNormalized);
    }
    if (snapshot.hasToolStateOverlayPos) {
        settings.setValue(kWorkspaceToolStateOverlayPosKey, snapshot.toolStateOverlayPosNormalized);
    }
    if (snapshot.hasStylusJoystickPos) {
        settings.setValue(kWorkspaceStylusJoystickPosKey, snapshot.stylusJoystickPosNormalized);
    }
    settings.setValue(kWorkspaceStylusJoystickAbovePanelKey, snapshot.stylusJoystickAbovePanel);
    for (const ruwa::ui::CanvasWidget widget : ruwa::ui::kCanvasWidgets) {
        settings.setValue(canvasWidgetVisibleKey(widget), snapshot.canvasWidgets[widget]);
    }
    settings.setValue(
        kWorkspaceForegroundColorKey, static_cast<quint32>(snapshot.foregroundColorRgba));
    settings.setValue(
        kWorkspaceBackgroundColorKey, static_cast<quint32>(snapshot.backgroundColorRgba));
    settings.setValue(kWorkspaceEditingForegroundColorKey, snapshot.editingForegroundColor);
    settings.sync();
}

void WorkspaceTab::flushWorkspaceStateSync()
{
    if (!m_workspaceStateSyncPending) {
        return;
    }

    if (!m_workspaceStateFlushWatcher) {
        writeWorkspaceStateSnapshot(m_pendingWorkspaceStateSnapshot);
        m_workspaceStateSyncPending = false;
        return;
    }

    if (m_workspaceStateFlushWatcher->isRunning()) {
        return;
    }

    const WorkspaceStateSnapshot snapshot = m_pendingWorkspaceStateSnapshot;
    m_workspaceStateSyncPending = false;
    m_workspaceStateFlushWatcher->setFuture(
        QtConcurrent::run([snapshot]() { writeWorkspaceStateSnapshot(snapshot); }));
}

void WorkspaceTab::discardPendingWorkspaceStateSync()
{
    if (m_dockLayoutSaveTimer) {
        m_dockLayoutSaveTimer->stop();
    }
    m_workspaceStateSyncPending = false;
    if (m_workspaceStateFlushWatcher) {
        m_workspaceStateFlushWatcher->waitForFinished();
    }
}

void WorkspaceTab::finalizeSuccessfulSave(const QString& filePath, quint64 savedRevision)
{
    m_filePath = filePath;
    if (savedRevision == m_projectChangeRevision) {
        setModified(false);
    }

    ruwa::core::serialization::RecentProjectsManager::instance().addEntry(
        filePath, m_projectName, m_settings.canvasSize, m_tabIconAlias);
}

void WorkspaceTab::finalizeFailedSave(const QString& filePath, const QString& errorMessage)
{
    const QString message = tr("Failed to save project:\n%1").arg(errorMessage);
    ruwa::ui::widgets::MessagePopupManager::show(
        this, message, { { tr("OK"), false, []() { } } }, 360);
}

WorkspaceTab* WorkspaceTab::loadFromFile(const QString& filePath, QWidget* parent)
{
    ruwa::core::serialization::ProjectSerializer serializer;
    ruwa::core::serialization::ProjectData projData;

    if (!serializer.load(filePath, projData)) {
        return nullptr;
    }

    return createFromLoadedData(std::move(projData), filePath, parent);
}

WorkspaceTab* WorkspaceTab::createFromLoadedData(
    ruwa::core::serialization::ProjectData&& projData, const QString& filePath, QWidget* parent)
{
    return createFromLoadedData(std::move(projData), filePath, QUuid(), true, parent);
}

WorkspaceTab* WorkspaceTab::createFromLoadedData(ruwa::core::serialization::ProjectData&& projData,
    const QString& filePath, const QUuid& tabId, bool updateRecentProjects, QWidget* parent)
{
    ProjectSettings settings;
    settings.name = projData.projectName;
    settings.exportFrame = normalizedExportFrame(projData.exportFrame, projData.canvasSize);
    settings.canvasSize = projData.canvasSize;
    settings.canvasBoundsMode = projData.canvasBoundsMode;
    settings.tileFormat = projData.contentTileFormat;

    auto* tab = new WorkspaceTab(settings, tabId, parent);
    tab->m_filePath = filePath;
    tab->m_tabTitle = projData.tabTitle;
    tab->m_tabIconAlias = projData.tabIconAlias;

    // Defer layer population until after initialize()
    tab->m_pendingProjectData
        = std::make_unique<ruwa::core::serialization::ProjectData>(std::move(projData));

    if (updateRecentProjects && !filePath.isEmpty()) {
        ruwa::core::serialization::RecentProjectsManager::instance().addEntry(
            filePath, settings.name, settings.canvasSize, tab->m_tabIconAlias);
    }

    return tab;
}

WorkspaceTab* WorkspaceTab::createLoadingPlaceholder(const QString& filePath, QWidget* parent)
{
    QFileInfo info(filePath);

    ProjectSettings settings;
    settings.name = info.completeBaseName().isEmpty()
        ? QCoreApplication::translate("WorkspaceTab", "Loading Project")
        : info.completeBaseName();
    settings.canvasSize = QSize(1920, 1080);
    settings.exportFrame = normalizedExportFrame(settings.exportFrame, settings.canvasSize);

    auto* tab = new WorkspaceTab(settings, parent);
    tab->m_filePath = filePath;
    tab->m_tabTitle = settings.name;
    tab->m_waitingForAsyncProjectData = true;
    return tab;
}

void WorkspaceTab::acceptLoadedProjectData(
    ruwa::core::serialization::ProjectData&& data, const QString& filePath)
{
    m_filePath = filePath;
    m_projectName = data.projectName;
    m_tabTitle = data.tabTitle;
    m_tabIconAlias = data.tabIconAlias;
    m_settings.exportFrame = normalizedExportFrame(data.exportFrame, data.canvasSize);
    m_settings.canvasSize = data.canvasSize;
    m_settings.canvasBoundsMode = data.canvasBoundsMode;
    m_settings.tileFormat = data.contentTileFormat;
    m_waitingForAsyncProjectData = false;
    m_pendingProjectData
        = std::make_unique<ruwa::core::serialization::ProjectData>(std::move(data));

    ruwa::core::serialization::RecentProjectsManager::instance().addEntry(
        filePath, m_projectName, m_settings.canvasSize, m_tabIconAlias);

    m_initialThumbnailCaptureScheduled = false;
    if (m_asyncStartupCompleted) {
        scheduleRecentProjectsThumbnailCapture();
    }

    emit titleChanged(title());
    emit iconChanged(icon());

    if (isInitialized()) {
        if (m_loadingTitleLabel) {
            m_loadingTitleLabel->setText(tr("Loading workspace"));
        }
        showLoadingShell(tr("Preparing project data..."));
        queuePostTransitionInitialization();
    }
}

WorkspaceTab* WorkspaceTab::duplicate(WorkspaceTab* source, QWidget* parent)
{
    if (!source)
        return nullptr;

    ruwa::core::serialization::ProjectData data = source->toProjectData();
    data.projectName = data.projectName + QLatin1String(" (copy)");
    if (!data.tabTitle.isEmpty()) {
        data.tabTitle = data.tabTitle + QLatin1String(" (copy)");
    } else {
        data.tabTitle = data.projectName;
    }

    ProjectSettings settings;
    settings.name = data.projectName;
    settings.exportFrame = normalizedExportFrame(data.exportFrame, data.canvasSize);
    settings.canvasSize = data.canvasSize;
    settings.canvasBoundsMode = data.canvasBoundsMode;
    settings.tileFormat = data.contentTileFormat;
    settings.templateType = QStringLiteral("RGB Color");

    auto* tab = new WorkspaceTab(settings, parent);
    tab->m_tabTitle = data.tabTitle;
    tab->m_tabIconAlias = data.tabIconAlias;
    tab->m_pendingProjectData
        = std::make_unique<ruwa::core::serialization::ProjectData>(std::move(data));

    return tab;
}

void WorkspaceTab::flushPendingStartupImageImport()
{
    if ((m_pendingStartupImageImportPaths.isEmpty() && m_pendingStartupImageImports.isEmpty())
        || !m_canvasPanel) {
        return;
    }
    QStringList paths;
    paths.swap(m_pendingStartupImageImportPaths);
    if (!paths.isEmpty()) {
        m_canvasPanel->importImageFilesBelowSelectedKeepingSelection(paths);
    }

    QList<PendingStartupImageImport> imports;
    imports.swap(m_pendingStartupImageImports);
    for (const PendingStartupImageImport& import : imports) {
        m_canvasPanel->importImageBelowSelectedKeepingSelection(import.image, import.layerName);
    }
}

bool WorkspaceTab::importImageFilesBelowSelectedKeepingSelection(const QStringList& filePaths)
{
    if (filePaths.isEmpty()) {
        return false;
    }
    if (!m_canvasPanel || !m_asyncStartupCompleted) {
        m_pendingStartupImageImportPaths.append(filePaths);
        return true;
    }
    m_canvasPanel->importImageFilesBelowSelectedKeepingSelection(filePaths);
    return true;
}

void WorkspaceTab::promptImportImageFiles(const QStringList& filePaths)
{
    if (filePaths.isEmpty() || !m_canvasPanel) {
        return;
    }
    if (!m_asyncStartupCompleted) {
        m_pendingPromptImportPaths.append(filePaths);
        return;
    }
    m_canvasPanel->promptImportImageFiles(filePaths);
}

bool WorkspaceTab::copyCanvasToClipboard()
{
    return m_canvasPanel && m_canvasPanel->copyCanvasToClipboard();
}

bool WorkspaceTab::isLayerClipboardTargetActive() const
{
    if (!m_layersPanel) {
        return false;
    }

    const QPoint cursorPos = QCursor::pos();
    const bool isOverLayersPanel
        = m_layersPanel->rect().contains(m_layersPanel->mapFromGlobal(cursorPos));

    QWidget* focus = QApplication::focusWidget();
    const bool hasLayerFocus
        = focus && (focus == m_layersPanel || m_layersPanel->isAncestorOf(focus));

    return isOverLayersPanel || hasLayerFocus;
}

bool WorkspaceTab::handleCutRequest()
{
    if (!m_layersPanel || !isLayerClipboardTargetActive()) {
        return false;
    }
    if (!m_layersPanel->selectedLayer()) {
        return false;
    }

    if (m_canvasPanel) {
        m_canvasPanel->commitTransformBeforeDocumentMutation();
    }
    QList<std::shared_ptr<LayerData>> snapshots;
    if (!m_layersPanel->copySelectedLayerSnapshots(&snapshots)) {
        return false;
    }

    if (!m_layersPanel->deleteSelectedLayers()) {
        return false;
    }

    m_layerCopyArmed = false;
    m_layerCutArmed = true;
    m_layerCutClipboard = std::move(snapshots);
    return true;
}

bool WorkspaceTab::handleCopyRequest()
{
    if (!m_layersPanel) {
        m_layerCopyArmed = false;
        return false;
    }

    const bool hasSelectedLayer = m_layersPanel->selectedLayer() != nullptr;
    m_layerCopyArmed = isLayerClipboardTargetActive() && hasSelectedLayer;
    if (m_layerCopyArmed) {
        m_layerCutArmed = false;
        m_layerCutClipboard.clear();
    }
    return m_layerCopyArmed;
}

bool WorkspaceTab::handlePasteRequest()
{
    if (m_layerCutArmed && m_layersPanel
        && m_layersPanel->pasteLayerSnapshots(m_layerCutClipboard)) {
        return true;
    }

    if (m_layerCopyArmed && m_layersPanel && m_layersPanel->duplicateSelectedLayers()) {
        return true;
    }

    if (m_canvasPanel && m_canvasPanel->importImageFromClipboard()) {
        return true;
    }

    return false;
}

// ============================================================================
// Auto-Save (per-workspace timer)
// ============================================================================

void WorkspaceTab::startAutoSaveTimer()
{
    stopAutoSaveTimer();
    const int intervalMin
        = ruwa::core::SettingsManager::instance().settings().editor.autoSaveInterval;
    if (intervalMin > 0) {
        m_autoSaveTimer->start(intervalMin * 60 * 1000); // minutes -> ms
    }
}

void WorkspaceTab::stopAutoSaveTimer()
{
    m_autoSaveTimer->stop();
}

// ============================================================================
// Dock State Management
// ============================================================================

QByteArray WorkspaceTab::saveDockState() const
{
    if (!m_serializer)
        return QByteArray();

    QJsonObject state = m_serializer->saveState();

    // Save hidden panel placements for restore when user shows them
    QJsonObject hiddenPlacements;
    auto addPlacement
        = [&hiddenPlacements](const char* key, const std::optional<docking::PanelPlacement>& p) {
              if (p) {
                  hiddenPlacements[QLatin1String(key)] = p->toJson();
              }
          };
    addPlacement("Tools", m_savedToolsPlacement);
    addPlacement("Brushes", m_savedBrushesPlacement);
    addPlacement("Layers", m_savedLayersPlacement);
    addPlacement("Layer Properties", m_savedLayerPropertiesPlacement);
    addPlacement("Layer Effects", m_savedLayerEffectsPlacement);
    addPlacement("Color", m_savedColorPlacement);
    addPlacement("Navigator", m_savedNavigatorPlacement);
    if (!hiddenPlacements.isEmpty()) {
        state["hiddenPlacements"] = hiddenPlacements;
    }

    return QJsonDocument(state).toJson(QJsonDocument::Compact);
}

bool WorkspaceTab::restoreDockState(const QByteArray& state)
{
    if (!m_serializer || state.isEmpty())
        return false;

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(state, &error);
    if (error.error != QJsonParseError::NoError) {
        return false;
    }

    QJsonObject stateObj = doc.object();

    // Restore hidden panel placements before passing to serializer
    const QJsonObject hiddenPlacements = stateObj["hiddenPlacements"].toObject();
    auto restorePlacement
        = [&hiddenPlacements](const char* key) -> std::optional<docking::PanelPlacement> {
        QJsonObject obj = hiddenPlacements[QLatin1String(key)].toObject();
        if (obj.isEmpty())
            return std::nullopt;
        return docking::PanelPlacement::fromJson(obj);
    };
    m_savedToolsPlacement = restorePlacement("Tools");
    m_savedBrushesPlacement = restorePlacement("Brushes");
    m_savedLayersPlacement = restorePlacement("Layers");
    m_savedLayerPropertiesPlacement = restorePlacement("Layer Properties");
    m_savedLayerEffectsPlacement = restorePlacement("Layer Effects");
    m_savedColorPlacement = restorePlacement("Color");
    m_savedNavigatorPlacement = restorePlacement("Navigator");

    return m_serializer->restoreFromByteArray(state);
}

void WorkspaceTab::resetDockLayout()
{
    if (!m_serializer || !m_dockManager) {
        return;
    }

    // Drop potentially corrupted persisted state before rebuilding defaults.
    discardPendingWorkspaceStateSync();
    QSettings settings;
    settings.remove(kWorkspaceDockLayoutKey);
    settings.remove(kWorkspaceBrushOverlayPosKey);
    settings.remove(kWorkspaceToolStateOverlayPosKey);
    settings.remove(kWorkspaceStylusJoystickPosKey);
    settings.remove(kWorkspaceStylusJoystickAbovePanelKey);
    for (const ruwa::ui::CanvasWidget widget : ruwa::ui::kCanvasWidgets) {
        settings.remove(canvasWidgetVisibleKey(widget));
    }
    settings.sync();

    m_savedToolsPlacement.reset();
    m_savedBrushesPlacement.reset();
    m_savedBrushSettingsPlacement.reset();
    m_savedLayersPlacement.reset();
    m_savedLayerPropertiesPlacement.reset();
    m_savedColorPlacement.reset();
    m_savedNavigatorPlacement.reset();
    m_serializedDockLayoutState.clear();
    m_serializedBrushOverlayPosNormalized = QPointF(-1.0, -1.0);
    m_serializedToolStateOverlayPosNormalized = QPointF(-1.0, -1.0);
    m_serializedStylusJoystickPosNormalized = QPointF(-1.0, -1.0);
    m_serializedStylusJoystickAbovePanel = true;
    m_serializedCanvasWidgets = ruwa::ui::CanvasWidgetVisibility {};
    m_hasSerializedWorkspaceState = false;

    const bool wasRestoring = m_restoringDockLayout;
    m_restoringDockLayout = true;
    const docking::DockLayoutPreset defaultPreset = docking::DockLayoutPreset::defaultWorkspace();
    const bool restored = defaultPreset.hasSerializedDockState()
        ? restoreDockState(defaultPreset.dockState)
        : m_serializer->applyPreset(defaultPreset);

    if (restored) {
        applyPresetCanvasWidgetState(defaultPreset);
    }

    m_restoringDockLayout = wasRestoring;
    if (!restored) {
        return;
    }

    emit panelsVisibilityChanged();

    m_workspaceStateDirty = true;
    saveDockLayoutNow();
}

void WorkspaceTab::applyLayoutPreset(const docking::DockLayoutPreset& preset)
{
    if (!m_serializer || !m_dockContainer) {
        return;
    }

    const bool hadAnimations = m_dockContainer->animationsEnabled();
    const QByteArray fallbackLayout = saveDockState();

    m_restoringDockLayout = true;
    m_dockContainer->setAnimationsEnabled(false);

    bool restored = false;
    if (preset.hasSerializedDockState()) {
        restored = restoreDockState(preset.dockState);
    } else {
        restored = m_serializer->applyPreset(preset);
    }

    if (restored) {
        applyPresetCanvasWidgetState(preset);
    } else if (!fallbackLayout.isEmpty()) {
        restoreDockState(fallbackLayout);
    }

    m_dockContainer->setAnimationsEnabled(hadAnimations);
    m_restoringDockLayout = false;

    if (!restored) {
        return;
    }

    emit panelsVisibilityChanged();
    m_workspaceStateDirty = true;
    saveDockLayoutNow();
}

bool WorkspaceTab::createCurrentLayoutPreset(
    docking::DockLayoutPreset* outPreset, QString* errorMessage) const
{
    if (!outPreset) {
        if (errorMessage) {
            *errorMessage = tr("Internal error: missing export target.");
        }
        return false;
    }
    if (!m_serializer || !m_dockContainer) {
        if (errorMessage) {
            *errorMessage = tr("Workspace is not ready.");
        }
        return false;
    }
    if (!m_dockContainer->layoutRoot()) {
        if (errorMessage) {
            *errorMessage = tr("Saving this layout is not supported.");
        }
        return false;
    }

    auto& store = ruwa::ui::docking::DockLayoutPresetStore::instance();
    docking::DockLayoutPreset preset
        = m_serializer->createPresetFromCurrent(store.suggestUniqueName(tr("My layout")));
    const WorkspaceStateSnapshot snapshot = captureWorkspaceStateSnapshot();
    preset.dockState = snapshot.dockLayoutState;
    preset.brushOverlayPosNormalized = snapshot.brushOverlayPosNormalized;
    preset.toolStateOverlayPosNormalized = snapshot.toolStateOverlayPosNormalized;
    preset.stylusJoystickPosNormalized = snapshot.stylusJoystickPosNormalized;
    preset.hasBrushOverlayPos = snapshot.hasBrushOverlayPos;
    preset.hasToolStateOverlayPos = snapshot.hasToolStateOverlayPos;
    preset.hasStylusJoystickPos = snapshot.hasStylusJoystickPos;
    preset.stylusJoystickAbovePanel = snapshot.stylusJoystickAbovePanel;
    preset.canvasWidgets = snapshot.canvasWidgets;
    if (preset.dockState.isEmpty()
        && (preset.layoutTree.isEmpty()
            || !preset.layoutTree.value(QStringLiteral("hasRoot")).toBool(false))) {
        if (errorMessage) {
            *errorMessage = tr("Nothing to save (empty layout).");
        }
        return false;
    }

    *outPreset = std::move(preset);
    return true;
}

bool WorkspaceTab::captureCurrentLayoutAsPreset(QString* errorMessage)
{
    docking::DockLayoutPreset preset;
    if (!createCurrentLayoutPreset(&preset, errorMessage)) {
        return false;
    }

    auto& store = ruwa::ui::docking::DockLayoutPresetStore::instance();
    store.addCustomPreset(preset);
    return true;
}

void WorkspaceTab::applyPanelPlacement(
    docking::DockPanel* panel, const docking::PanelPlacement& placement)
{
    if (!panel || !m_dockManager)
        return;

    if (placement.relativeTo.isEmpty()) {
        m_dockManager->showPanel(panel, placement.position);
    } else {
        docking::DockPanel* ref = m_dockManager->panelByPersistentKey(placement.relativeTo);
        if (!ref) {
            ref = m_dockManager->panelByTitle(placement.relativeTo);
        }
        if (ref && !ref->isHidden()) {
            m_dockManager->addPanelRelativeTo(panel, ref, placement.position);
        } else {
            m_dockManager->showPanel(panel, placement.position);
        }
    }
}

void WorkspaceTab::setToolsPanelVisible(bool visible)
{
    if (!m_toolsPanel || !m_dockManager)
        return;
    if (visible && m_toolsPanel->isHidden()) {
        if (m_savedToolsPlacement) {
            applyPanelPlacement(m_toolsPanel, *m_savedToolsPlacement);
        } else {
            m_dockManager->showPanel(m_toolsPanel, docking::DockPosition::Left);
        }
    } else if (!visible && !m_toolsPanel->isHidden()) {
        if (m_dockContainer) {
            m_savedToolsPlacement = m_dockContainer->getPanelPlacement(m_toolsPanel);
        }
        m_dockManager->closePanel(m_toolsPanel);
    }
}

void WorkspaceTab::setBrushesPanelVisible(bool visible)
{
    if (!m_brushesPanel || !m_dockManager)
        return;
    if (visible && m_brushesPanel->isHidden()) {
        if (m_savedBrushesPlacement) {
            applyPanelPlacement(m_brushesPanel, *m_savedBrushesPlacement);
        } else if (m_toolsPanel && !m_toolsPanel->isHidden()) {
            m_dockManager->addPanelRelativeTo(
                m_brushesPanel, m_toolsPanel, docking::DockPosition::Bottom);
        } else {
            m_dockManager->showPanel(m_brushesPanel, docking::DockPosition::Left);
        }
    } else if (!visible && !m_brushesPanel->isHidden()) {
        if (m_dockContainer) {
            m_savedBrushesPlacement = m_dockContainer->getPanelPlacement(m_brushesPanel);
        }
        m_dockManager->closePanel(m_brushesPanel);
    }
}

void WorkspaceTab::setBrushSettingsPanelVisible(bool visible)
{
    if (!m_brushSettingsPanel || !m_dockManager)
        return;
    if (visible && m_brushSettingsPanel->isHidden()) {
        if (m_savedBrushSettingsPlacement) {
            applyPanelPlacement(m_brushSettingsPanel, *m_savedBrushSettingsPlacement);
        } else if (m_brushesPanel && !m_brushesPanel->isHidden()) {
            m_dockManager->addPanelRelativeTo(
                m_brushSettingsPanel, m_brushesPanel, docking::DockPosition::Bottom);
        } else {
            m_dockManager->showPanel(m_brushSettingsPanel, docking::DockPosition::Left);
        }
    } else if (!visible && !m_brushSettingsPanel->isHidden()) {
        if (m_dockContainer) {
            m_savedBrushSettingsPlacement
                = m_dockContainer->getPanelPlacement(m_brushSettingsPanel);
        }
        m_dockManager->closePanel(m_brushSettingsPanel);
    }
}

void WorkspaceTab::setLayersPanelVisible(bool visible)
{
    if (!m_layersPanel || !m_dockManager)
        return;
    if (visible && m_layersPanel->isHidden()) {
        if (m_savedLayersPlacement) {
            applyPanelPlacement(m_layersPanel, *m_savedLayersPlacement);
        } else {
            m_dockManager->showPanel(m_layersPanel, docking::DockPosition::Right);
        }
    } else if (!visible && !m_layersPanel->isHidden()) {
        if (m_dockContainer) {
            m_savedLayersPlacement = m_dockContainer->getPanelPlacement(m_layersPanel);
        }
        m_dockManager->closePanel(m_layersPanel);
    }
}

void WorkspaceTab::setLayerPropertiesPanelVisible(bool visible)
{
    if (!m_layerPropertiesPanel || !m_dockManager)
        return;
    if (visible && m_layerPropertiesPanel->isHidden()) {
        if (m_savedLayerPropertiesPlacement) {
            applyPanelPlacement(m_layerPropertiesPanel, *m_savedLayerPropertiesPlacement);
        } else {
            m_dockManager->addPanelRelativeTo(
                m_layerPropertiesPanel, m_layersPanel, docking::DockPosition::Bottom);
        }
    } else if (!visible && !m_layerPropertiesPanel->isHidden()) {
        if (m_dockContainer) {
            m_savedLayerPropertiesPlacement
                = m_dockContainer->getPanelPlacement(m_layerPropertiesPanel);
        }
        m_dockManager->closePanel(m_layerPropertiesPanel);
    }
}

void WorkspaceTab::setLayerEffectsPanelVisible(bool visible)
{
    if (!m_layerEffectsPanel || !m_dockManager)
        return;
    if (visible && m_layerEffectsPanel->isHidden()) {
        if (m_savedLayerEffectsPlacement) {
            applyPanelPlacement(m_layerEffectsPanel, *m_savedLayerEffectsPlacement);
        } else {
            docking::DockPanel* ref
                = (m_layerPropertiesPanel && !m_layerPropertiesPanel->isHidden())
                ? static_cast<docking::DockPanel*>(m_layerPropertiesPanel)
                : static_cast<docking::DockPanel*>(m_layersPanel);
            m_dockManager->addPanelRelativeTo(
                m_layerEffectsPanel, ref, docking::DockPosition::Bottom);
        }
    } else if (!visible && !m_layerEffectsPanel->isHidden()) {
        if (m_dockContainer) {
            m_savedLayerEffectsPlacement = m_dockContainer->getPanelPlacement(m_layerEffectsPanel);
        }
        m_dockManager->closePanel(m_layerEffectsPanel);
    }
}

void WorkspaceTab::setColorPanelVisible(bool visible)
{
    if (!m_colorPanel || !m_dockManager)
        return;
    if (visible && m_colorPanel->isHidden()) {
        if (m_savedColorPlacement) {
            applyPanelPlacement(m_colorPanel, *m_savedColorPlacement);
        } else {
            docking::DockPanel* ref = (m_layerEffectsPanel && !m_layerEffectsPanel->isHidden())
                ? static_cast<docking::DockPanel*>(m_layerEffectsPanel)
                : ((m_layerPropertiesPanel && !m_layerPropertiesPanel->isHidden())
                          ? static_cast<docking::DockPanel*>(m_layerPropertiesPanel)
                          : static_cast<docking::DockPanel*>(m_layersPanel));
            m_dockManager->addPanelRelativeTo(m_colorPanel, ref, docking::DockPosition::Bottom);
        }
    } else if (!visible && !m_colorPanel->isHidden()) {
        if (m_dockContainer) {
            m_savedColorPlacement = m_dockContainer->getPanelPlacement(m_colorPanel);
        }
        m_dockManager->closePanel(m_colorPanel);
    }
}

void WorkspaceTab::setNavigatorPanelVisible(bool visible)
{
    if (!m_navigatorPanel || !m_dockManager)
        return;
    if (visible && m_navigatorPanel->isHidden()) {
        if (m_savedNavigatorPlacement) {
            applyPanelPlacement(m_navigatorPanel, *m_savedNavigatorPlacement);
        } else {
            docking::DockPanel* ref = (m_colorPanel && !m_colorPanel->isHidden())
                ? static_cast<docking::DockPanel*>(m_colorPanel)
                : ((m_layerEffectsPanel && !m_layerEffectsPanel->isHidden())
                          ? static_cast<docking::DockPanel*>(m_layerEffectsPanel)
                          : ((m_layerPropertiesPanel && !m_layerPropertiesPanel->isHidden())
                                    ? static_cast<docking::DockPanel*>(m_layerPropertiesPanel)
                                    : static_cast<docking::DockPanel*>(m_layersPanel)));
            m_dockManager->addPanelRelativeTo(m_navigatorPanel, ref, docking::DockPosition::Bottom);
        }
    } else if (!visible && !m_navigatorPanel->isHidden()) {
        if (m_dockContainer) {
            m_savedNavigatorPlacement = m_dockContainer->getPanelPlacement(m_navigatorPanel);
        }
        m_dockManager->closePanel(m_navigatorPanel);
    }
}

void WorkspaceTab::setCanvasWidgetVisible(ruwa::ui::CanvasWidget widget, bool visible)
{
    if (m_canvasPanel) {
        m_canvasPanel->setCanvasWidgetVisible(widget, visible);
        scheduleDockLayoutSave();
        emit panelsVisibilityChanged();
    }
}

bool WorkspaceTab::isToolsPanelVisible() const
{
    return m_toolsPanel && !m_toolsPanel->isHidden();
}

bool WorkspaceTab::isBrushesPanelVisible() const
{
    return m_brushesPanel && !m_brushesPanel->isHidden();
}

bool WorkspaceTab::isBrushSettingsPanelVisible() const
{
    return m_brushSettingsPanel && !m_brushSettingsPanel->isHidden();
}

bool WorkspaceTab::isLayersPanelVisible() const
{
    return m_layersPanel && !m_layersPanel->isHidden();
}

bool WorkspaceTab::isLayerPropertiesPanelVisible() const
{
    return m_layerPropertiesPanel && !m_layerPropertiesPanel->isHidden();
}

bool WorkspaceTab::isLayerEffectsPanelVisible() const
{
    return m_layerEffectsPanel && !m_layerEffectsPanel->isHidden();
}

bool WorkspaceTab::isColorPanelVisible() const
{
    return m_colorPanel && !m_colorPanel->isHidden();
}

bool WorkspaceTab::isNavigatorPanelVisible() const
{
    return m_navigatorPanel && !m_navigatorPanel->isHidden();
}

bool WorkspaceTab::isCanvasWidgetVisible(ruwa::ui::CanvasWidget widget) const
{
    return m_canvasPanel && m_canvasPanel->isCanvasWidgetVisible(widget);
}

ruwa::ui::CanvasWidgetVisibility WorkspaceTab::canvasWidgetVisibility() const
{
    return m_canvasPanel ? m_canvasPanel->canvasWidgetVisibility()
                         : ruwa::ui::CanvasWidgetVisibility {};
}

void WorkspaceTab::refreshToolbarState()
{
    if (m_toolbarSizeLabel) {
        const QSize size = hasFiniteDocumentBounds() ? m_settings.canvasSize : exportFrameSize();
        m_toolbarSizeLabel->setText(QString("%1 Ã— %2").arg(size.width()).arg(size.height()));
    }
    if (m_toolbarModeLabel) {
        m_toolbarModeLabel->setText(isInfiniteCanvas() ? tr("Infinite") : tr("Classic"));
    }
    if (m_toolbarModeToggleButton) {
        m_toolbarModeToggleButton->setText(
            isInfiniteCanvas() ? tr("Switch to classic") : tr("Switch to infinite"));
        m_toolbarModeToggleButton->syncSizeToText();
    }
}

// ==========================================================================
//   E X P O R T   M O D E
// ==========================================================================

void WorkspaceTab::toggleExportMode()
{
    if (m_canvasPanel) {
        m_canvasPanel->toggleExportMode();
    }
}

bool WorkspaceTab::isExportMode() const
{
    return m_canvasPanel && m_canvasPanel->isExportMode();
}

bool WorkspaceTab::fastExportPng()
{
    if (!m_canvasPanel) {
        return false;
    }

    // Seed the file name from the project file (if saved) or the tab title.
    QString baseName;
    if (hasFilePath()) {
        baseName = QFileInfo(m_filePath).completeBaseName();
    }
    if (baseName.isEmpty()) {
        baseName = baseTitle();
    }

    return m_canvasPanel->fastExportPng(baseName);
}

} // namespace ruwa::ui::tabs
