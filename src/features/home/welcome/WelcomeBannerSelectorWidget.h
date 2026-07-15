// SPDX-License-Identifier: MPL-2.0

// WelcomeBannerSelectorWidget.h
#ifndef RUWA_UI_WIDGETS_WELCOME_WELCOMEBANNERSELECTORWIDGET_H
#define RUWA_UI_WIDGETS_WELCOME_WELCOMEBANNERSELECTORWIDGET_H

#include "features/settings/BaseSettingsWidget.h"

#include <QPointer>
#include <QStringList>
#include <QVector>

class QEvent;
class QHBoxLayout;
class QLabel;

namespace ruwa::ui::widgets {

class WelcomeBannerPreviewWidget;
class WelcomeBannerAddImageWidget;
class WelcomeBannerCropOverlay;
class SegmentedOptionSelector;

/**
 * @brief Welcome banner image picker: built-in and custom thumbnails, add image, random vs fixed.
 */
class WelcomeBannerSelectorWidget : public BaseSettingsWidget {
    Q_OBJECT

public:
    explicit WelcomeBannerSelectorWidget(QWidget* parent = nullptr);
    ~WelcomeBannerSelectorWidget() override = default;

    void loadFromSettings();
    void setSelectedImageKey(const QString& key);
    void setRandomize(bool randomize);

protected:
    void setupContent() override;
    void updateThemeColors() override;
    void changeEvent(QEvent* event) override;

public:
    void retranslateUi();

private:
    void updateScaledSizes();
    void rebuildPreviews();
    void syncSelectionVisuals();
    void onPreviewClicked(const QString& key);
    void onModeIndexChanged(int index);
    void onAddImageClicked();
    /// Show the crop overlay for the next queued imported image (one at a time).
    void processNextPendingCrop();
    void onTextColorModeChanged(int index);
    void removeCustomBannerImage(const QString& path);
    /// Re-open the crop overlay for an already-added custom image to change its crop.
    void editCustomBannerImageCrop(const QString& path);
    /// Replace default HBox with a grid so the divider can span both columns (full row width).
    void reinstallBannerRootGridLayout();

private:
    QWidget* m_previewContainer { nullptr };
    QHBoxLayout* m_previewLayout { nullptr };
    QLabel* m_separatorLabel { nullptr };
    QVector<WelcomeBannerPreviewWidget*> m_previews;
    WelcomeBannerAddImageWidget* m_addTile { nullptr };

    QLabel* m_modeLabel { nullptr };
    SegmentedOptionSelector* m_modeSelector { nullptr };
    QWidget* m_modeRowHost { nullptr };

    QLabel* m_textColorLabel { nullptr };
    SegmentedOptionSelector* m_textColorSelector { nullptr };
    QWidget* m_textColorRowHost { nullptr };

    QWidget* m_dividerWrap { nullptr };
    QWidget* m_sectionDivider { nullptr };

    QStringList m_customPaths;
    /// Custom paths last used when building m_previews (for loadFromSettings skip vs rebuild).
    QStringList m_previewRowCustomPaths;
    /// Imported images awaiting a crop selection, processed front-to-back one at a time.
    QStringList m_pendingCropQueue;
    QPointer<WelcomeBannerCropOverlay> m_cropOverlay;
    QString m_selectedKey;
    bool m_randomize { true };
    int m_textColorMode { 0 };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_WELCOME_WELCOMEBANNERSELECTORWIDGET_H
