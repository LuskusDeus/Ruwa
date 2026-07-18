// SPDX-License-Identifier: MPL-2.0

// LayersPanel.h
#ifndef RUWA_UI_WORKSPACE_PANELS_LAYERSPANEL_H
#define RUWA_UI_WORKSPACE_PANELS_LAYERSPANEL_H

#include "shell/docking/widgets/DockPanel.h"
#include "features/layers/model/LayerData.h"
#include "features/layers/model/LayerModel.h"
#include "features/layers/ui/LayerListView.h"

#include <QRect>
#include <QSize>
#include <QTimer>
#include <functional>
#include <memory>

class QPushButton;

namespace aether {
class IUndoCommand;
}

namespace ruwa::ui::widgets {
class BaseStyledWidget;
class BaseAnimatedButton;
class OpacitySliderWidget;
class AnimatedComboBox;
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
    explicit LayersPanel(QWidget* parent = nullptr);
    ~LayersPanel() override;

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
    void scheduleThumbnailRefresh();
    void invalidateLayerThumbnails(const QList<ruwa::core::layers::LayerId>& ids);
    void setThumbnailLoadingMode(bool active);
    void setFillProcessingLayer(const ruwa::core::layers::LayerId& id);
    ruwa::core::layers::LayerData* selectedLayer() const;
    void selectLayer(const ruwa::core::layers::LayerId& id);
    bool copySelectedLayerSnapshots(
        QList<std::shared_ptr<ruwa::core::layers::LayerData>>* snapshots) const;
    bool pasteLayerSnapshots(
        const QList<std::shared_ptr<ruwa::core::layers::LayerData>>& snapshots);
    bool deleteSelectedLayers();
    bool duplicateSelectedLayers();
    bool mergeSelectedLayerDown();
    bool mergeVisibleLayers();
    bool mergeSelectedLayers();
    /// Contextual merge (Ctrl+E / toolbar): merges the selection or merges down,
    /// showing a custom warning when the merge is blocked by a Background or
    /// Smart/Board layer. Returns true only if a merge actually happened.
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
    /// Emitted before an operation invalidates the current canvas edit target.
    /// Pending operations such as transform must be committed synchronously.
    void aboutToPerformTransformIncompatibleEdit();
    void layerSelected(const ruwa::core::layers::LayerId& id);
    void layerContentSelectionRequested(const ruwa::core::layers::LayerId& id);
    void layerTextEditRequested(const ruwa::core::layers::LayerId& id);
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

    void layerNameChanged(const ruwa::core::layers::LayerId& id, const QString& name);
    void layerOrderChanged(const ruwa::core::layers::LayerId& movedId,
        const ruwa::core::layers::LayerId& newParentId, int newIndex);

    /// Raster pixel clear (handled by workspace → canvas GL).
    void layerClearPixelContentRequested(const ruwa::core::layers::LayerId& id);
    /// Smart → raster bake (handled by workspace → canvas GL).
    void layerRasterizeSmartRequested(const ruwa::core::layers::LayerId& id);
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
    void changeEvent(QEvent* event) override;
    void onThemeChanged() override;

private slots:
    void onLayerSelected(const ruwa::core::layers::LayerId& id, Qt::KeyboardModifiers modifiers);
    void onLayerPaintTargetSelected(
        const ruwa::core::layers::LayerId& id, bool maskTarget, Qt::KeyboardModifiers modifiers);
    void onLayerContentSelectionRequested(const ruwa::core::layers::LayerId& id);
    void onLayerTextEditRequested(const ruwa::core::layers::LayerId& id);
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
    void onLayerDeleteRequested(const ruwa::core::layers::LayerId& id);
    void onLayerQuickClippingMaskRequested(const ruwa::core::layers::LayerId& id);
    void onLayerToggleAlphaLockRequested(const ruwa::core::layers::LayerId& id);
    void onLayerToggleLockRequested(const ruwa::core::layers::LayerId& id);
    void onLayerClearPixelsRequested(const ruwa::core::layers::LayerId& id);
    void onLayerRasterizeSmartRequested(const ruwa::core::layers::LayerId& id);
    void onLayerApplyMaskRequested(const ruwa::core::layers::LayerId& id);
    void onLayerInvertMaskRequested(const ruwa::core::layers::LayerId& id);
    void onLayerApplyEffectsRequested(const ruwa::core::layers::LayerId& id);

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
    void showMergeWarning(const QString& message);

    void setupToolbar(QWidget* container);
    void applyToolbarTheme();
    void populateBlendModeCombo();
    void retranslateUi();
    void syncLayerControls();

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

    QRect m_displayFrame;
    QTimer m_thumbnailRefreshTimer;
    PushUndoFn m_pushUndoFn;
    RequestRenderFn m_requestRenderFn;
    OnContentChangedFn m_onContentChangedFn;
    FillMaskFromSelectionFn m_fillMaskFromSelectionFn;
    bool m_syncingLayerControls = false;
    bool m_lastMaskEditTarget = false;
    ruwa::core::layers::BlendMode m_blendModeBeforePreview = ruwa::core::layers::BlendMode::Normal;
    bool m_isBlendModePreviewActive = false;
    bool m_blendModeSelectionCommitted = false;
    qreal m_opacityBeforeDrag = 1.0;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_LAYERSPANEL_H
