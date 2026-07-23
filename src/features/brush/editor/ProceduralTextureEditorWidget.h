// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WINDOWS_BRUSHEDITOR_PROCEDURALTEXTUREEDITORWIDGET_H
#define RUWA_UI_WINDOWS_BRUSHEDITOR_PROCEDURALTEXTUREEDITORWIDGET_H

#include "features/brush/manager/BrushSettings.h"
#include "shared/widgets/BaseStyledPanel.h"

#include <QFutureWatcher>

class QImage;

namespace ruwa::ui::widgets {
class BrushSettingsWidget;
class ImageDropdownSelector;
class SmoothScrollArea;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::windows {

class ProceduralTexturePreviewWidget;

class ProceduralTextureEditorWidget final : public widgets::BaseStyledPanel {
    Q_OBJECT

public:
    using BrushSettingsData = ruwa::core::brushes::BrushSettingsData;

    explicit ProceduralTextureEditorWidget(QWidget* parent = nullptr);

    const QVector<widgets::BrushSettingsWidget*>& typeParameterSettingsWidgets() const
    {
        return m_typeParameterSettingsWidgets;
    }

    void setSettings(const BrushSettingsData& settings);

signals:
    void textureTypeChanged(int textureType);

private:
    void setActiveTextureType(int textureType);
    void updateLayoutMetrics();
    void updateSeparatorStyle();
    void updatePreview();
    void dispatchPreviewRender();
    void handlePreviewRenderFinished();

    BrushSettingsData m_settings;
    ProceduralTexturePreviewWidget* m_previewWidget = nullptr;
    QWidget* m_settingsColumn = nullptr;
    widgets::ImageDropdownSelector* m_typeSelector = nullptr;
    QWidget* m_separator = nullptr;
    widgets::SmoothScrollArea* m_settingsScrollArea = nullptr;
    QWidget* m_typeSettingsContent = nullptr;
    QVector<widgets::BrushSettingsWidget*> m_typeParameterSettingsWidgets;
    QFutureWatcher<QImage>* m_previewRenderWatcher = nullptr;
    BrushSettingsData m_pendingPreviewSettings;
    bool m_hasPendingPreviewRender = false;
    bool m_previewRenderInFlight = false;
};

} // namespace ruwa::ui::windows

#endif // RUWA_UI_WINDOWS_BRUSHEDITOR_PROCEDURALTEXTUREEDITORWIDGET_H
