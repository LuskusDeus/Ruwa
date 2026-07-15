// SPDX-License-Identifier: MPL-2.0

// LayerSelectionManager.h
#ifndef RUWA_CORE_LAYERS_LAYERSELECTIONMANAGER_H
#define RUWA_CORE_LAYERS_LAYERSELECTIONMANAGER_H

#include "LayerData.h"

#include <QObject>
#include <QSet>
#include <QList>
#include <functional>

namespace ruwa::core::layers {

/**
 * @brief Централизованный менеджер выделения слоёв.
 *
 * Единый источник истины для состояния выделения.
 * Отвечает за:
 *   - Хранение primary selection и multi-selection
 *   - Операции: set, add, remove, toggle, range, clear
 *   - Уведомления через сигналы
 *   - Простой API для проверки состояния
 *
 * Не владеет данными слоёв — работает только с LayerId.
 * Для операций типа selectRange требуется функция-резолвер
 * для получения flattened списка.
 */
class LayerSelectionManager : public QObject {
    Q_OBJECT

public:
    explicit LayerSelectionManager(QObject* parent = nullptr);
    ~LayerSelectionManager() override = default;

    // ========================================================================
    // State Query
    // ========================================================================

    /** @brief Primary selected layer ID (может быть null) */
    LayerId primaryId() const { return m_primaryId; }

    /** @brief Все выделенные ID */
    const QSet<LayerId>& selectedIds() const { return m_selectedIds; }

    /** @brief Количество выделенных */
    int count() const { return m_selectedIds.size(); }

    /** @brief Есть ли выделение */
    bool hasSelection() const { return !m_selectedIds.isEmpty(); }

    /** @brief Проверить, выделен ли конкретный слой */
    bool isSelected(const LayerId& id) const { return m_selectedIds.contains(id); }

    /** @brief Проверить, является ли слой primary */
    bool isPrimary(const LayerId& id) const { return m_primaryId == id; }

    // ========================================================================
    // Selection Operations
    // ========================================================================

    /**
     * @brief Установить единственное выделение (очищает предыдущее).
     * Стандартный клик без модификаторов.
     */
    void setSelection(const LayerId& id);

    /**
     * @brief Добавить к выделению (Ctrl+click).
     * Новый id становится primary.
     */
    void addToSelection(const LayerId& id);

    /**
     * @brief Убрать из выделения.
     * Если это был primary — выбираем другой из оставшихся.
     */
    void removeFromSelection(const LayerId& id);

    /**
     * @brief Переключить состояние (Ctrl+click на уже выделенном).
     */
    void toggleSelection(const LayerId& id);

    /**
     * @brief Выделить диапазон (Shift+click).
     * Требует flattened список для определения диапазона.
     * @param fromId Начало диапазона (обычно текущий primary)
     * @param toId Конец диапазона (куда кликнули)
     * @param flattenedLayers Функция, возвращающая flattened список
     */
    void selectRange(const LayerId& fromId, const LayerId& toId,
        const std::function<QList<LayerData*>()>& flattenedLayers);

    /**
     * @brief Выделить все слои.
     * @param allLayers Функция, возвращающая все слои (обычно через forEach)
     */
    void selectAll(const std::function<void(const std::function<void(LayerData*)>&)>& forEachLayer);

    /**
     * @brief Очистить всё выделение.
     */
    void clearSelection();

    // ========================================================================
    // Navigation
    // ========================================================================

    /**
     * @brief Выделить следующий слой в списке.
     * @param flattenedLayers Функция для получения flattened списка
     */
    void selectNext(const std::function<QList<LayerData*>()>& flattenedLayers);

    /**
     * @brief Выделить предыдущий слой в списке.
     * @param flattenedLayers Функция для получения flattened списка
     */
    void selectPrevious(const std::function<QList<LayerData*>()>& flattenedLayers);

    // ========================================================================
    // Maintenance
    // ========================================================================

    /**
     * @brief Обновить выделение при удалении слоя.
     * Убирает id из выделения, а также всех его потомков.
     * @param id Удаляемый слой
     * @param descendants Список потомков (можно пустой)
     */
    void onLayerRemoved(const LayerId& id, const QList<LayerId>& descendants = {});

    /**
     * @brief Принудительно переэмитить selectionChanged.
     * Используется после rebuild виджетов для гарантированной синхронизации.
     */
    void notifySelectionChanged();

    // ========================================================================
    // Undo/Redo: apply saved state (for selection restore)
    // ========================================================================

    /**
     * @brief Применить сохранённое состояние выделения.
     * Используется при undo/redo.
     */
    void applySelectionState(const LayerId& primaryId, const QSet<LayerId>& selectedIds);

signals:
    /**
     * @brief Выделение изменилось.
     * @param primaryId Текущий primary (может быть null)
     */
    void selectionChanged(const LayerId& primaryId);

    /**
     * @brief Конкретный слой был добавлен/убран из выделения.
     * Для точечных обновлений UI без полной пересинхронизации.
     */
    void layerSelectionStateChanged(const LayerId& id, bool selected);

private:
    void emitChange();
    void setPrimaryFromRemaining();

    LayerId m_primaryId;
    QSet<LayerId> m_selectedIds;
};

} // namespace ruwa::core::layers

#endif // RUWA_CORE_LAYERS_LAYERSELECTIONMANAGER_H
