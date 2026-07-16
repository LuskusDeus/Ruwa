// SPDX-License-Identifier: MPL-2.0

// ProjectData.h
#ifndef RUWA_CORE_SERIALIZATION_PROJECTDATA_H
#define RUWA_CORE_SERIALIZATION_PROJECTDATA_H

#include "features/canvas/CanvasBoundsMode.h"
#include "features/effects/LayerEffectTypes.h"
#include "shared/tiles/TileFormat.h"
#include "shared/types/CanvasWidgets.h"

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
    QList<TileEntry> tiles; // Pixel layer tile payload (Raster/Smart, premultiplied RGBA8)
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

    static constexpr quint32 CURRENT_VERSION = 28; // Isolated/pass-through group compositing

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

    // Pixel storage format of CONTENT tile payloads in this file (v27+). Mask
    // tiles are always RGBA8 and are unaffected. Files written before v27 had no
    // tag and were always RGBA8; the reader defaults to RGBA8 for them. On load
    // the serializer converts content tiles from this format to the live
    // document format (aether::kDefaultTileFormat) so consumers never see a
    // mismatched payload.
    aether::TilePixelFormat contentTileFormat = aether::kDefaultTileFormat;

    // Recovery flag (not serialized). Set by the loader when a file's header
    // claims version >= 27 but its ProjectInfo section was written before the
    // content-format tag existed (a transitional dev build). Such files also
    // have TILE_BYTE_SIZE-truncated content payloads, so the tile reader loads
    // them as a raw prefix of the live-format buffer instead of failing.
    bool legacyUntaggedContentTiles = false;

    // Layers (root level, children are nested)
    QList<LayerEntry> rootLayers;
    QList<LayerEffectsEntry> layerEffects;
    QUuid selectedLayerId;

    // Workspace tool/color state
    int currentTool = 0; // workspace::CanvasPanel::ToolMode as int
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

    // Workspace dock/canvas widget state
    QByteArray dockLayoutState;
    QPointF brushOverlayPosNormalized = QPointF(-1.0, -1.0);
    QPointF toolStateOverlayPosNormalized = QPointF(-1.0, -1.0);
    QPointF stylusJoystickPosNormalized = QPointF(-1.0, -1.0);
    bool stylusJoystickAbovePanel = true;
    ruwa::ui::CanvasWidgetVisibility canvasWidgets;

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
