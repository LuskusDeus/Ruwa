// SPDX-License-Identifier: MPL-2.0

// LayerModel.cpp
#include "LayerModel.h"
#include "features/effects/LayerEffectRegistry.h"
#include "features/effects/plugin/EffectPluginManager.h"
#include "features/layers/model/BlendModeUtils.h"
#include "features/project/ProjectData.h"
#include <cstring>

namespace ruwa::core::layers {

namespace {

ruwa::core::serialization::LayerEntry::SerializedVec2 toSerializedVec2(const aether::Vector2& value)
{
    return { value.x, value.y };
}

aether::Vector2 fromSerializedVec2(
    const ruwa::core::serialization::LayerEntry::SerializedVec2& value)
{
    return { value.x, value.y };
}

ruwa::core::serialization::LayerEntry::SerializedRect toSerializedRect(const aether::Rect& value)
{
    return { value.x, value.y, value.width, value.height };
}

aether::Rect fromSerializedRect(const ruwa::core::serialization::LayerEntry::SerializedRect& value)
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

aether::TransformState& mutableSerializedTransformForLayer(LayerData* layer)
{
    if (layer && layer->isText() && layer->textData) {
        return layer->textData->transform;
    }
    return layer->smartTransform;
}

TextAlignment textAlignmentFromValue(int value)
{
    return static_cast<TextAlignment>(qBound(0, value, static_cast<int>(TextAlignment::Justify)));
}

GroupCompositingMode groupCompositingModeFromValue(int value)
{
    return value == static_cast<int>(GroupCompositingMode::PassThrough)
        ? GroupCompositingMode::PassThrough
        : GroupCompositingMode::Isolated;
}

std::shared_ptr<LayerData> cloneLayerTreeImpl(const LayerData* source, bool preserveIds)
{
    if (!source) {
        return nullptr;
    }

    auto clone = LayerData::create(source->type, source->name);
    if (preserveIds) {
        clone->id = source->id;
    }
    clone->visible = source->visible;
    clone->locked = source->locked;
    clone->opacity = source->opacity;
    clone->blendMode = source->blendMode;
    clone->groupCompositingMode = source->groupCompositingMode;
    clone->displayColorIndex = source->displayColorIndex;
    clone->backgroundColor = source->backgroundColor;
    clone->backgroundTransparent = source->backgroundTransparent;
    clone->clippedToBelow = source->clippedToBelow;
    clone->nameIsCustom = source->nameIsCustom;
    clone->effects = source->effects;
    clone->effectChainRevision = source->effectChainRevision;
    clone->thumbnail = source->thumbnail;
    clone->thumbnailDirty = true;
    clone->expanded = source->expanded;
    clone->smartTransform = source->smartTransform;
    if (source->textData) {
        clone->textData = std::make_unique<TextLayerData>(*source->textData);
    }

    if (const auto* srcGrid = source->pixelGrid()) {
        if (auto* dstGrid = clone->pixelGrid()) {
            // A clone keeps the source's self-describing format. Stamp it BEFORE
            // creating tiles so dst buffers size correctly, and copy the exact
            // per-format byte count (not the RGBA8 TILE_BYTE_SIZE, which would
            // truncate 16F/32F content).
            dstGrid->setFormat(srcGrid->format());
            dstGrid->clear();
            const size_t copyBytes = aether::tileByteSize(srcGrid->format());
            for (const auto& [key, tile] : srcGrid->tiles()) {
                auto& dstTile = dstGrid->getOrCreateTile(key);
                std::memcpy(dstTile.pixels(), tile.pixels(), copyBytes);
                dstTile.markDirty();
            }
        }
    }

    if (const auto* srcMask = source->maskTileGrid()) {
        auto* dstMask = clone->ensureMask();
        dstMask->clear();
        dstMask->setDefaultFillPacked(srcMask->defaultFillPacked());
        for (const auto& [key, tile] : srcMask->tiles()) {
            auto& dstTile = dstMask->getOrCreateTile(key);
            if (tile.isSolid()) {
                // Preserve the uniform-color marker without allocating a buffer.
                dstTile.setSolidPacked(tile.solidColorPacked());
            } else {
                std::memcpy(dstTile.pixels(), tile.pixels(), aether::TILE_BYTE_SIZE);
            }
            dstTile.markDirty();
        }
        clone->maskEnabled = source->maskEnabled;
        clone->maskLinked = source->maskLinked;
        clone->maskEditActive = source->maskEditActive;
        clone->maskThumbnailDirty = true;
    }

    for (const auto& child : source->children) {
        if (auto childClone = cloneLayerTreeImpl(child.get(), preserveIds)) {
            clone->addChild(childClone);
        }
    }

    return clone;
}

bool containsBackgroundType(const QList<std::shared_ptr<LayerData>>& layers)
{
    for (const auto& layer : layers) {
        if (!layer) {
            continue;
        }
        if (layer->isBackground() || containsBackgroundType(layer->children)) {
            return true;
        }
    }
    return false;
}

void normalizeLoadedLayers(QList<std::shared_ptr<LayerData>>& rootLayers)
{
    const bool hasRealBackground = containsBackgroundType(rootLayers);
    if (!hasRealBackground) {
        rootLayers.append(LayerData::create(LayerType::Background, QStringLiteral("Background")));
    }

    if (hasRealBackground || rootLayers.size() < 2) {
        return;
    }

    const auto& legacyCandidate = rootLayers.at(rootLayers.size() - 2);
    if (!legacyCandidate) {
        return;
    }

    const bool isLegacyNamedBackground = legacyCandidate->name == QStringLiteral("Background")
        && legacyCandidate->type == LayerType::Smart;
    if (isLegacyNamedBackground) {
        rootLayers.removeAt(rootLayers.size() - 2);
    }
}

int effectIndexByInstanceId(
    const QList<ruwa::core::effects::LayerEffectState>& effects, const QUuid& instanceId)
{
    if (instanceId.isNull()) {
        return -1;
    }
    for (int i = 0; i < effects.size(); ++i) {
        if (effects.at(i).instanceId == instanceId) {
            return i;
        }
    }
    return -1;
}

} // namespace

LayerModel::LayerModel(QObject* parent)
    : QObject(parent)
{
    // Подключаем через слот для надёжного пробрасывания
    connect(&m_selection, &LayerSelectionManager::selectionChanged, this,
        &LayerModel::onSelectionManagerChanged);
}

std::shared_ptr<LayerData> LayerModel::cloneLayerTree(const LayerData* source, bool preserveIds)
{
    return cloneLayerTreeImpl(source, preserveIds);
}

// ============================================================================
// Layer Access
// ============================================================================

LayerData* LayerModel::layerById(const LayerId& id) const
{
    if (id.isNull())
        return nullptr;
    return findLayerRecursive(m_rootLayers, id);
}

int LayerModel::totalCount() const
{
    return countRecursive(m_rootLayers);
}

int LayerModel::visibleCount() const
{
    return flattenedLayers().size();
}

// ============================================================================
// Flattened Views
// ============================================================================

QList<LayerData*> LayerModel::flattenedLayers() const
{
    QList<LayerData*> result;
    flattenRecursive(m_rootLayers, result, true);
    return result;
}

QList<LayerData*> LayerModel::allLayersFlattened() const
{
    QList<LayerData*> result;
    flattenRecursive(m_rootLayers, result, false);
    return result;
}

int LayerModel::indexInFlattenedList(const LayerId& id) const
{
    QList<LayerData*> flattened = flattenedLayers();
    for (int i = 0; i < flattened.size(); ++i) {
        if (flattened[i]->id == id)
            return i;
    }
    return -1;
}

LayerData* LayerModel::layerAtFlatIndex(int index) const
{
    QList<LayerData*> flattened = flattenedLayers();
    if (index < 0 || index >= flattened.size())
        return nullptr;
    return flattened[index];
}

// ============================================================================
// Layer Hierarchy Information
// ============================================================================

LayerData* LayerModel::parentOf(const LayerId& id) const
{
    auto* layer = layerById(id);
    return layer ? layer->parent : nullptr;
}

QList<LayerData*> LayerModel::siblingsOf(const LayerId& id) const
{
    auto* layer = layerById(id);
    if (!layer)
        return {};
    return layer->siblings();
}

QList<LayerData*> LayerModel::ancestorsOf(const LayerId& id) const
{
    auto* layer = layerById(id);
    if (!layer)
        return {};
    return layer->ancestors();
}

QList<LayerData*> LayerModel::descendantsOf(const LayerId& id) const
{
    auto* layer = layerById(id);
    if (!layer)
        return {};

    QList<LayerData*> result;
    layer->flatten(result, false);
    return result;
}

bool LayerModel::isDescendantOf(const LayerId& descendant, const LayerId& ancestor) const
{
    auto* descLayer = layerById(descendant);
    auto* ancLayer = layerById(ancestor);
    if (!descLayer || !ancLayer)
        return false;
    return descLayer->isDescendantOf(ancLayer);
}

int LayerModel::depthOf(const LayerId& id) const
{
    auto* layer = layerById(id);
    return layer ? layer->depth : -1;
}

// ============================================================================
// Layer Addition
// ============================================================================

void LayerModel::inheritClippingAtInsertion(
    LayerData* layer, const LayerData* parent, int index) const
{
    if (!layer || layer->isBackground()) {
        return;
    }

    const auto& siblings = parent ? parent->children : m_rootLayers;
    const int insertionIndex = parent
        ? (index < 0 ? siblings.size() : qBound(0, index, siblings.size()))
        : clampRootInsertIndexForBackground(index, layer);
    if (insertionIndex <= 0 || insertionIndex > siblings.size()) {
        return;
    }

    const auto& layerAbove = siblings[insertionIndex - 1];
    layer->clippedToBelow = layerAbove && layerAbove->clippedToBelow;
}

void LayerModel::stampDocumentFormatOnGrids(LayerData* layer) const
{
    if (!layer)
        return;
    // Only stamp EMPTY content grids: a populated grid (paste / duplicate /
    // image import / deserialized) already carries its own self-describing
    // format via move or clone, and setFormat does not convert existing tiles.
    // Masks are alpha coverage and stay RGBA8 (handled by ensureMask) — never
    // touched here.
    if (layer->tileGrid && layer->tileGrid->empty()) {
        layer->tileGrid->setFormat(m_documentTileFormat);
    }
    if (layer->smartContentGrid && layer->smartContentGrid->empty()) {
        layer->smartContentGrid->setFormat(m_documentTileFormat);
    }
}

void LayerModel::addLayer(std::shared_ptr<LayerData> layer, int index)
{
    if (!layer)
        return;

    stampDocumentFormatOnGrids(layer.get());
    layer->depth = 0;
    layer->parent = nullptr;
    const int clampedIndex = clampRootInsertIndexForBackground(index, layer.get());
    m_rootLayers.insert(clampedIndex, layer);

    emit layerAdded(layer->id, LayerId());
    refreshClippingConsistency();
    emit layersChanged();
}

void LayerModel::addLayers(const QList<std::shared_ptr<LayerData>>& layers, int index)
{
    if (layers.isEmpty())
        return;

    int insertIndex = index;
    for (const auto& layer : layers) {
        if (!layer)
            continue;
        stampDocumentFormatOnGrids(layer.get());
        layer->depth = 0;
        layer->parent = nullptr;
        const int clampedIndex = clampRootInsertIndexForBackground(insertIndex, layer.get());
        m_rootLayers.insert(clampedIndex, layer);
        emit layerAdded(layer->id, LayerId());
        if (insertIndex >= 0) {
            insertIndex = clampedIndex + 1;
        }
    }

    refreshClippingConsistency();
    emit layersChanged();
}

void LayerModel::addLayerTo(std::shared_ptr<LayerData> layer, LayerData* parent, int index)
{
    if (!layer)
        return;

    if (!parent) {
        addLayer(layer, index);
        return;
    }
    if (parent->isBackground()) {
        return;
    }

    stampDocumentFormatOnGrids(layer.get());

    if (index < 0) {
        parent->addChild(layer);
    } else {
        parent->insertChild(index, layer);
    }

    emit layerAdded(layer->id, parent->id);
    refreshClippingConsistency();
    emit layersChanged();
}

void LayerModel::addLayerTo(std::shared_ptr<LayerData> layer, const LayerId& parentId, int index)
{
    addLayerTo(layer, layerById(parentId), index);
}

LayerData* LayerModel::createLayer(const QString& name, int index)
{
    auto layer = LayerData::createLayer(
        name == QStringLiteral("Layer") || name.isEmpty() ? nextDefaultLayerName() : name);
    inheritClippingAtInsertion(layer.get(), nullptr, index);
    addLayer(layer, index);
    return layer.get();
}

LayerData* LayerModel::createSmartLayer(const QString& name, int index)
{
    auto layer = LayerData::createSmart(name);
    inheritClippingAtInsertion(layer.get(), nullptr, index);
    addLayer(layer, index);
    return layer.get();
}

LayerData* LayerModel::createTextLayer(const QString& text, int index)
{
    auto layer = LayerData::createText(LayerData::standardTextLayerName(text), text);
    inheritClippingAtInsertion(layer.get(), nullptr, index);
    addLayer(layer, index);
    return layer.get();
}

LayerData* LayerModel::createGroup(const QString& name, int index)
{
    auto group = LayerData::createGroup(name);
    inheritClippingAtInsertion(group.get(), nullptr, index);
    addLayer(group, index);
    return group.get();
}

LayerData* LayerModel::createAdjustmentLayer(const QString& name, int index)
{
    auto layer = LayerData::create(LayerType::Adjustment, name);
    inheritClippingAtInsertion(layer.get(), nullptr, index);
    addLayer(layer, index);
    return layer.get();
}

LayerData* LayerModel::createLayerIn(LayerData* parent, const QString& name, int index)
{
    auto layer = LayerData::createLayer(
        name == QStringLiteral("Layer") || name.isEmpty() ? nextDefaultLayerName() : name);
    inheritClippingAtInsertion(layer.get(), parent, index);
    addLayerTo(layer, parent, index);
    return layer.get();
}

LayerData* LayerModel::createGroupIn(LayerData* parent, const QString& name, int index)
{
    auto group = LayerData::createGroup(name);
    inheritClippingAtInsertion(group.get(), parent, index);
    addLayerTo(group, parent, index);
    return group.get();
}

LayerData* LayerModel::createAdjustmentLayerIn(LayerData* parent, const QString& name, int index)
{
    auto layer = LayerData::create(LayerType::Adjustment, name);
    inheritClippingAtInsertion(layer.get(), parent, index);
    addLayerTo(layer, parent, index);
    return layer.get();
}

LayerData* LayerModel::backgroundLayer() const
{
    for (const auto& layer : m_rootLayers) {
        if (layer && layer->isBackground()) {
            return layer.get();
        }
    }
    return nullptr;
}

// ============================================================================
// Layer Removal
// ============================================================================

namespace {

// Collect layer id and all descendant ids (depth-first). LayerListView must receive
// layerAboutToBeRemoved for each before removal to avoid UAF when rows reference deleted LayerData.
void collectSubtreeIds(const LayerData* layer, QList<LayerId>& out)
{
    if (!layer || layer->id.isNull())
        return;
    for (const auto& child : layer->children) {
        if (child)
            collectSubtreeIds(child.get(), out);
    }
    out.append(layer->id);
}

} // namespace

void LayerModel::removeLayer(const LayerId& id)
{
    auto* layer = layerById(id);
    if (!layer || layer->isBackground()) {
        return;
    }

    updateSelectionOnRemove(id);

    QList<LayerId> idsToNotify;
    collectSubtreeIds(layer, idsToNotify);
    for (const LayerId& notifyId : idsToNotify) {
        emit layerAboutToBeRemoved(notifyId);
    }

    if (removeLayerRecursive(m_rootLayers, id)) {
        refreshClippingConsistency();
        emit layerRemoved(id);
        emit layersChanged();
    }
}

void LayerModel::removeLayers(const QList<LayerId>& ids)
{
    bool anyRemoved = false;

    for (const auto& id : ids) {
        auto* layer = layerById(id);
        if (!layer || layer->isBackground()) {
            continue;
        }
        updateSelectionOnRemove(id);

        QList<LayerId> idsToNotify;
        collectSubtreeIds(layer, idsToNotify);
        for (const LayerId& notifyId : idsToNotify) {
            emit layerAboutToBeRemoved(notifyId);
        }

        if (removeLayerRecursive(m_rootLayers, id)) {
            emit layerRemoved(id);
            anyRemoved = true;
        }
    }

    if (anyRemoved) {
        refreshClippingConsistency();
        emit layersChanged();
    }
}

void LayerModel::clear()
{
    if (m_rootLayers.isEmpty())
        return;

    clearSelection();

    // Notify UI before destroying layers to avoid UAF (same as removeLayers).
    QList<LayerId> idsToNotify;
    for (const auto& layer : m_rootLayers) {
        if (layer && !layer->id.isNull()) {
            collectSubtreeIds(layer.get(), idsToNotify);
        }
    }
    for (const LayerId& id : idsToNotify) {
        emit layerAboutToBeRemoved(id);
    }

    m_rootLayers.clear();
    m_clipParentByLayer.clear();
    m_nextDefaultLayerNumber = 1;

    emit layersChanged();
}

// ============================================================================
// Layer Movement
// ============================================================================

bool LayerModel::moveLayer(const LayerId& id, LayerData* newParent, int index)
{
    LayerData* layer = layerById(id);
    if (!layer) {
        return false;
    }

    // Background is always fixed at root bottom.
    if (layer->isBackground()) {
        return false;
    }
    if (newParent && newParent->isBackground()) {
        return false;
    }

    // Check if same position
    if (isSamePosition(layer, newParent, index)) {
        return false;
    }

    // Prevent moving into self or into own descendants (would create circular ref)
    if (newParent) {
        if (newParent == layer) {
            return false;
        }
        if (layer->isAncestorOf(newParent)) {
            return false;
        }
    }

    LayerId oldParentId = layer->parent ? layer->parent->id : LayerId();

    // Extract from current position
    auto sharedLayer = extractLayer(id);
    if (!sharedLayer) {
        return false;
    }

    // Insert at new position
    if (newParent) {
        newParent->insertChild(index, sharedLayer);
    } else {
        sharedLayer->parent = nullptr;
        sharedLayer->depth = 0;
        sharedLayer->updateChildrenDepth();
        const int clampedIndex = clampRootInsertIndexForBackground(index, sharedLayer.get());
        m_rootLayers.insert(clampedIndex, sharedLayer);
    }

    LayerId newParentId = newParent ? newParent->id : LayerId();
    emit layerMoved(id, oldParentId, newParentId);
    refreshClippingConsistency();
    emit layersChanged();
    // После rebuild виджеты пересоздаются из pool — принудительно уведомляем о выделении
    m_selection.notifySelectionChanged();

    return true;
}

bool LayerModel::moveLayers(const QList<LayerId>& ids, LayerData* newParent, int index)
{
    if (ids.isEmpty()) {
        return false;
    }
    if (newParent && newParent->isBackground()) {
        return false;
    }

    struct PendingMove {
        LayerId id;
        LayerId oldParentId;
        std::shared_ptr<LayerData> layer;
    };

    QList<PendingMove> pendingMoves;
    pendingMoves.reserve(ids.size());

    for (const LayerId& id : ids) {
        LayerData* layer = layerById(id);
        if (!layer || layer->isBackground()) {
            continue;
        }
        if (newParent) {
            if (newParent == layer) {
                return false;
            }
            if (layer->isAncestorOf(newParent)) {
                return false;
            }
        }
        if (isSamePosition(layer, newParent, index)) {
            continue;
        }

        PendingMove move { id, layer->parent ? layer->parent->id : LayerId(), extractLayer(id) };
        if (move.layer) {
            pendingMoves.append(std::move(move));
        }
    }

    if (pendingMoves.isEmpty()) {
        return false;
    }

    int insertIndex = index;
    for (auto& move : pendingMoves) {
        if (newParent) {
            if (insertIndex < 0) {
                newParent->addChild(move.layer);
            } else {
                newParent->insertChild(insertIndex, move.layer);
                ++insertIndex;
            }
        } else {
            move.layer->parent = nullptr;
            move.layer->depth = 0;
            move.layer->updateChildrenDepth();

            const int clampedIndex
                = clampRootInsertIndexForBackground(insertIndex, move.layer.get());
            m_rootLayers.insert(clampedIndex, move.layer);
            if (insertIndex >= 0) {
                insertIndex = clampedIndex + 1;
            }
        }
    }

    const LayerId newParentId = newParent ? newParent->id : LayerId();
    for (const auto& move : pendingMoves) {
        emit layerMoved(move.id, move.oldParentId, newParentId);
    }

    refreshClippingConsistency();
    emit layersChanged();
    m_selection.notifySelectionChanged();
    return true;
}

bool LayerModel::moveToRoot(const LayerId& id, int index)
{
    return moveLayer(id, nullptr, index);
}

bool LayerModel::moveTo(const LayerId& id, const LayerId& newParentId, int index)
{
    return moveLayer(id, layerById(newParentId), index);
}

bool LayerModel::reorder(const LayerId& id, int newIndex)
{
    LayerData* layer = layerById(id);
    if (!layer)
        return false;

    return moveLayer(id, layer->parent, newIndex);
}

bool LayerModel::moveUp(const LayerId& id)
{
    LayerData* layer = layerById(id);
    if (!layer)
        return false;
    if (layer->isBackground())
        return false;

    int currentIndex = layer->indexInParent();
    LayerId parentId = layer->parent ? layer->parent->id : LayerId();

    if (currentIndex < 0) {
        // Root layer
        for (int i = 0; i < m_rootLayers.size(); ++i) {
            if (m_rootLayers[i]->id == id) {
                if (i == 0)
                    return false;
                m_rootLayers.swapItemsAt(i, i - 1);
                emit layerMoved(id, parentId, parentId);
                refreshClippingConsistency();
                emit layersChanged();
                m_selection.notifySelectionChanged();
                return true;
            }
        }
    } else if (currentIndex > 0) {
        layer->parent->moveChild(currentIndex, currentIndex - 1);
        emit layerMoved(id, parentId, parentId);
        refreshClippingConsistency();
        emit layersChanged();
        m_selection.notifySelectionChanged();
        return true;
    }

    return false;
}

bool LayerModel::moveDown(const LayerId& id)
{
    LayerData* layer = layerById(id);
    if (!layer)
        return false;
    if (layer->isBackground())
        return false;

    int currentIndex = layer->indexInParent();
    LayerId parentId = layer->parent ? layer->parent->id : LayerId();

    if (currentIndex < 0) {
        // Root layer
        for (int i = 0; i < m_rootLayers.size(); ++i) {
            if (m_rootLayers[i]->id == id) {
                if (i >= m_rootLayers.size() - 1)
                    return false;
                if (m_rootLayers[i + 1]->isBackground())
                    return false;
                m_rootLayers.swapItemsAt(i, i + 1);
                emit layerMoved(id, parentId, parentId);
                refreshClippingConsistency();
                emit layersChanged();
                m_selection.notifySelectionChanged();
                return true;
            }
        }
    } else if (layer->parent && currentIndex < layer->parent->childCount() - 1) {
        layer->parent->moveChild(currentIndex, currentIndex + 1);
        emit layerMoved(id, parentId, parentId);
        refreshClippingConsistency();
        emit layersChanged();
        m_selection.notifySelectionChanged();
        return true;
    }

    return false;
}

// ============================================================================
// Layer Properties
// ============================================================================

void LayerModel::setLayerName(const LayerId& id, const QString& name)
{
    if (auto* layer = layerById(id)) {
        const QString clampedName = LayerData::clampedName(name);
        const bool customChanged = !layer->nameIsCustom;
        if (layer->name != clampedName || customChanged) {
            layer->name = clampedName;
            layer->nameIsCustom = true;
            emit layerDataChanged(id);
        }
    }
}

bool LayerModel::refreshTextLayerAutoName(const LayerId& id)
{
    auto* layer = layerById(id);
    if (!layer || !layer->isText() || !layer->textData || layer->nameIsCustom) {
        return false;
    }

    const QString autoName = LayerData::standardTextLayerName(layer->textData->text);
    if (layer->name == autoName) {
        return false;
    }

    layer->name = autoName;
    emit layerDataChanged(id);
    return true;
}

void LayerModel::setLayerVisible(const LayerId& id, bool visible)
{
    if (auto* layer = layerById(id)) {
        if (layer->visible != visible) {
            layer->visible = visible;
            layer->thumbnail = QPixmap();
            layer->thumbnailDirty = true;
            emit layerDataChanged(id);
        }
    }
}

void LayerModel::setLayerLocked(const LayerId& id, bool locked)
{
    if (auto* layer = layerById(id)) {
        if (layer->locked != locked) {
            layer->locked = locked;
            emit layerDataChanged(id);
        }
    }
}

void LayerModel::setLayerAlphaLock(const LayerId& id, bool alphaLock)
{
    if (auto* layer = layerById(id)) {
        if (!layer->isPixelLayer()) {
            return;
        }
        if (layer->alphaLock != alphaLock) {
            layer->alphaLock = alphaLock;
            emit layerDataChanged(id);
        }
    }
}

void LayerModel::setLayerExpanded(const LayerId& id, bool expanded)
{
    auto* layer = layerById(id);
    if (!layer)
        return;

    if (layer->expanded == expanded)
        return;

    // When collapsing, if any selected layer is inside this group,
    // move the primary selection to the group itself but keep
    // descendants selected (they'll reappear when expanded)
    if (!expanded) {
        LayerId primaryId = m_selection.primaryId();
        if (!primaryId.isNull()) {
            auto* primaryLayer = layerById(primaryId);
            if (primaryLayer && primaryLayer->isDescendantOf(layer)) {
                // Move primary to the group itself
                m_selection.setSelection(layer->id);
            }
        }
    }

    layer->expanded = expanded;
    emit groupExpansionChanged(id, expanded);
    emit layersChanged();
}

void LayerModel::setLayerOpacity(const LayerId& id, qreal opacity)
{
    if (auto* layer = layerById(id)) {
        qreal clamped = qBound(0.0, opacity, 1.0);
        if (!qFuzzyCompare(layer->opacity, clamped)) {
            layer->opacity = clamped;
            layer->thumbnail = QPixmap();
            layer->thumbnailDirty = true;
            emit layerDataChanged(id);
        }
    }
}

void LayerModel::setLayerBlendMode(const LayerId& id, BlendMode mode)
{
    if (auto* layer = layerById(id)) {
        if (layer->blendMode != mode) {
            layer->blendMode = mode;
            layer->thumbnail = QPixmap();
            layer->thumbnailDirty = true;
            emit layerDataChanged(id);
        }
    }
}

void LayerModel::setGroupCompositingMode(const LayerId& id, GroupCompositingMode mode)
{
    if (auto* layer = layerById(id)) {
        if (!layer->isGroup() || layer->groupCompositingMode == mode) {
            return;
        }
        layer->groupCompositingMode = mode;
        layer->thumbnail = QPixmap();
        layer->thumbnailDirty = true;
        emit layerDataChanged(id);
    }
}

void LayerModel::setLayerDisplayColorIndex(const LayerId& id, quint8 colorIndex)
{
    if (auto* layer = layerById(id)) {
        const quint8 clamped = static_cast<quint8>(qBound(
            0, static_cast<int>(colorIndex), static_cast<int>(LayerData::kMaxDisplayColorIndex)));
        if (layer->displayColorIndex != clamped) {
            layer->displayColorIndex = clamped;
            emit layerDataChanged(id);
        }
    }
}

void LayerModel::setLayerBackgroundColor(const LayerId& id, const QColor& color)
{
    if (auto* layer = layerById(id)) {
        if (!layer->isBackground()) {
            return;
        }
        if (layer->backgroundColor != color) {
            layer->backgroundColor = color;
            layer->thumbnail = QPixmap();
            layer->thumbnailDirty = true;
            emit layerDataChanged(id);
        }
    }
}

void LayerModel::setLayerBackgroundTransparent(const LayerId& id, bool transparent)
{
    if (auto* layer = layerById(id)) {
        if (!layer->isBackground()) {
            return;
        }
        if (layer->backgroundTransparent != transparent) {
            layer->backgroundTransparent = transparent;
            layer->thumbnail = QPixmap();
            layer->thumbnailDirty = true;
            emit layerDataChanged(id);
        }
    }
}

void LayerModel::setLayerClippedToBelow(const LayerId& id, bool clipped)
{
    if (auto* layer = layerById(id)) {
        if (layer->isBackground()) {
            return;
        }
        LayerId baseId;
        if (clipped) {
            const QList<LayerData*> flat = allLayersFlattened();
            const int layerIdx = flat.indexOf(layer);
            if (layerIdx >= 0) {
                baseId = inferClipBaseForIndex(flat, layerIdx);
            }
            if (baseId.isNull()) {
                clipped = false;
            }
        }
        if (layer->clippedToBelow != clipped) {
            layer->clippedToBelow = clipped;
            if (clipped) {
                m_clipParentByLayer[layer->id] = baseId;
            } else {
                m_clipParentByLayer.remove(layer->id);
            }
            layer->thumbnail = QPixmap();
            layer->thumbnailDirty = true;
            emit layerDataChanged(id);
        }
    }
}

QUuid LayerModel::addLayerEffect(const LayerId& layerId, const QString& typeId, int index,
    const std::optional<QSize>& canvasSize)
{
    auto* layer = layerById(layerId);
    if (!layer || typeId.isEmpty()) {
        return {};
    }

    auto state = effects::LayerEffectRegistry::instance().createState(typeId, canvasSize);
    const int insertIndex
        = index < 0 ? layer->effects.size() : qBound(0, index, layer->effects.size());
    layer->effects.insert(insertIndex, state);
    markLayerEffectsChanged(layer, true);
    return state.instanceId;
}

bool LayerModel::removeLayerEffect(const LayerId& layerId, const QUuid& effectInstanceId)
{
    auto* layer = layerById(layerId);
    if (!layer) {
        return false;
    }

    const int index = effectIndexByInstanceId(layer->effects, effectInstanceId);
    if (index < 0) {
        return false;
    }

    layer->effects.removeAt(index);
    markLayerEffectsChanged(layer, true);
    return true;
}

bool LayerModel::moveLayerEffect(
    const LayerId& layerId, const QUuid& effectInstanceId, int newIndex)
{
    auto* layer = layerById(layerId);
    if (!layer || layer->effects.isEmpty()) {
        return false;
    }

    const int oldIndex = effectIndexByInstanceId(layer->effects, effectInstanceId);
    const int clampedIndex = qBound(0, newIndex, layer->effects.size() - 1);
    if (oldIndex < 0 || oldIndex == clampedIndex) {
        return false;
    }

    const auto state = layer->effects.takeAt(oldIndex);
    layer->effects.insert(clampedIndex, state);
    markLayerEffectsChanged(layer, true);
    return true;
}

bool LayerModel::setLayerEffectEnabled(
    const LayerId& layerId, const QUuid& effectInstanceId, bool enabled)
{
    auto* layer = layerById(layerId);
    if (!layer) {
        return false;
    }

    const int index = effectIndexByInstanceId(layer->effects, effectInstanceId);
    if (index < 0 || layer->effects[index].enabled == enabled) {
        return false;
    }

    layer->effects[index].enabled = enabled;
    markLayerEffectsChanged(layer, true);
    return true;
}

bool LayerModel::setLayerEffectRealtimePreviewEnabled(
    const LayerId& layerId, const QUuid& effectInstanceId, bool enabled)
{
    auto* layer = layerById(layerId);
    if (!layer) {
        return false;
    }

    const int index = effectIndexByInstanceId(layer->effects, effectInstanceId);
    if (index < 0 || layer->effects[index].realtimePreviewEnabled == enabled) {
        return false;
    }

    layer->effects[index].realtimePreviewEnabled = enabled;
    markLayerEffectsChanged(layer, false);
    return true;
}

bool LayerModel::setLayerEffectParam(const LayerId& layerId, const QUuid& effectInstanceId,
    const QString& key, const QVariant& value)
{
    auto* layer = layerById(layerId);
    if (!layer || key.isEmpty()) {
        return false;
    }

    const int index = effectIndexByInstanceId(layer->effects, effectInstanceId);
    if (index < 0 || layer->effects[index].params.value(key) == value) {
        return false;
    }

    layer->effects[index].params.insert(key, value);
    markLayerEffectsChanged(layer, true);
    return true;
}

void LayerModel::beginLayerEffectParamEdit(
    const LayerId& layerId, const QUuid& effectInstanceId, const QString& paramKey)
{
    auto* layer = layerById(layerId);
    if (!layer || effectIndexByInstanceId(layer->effects, effectInstanceId) < 0) {
        return;
    }
    layer->liveEditedEffectId = effectInstanceId;
    layer->liveEditedEffectParamKey = paramKey;
    ++layer->liveEffectEditGeneration;
}

void LayerModel::endLayerEffectParamEdit(const LayerId& layerId, const QUuid& effectInstanceId)
{
    auto* layer = layerById(layerId);
    if (layer && layer->liveEditedEffectId == effectInstanceId) {
        layer->liveEditedEffectId = QUuid();
        layer->liveEditedEffectParamKey.clear();
    }
}

bool LayerModel::replaceLayerEffectState(
    const LayerId& layerId, const QUuid& effectInstanceId, const effects::LayerEffectState& state)
{
    auto* layer = layerById(layerId);
    if (!layer) {
        return false;
    }

    const int index = effectIndexByInstanceId(layer->effects, effectInstanceId);
    if (index < 0) {
        return false;
    }

    auto normalized = effects::LayerEffectRegistry::instance().normalizeState(state);
    normalized.instanceId = effectInstanceId;
    if (layer->effects[index] == normalized) {
        return false;
    }

    const bool affectsDocument = layer->effects[index].enabled != normalized.enabled
        || layer->effects[index].typeId != normalized.typeId
        || layer->effects[index].version != normalized.version
        || layer->effects[index].params != normalized.params;
    layer->effects[index] = normalized;
    markLayerEffectsChanged(layer, affectsDocument);
    return true;
}

bool LayerModel::replaceLayerEffects(const LayerId& layerId,
    const QList<effects::LayerEffectState>& effects, bool affectsDocumentResult)
{
    auto* layer = layerById(layerId);
    if (!layer) {
        return false;
    }

    QList<effects::LayerEffectState> normalized;
    normalized.reserve(effects.size());
    for (const auto& effect : effects) {
        normalized.append(effects::LayerEffectRegistry::instance().normalizeState(effect));
    }

    if (layer->effects == normalized) {
        return false;
    }

    layer->effects = std::move(normalized);
    markLayerEffectsChanged(layer, affectsDocumentResult);
    return true;
}

void LayerModel::notifyLayerDataChanged(const LayerId& id)
{
    if (auto* layer = layerById(id)) {
        layer->thumbnail = QPixmap();
        layer->thumbnailDirty = true;
        emit layerDataChanged(id);
    }
}

void LayerModel::markLayerEffectsChanged(LayerData* layer, bool affectsDocumentResult)
{
    if (!layer) {
        return;
    }

    ++layer->effectChainRevision;
    if (affectsDocumentResult) {
        layer->thumbnail = QPixmap();
        layer->thumbnailDirty = true;
        emit layerDataChanged(layer->id);
    }
    emit layerEffectsChanged(layer->id, layer->effectChainRevision);
}

void LayerModel::notifyBulkLayerContentChanged()
{
    forEach([&](LayerData* layer) {
        if (!layer || layer->isGroup()) {
            return;
        }

        layer->thumbnail = QPixmap();
        layer->thumbnailDirty = true;
    });

    emit layersChanged();
}

bool LayerModel::applyClippingToSelection(const LayerId& baseLayerId)
{
    auto* baseLayer = layerById(baseLayerId);
    if (!baseLayer || baseLayer->isBackground()) {
        return false;
    }

    const auto& selected = m_selection.selectedIds();
    if (selected.isEmpty()) {
        return false;
    }

    QList<LayerData*> layersToToggle;
    for (const LayerId& id : selected) {
        if (id == baseLayerId) {
            continue;
        }
        auto* layer = layerById(id);
        if (!layer || layer->isBackground()) {
            continue;
        }
        if (layer->parent != baseLayer->parent) {
            continue;
        }
        layersToToggle.append(layer);
    }

    if (layersToToggle.isEmpty()) {
        return false;
    }

    // Toggle behavior:
    // - if all selected target layers are already clipped -> unclip all
    // - otherwise -> clip all
    bool allClipped = true;
    for (auto* layer : layersToToggle) {
        if (!layer->clippedToBelow) {
            allClipped = false;
            break;
        }
    }
    const bool targetClipped = !allClipped;

    bool anyChanged = false;
    for (auto* layer : layersToToggle) {
        if (layer->clippedToBelow == targetClipped) {
            if (targetClipped) {
                m_clipParentByLayer[layer->id] = baseLayerId;
            } else {
                m_clipParentByLayer.remove(layer->id);
            }
            continue;
        }
        layer->clippedToBelow = targetClipped;
        if (targetClipped) {
            m_clipParentByLayer[layer->id] = baseLayerId;
        } else {
            m_clipParentByLayer.remove(layer->id);
        }
        emit layerDataChanged(layer->id);
        anyChanged = true;
    }
    return anyChanged;
}

bool LayerModel::applyQuickClippingMaskToSelection()
{
    const auto& selected = m_selection.selectedIds();
    if (selected.isEmpty()) {
        return false;
    }

    const QList<LayerData*> flat = allLayersFlattened();
    if (flat.isEmpty()) {
        return false;
    }

    int topSelectedIndex = -1;
    for (int i = 0; i < flat.size(); ++i) {
        const auto* layer = flat[i];
        if (layer && selected.contains(layer->id) && !layer->isBackground()) {
            topSelectedIndex = i;
            break;
        }
    }

    if (topSelectedIndex < 0) {
        return false;
    }

    const LayerData* topSelectedLayer = flat[topSelectedIndex];
    if (!topSelectedLayer) {
        return false;
    }

    LayerId baseLayerId;
    for (int i = topSelectedIndex + 1; i < flat.size(); ++i) {
        const auto* candidate = flat[i];
        if (!candidate || candidate->isBackground()) {
            continue;
        }
        if (candidate->parent != topSelectedLayer->parent) {
            continue;
        }
        if (selected.contains(candidate->id)) {
            continue;
        }
        if (!candidate->clippedToBelow) {
            baseLayerId = candidate->id;
            break;
        }
    }

    if (baseLayerId.isNull()) {
        return false;
    }

    return applyClippingToSelection(baseLayerId);
}

bool LayerModel::hasQuickClippingMaskTargetBelow(const LayerId& layerId) const
{
    auto* layer = layerById(layerId);
    if (!layer || layer->isBackground()) {
        return false;
    }

    // Must match applyQuickClippingMaskToSelection() for a single-layer selection: same flat list
    // and the same base scan (allLayersFlattened includes collapsed children; quick clip uses it).
    const QList<LayerData*> flat = allLayersFlattened();
    if (flat.isEmpty()) {
        return false;
    }

    int topSelectedIndex = -1;
    for (int i = 0; i < flat.size(); ++i) {
        const auto* l = flat[i];
        if (l && l->id == layerId && !l->isBackground()) {
            topSelectedIndex = i;
            break;
        }
    }
    if (topSelectedIndex < 0) {
        return false;
    }

    const LayerData* topSelectedLayer = flat[topSelectedIndex];
    if (!topSelectedLayer) {
        return false;
    }

    QSet<LayerId> selected;
    selected.insert(layerId);

    LayerId baseLayerId;
    for (int i = topSelectedIndex + 1; i < flat.size(); ++i) {
        const auto* candidate = flat[i];
        if (!candidate || candidate->isBackground()) {
            continue;
        }
        if (candidate->parent != topSelectedLayer->parent) {
            continue;
        }
        if (selected.contains(candidate->id)) {
            continue;
        }
        if (!candidate->clippedToBelow) {
            baseLayerId = candidate->id;
            break;
        }
    }

    return !baseLayerId.isNull();
}

bool LayerModel::clipLayerToBelow(const LayerId& layerId)
{
    auto* layer = layerById(layerId);
    if (!layer || layer->isBackground() || layer->clippedToBelow) {
        return false;
    }

    const QList<LayerData*> flat = flattenedLayers();
    int layerIndex = -1;
    for (int i = 0; i < flat.size(); ++i) {
        if (flat[i] && flat[i]->id == layerId) {
            layerIndex = i;
            break;
        }
    }
    if (layerIndex < 0 || layerIndex >= flat.size() - 1) {
        return false;
    }

    const LayerId baseId = inferClipBaseForIndex(flat, layerIndex);
    if (baseId.isNull()) {
        return false;
    }

    layer->clippedToBelow = true;
    m_clipParentByLayer[layer->id] = baseId;
    emit layerDataChanged(layer->id);
    return true;
}

bool LayerModel::unclipLayerFromBelow(const LayerId& layerId)
{
    auto* layer = layerById(layerId);
    if (!layer || layer->isBackground() || !layer->clippedToBelow) {
        return false;
    }

    layer->clippedToBelow = false;
    m_clipParentByLayer.remove(layer->id);
    emit layerDataChanged(layer->id);
    return true;
}

QList<LayerId> LayerModel::duplicateSelectedLayers()
{
    QList<LayerId> addedIds;
    const auto& selected = m_selection.selectedIds();
    if (selected.isEmpty()) {
        return addedIds;
    }

    const QList<LayerData*> flat = allLayersFlattened();
    if (flat.isEmpty()) {
        return addedIds;
    }

    QList<LayerData*> duplicateRoots;
    duplicateRoots.reserve(selected.size());

    for (LayerData* layer : flat) {
        if (!layer || !selected.contains(layer->id) || layer->isBackground()) {
            continue;
        }

        bool hasSelectedAncestor = false;
        for (LayerData* anc = layer->parent; anc; anc = anc->parent) {
            if (selected.contains(anc->id)) {
                hasSelectedAncestor = true;
                break;
            }
        }

        if (!hasSelectedAncestor) {
            duplicateRoots.append(layer);
        }
    }

    if (duplicateRoots.isEmpty()) {
        return addedIds;
    }

    QSet<LayerId> duplicatedSelection;
    for (LayerData* srcRoot : duplicateRoots) {
        auto clone = cloneLayerTreeImpl(srcRoot, false);
        if (!clone) {
            continue;
        }

        clone->name = LayerData::copiedName(srcRoot->name);
        clone->nameIsCustom = true;

        const int sourceIndex = srcRoot->parent ? srcRoot->indexInParent() : rootIndexOf(srcRoot);
        if (sourceIndex < 0) {
            continue;
        }

        if (srcRoot->parent) {
            // In this list model, smaller index is visually above.
            // So duplicate must be inserted at source index (not +1).
            srcRoot->parent->insertChild(sourceIndex, clone);
        } else {
            const int insertIndex = clampRootInsertIndexForBackground(sourceIndex, clone.get());
            m_rootLayers.insert(insertIndex, clone);
        }

        duplicatedSelection.insert(clone->id);
        addedIds.append(clone->id);
    }

    if (duplicatedSelection.isEmpty()) {
        return addedIds;
    }

    refreshClippingConsistency();
    emit layersChanged();

    LayerId primaryDuplicate;
    const QList<LayerData*> newFlat = allLayersFlattened();
    for (const LayerData* layer : newFlat) {
        if (layer && duplicatedSelection.contains(layer->id)) {
            primaryDuplicate = layer->id;
            break;
        }
    }
    if (primaryDuplicate.isNull()) {
        primaryDuplicate = *duplicatedSelection.begin();
    }

    m_selection.setSelection(primaryDuplicate);
    for (const LayerId& id : duplicatedSelection) {
        if (id != primaryDuplicate) {
            m_selection.addToSelection(id);
        }
    }

    return addedIds;
}

bool LayerModel::toggleVisibilityForSelection()
{
    const auto& selected = m_selection.selectedIds();
    if (selected.isEmpty()) {
        return false;
    }

    QList<LayerData*> layers;
    layers.reserve(selected.size());
    for (const LayerId& id : selected) {
        auto* layer = layerById(id);
        if (!layer || layer->isBackground()) {
            continue;
        }
        layers.append(layer);
    }

    if (layers.isEmpty()) {
        return false;
    }

    bool allVisible = true;
    for (const LayerData* layer : layers) {
        if (!layer->visible) {
            allVisible = false;
            break;
        }
    }

    const bool targetVisible = !allVisible;
    bool anyChanged = false;
    for (LayerData* layer : layers) {
        if (layer->visible == targetVisible) {
            continue;
        }
        layer->visible = targetVisible;
        emit layerDataChanged(layer->id);
        anyChanged = true;
    }

    return anyChanged;
}

void LayerModel::toggleVisibility(const LayerId& id)
{
    if (auto* layer = layerById(id)) {
        setLayerVisible(id, !layer->visible);
    }
}

void LayerModel::toggleLock(const LayerId& id)
{
    if (auto* layer = layerById(id)) {
        setLayerLocked(id, !layer->locked);
    }
}

bool LayerModel::toggleAlphaLockForSelection()
{
    const auto& selected = m_selection.selectedIds();
    if (selected.isEmpty()) {
        return false;
    }

    // Determine target state from primary selected layer
    auto* primary = selectedLayer();
    if (!primary || !primary->isPixelLayer()) {
        return false;
    }

    const bool newState = !primary->alphaLock;
    bool anyChanged = false;

    for (const LayerId& id : selected) {
        auto* layer = layerById(id);
        if (layer && layer->isPixelLayer() && layer->alphaLock != newState) {
            layer->alphaLock = newState;
            emit layerDataChanged(id);
            anyChanged = true;
        }
    }

    return anyChanged;
}

void LayerModel::toggleExpanded(const LayerId& id)
{
    if (auto* layer = layerById(id)) {
        setLayerExpanded(id, !layer->expanded);
    }
}

// ============================================================================
// Selection
// ============================================================================

QList<LayerData*> LayerModel::selectedLayers() const
{
    QList<LayerData*> result;
    for (const auto& id : m_selection.selectedIds()) {
        if (auto* layer = layerById(id)) {
            result.append(layer);
        }
    }
    return result;
}

void LayerModel::setSelectedLayer(const LayerId& id)
{
    m_selection.setSelection(id);
}

void LayerModel::addToSelection(const LayerId& id)
{
    auto* layer = layerById(id);
    if (!layer)
        return;

    // Background cannot be multi-selected with others.
    if (layer->isBackground()) {
        m_selection.setSelection(id);
        return;
    }

    if (auto* bg = backgroundLayer(); bg && m_selection.isSelected(bg->id)) {
        m_selection.removeFromSelection(bg->id);
    }
    m_selection.addToSelection(id);
}

void LayerModel::removeFromSelection(const LayerId& id)
{
    m_selection.removeFromSelection(id);
}

void LayerModel::toggleSelection(const LayerId& id)
{
    auto* layer = layerById(id);
    if (!layer)
        return;

    // Background cannot be part of multi-selection set.
    if (layer->isBackground()) {
        m_selection.setSelection(id);
        return;
    }

    if (auto* bg = backgroundLayer(); bg && m_selection.isSelected(bg->id)) {
        m_selection.removeFromSelection(bg->id);
    }
    m_selection.toggleSelection(id);
}

void LayerModel::selectRange(const LayerId& fromId, const LayerId& toId)
{
    auto* toLayer = layerById(toId);
    if (toLayer && toLayer->isBackground()) {
        m_selection.setSelection(toId);
        return;
    }

    m_selection.selectRange(fromId, toId, [this]() { return flattenedLayers(); });

    // Background should never stay selected together with other layers.
    if (auto* bg = backgroundLayer();
        bg && m_selection.isSelected(bg->id) && m_selection.count() > 1) {
        m_selection.removeFromSelection(bg->id);
    }
}

void LayerModel::selectAll()
{
    m_selection.selectAll([this](const std::function<void(LayerData*)>& callback) {
        forEach([&callback](LayerData* layer) {
            if (!layer->isBackground()) {
                callback(layer);
            }
        });
    });
}

void LayerModel::clearSelection()
{
    m_selection.clearSelection();
}

void LayerModel::selectNext()
{
    m_selection.selectNext([this]() { return flattenedLayers(); });
}

void LayerModel::selectPrevious()
{
    m_selection.selectPrevious([this]() { return flattenedLayers(); });
}

// ============================================================================
// Private Slots
// ============================================================================

void LayerModel::onSelectionManagerChanged(const LayerId& id)
{
    emit selectionChanged(id);
}

// ============================================================================
// Iteration Helpers
// ============================================================================

void LayerModel::forEach(const std::function<void(LayerData*)>& callback) const
{
    forEachRecursive(m_rootLayers, callback);
}

void LayerModel::forEachUntil(const std::function<bool(LayerData*)>& callback) const
{
    forEachUntilRecursive(m_rootLayers, callback);
}

LayerData* LayerModel::find(const std::function<bool(const LayerData*)>& predicate) const
{
    LayerData* result = nullptr;

    forEachUntil([&](LayerData* layer) {
        if (predicate(layer)) {
            result = layer;
            return false; // Stop iteration
        }
        return true; // Continue
    });

    return result;
}

QList<LayerData*> LayerModel::findAll(const std::function<bool(const LayerData*)>& predicate) const
{
    QList<LayerData*> result;

    forEach([&](LayerData* layer) {
        if (predicate(layer)) {
            result.append(layer);
        }
    });

    return result;
}

// ============================================================================
// Private Helpers
// ============================================================================

LayerData* LayerModel::findLayerRecursive(
    const QList<std::shared_ptr<LayerData>>& layers, const LayerId& id) const
{
    for (const auto& layer : layers) {
        if (layer->id == id)
            return layer.get();
        if (auto* found = findLayerRecursive(layer->children, id)) {
            return found;
        }
    }
    return nullptr;
}

std::shared_ptr<LayerData> LayerModel::extractLayer(const LayerId& id)
{
    return extractLayerRecursive(m_rootLayers, id);
}

std::shared_ptr<LayerData> LayerModel::extractLayerRecursive(
    QList<std::shared_ptr<LayerData>>& layers, const LayerId& id)
{
    for (int i = 0; i < layers.size(); ++i) {
        if (layers[i]->id == id) {
            auto layer = layers[i];
            layer->parent = nullptr;
            layers.removeAt(i);
            return layer;
        }
        if (auto found = extractLayerRecursive(layers[i]->children, id)) {
            return found;
        }
    }
    return nullptr;
}

bool LayerModel::removeLayerRecursive(QList<std::shared_ptr<LayerData>>& layers, const LayerId& id)
{
    for (int i = 0; i < layers.size(); ++i) {
        if (layers[i]->id == id) {
            layers.removeAt(i);
            return true;
        }
        if (removeLayerRecursive(layers[i]->children, id)) {
            return true;
        }
    }
    return false;
}

void LayerModel::flattenRecursive(const QList<std::shared_ptr<LayerData>>& layers,
    QList<LayerData*>& result, bool respectExpansion) const
{
    for (const auto& layer : layers) {
        result.append(layer.get());
        if (!respectExpansion || layer->expanded) {
            flattenRecursive(layer->children, result, respectExpansion);
        }
    }
}

void LayerModel::forEachRecursive(const QList<std::shared_ptr<LayerData>>& layers,
    const std::function<void(LayerData*)>& callback) const
{
    for (const auto& layer : layers) {
        callback(layer.get());
        forEachRecursive(layer->children, callback);
    }
}

bool LayerModel::forEachUntilRecursive(const QList<std::shared_ptr<LayerData>>& layers,
    const std::function<bool(LayerData*)>& callback) const
{
    for (const auto& layer : layers) {
        if (!callback(layer.get()))
            return false;
        if (!forEachUntilRecursive(layer->children, callback))
            return false;
    }
    return true;
}

int LayerModel::countRecursive(const QList<std::shared_ptr<LayerData>>& layers) const
{
    int count = 0;
    for (const auto& layer : layers) {
        count += 1 + countRecursive(layer->children);
    }
    return count;
}

bool LayerModel::isSamePosition(LayerData* layer, LayerData* newParent, int newIndex) const
{
    if (!layer)
        return true;

    LayerData* currentParent = layer->parent;

    if (currentParent != newParent) {
        return false;
    }

    int currentIndex = -1;

    if (currentParent) {
        currentIndex = layer->indexInParent();
    } else {
        for (int i = 0; i < m_rootLayers.size(); ++i) {
            if (m_rootLayers[i].get() == layer) {
                currentIndex = i;
                break;
            }
        }
    }

    // Same index means inserting back to the same position after extraction
    return (newIndex == currentIndex);
}

QString LayerModel::nextDefaultLayerName()
{
    const int number = m_nextDefaultLayerNumber++;
    return QStringLiteral("Layer %1").arg(number);
}

void LayerModel::rebuildDefaultLayerCounter()
{
    int maxNumber = 0;
    forEach([&maxNumber](LayerData* layer) {
        if (!layer) {
            return;
        }
        const QString prefix = QStringLiteral("Layer ");
        if (!layer->name.startsWith(prefix)) {
            return;
        }

        bool ok = false;
        const int number = layer->name.mid(prefix.size()).toInt(&ok);
        if (ok) {
            maxNumber = qMax(maxNumber, number);
        }
    });
    m_nextDefaultLayerNumber = maxNumber + 1;
}

LayerId LayerModel::inferClipBaseForIndex(const QList<LayerData*>& flat, int layerIndex) const
{
    if (layerIndex < 0 || layerIndex >= flat.size()) {
        return LayerId();
    }

    LayerData* layer = flat[layerIndex];
    if (!layer) {
        return LayerId();
    }

    for (int i = layerIndex + 1; i < flat.size(); ++i) {
        LayerData* below = flat[i];
        if (!below || below->isBackground()) {
            continue;
        }
        if (below->parent != layer->parent) {
            continue;
        }
        if (!below->clippedToBelow) {
            return below->id;
        }
    }

    return LayerId();
}

bool LayerModel::refreshClippingConsistency()
{
    bool anyChanged = false;
    const QList<LayerData*> flat = allLayersFlattened();
    if (flat.isEmpty()) {
        m_clipParentByLayer.clear();
        return false;
    }

    QHash<LayerId, int> indexById;
    indexById.reserve(flat.size());
    for (int i = 0; i < flat.size(); ++i) {
        if (flat[i]) {
            indexById.insert(flat[i]->id, i);
        }
    }

    for (auto it = m_clipParentByLayer.begin(); it != m_clipParentByLayer.end();) {
        if (!indexById.contains(it.key()) || !indexById.contains(it.value())) {
            it = m_clipParentByLayer.erase(it);
        } else {
            ++it;
        }
    }

    for (int i = 0; i < flat.size(); ++i) {
        LayerData* layer = flat[i];
        if (!layer || layer->isBackground()) {
            continue;
        }

        if (!layer->clippedToBelow) {
            m_clipParentByLayer.remove(layer->id);
            continue;
        }

        LayerId parentId = m_clipParentByLayer.value(layer->id);
        if (parentId.isNull()) {
            parentId = inferClipBaseForIndex(flat, i);
            if (!parentId.isNull()) {
                m_clipParentByLayer.insert(layer->id, parentId);
            }
        }

        bool valid = !parentId.isNull() && indexById.contains(parentId);
        int parentIndex = valid ? indexById.value(parentId) : -1;
        if (valid && parentIndex <= i) {
            valid = false;
        }
        if (valid) {
            LayerData* parentLayer = flat[parentIndex];
            if (!parentLayer || parentLayer->parent != layer->parent) {
                valid = false;
            }
        }

        // A clipped layer remains valid while all layers between it and its parent
        // are clipped too (their internal order is free).
        if (valid) {
            for (int j = i + 1; j < parentIndex; ++j) {
                LayerData* between = flat[j];
                if (!between) {
                    continue;
                }
                if (!between->clippedToBelow) {
                    valid = false;
                    break;
                }
            }
        }

        if (!valid) {
            layer->clippedToBelow = false;
            m_clipParentByLayer.remove(layer->id);
            emit layerDataChanged(layer->id);
            anyChanged = true;
        }
    }

    return anyChanged;
}

void LayerModel::updateSelectionOnRemove(const LayerId& id)
{
    // Remember the flattened position of the removed layer for fallback selection
    int removedIndex = -1;
    QList<LayerData*> flatBefore = flattenedLayers();
    for (int i = 0; i < flatBefore.size(); ++i) {
        if (flatBefore[i]->id == id) {
            removedIndex = i;
            break;
        }
    }

    // Collect descendant IDs
    QList<LayerId> descendantIds;
    auto* layer = layerById(id);
    if (layer) {
        QList<LayerData*> descendants;
        layer->flatten(descendants, false);
        for (auto* desc : descendants) {
            descendantIds.append(desc->id);
        }
    }

    // Remove from selection manager
    m_selection.onLayerRemoved(id, descendantIds);

    // If selection is now empty, auto-select nearest layer
    if (!m_selection.hasSelection() && removedIndex >= 0) {
        // Build list of remaining candidates (exclude the one being removed + descendants)
        QSet<LayerId> removedIds;
        removedIds.insert(id);
        for (const auto& dId : descendantIds)
            removedIds.insert(dId);

        // Try the layer below, then above
        for (int i = removedIndex + 1; i < flatBefore.size(); ++i) {
            if (!removedIds.contains(flatBefore[i]->id)) {
                m_selection.setSelection(flatBefore[i]->id);
                return;
            }
        }
        for (int i = removedIndex - 1; i >= 0; --i) {
            if (!removedIds.contains(flatBefore[i]->id)) {
                m_selection.setSelection(flatBefore[i]->id);
                return;
            }
        }
    }
}

// ============================================================================
// Serialization
// ============================================================================

using ruwa::core::serialization::LayerEntry;

LayerEntry LayerModel::layerDataToEntry(const LayerData* data)
{
    LayerEntry entry;
    entry.id = data->id;
    entry.name = data->name;
    entry.nameIsCustom = data->nameIsCustom;
    entry.type = static_cast<int>(data->type);
    entry.visible = data->visible;
    entry.locked = data->locked;
    entry.expanded = data->expanded;
    entry.opacity = data->opacity;
    entry.blendMode = static_cast<int>(data->blendMode);
    entry.groupCompositingMode = static_cast<int>(data->groupCompositingMode);
    entry.displayColorIndex = data->displayColorIndex;
    entry.backgroundColorRgba = data->backgroundColor.rgba();
    entry.backgroundTransparent = data->backgroundTransparent;
    entry.clippedToBelow = data->clippedToBelow;
    const auto& transform = serializedTransformForLayer(data);
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
    if (data->isText() && data->textData) {
        entry.hasTextPayload = true;
        entry.text = data->textData->text;
        entry.textFontFamily = data->textData->fontFamily;
        entry.textFontSize = data->textData->fontSize;
        entry.textColorRgba = data->textData->color.rgba();
        entry.textAlignment = static_cast<int>(data->textData->alignment);
        entry.textLineHeight = data->textData->lineHeight;
        entry.textStyleRuns.reserve(data->textData->styleRuns.size());
        for (const auto& run : data->textData->styleRuns) {
            entry.textStyleRuns.append({ run.start, run.length, run.fontFamily, run.fontSize,
                run.color.rgba(), run.bold, run.italic, run.underline });
        }
    }

    for (const auto& child : data->children) {
        entry.children.append(layerDataToEntry(child.get()));
    }

    return entry;
}

std::shared_ptr<LayerData> LayerModel::entryToLayerData(const LayerEntry& entry, LayerData* parent)
{
    auto data = std::make_shared<LayerData>();
    data->id = entry.id.isNull() ? QUuid::createUuid() : entry.id;
    data->name = LayerData::clampedName(entry.name);
    data->nameIsCustom = entry.nameIsCustom;
    // Legacy: type 3 was LayerType::Text, now removed; load as Smart to preserve pixel data
    const int type = (entry.type == 3) ? static_cast<int>(LayerType::Smart) : entry.type;
    data->type = static_cast<LayerType>(type);
    data->visible = entry.visible;
    data->locked = entry.locked;
    data->expanded = entry.expanded;
    data->opacity = entry.opacity;
    data->blendMode = blendModeFromValue(entry.blendMode);
    data->groupCompositingMode = groupCompositingModeFromValue(entry.groupCompositingMode);
    data->displayColorIndex
        = static_cast<quint8>(qBound(0, static_cast<int>(entry.displayColorIndex),
            static_cast<int>(LayerData::kMaxDisplayColorIndex)));
    data->backgroundColor = QColor::fromRgba(entry.backgroundColorRgba);
    data->backgroundTransparent = entry.backgroundTransparent;
    data->clippedToBelow = entry.clippedToBelow;
    if (data->isText()) {
        data->textData = std::make_unique<TextLayerData>();
        if (entry.hasTextPayload) {
            data->textData->text = entry.text;
            data->textData->fontFamily = entry.textFontFamily;
            data->textData->fontSize = entry.textFontSize;
            data->textData->color = QColor::fromRgba(entry.textColorRgba);
            data->textData->alignment = textAlignmentFromValue(entry.textAlignment);
            data->textData->lineHeight = entry.textLineHeight;
            data->textData->styleRuns.reserve(entry.textStyleRuns.size());
            for (const auto& run : entry.textStyleRuns) {
                data->textData->styleRuns.append(
                    { run.start, run.length, run.fontFamily, run.fontSize,
                        QColor::fromRgba(run.colorRgba), run.bold, run.italic, run.underline });
            }
        }
    }
    if (data->isText() && data->textData && !data->nameIsCustom) {
        const QString standardName = LayerData::standardTextLayerName(data->textData->text);
        data->nameIsCustom = data->name != QStringLiteral("Text") && data->name != standardName;
    }
    auto& transform = mutableSerializedTransformForLayer(data.get());
    transform.contentBounds = fromSerializedRect(entry.contentBounds);
    transform.translation = fromSerializedVec2(entry.translation);
    transform.rotation = entry.rotation;
    transform.scale = fromSerializedVec2(entry.scale);
    transform.pivot = fromSerializedVec2(entry.pivot);
    if (entry.hasFreeCorners) {
        std::array<aether::Vector2, 4> corners {};
        for (int i = 0; i < 4; ++i) {
            corners[static_cast<size_t>(i)]
                = fromSerializedVec2(entry.freeCorners[static_cast<size_t>(i)]);
        }
        transform.freeCorners = corners;
    }
    if (entry.hasDeformMesh) {
        aether::TransformState::DeformMesh mesh;
        mesh.rows = entry.deformLatticeRows;
        mesh.cols = entry.deformLatticeCols;
        mesh.vertices.reserve(static_cast<size_t>(entry.deformVertices.size()));
        for (const auto& vertex : entry.deformVertices) {
            mesh.vertices.push_back(
                { fromSerializedVec2(vertex.source), fromSerializedVec2(vertex.target) });
        }
        transform.deformMesh = std::move(mesh);
    }
    data->parent = parent;
    data->depth = parent ? parent->depth + 1 : 0;

    // Create tile storage for paintable raster layers. Stamp the document format
    // BEFORE the async content-tile restore fills tiles (documentTileFormat is set
    // from the file's contentTileFormat prior to loadFromEntries).
    if (data->isRaster()) {
        data->tileGrid = std::make_unique<aether::TileGrid>();
        data->tileGrid->setFormat(documentTileFormat());
    } else if (data->isIsolatedPixelLayer()) {
        data->smartContentGrid = std::make_unique<aether::TileGrid>();
        data->smartContentGrid->setFormat(documentTileFormat());
    }

    // Restore the layer mask (painted grayscale). Masks are typically small, so
    // their tiles are restored synchronously here rather than via the async
    // pixel-tile path.
    if (entry.hasMask) {
        auto* maskGrid = data->ensureMask();
        data->maskEnabled = entry.maskEnabled;
        data->maskLinked = entry.maskLinked;
        data->maskEditActive = false;
        maskGrid->setDefaultFillPacked(entry.maskDefaultFill);
        for (const auto& tile : entry.maskTiles) {
            aether::TileData& dst = maskGrid->getOrCreateTile(aether::TileKey { tile.x, tile.y });
            if (tile.solid) {
                dst.setSolidPacked(tile.solidColor);
            } else {
                if (tile.pixels.size() != static_cast<int>(aether::TILE_BYTE_SIZE)) {
                    maskGrid->removeTile(aether::TileKey { tile.x, tile.y });
                    continue;
                }
                std::memcpy(dst.pixels(), tile.pixels.constData(), aether::TILE_BYTE_SIZE);
            }
            dst.markDirty();
        }
    }

    for (const auto& childEntry : entry.children) {
        auto child = entryToLayerData(childEntry, data.get());
        data->children.append(child);
    }

    return data;
}

int LayerModel::rootIndexOf(const LayerData* layer) const
{
    if (!layer)
        return -1;
    for (int i = 0; i < m_rootLayers.size(); ++i) {
        if (m_rootLayers[i].get() == layer) {
            return i;
        }
    }
    return -1;
}

int LayerModel::clampRootInsertIndexForBackground(int index, const LayerData* movingLayer) const
{
    const LayerData* bg = backgroundLayer();
    const int size = m_rootLayers.size();
    int target = index;
    if (target < 0 || target > size) {
        target = size;
    }

    if (!bg) {
        return target;
    }

    const int bgIndex = rootIndexOf(bg);
    if (bgIndex < 0) {
        return target;
    }

    const bool movingBackground = movingLayer && movingLayer->isBackground();
    if (movingBackground) {
        return size;
    }

    // Never insert below background: max allowed root index is background position.
    return qMin(target, bgIndex);
}

QList<LayerEntry> LayerModel::toLayerEntries() const
{
    QList<LayerEntry> entries;
    entries.reserve(m_rootLayers.size());

    for (const auto& root : m_rootLayers) {
        entries.append(layerDataToEntry(root.get()));
    }

    return entries;
}

QList<ruwa::core::serialization::LayerEffectsEntry> LayerModel::toLayerEffectsEntries() const
{
    QList<ruwa::core::serialization::LayerEffectsEntry> entries;
    forEach([&entries](LayerData* layer) {
        if (!layer || layer->effects.isEmpty()) {
            return;
        }
        entries.append({ layer->id, layer->effects });
    });
    return entries;
}

void LayerModel::loadFromEntries(const QList<LayerEntry>& entries)
{
    // Clear everything
    clearSelection();
    m_rootLayers.clear();
    m_clipParentByLayer.clear();

    // Build layer tree from entries
    m_rootLayers.reserve(entries.size());
    for (const auto& entry : entries) {
        m_rootLayers.append(entryToLayerData(entry, nullptr));
    }

    normalizeLoadedLayers(m_rootLayers);
    rebuildDefaultLayerCounter();

    // Select first layer if available
    if (!m_rootLayers.isEmpty()) {
        // Find first non-group leaf, or just use first layer
        LayerData* first = nullptr;
        forEachUntil([&first](LayerData* d) {
            first = d;
            return true; // stop at first
        });
        if (first) {
            m_selection.setSelection(first->id);
        }
    }

    refreshClippingConsistency();
    emit layersChanged();
}

void LayerModel::applyLayerEffectsEntries(
    const QList<ruwa::core::serialization::LayerEffectsEntry>& entries)
{
    bool anyChanged = false;
    for (const auto& entry : entries) {
        auto* layer = layerById(entry.layerId);
        if (!layer) {
            continue;
        }

        QList<effects::LayerEffectState> normalized;
        normalized.reserve(entry.effects.size());
        for (const auto& effect : entry.effects) {
            // Apply any plugin schema migrations on the stored version BEFORE
            // normalizeState() adopts the current version and fills defaults.
            // No-op for built-in effects and effects already at their version.
            effects::LayerEffectState migrated = effect;
            effects::plugin::EffectPluginManager::instance().migrateState(migrated);
            normalized.append(effects::LayerEffectRegistry::instance().normalizeState(migrated));
        }

        if (layer->effects == normalized) {
            continue;
        }

        layer->effects = std::move(normalized);
        ++layer->effectChainRevision;
        layer->thumbnail = QPixmap();
        layer->thumbnailDirty = true;
        emit layerEffectsChanged(layer->id, layer->effectChainRevision);
        anyChanged = true;
    }

    if (anyChanged) {
        emit layersChanged();
    }
}

} // namespace ruwa::core::layers
