// SPDX-License-Identifier: MPL-2.0

// Untrusted-input tests for the .rwf project parser.
//
// A .rwf file is fully untrusted input (see SECURITY.md). These tests craft
// hostile and boundary-case files and assert the parser rejects them cleanly —
// returning false with an error message rather than overflowing a size cast,
// attempting a multi-gigabyte allocation, spinning on a bogus count, or
// overflowing the call stack on a deeply nested layer tree. A normal
// save/load round-trip is included to make sure the hardening did not break
// loading of well-formed files.

#include <catch2/catch_test_macros.hpp>

#include "features/project/ProjectData.h"
#include "features/project/ProjectSerializer.h"
#include "features/effects/LayerEffectTypes.h"
#include "shared/tiles/TileFormat.h"

#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QTemporaryDir>
#include <QUuid>

#include <limits>
#include <utility>

using ruwa::core::serialization::LayerEntry;
using ruwa::core::serialization::ProjectData;
using ruwa::core::serialization::ProjectSerializer;
using ruwa::core::serialization::TileEntry;

namespace {

// The on-disk section enum values (kept in sync with ProjectSerializer.h,
// which does not export them).
enum : quint32 {
    kSectionEnd = 0,
    kSectionProjectInfo = 1,
    kSectionLayerTree = 2,
    kSectionLayerEffects = 3
};

// Serializes into a QByteArray using the same stream settings the parser uses,
// letting the caller fill it via `fn(QDataStream&)`. (QDataStream is neither
// copyable nor movable, so it cannot simply be returned by value.)
template <typename F> QByteArray buildBytes(F&& fn)
{
    QByteArray b;
    QDataStream s(&b, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::LittleEndian);
    s.setVersion(QDataStream::Qt_6_0);
    fn(s);
    return b;
}

// Writes `bytes` to a fresh file inside `dir` and returns its path.
QString writeTempFile(const QTemporaryDir& dir, const QString& name, const QByteArray& bytes)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    REQUIRE(f.write(bytes) == bytes.size());
    f.close();
    return path;
}

// Writes the 4-byte magic + version header the parser expects.
void writeHeader(QDataStream& s, quint32 version = ProjectData::CURRENT_VERSION)
{
    s.writeRawData("UWA\0", 4); // magic: 'U','W','A','\0'
    s << version;
}

// Returns the payload of the first section of `type` in a whole .rwf file, or an
// empty array when there is none. Lets a test reuse a section the real writer
// produced while replacing another one with hand-crafted bytes.
QByteArray extractSection(const QByteArray& fileBytes, quint32 type)
{
    QDataStream s(fileBytes);
    s.setByteOrder(QDataStream::LittleEndian);
    s.setVersion(QDataStream::Qt_6_0);

    char magic[4] = {};
    quint32 version = 0;
    s.readRawData(magic, 4); // header is not what this helper is after
    s >> version;
    Q_UNUSED(magic);
    Q_UNUSED(version);

    while (s.status() == QDataStream::Ok && !s.atEnd()) {
        quint32 sectionType = 0;
        quint64 sectionSize = 0;
        s >> sectionType;
        s >> sectionSize;
        if (s.status() != QDataStream::Ok || sectionType == kSectionEnd) {
            break;
        }
        QByteArray blob(static_cast<int>(sectionSize), Qt::Uninitialized);
        if (s.readRawData(blob.data(), blob.size()) != blob.size()) {
            break;
        }
        if (sectionType == type) {
            return blob;
        }
    }
    return {};
}

QByteArray buildV28LayerTree(const QList<QByteArray>& tilePayloads)
{
    return buildBytes([&](QDataStream& s) {
        s << quint32(1); // root count
        s << QUuid::createUuid();
        s << QStringLiteral("Legacy layer");
        s << quint8(0); // nameIsCustom
        s << quint8(0); // raster
        s << quint8(1); // visible
        s << quint8(0); // locked
        s << quint8(1); // expanded
        s << qreal(1.0);
        s << quint8(0); // blendMode
        s << quint8(0); // groupCompositingMode
        s << quint8(0); // displayColorIndex
        s << quint32(0xFFFFFFFFu);
        s << quint8(0); // backgroundTransparent
        s << quint8(0); // clippedToBelow
        for (int i = 0; i < 11; ++i) {
            s << float(0.0f);
        }
        s << quint8(0); // hasFreeCorners
        s << quint8(0); // hasDeformMesh
        s << quint8(0); // hasTextPayload
        s << quint32(tilePayloads.size());
        for (qsizetype i = 0; i < tilePayloads.size(); ++i) {
            s << qint32(i) << qint32(0) << tilePayloads.at(i);
        }
        s << quint8(0); // hasMask
        s << quint32(0); // child count
    });
}

QString writeV28Project(const QTemporaryDir& dir, const QString& fileName,
    aether::TilePixelFormat documentFormat, const QList<QByteArray>& tilePayloads)
{
    ProjectData source;
    source.projectName = QStringLiteral("legacy format source");
    source.canvasSize = QSize(64, 64);
    source.contentTileFormat = documentFormat;
    const QString sourcePath = dir.filePath(fileName + QStringLiteral(".source"));
    ProjectSerializer writer;
    REQUIRE(writer.save(sourcePath, source));
    QFile sourceFile(sourcePath);
    REQUIRE(sourceFile.open(QIODevice::ReadOnly));
    const QByteArray infoBlob = extractSection(sourceFile.readAll(), kSectionProjectInfo);
    REQUIRE_FALSE(infoBlob.isEmpty());

    const QByteArray layerBlob = buildV28LayerTree(tilePayloads);
    const QByteArray file = buildBytes([&](QDataStream& s) {
        writeHeader(s, 28);
        s << quint32(kSectionProjectInfo) << quint64(infoBlob.size());
        s.writeRawData(infoBlob.constData(), infoBlob.size());
        s << quint32(kSectionLayerTree) << quint64(layerBlob.size());
        s.writeRawData(layerBlob.constData(), layerBlob.size());
        s << quint32(kSectionEnd) << quint64(0);
    });
    return writeTempFile(dir, fileName, file);
}

} // namespace

TEST_CASE("Well-formed project round-trips through save/load", "[serializer]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    ProjectData data;
    data.projectName = QStringLiteral("hello");
    data.canvasSize = QSize(128, 128);

    LayerEntry layer;
    layer.id = QUuid::createUuid();
    layer.name = QStringLiteral("Layer 1");
    layer.type = 0; // raster

    TileEntry tile;
    tile.x = 0;
    tile.y = 0;
    tile.pixels
        = QByteArray(static_cast<int>(aether::tileByteSize(aether::TilePixelFormat::RGBA8)), '\0');
    layer.tiles.append(tile);
    data.rootLayers.append(layer);

    const QString path = dir.filePath("valid.rwf");
    ProjectSerializer writer;
    REQUIRE(writer.save(path, data));

    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE(reader.load(path, loaded));

    CHECK(loaded.projectName == QStringLiteral("hello"));
    CHECK(loaded.canvasSize == QSize(128, 128));
    REQUIRE(loaded.rootLayers.size() == 1);
    REQUIRE(loaded.rootLayers[0].tiles.size() == 1);
    CHECK(loaded.rootLayers[0].tiles[0].pixels.size() == tile.pixels.size());
}

TEST_CASE("Mixed content-grid formats round-trip without converting imported pixels",
    "[serializer][tile-format]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    ProjectData data;
    data.projectName = QStringLiteral("mixed formats");
    data.canvasSize = QSize(256, 256);
    data.contentTileFormat = aether::TilePixelFormat::RGBA16F;

    LayerEntry imported;
    imported.id = QUuid::createUuid();
    imported.name = QStringLiteral("Imported RGBA8");
    imported.type = 0;
    imported.tileFormat = aether::TilePixelFormat::RGBA8;
    TileEntry importedTile;
    importedTile.pixels
        = QByteArray(static_cast<int>(aether::tileByteSize(imported.tileFormat)), '\x11');
    imported.tiles.append(importedTile);

    LayerEntry painted;
    painted.id = QUuid::createUuid();
    painted.name = QStringLiteral("Painted RGBA16F");
    painted.type = 0;
    painted.tileFormat = aether::TilePixelFormat::RGBA16F;
    TileEntry paintedTile;
    paintedTile.pixels
        = QByteArray(static_cast<int>(aether::tileByteSize(painted.tileFormat)), '\x22');
    painted.tiles.append(paintedTile);

    LayerEntry emptyHighPrecision;
    emptyHighPrecision.id = QUuid::createUuid();
    emptyHighPrecision.name = QStringLiteral("Empty RGBA32F");
    emptyHighPrecision.type = 0;
    emptyHighPrecision.tileFormat = aether::TilePixelFormat::RGBA32F;

    data.rootLayers = { imported, painted, emptyHighPrecision };

    const QString path = dir.filePath("mixed-formats.rwf");
    ProjectSerializer writer;
    REQUIRE(writer.save(path, data));

    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE(reader.load(path, loaded));
    REQUIRE(loaded.rootLayers.size() == 3);
    CHECK(loaded.contentTileFormat == aether::TilePixelFormat::RGBA16F);
    CHECK(loaded.rootLayers[0].tileFormat == aether::TilePixelFormat::RGBA8);
    CHECK(loaded.rootLayers[0].tiles[0].pixels == importedTile.pixels);
    CHECK(loaded.rootLayers[1].tileFormat == aether::TilePixelFormat::RGBA16F);
    CHECK(loaded.rootLayers[1].tiles[0].pixels == paintedTile.pixels);
    CHECK(loaded.rootLayers[2].tileFormat == aether::TilePixelFormat::RGBA32F);
    CHECK(loaded.rootLayers[2].tiles.isEmpty());
}

TEST_CASE(
    "Save rejects a tile payload that disagrees with its grid format", "[serializer][tile-format]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    ProjectData data;
    data.projectName = QStringLiteral("invalid mixed formats");
    data.canvasSize = QSize(64, 64);

    LayerEntry layer;
    layer.id = QUuid::createUuid();
    layer.name = QStringLiteral("Broken RGBA16F");
    layer.type = 0;
    layer.tileFormat = aether::TilePixelFormat::RGBA16F;
    TileEntry tile;
    tile.pixels
        = QByteArray(static_cast<int>(aether::tileByteSize(aether::TilePixelFormat::RGBA8)), '\0');
    layer.tiles.append(tile);
    data.rootLayers.append(layer);

    ProjectSerializer writer;
    REQUIRE_FALSE(writer.save(dir.filePath("invalid.rwf"), data));
    CHECK(writer.lastError().contains(QStringLiteral("RGBA16F")));
    CHECK_FALSE(QFileInfo::exists(dir.filePath("invalid.rwf")));
}

TEST_CASE("Save rejects an unknown content-grid format", "[serializer][tile-format]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    ProjectData data;
    data.projectName = QStringLiteral("unknown format");
    data.canvasSize = QSize(64, 64);

    LayerEntry layer;
    layer.id = QUuid::createUuid();
    layer.type = 0;
    layer.tileFormat = static_cast<aether::TilePixelFormat>(99);
    data.rootLayers.append(layer);

    ProjectSerializer writer;
    REQUIRE_FALSE(writer.save(dir.filePath("unknown-format.rwf"), data));
    CHECK(writer.lastError().contains(
        QStringLiteral("unsupported tile format"), Qt::CaseInsensitive));
}

TEST_CASE("Empty grids from before v32 inherit the document format",
    "[serializer][tile-format][compatibility]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString path = writeV28Project(
        dir, QStringLiteral("empty-v28.rwf"), aether::TilePixelFormat::RGBA16F, {});
    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE(reader.load(path, loaded));
    REQUIRE(loaded.rootLayers.size() == 1);
    CHECK(loaded.rootLayers[0].tileFormat == aether::TilePixelFormat::RGBA16F);
    CHECK_FALSE(loaded.recoveredMixedTileFormats);
}

TEST_CASE("Pre-v32 mixed-format grids are recovered from exact tile payload sizes",
    "[serializer][tile-format][compatibility][recovery]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    constexpr aether::TilePixelFormat formats[] = { aether::TilePixelFormat::RGBA8,
        aether::TilePixelFormat::RGBA16F, aether::TilePixelFormat::RGBA32F };
    for (const aether::TilePixelFormat format : formats) {
        const QString formatName = QString::fromLatin1(aether::tileFormatName(format));
        const QByteArray pixels(static_cast<int>(aether::tileByteSize(format)),
            static_cast<char>(0x31 + static_cast<int>(format)));
        const QString path
            = writeV28Project(dir, QStringLiteral("mixed-v28-%1.rwf").arg(formatName),
                aether::TilePixelFormat::RGBA16F, { pixels });

        ProjectSerializer reader;
        ProjectData loaded;
        REQUIRE(reader.load(path, loaded));
        REQUIRE(loaded.rootLayers.size() == 1);
        CHECK(loaded.contentTileFormat == aether::TilePixelFormat::RGBA16F);
        CHECK(loaded.rootLayers[0].tileFormat == format);
        REQUIRE(loaded.rootLayers[0].tiles.size() == 1);
        CHECK(loaded.rootLayers[0].tiles[0].pixels == pixels);
        CHECK(loaded.recoveredMixedTileFormats == (format != aether::TilePixelFormat::RGBA16F));

        // The normal writer upgrades the in-memory representation to v32.
        const QString migratedPath
            = dir.filePath(QStringLiteral("migrated-v32-%1.rwf").arg(formatName));
        ProjectSerializer writer;
        REQUIRE(writer.save(migratedPath, loaded));

        ProjectData migrated;
        REQUIRE(reader.load(migratedPath, migrated));
        CHECK(migrated.version == ProjectData::CURRENT_VERSION);
        CHECK(migrated.rootLayers[0].tileFormat == format);
        CHECK_FALSE(migrated.recoveredMixedTileFormats);
    }
}

TEST_CASE("Pre-v32 recovery rejects conflicting formats inside one grid",
    "[serializer][tile-format][compatibility][recovery]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QByteArray rgba8(
        static_cast<int>(aether::tileByteSize(aether::TilePixelFormat::RGBA8)), '\0');
    const QByteArray rgba16f(
        static_cast<int>(aether::tileByteSize(aether::TilePixelFormat::RGBA16F)), '\0');
    const QString path = writeV28Project(dir, QStringLiteral("conflicting-v28.rwf"),
        aether::TilePixelFormat::RGBA16F, { rgba8, rgba16f });

    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE_FALSE(reader.load(path, loaded));
    CHECK(reader.lastError().contains(QStringLiteral("conflicting legacy tile payload formats")));
}

TEST_CASE("Pre-v32 recovery rejects an unknown tile payload size",
    "[serializer][tile-format][compatibility][recovery]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString path = writeV28Project(dir, QStringLiteral("unknown-size-v28.rwf"),
        aether::TilePixelFormat::RGBA16F, { QByteArray(123, '\0') });

    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE_FALSE(reader.load(path, loaded));
    CHECK(reader.lastError().contains(QStringLiteral("unsupported 123-byte legacy tile payload")));
}

TEST_CASE("Bad magic bytes are rejected", "[serializer]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QByteArray buf = buildBytes([](QDataStream& s) {
        s.writeRawData("XXXX", 4);
        s << quint32(ProjectData::CURRENT_VERSION);
    });
    const QString path = writeTempFile(dir, "badmagic.rwf", buf);

    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE_FALSE(reader.load(path, loaded));
    CHECK_FALSE(reader.lastError().isEmpty());
}

TEST_CASE("Truncated header (no version) is rejected", "[serializer]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QByteArray buf = buildBytes([](QDataStream& s) {
        s.writeRawData("UWA\0", 4); // magic only, version missing
    });
    const QString path = writeTempFile(dir, "truncated.rwf", buf);

    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE_FALSE(reader.load(path, loaded));
}

TEST_CASE("A future project format is rejected explicitly", "[serializer][compatibility]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QByteArray buf
        = buildBytes([](QDataStream& s) { writeHeader(s, ProjectData::CURRENT_VERSION + 1); });
    const QString path = writeTempFile(dir, "future-version.rwf", buf);

    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE_FALSE(reader.load(path, loaded));
    CHECK(reader.lastError().contains(QStringLiteral("Unsupported project format version")));
    CHECK_FALSE(reader.validate(path));
}

TEST_CASE("Section size larger than the file is rejected without huge allocation", "[serializer]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QByteArray buf = buildBytes([](QDataStream& s) {
        writeHeader(s);
        s << quint32(kSectionProjectInfo);
        s << quint64(std::numeric_limits<quint64>::max()); // absurd size
        // No payload follows — the size must be rejected before any read.
    });
    const QString path = writeTempFile(dir, "bigsection.rwf", buf);

    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE_FALSE(reader.load(path, loaded)); // must not crash / OOM
    CHECK_FALSE(reader.lastError().isEmpty());
}

TEST_CASE("Section size that overflows the int cast is rejected", "[serializer]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QByteArray buf = buildBytes([](QDataStream& s) {
        writeHeader(s);
        s << quint32(kSectionProjectInfo);
        // > INT_MAX: a naive static_cast<int> would wrap negative.
        s << quint64(static_cast<quint64>(std::numeric_limits<int>::max()) + 1);
    });
    const QString path = writeTempFile(dir, "intoverflow.rwf", buf);

    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE_FALSE(reader.load(path, loaded));
}

TEST_CASE("Implausible root layer count is rejected", "[serializer]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    // A LayerTree section whose blob is only a 4-byte count claiming ~4 billion
    // root layers. The reader must reject it before reserve()/looping.
    const QByteArray sectionBlob = buildBytes([](QDataStream& s) { s << quint32(0xFFFFFFFFu); });

    const QByteArray buf = buildBytes([&](QDataStream& s) {
        writeHeader(s);
        s << quint32(kSectionLayerTree);
        s << quint64(sectionBlob.size());
        s.writeRawData(sectionBlob.constData(), sectionBlob.size());
    });
    const QString path = writeTempFile(dir, "bigcount.rwf", buf);

    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE_FALSE(reader.load(path, loaded)); // must not OOM
    CHECK_FALSE(reader.lastError().isEmpty());
}

TEST_CASE("Implausible layer-effects count is rejected", "[serializer]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QByteArray sectionBlob = buildBytes([](QDataStream& s) { s << quint32(0xFFFFFFFFu); });

    const QByteArray buf = buildBytes([&](QDataStream& s) {
        writeHeader(s);
        s << quint32(kSectionLayerEffects);
        s << quint64(sectionBlob.size());
        s.writeRawData(sectionBlob.constData(), sectionBlob.size());
    });
    const QString path = writeTempFile(dir, "bigeffects.rwf", buf);

    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE_FALSE(reader.load(path, loaded));
}

TEST_CASE("Deeply nested layer tree is rejected without stack overflow", "[serializer]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    // Build a chain of nested groups far deeper than any real document, written
    // by the real serializer so the bytes are otherwise valid. Loading it must
    // trip the depth guard and fail cleanly rather than recurse into a crash.
    ProjectData data;
    data.projectName = QStringLiteral("deep");
    data.canvasSize = QSize(64, 64);

    LayerEntry root;
    root.id = QUuid::createUuid();
    root.type = 1; // group
    LayerEntry* tip = &root;
    for (int i = 0; i < 600; ++i) {
        LayerEntry child;
        child.id = QUuid::createUuid();
        child.type = 1; // group
        tip->children.append(child);
        tip = &tip->children.last();
    }
    data.rootLayers.append(root);

    const QString path = dir.filePath("deep.rwf");
    ProjectSerializer writer;
    REQUIRE(writer.save(path, data));

    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE_FALSE(reader.load(path, loaded)); // must not stack-overflow
    CHECK_FALSE(reader.lastError().isEmpty());
}

// ============================================================================
// Smart-object content (file version >= 29)
// ============================================================================

TEST_CASE("Shared smart content is written once and reloads as instances", "[serializer]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    ProjectData data;
    data.projectName = QStringLiteral("instances");
    data.canvasSize = QSize(128, 128);

    const QUuid contentId = QUuid::createUuid();
    TileEntry tile;
    tile.x = 0;
    tile.y = 0;
    tile.pixels = QByteArray(
        static_cast<int>(aether::tileByteSize(aether::TilePixelFormat::RGBA16F)), '\0');

    // Two layers showing one smart object: same contentId, same pixels.
    LayerEntry first;
    first.id = QUuid::createUuid();
    first.name = QStringLiteral("Object");
    first.type = 6; // smart
    first.tileFormat = aether::TilePixelFormat::RGBA16F;
    first.smartContentId = contentId;
    first.smartSourceHash = QByteArray("hash-bytes");
    first.tiles.append(tile);

    LayerEntry second = first;
    second.id = QUuid::createUuid();
    second.name = QStringLiteral("Object instance");

    data.rootLayers.append(first);
    data.rootLayers.append(second);

    const QString path = dir.filePath("instances.rwf");
    ProjectSerializer writer;
    REQUIRE(writer.save(path, data));

    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE(reader.load(path, loaded));
    REQUIRE(loaded.rootLayers.size() == 2);

    // Identity survives on both, so the loader can re-link them as instances.
    CHECK(loaded.rootLayers[0].smartContentId == contentId);
    CHECK(loaded.rootLayers[1].smartContentId == contentId);
    CHECK(loaded.rootLayers[0].smartSourceHash == QByteArray("hash-bytes"));
    CHECK(loaded.rootLayers[0].tileFormat == aether::TilePixelFormat::RGBA16F);
    CHECK(loaded.rootLayers[1].tileFormat == aether::TilePixelFormat::RGBA16F);

    // ...but the pixels are stored exactly once.
    CHECK(loaded.rootLayers[0].tiles.size() == 1);
    CHECK(loaded.rootLayers[1].tiles.isEmpty());
}

TEST_CASE("Smart source path follows a project moved with its assets", "[serializer]")
{
    QTemporaryDir originalDir;
    QTemporaryDir movedDir;
    REQUIRE(originalDir.isValid());
    REQUIRE(movedDir.isValid());

    // An asset sitting next to the project file.
    const QString assetName = QStringLiteral("source.png");
    for (const QTemporaryDir* d : { &originalDir, &movedDir }) {
        QFile asset(d->filePath(assetName));
        REQUIRE(asset.open(QIODevice::WriteOnly));
        asset.write("not-really-an-image");
        asset.close();
    }

    ProjectData data;
    data.projectName = QStringLiteral("linked");
    data.canvasSize = QSize(64, 64);

    LayerEntry layer;
    layer.id = QUuid::createUuid();
    layer.type = 6; // smart
    layer.smartContentId = QUuid::createUuid();
    layer.smartSourcePath = originalDir.filePath(assetName);
    data.rootLayers.append(layer);

    const QString originalPath = originalDir.filePath("linked.rwf");
    ProjectSerializer writer;
    REQUIRE(writer.save(originalPath, data));

    // Loading in place resolves to the original asset.
    {
        ProjectSerializer reader;
        ProjectData loaded;
        REQUIRE(reader.load(originalPath, loaded));
        REQUIRE(loaded.rootLayers.size() == 1);
        CHECK(QFileInfo(loaded.rootLayers[0].smartSourcePath)
            == QFileInfo(originalDir.filePath(assetName)));
    }

    // Copying the whole folder elsewhere must re-point the source at the copy:
    // that is what the project-relative path is for.
    const QString movedPath = movedDir.filePath("linked.rwf");
    REQUIRE(QFile::copy(originalPath, movedPath));

    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE(reader.load(movedPath, loaded));
    REQUIRE(loaded.rootLayers.size() == 1);
    CHECK(
        QFileInfo(loaded.rootLayers[0].smartSourcePath) == QFileInfo(movedDir.filePath(assetName)));
}

TEST_CASE("Files written before v29 load with no smart content identity", "[serializer]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    // A v28 layer entry: everything up to the smart block, then straight to the
    // tile count. The reader must not consume the smart fields for such a file.
    const QByteArray layerBlob = buildBytes([](QDataStream& s) {
        s << quint32(1); // root layer count
        s << QUuid::createUuid();
        s << QStringLiteral("Legacy smart");
        s << quint8(0); // nameIsCustom (v21+)
        s << quint8(6); // type: smart
        s << quint8(1); // visible
        s << quint8(0); // locked
        s << quint8(1); // expanded
        s << qreal(1.0); // opacity
        s << quint8(0); // blendMode
        s << quint8(0); // groupCompositingMode (v28+)
        s << quint8(0); // displayColorIndex (v16+)
        s << quint32(0xFFFFFFFFu); // backgroundColorRgba (v3+)
        s << quint8(0); // backgroundTransparent
        s << quint8(0); // clippedToBelow (v4+)
        for (int i = 0; i < 11; ++i) {
            s << float(0.0f); // contentBounds + translation + rotation + scale + pivot (v15+)
        }
        s << quint8(0); // hasFreeCorners
        s << quint8(0); // hasDeformMesh
        s << quint8(0); // hasTextPayload (v20+)
        s << quint32(0); // tile count (v2+)
        s << quint8(0); // hasMask (v25+)
        s << quint32(0); // child count
    });

    // ProjectInfo has not changed shape since v27, so the real writer's section
    // is exactly what a v28 file carries — reuse it instead of hand-rolling the
    // thirty-odd fields the reader expects.
    ProjectData source;
    source.projectName = QStringLiteral("legacy");
    source.canvasSize = QSize(64, 64);
    const QString sourcePath = dir.filePath("source-for-info.rwf");
    ProjectSerializer writer;
    REQUIRE(writer.save(sourcePath, source));

    QFile sourceFile(sourcePath);
    REQUIRE(sourceFile.open(QIODevice::ReadOnly));
    const QByteArray infoBlob = extractSection(sourceFile.readAll(), kSectionProjectInfo);
    REQUIRE_FALSE(infoBlob.isEmpty());

    const QByteArray file = buildBytes([&](QDataStream& s) {
        writeHeader(s, 28);
        s << quint32(kSectionProjectInfo);
        s << quint64(infoBlob.size());
        s.writeRawData(infoBlob.constData(), infoBlob.size());
        s << quint32(kSectionLayerTree);
        s << quint64(layerBlob.size());
        s.writeRawData(layerBlob.constData(), layerBlob.size());
        s << quint32(kSectionEnd);
        s << quint64(0);
    });

    const QString path = writeTempFile(dir, QStringLiteral("v28.rwf"), file);

    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE(reader.load(path, loaded));
    REQUIRE(loaded.rootLayers.size() == 1);
    CHECK(loaded.rootLayers[0].name == QStringLiteral("Legacy smart"));
    // No identity in the file: the loader gives such a layer its own content.
    CHECK(loaded.rootLayers[0].smartContentId.isNull());
    CHECK(loaded.rootLayers[0].smartSourcePath.isEmpty());
}

TEST_CASE("A smart object's nested document round-trips and is stored once", "[serializer]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    ProjectData data;
    data.projectName = QStringLiteral("nested");
    data.canvasSize = QSize(128, 128);

    const int tileBytes = static_cast<int>(aether::tileByteSize(aether::TilePixelFormat::RGBA8));
    TileEntry tile;
    tile.x = 0;
    tile.y = 0;
    tile.pixels = QByteArray(tileBytes, '\0');

    // The object's contents: two roots, the lower one carrying pixels and a mask.
    LayerEntry nestedTop;
    nestedTop.id = QUuid::createUuid();
    nestedTop.name = QStringLiteral("Contents top");
    nestedTop.type = 0; // raster
    nestedTop.tileFormat = aether::TilePixelFormat::RGBA16F;

    LayerEntry nestedBottom;
    nestedBottom.id = QUuid::createUuid();
    nestedBottom.name = QStringLiteral("Contents bottom");
    nestedBottom.type = 0;
    nestedBottom.tiles.append(tile);
    // A nested layer's effect chain travels inside its own entry: the id-keyed
    // effects section is resolved against the model, which it is not part of.
    ruwa::core::effects::LayerEffectState nestedEffect;
    nestedEffect.typeId = QStringLiteral("blur.gaussian");
    nestedEffect.params.insert(QStringLiteral("radius"), 4.5);
    // v31: which space the filter runs in travels with it.
    nestedEffect.contentSpace = true;
    nestedBottom.effects.append(nestedEffect);
    nestedBottom.hasMask = true;
    TileEntry maskTile;
    maskTile.x = 0;
    maskTile.y = 0;
    maskTile.solid = true;
    maskTile.solidColor = 0xFFFFFFFFu;
    nestedBottom.maskTiles.append(maskTile);

    const QUuid contentId = QUuid::createUuid();
    LayerEntry object;
    object.id = QUuid::createUuid();
    object.name = QStringLiteral("Object");
    object.type = 6; // smart
    object.smartContentId = contentId;
    object.tiles.append(tile); // the COMPOSITE, always written
    object.hasSmartDocument = true;
    object.smartDocumentSize = QSize(64, 48);
    object.smartDocumentFormat = 1; // RGBA16F
    object.smartDocumentLayers.append(nestedTop);
    object.smartDocumentLayers.append(nestedBottom);

    // A second instance of the same object carries neither pixels nor contents.
    LayerEntry instance = object;
    instance.id = QUuid::createUuid();
    instance.name = QStringLiteral("Object instance");

    data.rootLayers.append(object);
    data.rootLayers.append(instance);

    const QString path = dir.filePath("nested.rwf");
    ProjectSerializer writer;
    REQUIRE(writer.save(path, data));

    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE(reader.load(path, loaded));
    REQUIRE(loaded.rootLayers.size() == 2);

    const LayerEntry& loadedObject = loaded.rootLayers[0];
    REQUIRE(loadedObject.hasSmartDocument);
    CHECK(loadedObject.smartDocumentSize == QSize(64, 48));
    CHECK(loadedObject.smartDocumentFormat == 1);
    REQUIRE(loadedObject.smartDocumentLayers.size() == 2);
    CHECK(loadedObject.smartDocumentLayers[0].name == QStringLiteral("Contents top"));
    CHECK(loadedObject.smartDocumentLayers[0].id == nestedTop.id);

    const LayerEntry& loadedNestedBottom = loadedObject.smartDocumentLayers[1];
    CHECK(loadedObject.smartDocumentLayers[0].tileFormat == aether::TilePixelFormat::RGBA16F);
    CHECK(loadedNestedBottom.tileFormat == aether::TilePixelFormat::RGBA8);
    REQUIRE(loadedNestedBottom.tiles.size() == 1);
    CHECK(loadedNestedBottom.tiles[0].pixels.size() == tileBytes);
    REQUIRE(loadedNestedBottom.hasMask);
    REQUIRE(loadedNestedBottom.maskTiles.size() == 1);
    CHECK(loadedNestedBottom.maskTiles[0].solid);
    REQUIRE(loadedNestedBottom.effects.size() == 1);
    CHECK(loadedNestedBottom.effects[0].typeId == QStringLiteral("blur.gaussian"));
    CHECK(qFuzzyCompare(
        loadedNestedBottom.effects[0].params.value(QStringLiteral("radius")).toDouble(), 4.5));
    CHECK(loadedNestedBottom.effects[0].instanceId == nestedEffect.instanceId);
    CHECK(loadedNestedBottom.effects[0].contentSpace);

    // The composite travels with the file so it opens without re-compositing...
    CHECK(loadedObject.tiles.size() == 1);
    // ...and the contents are stored exactly once, like the pixels.
    CHECK_FALSE(loaded.rootLayers[1].hasSmartDocument);
    CHECK(loaded.rootLayers[1].smartDocumentLayers.isEmpty());
    CHECK(loaded.rootLayers[1].tiles.isEmpty());
    CHECK(loaded.rootLayers[1].smartContentId == contentId);
}

TEST_CASE("An empty smart content still stores its nested document", "[serializer]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    ProjectData data;
    data.projectName = QStringLiteral("empty-content");
    data.canvasSize = QSize(64, 64);

    // A smart object whose composite is empty (nothing painted in it yet). Its
    // document must not be able to claim the "already written" slot the pixels
    // use, or the instance below would lose the tiles it does carry.
    const QUuid contentId = QUuid::createUuid();
    LayerEntry emptyObject;
    emptyObject.id = QUuid::createUuid();
    emptyObject.name = QStringLiteral("Empty object");
    emptyObject.type = 6;
    emptyObject.smartContentId = contentId;
    emptyObject.hasSmartDocument = true;
    emptyObject.smartDocumentSize = QSize(32, 32);
    LayerEntry nested;
    nested.id = QUuid::createUuid();
    nested.name = QStringLiteral("Contents");
    nested.type = 0;
    emptyObject.smartDocumentLayers.append(nested);

    LayerEntry withPixels = emptyObject;
    withPixels.id = QUuid::createUuid();
    withPixels.hasSmartDocument = false;
    withPixels.smartDocumentLayers.clear();
    TileEntry tile;
    tile.x = 1;
    tile.y = 1;
    tile.pixels
        = QByteArray(static_cast<int>(aether::tileByteSize(aether::TilePixelFormat::RGBA8)), '\0');
    withPixels.tiles.append(tile);

    data.rootLayers.append(emptyObject);
    data.rootLayers.append(withPixels);

    const QString path = dir.filePath("empty-content.rwf");
    ProjectSerializer writer;
    REQUIRE(writer.save(path, data));

    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE(reader.load(path, loaded));
    REQUIRE(loaded.rootLayers.size() == 2);

    CHECK(loaded.rootLayers[0].hasSmartDocument);
    REQUIRE(loaded.rootLayers[0].smartDocumentLayers.size() == 1);
    // The pixels are still there: an empty document did not consume their slot.
    REQUIRE(loaded.rootLayers[1].tiles.size() == 1);
}

TEST_CASE("A v29 file loads with no nested document", "[serializer]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    // A v29 smart layer: the smart identity block is present, the nested
    // document block that follows the tiles in v30 is not.
    const QUuid contentId = QUuid::createUuid();
    const QByteArray layerBlob = buildBytes([&](QDataStream& s) {
        s << quint32(1); // root layer count
        s << QUuid::createUuid();
        s << QStringLiteral("v29 smart");
        s << quint8(0); // nameIsCustom
        s << quint8(6); // type: smart
        s << quint8(1); // visible
        s << quint8(0); // locked
        s << quint8(1); // expanded
        s << qreal(1.0); // opacity
        s << quint8(0); // blendMode
        s << quint8(0); // groupCompositingMode
        s << quint8(0); // displayColorIndex
        s << quint32(0xFFFFFFFFu); // backgroundColorRgba
        s << quint8(0); // backgroundTransparent
        s << quint8(0); // clippedToBelow
        for (int i = 0; i < 11; ++i) {
            s << float(0.0f); // transform block
        }
        s << quint8(0); // hasFreeCorners
        s << quint8(0); // hasDeformMesh
        s << quint8(0); // hasTextPayload
        s << contentId; // v29 smart content identity
        s << QString(); // relative source path
        s << QString(); // absolute source path
        s << quint8(0); // sourceKind
        s << QByteArray(); // sourceHash
        s << quint32(0); // tile count
        s << quint8(0); // hasMask
        s << quint32(0); // child count
    });

    ProjectData source;
    source.projectName = QStringLiteral("v29");
    source.canvasSize = QSize(64, 64);
    const QString sourcePath = dir.filePath("source-for-info.rwf");
    ProjectSerializer writer;
    REQUIRE(writer.save(sourcePath, source));

    QFile sourceFile(sourcePath);
    REQUIRE(sourceFile.open(QIODevice::ReadOnly));
    const QByteArray infoBlob = extractSection(sourceFile.readAll(), kSectionProjectInfo);
    REQUIRE_FALSE(infoBlob.isEmpty());

    const QByteArray file = buildBytes([&](QDataStream& s) {
        writeHeader(s, 29);
        s << quint32(kSectionProjectInfo);
        s << quint64(infoBlob.size());
        s.writeRawData(infoBlob.constData(), infoBlob.size());
        s << quint32(kSectionLayerTree);
        s << quint64(layerBlob.size());
        s.writeRawData(layerBlob.constData(), layerBlob.size());
        s << quint32(kSectionEnd);
        s << quint64(0);
    });

    const QString path = writeTempFile(dir, QStringLiteral("v29.rwf"), file);

    ProjectSerializer reader;
    ProjectData loaded;
    REQUIRE(reader.load(path, loaded));
    REQUIRE(loaded.rootLayers.size() == 1);
    CHECK(loaded.rootLayers[0].smartContentId == contentId);
    // Nothing invented for a file that predates nested documents: the object is
    // flat, and opening its contents wraps the pixels into a document on demand.
    CHECK_FALSE(loaded.rootLayers[0].hasSmartDocument);
    CHECK(loaded.rootLayers[0].smartDocumentLayers.isEmpty());
}
