// SPDX-License-Identifier: MPL-2.0

#include "LayerEffectsPanel.h"

#include "EffectCard.h"
#include "EffectPickerPopup.h"
#include "EffectsListView.h"

#include "features/effects/LayerEffectRegistry.h"
#include "features/layers/model/LayerModel.h"
#include "shared/resources/IconProvider.h"
#include "shared/undo/LayerEffectCommands.h"
#include "shared/widgets/CapsuleButton.h"
#include "shared/widgets/inputs/SearchBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace ruwa::ui::workspace {

using namespace ruwa::core::effects;
using namespace ruwa::core::layers;

namespace {

/// Dashed rounded panel used for the "no effects yet" placeholder.
class DashedPanel : public QWidget {
public:
    explicit DashedPanel(QColor border, QWidget* parent)
        : QWidget(parent)
        , m_border(border)
    {
    }
    void setBorderColor(QColor color)
    {
        m_border = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(m_border, 1.4);
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern({ 4, 4 });
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 8, 8);
    }

private:
    QColor m_border;
};

} // namespace

LayerEffectsPanel::LayerEffectsPanel(QWidget* parent)
    : DockPanel(tr("Layer Effects"), parent)
{
    setIconType(ruwa::ui::core::IconProvider::StandardIcon::LayerEffectsPanel);
    setMinimumPanelSize(220, 180);
    setPreferredPanelSize(300, 360);
    setClosable(true);
    setFloatable(true);
    setMovable(true);

    // Delimits continuous input sessions. Undo commands themselves are pushed
    // immediately and merge by session id, so history never depends on timeout.
    m_paramSessionTimer = new QTimer(this);
    m_paramSessionTimer->setSingleShot(true);
    m_paramSessionTimer->setInterval(350);
    connect(
        m_paramSessionTimer, &QTimer::timeout, this, &LayerEffectsPanel::finishParamEditSession);
}

LayerEffectsPanel::~LayerEffectsPanel()
{
    finishParamEditSession();
    // The picker is reparented onto the shared OverlayContainer, so it is not
    // destroyed with this panel — clean it up explicitly.
    if (m_picker) {
        delete m_picker;
    }
}

void LayerEffectsPanel::setLayerModel(LayerModel* model)
{
    if (m_layerModel == model) {
        return;
    }

    finishParamEditSession();

    if (m_layerModel) {
        disconnect(m_layerModel, nullptr, this, nullptr);
    }

    m_layerModel = model;
    if (m_layerModel) {
        connect(m_layerModel, &LayerModel::selectionChanged, this,
            [this](const LayerId&) { refreshUi(); });
        connect(m_layerModel, &LayerModel::layerDataChanged, this,
            [this](const LayerId&) { refreshUi(); });
        connect(m_layerModel, &LayerModel::layerEffectsChanged, this,
            [this](const LayerId&, quint64) { refreshUi(); });
        connect(m_layerModel, &LayerModel::layersChanged, this, &LayerEffectsPanel::refreshUi);
    }
    refreshUi();
}

void LayerEffectsPanel::setPushUndoFn(PushUndoFn fn)
{
    m_pushUndoFn = std::move(fn);
}

void LayerEffectsPanel::setUndoCallbacks(
    RequestRenderFn requestRender, OnContentChangedFn onContentChanged)
{
    m_requestRender = std::move(requestRender);
    m_onContentChanged = std::move(onContentChanged);
}

void LayerEffectsPanel::setCanvasSizeProvider(CanvasSizeProviderFn fn)
{
    m_canvasSizeProvider = std::move(fn);
}

QWidget* LayerEffectsPanel::createContent()
{
    m_contentWidget = new QWidget();
    auto* rootLayout = new QVBoxLayout(m_contentWidget);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(10);

    // --- Toolbar row: Add button + search field ---
    auto* toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(8);

    // Style matches the colour-input capsule in the Color panel (pill border +
    // soft hover plate), i.e. the CapsuleButton Secondary variant.
    m_addEffectButton = new ruwa::ui::widgets::CapsuleButton(
        tr("+  Add"), ruwa::ui::widgets::CapsuleButton::Variant::Secondary, m_contentWidget);
    m_addEffectButton->setBannerBaseHeight(34);
    m_addEffectButton->setBaseMinimumWidth(0);
    m_addEffectButton->setSecondaryRestingFillAlt(true);
    m_addEffectButton->syncSizeToText();
    connect(m_addEffectButton, &QPushButton::clicked, this, &LayerEffectsPanel::showAddEffectPopup);
    toolbar->addWidget(m_addEffectButton);

    m_searchBar = new ruwa::ui::widgets::SearchBar(m_contentWidget);
    m_searchBar->setPlaceholder(tr("Search effects..."));
    m_searchBar->setBarHeight(34);
    m_searchBar->setMinimumBarWidth(0);
    connect(m_searchBar, &ruwa::ui::widgets::SearchBar::textChanged, this,
        &LayerEffectsPanel::applyCardFilter);
    toolbar->addWidget(m_searchBar, 1);

    rootLayout->addLayout(toolbar);

    // --- Message state (no layer / unavailable) ---
    m_messageLabel = new QLabel(m_contentWidget);
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setAlignment(Qt::AlignCenter);
    rootLayout->addWidget(m_messageLabel, 1);

    // --- Empty state (dashed placeholder) ---
    m_emptyState = new DashedPanel(colors().border, m_contentWidget);
    auto* emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setContentsMargins(16, 24, 16, 24);
    emptyLayout->setSpacing(10);
    emptyLayout->addStretch(1);
    auto* plus = new QLabel(QStringLiteral("+"), m_emptyState);
    plus->setAlignment(Qt::AlignCenter);
    QFont plusFont = colors().fonts.getUIFont(22);
    plus->setFont(plusFont);
    emptyLayout->addWidget(plus);
    auto* emptyText = new QLabel(tr("No effects yet.\nPress Add to apply one."), m_emptyState);
    emptyText->setAlignment(Qt::AlignCenter);
    emptyText->setWordWrap(true);
    emptyText->setObjectName(QStringLiteral("effectsEmptyText"));
    emptyLayout->addWidget(emptyText);
    emptyLayout->addStretch(1);
    rootLayout->addWidget(m_emptyState, 1);

    // --- Effects list (animated, drag-reorderable) ---
    m_listView = new EffectsListView(m_contentWidget);
    connect(m_listView, &EffectsListView::reordered, this,
        [this](const QUuid& id, int newIndex) { applyMoveEffect(id, newIndex); });
    rootLayout->addWidget(m_listView, 1);

    onThemeChanged();
    refreshUi();
    return m_contentWidget;
}

void LayerEffectsPanel::onThemeChanged()
{
    applyTheme();
}

void LayerEffectsPanel::showState(ViewState state, const QString& message)
{
    if (!m_contentWidget) {
        return;
    }
    m_messageLabel->setVisible(state == ViewState::Message);
    m_emptyState->setVisible(state == ViewState::Empty);
    m_listView->setVisible(state == ViewState::Effects);
    const bool canAdd = state != ViewState::Message;
    m_addEffectButton->setEnabled(canAdd);
    m_searchBar->setVisible(canAdd);
    if (state == ViewState::Message) {
        m_messageLabel->setText(message);
    }
}

void LayerEffectsPanel::refreshUi()
{
    if (!m_contentWidget || m_applyingMutation) {
        return;
    }

    LayerData* layer = selectedLayer();

    // Detect a layer switch: the whole card list is swapped for another
    // layer's effects, which is navigation — the list must snap into place.
    // The reveal/roll-up animations only play for add/remove happening on the
    // layer that is already shown (including its very first effect: the empty
    // state above still tracks m_shownLayerId, so adding to the shown layer
    // animates).
    const LayerId currentId = layer ? layer->id : LayerId();
    const bool sameLayer = (currentId == m_shownLayerId);
    m_shownLayerId = currentId;

    // Navigating away from a layer mid-drag: commit its pending edit now so it
    // lands on the correct layer's undo history before the list is swapped.
    if (m_paramEditActive && m_paramEditLayerId != currentId) {
        finishParamEditSession();
    }

    if (!m_layerModel || !layer) {
        removeAllCards();
        showState(ViewState::Message, tr("Select a layer to add effects."));
        return;
    }
    if (!canEditEffects(layer)) {
        removeAllCards();
        showState(ViewState::Message, tr("Layer effects are not available for this layer."));
        return;
    }
    if (layer->effects.isEmpty()) {
        removeAllCards();
        showState(ViewState::Empty);
        return;
    }

    showState(ViewState::Effects);
    syncCardsToLayer(sameLayer);
}

void LayerEffectsPanel::showAddEffectPopup()
{
    LayerData* layer = selectedLayer();
    if (!layer || !canEditEffects(layer) || !m_addEffectButton) {
        return;
    }
    // Re-click on the Add button while the picker is open toggles it closed. The
    // picker's own event filter already hides it on the anchor press (its shadow
    // margin can eat the button click), so here we only need to make sure a click
    // that *does* reach the button never re-opens it while it is open/closing.
    if (m_picker && m_picker->isPopupVisible()) {
        if (!m_picker->isHiding()) {
            m_picker->hidePopup();
        }
        return;
    }
    if (!m_picker) {
        m_picker = new EffectPickerPopup(this);
        connect(m_picker, &EffectPickerPopup::effectChosen, this,
            [this](const QString& typeId) { applyAddEffect(typeId); });
    }
    m_picker->popupUnder(m_addEffectButton);
}

bool LayerEffectsPanel::canEditEffects(const LayerData* layer) const
{
    return layer && !layer->isBackground();
}

LayerData* LayerEffectsPanel::selectedLayer() const
{
    return m_layerModel ? m_layerModel->selectedLayer() : nullptr;
}

void LayerEffectsPanel::removeAllCards()
{
    QList<ruwa::ui::widgets::ReorderableRowWidget*> all;
    for (EffectCard* card : m_cardById) {
        all.append(card);
    }
    m_cardById.clear();
    m_cardOrder.clear();
    // The view detaches the rows from its layout and deletes them.
    if (m_listView) {
        m_listView->syncRows({}, {}, all, false);
    }
}

EffectCard* LayerEffectsPanel::buildCard(
    const LayerData* layer, const LayerEffectState& effect, int index)
{
    const auto* descriptor = LayerEffectRegistry::instance().descriptor(effect.typeId);
    LayerEffectDescriptor descCopy = descriptor ? *descriptor : LayerEffectDescriptor {};
    if (!descriptor) {
        descCopy.typeId = effect.typeId;
        descCopy.displayName = effect.typeId;
    }

    auto* card = new EffectCard(descCopy, effect);
    card->setMoveEnabled(index > 0, layer && index < layer->effects.size() - 1);

    const QUuid id = effect.instanceId;
    connect(card, &EffectCard::dragInitiated, m_listView, &EffectsListView::beginDrag);
    connect(card, &EffectCard::enabledToggled, this,
        [this, id](bool enabled) { applyEnabled(id, enabled); });
    connect(card, &EffectCard::previewToggled, this,
        [this, id](bool enabled) { applyRealtimePreview(id, enabled); });
    // Move actions resolve the current index at click time (order changes).
    connect(card, &EffectCard::moveUpRequested, this,
        [this, id]() { applyMoveEffect(id, m_cardOrder.indexOf(id) - 1); });
    connect(card, &EffectCard::moveDownRequested, this,
        [this, id]() { applyMoveEffect(id, m_cardOrder.indexOf(id) + 1); });
    connect(
        card, &EffectCard::duplicateRequested, this, [this, id]() { applyDuplicateEffect(id); });
    connect(card, &EffectCard::resetRequested, this, [this, id]() { applyResetEffect(id); });
    connect(card, &EffectCard::removeRequested, this, [this, id]() { applyRemoveEffect(id); });
    connect(card, &EffectCard::paramChanged, this,
        [this, id](const QString& key, const QVariant& value) { applyParam(id, key, value); });
    connect(card, &EffectCard::paramLiveChanged, this,
        [this, id](const QString& key, const QVariant& value) { applyParamLive(id, key, value); });
    connect(card, &EffectCard::paramEditFinished, this, &LayerEffectsPanel::finishParamEditSession);
    // Bubble the colour-picker request up to the overlay (via WorkspaceTab).
    connect(
        card, &EffectCard::colorPickerRequested, this, &LayerEffectsPanel::colorPickerRequested);
    // Bubble the position-picker request up to the canvas (via WorkspaceTab).
    connect(card, &EffectCard::positionPickerRequested, this,
        &LayerEffectsPanel::positionPickerRequested);

    return card;
}

void LayerEffectsPanel::syncCardsToLayer(bool animate)
{
    LayerData* layer = selectedLayer();
    if (!m_listView || !layer || !canEditEffects(layer)) {
        return;
    }

    // Reuse existing cards by instance id; create cards for new effects.
    QSet<QUuid> desired;
    QSet<QUuid> newIds;
    QList<QUuid> order;
    const int n = layer->effects.size();
    for (int i = 0; i < n; ++i) {
        const LayerEffectState& e = layer->effects.at(i);
        const QUuid id = e.instanceId;
        EffectCard* card = m_cardById.value(id, nullptr);
        if (!card) {
            card = buildCard(layer, e, i);
            m_cardById.insert(id, card);
            newIds.insert(id);
        } else {
            card->setEffectState(e);
            card->setMoveEnabled(i > 0, i < n - 1);
        }
        order.append(id);
        desired.insert(id);
    }
    m_cardOrder = order;

    // Cards whose effect no longer exists — hand to the view to animate out.
    QList<ruwa::ui::widgets::ReorderableRowWidget*> removedRows;
    for (auto it = m_cardById.begin(); it != m_cardById.end();) {
        if (!desired.contains(it.key())) {
            removedRows.append(it.value());
            it = m_cardById.erase(it);
        } else {
            ++it;
        }
    }

    // Apply the search filter to build the visible ordered rows.
    const QString needle = m_cardFilter.trimmed();
    QList<ruwa::ui::widgets::ReorderableRowWidget*> visibleRows;
    QSet<QUuid> visibleNewIds;
    for (const QUuid& id : m_cardOrder) {
        EffectCard* card = m_cardById.value(id, nullptr);
        if (!card) {
            continue;
        }
        bool match = needle.isEmpty();
        if (!match) {
            for (QLabel* l : card->findChildren<QLabel*>()) {
                if (l->text().contains(needle, Qt::CaseInsensitive)) {
                    match = true;
                    break;
                }
            }
        }
        if (match) {
            visibleRows.append(card);
            if (newIds.contains(id)) {
                visibleNewIds.insert(id);
            }
        }
    }

    m_listView->syncRows(visibleRows, visibleNewIds, removedRows, animate);
}

void LayerEffectsPanel::applyCardFilter(const QString& text)
{
    m_cardFilter = text;
    // Filtering never animates and never adds/removes model cards.
    syncCardsToLayer(false);
}

template <typename CommandT, typename MutateFn>
void LayerEffectsPanel::applyEffectMutation(const LayerId& layerId, MutateFn mutate)
{
    if (!m_layerModel) {
        return;
    }
    // Close out any in-flight live edit first so undo history stays ordered and
    // this mutation's `before` snapshot already includes the dragged value.
    finishParamEditSession();
    LayerData* layer = m_layerModel->layerById(layerId);
    if (!layer || !canEditEffects(layer)) {
        return;
    }

    const EffectList before = layer->effects;
    m_applyingMutation = true;
    const bool changed = mutate();
    m_applyingMutation = false;
    if (!changed) {
        return;
    }

    layer = m_layerModel->layerById(layerId);
    const EffectList after = layer ? layer->effects : EffectList();
    if (before == after) {
        return;
    }
    if (m_pushUndoFn) {
        m_pushUndoFn(std::make_unique<CommandT>(
            m_layerModel, layerId, before, after, m_requestRender, m_onContentChanged));
    }
    refreshUi();
}

void LayerEffectsPanel::applyAddEffect(const QString& typeId)
{
    LayerData* layer = selectedLayer();
    if (!layer)
        return;
    const LayerId layerId = layer->id;
    const std::optional<QSize> canvasSize
        = m_canvasSizeProvider ? m_canvasSizeProvider() : std::nullopt;
    applyEffectMutation<aether::LayerEffectAddCommand>(
        layerId, [this, layerId, typeId, canvasSize]() {
            return !m_layerModel->addLayerEffect(layerId, typeId, -1, canvasSize).isNull();
        });
}

void LayerEffectsPanel::applyRemoveEffect(const QUuid& effectId)
{
    LayerData* layer = selectedLayer();
    if (!layer)
        return;
    const LayerId layerId = layer->id;
    applyEffectMutation<aether::LayerEffectRemoveCommand>(layerId,
        [this, layerId, effectId]() { return m_layerModel->removeLayerEffect(layerId, effectId); });
}

void LayerEffectsPanel::applyMoveEffect(const QUuid& effectId, int newIndex)
{
    LayerData* layer = selectedLayer();
    if (!layer)
        return;
    const LayerId layerId = layer->id;
    applyEffectMutation<aether::LayerEffectMoveCommand>(
        layerId, [this, layerId, effectId, newIndex]() {
            return m_layerModel->moveLayerEffect(layerId, effectId, newIndex);
        });
}

void LayerEffectsPanel::applyEnabled(const QUuid& effectId, bool enabled)
{
    LayerData* layer = selectedLayer();
    if (!layer)
        return;
    const LayerId layerId = layer->id;
    applyEffectMutation<aether::LayerEffectEnabledCommand>(
        layerId, [this, layerId, effectId, enabled]() {
            return m_layerModel->setLayerEffectEnabled(layerId, effectId, enabled);
        });
}

void LayerEffectsPanel::applyRealtimePreview(const QUuid& effectId, bool enabled)
{
    LayerData* layer = selectedLayer();
    if (!layer)
        return;
    const LayerId layerId = layer->id;
    applyEffectMutation<aether::LayerEffectRealtimePreviewCommand>(
        layerId, [this, layerId, effectId, enabled]() {
            return m_layerModel->setLayerEffectRealtimePreviewEnabled(layerId, effectId, enabled);
        });
}

void LayerEffectsPanel::applyParam(const QUuid& effectId, const QString& key, const QVariant& value)
{
    LayerData* layer = selectedLayer();
    if (!layer)
        return;
    const LayerId layerId = layer->id;
    applyEffectMutation<aether::LayerEffectParamCommand>(
        layerId, [this, layerId, effectId, key, value]() {
            return m_layerModel->setLayerEffectParam(layerId, effectId, key, value);
        });
}

void LayerEffectsPanel::applyParamLive(
    const QUuid& effectId, const QString& key, const QVariant& value)
{
    if (!m_layerModel) {
        return;
    }
    LayerData* layer = selectedLayer();
    if (!layer || !canEditEffects(layer)) {
        return;
    }
    const LayerId layerId = layer->id;

    // A live edit that jumps to a different layer or effect closes the previous
    // merge transaction before opening a new one.
    if (m_paramEditActive && (m_paramEditLayerId != layerId || m_paramEditEffectId != effectId)) {
        finishParamEditSession();
    }
    if (!m_paramEditActive) {
        m_paramEditActive = true;
        m_paramEditLayerId = layerId;
        m_paramEditEffectId = effectId;
        m_paramEditSessionId = QUuid::createUuid();
        m_layerModel->beginLayerEffectParamEdit(layerId, effectId, key);
    }

    const EffectList before = layer->effects;

    // Apply live so the canvas updates (model emits layerEffectsChanged); the
    // m_applyingMutation guard suppresses our own refreshUi so the card — and the
    // control the user is dragging — is not rebuilt mid-drag.
    m_applyingMutation = true;
    const bool changed = m_layerModel->setLayerEffectParam(layerId, effectId, key, value);
    m_applyingMutation = false;

    if (changed && m_pushUndoFn) {
        layer = m_layerModel->layerById(layerId);
        const EffectList after = layer ? layer->effects : EffectList();
        if (before != after) {
            m_pushUndoFn(std::make_unique<aether::LayerEffectParamCommand>(m_layerModel, layerId,
                before, after, m_requestRender, m_onContentChanged, m_paramEditSessionId));
        }
    }

    m_paramSessionTimer->start(); // (re)arm the session boundary
}

void LayerEffectsPanel::finishParamEditSession()
{
    if (!m_paramEditActive) {
        return;
    }
    m_paramEditActive = false;
    m_paramSessionTimer->stop();

    const LayerId editLayerId = m_paramEditLayerId;
    const QUuid editEffectId = m_paramEditEffectId;
    m_paramEditLayerId = LayerId();
    m_paramEditEffectId = QUuid();
    m_paramEditSessionId = QUuid();

    if (!m_layerModel) {
        return;
    }
    m_layerModel->endLayerEffectParamEdit(editLayerId, editEffectId);
}

void LayerEffectsPanel::applyDuplicateEffect(const QUuid& effectId)
{
    LayerData* layer = selectedLayer();
    if (!layer)
        return;
    const LayerId layerId = layer->id;

    // Snapshot the source effect before mutating.
    LayerEffectState source;
    int sourceIndex = -1;
    for (int i = 0; i < layer->effects.size(); ++i) {
        if (layer->effects.at(i).instanceId == effectId) {
            source = layer->effects.at(i);
            sourceIndex = i;
            break;
        }
    }
    if (sourceIndex < 0)
        return;

    applyEffectMutation<aether::LayerEffectAddCommand>(layerId, [&]() {
        const QUuid newId = m_layerModel->addLayerEffect(layerId, source.typeId, sourceIndex + 1);
        if (newId.isNull()) {
            return false;
        }
        for (auto it = source.params.cbegin(); it != source.params.cend(); ++it) {
            m_layerModel->setLayerEffectParam(layerId, newId, it.key(), it.value());
        }
        m_layerModel->setLayerEffectEnabled(layerId, newId, source.enabled);
        m_layerModel->setLayerEffectRealtimePreviewEnabled(
            layerId, newId, source.realtimePreviewEnabled);
        return true;
    });
}

void LayerEffectsPanel::applyResetEffect(const QUuid& effectId)
{
    LayerData* layer = selectedLayer();
    if (!layer)
        return;
    const LayerId layerId = layer->id;

    QString typeId;
    for (const LayerEffectState& e : layer->effects) {
        if (e.instanceId == effectId) {
            typeId = e.typeId;
            break;
        }
    }
    const auto* descriptor = LayerEffectRegistry::instance().descriptor(typeId);
    if (!descriptor)
        return;

    applyEffectMutation<aether::LayerEffectParamCommand>(layerId, [&]() {
        bool changed = false;
        for (const EffectParamDefinition& param : descriptor->params) {
            if (param.key.isEmpty()) {
                continue;
            }
            if (m_layerModel->setLayerEffectParam(
                    layerId, effectId, param.key, param.defaultValue)) {
                changed = true;
            }
        }
        return changed;
    });
}

void LayerEffectsPanel::applyTheme()
{
    if (!m_contentWidget) {
        return;
    }
    const auto& c = colors();
    m_contentWidget->setStyleSheet(
        QString("background: %1; color: %2;").arg(c.surface.name(), c.text.name()));

    // m_addEffectButton is a CapsuleButton — it themes itself, no stylesheet.
    if (m_messageLabel) {
        m_messageLabel->setStyleSheet(QString("color: %1;").arg(c.textMuted.name()));
    }
    if (m_emptyState) {
        static_cast<DashedPanel*>(m_emptyState)->setBorderColor(c.border);
        for (QLabel* l : m_emptyState->findChildren<QLabel*>()) {
            l->setStyleSheet(QString("color: %1;").arg(c.textMuted.name()));
        }
    }
}

} // namespace ruwa::ui::workspace
