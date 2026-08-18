// SPDX-License-Identifier: MPL-2.0

// ProjectData.h
#ifndef RUWA_CORE_SERIALIZATION_PROJECTDATA_H
#define RUWA_CORE_SERIALIZATION_PROJECTDATA_H

#include "features/canvas/CanvasBoundsMode.h"
#include "features/effects/LayerEffectTypes.h"
#include "shared/tiles/TileFormat.h"

#include <QString>
#include <QSize>
#include <QRect>
#include <QUuid>
#include <QList>
#include <QByteArray>
#include <QPointF>
#include <array>

namespace ruwa::core::serialization {

// ============================================================================
// Serializable layer entry
//
// Maps to ruwa::core::layers::LayerData for save/load.
// type: ruwa::core::layers::LayerType
// (Raster=0, Group=1, Adjustment=2, Vector=3, Mask=4, Background=5, Smart=6, Board=7, Text=8).
// blendMode: ruwa::core::layers::BlendMode (Normal=0 .. Luminosity=26).
// groupCompositingMode: ruwa::core::layers::GroupCompositingMode
// (Isolated=0, PassThrough=1; serialized since file version 28).
// ============================================================================

struct TileEntry {
    int x = 0;
    int y = 0;
    QByteArray pixels;
    // Uniform-color ("solid") mask tile: when true, `pixels` is empty and the
    // tile is a single premultiplied RGBA color (packed r|g<<8|b<<16|a<<24).
    // Used only by mask tiles (file version >= 26).
    bool solid = false;
    quint32 solidColor = 0;
};

struct LayerEntry {
    struct SerializedVec2 {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct SerializedRect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    struct SerializedDeformVertex {
        SerializedVec2 source;
        SerializedVec2 target;
    };

    struct SerializedTextStyleRun {
        int start = 0;
        int length = 0;
        QString fontFamily;
        qreal fontSize = 48.0;
        quint32 colorRgba = 0xFF000000u;
        bool bold = false;
        bool italic = false;
        bool underline = false;
        bool strikethrough = false;
        qreal tracking = 0.0;
        int caps = 0;
    };

    QUuid id;
    QString name;
    int type = 0; // LayerType enum → int
    bool visible = true;
    bool locked = false;
    bool expanded = true;
    qreal opacity = 1.0;
    quint8 displayColorIndex = 0; // 0 = base, 1..8 = palette slots
    int blendMode = 0; // BlendMode enum → int
    int groupCompositingMode = 0; // GroupCompositingMode: Isolated=0, PassThrough=1
    quint32 backgroundColorRgba = 0xFFFFFFFFu;
    bool backgroundTransparent = false;
    bool clippedToBelow = false;
    bool nameIsCustom = false;
    // Pixel storage belongs to the content grid, not to the document as a
    // whole. Imported images deliberately remain RGBA8 inside 16F/32F
    // documents, while newly painted grids use the document format. Serialized
    // since v32; tagged v27-v31 files infer populated grids from their exact
    // payload sizes, while empty grids inherit ProjectData::contentTileFormat.
    aether::TilePixelFormat tileFormat = aether::TilePixelFormat::RGBA8;
    QList<TileEntry> tiles; // Pixel layer tile payload (Raster/Smart, premultiplied RGBA)

    // ---- Smart-object content identity and source (file version >= 29) ----
    //
    // `smartContentId` is the identity of the PIXELS, not of the layer: several
    // entries carrying the same id are instances of one smart object and share
    // one content on load. Only the first entry with a given id carries the tile
    // payload — the serializer drops it from the rest, and the reader hands them
    // the same content, so the file stores shared content once.
    //
    // The source fields describe where the content can be REFRESHED from; the
    // pixels always travel with the file, for every sourceKind, so a project
    // stays openable by someone who does not have the source. Both path forms
    // are written: `smartSourceRelativePath` (set when the source sits under the
    // project directory) keeps links alive when a whole folder is moved or
    // shared, `smartSourcePath` is the absolute fallback. The serializer derives
    // the relative form on save and resolves it back on load — everything above
    // the serializer only ever deals with the absolute path.
    QUuid smartContentId;
    QString smartSourcePath;
    QString smartSourceRelativePath;
    int smartSourceKind = 0; // SmartSourceKind: Embedded=0, LinkedFile=1
    QByteArray smartSourceHash;

    // ---- Smart-object nested document (file version >= 30) ----
    //
    // The layer stack living INSIDE a smart object. Separate from `children`,
    // which is the UI hierarchy of THIS document: a smart object's contents are
    // another document entirely, with their own size and storage format.
    //
    // `tiles` above still carries the object's COMPOSITED pixels, always. A file
    // must open, draw and export without re-compositing anything — the nested
    // layers are what the "edit contents" tab needs, not what the canvas draws.
    //
    // Written once per smartContentId, exactly like the tile payload (instances
    // adopt the first entry's content on load), but tracked separately: an empty
    // content writes no tiles and would otherwise let its document claim the id
    // and strip pixels from the instance that does have them.
    bool hasSmartDocument = false;
    QSize smartDocumentSize;
    int smartDocumentFormat = 0; // aether::TilePixelFormat: RGBA8=0, RGBA16F=1, RGBA32F=2
    QList<LayerEntry> smartDocumentLayers; // nested roots, top-first

    // Effect chain of a layer that lives INSIDE a nested document (v30). Empty
    // for every layer of the document itself: those are stored in the separate
    // LayerEffects section, keyed by layer id. A nested layer cannot use that
    // section — it is not in the model the ids are resolved against, and ids are
    // only unique within one document anyway (a detached copy of a smart object
    // keeps its nested ids), so its chain travels with its own entry.
    QList<ruwa::core::effects::LayerEffectState> effects;
    bool hasMask = false;
    bool maskEnabled = true;
    bool maskLinked = true;
    quint32 maskDefaultFill = 0; // Mask background: implicit value of absent tiles (v26+)
    QList<TileEntry> maskTiles; // Layer mask painted grayscale (premultiplied RGBA8)
    SerializedRect contentBounds;
    SerializedVec2 translation;
    float rotation = 0.0f;
    SerializedVec2 scale { 1.0f, 1.0f };
    SerializedVec2 pivot;
    bool hasFreeCorners = false;
    std::array<SerializedVec2, 4> freeCorners {};
    bool hasDeformMesh = false;
    int deformLatticeRows = 4;
    int deformLatticeCols = 4;
    QList<SerializedDeformVertex> deformVertices;
    bool hasTextPayload = false;
    QString text;
    QString textFontFamily = QStringLiteral("Arial");
    qreal textFontSize = 48.0;
    quint32 textColorRgba = 0xFF000000u;
    int textAlignment = 0;
    qreal textLineHeight = 1.2;
    bool textStrikethrough = false;
    qreal textTracking = 0.0;
    int textCaps = 0;
    qreal textSpaceBefore = 0.0;
    qreal textSpaceAfter = 0.0;
    QList<SerializedTextStyleRun> textStyleRuns;
    QList<LayerEntry> children;
};

struct LayerEffectsEntry {
    QUuid layerId;
    QList<ruwa::core::effects::LayerEffectState> effects;
};

// ============================================================================
// Complete project data
// ============================================================================

struct ProjectData {
    struct ExportFrame {
        bool enabled = true;
        QRect rect = QRect(0, 0, 1920, 1080);

        bool isValid() const { return enabled && rect.width() > 0 && rect.height() > 0; }

        QSize size() const { return rect.size(); }
    };

    struct ToolState {
        QString brushId;
        qreal brushSize = 0.3;
        qreal brushOpacity = 1.0;
        quint32 colorRgba = 0xFF000000u;
        bool valid = false;
    };

    static constexpr quint32 CURRENT_VERSION = 33; // Text: strikethrough, tracking,
                                                   // caps, paragraph spacing

    quint32 version = CURRENT_VERSION;

    // Project info
    QString projectName;
    QString tabTitle;
    QString tabIconAlias; // Icon resource alias (e.g. "Brush", "BasicFile")

    // Canvas
    QSize canvasSize = QSize(1920, 1080);
    ruwa::core::canvas::CanvasBoundsMode canvasBoundsMode
        = ruwa::core::canvas::CanvasBoundsMode::Bounded;
    ExportFrame exportFrame;

    // Default pixel storage format for newly created content grids in this
    // document (v27+). Since v32 each LayerEntry also records the actual format
    // of its own grid, allowing deliberately cheap RGBA8 imports inside a
    // 16F/32F document. Mask tiles are always RGBA8 and are unaffected. Empty
    // grids from older files inherit this format; populated v27-v31 grids can
    // be migrated from their unambiguous payload sizes.
    aether::TilePixelFormat contentTileFormat = aether::kDefaultTileFormat;

    // Recovery flag (not serialized). Set by the loader when a file's header
    // claims version >= 27 but its ProjectInfo section was written before the
    // content-format tag existed (a transitional dev build). Such files also
    // have TILE_BYTE_SIZE-truncated content payloads, so the tile reader loads
    // them as a raw prefix of the live-format buffer instead of failing.
    bool legacyUntaggedContentTiles = false;

    // Migration flag (not serialized). Set when a tagged v27-v31 file contains
    // content grids whose exact payload sizes prove that their real formats
    // differ from contentTileFormat. The workspace marks such a project
    // modified so its next save upgrades the file to the self-describing v32
    // layout.
    bool recoveredMixedTileFormats = false;

    // Layers (root level, children are nested)
    QList<LayerEntry> rootLayers;
    QList<LayerEffectsEntry> layerEffects;
    QUuid selectedLayerId;

    // Workspace tool/color state
    int currentTool = 0; // workspace::ToolId as int
    ToolState brushToolState;
    ToolState eraserToolState;
    ToolState blurToolState;
    ToolState smudgeToolState;
    qreal lassoStabilization = 0.0;
    qreal lassoFillStabilization = 0.0;
    quint32 lastUsedColorRgba = 0xFF000000u;
    quint32 foregroundColorRgba = 0xFF000000u;
    quint32 backgroundColorRgba = 0xFFFFFFFFu;
    bool editingForegroundColor = true;

    // NOTE: workspace UI state - the dock layout, canvas overlay positions and overlay
    // visibility - is deliberately NOT part of a project. It is a user preference and
    // lives only in QSettings ("Workspace/..."). The .rwf format still carries neutral
    // placeholders in those slots so files stay readable in both directions.

    bool isValid() const
    {
        return !projectName.isEmpty() && canvasSize.width() > 0 && canvasSize.height() > 0
            && exportFrame.isValid();
    }

    bool isInfiniteCanvas() const { return ruwa::core::canvas::isInfiniteCanvas(canvasBoundsMode); }

    bool hasFiniteDocumentBounds() const
    {
        return ruwa::core::canvas::hasFiniteDocumentBounds(canvasBoundsMode);
    }
};

} // namespace ruwa::core::serialization

#endif // RUWA_CORE_SERIALIZATION_PROJECTDATA_H
