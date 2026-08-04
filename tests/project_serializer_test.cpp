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
    tile.pixels
        = QByteArray(static_cast<int>(aether::tileByteSize(aether::TilePixelFormat::RGBA8)), '\0');

    // Two layers showing one smart object: same contentId, same pixels.
    LayerEntry first;
    first.id = QUuid::createUuid();
    first.name = QStringLiteral("Object");
    first.type = 6; // smart
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
    CHECK(QFileInfo(loaded.rootLayers[0].smartSourcePath)
        == QFileInfo(movedDir.filePath(assetName)));
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
