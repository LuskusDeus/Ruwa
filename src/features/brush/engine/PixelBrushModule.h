// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_BRUSHENGINE_PIXELBRUSHMODULE_H
#define RUWA_CORE_BRUSHENGINE_PIXELBRUSHMODULE_H

#include "features/brush/engine/BrushModule.h"
#include "features/brush/manager/BrushSettings.h"
#include "shared/tiles/TileBrush.h"

namespace ruwa::core::brushes {

class PixelBrushSession final : public IBrushEngineSession {
public:
    explicit PixelBrushSession(const BrushSessionConfig& config);

    BrushEngineId engineId() const override;
    int engineVersion() const override;
    std::shared_ptr<IEditableBrushStrokeReplayData> activeStrokeReplayData() override;

    aether::TileBrush& brush() { return m_brush; }
    const aether::TileBrush& brush() const { return m_brush; }

private:
    BrushEngineId m_engineId = QLatin1String(kPixelBrushEngineId);
    int m_engineVersion = kPixelBrushEngineVersion;
    aether::TileBrush m_brush;
    std::shared_ptr<IEditableBrushStrokeReplayData> m_activeReplayData;
};

class PixelBrushModule final : public IBrushEngineModule {
public:
    BrushEngineDescriptor descriptor() const override;
    int currentVersion() const override;
    QVariantMap defaultSettings() const override;
    QVariantMap normalizeSettings(const QVariantMap& settings) const override;
    QVariantMap upgradeSettings(int fromVersion, const QVariantMap& settings) const override;
    std::unique_ptr<IBrushEngineSession> createSession(
        const BrushSessionConfig& config) const override;
    QImage renderPreview(const BrushPreviewRequest& request) const override;
    QImage buildCursorStamp(const BrushCursorRequest& request) const override;

    static QVariantMap settingsToVariantMap(const BrushSettingsData& settings);
    static BrushSettingsData settingsFromVariantMap(const QVariantMap& settings);
    static BrushSettingsData normalizeCompatibilitySettings(const BrushSettingsData& settings);
};

} // namespace ruwa::core::brushes

#endif // RUWA_CORE_BRUSHENGINE_PIXELBRUSHMODULE_H
