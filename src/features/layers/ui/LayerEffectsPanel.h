// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WORKSPACE_PANELS_LAYEREFFECTSPANEL_H
#define RUWA_UI_WORKSPACE_PANELS_LAYEREFFECTSPANEL_H

#include "features/effects/LayerEffectTypes.h"
#include "features/layers/model/LayerData.h"
#include "shell/docking/widgets/DockPanel.h"
#include "shared/undo/UndoManager.h"

#include <QColor>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QPointF>
#include <QSize>
#include <QUuid>

#include <functional>
#include <memory>
#include <optional>

class QVBoxLayout;
class QWidget;
class QLabel;
class QPushButton;

namespace ruwa::ui::widgets {
class SearchBar;
class CapsuleButton;
} // namespace ruwa::ui::widgets

namespace ruwa::core::layers {
class LayerModel;
}

namespace ruwa::ui::workspace {

class EffectPickerPopup;
class EffectCard;
class EffectsListView;

class LayerEffectsPanel : public ruwa::ui::docking::DockPanel {
    Q_OBJECT

public:
    explicit LayerEffectsPanel(QWidget* parent = nullptr);
    ~LayerEffectsPanel() override;

    using PushUndoFn = std::function<void(std::unique_ptr<aether::IUndoCommand>)>;
    using RequestRenderFn = std::function<void()>;
    using OnContentChangedFn = std::function<void()>;
    /// Returns nullopt for an infinite canvas; otherwise the current document
    /// size, used to seed canvas-bound effect param defaults (e.g. Gradient
    /// Overlay's End Pos = bottom-right corner).
    using CanvasSizeProviderFn = std::function<std::optional<QSize>()>;

    void setLayerModel(ruwa::core::layers::LayerModel* model);
    void setPushUndoFn(PushUndoFn fn);
    void setUndoCallbacks(RequestRenderFn requestRender, OnContentChangedFn onContentChanged);
    void setCanvasSizeProvider(CanvasSizeProviderFn fn);

signals:
    /// Bubbled up from an effect's colour capsule; WorkspaceTab routes it to the
    /// shared colour-picker overlay (same path as LayerPropertiesPanel).
    void colorPickerRequested(const QColor& color, QWidget* sourceButton);

    /// Bubbled up from an effect's position capsule; WorkspaceTab routes it to
    /// CanvasPanel::beginPositionPicking.
    void positionPickerRequested(QWidget* sourceField, const QPointF& currentPosition);

protected:
    QWidget* createContent() override;
    void onThemeChanged() override;

private slots:
    void refreshUi();
    void showAddEffectPopup();

private:
    using EffectList = QList<ruwa::core::effects::LayerEffectState>;

    enum class ViewState { Message, Empty, Effects };

    bool canEditEffects(const ruwa::core::layers::LayerData* layer) const;
    ruwa::core::layers::LayerData* selectedLayer() const;
    /// Delete every card immediately (used when leaving the effects view state).
    void removeAllCards();
    void showState(ViewState state, const QString& message = QString());
    EffectCard* buildCard(const ruwa::core::layers::LayerData* layer,
        const ruwa::core::effects::LayerEffectState& effect, int index);
    /// Reconcile the persistent card set to the selected layer's effects and
    /// hand the (filtered) ordered rows to the list view.
    void syncCardsToLayer(bool animate);
    void applyCardFilter(const QString& text);

    template <typename CommandT, typename MutateFn>
    void applyEffectMutation(const ruwa::core::layers::LayerId& layerId, MutateFn mutate);

    void applyAddEffect(const QString& typeId);
    void applyRemoveEffect(const QUuid& effectId);
    void applyMoveEffect(const QUuid& effectId, int newIndex);
    void applyEnabled(const QUuid& effectId, bool enabled);
    void applyRealtimePreview(const QUuid& effectId, bool enabled);
    void applyParam(const QUuid& effectId, const QString& key, const QVariant& value);
    void applyDuplicateEffect(const QUuid& effectId);
    void applyResetEffect(const QUuid& effectId);
    void applyTheme();

    /// Live param edits arrive continuously while dragging — a colour picker
    /// or range slider alike. Every model change is recorded in the undo stack
    /// immediately; commands
    /// from the same edit session merge into one undo step. The session timer
    /// only delimits otherwise continuous input and is not responsible for
    /// history safety.
    void applyParamLive(const QUuid& effectId, const QString& key, const QVariant& value);
    void finishParamEditSession();

private:
    ruwa::core::layers::LayerModel* m_layerModel = nullptr;
    QWidget* m_contentWidget = nullptr;
    ruwa::ui::widgets::CapsuleButton* m_addEffectButton = nullptr;
    ruwa::ui::widgets::SearchBar* m_searchBar = nullptr;
    QLabel* m_messageLabel = nullptr;
    QWidget* m_emptyState = nullptr;
    EffectsListView* m_listView = nullptr;
    QHash<QUuid, EffectCard*> m_cardById; // persistent cards, keyed by effect instance id
    QList<QUuid> m_cardOrder; // current effect order (by instance id)
    QPointer<EffectPickerPopup> m_picker;
    PushUndoFn m_pushUndoFn;
    RequestRenderFn m_requestRender;
    OnContentChangedFn m_onContentChanged;
    CanvasSizeProviderFn m_canvasSizeProvider;
    bool m_applyingMutation = false;
    QString m_cardFilter;

    // Live-param-edit transaction (see applyParamLive).
    class QTimer* m_paramSessionTimer = nullptr;
    ruwa::core::layers::LayerId m_paramEditLayerId;
    QUuid m_paramEditEffectId;
    QUuid m_paramEditSessionId;
    bool m_paramEditActive = false;
    // Layer whose effects the panel currently shows (null = none). Switching
    // layers swaps the whole list — that's navigation and must snap; the
    // reveal/roll-up animations are reserved for add/remove within one layer.
    ruwa::core::layers::LayerId m_shownLayerId;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_LAYEREFFECTSPANEL_H
