// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   S M A R T   C O N T E N T   C O M P O S I T O R
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_RENDERING_SMARTCONTENTCOMPOSITOR_H
#define RUWA_FEATURES_CANVAS_RENDERING_SMARTCONTENTCOMPOSITOR_H

#include <QSet>
#include <QUuid>

namespace ruwa::core::layers {
struct SmartContent;
}

namespace aether {

class GLRenderer;
class LayerCompositingBuilder;

/**
 * @brief Flattens a smart object's nested document into the composited grid its
 *        instances are drawn from.
 *
 * A document-backed `SmartContent` keeps its layers in `SmartContent::document`
 * and a composite of them in `SmartContent::grid`. Everything downstream — the
 * projection into the parent document, effects, masks, thumbnails, the file
 * format — reads that grid, so the ONLY thing that has to happen when the nested
 * document changes is this: recomposite, and stamp the document revision the
 * pixels now correspond to.
 *
 * The composite runs through the ordinary GPU compositor
 * (`GLCompositor::compositeStackIntoGrid`), so a nested document blends,
 * clips, masks and applies effects exactly like the document that hosts it —
 * there is no second, divergent compositing implementation.
 *
 * The result is CLIPPED to the nested document's canvas: that canvas is the
 * smart object's frame, and it is what the contents tab shows. Only the cached
 * pixels are cut — the nested layers keep whatever they painted outside it.
 *
 * Requires a current GL context; it is the caller's job to have made one
 * current (and not to call this from inside a frame's compositing pass — the
 * composite runs as its own batch). When the renderer is not up yet, nothing is
 * composited and the content keeps its last valid pixels; the caller retries.
 */
class SmartContentCompositor {
public:
    SmartContentCompositor(GLRenderer* renderer, const LayerCompositingBuilder* stackBuilder);

    /**
     * @brief Recomposite @p content if its cache no longer matches its document.
     * @return true when the pixels were replaced — the caller then rebuilds the
     *         projections of every layer showing this content.
     *
     * A flat content (no document) and an already-current cache both answer
     * false without touching the GPU, so this is cheap to call defensively.
     */
    bool ensureComposite(ruwa::core::layers::SmartContent& content);

    /// How deep smart-inside-smart nesting may go before the composite refuses
    /// to descend further. A content that appears twice on one path is a cycle
    /// and is cut regardless of depth; this bound catches the merely absurd.
    static constexpr int kMaxNestingDepth = 8;

private:
    bool ensureCompositeRecursive(
        ruwa::core::layers::SmartContent& content, int depth, QSet<QUuid>& activePath);

    GLRenderer* m_renderer = nullptr;
    const LayerCompositingBuilder* m_stackBuilder = nullptr;
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_RENDERING_SMARTCONTENTCOMPOSITOR_H
