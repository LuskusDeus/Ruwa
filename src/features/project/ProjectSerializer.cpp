// SPDX-License-Identifier: MPL-2.0

// ProjectSerializer.cpp
#include "ProjectSerializer.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QDataStream>
#include <QBuffer>
#include <QRegularExpression>
#include <QDebug>
#include <QIODevice>
#include "shared/tiles/TileTypes.h"
#include "shared/tiles/TileFormat.h"
#include "shared/tiles/TilePixelAccess.h"

#include <algorithm>
#include <limits>

namespace ruwa::core::serialization {

namespace {

constexpr int kSerializedTextLayerType = 8;

// ============================================================================
// Untrusted-input hardening limits
//
// A .rwf file is fully untrusted (see SECURITY.md). The parser must never take
// a size or count read from the stream at face value: every one is validated
// against the bytes actually available before it drives an allocation, a
// reserve(), or a loop bound. Without this a hand-crafted file can trigger an
// integer overflow, a multi-gigabyte allocation, a multi-billion-iteration
// spin, or (via nested groups) unbounded recursion / stack overflow.
// ============================================================================

// Upper bound on a single section blob. Capped at INT_MAX because the blob is
// held in a QByteArray, whose size is an int; this guarantees the quint64 ->
// int size cast below never overflows. The practical bound is far tighter — a
// section can never exceed the bytes still left in the file (checked too).
constexpr qint64 kMaxSectionSize = static_cast<qint64>(std::numeric_limits<int>::max());

// Deepest layer-group nesting the tree reader will follow before treating the
// file as corrupt. Real documents nest a handful of groups deep; this only
// exists to stop a maliciously deep tree from overflowing the call stack.
constexpr int kMaxLayerDepth = 512;

// A nested smart-object document's canvas size comes straight from the file and
// becomes a real canvas dimension the moment those contents are opened for
// editing, so a negative or absurd value is clamped away here rather than
// reaching a widget.
constexpr int kMaxSmartDocumentDimension = 1000000;

// The document's own canvas size gets the same treatment, except it cannot be
// clamped: a zero-width canvas is not a size to repair, it is a file that never
// described a document. Opening one leaves the tab spinning forever, so it is
// rejected up front with an error the user can read.
constexpr int kMaxCanvasDimension = kMaxSmartDocumentDimension;

// Conservative lower bounds (in bytes) on the on-disk size of one element of
// each repeated structure. Used by countFitsInStream() to reject impossible
// counts. These are deliberate under-estimates: it is always safe to accept a
// count that is too small to be a threat, never one that is too large.
constexpr int kMinLayerEntryBytes = 16; // at least a QUuid id
constexpr int kMinTileEntryBytes = 12; // x(4) + y(4) + byte-array length(4)
constexpr int kMinMaskTileEntryBytes = 8; // x(4) + y(4)
constexpr int kMinDeformVertexBytes = 16; // 4 floats
constexpr int kMinTextStyleRunBytes = 16; // start(4)+length(4)+family len(4)+fontSize(4)
constexpr int kMinEffectsEntryBytes = 20; // layerId(16) + effect count(4)
constexpr int kMinEffectStateBytes = 24; // instanceId(16)+typeId len(4)+version(4)

// True when `count` elements, each at least `minElementBytes` on disk, could
// actually be present in the bytes still available on `in`. A count that fails
// this is structurally impossible for the remaining data and signals a corrupt
// or hostile file, so the caller can reject it before reserve()/looping. A
// zero count is trivially fine.
bool countFitsInStream(QDataStream& in, quint32 count, int minElementBytes)
{
    if (count == 0) {
        return true;
    }
    const QIODevice* dev = in.device();
    const qint64 avail = dev ? dev->bytesAvailable() : 0;
    return static_cast<qint64>(count) * static_cast<qint64>(minElementBytes) <= avail;
}

// Re-pack a full tile's raw bytes from one pixel format to another by routing
// each pixel through the normalized-float accessor. A no-op (just returns the
// source) when the formats already match.
//
// Under the per-document format model a loaded document simply ADOPTS its
// saved content format (document format := file contentTileFormat), so normal
// loads store tiles size-exact with no conversion. This helper is retained for
// a future explicit "change document bit depth" command, hence [[maybe_unused]].
[[maybe_unused]] QByteArray convertTileBytes(
    const QByteArray& src, aether::TilePixelFormat from, aether::TilePixelFormat to)
{
    if (from == to) {
        return src;
    }
    QByteArray dst(static_cast<int>(aether::tileByteSize(to)), Qt::Uninitialized);
    const auto* s = reinterpret_cast<const uint8_t*>(src.constData());
    auto* d = reinterpret_cast<uint8_t*>(dst.data());
    for (uint32_t y = 0; y < aether::TILE_SIZE; ++y) {
        for (uint32_t x = 0; x < aether::TILE_SIZE; ++x) {
            float rgba[4];
            aether::readTilePixelF(s, from, x, y, rgba);
            aether::writeTilePixelF(d, to, x, y, rgba);
        }
    }
    return dst;
}

// ============================================================================
// Smart-object source paths (file version >= 29)
//
// A smart content records where it can be REFRESHED from. Storing that as a
// path relative to the project file (when the source sits under the project
// directory) is what keeps links alive when a whole folder is moved, copied to
// another machine or shared; the absolute path stays as the fallback for
// sources that live elsewhere. Only the serializer deals in the relative form —
// it is derived on save and resolved back on load.
// ============================================================================

QString smartSourceRelativePathFor(const QString& absolutePath, const QString& projectDirectory)
{
    if (absolutePath.isEmpty() || projectDirectory.isEmpty()) {
        return {};
    }
    const QString relative = QDir(projectDirectory).relativeFilePath(absolutePath);
    // Only paths that stay INSIDE the project directory are useful as relative
    // ones: "../../elsewhere/img.png" would break the moment the project moves,
    // which is exactly the case the absolute fallback already covers.
    if (relative.isEmpty() || relative.startsWith(QStringLiteral(".."))
        || QDir::isAbsolutePath(relative)) {
        return {};
    }
    return relative;
}

QString resolveSmartSourcePath(
    const QString& relativePath, const QString& absolutePath, const QString& projectDirectory)
{
    if (!relativePath.isEmpty() && !projectDirectory.isEmpty()) {
        const QString candidate = QDir(projectDirectory).absoluteFilePath(relativePath);
        // The relative form wins only when it actually resolves: a project that
        // travelled with its assets finds them next to itself, one that did not
        // falls back to wherever the source used to live. A path that resolves
        // to nothing at all is not an error — the layer keeps its saved pixels.
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }
    return absolutePath;
}

} // namespace

// ============================================================================
// Public API
// ============================================================================

bool ProjectSerializer::save(const QString& filePath, const ProjectData& data)
{
    if (!data.isValid()) {
        m_lastError = QStringLiteral("Invalid project data");
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_lastError = QStringLiteral("Cannot open file for writing: %1").arg(file.errorString());
        return false;
    }

    m_projectDirectory = QFileInfo(filePath).absolutePath();

    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setVersion(QDataStream::Qt_6_0);

    // --- Header ---
    out.writeRawData(MAGIC, 4);
    out << ProjectData::CURRENT_VERSION;

    // --- ProjectInfo section ---
    {
        QByteArray blob = writeProjectInfo(data);
        out << static_cast<quint32>(SectionType::ProjectInfo);
        out << static_cast<quint64>(blob.size());
        out.writeRawData(blob.constData(), blob.size());
    }

    // --- LayerTree section ---
    {
        QByteArray blob = writeLayerTree(data);
        out << static_cast<quint32>(SectionType::LayerTree);
        out << static_cast<quint64>(blob.size());
        out.writeRawData(blob.constData(), blob.size());
    }

    // --- LayerEffects section ---
    {
        QByteArray blob = writeLayerEffects(data);
        out << static_cast<quint32>(SectionType::LayerEffects);
        out << static_cast<quint64>(blob.size());
        out.writeRawData(blob.constData(), blob.size());
    }

    // --- End marker ---
    out << static_cast<quint32>(SectionType::End);
    out << static_cast<quint64>(0);

    if (!file.commit()) {
        m_lastError = QStringLiteral("Cannot finalize file write: %1").arg(file.errorString());
        return false;
    }

    return true;
}

bool ProjectSerializer::load(const QString& filePath, ProjectData& data)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = QStringLiteral("Cannot open file: %1").arg(file.errorString());
        return false;
    }

    m_projectDirectory = QFileInfo(filePath).absolutePath();

    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);
    in.setVersion(QDataStream::Qt_6_0);

    // --- Header ---
    quint32 version = 0;
    if (!readHeader(in, version)) {
        return false;
    }

    data.version = version;
    // --- Sections ---
    data.layerEffects.clear();
    bool gotProjectInfo = false;

    while (!in.atEnd()) {
        quint32 sectionTypeRaw = 0;
        quint64 sectionSize = 0;

        in >> sectionTypeRaw;
        in >> sectionSize;

        if (in.status() != QDataStream::Ok) {
            m_lastError = QStringLiteral("Read error while parsing section header");
            return false;
        }

        auto sectionType = static_cast<SectionType>(sectionTypeRaw);

        if (sectionType == SectionType::End) {
            break;
        }

        // The declared section size is untrusted. It can never legitimately
        // exceed the bytes still left in the file, and we additionally cap it at
        // INT_MAX so the QByteArray size cast below cannot overflow into a
        // negative or truncated allocation. Reject anything that violates either
        // bound instead of trusting it into an allocation.
        const qint64 remaining = file.bytesAvailable();
        if (sectionSize > static_cast<quint64>(kMaxSectionSize)
            || static_cast<qint64>(sectionSize) > remaining) {
            m_lastError = QStringLiteral(
                "Invalid section size %1 in section %2 (%3 bytes remaining) — file corrupt")
                              .arg(sectionSize)
                              .arg(sectionTypeRaw)
                              .arg(remaining);
            return false;
        }

        // Read section blob
        QByteArray blob(static_cast<int>(sectionSize), Qt::Uninitialized);
        if (in.readRawData(blob.data(), blob.size()) != blob.size()) {
            m_lastError
                = QStringLiteral("Unexpected end of file in section %1").arg(sectionTypeRaw);
            return false;
        }

        // Dispatch
        switch (sectionType) {
        case SectionType::ProjectInfo:
            if (!readProjectInfo(blob, data))
                return false;
            gotProjectInfo = true;
            break;

        case SectionType::LayerTree:
            if (!readLayerTree(blob, data))
                return false;
            break;

        case SectionType::LayerEffects:
            if (!readLayerEffects(blob, data))
                return false;
            break;

        default:
            // Unknown section — skip (forward compatibility)
            break;
        }
    }

    if (!gotProjectInfo) {
        m_lastError = QStringLiteral("Missing ProjectInfo section");
        return false;
    }

    return true;
}

bool ProjectSerializer::validate(const QString& filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);
    in.setVersion(QDataStream::Qt_6_0);

    quint32 version = 0;
    // Reuse readHeader but we can't set m_lastError (const).
    // Just do a quick inline check.
    char magic[4] = {};
    if (in.readRawData(magic, 4) != 4)
        return false;
    if (memcmp(magic, MAGIC, 4) != 0)
        return false;

    in >> version;
    return in.status() == QDataStream::Ok && version >= 1;
}

QString ProjectSerializer::defaultFileName(const QString& projectName)
{
    QString safe = projectName.trimmed();
    safe.replace(
        QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1F]")), QStringLiteral("_"));
    safe.replace(QRegularExpression(QStringLiteral("[\\. ]+$")), QString());
    if (safe.isEmpty()) {
        safe = QStringLiteral("Untitled");
    }
    return safe + QStringLiteral(".rwf");
}

// ============================================================================
// Section Writers
// ============================================================================

QByteArray ProjectSerializer::writeProjectInfo(const ProjectData& data) const
{
    QByteArray blob;
    QDataStream out(&blob, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setVersion(QDataStream::Qt_6_0);

    out << data.projectName;
    out << data.tabTitle;
    out << data.tabIconAlias;
    out << static_cast<quint32>(data.canvasSize.width());
    out << static_cast<quint32>(data.canvasSize.height());
    out << static_cast<quint8>(data.isInfiniteCanvas() ? 1 : 0);
    out << static_cast<quint8>(data.exportFrame.enabled ? 1 : 0);
    out << static_cast<qint32>(data.exportFrame.rect.x());
    out << static_cast<qint32>(data.exportFrame.rect.y());
    out << static_cast<qint32>(data.exportFrame.rect.width());
    out << static_cast<qint32>(data.exportFrame.rect.height());

    out << static_cast<qint32>(data.currentTool);
    out << data.brushToolState.brushId;
    out << data.brushToolState.brushSize;
    out << data.brushToolState.brushOpacity;
    out << static_cast<quint8>(data.brushToolState.valid ? 1 : 0);
    out << data.brushToolState.colorRgba;
    out << data.eraserToolState.brushId;
    out << data.eraserToolState.brushSize;
    out << data.eraserToolState.brushOpacity;
    out << static_cast<quint8>(data.eraserToolState.valid ? 1 : 0);
    out << data.eraserToolState.colorRgba;
    out << data.blurToolState.brushId;
    out << data.blurToolState.brushSize;
    out << data.blurToolState.brushOpacity;
    out << static_cast<quint8>(data.blurToolState.valid ? 1 : 0);
    out << data.blurToolState.colorRgba;
    out << data.smudgeToolState.brushId;
    out << data.smudgeToolState.brushSize;
    out << data.smudgeToolState.brushOpacity;
    out << static_cast<quint8>(data.smudgeToolState.valid ? 1 : 0);
    out << data.smudgeToolState.colorRgba;
    out << data.lassoStabilization;
    out << data.lassoFillStabilization;
    out << data.lastUsedColorRgba;
    out << data.foregroundColorRgba;
    out << data.backgroundColorRgba;
    out << static_cast<quint8>(data.editingForegroundColor ? 1 : 0);
    // Workspace UI placeholders: the dock layout, canvas overlay positions and overlay
    // visibility are user preferences stored in QSettings, never in the project. The
    // slots stay in the stream (as neutral "unset" values) so the section layout is
    // stable in both directions.
    out << QByteArray(); // dock layout
    out << QPointF(-1.0, -1.0); // brush overlay position
    out << QPointF(-1.0, -1.0); // tool state overlay position
    out << QPointF(-1.0, -1.0); // stylus joystick position
    out << static_cast<quint8>(1); // stylus joystick above panel
    out << static_cast<quint8>(1); // joystick visible
    out << static_cast<quint8>(1); // brush control visible
    out << static_cast<quint8>(1); // tool state overlay visible
    out << data.selectedLayerId;
    out << static_cast<quint8>(data.contentTileFormat); // v27: content tile pixel format

    return blob;
}

QByteArray ProjectSerializer::writeLayerTree(const ProjectData& data) const
{
    QByteArray blob;
    QDataStream out(&blob, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setVersion(QDataStream::Qt_6_0);

    out << static_cast<quint32>(data.rootLayers.size());

    // Shared smart content is written by whichever entry the tree walk reaches
    // first; the state carries that knowledge across the whole (recursive) walk,
    // nested documents included.
    SmartContentWriteState smartState;
    for (const auto& layer : data.rootLayers) {
        writeLayerEntry(out, layer, smartState);
    }

    return blob;
}

void ProjectSerializer::writeLayerEntry(
    QDataStream& out, const LayerEntry& layer, SmartContentWriteState& smartState) const
{
    out << layer.id;
    out << layer.name;
    out << static_cast<quint8>(layer.nameIsCustom ? 1 : 0);
    out << static_cast<quint8>(layer.type);
    out << static_cast<quint8>(layer.visible ? 1 : 0);
    out << static_cast<quint8>(layer.locked ? 1 : 0);
    out << static_cast<quint8>(layer.expanded ? 1 : 0);
    out << layer.opacity;
    out << static_cast<quint8>(layer.blendMode);
    out << static_cast<quint8>(layer.groupCompositingMode);
    out << layer.displayColorIndex;
    out << layer.backgroundColorRgba;
    out << static_cast<quint8>(layer.backgroundTransparent ? 1 : 0);
    out << static_cast<quint8>(layer.clippedToBelow ? 1 : 0);
    out << layer.contentBounds.x;
    out << layer.contentBounds.y;
    out << layer.contentBounds.width;
    out << layer.contentBounds.height;
    out << layer.translation.x;
    out << layer.translation.y;
    out << layer.rotation;
    out << layer.scale.x;
    out << layer.scale.y;
    out << layer.pivot.x;
    out << layer.pivot.y;
    out << static_cast<quint8>(layer.hasFreeCorners ? 1 : 0);
    if (layer.hasFreeCorners) {
        for (const auto& corner : layer.freeCorners) {
            out << corner.x;
            out << corner.y;
        }
    }
    out << static_cast<quint8>(layer.hasDeformMesh ? 1 : 0);
    if (layer.hasDeformMesh) {
        out << static_cast<qint32>(layer.deformLatticeRows);
        out << static_cast<qint32>(layer.deformLatticeCols);
        out << static_cast<quint32>(layer.deformVertices.size());
        for (const auto& vertex : layer.deformVertices) {
            out << vertex.source.x;
            out << vertex.source.y;
            out << vertex.target.x;
            out << vertex.target.y;
        }
    }
    out << static_cast<quint8>(layer.hasTextPayload ? 1 : 0);
    if (layer.hasTextPayload) {
        out << layer.text;
        out << layer.textFontFamily;
        out << layer.textFontSize;
        out << layer.textColorRgba;
        out << static_cast<qint32>(layer.textAlignment);
        out << layer.textLineHeight;
        out << static_cast<quint32>(layer.textStyleRuns.size());
        for (const auto& run : layer.textStyleRuns) {
            out << static_cast<qint32>(run.start);
            out << static_cast<qint32>(run.length);
            out << run.fontFamily;
            out << run.fontSize;
            out << run.colorRgba;
            out << static_cast<quint8>(run.bold ? 1 : 0);
            out << static_cast<quint8>(run.italic ? 1 : 0);
            out << static_cast<quint8>(run.underline ? 1 : 0);
        }
    }

    // v29: smart-object content identity + source. Written for every layer type
    // (a null id simply means "this layer has no smart content"), so the reader
    // never has to know which types can carry one.
    const bool contentAlreadyWritten = !layer.smartContentId.isNull()
        && smartState.writtenTileIds.contains(layer.smartContentId);
    out << layer.smartContentId;
    out << smartSourceRelativePathFor(layer.smartSourcePath, m_projectDirectory);
    out << layer.smartSourcePath;
    out << static_cast<quint8>(layer.smartSourceKind);
    out << layer.smartSourceHash;

    // A text layer stores its glyphs, not pixels; an instance stores nothing,
    // because the entry that first carried this content already wrote the
    // pixels. A content only counts as written once pixels actually went out,
    // so an entry whose tiles were already stripped upstream cannot claim the
    // id and leave the copy that still has them dropped too.
    const bool writeTilePayload
        = (layer.type != kSerializedTextLayerType) && !contentAlreadyWritten;
    if (!layer.smartContentId.isNull() && writeTilePayload && !layer.tiles.isEmpty()) {
        smartState.writtenTileIds.insert(layer.smartContentId);
    }
    out << static_cast<quint32>(writeTilePayload ? layer.tiles.size() : 0);
    if (writeTilePayload) {
        for (const auto& tile : layer.tiles) {
            out << static_cast<qint32>(tile.x);
            out << static_cast<qint32>(tile.y);
            out << tile.pixels;
        }
    }

    // v30: the smart object's nested document — its contents as a layer stack.
    // Written after the composited pixels above, which stay unconditional: a
    // reader that stops at v29 has everything it needs to DRAW the object, and
    // only loses the ability to edit its contents as layers.
    //
    // Once per content, like the pixels, but tracked separately (see
    // SmartContentWriteState). The nested roots go through the same
    // writeLayerEntry and share the same state, so a content used both in this
    // document and inside another object's contents is still stored once.
    const bool writeSmartDocument = layer.hasSmartDocument && !layer.smartContentId.isNull()
        && !smartState.writtenDocumentIds.contains(layer.smartContentId);
    out << static_cast<quint8>(writeSmartDocument ? 1 : 0);
    if (writeSmartDocument) {
        smartState.writtenDocumentIds.insert(layer.smartContentId);
        out << static_cast<qint32>(layer.smartDocumentSize.width());
        out << static_cast<qint32>(layer.smartDocumentSize.height());
        out << static_cast<quint8>(layer.smartDocumentFormat);
        out << static_cast<quint32>(layer.smartDocumentLayers.size());
        for (const auto& nested : layer.smartDocumentLayers) {
            writeLayerEntry(out, nested, smartState);
        }
    }

    // v30: the effect chain of a NESTED layer, which cannot use the id-keyed
    // LayerEffects section (see LayerEntry::effects). Always written — four
    // bytes for the layers of this document, which leave it empty.
    out << static_cast<quint32>(layer.effects.size());
    for (const auto& effect : layer.effects) {
        writeLayerEffectState(out, effect);
    }

    // Layer mask payload (version >= 25; defaultFill + per-tile solid added in 26).
    out << static_cast<quint8>(layer.hasMask ? 1 : 0);
    if (layer.hasMask) {
        out << static_cast<quint8>(layer.maskEnabled ? 1 : 0);
        out << static_cast<quint8>(layer.maskLinked ? 1 : 0);
        out << static_cast<quint32>(layer.maskDefaultFill); // v26: mask background
        out << static_cast<quint32>(layer.maskTiles.size());
        for (const auto& tile : layer.maskTiles) {
            out << static_cast<qint32>(tile.x);
            out << static_cast<qint32>(tile.y);
            out << static_cast<quint8>(tile.solid ? 1 : 0); // v26: uniform-color tile
            if (tile.solid) {
                out << static_cast<quint32>(tile.solidColor);
            } else {
                out << tile.pixels;
            }
        }
    }

    out << static_cast<quint32>(layer.children.size());
    for (const auto& child : layer.children) {
        writeLayerEntry(out, child, smartState);
    }
}

QByteArray ProjectSerializer::writeLayerEffects(const ProjectData& data) const
{
    QByteArray blob;
    QDataStream out(&blob, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setVersion(QDataStream::Qt_6_0);

    out << static_cast<quint32>(data.layerEffects.size());
    for (const auto& entry : data.layerEffects) {
        out << entry.layerId;
        out << static_cast<quint32>(entry.effects.size());
        for (const auto& effect : entry.effects) {
            writeLayerEffectState(out, effect);
        }
    }

    return blob;
}

void ProjectSerializer::writeLayerEffectState(
    QDataStream& out, const ruwa::core::effects::LayerEffectState& state) const
{
    out << state.instanceId;
    out << state.typeId;
    out << static_cast<quint32>(state.version);
    out << static_cast<quint8>(state.enabled ? 1 : 0);
    out << static_cast<quint8>(state.realtimePreviewEnabled ? 1 : 0);
    out << static_cast<quint8>(state.uiExpanded ? 1 : 0);
    // v31: which space this filter runs in. Written before params so the reader
    // can tell the two layouts apart by version alone.
    out << static_cast<quint8>(state.contentSpace ? 1 : 0);
    out << state.params;
}

// ============================================================================
// Section Readers
// ============================================================================

bool ProjectSerializer::readProjectInfo(const QByteArray& blob, ProjectData& data) const
{
    QDataStream in(blob);
    in.setByteOrder(QDataStream::LittleEndian);
    in.setVersion(QDataStream::Qt_6_0);

    quint32 w = 0, h = 0;
    quint8 canvasBoundsMode = 0;
    quint8 exportFrameEnabled = 1;
    qint32 exportFrameX = 0;
    qint32 exportFrameY = 0;
    qint32 exportFrameW = 0;
    qint32 exportFrameH = 0;
    qint32 currentTool = 0;
    quint8 brushValid = 0;
    quint8 eraserValid = 0;
    quint8 blurValid = 0;
    quint8 smudgeValid = 0;
    quint8 editingForegroundColor = 1;

    data.selectedLayerId = QUuid();
    // Pre-v27 files carry no format tag — content tiles were always RGBA8. v27+
    // overrides this from the stream below.
    data.contentTileFormat = aether::TilePixelFormat::RGBA8;
    data.legacyUntaggedContentTiles = false;

    in >> data.projectName;
    in >> data.tabTitle;
    in >> data.tabIconAlias;
    in >> w >> h;
    if (data.version >= 11) {
        in >> canvasBoundsMode;
    }
    if (data.version >= 12) {
        in >> exportFrameEnabled;
        in >> exportFrameX;
        in >> exportFrameY;
        in >> exportFrameW;
        in >> exportFrameH;
    }
    if (data.version >= 5) {
        in >> currentTool;
        in >> data.brushToolState.brushId;
        in >> data.brushToolState.brushSize;
        in >> data.brushToolState.brushOpacity;
        in >> brushValid;
        if (data.version >= 6) {
            in >> data.brushToolState.colorRgba;
        }
        in >> data.eraserToolState.brushId;
        in >> data.eraserToolState.brushSize;
        in >> data.eraserToolState.brushOpacity;
        in >> eraserValid;
        if (data.version >= 6) {
            in >> data.eraserToolState.colorRgba;
        }
        if (data.version >= 10) {
            in >> data.blurToolState.brushId;
            in >> data.blurToolState.brushSize;
            in >> data.blurToolState.brushOpacity;
            in >> blurValid;
            in >> data.blurToolState.colorRgba;
        }
        if (data.version >= 24) {
            in >> data.smudgeToolState.brushId;
            in >> data.smudgeToolState.brushSize;
            in >> data.smudgeToolState.brushOpacity;
            in >> smudgeValid;
            in >> data.smudgeToolState.colorRgba;
        }
        if (data.version >= 19) {
            in >> data.lassoStabilization;
            in >> data.lassoFillStabilization;
        }
        in >> data.lastUsedColorRgba;
        if (data.version >= 18) {
            in >> data.foregroundColorRgba;
            in >> data.backgroundColorRgba;
            in >> editingForegroundColor;
        }
        if (data.version >= 7) {
            // Workspace UI slot: read and discard. Older files carry the dock layout,
            // canvas overlay positions and overlay visibility here, but all of that is a
            // user preference (QSettings) and must never be applied from a project.
            QByteArray discardedDockLayoutState;
            QPointF discardedOverlayPos;
            quint8 discardedFlag = 0;
            in >> discardedDockLayoutState;
            in >> discardedOverlayPos; // brush overlay
            if (data.version >= 8) {
                in >> discardedOverlayPos; // tool state overlay
            }
            in >> discardedOverlayPos; // stylus joystick
            in >> discardedFlag; // stylus joystick above panel
            in >> discardedFlag; // joystick visible
            in >> discardedFlag; // brush control visible
            if (data.version >= 9) {
                in >> discardedFlag; // tool state overlay visible
            }
        }
        if (data.version >= 23) {
            in >> data.selectedLayerId;
        }
        if (data.version >= 27) {
            if (!in.atEnd()) {
                quint8 contentTileFormat = static_cast<quint8>(aether::TilePixelFormat::RGBA8);
                in >> contentTileFormat;
                data.contentTileFormat = static_cast<aether::TilePixelFormat>(contentTileFormat);
            } else {
                // RECOVERY: header claims v27 but ProjectInfo ends before the
                // format tag — this file was written by a transitional dev build
                // whose writer had the bumped version but not yet the tag. Do not
                // treat the missing byte as corruption; flag it so the tile reader
                // loads the (TILE_BYTE_SIZE-truncated) payloads as a raw prefix.
                data.legacyUntaggedContentTiles = true;
                data.contentTileFormat = aether::kDefaultTileFormat;
                qWarning("ProjectSerializer: v27 file without content-format tag; "
                         "entering legacy recovery (content tiles were truncated on save, "
                         "top portion of each tile recoverable).");
            }
        }
        data.currentTool = currentTool;
        data.brushToolState.valid = (brushValid != 0);
        data.eraserToolState.valid = (eraserValid != 0);
        if (data.version >= 10) {
            data.blurToolState.valid = (blurValid != 0);
        } else {
            data.blurToolState = data.brushToolState;
        }
        if (data.version >= 24) {
            data.smudgeToolState.valid = (smudgeValid != 0);
        } else {
            data.smudgeToolState = data.brushToolState;
        }
        data.editingForegroundColor = (editingForegroundColor != 0);
        if (data.version < 6) {
            data.brushToolState.colorRgba = data.lastUsedColorRgba;
            data.eraserToolState.colorRgba = data.lastUsedColorRgba;
            data.blurToolState.colorRgba = data.lastUsedColorRgba;
            data.smudgeToolState.colorRgba = data.lastUsedColorRgba;
        }
        if (data.version < 18) {
            data.foregroundColorRgba = data.lastUsedColorRgba;
            data.backgroundColorRgba = 0xFFFFFFFFu;
            data.editingForegroundColor = true;
        }
    }

    if (in.status() != QDataStream::Ok) {
        m_lastError = QStringLiteral("Corrupted ProjectInfo section");
        return false;
    }

    if (w == 0 || h == 0 || w > static_cast<quint32>(kMaxCanvasDimension)
        || h > static_cast<quint32>(kMaxCanvasDimension)) {
        m_lastError = QStringLiteral("Invalid canvas size %1x%2 in ProjectInfo — file corrupt")
                          .arg(w)
                          .arg(h);
        return false;
    }

    data.canvasSize = QSize(static_cast<int>(w), static_cast<int>(h));
    data.canvasBoundsMode = (canvasBoundsMode != 0) ? ruwa::core::canvas::CanvasBoundsMode::Infinite
                                                    : ruwa::core::canvas::CanvasBoundsMode::Bounded;
    if (data.version >= 12) {
        data.exportFrame.enabled = (exportFrameEnabled != 0);
        data.exportFrame.rect = QRect(exportFrameX, exportFrameY, exportFrameW, exportFrameH);
    } else {
        data.exportFrame.enabled = true;
        data.exportFrame.rect = QRect(0, 0, data.canvasSize.width(), data.canvasSize.height());
    }
    if (data.version < 5) {
        data.currentTool = 0;
        data.brushToolState = ProjectData::ToolState {};
        data.eraserToolState = ProjectData::ToolState {};
        data.blurToolState = ProjectData::ToolState {};
        data.smudgeToolState = ProjectData::ToolState {};
        data.lassoStabilization = 0.0;
        data.lassoFillStabilization = 0.0;
        data.lastUsedColorRgba = 0xFF000000u;
        data.foregroundColorRgba = 0xFF000000u;
        data.backgroundColorRgba = 0xFFFFFFFFu;
        data.editingForegroundColor = true;
    }
    if (data.version < 19) {
        data.lassoStabilization = 0.0;
        data.lassoFillStabilization = 0.0;
    }
    if (data.version < 11) {
        data.canvasBoundsMode = ruwa::core::canvas::CanvasBoundsMode::Bounded;
    }
    if (!data.exportFrame.isValid()) {
        data.exportFrame.enabled = true;
        data.exportFrame.rect = QRect(0, 0, data.canvasSize.width(), data.canvasSize.height());
    }

    return true;
}

bool ProjectSerializer::readLayerTree(const QByteArray& blob, ProjectData& data) const
{
    QDataStream in(blob);
    in.setByteOrder(QDataStream::LittleEndian);
    in.setVersion(QDataStream::Qt_6_0);

    quint32 count = 0;
    in >> count;

    if (in.status() != QDataStream::Ok) {
        m_lastError = QStringLiteral("Corrupted LayerTree section header");
        return false;
    }

    if (!countFitsInStream(in, count, kMinLayerEntryBytes)) {
        m_lastError = QStringLiteral("Implausible root layer count %1 — file corrupt").arg(count);
        return false;
    }

    data.rootLayers.clear();
    data.rootLayers.reserve(static_cast<int>(count));

    const quint32 tileSize = static_cast<quint32>(aether::TILE_SIZE);
    for (quint32 i = 0; i < count; ++i) {
        data.rootLayers.append(readLayerEntry(in, data.version, tileSize, data.contentTileFormat,
            data.legacyUntaggedContentTiles, 0));

        if (in.status() != QDataStream::Ok) {
            m_lastError = QStringLiteral("Corrupted layer entry at index %1").arg(i);
            return false;
        }
    }

    return true;
}

bool ProjectSerializer::readLayerEffects(const QByteArray& blob, ProjectData& data) const
{
    QDataStream in(blob);
    in.setByteOrder(QDataStream::LittleEndian);
    in.setVersion(QDataStream::Qt_6_0);

    quint32 layerCount = 0;
    in >> layerCount;
    if (in.status() != QDataStream::Ok) {
        m_lastError = QStringLiteral("Corrupted LayerEffects section header");
        return false;
    }

    if (!countFitsInStream(in, layerCount, kMinEffectsEntryBytes)) {
        m_lastError
            = QStringLiteral("Implausible layer-effects count %1 — file corrupt").arg(layerCount);
        return false;
    }

    data.layerEffects.clear();
    data.layerEffects.reserve(static_cast<int>(layerCount));
    for (quint32 i = 0; i < layerCount; ++i) {
        LayerEffectsEntry entry;
        quint32 effectCount = 0;
        in >> entry.layerId;
        in >> effectCount;
        if (in.status() != QDataStream::Ok
            || !countFitsInStream(in, effectCount, kMinEffectStateBytes)) {
            m_lastError = QStringLiteral("Corrupted LayerEffects entry at index %1").arg(i);
            return false;
        }
        entry.effects.reserve(static_cast<int>(effectCount));
        for (quint32 j = 0; j < effectCount; ++j) {
            entry.effects.append(readLayerEffectState(in, data.version));
            if (in.status() != QDataStream::Ok) {
                m_lastError = QStringLiteral("Corrupted LayerEffects entry at index %1").arg(i);
                return false;
            }
        }
        data.layerEffects.append(std::move(entry));
    }

    return true;
}

ruwa::core::effects::LayerEffectState ProjectSerializer::readLayerEffectState(
    QDataStream& in, quint32 fileVersion) const
{
    ruwa::core::effects::LayerEffectState state;
    quint32 version = 1;
    quint8 enabled = 1;
    quint8 realtimePreviewEnabled = 1;
    quint8 uiExpanded = 1;
    quint8 contentSpace = 0;

    in >> state.instanceId;
    in >> state.typeId;
    in >> version;
    in >> enabled;
    in >> realtimePreviewEnabled;
    in >> uiExpanded;
    if (fileVersion >= 31) {
        in >> contentSpace;
    }
    in >> state.params;

    state.version = version;
    state.enabled = (enabled != 0);
    state.realtimePreviewEnabled = (realtimePreviewEnabled != 0);
    state.uiExpanded = (uiExpanded != 0);
    // Older files predate content space entirely: every effect they carry was
    // authored in document space, which is exactly what false means.
    state.contentSpace = (contentSpace != 0);
    return state;
}

LayerEntry ProjectSerializer::readLayerEntry(QDataStream& in, quint32 version, quint32 tileSize,
    aether::TilePixelFormat contentFormat, bool legacyUntaggedTiles, int depth) const
{
    LayerEntry layer;

    // Guard the recursion: the layer tree is read depth-first via child counts
    // straight from the untrusted file. A file that declares a group nested
    // beyond any real document would otherwise recurse until the call stack
    // overflows. Stop and flag corruption instead.
    if (depth > kMaxLayerDepth) {
        m_lastError = QStringLiteral("Layer nesting exceeds %1 levels — file corrupt or malicious")
                          .arg(kMaxLayerDepth);
        in.setStatus(QDataStream::ReadCorruptData);
        return layer;
    }

    quint8 type = 0, vis = 0, lock = 0, exp = 0, blend = 0;
    // Files before v28 had implicit pass-through groups. They intentionally
    // migrate to the new isolated default; pass-through has no UI entry point yet.
    quint8 groupCompositingMode = 0;
    quint8 nameIsCustom = 0;
    quint8 displayColorIndex = 0;
    quint8 bgTransparent = 0;
    quint8 clipped = 0;
    quint8 hasFreeCorners = 0;
    quint8 hasDeformMesh = 0;
    quint8 hasTextPayload = 0;
    quint32 tileCount = 0;
    quint32 childCount = 0;
    quint32 deformVertexCount = 0;
    quint32 textStyleRunCount = 0;

    in >> layer.id;
    in >> layer.name;
    if (version >= 21) {
        in >> nameIsCustom;
    }
    in >> type;
    in >> vis;
    in >> lock;
    in >> exp;
    in >> layer.opacity;
    in >> blend;
    if (version >= 28) {
        in >> groupCompositingMode;
    }
    if (version >= 16) {
        in >> displayColorIndex;
    }
    if (version >= 3) {
        in >> layer.backgroundColorRgba;
        in >> bgTransparent;
    }
    if (version >= 4) {
        in >> clipped;
    }
    if (version >= 15) {
        in >> layer.contentBounds.x;
        in >> layer.contentBounds.y;
        in >> layer.contentBounds.width;
        in >> layer.contentBounds.height;
        in >> layer.translation.x;
        in >> layer.translation.y;
        in >> layer.rotation;
        in >> layer.scale.x;
        in >> layer.scale.y;
        in >> layer.pivot.x;
        in >> layer.pivot.y;
        in >> hasFreeCorners;
        if (hasFreeCorners != 0) {
            for (auto& corner : layer.freeCorners) {
                in >> corner.x;
                in >> corner.y;
            }
        }
        in >> hasDeformMesh;
        if (hasDeformMesh != 0) {
            qint32 latticeRows = 4, latticeCols = 4;
            in >> latticeRows;
            in >> latticeCols;
            layer.deformLatticeRows = static_cast<int>(latticeRows);
            layer.deformLatticeCols = static_cast<int>(latticeCols);
            in >> deformVertexCount;
            if (!countFitsInStream(in, deformVertexCount, kMinDeformVertexBytes)) {
                m_lastError = QStringLiteral("Corrupted deform-mesh vertex count");
                in.setStatus(QDataStream::ReadCorruptData);
                return layer;
            }
            layer.deformVertices.reserve(static_cast<int>(deformVertexCount));
            for (quint32 i = 0; i < deformVertexCount; ++i) {
                LayerEntry::SerializedDeformVertex vertex;
                in >> vertex.source.x;
                in >> vertex.source.y;
                in >> vertex.target.x;
                in >> vertex.target.y;
                layer.deformVertices.append(vertex);
            }
        }
    }
    if (version >= 20) {
        in >> hasTextPayload;
        if (hasTextPayload != 0) {
            qint32 alignment = 0;
            in >> layer.text;
            in >> layer.textFontFamily;
            in >> layer.textFontSize;
            in >> layer.textColorRgba;
            in >> alignment;
            in >> layer.textLineHeight;
            layer.textAlignment = static_cast<int>(alignment);
            in >> textStyleRunCount;
            if (!countFitsInStream(in, textStyleRunCount, kMinTextStyleRunBytes)) {
                m_lastError = QStringLiteral("Corrupted text style-run count");
                in.setStatus(QDataStream::ReadCorruptData);
                return layer;
            }
            layer.textStyleRuns.reserve(static_cast<int>(textStyleRunCount));
            for (quint32 i = 0; i < textStyleRunCount; ++i) {
                LayerEntry::SerializedTextStyleRun run;
                qint32 start = 0;
                qint32 length = 0;
                quint8 bold = 0;
                quint8 italic = 0;
                quint8 underline = 0;
                in >> start;
                in >> length;
                in >> run.fontFamily;
                in >> run.fontSize;
                in >> run.colorRgba;
                if (version >= 22) {
                    in >> bold;
                    in >> italic;
                    in >> underline;
                }
                run.start = static_cast<int>(start);
                run.length = static_cast<int>(length);
                run.bold = (bold != 0);
                run.italic = (italic != 0);
                run.underline = (underline != 0);
                layer.textStyleRuns.append(run);
            }
        }
    }
    if (version >= 29) {
        quint8 sourceKind = 0;
        QString storedRelativePath;
        QString storedAbsolutePath;
        in >> layer.smartContentId;
        in >> storedRelativePath;
        in >> storedAbsolutePath;
        in >> sourceKind;
        in >> layer.smartSourceHash;
        if (in.status() != QDataStream::Ok) {
            return layer;
        }
        layer.smartSourceKind = static_cast<int>(sourceKind);
        layer.smartSourceRelativePath = storedRelativePath;
        layer.smartSourcePath
            = resolveSmartSourcePath(storedRelativePath, storedAbsolutePath, m_projectDirectory);
    }
    if (version >= 2) {
        in >> tileCount;
        if (!countFitsInStream(in, tileCount, kMinTileEntryBytes)) {
            m_lastError = QStringLiteral("Corrupted layer tile count");
            in.setStatus(QDataStream::ReadCorruptData);
            return layer;
        }
        layer.tiles.reserve(static_cast<int>(tileCount));
        for (quint32 i = 0; i < tileCount; ++i) {
            TileEntry tile;
            qint32 tx = 0;
            qint32 ty = 0;
            in >> tx;
            in >> ty;
            in >> tile.pixels;
            tile.x = static_cast<int>(tx);
            tile.y = static_cast<int>(ty);

            if (in.status() != QDataStream::Ok) {
                return layer;
            }

            if (legacyUntaggedTiles) {
                // RECOVERY path: a transitional v27 save wrote a fixed
                // TILE_BYTE_SIZE (256 KB) slice of each tile, truncating
                // higher-precision buffers. Rebuild a full document-format buffer
                // from whatever prefix survived and zero-fill the rest. Untagged
                // files have no recorded depth, so the reader assigned
                // contentFormat = kDefaultTileFormat (the historical dev knob) and
                // the document is stamped to that same format — the surviving prefix
                // bytes are therefore already valid document-format pixels (no
                // reinterpretation) and the buffer must be sized to contentFormat so
                // it matches the grid the document adopts.
                const int liveBytes = static_cast<int>(aether::tileByteSize(contentFormat));
                QByteArray fixed(liveBytes, '\0');
                const int copyBytes = std::min(static_cast<int>(tile.pixels.size()), liveBytes);
                std::memcpy(fixed.data(), tile.pixels.constData(), static_cast<size_t>(copyBytes));
                tile.pixels = std::move(fixed);
                layer.tiles.append(std::move(tile));
                continue;
            }

            // Content tiles are stored in the file's recorded content format.
            const quint32 expectedBytes = aether::tileByteSize(contentFormat);
            if (tile.pixels.size() != static_cast<int>(expectedBytes)) {
                m_lastError = QStringLiteral("Corrupted layer tile payload");
                in.setStatus(QDataStream::ReadCorruptData);
                return layer;
            }
            // Per-document format: the loaded document adopts this file's content
            // format (WorkspaceTab stamps documentTileFormat = data.contentTileFormat
            // and its restore memcpy sizes by grid.format()), so the payload is kept
            // size-exact in contentFormat with NO conversion. Converting to a global
            // knob here would mis-size the buffer against the grid on any doc whose
            // depth differs from the knob.

            layer.tiles.append(std::move(tile));
        }
    }
    if (version >= 30) {
        quint8 hasSmartDocument = 0;
        in >> hasSmartDocument;
        if (in.status() != QDataStream::Ok) {
            return layer;
        }
        if (hasSmartDocument != 0) {
            qint32 docWidth = 0;
            qint32 docHeight = 0;
            quint8 docFormat = 0;
            quint32 nestedCount = 0;
            in >> docWidth;
            in >> docHeight;
            in >> docFormat;
            in >> nestedCount;
            if (in.status() != QDataStream::Ok) {
                return layer;
            }
            if (!countFitsInStream(in, nestedCount, kMinLayerEntryBytes)) {
                m_lastError = QStringLiteral("Corrupted smart-object document layer count");
                in.setStatus(QDataStream::ReadCorruptData);
                return layer;
            }
            layer.hasSmartDocument = true;
            // Clamped, not trusted: the size is used as a canvas dimension when
            // the contents are opened for editing.
            layer.smartDocumentSize
                = QSize(std::clamp<int>(docWidth, 0, kMaxSmartDocumentDimension),
                    std::clamp<int>(docHeight, 0, kMaxSmartDocumentDimension));
            // Unknown formats read as RGBA8 rather than indexing a switch with
            // whatever the file said.
            layer.smartDocumentFormat
                = (docFormat <= 2) ? static_cast<int>(docFormat) : 0;
            layer.smartDocumentLayers.reserve(static_cast<int>(nestedCount));
            for (quint32 i = 0; i < nestedCount; ++i) {
                // depth + 1: a nested document is a whole tree of its own, and
                // the one depth budget has to bound every level of nesting
                // together — smart objects inside smart objects included.
                layer.smartDocumentLayers.append(readLayerEntry(
                    in, version, tileSize, contentFormat, legacyUntaggedTiles, depth + 1));
                if (in.status() != QDataStream::Ok) {
                    return layer;
                }
            }
        }

        quint32 effectCount = 0;
        in >> effectCount;
        if (in.status() != QDataStream::Ok) {
            return layer;
        }
        if (!countFitsInStream(in, effectCount, kMinEffectStateBytes)) {
            m_lastError = QStringLiteral("Corrupted nested layer effect count");
            in.setStatus(QDataStream::ReadCorruptData);
            return layer;
        }
        layer.effects.reserve(static_cast<int>(effectCount));
        for (quint32 i = 0; i < effectCount; ++i) {
            layer.effects.append(readLayerEffectState(in, version));
            if (in.status() != QDataStream::Ok) {
                return layer;
            }
        }
    }
    if (version >= 25) {
        quint8 hasMask = 0;
        in >> hasMask;
        if (hasMask != 0) {
            layer.hasMask = true;
            quint8 maskEnabled = 1;
            quint8 maskLinked = 1;
            quint32 maskTileCount = 0;
            in >> maskEnabled;
            in >> maskLinked;
            if (version >= 26) {
                quint32 maskDefaultFill = 0;
                in >> maskDefaultFill;
                layer.maskDefaultFill = maskDefaultFill;
            }
            in >> maskTileCount;
            if (!countFitsInStream(in, maskTileCount, kMinMaskTileEntryBytes)) {
                m_lastError = QStringLiteral("Corrupted layer mask tile count");
                in.setStatus(QDataStream::ReadCorruptData);
                return layer;
            }
            layer.maskEnabled = (maskEnabled != 0);
            layer.maskLinked = (maskLinked != 0);
            layer.maskTiles.reserve(static_cast<int>(maskTileCount));
            for (quint32 i = 0; i < maskTileCount; ++i) {
                TileEntry tile;
                qint32 tx = 0;
                qint32 ty = 0;
                in >> tx;
                in >> ty;
                tile.x = static_cast<int>(tx);
                tile.y = static_cast<int>(ty);

                quint8 solid = 0;
                if (version >= 26) {
                    in >> solid;
                }
                if (solid != 0) {
                    quint32 solidColor = 0;
                    in >> solidColor;
                    tile.solid = true;
                    tile.solidColor = solidColor;
                    if (in.status() != QDataStream::Ok) {
                        return layer;
                    }
                } else {
                    in >> tile.pixels;
                    if (in.status() != QDataStream::Ok) {
                        return layer;
                    }
                    const quint32 expectedBytes = tileSize * tileSize * aether::TILE_CHANNELS;
                    if (tile.pixels.size() != static_cast<int>(expectedBytes)) {
                        m_lastError = QStringLiteral("Corrupted layer mask tile payload");
                        in.setStatus(QDataStream::ReadCorruptData);
                        return layer;
                    }
                }

                layer.maskTiles.append(std::move(tile));
            }
        }
    }
    in >> childCount;
    if (!countFitsInStream(in, childCount, kMinLayerEntryBytes)) {
        m_lastError = QStringLiteral("Corrupted layer child count");
        in.setStatus(QDataStream::ReadCorruptData);
        return layer;
    }

    layer.type = static_cast<int>(type);
    layer.visible = (vis != 0);
    layer.locked = (lock != 0);
    layer.expanded = (exp != 0);
    layer.blendMode = static_cast<int>(blend);
    layer.groupCompositingMode = groupCompositingMode == 1 ? 1 : 0;
    layer.displayColorIndex = displayColorIndex;
    layer.backgroundTransparent = (bgTransparent != 0);
    layer.clippedToBelow = (clipped != 0);
    layer.hasFreeCorners = (hasFreeCorners != 0);
    layer.hasDeformMesh = (hasDeformMesh != 0);
    layer.hasTextPayload = (hasTextPayload != 0);
    layer.nameIsCustom = (nameIsCustom != 0);

    layer.children.reserve(static_cast<int>(childCount));
    for (quint32 i = 0; i < childCount; ++i) {
        layer.children.append(
            readLayerEntry(in, version, tileSize, contentFormat, legacyUntaggedTiles, depth + 1));
        // Bail on the first corrupt child instead of spinning the rest of the
        // (bounded) count doing nothing — the stream can no longer be trusted.
        if (in.status() != QDataStream::Ok) {
            return layer;
        }
    }

    return layer;
}

// ============================================================================
// Header
// ============================================================================

bool ProjectSerializer::readHeader(QDataStream& in, quint32& version) const
{
    char magic[4] = {};
    if (in.readRawData(magic, 4) != 4) {
        m_lastError = QStringLiteral("File too small — cannot read magic bytes");
        return false;
    }

    if (memcmp(magic, MAGIC, 4) != 0) {
        m_lastError = QStringLiteral("Not a Ruwa project file (invalid magic)");
        return false;
    }

    in >> version;

    if (in.status() != QDataStream::Ok) {
        m_lastError = QStringLiteral("Cannot read format version");
        return false;
    }

    if (version > ProjectData::CURRENT_VERSION) { }

    return true;
}

} // namespace ruwa::core::serialization
