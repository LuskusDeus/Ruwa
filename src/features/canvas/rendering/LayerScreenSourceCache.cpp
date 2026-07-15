// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/rendering/LayerScreenSourceCache.h"

#include "features/canvas/rendering/GLRenderer.h"
#include "features/canvas/scene/Canvas.h"
#include "shared/rendering/GLTextureFactory.h"
#include "shared/rendering/GLStateGuard.h"

#include <algorithm>

namespace aether {

namespace {

size_t hashCombine(size_t seed, size_t value)
{
    return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
}

} // namespace

LayerScreenSourceCache::LayerScreenSourceCache(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

LayerScreenSourceCache::~LayerScreenSourceCache()
{
    clear();
}

GLuint LayerScreenSourceCache::acquireLayerTexture(const CompositeLayerInfo& layer,
    GLRenderer& renderer, const Viewport& viewport, uint32_t canvasWidth, uint32_t canvasHeight,
    bool flipH, bool flipV, uint64_t viewportRevision, SourceKind kind,
    ruwa::core::effects::LayerSourcePurpose sourcePurpose, uint64_t previewOverrideRevision)
{
    flushPendingDeletes();
    const uint64_t accessGeneration = nextAccessGeneration();
    pruneStaleState(accessGeneration);

    if (layer.id.isNull()) {
        // Synthetic preview layers do not participate in the invalidateByLayer lifecycle.
        return renderUncachedLayer(layer, renderer, viewport, canvasWidth, canvasHeight, flipH,
            flipV, kind, sourcePurpose);
    }

    LayerRevisionState& revisionState = m_layerRevisions[layer.id];
    revisionState.lastAccessGeneration = accessGeneration;
    const uint64_t layerRevision = revisionState.revision;
    const Key key { layer.id, layerRevision, layer.effectChainRevision, previewOverrideRevision,
        viewportRevision, static_cast<int>(kind), static_cast<int>(sourcePurpose) };
    auto [it, inserted] = m_entries.try_emplace(key);
    Entry& entry = it->second;
    entry.lastAccessGeneration = accessGeneration;

    const uint32_t vpWidth = viewport.width();
    const uint32_t vpHeight = viewport.height();
    const bool textureRecreated = ensureEntrySize(entry, vpWidth, vpHeight);
    if (!entry.texture || !entry.fbo) {
        return 0;
    }

    // The cache key does not include viewport size, so an existing entry whose
    // texture was just (re)allocated due to a size change has empty content and
    // must be re-rendered. Otherwise an entry resized for transform-preview
    // overscan growth would return a blank texture and the preview would crop
    // along viewport edges as the camera moves.
    if (!inserted && !textureRecreated) {
        return entry.texture;
    }

    const bool directRenderable
        = layer.tileGrid && !layer.isGroup && !layer.transform && !layer.retainedPayload;
    if (directRenderable || layer.hasSolidColor) {
        renderDirect(
            layer, entry, renderer, viewport, canvasWidth, canvasHeight, flipH, flipV, kind);
    } else {
        renderFallback(layer, entry, renderer, viewport, canvasWidth, canvasHeight, flipH, flipV,
            kind, sourcePurpose);
    }

    return entry.texture;
}

void LayerScreenSourceCache::invalidateByLayer(const QUuid& layerId)
{
    if (layerId.isNull()) {
        return;
    }

    const uint64_t accessGeneration = nextAccessGeneration();
    LayerRevisionState& revisionState = m_layerRevisions[layerId];
    ++revisionState.revision;
    revisionState.lastAccessGeneration = accessGeneration;
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (it->first.layerId == layerId) {
            deferDestroyEntry(std::move(it->second));
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }
    pruneStaleState(accessGeneration);
}

void LayerScreenSourceCache::invalidateByViewport()
{
    ++m_viewportInvalidationSerial;
    clear();
    pruneStaleState(nextAccessGeneration());
}

void LayerScreenSourceCache::clear()
{
    for (auto& [key, entry] : m_entries) {
        Q_UNUSED(key);
        deferDestroyEntry(std::move(entry));
    }
    m_entries.clear();
    flushPendingDeletes();
}

size_t LayerScreenSourceCache::KeyHash::operator()(const Key& key) const noexcept
{
    size_t seed = qHash(key.layerId);
    seed = hashCombine(seed, static_cast<size_t>(key.layerRevision));
    seed = hashCombine(seed, static_cast<size_t>(key.effectChainRevision));
    seed = hashCombine(seed, static_cast<size_t>(key.previewOverrideRevision));
    seed = hashCombine(seed, static_cast<size_t>(key.viewportRevision));
    seed = hashCombine(seed, static_cast<size_t>(key.kind));
    seed = hashCombine(seed, static_cast<size_t>(key.sourcePurpose));
    return seed;
}

void LayerScreenSourceCache::collectCompositeLayerKeys(
    const std::vector<CompositeLayerInfo>& layers,
    std::unordered_set<TileKey, TileKeyHash>& outKeys)
{
    for (const auto& layer : layers) {
        if (!layer.visible) {
            continue;
        }
        if (layer.isGroup) {
            collectCompositeLayerKeys(layer.children, outKeys);
            continue;
        }
        if (layer.tileGrid) {
            for (const auto& [key, tile] : layer.tileGrid->tiles()) {
                Q_UNUSED(tile);
                outKeys.insert(key);
            }
        }
        if (layer.retainedPayload && !layer.retainedPayload->empty()) {
            const auto retainedKeys = retainedCoverageTileKeys(layer.retainedPayload->worldBounds);
            outKeys.insert(retainedKeys.begin(), retainedKeys.end());
        }
    }
}

bool LayerScreenSourceCache::ensureEntrySize(Entry& entry, uint32_t width, uint32_t height)
{
    if (entry.texture && entry.width == width && entry.height == height) {
        return false;
    }

    destroyEntry(entry);
    if (width == 0 || height == 0) {
        return false;
    }

    m_gl->glGenFramebuffers(1, &entry.fbo);
    entry.texture = createTexture2D(m_gl, width, height, { GL_LINEAR, GL_LINEAR });
    if (!entry.fbo || !entry.texture) {
        destroyEntry(entry);
        return false;
    }

    entry.width = width;
    entry.height = height;
    return true;
}

void LayerScreenSourceCache::renderDirect(const CompositeLayerInfo& layer, Entry& entry,
    GLRenderer& renderer, const Viewport& viewport, uint32_t canvasWidth, uint32_t canvasHeight,
    bool flipH, bool flipV, SourceKind kind)
{
    GLFboViewportGuard guard(m_gl);

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, entry.fbo);
    m_gl->glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, entry.texture, 0);
    m_gl->glViewport(0, 0, static_cast<GLsizei>(entry.width), static_cast<GLsizei>(entry.height));
    renderer.beginFrame(entry.width, entry.height);
    // For a mask source, clear to the grid's default-fill background so the whole
    // screen (including the infinite area beyond any painted tile) carries the
    // mask's background reveal — a hide-all mask's black background then gates the
    // previewed content everywhere it has no painted mask tile, instead of the
    // transparent (= fully revealed) default that let content show through.
    if (kind == SourceKind::LayerMask && layer.tileGrid) {
        uint8_t r = 0, g = 0, b = 0, a = 0;
        layer.tileGrid->defaultFill(r, g, b, a);
        m_gl->glClearColor(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    } else {
        m_gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    }
    m_gl->glClear(GL_COLOR_BUFFER_BIT);

    if (layer.hasSolidColor) {
        if (kind == SourceKind::LayerColor && canvasWidth > 0 && canvasHeight > 0) {
            Canvas canvas(canvasWidth, canvasHeight);
            renderer.drawCanvas(
                canvas, viewport, layer.solidColor, layer.solidColor, 1.0f, 0.0f, flipH, flipV);
        } else {
            renderer.drawBackground(layer.solidColor);
        }
    } else if (layer.tileGrid) {
        TileGrid* mutableGrid = const_cast<TileGrid*>(layer.tileGrid);
        renderer.uploadDirtyTiles(*mutableGrid);
        const bool clipToCanvas = kind == SourceKind::LayerColor;
        renderer.drawTiles(*mutableGrid, viewport, canvasWidth, canvasHeight, 0.0f, flipH, flipV,
            false, Color::transparent(), clipToCanvas);
    }

    renderer.endFrame();
}

void LayerScreenSourceCache::renderFallback(const CompositeLayerInfo& layer, Entry& entry,
    GLRenderer& renderer, const Viewport& viewport, uint32_t canvasWidth, uint32_t canvasHeight,
    bool flipH, bool flipV, SourceKind kind, ruwa::core::effects::LayerSourcePurpose sourcePurpose)
{
    CompositeLayerInfo sourceLayer = layer;
    const bool rawSource = sourcePurpose == ruwa::core::effects::LayerSourcePurpose::RawContent
        || sourcePurpose == ruwa::core::effects::LayerSourcePurpose::BoardRawContent;
    if (rawSource) {
        // The viewport compositor owns the effect chain and final layer-mask blend.
        // Fallback sources (notably retained content) must therefore be materialised
        // raw just like renderDirect sources, rather than baking either operation in.
        sourceLayer.effects.clear();
        sourceLayer.externalClipMaskGrid = nullptr;
        sourceLayer.clipMaskLuminanceReveal = false;
        sourceLayer.clipMaskGrid2 = nullptr;
        sourceLayer.clipMaskAlphaOnly = false;
        sourceLayer.clipMaskAsAlphaCap = false;
        sourceLayer.clipMaskEditPreview = false;
        sourceLayer.clipMaskEditReplace = false;
        sourceLayer.clipMaskReplaceFallback = false;
        sourceLayer.subtractClipRevealFromSrc = false;
        sourceLayer.useRadialReveal = false;
        sourceLayer.opacity = 1.0f;
        sourceLayer.blendMode = 0;
        sourceLayer.clippedToBelow = false;
        sourceLayer.preserveBaseAlpha = false;
        sourceLayer.replaceBase = false;
    }

    CompositionCache temporaryCache;
    std::unordered_set<TileKey, TileKeyHash> dirtyKeys;
    collectCompositeLayerKeys({ sourceLayer }, dirtyKeys);
    if (!dirtyKeys.empty()) {
        temporaryCache.markDirty(dirtyKeys);
        // compositeAllDirty routes non-direct sources through the per-tile renderer.
        // Raw viewport sources have already had their top-level effects and mask
        // removed above so GLViewportCompositor can apply both exactly once.
        renderer.compositeAllDirty({ sourceLayer }, temporaryCache);
    }

    CompositeLayerInfo fallbackLayer = layer;
    fallbackLayer.tileGrid = &temporaryCache.grid();
    fallbackLayer.children.clear();
    fallbackLayer.isGroup = false;
    fallbackLayer.transform = nullptr;
    fallbackLayer.transformRenderer = nullptr;
    fallbackLayer.retainedPayload = nullptr;
    fallbackLayer.hasSolidColor = false;
    // The temporary grid contains every operation retained by sourceLayer. Drop
    // metadata that renderDirect does not consume from this flattened result.
    fallbackLayer.externalClipMaskGrid = nullptr;
    fallbackLayer.clipMaskLuminanceReveal = false;
    fallbackLayer.clipMaskGrid2 = nullptr;

    renderDirect(
        fallbackLayer, entry, renderer, viewport, canvasWidth, canvasHeight, flipH, flipV, kind);
}

GLuint LayerScreenSourceCache::renderUncachedLayer(const CompositeLayerInfo& layer,
    GLRenderer& renderer, const Viewport& viewport, uint32_t canvasWidth, uint32_t canvasHeight,
    bool flipH, bool flipV, SourceKind kind, ruwa::core::effects::LayerSourcePurpose sourcePurpose)
{
    Entry entry;
    ensureEntrySize(entry, viewport.width(), viewport.height());
    if (!entry.texture || !entry.fbo) {
        destroyEntry(entry);
        return 0;
    }

    const bool directRenderable
        = layer.tileGrid && !layer.isGroup && !layer.transform && !layer.retainedPayload;
    if (directRenderable || layer.hasSolidColor) {
        renderDirect(
            layer, entry, renderer, viewport, canvasWidth, canvasHeight, flipH, flipV, kind);
    } else {
        renderFallback(layer, entry, renderer, viewport, canvasWidth, canvasHeight, flipH, flipV,
            kind, sourcePurpose);
    }

    const GLuint texture = entry.texture;
    destroyOnNextFlush(std::move(entry));
    return texture;
}

bool LayerScreenSourceCache::canDeleteNow() const
{
    return QOpenGLContext::currentContext() != nullptr;
}

void LayerScreenSourceCache::deferDestroyEntry(Entry&& entry)
{
    if (!entry.texture && !entry.fbo) {
        return;
    }

    if (canDeleteNow()) {
        destroyEntry(entry);
        return;
    }

    m_pendingDeletes.push_back(std::move(entry));
}

void LayerScreenSourceCache::destroyOnNextFlush(Entry&& entry)
{
    if (!entry.texture && !entry.fbo) {
        return;
    }

    m_pendingDeletes.push_back(std::move(entry));
}

void LayerScreenSourceCache::flushPendingDeletes()
{
    if (!canDeleteNow() || m_pendingDeletes.empty()) {
        return;
    }

    for (Entry& entry : m_pendingDeletes) {
        destroyEntry(entry);
    }
    m_pendingDeletes.clear();
}

void LayerScreenSourceCache::destroyEntry(Entry& entry)
{
    if (entry.texture) {
        m_gl->glDeleteTextures(1, &entry.texture);
        entry.texture = 0;
    }
    if (entry.fbo) {
        m_gl->glDeleteFramebuffers(1, &entry.fbo);
        entry.fbo = 0;
    }
    entry.width = 0;
    entry.height = 0;
}

uint64_t LayerScreenSourceCache::nextAccessGeneration()
{
    return ++m_accessGeneration;
}

void LayerScreenSourceCache::pruneStaleState(uint64_t currentGeneration)
{
    const uint64_t idleThreshold = maxIdleGenerations();

    for (auto it = m_entries.begin(); it != m_entries.end();) {
        const Entry& entry = it->second;
        if (currentGeneration > entry.lastAccessGeneration
            && currentGeneration - entry.lastAccessGeneration > idleThreshold) {
            deferDestroyEntry(std::move(it->second));
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = m_layerRevisions.begin(); it != m_layerRevisions.end();) {
        const LayerRevisionState& state = it->second;
        if (currentGeneration > state.lastAccessGeneration
            && currentGeneration - state.lastAccessGeneration > idleThreshold) {
            it = m_layerRevisions.erase(it);
        } else {
            ++it;
        }
    }
}

uint64_t LayerScreenSourceCache::maxIdleGenerations() const
{
    constexpr uint64_t kMaxIdleFrames = 3;
    // One compositing pass typically touches each live cached entry once.
    const uint64_t liveEntrySpan = static_cast<uint64_t>(std::max<size_t>(1, m_entries.size()));
    return kMaxIdleFrames * liveEntrySpan;
}

} // namespace aether
