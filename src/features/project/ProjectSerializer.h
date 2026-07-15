// SPDX-License-Identifier: MPL-2.0

// ProjectSerializer.h
#ifndef RUWA_CORE_SERIALIZATION_PROJECTSERIALIZER_H
#define RUWA_CORE_SERIALIZATION_PROJECTSERIALIZER_H

#include "ProjectData.h"

#include <QString>
#include <QByteArray>
#include <QDataStream>

namespace ruwa::core::serialization {

// ============================================================================
// Section types — extensible via new enum values
// ============================================================================

enum class SectionType : quint32 {
    End = 0,
    ProjectInfo = 1,
    LayerTree = 2,
    LayerEffects = 3,
    // Future:
    // CanvasPixels = 4,
    // BrushPresets = 5,
    // Thumbnail    = 6,
};

// ============================================================================
// ProjectSerializer
// ============================================================================

/**
 * @brief Binary serializer for Ruwa project files (.rwf/.uwa)
 *
 * File layout:
 *   [Magic "UWA\0"]  [Version u32]  [Section]...  [End section]
 *
 * Each section:
 *   [SectionType u32]  [DataSize u64]  [Data bytes...]
 *
 * Unknown sections are skipped by size, so older readers
 * can open files written by newer versions.
 */
class ProjectSerializer {
public:
    ProjectSerializer() = default;

    // === Main API ===

    bool save(const QString& filePath, const ProjectData& data);
    bool load(const QString& filePath, ProjectData& data);

    /// Quick header check without full load
    bool validate(const QString& filePath) const;

    QString lastError() const { return m_lastError; }

    // === Static helpers ===

    static QString fileExtension() { return QStringLiteral("rwf"); }
    static QString fileFilter() { return QStringLiteral("Ruwa Projects (*.rwf *.uwa)"); }
    static QString defaultFileName(const QString& projectName);

private:
    // Section writers — each returns a self-contained blob
    QByteArray writeProjectInfo(const ProjectData& data) const;
    QByteArray writeLayerTree(const ProjectData& data) const;
    QByteArray writeLayerEffects(const ProjectData& data) const;
    void writeLayerEntry(QDataStream& out, const LayerEntry& layer) const;
    void writeLayerEffectState(
        QDataStream& out, const ruwa::core::effects::LayerEffectState& state) const;

    // Section readers
    bool readProjectInfo(const QByteArray& blob, ProjectData& data) const;
    bool readLayerTree(const QByteArray& blob, ProjectData& data) const;
    bool readLayerEffects(const QByteArray& blob, ProjectData& data) const;
    LayerEntry readLayerEntry(QDataStream& in, quint32 version, quint32 tileSize,
        aether::TilePixelFormat contentFormat, bool legacyUntaggedTiles, int depth) const;
    ruwa::core::effects::LayerEffectState readLayerEffectState(QDataStream& in) const;

    // Low-level helpers
    bool readHeader(QDataStream& in, quint32& version) const;

    mutable QString m_lastError;

    static constexpr char MAGIC[4] = { 'U', 'W', 'A', '\0' };
};

} // namespace ruwa::core::serialization

#endif // RUWA_CORE_SERIALIZATION_PROJECTSERIALIZER_H
