// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_BRUSHENGINE_BRUSHMODULE_H
#define RUWA_CORE_BRUSHENGINE_BRUSHMODULE_H

#include "features/brush/engine/BrushStrokeReplay.h"
#include "features/brush/manager/BrushSettingDefs.h"

#include <QColor>
#include <QImage>
#include <QString>
#include <QVariantMap>

#include <memory>

namespace ruwa::core::brushes {

using BrushEngineId = QString;

inline constexpr auto kPixelBrushEngineId = "pixel";
inline constexpr int kPixelBrushEngineVersion = 7;

enum class BrushToolMode {
    Paint = 0,
    Erase,
    Blur,
    Smudge,
};

struct BrushEngineCapabilities {
    bool previewRendering = false;
    bool cursorStamp = false;
    bool strokeReplay = false;
};

struct BrushEngineDescriptor {
    BrushEngineId id;
    QString displayName;
    QVector<BrushTabDef> settingsTabs;
    BrushEngineCapabilities capabilities;
};

struct BrushSessionConfig {
    BrushToolMode toolMode = BrushToolMode::Paint;
    int engineVersion = kPixelBrushEngineVersion;
    QVariantMap settings;
};

struct BrushPreviewRequest {
    BrushEngineId engineId;
    int engineVersion = kPixelBrushEngineVersion;
    QVariantMap settings;
    qreal sizeNorm = 1.0;
    qreal opacityNorm = 1.0;
    QColor color = Qt::black;
    int width = 0;
    int height = 0;
    bool strokePreview = true;
    int strokeSegmentCountHint = 0;
};

struct BrushCursorRequest {
    BrushEngineId engineId;
    int engineVersion = kPixelBrushEngineVersion;
    QVariantMap settings;
    qreal sizeNorm = 1.0;
    QColor color = Qt::black;
    int size = 0;
};

class IBrushEngineSession {
public:
    virtual ~IBrushEngineSession() = default;

    virtual BrushEngineId engineId() const = 0;
    virtual int engineVersion() const = 0;

    virtual std::shared_ptr<IEditableBrushStrokeReplayData> activeStrokeReplayData() { return {}; }
};

class IBrushEngineModule {
public:
    virtual ~IBrushEngineModule() = default;

    virtual BrushEngineDescriptor descriptor() const = 0;
    virtual int currentVersion() const = 0;
    virtual QVariantMap defaultSettings() const = 0;
    virtual QVariantMap normalizeSettings(const QVariantMap& settings) const = 0;
    virtual QVariantMap upgradeSettings(int fromVersion, const QVariantMap& settings) const = 0;
    virtual std::unique_ptr<IBrushEngineSession> createSession(
        const BrushSessionConfig& config) const
        = 0;
    virtual QImage renderPreview(const BrushPreviewRequest& request) const = 0;

    virtual QImage buildCursorStamp(const BrushCursorRequest& request) const
    {
        Q_UNUSED(request);
        return {};
    }
};

} // namespace ruwa::core::brushes

#endif // RUWA_CORE_BRUSHENGINE_BRUSHMODULE_H
