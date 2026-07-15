// SPDX-License-Identifier: MPL-2.0

// NewProjectContent.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_NEWPROJECTCONTENT_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_NEWPROJECTCONTENT_H

#include "../HomePageContent.h"
#include "shared/tiles/TileFormat.h"
#include <QColor>
#include <QMap>
#include <QVector>

class QLabel;
class QEvent;
class QMouseEvent;
class QResizeEvent;
class QShowEvent;
class QVBoxLayout;
class QSpacerItem;
class QWidget;

namespace ruwa::ui::widgets {

class ProjectSettingsField;
class ProjectPresetCard;
class CanvasThumbnail;
class AspectRatioLockButton;
class CapsuleButton;
class AnimatedStackedWidget;
class SegmentedOptionSelector;
class ColorInputButton;

/**
 * @brief New Project content panel
 *
 * Layout (42/58 split):
 * - Left (42%): Canvas thumbnail | Name | Canvas mode | Width/lock/height | Create
 * - Right (58%): Category capsule tabs + Preset cards
 */
class NewProjectContent : public HomePageContent {
    Q_OBJECT

public:
    explicit NewProjectContent(QWidget* parent = nullptr);
    ~NewProjectContent() override = default;

    QString title() const override { return tr("New Project"); }

    QString projectName() const;
    QSize canvasSize() const;
    bool infiniteCanvasEnabled() const;
    QString colorMode() const;
    QColor backgroundColor() const;
    aether::TilePixelFormat tileFormat() const;

signals:
    void projectCreateRequested(const QString& name, const QSize& size, bool infiniteCanvasEnabled,
        const QString& colorMode, const QColor& backgroundColor,
        aether::TilePixelFormat tileFormat);
    void colorPickerRequested(const QColor& initialColor, QWidget* sourceButton);

protected:
    void setupContent() override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void onPresetSelected(const QString& presetName);
    void onWidthChanged(int width);
    void onHeightChanged(int height);
    void onAspectLockToggled(bool locked);
    void onCanvasBoundsSelectionChanged(int index);
    void onThemeChanged();

private:
    void retranslateUi();
    void createPresets();
    void createSettingsPanel(QWidget* fieldParent);
    void updateDimensionsFromPreset(const QSize& dimensions);
    void updateThumbnail();
    void clearAllInputFocus();
    void updateThemeColors();
    void updateScaledSizes();
    void syncLockColumnLayout();
    void updateMemoryLabel();
    void syncLockedHeightFromWidth(int width);
    void syncLockedWidthFromHeight(int height);
    void updateLockedAspectRatio();
    void clearSelectedPreset();
    void setActivePresetCategory(int index);
    QString formatRatio(const QSize& size) const;
    void syncDimensionFieldsEnabledState();

private:
    CanvasThumbnail* m_canvasThumbnail { nullptr };

    ProjectSettingsField* m_projectNameField { nullptr };
    ProjectSettingsField* m_widthField { nullptr };
    ProjectSettingsField* m_heightField { nullptr };
    ColorInputButton* m_backgroundColorInput { nullptr };
    QWidget* m_backgroundColorSection { nullptr };
    QLabel* m_backgroundColorTitleLabel { nullptr };
    QWidget* m_canvasBoundsSection { nullptr };
    QLabel* m_canvasBoundsTitleLabel { nullptr };
    SegmentedOptionSelector* m_canvasBoundsSelector { nullptr };
    QWidget* m_bitDepthSection { nullptr };
    QLabel* m_bitDepthTitleLabel { nullptr };
    SegmentedOptionSelector* m_bitDepthSelector { nullptr };

    QMap<QString, ProjectPresetCard*> m_presetCards;
    QMap<QString, int> m_presetCategoryIndices;
    QString m_selectedPreset;
    QVector<CapsuleButton*> m_categoryButtons;

    QLabel* m_titleLabel { nullptr };
    QWidget* m_presetsPanel { nullptr };
    AnimatedStackedWidget* m_presetsStack { nullptr };
    CapsuleButton* m_createButton { nullptr };
    AspectRatioLockButton* m_aspectLockButton { nullptr };
    QWidget* m_lockColumn { nullptr };
    QVBoxLayout* m_lockColumnLayout { nullptr };
    QSpacerItem* m_lockColumnTopSpacer { nullptr };
    qreal m_lockedAspectRatio { 0.0 };
    bool m_syncingLockedDimensions { false };
    QColor m_backgroundColor { Qt::white };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_NEWPROJECTCONTENT_H
