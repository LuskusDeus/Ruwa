// SPDX-License-Identifier: MPL-2.0

// UpdatesSettingsWidget.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_UPDATESSETTINGSWIDGET_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_UPDATESSETTINGSWIDGET_H

#include <QPixmap>
#include <QWidget>

class QEvent;
class QLabel;
class QVBoxLayout;
class QHBoxLayout;

namespace ruwa::ui::widgets {

class ProgressSlider;
class WelcomeBannerButton;

/**
 * @brief Update state for the updates banner
 */
enum class UpdateState {
    UpToDate, ///< Program is up to date - gray, inactive, check icon
    Downloading, ///< Download in progress - gray, "Downloading"
    UpdateAvailable, ///< Update available - accent, click to download
    ReadyToRestart ///< Update downloaded - accent, click to restart
};

/**
 * @brief Updates banner widget - WelcomeBanner-style design with stateful button
 *
 * Title and subtitle are painted directly into a cached background pixmap
 * to avoid per-frame child widget compositing during scrolling.
 */
class UpdatesSettingsWidget : public QWidget {
    Q_OBJECT

public:
    explicit UpdatesSettingsWidget(QWidget* parent = nullptr);
    ~UpdatesSettingsWidget() override = default;

    void setUpdateState(UpdateState state);
    UpdateState updateState() const { return m_updateState; }

    void setDownloadProgress(int percent);
    void setUpdateVersion(const QString& version);
    void setLastCheckedMinutesAgo(int minutes);
    void setReleaseDescription(const QString& description);
    void setRecheckInProgress(bool inProgress);

    /// Refresh strings after language change (also triggered by QEvent::LanguageChange).
    void retranslateUi();

signals:
    void updateActionClicked();
    void releaseNotesClicked();
    void whatsNewClicked();
    void updateRecheckClicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void setupUI();
    void loadBackgroundImage();
    void updateScaledSizes();
    void updateThemeColors();
    void applyState();
    void updateProgressSliderPosition();
    void invalidateBackgroundCache();
    void rebuildBackgroundCache();

private slots:
    void onThemeChanged();

private:
    QVBoxLayout* m_mainLayout { nullptr };
    QHBoxLayout* m_buttonLayout { nullptr };
    QWidget* m_buttonContainer { nullptr };
    WelcomeBannerButton* m_primaryButton { nullptr };
    WelcomeBannerButton* m_secondaryButton { nullptr };
    ProgressSlider* m_progressSlider { nullptr };
    QLabel* m_progressValueLabel { nullptr };
    QPixmap m_backgroundImage;
    QPixmap m_cachedBackground;
    QString m_titleText;
    QString m_descriptionText;
    QString m_updateVersion;
    QString m_releaseDescription;
    UpdateState m_updateState { UpdateState::UpToDate };
    int m_lastCheckedMinutesAgo { 0 };
    int m_downloadProgress { 0 };
    bool m_recheckInProgress { false };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_UPDATESSETTINGSWIDGET_H
