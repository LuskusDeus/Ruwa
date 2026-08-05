// SPDX-License-Identifier: MPL-2.0

// ============================================================================
//   R U W A   |   C O R E   |   S M A R T   C O N T E N T
// ============================================================================

#ifndef RUWA_CORE_LAYERS_SMARTCONTENT_H
#define RUWA_CORE_LAYERS_SMARTCONTENT_H

#include <QByteArray>
#include <QSize>
#include <QString>
#include <QUuid>
#include <QtGlobal>
#include <memory>

#include "shared/tiles/TileGrid.h"
#include "features/transform/TransformState.h"

namespace ruwa::core::layers {

/**
 * The layer stack living inside a smart object (stage 2). Deliberately left
 * INCOMPLETE here: it holds `LayerData`, which holds a `SmartContent`, so
 * including it would close a cycle. Everything that has to look inside a
 * document lives in SmartContent.cpp.
 */
struct SmartDocument;

/**
 * @brief Where a smart layer's content came from.
 *
 * Embedded content lives entirely inside the document. LinkedFile content is
 * mirrored from a file on disk (sourcePath / sourceHash describe it) and can be
 * relinked or refreshed. Only Embedded is produced today; the linked path lands
 * with "Replace Contents" / "Relink to File".
 */
enum class SmartSourceKind : quint8 { Embedded = 0, LinkedFile = 1 };

/**
 * @brief The payload of a smart object: its pixels plus the identity of those
 *        pixels, independent of any layer that shows them.
 *
 * A smart layer owns a `shared_ptr<SmartContent>` rather than a grid, so several
 * layers can later become *instances* of one content while each keeps its own
 * `LayerData::smartTransform`. Everything that identifies the content (its id,
 * its source, its revision) belongs here; everything that describes how one
 * layer *places* it stays on the layer.
 *
 * The content grid is in CONTENT space (the space the pixels were authored in),
 * not document space. Projection into document space is the compositor's job
 * (`OpenGLCanvasWidget::buildSmartProjectedGrid`).
 *
 * A content is either FLAT or DOCUMENT-BACKED, and `document` is the flag:
 *  - `document == nullptr` — `grid` IS the content (everything before stage 2,
 *    every imported image, every rasterize round trip).
 *  - `document != nullptr` — the nested layer stack is the content and `grid` is
 *    a CACHE of it composited down, valid only while `compositeRevision ==
 *    document->revision`. Consumers keep reading `grid`; whoever is about to read
 *    it is responsible for recompositing a stale cache first.
 *
 * Whichever it is, `grid` is never null: a file must open, and a layer must
 * draw, without anyone having to composite anything first.
 *
 * Not copyable: content is shared by pointer. Use cloneDetached() when a real
 * duplicate (a new contentId, a private grid) is wanted.
 */
struct SmartContent {
    /// Identity of the pixels. Two layers with the same contentId show the same
    /// content and must stay in sync; the file format writes such content once.
    QUuid contentId = QUuid::createUuid();

    /// Premultiplied pixels in content space. Never null for a live content.
    /// The content itself while `document` is null, its composited cache once a
    /// document exists (see the class docs).
    std::unique_ptr<aether::TileGrid> grid;

    /// The nested layer stack, or null for a flat content. Shared by every
    /// instance of this content, exactly like the pixels.
    std::shared_ptr<SmartDocument> document;

    /// The `document->revision` that `grid` was composited from. Meaningless
    /// while `document` is null.
    quint64 compositeRevision = 0;

    /// Bumped whenever the content is REPLACED wholesale (import, relink,
    /// rasterize-and-restore). Pixel-level edits are tracked by the grid's own
    /// contentVersion(); contentStamp() folds both into one invalidation key.
    quint64 contentRevision = 0;

    // ---- Source (linked content; unused while sourceKind == Embedded) ----
    QString sourcePath;
    SmartSourceKind sourceKind = SmartSourceKind::Embedded;
    QByteArray sourceHash;

    SmartContent()
        : grid(std::make_unique<aether::TileGrid>())
    {
    }

    explicit SmartContent(std::unique_ptr<aether::TileGrid> contentGrid)
        : grid(contentGrid ? std::move(contentGrid) : std::make_unique<aether::TileGrid>())
    {
    }

    SmartContent(const SmartContent&) = delete;
    SmartContent& operator=(const SmartContent&) = delete;

    // Out of line only because `document` is incomplete here.
    ~SmartContent();

    /**
     * @brief Replace the pixels wholesale and invalidate every derived cache.
     *
     * Passing null installs an empty grid rather than leaving the content
     * grid-less, so `grid` is always dereferenceable.
     *
     * These pixels ARE the content afterwards, so a nested document is dropped:
     * "replace contents" replaces the layers too, and leaving them behind would
     * mean the very next composite silently undid the replacement. Undo restores
     * both halves — `SmartContentSwapCommand` parks the document alongside the
     * grid. A caller that means "the composite finished" wants
     * `setCompositeGrid()` instead; one that means "here are new contents to
     * edit" wants `setDocument()`.
     */
    void setGrid(std::unique_ptr<aether::TileGrid> newGrid);

    /**
     * @brief Signal that the pixels changed outside the grid's own mutation
     *        entry points, or that the whole grid was swapped.
     *
     * Bumps contentRevision, which every consumer keyed on contentStamp() sees.
     */
    void markContentChanged()
    {
        ++contentRevision;
        m_boundsValid = false;
    }

    /**
     * @brief The invalidation key for anything derived from this content.
     *
     * Folds the wholesale-replacement counter with the grid's own per-mutation
     * contentVersion() and tile count, so a cache that stores this value can
     * never be revalidated against content that has since changed — including
     * across a grid swap, where contentVersion() would otherwise restart low.
     *
     * A document-backed content folds in `document->revision` as well, so an edit
     * inside the nested stack invalidates the parent's caches the moment it
     * happens — before anything has recomposited. The grid's own contribution
     * stays in the mix on purpose: it is what moves again when the composite
     * lands, so a cache built from the STALE pixels in between cannot survive it.
     */
    quint64 contentStamp() const
    {
        constexpr quint64 kMix = 0x9E3779B97F4A7C15ull;
        quint64 stamp = contentRevision * kMix;
        stamp = mixIn(stamp, grid ? static_cast<quint64>(grid->contentVersion()) : 0ull);
        stamp = mixIn(stamp, grid ? static_cast<quint64>(grid->tileCount()) : 0ull);
        stamp = mixIn(stamp, documentRevision());
        return stamp;
    }

    // ---- Nested document (stage 2) -----------------------------------------

    bool hasDocument() const { return document != nullptr; }

    /// `document->revision`, or 0 for a flat content. Out of line: the document
    /// is incomplete here.
    quint64 documentRevision() const;

    /// Does `grid` still describe the document? Always true for a flat content —
    /// there is nothing to composite.
    bool compositeUpToDate() const;

    /**
     * @brief Install a nested document. The pixels are left alone and marked as
     *        belonging to @p compositeRevisionForCurrentGrid.
     *
     * Pass the document's current revision when the grid ALREADY shows it (the
     * loader restoring a saved composite, `wrapGridInDocument()` below); pass
     * nothing to declare the cache stale, which forces the first read to
     * composite.
     */
    void setDocument(std::shared_ptr<SmartDocument> newDocument,
        quint64 compositeRevisionForCurrentGrid = 0);

    /**
     * @brief Turn a flat content into a document-backed one without changing a
     *        single pixel: the grid becomes a one-layer document AND stays as
     *        that document's already-valid composite.
     *
     * This is how every pre-stage-2 smart object (and every imported image) gets
     * something editable behind it, on demand and exactly once. No-op when a
     * document already exists; returns it either way, never null.
     */
    SmartDocument* wrapGridInDocument(const QString& layerName = QString());

    /**
     * @brief Record that something inside the nested document changed.
     *
     * Bumps `document->revision`, which invalidates the composite and every
     * consumer keyed on contentStamp(). No-op for a flat content.
     */
    void markDocumentChanged();

    /**
     * @brief Install freshly composited pixels for @p forRevision.
     *
     * The cache writer, and the one grid write that does NOT bump
     * contentRevision: the stamp already moved when the document changed, and
     * bumping again would invalidate the very caches this composite exists to
     * feed. Null installs an empty grid, same as setGrid().
     */
    void setCompositeGrid(std::unique_ptr<aether::TileGrid> composited, quint64 forRevision);

    /**
     * @brief Declare the pixels already in `grid` to be the composite of the
     *        document as it stands.
     *
     * For the cases where recompositing is impossible rather than unnecessary —
     * a cyclic or absurdly deep nesting — so the doomed attempt is not repeated
     * on every refresh. No-op for a flat content.
     */
    void markCompositeCurrent();

    /**
     * @brief Adopt a document together with the grid it was composited from.
     *
     * The undo/restore entry point (`SmartContentSwapCommand`): the caller has
     * just installed the matching pixels, so nothing is invalidated beyond the
     * bounds cache.
     */
    void adoptDocument(std::shared_ptr<SmartDocument> newDocument, quint64 compositeRevisionValue);

    /**
     * @brief The size of the nested canvas this content implies.
     *
     * (0,0) to the far corner of the pixels — never a crop re-centred on them,
     * because the projection back into the parent document is anchored at the
     * content-space origin. Falls back to 512² for content with nothing painted
     * in it, which has no size to derive. A document-backed content answers with
     * the document's own size.
     */
    QSize contentSpaceSize() const;

    /**
     * @brief The precision the contents are AUTHORED in.
     *
     * The document's own format once there is one — not the composite's. The
     * flattened cache is always RGBA8 (the compositor's ping-pong is), so
     * reading the format off `grid` would quietly downgrade a 16F/32F smart
     * object to 8 bits the first time its contents were opened after a commit.
     */
    aether::TilePixelFormat storageFormat() const;

    /**
     * @brief Pixel-tight bounds of the content in content space.
     *
     * `TransformState::computeContentBounds` scans every pixel of every tile, and
     * this is asked for on hit-tests and on every projection rebuild, so the
     * result is cached against contentStamp(). A mutation that bypasses both the
     * grid's mutation entry points and markContentChanged() would keep a stale
     * value — that is the same contract TileGrid::contentVersion() already
     * documents for its own consumers.
     *
     * The cache is not synchronized: like the rest of LayerData, smart content
     * is touched from the GUI thread only.
     *
     * For a document-backed content these are the bounds of the CACHED composite:
     * while the cache is stale they still describe the previous pixels. That is
     * deliberate — bounds must stay cheap and side-effect free — so a caller who
     * needs them exact recomposites first.
     */
    aether::Rect nativeBounds() const
    {
        const quint64 stamp = contentStamp();
        if (!m_boundsValid || m_boundsStamp != stamp) {
            m_nativeBounds
                = grid ? aether::TransformState::computeContentBounds(*grid) : aether::Rect {};
            m_boundsStamp = stamp;
            m_boundsValid = true;
        }
        return m_nativeBounds;
    }

    bool isEmpty() const { return !grid || grid->empty(); }

    /**
     * @brief A private duplicate: same pixels and source description, new identity.
     *
     * Used when a smart layer is duplicated as an independent copy rather than as
     * an instance ("New Smart Object via Copy"). A nested document is deep-copied
     * with it — the whole point of detaching is that the copy can be edited
     * without the original following.
     */
    std::shared_ptr<SmartContent> cloneDetached() const;

private:
    static quint64 mixIn(quint64 seed, quint64 value)
    {
        return seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2));
    }

    mutable aether::Rect m_nativeBounds {};
    mutable quint64 m_boundsStamp = 0;
    mutable bool m_boundsValid = false;
};

} // namespace ruwa::core::layers

#endif // RUWA_CORE_LAYERS_SMARTCONTENT_H
