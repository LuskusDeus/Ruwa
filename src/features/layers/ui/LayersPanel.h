// SPDX-License-Identifier: MPL-2.0

// LayersPanel.h
#ifndef RUWA_UI_WORKSPACE_PANELS_LAYERSPANEL_H
#define RUWA_UI_WORKSPACE_PANELS_LAYERSPANEL_H

#include "shell/docking/widgets/DockPanel.h"
#include "features/layers/model/LayerData.h"
#include "features/layers/model/LayerModel.h"
#include "features/layers/ui/LayerListView.h"

#include <QList>
#include <QMap>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QTimer>
#include <functional>
#include <memory>

class QPushButton;
class QEvent;
class QObject;
class QJsonArray;
class QVariantAnimation;

namespace aether {
class IUndoCommand;
}

namespace ruwa::ui::widgets {
class BaseStyledWidget;
class BaseAnimatedButton;
class OpacitySliderWidget;
class AnimatedComboBox;
class AnimatedFlowWidget;
class DragGhostWidget;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::workspace {

/**
 * @brief Panel for layer management.
 *
 * Contains:
 *   - LayerListView (animated list with drag & drop)
 *   - Toolbar: add layer, add group, delete
 *
 * All operations go through LayerModel.
 * Drag & drop result uses X-based depth to resolve (parent, childIndex).
 */
class LayersPanel : public ruwa::ui::docking::DockPanel {
    Q_OBJECT

public:
    /// Reorderable slots of the toolbar's left-hand (action) block. The alpha-lock
    /// and layer-lock toggles are pinned to the right and never take part.
    enum class ToolbarItem {
        AddLayer,
        AddAdjustment,
        AddGroup,
        AddMask,
        Duplicate,
        Merge,
        Separator,
        Delete,
    };

    explicit LayersPanel(QWidget* parent = nullptr);
    ~LayersPanel() override;

    /// Toolbar buttons the panel context menu can show/hide. The divider is not
    /// listed: it is chrome, and the flow already drops it at row edges.
    static QList<ToolbarItem> configurableToolbarItems();
    static QString toolbarItemDisplayName(ToolbarItem item);
    static ruwa::ui::core::IconProvider::StandardIcon toolbarItemIconType(ToolbarItem item);
    bool isToolbarItemVisible(ToolbarItem item) const;
    void setToolbarItemVisible(ToolbarItem item, bool visible);

    // === Model ===
    ruwa::core::layers::LayerModel* layerModel() { return &m_layerModel; }
    const ruwa::core::layers::LayerModel* layerModel() const { return &m_layerModel; }

    void refreshLayers();
    void setCanvasSize(const QSize& size);
    void setDisplayFrame(const QRect& frame);
    void setInsertAnimationsEnabled(bool enabled);

    using PushUndoFn = std::function<void(std::unique_ptr<aether::IUndoCommand>)>;
    void setPushUndoFn(PushUndoFn fn);

    using RequestRenderFn = std::function<void()>;
    using OnContentChangedFn = std::function<void()>;
    void setUndoCallbacks(RequestRenderFn requestRender, OnContentChangedFn onContentChanged);

    /// Hook that bakes the active canvas selection into a layer's just-created
    /// mask (inside = visible, outside = hidden). Returns true if it filled.
    using FillMaskFromSelectionFn = std::function<bool(const ruwa::core::layers::LayerId&)>;
    void setFillMaskFromSelectionFn(FillMaskFromSelectionFn fn);

    /// Canvas-side operations the panel needs to prepare layers for an edit it
    /// owns (merge rasterizes smart layers and bakes masks). Each of them pushes
    /// its own undo command, which is why the transaction hooks come along: the
    /// whole preparation plus the edit must collapse into one undo step.
    struct CanvasLayerOps {
        std::function<bool(const ruwa::core::layers::LayerId&)> rasterizeLayer;
        std::function<bool(const ruwa::core::layers::LayerId&)> applyLayerMask;
        std::function<void(const QString&)> beginUndoTransaction;
        std::function<void()> endUndoTransaction;
    };
    void setCanvasLayerOps(CanvasLayerOps ops);
    void scheduleThumbnailRefresh();
    void invalidateLayerThumbnails(const QList<ruwa::core::layers::LayerId>& ids);
    void setThumbnailLoadingMode(bool active);
    bool visibleThumbnailsReady() const;
    void preparePresentationSnapshot();
    void setFillProcessingLayer(const ruwa::core::layers::LayerId& id);
    ruwa::core::layers::LayerData* selectedLayer() const;
    void selectLayer(const ruwa::core::layers::LayerId& id);
    bool copySelectedLayerSnapshots(
        QList<std::shared_ptr<ruwa::core::layers::LayerData>>* snapshots) const;
    bool pasteLayerSnapshots(
        const QList<std::shared_ptr<ruwa::core::layers::LayerData>>& snapshots);
    bool deleteSelectedLayers();
    /// Drop the selected layer's mask without baking it into the pixels (undoable).
    bool deleteSelectedLayerMask();
    /// Whether the selected layer's mask (not its pixels) is the active paint target.
    bool selectedLayerMaskIsPaintTarget() const;
    /// Put the selected layer's mask on the edit clipboard. Returns false when the
    /// layer carries no mask.
    bool copySelectedLayerMask();
    /// Copy the selected layer's mask, then drop it from the layer (undoable).
    bool cutSelectedLayerMask();
    /// Whether the clipboard holds a mask the selected layer could take.
    bool canPasteMaskToSelectedLayer() const;
    /// Give the selected layer the mask sitting on the clipboard, replacing any
    /// mask it already has (undoable).
    bool pasteMaskToSelectedLayer();
    /// Duplicate the selection. A duplicated smart object shares its source's
    /// content (it becomes an instance) unless @p detachSmartContent is set,
    /// which is the "New Smart Object via Copy" semantic.
    bool duplicateSelectedLayers(bool detachSmartContent = false);
    bool mergeSelectedLayerDown();
    bool mergeVisibleLayers();
    bool mergeSelectedLayers();
    /// Contextual merge (Ctrl+E / toolbar): merges the selection or merges down,
    /// showing a custom warning when the merge is blocked by a Background or
    /// Board layer. Returns true only if a merge actually happened.
    bool performMerge();
    /// Whether a merge action should be offered (it will either merge or warn).
    bool hasMergeIntent() const;
    bool applyQuickClippingMask();
    bool toggleSelectedLayerVisibility();
    bool canMergeSelectedLayerDown() const;
    bool canMergeVisibleLayers() const;
    bool canMergeSelectedLayers() const;
    void addLayer();
    void addGroup();
    void addAdjustmentLayer();

signals:
    void visibleThumbnailStateChanged();
    /// Emitted before an operation invalidates the current canvas edit target.
    /// Pending operations such as transform must be committed synchronously.
    void aboutToPerformTransformIncompatibleEdit();
    void layerSelected(const ruwa::core::layers::LayerId& id);
    void layerContentSelectionRequested(const ruwa::core::layers::LayerId& id);
    void layerMaskSelectionRequested(const ruwa::core::layers::LayerId& id);
    void layerTextEditRequested(const ruwa::core::layers::LayerId& id);
    /// Double click on a smart object's thumbnail: open its contents in their own tab.
    void layerSmartContentEditRequested(const ruwa::core::layers::LayerId& id);
    void layerVisibilityChanged(const ruwa::core::layers::LayerId& id, bool visible);
    void layerLockChanged(const ruwa::core::layers::LayerId& id, bool locked);
    void layerAlphaLockChanged(const ruwa::core::layers::LayerId& id, bool alphaLock);
    void layerOpacityChanged(const ruwa::core::layers::LayerId& id, qreal opacity);
    void layerBlendModeChanged(
        const ruwa::core::layers::LayerId& id, ruwa::core::layers::BlendMode mode);
    void layerOpacityEditStarted(const ruwa::core::layers::LayerId& id);
    void layerOpacityEditFinished(const ruwa::core::layers::LayerId& id, bool changed);

    void addLayerRequested();
    void deleteLayerRequested();
    void addGroupRequested();

    /// Emitted when persistent panel state (currently the toolbar order) changes,
    /// so the workspace can schedule a dock-layout save.
    void panelStateChanged();

    void layerNameChanged(const ruwa::core::layers::LayerId& id, const QString& name);
    void layerOrderChanged(const ruwa::core::layers::LayerId& movedId,
        const ruwa::core::layers::LayerId& newParentId, int newIndex);

    /// Raster pixel clear (handled by workspace → canvas GL).
    void layerClearPixelContentRequested(const ruwa::core::layers::LayerId& id);
    /// Smart → raster bake (handled by workspace → canvas GL).
    void layerRasterizeSmartRequested(const ruwa::core::layers::LayerId& id);
    /// Raster/text → smart object wrap (handled by workspace → canvas GL).
    void layerConvertToSmartObjectRequested(const ruwa::core::layers::LayerId& id);
    /// Give a smart object new contents from a file (handled by workspace → canvas GL).
    void layerReplaceSmartContentsRequested(const ruwa::core::layers::LayerId& id);
    /// Bake a layer's mask into its pixels and remove it (handled by workspace → canvas GL).
    void layerApplyMaskRequested(const ruwa::core::layers::LayerId& id);
    /// Invert a layer mask (all tiles + background) (workspace → canvas GL).
    void layerInvertMaskRequested(const ruwa::core::layers::LayerId& id);
    /// Bake a raster layer's effect chain into its pixels and clear the chain
    /// (handled by workspace → canvas GL).
    void layerApplyEffectsRequested(const ruwa::core::layers::LayerId& id);
    /// Emitted when the active paint target switches between layer pixels and a
    /// layer mask. `active` is true while the selected layer's mask is the paint
    /// target. The color panel uses this to fade to a grayscale display.
    void maskEditTargetChanged(bool active);

protected:
    QWidget* createContent() override;
    void retranslateUi() override;
    void onThemeChanged() override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    QJsonObject savePanelState() const override;
    void restorePanelState(const QJsonObject& state) override;

private slots:
    void onLayerSelected(const ruwa::core::layers::LayerId& id, Qt::KeyboardModifiers modifiers);
    void onLayerPaintTargetSelected(
        const ruwa::core::layers::LayerId& id, bool maskTarget, Qt::KeyboardModifiers modifiers);
    void onLayerContentSelectionRequested(const ruwa::core::layers::LayerId& id);
    void onLayerMaskSelectionRequested(const ruwa::core::layers::LayerId& id);
    void onLayerTextEditRequested(const ruwa::core::layers::LayerId& id);
    void onLayerSmartContentEditRequested(const ruwa::core::layers::LayerId& id);
    void onLayerExpandToggled(const ruwa::core::layers::LayerId& id);
    void onLayerVisibilityToggled(const ruwa::core::layers::LayerId& id);
    void onLayerDragDropped(
        const ruwa::core::layers::LayerId& id, int dropInsertIndex, int targetDepth);
    void onLayerDragCopyDropped(
        const ruwa::core::layers::LayerId& id, int dropInsertIndex, int targetDepth);
    void onLayerRenamed(const ruwa::core::layers::LayerId& id, const QString& newName);
    void onClipSelectionRequested(const ruwa::core::layers::LayerId& baseLayerId);
    void onClipSwipeRequested(const ruwa::core::layers::LayerId& baseLayerId, bool leftToRight);
    void onLayerAlphaLockClicked(const ruwa::core::layers::LayerId& id);
    void onLayerLockClicked(const ruwa::core::layers::LayerId& id);
    void onLayerDuplicateRequested(const ruwa::core::layers::LayerId& id);
    void onLayerNewSmartObjectViaCopyRequested(const ruwa::core::layers::LayerId& id);
    void onLayerDeleteRequested(const ruwa::core::layers::LayerId& id);
    void onLayerQuickClippingMaskRequested(const ruwa::core::layers::LayerId& id);
    void onLayerToggleAlphaLockRequested(const ruwa::core::layers::LayerId& id);
    void onLayerToggleLockRequested(const ruwa::core::layers::LayerId& id);
    void onLayerClearPixelsRequested(const ruwa::core::layers::LayerId& id);
    void onLayerRasterizeSmartRequested(const ruwa::core::layers::LayerId& id);
    void onLayerConvertToSmartObjectRequested(const ruwa::core::layers::LayerId& id);
    void onLayerReplaceSmartContentsRequested(const ruwa::core::layers::LayerId& id);
    void onLayerApplyMaskRequested(const ruwa::core::layers::LayerId& id);
    void onLayerInvertMaskRequested(const ruwa::core::layers::LayerId& id);
    void onLayerApplyEffectsRequested(const ruwa::core::layers::LayerId& id);
    /// Context menu raised on a multi-selection: the action applies to every
    /// selected layer, not to the row that was clicked.
    void onLayerMultiActionRequested(ruwa::ui::widgets::MultiLayerAction action);

    void onAddLayer();
    void onAddGroup();
    void onAddAdjustmentLayer();
    void onAddMask();
    void onDeleteLayer();
    void onAlphaLockToggled();
    void onLockToggled();
    void onBlendModeChanged(int index);
    void onBlendModeHovered(int index);
    void onBlendModePopupShown();
    void onBlendModePopupHidden();
    void onOpacityChanged(qreal opacity);
    void onOpacityDragStarted(qreal opacity);
    void onOpacityCommitted(qreal opacity);
    void onModelLayerDataChanged(const ruwa::core::layers::LayerId& id);

private:
    bool mergeLayerSet(
        const QList<ruwa::core::layers::LayerData*>& orderedTopToBottom, const QString& undoLabel);
    /// Turn every layer in the set into plain, mask-free pixels so the flat
    /// compositing that merge performs sees what the screen shows: smart layers
    /// are rasterized (which bakes their transform) and masks are baked in.
    /// Runs inside the caller's undo transaction. False = merge must be aborted.
    bool prepareLayersForMerge(const QList<ruwa::core::layers::LayerId>& ids);
    static QString hiddenMaskMergeWarning();
    void showMergeWarning(const QString& message);

    /// Lock / alpha-lock every eligible selected layer as one undo step. The
    /// caller decides the target state; the context menu derives it the same way
    /// the labels do (all locked → unlock, otherwise lock).
    bool setSelectionLocked(bool locked);
    bool setSelectionAlphaLocked(bool alphaLock);
    /// Wrap the whole selection into a single smart object, Photoshop-style: the
    /// selected layers become that object's contents and leave the stack. A
    /// selection of one falls back to the plain single-layer conversion.
    bool convertSelectionToSmartObject();
    /// Run a per-layer canvas operation (rasterize, convert, clear, mask, effects)
    /// over the whole selection, collapsed into a single undo step.
    void performSelectionCanvasOp(ruwa::ui::widgets::MultiLayerAction action);

    void setupToolbar(QWidget* container);
    void applyToolbarTheme();
    void populateBlendModeCombo();
    void syncLayerControls();

    // === Toolbar reordering (drag & drop) ===
    static QList<ToolbarItem> defaultToolbarOrder();
    static QString toolbarItemKey(ToolbarItem item);
    static QList<ToolbarItem> normalizedToolbarOrder(const QJsonArray& values);
    static QList<ToolbarItem> normalizedHiddenToolbarItems(const QJsonArray& values);
    QWidget* toolbarItemWidget(ToolbarItem item) const;
    void applyToolbarOrder(bool animate);
    void animateToolbarItemVisibility(ToolbarItem item, bool visible);
    void cancelToolbarItemVisibilityAnimation(ToolbarItem item);
    void startToolbarDrag(ToolbarItem item, QWidget* itemWidget, const QPoint& globalPos);
    void updateToolbarDrag(const QPoint& globalPos);
    void finishToolbarDrag(bool accepted, const QPoint& globalPos);
    QPoint toolbarGhostTargetPosition(const QPoint& globalPos) const;
    int toolbarInsertIndexAt(const QPoint& contentPos) const;
    void moveDraggedToolbarItemTo(int insertIndex);
    void cancelToolbarDragCandidate();

private:
    ruwa::core::layers::LayerModel m_layerModel;
    ruwa::ui::widgets::LayerListView* m_listView = nullptr;
    QWidget* m_contentWidget = nullptr;

    // Toolbar buttons
    ruwa::ui::widgets::BaseAnimatedButton* m_btnAdd = nullptr;
    ruwa::ui::widgets::BaseAnimatedButton* m_btnAdjustment = nullptr;
    ruwa::ui::widgets::BaseAnimatedButton* m_btnDuplicate = nullptr;
    ruwa::ui::widgets::BaseAnimatedButton* m_btnMergeDown = nullptr;
    ruwa::ui::widgets::BaseAnimatedButton* m_btnGroup = nullptr;
    ruwa::ui::widgets::BaseAnimatedButton* m_btnMask = nullptr;
    ruwa::ui::widgets::BaseAnimatedButton* m_btnAlphaLock = nullptr;
    ruwa::ui::widgets::BaseAnimatedButton* m_btnLock = nullptr;
    ruwa::ui::widgets::BaseAnimatedButton* m_btnDelete = nullptr;

    ruwa::ui::widgets::AnimatedComboBox* m_blendModeCombo = nullptr;
    ruwa::ui::widgets::OpacitySliderWidget* m_opacitySlider = nullptr;

    // Adaptive toolbar flow + its reorder-drag lifecycle.
    ruwa::ui::widgets::AnimatedFlowWidget* m_toolbarFlow = nullptr;
    QWidget* m_toolbarSeparator = nullptr;
    QList<ToolbarItem> m_toolbarOrder;
    QList<ToolbarItem> m_appliedToolbarOrder;
    QList<ToolbarItem> m_hiddenToolbarItems;
    QList<ToolbarItem> m_toolbarDragStartOrder;
    QMap<ToolbarItem, QPointer<QVariantAnimation>> m_toolbarVisibilityAnimations;
    QPointer<ruwa::ui::widgets::DragGhostWidget> m_toolbarDragGhost;
    QWidget* m_toolbarDragCandidate = nullptr;
    QWidget* m_toolbarDraggedWidget = nullptr;
    QPoint m_toolbarDragPressPosition;
    QPoint m_toolbarDragOffset;
    ToolbarItem m_toolbarDraggedItem = ToolbarItem::AddLayer;
    bool m_toolbarDragActive = false;
    bool m_toolbarDragSettling = false;
    bool m_toolbarDragCursorOverride = false;

    QRect m_displayFrame;
    QTimer m_thumbnailRefreshTimer;
    PushUndoFn m_pushUndoFn;
    RequestRenderFn m_requestRenderFn;
    OnContentChangedFn m_onContentChangedFn;
    FillMaskFromSelectionFn m_fillMaskFromSelectionFn;
    CanvasLayerOps m_canvasLayerOps;
    bool m_syncingLayerControls = false;
    bool m_lastMaskEditTarget = false;
    ruwa::core::layers::BlendMode m_blendModeBeforePreview = ruwa::core::layers::BlendMode::Normal;
    bool m_isBlendModePreviewActive = false;
    bool m_blendModeSelectionCommitted = false;
    qreal m_opacityBeforeDrag = 1.0;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_LAYERSPANEL_H
