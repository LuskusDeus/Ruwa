// SPDX-License-Identifier: MPL-2.0

// LayerSelectionManager.cpp
#include "LayerSelectionManager.h"
namespace ruwa::core::layers {

// ============================================================================
// Construction
// ============================================================================

LayerSelectionManager::LayerSelectionManager(QObject* parent)
    : QObject(parent)
{
}

// ============================================================================
// Selection Operations
// ============================================================================

void LayerSelectionManager::setSelection(const LayerId& id)
{
    if (id.isNull()) {
        clearSelection();
        return;
    }

    // Если уже единственное выделение этого ID — ничего не делаем
    if (m_selectedIds.size() == 1 && m_primaryId == id && m_selectedIds.contains(id)) {
        return;
    }

    // Запоминаем старые для точечных уведомлений
    QSet<LayerId> oldSelected = m_selectedIds;

    m_selectedIds.clear();
    m_selectedIds.insert(id);
    m_primaryId = id;

    // Уведомляем о снятии выделения со старых
    for (const LayerId& oldId : oldSelected) {
        if (oldId != id) {
            emit layerSelectionStateChanged(oldId, false);
        }
    }

    // Уведомляем о новом выделении (если не было раньше)
    if (!oldSelected.contains(id)) {
        emit layerSelectionStateChanged(id, true);
    }

    emitChange();
}

void LayerSelectionManager::addToSelection(const LayerId& id)
{
    if (id.isNull())
        return;

    if (m_selectedIds.contains(id)) {
        // Уже выделен — просто делаем primary
        if (m_primaryId != id) {
            m_primaryId = id;
            emitChange();
        }
        return;
    }

    m_selectedIds.insert(id);
    m_primaryId = id;

    emit layerSelectionStateChanged(id, true);
    emitChange();
}

void LayerSelectionManager::removeFromSelection(const LayerId& id)
{
    if (!m_selectedIds.contains(id))
        return;

    m_selectedIds.remove(id);

    // Если убрали primary — выбираем другой
    if (m_primaryId == id) {
        setPrimaryFromRemaining();
    }

    emit layerSelectionStateChanged(id, false);
    emitChange();
}

void LayerSelectionManager::toggleSelection(const LayerId& id)
{
    if (m_selectedIds.contains(id)) {
        removeFromSelection(id);
    } else {
        addToSelection(id);
    }
}

void LayerSelectionManager::selectRange(const LayerId& fromId, const LayerId& toId,
    const std::function<QList<LayerData*>()>& flattenedLayers)
{
    if (fromId.isNull() || toId.isNull() || !flattenedLayers) {
        setSelection(toId);
        return;
    }

    QList<LayerData*> flattened = flattenedLayers();

    int fromIndex = -1;
    int toIndex = -1;

    for (int i = 0; i < flattened.size(); ++i) {
        if (flattened[i]->id == fromId)
            fromIndex = i;
        if (flattened[i]->id == toId)
            toIndex = i;
    }

    if (fromIndex < 0 || toIndex < 0) {
        setSelection(toId);
        return;
    }

    if (fromIndex > toIndex)
        std::swap(fromIndex, toIndex);

    QSet<LayerId> oldSelected = m_selectedIds;
    m_selectedIds.clear();

    for (int i = fromIndex; i <= toIndex; ++i) {
        m_selectedIds.insert(flattened[i]->id);
    }

    m_primaryId = toId;

    // Точечные уведомления
    for (const LayerId& oldId : oldSelected) {
        if (!m_selectedIds.contains(oldId)) {
            emit layerSelectionStateChanged(oldId, false);
        }
    }
    for (const LayerId& newId : m_selectedIds) {
        if (!oldSelected.contains(newId)) {
            emit layerSelectionStateChanged(newId, true);
        }
    }

    emitChange();
}

void LayerSelectionManager::selectAll(
    const std::function<void(const std::function<void(LayerData*)>&)>& forEachLayer)
{
    if (!forEachLayer)
        return;

    QSet<LayerId> oldSelected = m_selectedIds;
    m_selectedIds.clear();

    forEachLayer([this](LayerData* layer) { m_selectedIds.insert(layer->id); });

    if (!m_selectedIds.isEmpty() && m_primaryId.isNull()) {
        m_primaryId = *m_selectedIds.begin();
    }

    // Точечные уведомления
    for (const LayerId& oldId : oldSelected) {
        if (!m_selectedIds.contains(oldId)) {
            emit layerSelectionStateChanged(oldId, false);
        }
    }
    for (const LayerId& newId : m_selectedIds) {
        if (!oldSelected.contains(newId)) {
            emit layerSelectionStateChanged(newId, true);
        }
    }

    emitChange();
}

void LayerSelectionManager::clearSelection()
{
    if (m_selectedIds.isEmpty() && m_primaryId.isNull())
        return;

    QSet<LayerId> oldSelected = m_selectedIds;

    m_selectedIds.clear();
    m_primaryId = LayerId();

    for (const LayerId& oldId : oldSelected) {
        emit layerSelectionStateChanged(oldId, false);
    }

    emitChange();
}

// ============================================================================
// Navigation
// ============================================================================

void LayerSelectionManager::selectNext(const std::function<QList<LayerData*>()>& flattenedLayers)
{
    if (!flattenedLayers)
        return;

    QList<LayerData*> flattened = flattenedLayers();
    if (flattened.isEmpty())
        return;

    if (m_primaryId.isNull()) {
        setSelection(flattened.first()->id);
        return;
    }

    int currentIndex = -1;
    for (int i = 0; i < flattened.size(); ++i) {
        if (flattened[i]->id == m_primaryId) {
            currentIndex = i;
            break;
        }
    }

    if (currentIndex < 0 || currentIndex >= flattened.size() - 1)
        return;

    setSelection(flattened[currentIndex + 1]->id);
}

void LayerSelectionManager::selectPrevious(
    const std::function<QList<LayerData*>()>& flattenedLayers)
{
    if (!flattenedLayers)
        return;

    QList<LayerData*> flattened = flattenedLayers();
    if (flattened.isEmpty())
        return;

    if (m_primaryId.isNull()) {
        setSelection(flattened.last()->id);
        return;
    }

    int currentIndex = -1;
    for (int i = 0; i < flattened.size(); ++i) {
        if (flattened[i]->id == m_primaryId) {
            currentIndex = i;
            break;
        }
    }

    if (currentIndex <= 0)
        return;

    setSelection(flattened[currentIndex - 1]->id);
}

// ============================================================================
// Maintenance
// ============================================================================

void LayerSelectionManager::onLayerRemoved(const LayerId& id, const QList<LayerId>& descendants)
{
    bool changed = false;

    // Убираем потомков
    for (const LayerId& descId : descendants) {
        if (m_selectedIds.remove(descId)) {
            emit layerSelectionStateChanged(descId, false);
            changed = true;
        }
    }

    // Убираем сам слой
    if (m_selectedIds.remove(id)) {
        emit layerSelectionStateChanged(id, false);
        changed = true;
    }

    // Обновляем primary если нужно
    if (m_primaryId == id || descendants.contains(m_primaryId)) {
        setPrimaryFromRemaining();
        changed = true;
    }

    if (changed) {
        emitChange();
    }
}

void LayerSelectionManager::notifySelectionChanged()
{
    emitChange();
}

void LayerSelectionManager::applySelectionState(
    const LayerId& primaryId, const QSet<LayerId>& selectedIds)
{
    if (m_primaryId == primaryId && m_selectedIds == selectedIds)
        return;

    QSet<LayerId> oldSelected = m_selectedIds;

    m_selectedIds = selectedIds;
    m_primaryId = primaryId;

    for (const LayerId& oldId : oldSelected) {
        if (!m_selectedIds.contains(oldId)) {
            emit layerSelectionStateChanged(oldId, false);
        }
    }
    for (const LayerId& newId : m_selectedIds) {
        if (!oldSelected.contains(newId)) {
            emit layerSelectionStateChanged(newId, true);
        }
    }

    emitChange();
}

// ============================================================================
// Private
// ============================================================================

void LayerSelectionManager::emitChange()
{
    emit selectionChanged(m_primaryId);
}

void LayerSelectionManager::setPrimaryFromRemaining()
{
    if (m_selectedIds.isEmpty()) {
        m_primaryId = LayerId();
    } else {
        m_primaryId = *m_selectedIds.begin();
    }
}

} // namespace ruwa::core::layers
