// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WORKSPACE_EFFECTCARD_H
#define RUWA_UI_WORKSPACE_EFFECTCARD_H

#include "features/effects/LayerEffectTypes.h"
#include "shell/context-menu/IContextMenuProvider.h"
#include "shared/widgets/reorderlist/ReorderableRowWidget.h"

#include <QColor>
#include <QHash>
#include <QPoint>
#include <QPointF>
#include <QUuid>
#include <QWidget>

class QVBoxLayout;
class QLabel;
class QPushButton;
class QGraphicsOpacityEffect;

namespace ruwa::ui::workspace {

/**
 * @brief A single layer-effect card: draggable-style header (grip + name +
 * bypass eye + overflow menu) above the effect's parameter editors.
 *
 * The card owns no model state; it emits intent signals and the owning
 * LayerEffectsPanel routes them through undoable mutations. Dragging the grip
 * starts a reorder (dragInitiated -> EffectsListView); reordering is also
 * available from the overflow menu.
 */
class EffectCard : public ruwa::ui::widgets::ReorderableRowWidget,
                   public ruwa::ui::widgets::IContextMenuProvider {
    Q_OBJECT

public:
    EffectCard(const ruwa::core::effects::LayerEffectDescriptor& descriptor,
        const ruwa::core::effects::LayerEffectState& state, QWidget* parent = nullptr);

    QUuid instanceId() const { return m_instanceId; }

    /// Synchronize the persistent card with the current model state. This is
    /// used after undo/redo and other external model mutations; editor signals
    /// are suppressed while values are being applied.
    void setEffectState(const ruwa::core::effects::LayerEffectState& state);

    // --- ReorderableRowWidget contract (flat list: no depth) ---
    QUuid itemId() const override { return m_instanceId; }
    int effectiveRowHeight() const override;
    void setDragging(bool dragging) override;
    void setRowOpacity(qreal v) override;

    /// Enable/disable the move actions in the overflow menu (list boundaries).
    void setMoveEnabled(bool canMoveUp, bool canMoveDown);

    // IContextMenuProvider — the "⋯" overflow menu is the project's themed
    // SimpleActions context menu, not a raw QMenu.
    ruwa::ui::widgets::ContextMenuType contextMenuType() const override;
    QVariantMap contextMenuContext() const override;
    void handleContextMenuAction(int actionId) override;

signals:
    void enabledToggled(bool enabled);
    void previewToggled(bool enabled);
    void moveUpRequested();
    void moveDownRequested();
    void duplicateRequested();
    void resetRequested();
    void removeRequested();
    void paramChanged(const QString& key, const QVariant& value);

    /// A param is being edited live via a drag — the shared colour picker or a
    /// range slider. The panel records each value immediately and merges all
    /// commands from the edit session into one undo step.
    void paramLiveChanged(const QString& key, const QVariant& value);
    /// Explicit boundary for editors that expose one (slider release / numeric
    /// editing finished). External pickers use the panel's inactivity fallback.
    void paramEditFinished();

    /// The colour capsule was clicked: open the shared colour picker anchored to
    /// \a sourceButton (a ColorInputButton). Forwarded up to the overlay.
    void colorPickerRequested(const QColor& color, QWidget* sourceButton);

    /// The position capsule was clicked: enter on-canvas position-picking mode.
    /// \a sourceField is the PositionInputField that requested it — the
    /// eventual pick (or cancel) is written straight back into it, which
    /// EffectCard turns into paramLiveChanged for the underlying x/y keys via
    /// PositionInputField::positionChanged.
    void positionPickerRequested(QWidget* sourceField, const QPointF& currentPosition);

    /// Emitted when the user drags the grip past the threshold, to start a reorder.
    void dragInitiated(const QUuid& id, const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildHeader(QVBoxLayout* root);
    void buildParams(QVBoxLayout* root);
    void addParamEditor(
        QVBoxLayout* parentLayout, const ruwa::core::effects::EffectParamDefinition& param);
    void addPositionParamEditor(QVBoxLayout* parentLayout,
        const ruwa::core::effects::EffectParamDefinition& xParam,
        const ruwa::core::effects::EffectParamDefinition& yParam);
    void showOverflowMenu();
    void refreshEyeIcon();
    void refreshMenuIcon();
    QVariant paramValue(const ruwa::core::effects::EffectParamDefinition& param) const;

    ruwa::core::effects::LayerEffectDescriptor m_descriptor;
    ruwa::core::effects::LayerEffectState m_state;
    QUuid m_instanceId;
    QHash<QString, QWidget*> m_paramEditorByKey;

    QLabel* m_titleLabel = nullptr;
    QPushButton* m_eyeButton = nullptr;
    QPushButton* m_menuButton = nullptr;
    QWidget* m_grip = nullptr;
    bool m_canMoveUp = false;
    bool m_canMoveDown = false;
    bool m_syncing = false;

    // Whole-card drag dim (see setDragging). Owned by setGraphicsEffect.
    QGraphicsOpacityEffect* m_dimEffect = nullptr;

    // Grip drag-initiation state
    bool m_dragging = false;
    bool m_gripPressed = false;
    bool m_dragArmed = false;
    QPoint m_pressGlobalPos;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_EFFECTCARD_H
