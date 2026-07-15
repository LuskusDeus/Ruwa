// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_RENDERING_LAYERSCREENSOURCECACHE_H
#define RUWA_FEATURES_CANVAS_RENDERING_LAYERSCREENSOURCECACHE_H

#include "features/canvas/rendering/GLCompositor.h"
#include "features/effects/LayerEffectTypes.h"

#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLContext>
#include <QUuid>

#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace aether {

class GLRenderer;
class Viewport;

class LayerScreenSourceCache {
public:
    enum class SourceKind {
        LayerColor,
        AlphaMask,
        // Unclipped layer color used as a board viewport-preview source.
        // Kept distinct from LayerColor so it never reuses the canvas-clipped
        // LayerColor entry, while still being cached across drag frames
        // (LayerColor for boards used to bypass the cache via a null id, which
        // re-rendered the whole board source every frame).
        BoardPreviewColor,
        // The layer's mask grid rendered raw (premultiplied grayscale) to screen
        // space at the standard viewport. Used to gate a viewport-preview target
        // texture (transform / lasso-fill) by the fixed-position layer mask:
        // the reveal = lum(rgb) + (1 - a) is computed by the composite shader's
        // uClipMaskLuminanceReveal branch. Distinct kind so it never collides with
        // the layer's color entry and is cached across drag frames.
        LayerMask
    };

    explicit LayerScreenSourceCache(QOpenGLFunctions_4_5_Core* gl);
    ~LayerScreenSourceCache();

    LayerScreenSourceCache(const LayerScreenSourceCache&) = delete;
    LayerScreenSourceCache& operator=(const LayerScreenSourceCache&) = delete;

    GLuint acquireLayerTexture(const CompositeLayerInfo& layer, GLRenderer& renderer,
        const Viewport& viewport, uint32_t canvasWidth, uint32_t canvasHeight, bool flipH,
        bool flipV, uint64_t viewportRevision, SourceKind kind = SourceKind::LayerColor,
        ruwa::core::effects::LayerSourcePurpose sourcePurpose
        = ruwa::core::effects::LayerSourcePurpose::FinalLayerColor,
        uint64_t previewOverrideRevision = 0);

    void invalidateByLayer(const QUuid& layerId);
    void invalidateByViewport();
    void clear();

private:
    struct QUuidHash {
        size_t operator()(const QUuid& value) const noexcept
        {
            return static_cast<size_t>(qHash(value));
        }
    };

    struct Entry {
        GLuint fbo = 0;
        GLuint texture = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t lastAccessGeneration = 0;
    };

    struct LayerRevisionState {
        uint64_t revision = 0;
        uint64_t lastAccessGeneration = 0;
    };

    struct Key {
        QUuid layerId;
        uint64_t layerRevision = 0;
        uint64_t effectChainRevision = 0;
        uint64_t previewOverrideRevision = 0;
        uint64_t viewportRevision = 0;
        int kind = 0;
        int sourcePurpose = 0;

        bool operator==(const Key& other) const
        {
            return layerId == other.layerId && layerRevision == other.layerRevision
                && effectChainRevision == other.effectChainRevision
                && previewOverrideRevision == other.previewOverrideRevision
                && viewportRevision == other.viewportRevision && kind == other.kind
                && sourcePurpose == other.sourcePurpose;
        }
    };

    struct KeyHash {
        size_t operator()(const Key& key) const noexcept;
    };

    static void collectCompositeLayerKeys(const std::vector<CompositeLayerInfo>& layers,
        std::unordered_set<TileKey, TileKeyHash>& outKeys);

    // Returns true if the underlying texture was (re)created (size changed or first allocation),
    // signaling that the entry's pixel content is empty and must be re-rendered.
    bool ensureEntrySize(Entry& entry, uint32_t width, uint32_t height);
    void renderDirect(const CompositeLayerInfo& layer, Entry& entry, GLRenderer& renderer,
        const Viewport& viewport, uint32_t canvasWidth, uint32_t canvasHeight, bool flipH,
        bool flipV, SourceKind kind);
    void renderFallback(const CompositeLayerInfo& layer, Entry& entry, GLRenderer& renderer,
        const Viewport& viewport, uint32_t canvasWidth, uint32_t canvasHeight, bool flipH,
        bool flipV, SourceKind kind, ruwa::core::effects::LayerSourcePurpose sourcePurpose);
    GLuint renderUncachedLayer(const CompositeLayerInfo& layer, GLRenderer& renderer,
        const Viewport& viewport, uint32_t canvasWidth, uint32_t canvasHeight, bool flipH,
        bool flipV, SourceKind kind, ruwa::core::effects::LayerSourcePurpose sourcePurpose);
    bool canDeleteNow() const;
    void deferDestroyEntry(Entry&& entry);
    void destroyOnNextFlush(Entry&& entry);
    void flushPendingDeletes();
    void destroyEntry(Entry& entry);
    uint64_t nextAccessGeneration();
    void pruneStaleState(uint64_t currentGeneration);
    uint64_t maxIdleGenerations() const;

private:
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    std::unordered_map<Key, Entry, KeyHash> m_entries;
    std::unordered_map<QUuid, LayerRevisionState, QUuidHash> m_layerRevisions;
    std::vector<Entry> m_pendingDeletes;
    uint64_t m_accessGeneration = 0;
    uint64_t m_viewportInvalidationSerial = 1;
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_RENDERING_LAYERSCREENSOURCECACHE_H
