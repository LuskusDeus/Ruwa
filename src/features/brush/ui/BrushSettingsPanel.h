// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WORKSPACE_PANELS_BRUSHSETTINGSPANEL_H
#define RUWA_UI_WORKSPACE_PANELS_BRUSHSETTINGSPANEL_H

#include "features/brush/manager/BrushSettings.h"
#include "shell/docking/widgets/DockPanel.h"

#include <QPointer>
#include <QString>
#include <QVector>

class QLabel;
class QVBoxLayout;

namespace ruwa::ui::windows::layout_internal {
class DotPreviewCanvas;
}

namespace ruwa::ui::widgets {
class BaseStyledPanel;
class BrushSettingsWidget;
class SmoothScrollArea;
}

namespace ruwa::ui::workspace {

class CanvasPanel;
class ToolButton;

/**
 * Dockable quick editor for the starred settings of the brush used by the
 * current painting tool.
 */
class BrushSettingsPanel final : public ruwa::ui::docking::DockPanel {
    Q_OBJECT

public:
    explicit BrushSettingsPanel(QWidget* parent = nullptr);
    ~BrushSettingsPanel() override;

    void setCanvasPanel(CanvasPanel* canvasPanel);
    CanvasPanel* canvasPanel() const;

signals:
    void brushEditorRequested(const QString& brushId);

protected:
    QWidget* createContent() override;
    void onThemeChanged() override;

private:
    using BrushSettingsData = ruwa::core::brushes::BrushSettingsData;

    void setCurrentBrush(const QString& brushId);
    void rebuildSettings();
    void refreshSectionValues(const BrushSettingsData& settings);
    void applySectionEdit(ruwa::ui::widgets::BrushSettingsWidget* editedSection);
    void updateHeader();
    void updateDabPreview(const BrushSettingsData* settings);
    void updateEmptyState(const QString& text);
    void refreshScrollGeometry();

    QPointer<CanvasPanel> m_canvasPanel;
    QString m_currentBrushId;
    BrushSettingsData m_currentSettings;

    QWidget* m_contentWidget = nullptr;
    ruwa::ui::widgets::BaseStyledPanel* m_headerCard = nullptr;
    ruwa::ui::windows::layout_internal::DotPreviewCanvas* m_dabPreview = nullptr;
    QLabel* m_headerCaptionLabel = nullptr;
    QLabel* m_brushNameLabel = nullptr;
    ToolButton* m_openEditorButton = nullptr;
    ruwa::ui::widgets::SmoothScrollArea* m_scrollArea = nullptr;
    QWidget* m_scrollContent = nullptr;
    QVBoxLayout* m_scrollLayout = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QVector<ruwa::ui::widgets::BrushSettingsWidget*> m_sectionWidgets;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_BRUSHSETTINGSPANEL_H
