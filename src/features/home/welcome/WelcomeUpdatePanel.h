// SPDX-License-Identifier: MPL-2.0

// WelcomeUpdatePanel.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_WELCOMEUPDATEPANEL_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_WELCOMEUPDATEPANEL_H

#include <QWidget>
#include <QString>
#include <QPixmap>

class QLabel;
class QEvent;
class QVBoxLayout;
class QHBoxLayout;

namespace ruwa::ui::widgets {

class WelcomeBannerButton;

/**
 * @brief Side panel shown inside WelcomeBanner when a newer version is available.
 *
 * Occupies the right third of the banner as a self-contained rounded card with a
 * short message and two actions: open the Updates settings, or dismiss ("No thanks").
 * Painting is independent of the banner background image — only the geometry is
 * coordinated by WelcomeBanner (see its split animation).
 */
class WelcomeUpdatePanel : public QWidget {
    Q_OBJECT

public:
    explicit WelcomeUpdatePanel(QWidget* parent = nullptr);
    ~WelcomeUpdatePanel() override = default;

    /// Version string shown in the message (e.g. "0.1.7"). Empty = generic copy.
    void setUpdateVersion(const QString& version);

    /// Short summary of the release (from the GitHub release body). Empty = generic copy.
    void setReleaseDescription(const QString& description);

signals:
    /// "Update" pressed — caller should navigate to the Updates settings page.
    void updateRequested();
    /// "No thanks" pressed — caller should remember the dismissal.
    void dismissed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUI();
    void loadBackgroundImage();
    void rebuildBlurredBackdrop();
    void retranslateUi();
    void updateScaledSizes();
    void applyThemeColors();

private slots:
    void onThemeChanged();

private:
    QVBoxLayout* m_mainLayout { nullptr };
    QLabel* m_eyebrowLabel { nullptr };
    QLabel* m_titleLabel { nullptr };
    QLabel* m_descriptionLabel { nullptr };
    QWidget* m_buttonContainer { nullptr };
    QHBoxLayout* m_buttonLayout { nullptr };
    WelcomeBannerButton* m_updateButton { nullptr };
    WelcomeBannerButton* m_dismissButton { nullptr };
    QString m_updateVersion;
    QString m_releaseDescription;
    QPixmap m_backgroundImage;
    /// Cached blurred backdrop scaled to the current size (rebuilt on resize).
    QPixmap m_blurredBackdrop;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_WELCOMEUPDATEPANEL_H
