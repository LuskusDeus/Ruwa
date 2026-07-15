// SPDX-License-Identifier: MPL-2.0

// LayerModel.h
#ifndef RUWA_CORE_LAYERS_LAYERMODEL_H
#define RUWA_CORE_LAYERS_LAYERMODEL_H

#include "LayerData.h"
#include "LayerSelectionManager.h"
#include "shared/tiles/TileFormat.h"

#include <QObject>
#include <QList>
#include <QSet>
#include <QHash>
#include <QSize>
#include <QVariant>
#include <memory>
#include <functional>
#include <optional>

namespace ruwa::core::serialization {
struct LayerEntry;
struct LayerEffectsEntry;
} // namespace ruwa::core::serialization

namespace ruwa::core::layers {

/**
 * @brief Central model for layer management
 *
 * Owns all layer data and manages:
 * - Layer hierarchy (add, remove, move, reorder)
 * - Selection state (single and multi-selection)
 * - Notifications via Qt signals
 *
 * Designed for easy UI integration with rich navigation API.
 */
class LayerModel : public QObject {
    Q_OBJECT

public:
    explicit LayerModel(QObject* parent = nullptr);
    ~LayerModel() override = default;

    // ========================================================================
    // Layer Access
    // ========================================================================

    /** @brief Get root layers (top-level) */
    const QList<std::shared_ptr<LayerData>>& rootLayers() const { return m_rootLayers; }

    /** @brief Find layer by ID anywhere in hierarchy */
    LayerData* layerById(const LayerId& id) const;

    /** @brief Check if layer exists */
    bool contains(const LayerId& id) const { return layerById(id) != nullptr; }

    /** @brief Is the model empty */
    bool isEmpty() const { return m_rootLayers.isEmpty(); }

    /** @brief Total count of all layers */
    int totalCount() const;

    // ========================================================================
    // Document tile format (Slice 2: per-document pixel storage format)
    // ========================================================================

    /** @brief The per-document tile pixel format new content grids are stamped
     *  with. Masks/coverage grids stay RGBA8 regardless. Falls back to
     *  kDefaultTileFormat for context-less models. */
    aether::TilePixelFormat documentTileFormat() const { return m_documentTileFormat; }
    void setDocumentTileFormat(aether::TilePixelFormat fmt) { m_documentTileFormat = fmt; }

    /** @brief Count of visible layers (respects expansion) */
    int visibleCount() const;

    /** @brief Alias for visibleCount */
    int totalVisibleCount() const { return visibleCount(); }

    // ========================================================================
    // Flattened Views (for UI)
    // ========================================================================

    /** @brief Get flattened list respecting expansion state */
    QList<LayerData*> flattenedLayers() const;

    /** @brief Get all layers flattened (ignores expansion) */
    QList<LayerData*> allLayersFlattened() const;

    /** @brief Alias for flattenedLayers */
    QList<LayerData*> flattenedVisibleLayers() const { return flattenedLayers(); }

    /** @brief Get index in flattened visible list */
    int indexInFlattenedList(const LayerId& id) const;

    /** @brief Get layer at flattened index */
    LayerData* layerAtFlatIndex(int index) const;

    // ========================================================================
    // Layer Hierarchy Information
    // ========================================================================

    /** @brief Get parent of layer (or nullptr for root) */
    LayerData* parentOf(const LayerId& id) const;

    /** @brief Get siblings of layer */
    QList<LayerData*> siblingsOf(const LayerId& id) const;

    /** @brief Get ancestors of layer (parent to root) */
    QList<LayerData*> ancestorsOf(const LayerId& id) const;

    /** @brief Get all descendants of layer */
    QList<LayerData*> descendantsOf(const LayerId& id) const;

    /** @brief Check if 'descendant' is inside 'ancestor' */
    bool isDescendantOf(const LayerId& descendant, const LayerId& ancestor) const;

    /** @brief Get depth/nesting level of layer */
    int depthOf(const LayerId& id) const;

    // ========================================================================
    // Layer Addition
    // ========================================================================

    /** @brief Add layer at root level */
    void addLayer(std::shared_ptr<LayerData> layer, int index = -1);
    /** @brief Add multiple root layers and emit one structure update */
    void addLayers(const QList<std::shared_ptr<LayerData>>& layers, int index = -1);

    /** @brief Add layer as child of parent */
    void addLayerTo(std::shared_ptr<LayerData> layer, LayerData* parent, int index = -1);

    /** @brief Add layer as child of parent by id */
    void addLayerTo(std::shared_ptr<LayerData> layer, const LayerId& parentId, int index = -1);

    /** @brief Create and add new layer */
    LayerData* createLayer(const QString& name = "Layer", int index = -1);

    /** @brief Create and add new smart layer */
    LayerData* createSmartLayer(const QString& name = "Smart", int index = -1);

    /** @brief Create and add new text layer */
    LayerData* createTextLayer(const QString& text = QString(), int index = -1);

    /** @brief Create and add new group */
    LayerData* createGroup(const QString& name = "Group", int index = -1);

    /** @brief Create and add new adjustment layer */
    LayerData* createAdjustmentLayer(const QString& name = QString(), int index = -1);

    /** @brief Create and add layer to parent */
    LayerData* createLayerIn(LayerData* parent, const QString& name = "Layer", int index = -1);

    /** @brief Create and add group to parent */
    LayerData* createGroupIn(LayerData* parent, const QString& name = "Group", int index = -1);

    /** @brief Create and add adjustment layer to parent */
    LayerData* createAdjustmentLayerIn(
        LayerData* parent, const QString& name = QString(), int index = -1);

    /** @brief Get root-level background layer if present */
    LayerData* backgroundLayer() const;

    // ========================================================================
    // Layer Removal
    // ========================================================================

    /** @brief Remove layer by ID */
    void removeLayer(const LayerId& id);

    /** @brief Remove multiple layers */
    void removeLayers(const QList<LayerId>& ids);

    /** @brief Clear all layers */
    void clear();

    // ========================================================================
    // Layer Movement
    // ========================================================================

    /**
     * @brief Move layer to new position
     * @param id Layer to move
     * @param newParent New parent (nullptr for root)
     * @param index Position within new parent (-1 for end)
     * @return true if moved, false if same position or invalid
     */
    bool moveLayer(const LayerId& id, LayerData* newParent, int index = -1);
    /** @brief Move multiple layers in the given order and emit one structure update. */
    bool moveLayers(const QList<LayerId>& ids, LayerData* newParent, int index = -1);

    /** @brief Move layer to root level */
    bool moveToRoot(const LayerId& id, int index = -1);

    /** @brief Move layer to be child of parent */
    bool moveTo(const LayerId& id, const LayerId& newParentId, int index = -1);

    /** @brief Reorder within same parent */
    bool reorder(const LayerId& id, int newIndex);

    /** @brief Move layer up in visual order */
    bool moveUp(const LayerId& id);

    /** @brief Move layer down in visual order */
    bool moveDown(const LayerId& id);

    // ========================================================================
    // Layer Properties
    // ========================================================================

    void setLayerName(const LayerId& id, const QString& name);
    bool refreshTextLayerAutoName(const LayerId& id);
    void setLayerVisible(const LayerId& id, bool visible);
    void setLayerLocked(const LayerId& id, bool locked);
    void setLayerAlphaLock(const LayerId& id, bool alphaLock);
    void setLayerExpanded(const LayerId& id, bool expanded);
    void setLayerOpacity(const LayerId& id, qreal opacity);
    void setLayerBlendMode(const LayerId& id, BlendMode mode);
    void setGroupCompositingMode(const LayerId& id, GroupCompositingMode mode);
    void setLayerDisplayColorIndex(const LayerId& id, quint8 colorIndex);
    void setLayerBackgroundColor(const LayerId& id, const QColor& color);
    void setLayerBackgroundTransparent(const LayerId& id, bool transparent);
    void setLayerClippedToBelow(const LayerId& id, bool clipped);
    /// canvasSize is nullopt for an infinite canvas; passed through to
    /// LayerEffectRegistry::createState so canvas-bound param defaults
    /// (e.g. Gradient Overlay's End Pos) resolve against the real document size.
    QUuid addLayerEffect(const LayerId& layerId, const QString& typeId, int index = -1,
        const std::optional<QSize>& canvasSize = std::nullopt);
    bool removeLayerEffect(const LayerId& layerId, const QUuid& effectInstanceId);
    bool moveLayerEffect(const LayerId& layerId, const QUuid& effectInstanceId, int newIndex);
    bool setLayerEffectEnabled(const LayerId& layerId, const QUuid& effectInstanceId, bool enabled);
    bool setLayerEffectRealtimePreviewEnabled(
        const LayerId& layerId, const QUuid& effectInstanceId, bool enabled);
    bool setLayerEffectParam(const LayerId& layerId, const QUuid& effectInstanceId,
        const QString& key, const QVariant& value);
    void beginLayerEffectParamEdit(
        const LayerId& layerId, const QUuid& effectInstanceId, const QString& paramKey);
    void endLayerEffectParamEdit(const LayerId& layerId, const QUuid& effectInstanceId);
    bool replaceLayerEffectState(const LayerId& layerId, const QUuid& effectInstanceId,
        const ruwa::core::effects::LayerEffectState& state);
    bool replaceLayerEffects(const LayerId& layerId,
        const QList<ruwa::core::effects::LayerEffectState>& effects,
        bool affectsDocumentResult = true);
    void notifyLayerDataChanged(const LayerId& id);
    void notifyBulkLayerContentChanged();
    bool applyClippingToSelection(const LayerId& baseLayerId);
    bool applyQuickClippingMaskToSelection();
    /** @brief Whether a clip base exists below \a layerId in the visible layer list
     * (flattenedLayers / panel order). */
    bool hasQuickClippingMaskTargetBelow(const LayerId& layerId) const;
    /** @brief Clip the given layer to the layer below it (swipe left-to-right on layer). */
    bool clipLayerToBelow(const LayerId& layerId);
    /** @brief Unclip the given layer (swipe right-to-left, reserved for future). */
    bool unclipLayerFromBelow(const LayerId& layerId);
    /** @brief Duplicate selected layers. Returns IDs of newly added layers. */
    QList<LayerId> duplicateSelectedLayers();
    bool toggleVisibilityForSelection();

    /** @brief Toggle visibility */
    void toggleVisibility(const LayerId& id);

    /** @brief Toggle lock */
    void toggleLock(const LayerId& id);

    /** @brief Toggle alpha lock for selected layer(s) */
    bool toggleAlphaLockForSelection();

    /** @brief Toggle expansion */
    void toggleExpanded(const LayerId& id);

    // ========================================================================
    // Selection
    // ========================================================================

    /** @brief Получить менеджер выделения (для прямого доступа из UI) */
    LayerSelectionManager* selectionManager() { return &m_selection; }
    const LayerSelectionManager* selectionManager() const { return &m_selection; }

    /** @brief Primary selected layer ID */
    LayerId selectedLayerId() const { return m_selection.primaryId(); }

    /** @brief Primary selected layer */
    LayerData* selectedLayer() const { return layerById(m_selection.primaryId()); }

    /** @brief All selected layer IDs */
    const QSet<LayerId>& selectedLayerIds() const { return m_selection.selectedIds(); }

    /** @brief Get all selected layers */
    QList<LayerData*> selectedLayers() const;

    /** @brief Number of selected layers */
    int selectionCount() const { return m_selection.count(); }

    /** @brief Is any layer selected */
    bool hasSelection() const { return m_selection.hasSelection(); }

    /** @brief Is specific layer selected */
    bool isSelected(const LayerId& id) const { return m_selection.isSelected(id); }

    /** @brief Set single selection (clears others) */
    void setSelectedLayer(const LayerId& id);

    /** @brief Add to selection (Ctrl+click style) */
    void addToSelection(const LayerId& id);

    /** @brief Remove from selection */
    void removeFromSelection(const LayerId& id);

    /** @brief Toggle selection state */
    void toggleSelection(const LayerId& id);

    /** @brief Select range (Shift+click style) */
    void selectRange(const LayerId& fromId, const LayerId& toId);

    /** @brief Select all layers */
    void selectAll();

    /** @brief Clear all selection */
    void clearSelection();

    /** @brief Select next layer in visual order */
    void selectNext();

    /** @brief Select previous layer in visual order */
    void selectPrevious();

    // ========================================================================
    // Serialization
    // ========================================================================

    /** @brief Convert layer tree to serializable entries */
    QList<ruwa::core::serialization::LayerEntry> toLayerEntries() const;

    /** @brief Convert layer effect chains to serializable entries */
    QList<ruwa::core::serialization::LayerEffectsEntry> toLayerEffectsEntries() const;

    /** @brief Load layer tree from serializable entries (replaces current state) */
    void loadFromEntries(const QList<ruwa::core::serialization::LayerEntry>& entries);

    /** @brief Apply serialized layer effect chains after loading the layer tree */
    void applyLayerEffectsEntries(
        const QList<ruwa::core::serialization::LayerEffectsEntry>& entries);

    // ========================================================================
    // Iteration Helpers
    // ========================================================================

    /** @brief Iterate all layers (depth-first) */
    void forEach(const std::function<void(LayerData*)>& callback) const;

    /** @brief Iterate with ability to stop */
    void forEachUntil(const std::function<bool(LayerData*)>& callback) const;

    /** @brief Find first layer matching predicate */
    LayerData* find(const std::function<bool(const LayerData*)>& predicate) const;

    /** @brief Find all layers matching predicate */
    QList<LayerData*> findAll(const std::function<bool(const LayerData*)>& predicate) const;

    /**
     * @brief Clone a layer tree (including children, tiles, smartTransform).
     * @param source Layer to clone
     * @param preserveIds If true, clone keeps same IDs as source (for undo snapshots)
     */
    static std::shared_ptr<LayerData> cloneLayerTree(
        const LayerData* source, bool preserveIds = false);

signals:
    // === Structure Changes ===

    /** @brief Layers added, removed, or reordered */
    void layersChanged();

    /** @brief Specific layer's data changed */
    void layerDataChanged(const LayerId& id);

    /** @brief Layer was moved */
    void layerMoved(const LayerId& id, const LayerId& oldParentId, const LayerId& newParentId);

    /** @brief Layer added */
    void layerAdded(const LayerId& id, const LayerId& parentId);

    /** @brief Layer about to be removed */
    void layerAboutToBeRemoved(const LayerId& id);

    /** @brief Layer removed */
    void layerRemoved(const LayerId& id);

    /** @brief Layer effect chain changed; revision is monotonically increasing per layer. */
    void layerEffectsChanged(const LayerId& id, quint64 revision);

    // === Selection Changes ===

    /** @brief Selection changed */
    void selectionChanged(const LayerId& primaryId);

    /** @brief Group expansion state changed */
    void groupExpansionChanged(const LayerId& id, bool expanded);

private slots:
    /** @brief Пробрасывает сигнал от менеджера выделения */
    void onSelectionManagerChanged(const LayerId& id);

private:
    // Internal helpers
    LayerData* findLayerRecursive(
        const QList<std::shared_ptr<LayerData>>& layers, const LayerId& id) const;

    std::shared_ptr<LayerData> extractLayer(const LayerId& id);
    std::shared_ptr<LayerData> extractLayerRecursive(
        QList<std::shared_ptr<LayerData>>& layers, const LayerId& id);

    bool removeLayerRecursive(QList<std::shared_ptr<LayerData>>& layers, const LayerId& id);

    void flattenRecursive(const QList<std::shared_ptr<LayerData>>& layers,
        QList<LayerData*>& result, bool respectExpansion) const;

    void forEachRecursive(const QList<std::shared_ptr<LayerData>>& layers,
        const std::function<void(LayerData*)>& callback) const;

    bool forEachUntilRecursive(const QList<std::shared_ptr<LayerData>>& layers,
        const std::function<bool(LayerData*)>& callback) const;

    int countRecursive(const QList<std::shared_ptr<LayerData>>& layers) const;

    bool isSamePosition(LayerData* layer, LayerData* newParent, int newIndex) const;
    bool refreshClippingConsistency();
    LayerId inferClipBaseForIndex(const QList<LayerData*>& flat, int layerIndex) const;
    QString nextDefaultLayerName();
    void rebuildDefaultLayerCounter();
    void markLayerEffectsChanged(LayerData* layer, bool affectsDocumentResult);

    void updateSelectionOnRemove(const LayerId& id);
    int rootIndexOf(const LayerData* layer) const;
    int clampRootInsertIndexForBackground(int index, const LayerData* movingLayer = nullptr) const;
    void inheritClippingAtInsertion(LayerData* layer, const LayerData* parent, int index) const;

    // Stamp the document tile format onto a layer's EMPTY content grids
    // (populated grids already carry their own format; masks stay RGBA8).
    void stampDocumentFormatOnGrids(LayerData* layer) const;

    // Serialization helpers
    static ruwa::core::serialization::LayerEntry layerDataToEntry(const LayerData* data);
    // Non-static: reads documentTileFormat() to stamp freshly created content grids.
    std::shared_ptr<LayerData> entryToLayerData(
        const ruwa::core::serialization::LayerEntry& entry, LayerData* parent = nullptr);

private:
    QList<std::shared_ptr<LayerData>> m_rootLayers;
    QHash<LayerId, LayerId> m_clipParentByLayer;
    int m_nextDefaultLayerNumber = 1;
    aether::TilePixelFormat m_documentTileFormat = aether::kDefaultTileFormat;

    // Selection manager - единый источник истины для выделения
    LayerSelectionManager m_selection;
};

} // namespace ruwa::core::layers

#endif // RUWA_CORE_LAYERS_LAYERMODEL_H
