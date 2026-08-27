// SPDX-License-Identifier: MPL-2.0

// WorkspaceTab.h
#ifndef RUWA_UI_TABS_WORKSPACETAB_H
#define RUWA_UI_TABS_WORKSPACETAB_H

#include "shell/tab-system/BaseTab.h"
#include "features/canvas/CanvasBoundsMode.h"
#include "features/effects/LayerEffectTypes.h"
#include "features/project/ProjectData.h"
#include "features/theme/manager/ThemeContext.h"
#include "shared/types/CanvasWidgets.h"
#include "shell/docking/DockTypes.h"
#include "shell/docking/state/DockLayoutPreset.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QStringList>
#include <QTimer>
#include <QUuid>
#include <memory>
#include <functional>
#include <optional>
#include <vector>

class QWidget;
class QLabel;
class QPushButton;
class QResizeEvent;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
template <typename T> class QFutureWatcher;

namespace ruwa::ui::widgets {
class CapsuleButton;
class DotGridLoadingIndicator;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::workspace::detail {
struct ImportedLayerBatch;
}

namespace ruwa::ui::docking {
class DockManager;
class DockContainerWidget;
class DockStateSerializer;
struct DockLayoutPreset;
} // namespace ruwa::ui::docking

namespace ruwa::ui::workspace {
class LayersPanel;
class LayerPropertiesPanel;
class LayerEffectsPanel;
class ToolsPanel;
class BrushesPanel;
class BrushSettingsPanel;
class ColorPanel;
class CanvasPanel;
class NavigatorPanel;
class NavigatorWidget;
} // namespace ruwa::ui::workspace

namespace ruwa::core::layers {
struct LayerData;
struct SmartContent;
struct SmartDocument;
} // namespace ruwa::core::layers

namespace ruwa::ui::tabs {

/**
 * @brief Workspace tab for project editing
 *
 * Uses custom DockingSystem instead of QMainWindow's dock system.
 *
 * Layout:
 * - Left dock: Tools, Brushes, and Brush Settings panels
 * - Right dock: Layers, Layer Properties, Color panels
 * - Center: Canvas panel
 * - Top: Toolbar
 */
class WorkspaceTab : public ruwa::core::BaseTab {
    Q_OBJECT

public:
    struct WorkspaceColorState {
        QColor foreground = Qt::black;
        QColor background = Qt::white;
        bool editingForeground = true;
    };

    struct ProjectSaveSnapshot {
        QString projectName;
        QString tabTitle;
        QString tabIconAlias;
        QSize canvasSize;
        ruwa::core::canvas::CanvasBoundsMode canvasBoundsMode
            = ruwa::core::canvas::CanvasBoundsMode::Bounded;
        ruwa::core::serialization::ProjectData::ExportFrame exportFrame;
        aether::TilePixelFormat tileFormat = aether::kDefaultTileFormat;
        QList<std::shared_ptr<ruwa::core::layers::LayerData>> rootLayers;
        QUuid selectedLayerId;
        int currentTool = 0;
        ruwa::core::serialization::ProjectData::ToolState brushToolState;
        ruwa::core::serialization::ProjectData::ToolState eraserToolState;
        ruwa::core::serialization::ProjectData::ToolState blurToolState;
        ruwa::core::serialization::ProjectData::ToolState smudgeToolState;
        qreal lassoStabilization = 0.0;
        qreal lassoFillStabilization = 0.0;
        quint32 lastUsedColorRgba = 0xFF000000u;
        quint32 foregroundColorRgba = QColor(Qt::black).rgba();
        quint32 backgroundColorRgba = QColor(Qt::white).rgba();
        bool editingForegroundColor = true;
        // No workspace UI state here: the dock layout and the canvas overlays (positions
        // and visibility) are user preferences kept in QSettings, never in a project.
    };

    struct ProjectSettings {
        QString name;
        QSize canvasSize;
        ruwa::core::canvas::CanvasBoundsMode canvasBoundsMode
            = ruwa::core::canvas::CanvasBoundsMode::Bounded;
        ruwa::core::serialization::ProjectData::ExportFrame exportFrame;
        QString templateType;
        QColor backgroundColor { 0xD9, 0xD9, 0xD9 };
        /// Per-document tile pixel storage format chosen at New Project (8/16/32-bit).
        /// Slice 1: captured + serialized; grid storage still follows the global knob.
        aether::TilePixelFormat tileFormat = aether::kDefaultTileFormat;
    };

    explicit WorkspaceTab(const ProjectSettings& settings, QWidget* parent = nullptr);
    ~WorkspaceTab() override;

    // === BaseTab Interface ===

    ruwa::core::BaseTab::TabType type() const override { return TabType::Workspace; }

    bool wantsThemeLoadingScreen() const override { return true; }

    /// Tab label without the unsaved-changes marker (for filenames, serialization, dialogs).
    QString baseTitle() const { return m_tabTitle.isEmpty() ? m_projectName : m_tabTitle; }
    /// Removes trailing dirty marker(s) from context-menu rename input (same suffix as title()).
    static QString sanitizedRenameInput(const QString& input);

    QString title() const override;

    QIcon icon() const override;

    bool canClose() override;

    QList<QUuid> dependentTabIds() const override;

    QVariantMap serialize() const override;

    // === Project File ===

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString& path) { m_filePath = path; }
    bool hasFilePath() const { return !m_filePath.isEmpty(); }

    QString projectName() const { return m_projectName; }
    QString tabTitle() const { return m_tabTitle; }
    ruwa::core::canvas::CanvasBoundsMode canvasBoundsMode() const
    {
        return m_settings.canvasBoundsMode;
    }
    bool infiniteCanvasEnabled() const { return isInfiniteCanvas(); }
    bool isInfiniteCanvas() const
    {
        return ruwa::core::canvas::isInfiniteCanvas(m_settings.canvasBoundsMode);
    }
    bool hasFiniteDocumentBounds() const
    {
        return ruwa::core::canvas::hasFiniteDocumentBounds(m_settings.canvasBoundsMode);
    }
    QRect documentBoundsRect() const;
    QRect layerPreviewFrame() const;
    bool hasExportFrame() const { return m_settings.exportFrame.isValid(); }
    QRect exportFrame() const;
    QSize exportFrameSize() const;
    QRect effectiveDisplayFrame() const;
    void setCanvasBoundsMode(ruwa::core::canvas::CanvasBoundsMode mode);
    void setTabTitle(const QString& tabTitle);

    QString tabIconAlias() const { return m_tabIconAlias; }
    void setTabIconAlias(const QString& alias);

    /// Serialize current state to ProjectData
    ruwa::core::serialization::ProjectData toProjectData() const;

    /// Apply loaded ProjectData (call after initialize)
    bool fromProjectData(const ruwa::core::serialization::ProjectData& data);

    /// Load layer structure only (names, hierarchy) - light, can run before animation
    bool fromProjectDataStructure(const ruwa::core::serialization::ProjectData& data);

    /// Load tile pixel data - heavy, run after animation
    bool fromProjectDataTiles(const ruwa::core::serialization::ProjectData& data);

    /// Save to current file path (returns false if no path set)
    bool saveProject();

    /// Save to a specific path (updates filePath)
    bool saveProjectAs(const QString& filePath);

    /// Start saving in background to current file path.
    bool saveProjectAsync();

    /// Start saving in background to a specific path.
    bool saveProjectAsAsync(const QString& filePath);

    bool isSaveInProgress() const { return m_saveInProgress; }

    /// Create a WorkspaceTab from a project file (.rwf/.uwa) (returns nullptr on failure)
    static WorkspaceTab* loadFromFile(const QString& filePath, QWidget* parent = nullptr);

    /// Create a WorkspaceTab from already-loaded ProjectData (for async loading).
    /// Must be called on the main thread.
    static WorkspaceTab* createFromLoadedData(ruwa::core::serialization::ProjectData&& data,
        const QString& filePath, QWidget* parent = nullptr);
    static WorkspaceTab* createLoadingPlaceholder(
        const QString& filePath, QWidget* parent = nullptr);
    void acceptLoadedProjectData(
        ruwa::core::serialization::ProjectData&& data, const QString& filePath);

    /// Create a duplicate of the given WorkspaceTab (returns nullptr if source is null)
    static WorkspaceTab* duplicate(WorkspaceTab* source, QWidget* parent = nullptr);

    // === Smart object contents ===

    /**
     * @brief Open (or re-focus) the tab that edits the contents of smart layer @p layerId.
     *
     * Sessions are keyed by CONTENT: double-clicking any instance lands in the
     * same tab. Nothing happens for non-smart layers. A contents tab may open
     * another one (a smart object inside a smart object) up to the nesting depth
     * the compositor is willing to descend.
     */
    void openSmartContentEditor(const QUuid& layerId);

    /// True for a tab that edits a smart object's contents rather than a project file.
    bool isSmartContentEditor() const { return m_smartEditContent != nullptr; }
    QUuid smartEditContentId() const { return m_smartEditContentId; }
    /// The tab this contents tab was opened from — itself a contents tab when the
    /// object is nested. May already be closed.
    QUuid smartEditParentTabId() const { return m_smartEditParentTabId; }
    /// The real project tab at the top of the chain: the one whose file every
    /// commit eventually lands in, and whose tab-strip slot the contents share.
    /// Own id for a tab that is not a contents editor.
    QUuid smartEditDocumentTabId() const;
    /// How many contents tabs deep this one sits (0 for a project tab).
    int smartContentNestingDepth() const;

    /**
     * @brief Build a contents-editing tab for @p content.
     *
     * The tab is seeded with a COPY of the content's nested document — its whole
     * layer stack, not a flattened image. Edits stay inside the tab until they
     * are committed back, which is what makes the commit a single undoable step
     * in the parent document.
     */
    static WorkspaceTab* createSmartContentEditor(const QUuid& contentId,
        std::shared_ptr<ruwa::core::layers::SmartContent> content, const QUuid& parentTabId,
        const QString& contentName, QWidget* parent = nullptr);

    /**
     * @brief Write this contents tab's layer stack back into the smart object it
     *        belongs to (one undo step over there, every instance follows).
     *
     * No-op returning true for a tab with nothing to commit. False when the
     * commit could not be made — the parent tab is gone, no layer there shows
     * these contents any more, or the flattening could not run; the tab then
     * keeps its edits, still marked modified.
     */
    bool commitSmartContentEdits();

    /// True for a contents tab carrying edits that have not been committed back.
    bool hasUncommittedSmartContentEdits() const { return isSmartContentEditor() && isModified(); }

    void seedStartupImageImportPaths(const QStringList& filePaths);
    void seedStartupImageImport(const QImage& image, const QString& layerName);
    bool importImageFilesBelowSelectedKeepingSelection(const QStringList& filePaths);
    void promptImportImageFiles(const QStringList& filePaths);
    /// Ctrl+X / Ctrl+C / Ctrl+V routed by what is focused: the layers panel cuts
    /// layers (or the selected layer's mask), the canvas cuts the pixels inside
    /// the active selection. Paste reinstates whichever of those was copied last,
    /// falling back to an image on the system clipboard. Copy Merged always reads
    /// the visible canvas composite inside the active selection.
    bool handleCutRequest();
    bool handleCopyRequest();
    bool handleCopyMergedRequest();
    bool handlePasteRequest();

    // === Panel Access ===

    workspace::LayersPanel* layersPanel() const { return m_layersPanel; }
    workspace::LayerPropertiesPanel* layerPropertiesPanel() const { return m_layerPropertiesPanel; }
    workspace::LayerEffectsPanel* layerEffectsPanel() const { return m_layerEffectsPanel; }
    workspace::ToolsPanel* toolsPanel() const { return m_toolsPanel; }
    workspace::BrushesPanel* brushesPanel() const { return m_brushesPanel; }
    workspace::BrushSettingsPanel* brushSettingsPanel() const { return m_brushSettingsPanel; }
    workspace::ColorPanel* colorPanel() const { return m_colorPanel; }
    workspace::CanvasPanel* canvasPanel() const { return m_canvasPanel; }
    workspace::NavigatorPanel* navigatorPanel() const { return m_navigatorPanel; }
    void setWorkspaceColorState(const WorkspaceColorState& state);

    // === Dock Management ===

    docking::DockManager* dockManager() const { return m_dockManager; }
    docking::DockContainerWidget* dockContainer() const { return m_dockContainer; }

    /// Save dock layout state
    QByteArray saveDockState() const;

    /// Restore dock layout state
    bool restoreDockState(const QByteArray& state);

    /// Reset docks to default layout
    void resetDockLayout();

    /// Apply a layout preset
    void applyLayoutPreset(const docking::DockLayoutPreset& preset);

    /// Capture the current workspace layout as a serializable preset snapshot.
    bool createCurrentLayoutPreset(
        docking::DockLayoutPreset* outPreset, QString* errorMessage = nullptr) const;

    /// Save current dock layout as a new user preset (active workspace only).
    /// @return false if not a workspace with a capturable layout; @p errorMessage set when
    /// non-null.
    bool captureCurrentLayoutAsPreset(QString* errorMessage = nullptr);

    /// Set panel visibility (called from View → Panels menu)
    void setToolsPanelVisible(bool visible);
    void setBrushesPanelVisible(bool visible);
    void setBrushSettingsPanelVisible(bool visible);
    void setLayersPanelVisible(bool visible);
    void setLayerPropertiesPanelVisible(bool visible);
    void setLayerEffectsPanelVisible(bool visible);
    void setColorPanelVisible(bool visible);
    void setNavigatorPanelVisible(bool visible);

    /// The input the shared colour picker is currently serving, or null when it
    /// closed. Only the text colour matters here: while its picker is up, the
    /// canvas drops the inverted selection highlight so the colour being
    /// previewed is the colour the user sees.
    void setColorPickerTarget(QWidget* sourceButton);

    /// Set canvas widgets visibility (called from View → Canvas widgets menu)
    void setCanvasWidgetVisible(ruwa::ui::CanvasWidget widget, bool visible);

    /// Current panel visibility (for syncing TopBar menu state)
    bool isToolsPanelVisible() const;
    bool isBrushesPanelVisible() const;
    bool isBrushSettingsPanelVisible() const;
    bool isLayersPanelVisible() const;
    bool isLayerPropertiesPanelVisible() const;
    bool isLayerEffectsPanelVisible() const;
    bool isColorPanelVisible() const;
    bool isNavigatorPanelVisible() const;

    /// Current canvas widgets visibility (for syncing TopBar menu state)
    bool isCanvasWidgetVisible(ruwa::ui::CanvasWidget widget) const;
    ruwa::ui::CanvasWidgetVisibility canvasWidgetVisibility() const;

    /// Toggle export mode (enter if not active, exit if active)
    void toggleExportMode();
    bool isExportMode() const;

    /// Fast export the canvas straight to a PNG file (single save dialog).
    /// Returns true if a file was written.
    bool fastExportPng();
    /// File name (no extension) both export paths seed their name field from:
    /// the saved project file name, falling back to the tab title.
    QString exportBaseName() const;

protected:
    void onInitialize() override;
    void onActivate() override;
    void onDeactivate() override;
    void onApplyThemeRefresh(std::function<void()> finished, bool showLoading) override;
    void onTransitionFinishedImpl() override;
    void resizeEvent(QResizeEvent* event) override;

signals:
    void colorPickerRequested(const QColor& initialColor, QWidget* sourceButton);
    /// Emitted when panel visibility changes (e.g. after layout restore) - sync TopBar
    void panelsVisibilityChanged();
    void startupCompleted();
    void saveFinished(bool success);

public:
    struct NavigatorTrackedLayerState {
        bool visible = true;
        qreal opacity = 1.0;
        int blendMode = 0;
        int groupCompositingMode = 0;
        bool clippedToBelow = false;
        bool backgroundTransparent = false;
        QColor backgroundColor = Qt::white;
        bool isBackground = false;
        bool isGroup = false;
        bool isPixelLayer = false;
        bool hasRetainedVisualContent = false;
        quint64 effectChainRevision = 0;
        QList<ruwa::core::effects::LayerEffectState> effects;
    };

private:
    struct PendingTileLayerLoad {
        QUuid layerId;
        const ruwa::core::serialization::LayerEntry* entry = nullptr;
    };

    struct WorkspaceStateSnapshot {
        QByteArray dockLayoutState;
        QPointF brushOverlayPosNormalized = QPointF(-1.0, -1.0);
        QPointF toolStateOverlayPosNormalized = QPointF(-1.0, -1.0);
        QPointF stylusJoystickPosNormalized = QPointF(-1.0, -1.0);
        QRgb foregroundColorRgba = QColor(Qt::black).rgba();
        QRgb backgroundColorRgba = QColor(Qt::white).rgba();
        bool editingForegroundColor = true;
        bool hasBrushOverlayPos = false;
        bool hasToolStateOverlayPos = false;
        bool hasStylusJoystickPos = false;
        bool stylusJoystickAbovePanel = true;
        ruwa::ui::CanvasWidgetVisibility canvasWidgets;
    };

private:
    workspace::NavigatorWidget* navigatorWidget() const;
    void invalidateNavigatorTiles(const QList<QPoint>& tilePositions);
    void invalidateNavigatorOverview();
    void rebuildNavigatorTrackedLayerStates();
    void onNavigatorTrackedLayerChanged(const QUuid& id);
    void onNavigatorTrackedLayerAboutToRemove(const QUuid& id);
    void onNavigatorOpacityEditStarted(const QUuid& id);
    void onNavigatorOpacityEditFinished(const QUuid& id, bool changed);
    void setupLoadingShell();
    void showLoadingShell(const QString& statusText);
    void hideLoadingShell(std::function<void()> onFinished = {});
    void hideLoadingShellImmediately();
    void clearLoadingShellFadeSnapshot();
    void requestInitialLoadingShellHide();
    void buildWorkspaceUi();
    void initializeEmptyProject();
    void initializeSmartContentDocument();
    /// Snapshot this tab's layer stack as a nested document (deep copy, ids
    /// preserved), sized and formatted like this tab's canvas.
    std::shared_ptr<ruwa::core::layers::SmartDocument> buildSmartDocumentFromModel() const;
    void scheduleSmartContentSessionReconcile();
    void reconcileSmartContentSessions();
    void queuePostTransitionInitialization();
    void startInitialWorkspaceWarmup();
    void updateInitialPresentationReadiness();
    void startInitialCanvasAppearanceAnimation();
    void startDeferredTileRestore();
    void enqueuePendingTileLoads(const QList<ruwa::core::serialization::LayerEntry>& entries);
    void scheduleNextTileRestoreBatch();
    void processPendingTileRestoreBatch();
    void tryFinishAsyncStartup();
    void flushPendingStartupImageImport();
    void scheduleRecentProjectsThumbnailCapture();
    void tryCaptureRecentProjectThumbnail(const QString& path);
    bool isLayerClipboardTargetActive() const;
    /// Snapshot + delete the selected layers, arming the layer paste path.
    bool cutSelectedLayersToClipboard();
    void setupUI();
    void setupDockSystem();
    void setupPanels();
    void setupToolbar();
    void setupDefaultLayout();
    void restoreUserDockLayout();
    void prepareInitialDockEntranceAnimation();
    void startInitialDockEntranceAnimation();
    void startInitialCanvasBorderAppearanceAnimation();
    void restoreInitialCanvasBorderImmediately();
    void scheduleDockLayoutSave();
    void saveDockLayoutNow();
    void connectPanelSignals();
    void applyPanelPlacement(docking::DockPanel* panel, const docking::PanelPlacement& placement);
    void applyPresetCanvasWidgetState(const docking::DockLayoutPreset& preset);
    void updateThemeColors();
    void setWorkspaceColorSlot(bool isForeground);
    void setWorkspaceColorForSlot(bool isForeground, const QColor& color);
    void syncColorPanelFromWorkspaceState();
    void syncCanvasColorFromWorkspaceState();
    QImage captureProjectThumbnail(int maxSize) const;
    ProjectSaveSnapshot captureProjectSaveSnapshot() const;
    WorkspaceStateSnapshot captureWorkspaceStateSnapshot() const;
    static void writeWorkspaceStateSnapshot(const WorkspaceStateSnapshot& snapshot);
    void markProjectModified();
    void flushWorkspaceStateSync();
    void discardPendingWorkspaceStateSync();
    bool waitForSaveToFinish();
    bool performSave(const QString& filePath, bool allowUserInputWhileWaiting);
    bool startAsyncSave(const QString& filePath);
    void finalizeSuccessfulSave(const QString& filePath, quint64 savedRevision);
    void finalizeFailedSave(const QString& filePath, const QString& errorMessage);
    void refreshToolbarState();

private:
    explicit WorkspaceTab(
        const ProjectSettings& settings, const QUuid& id, QWidget* parent = nullptr);
    static WorkspaceTab* createFromLoadedData(ruwa::core::serialization::ProjectData&& data,
        const QString& filePath, const QUuid& tabId, bool updateRecentProjects,
        QWidget* parent = nullptr);

    struct PendingStartupImageImport {
        QImage image;
        QString layerName;
    };

    ruwa::ui::core::ThemeContext m_theme;
    ProjectSettings m_settings;
    QString m_projectName;
    QString m_tabTitle;
    QString m_filePath;
    QString m_tabIconAlias;
    /// Non-null only in a contents-editing tab; also its "am I one" flag. Held by
    /// shared_ptr so the content outlives deletion/undo of the layer it came from.
    std::shared_ptr<ruwa::core::layers::SmartContent> m_smartEditContent;
    QUuid m_smartEditContentId;
    QUuid m_smartEditParentTabId;
    bool m_smartSessionReconcileQueued = false;
    bool m_transitionFinished = false;
    bool m_workspaceUiBuilt = false;
    bool m_postTransitionInitializationStarted = false;
    bool m_postTransitionInitializationQueued = false;
    bool m_tileRestoreScheduled = false;
    bool m_tileRestoreInProgress = false;
    bool m_tileRestoreLoadedAnyContent = false;
    bool m_canvasGlReady = false;
    bool m_asyncStartupCompleted = false;
    bool m_pendingCanvasAppearanceAnimation = false;
    bool m_initialThumbnailCaptureScheduled = false;
    bool m_waitingForAsyncProjectData = false;

    // Deferred load: stored until onInitialize() completes
    std::unique_ptr<ruwa::core::serialization::ProjectData> m_pendingProjectData;
    /// Image paths from Open / drop before canvas + GL are ready (see
    /// importImageFilesBelowSelectedKeepingSelection).
    QStringList m_pendingStartupImageImportPaths;
    QList<PendingStartupImageImport> m_pendingStartupImageImports;
    /// Edit → Import before async startup completes; flushed in tryFinishAsyncStartup().
    QStringList m_pendingPromptImportPaths;
    std::vector<PendingTileLayerLoad> m_pendingTileLoads;
    int m_pendingTileLoadIndex = 0;
    int m_pendingTileIndex = 0;
    QHash<QUuid, NavigatorTrackedLayerState> m_navigatorTrackedLayerStates;
    QSet<QPoint> m_pendingNavigatorRemovedTiles;
    QSet<QUuid> m_pendingNavigatorOpacityCommitIds;
    bool m_pendingNavigatorFullRefresh = false;

    // Docking system
    docking::DockContainerWidget* m_dockContainer = nullptr;
    docking::DockManager* m_dockManager = nullptr;
    docking::DockStateSerializer* m_serializer = nullptr;

    // Panels
    workspace::CanvasPanel* m_canvasPanel = nullptr;
    workspace::LayersPanel* m_layersPanel = nullptr;
    workspace::LayerPropertiesPanel* m_layerPropertiesPanel = nullptr;
    workspace::LayerEffectsPanel* m_layerEffectsPanel = nullptr;
    workspace::ToolsPanel* m_toolsPanel = nullptr;
    workspace::BrushesPanel* m_brushesPanel = nullptr;
    workspace::BrushSettingsPanel* m_brushSettingsPanel = nullptr;
    workspace::ColorPanel* m_colorPanel = nullptr;
    workspace::NavigatorPanel* m_navigatorPanel = nullptr;

    // Toolbar
    QWidget* m_toolbar = nullptr;
    QLabel* m_toolbarSizeLabel = nullptr;
    QLabel* m_toolbarModeLabel = nullptr;
    ruwa::ui::widgets::CapsuleButton* m_toolbarModeToggleButton = nullptr;
    QWidget* m_loadingShell = nullptr;
    QGraphicsOpacityEffect* m_loadingShellOpacity = nullptr;
    QPropertyAnimation* m_loadingShellFadeAnimation = nullptr;
    QPropertyAnimation* m_canvasBorderAppearanceAnimation = nullptr;
    QLabel* m_loadingShellFadeSnapshot = nullptr;
    QGraphicsOpacityEffect* m_loadingShellFadeSnapshotOpacity = nullptr;
    QLabel* m_loadingTitleLabel = nullptr;
    QLabel* m_loadingStatusLabel = nullptr;
    ruwa::ui::widgets::DotGridLoadingIndicator* m_loadingIndicator = nullptr;

    // Saved panel placements (for restore after hide)
    std::optional<docking::PanelPlacement> m_savedToolsPlacement;
    std::optional<docking::PanelPlacement> m_savedBrushesPlacement;
    std::optional<docking::PanelPlacement> m_savedBrushSettingsPlacement;
    std::optional<docking::PanelPlacement> m_savedLayersPlacement;
    std::optional<docking::PanelPlacement> m_savedLayerPropertiesPlacement;
    std::optional<docking::PanelPlacement> m_savedLayerEffectsPlacement;
    std::optional<docking::PanelPlacement> m_savedColorPlacement;
    std::optional<docking::PanelPlacement> m_savedNavigatorPlacement;

    // Per-workspace autosave timer
    QTimer* m_autoSaveTimer = nullptr;
    QTimer* m_dockLayoutSaveTimer = nullptr;
    QTimer* m_navigatorThumbnailRefreshTimer
        = nullptr; ///< Deferred refresh when user stops interacting
    QFutureWatcher<void>* m_workspaceStateFlushWatcher = nullptr;
    QFutureWatcher<ruwa::ui::workspace::detail::ImportedLayerBatch>* m_startupImageImportWatcher
        = nullptr;
    bool m_restoringDockLayout = false;
    bool m_initialDockLayoutRestored = false;
    bool m_initialWorkspaceWarmupStarted = false;
    bool m_initialPresentationCheckQueued = false;
    bool m_initialPresentationReady = false;
    bool m_initialDockEntrancePreparationQueued = false;
    bool m_initialDockEntrancePreparationFinished = false;
    bool m_initialDockEntranceStartRequested = false;
    bool m_initialDockEntranceHandled = false;
    bool m_initialLoadingShellHidePending = false;
    bool m_initialCanvasAppearanceStarted = false;
    bool m_initialCanvasBorderAppearanceArmed = false;
    bool m_layerCopyArmed = false;
    bool m_layerCutArmed = false;
    QList<std::shared_ptr<ruwa::core::layers::LayerData>> m_layerCutClipboard;
    bool m_suppressThemeRefreshLoadingShell = false;
    bool m_saveInProgress = false;
    bool m_requiresFormatMigrationSave = false;
    quint64 m_projectChangeRevision = 0;
    bool m_suppressModifiedChanges
        = false; ///< True during initial setup (new/load) - don't mark modified
    bool m_workspaceStateDirty = false;
    bool m_workspaceStateSyncPending = false;
    std::function<void()> m_loadingShellHideContinuation;
    WorkspaceStateSnapshot m_pendingWorkspaceStateSnapshot;
    WorkspaceColorState m_workspaceColorState;
    bool m_workspaceColorStateInitialized = false;
    bool m_workspaceColorStateSeededFromCanvasDefaults = false;
    bool m_syncingWorkspaceColorState = false;
    bool m_restoringWorkspaceUiState = false;
    void startAutoSaveTimer();
    void stopAutoSaveTimer();
};

} // namespace ruwa::ui::tabs

#endif // RUWA_UI_TABS_WORKSPACETAB_H
