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
