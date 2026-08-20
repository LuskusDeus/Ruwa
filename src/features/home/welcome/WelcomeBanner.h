// SPDX-License-Identifier: MPL-2.0

// WelcomeBanner.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_WELCOMEBANNER_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_WELCOMEBANNER_H

#include <QWidget>
#include <QImage>
#include <QPixmap>
#include <QRectF>
#include <QString>

class QLabel;
class QEvent;
class QVBoxLayout;
class QHBoxLayout;
class QPropertyAnimation;
class QTimer;

namespace ruwa::ui::widgets {

class WelcomeBannerButton;
class WelcomeUpdatePanel;

/**
 * @brief Welcome banner — background image (no fade) + title / actions
 *
 * When an update is available the banner splits: the background image occupies
 * the left ~2/3 (cropped, not rescaled) and a WelcomeUpdatePanel slides into the
 * right ~1/3 with a gap between them. The split is driven by the animated
 * @c splitProgress property (0 = banner only, 1 = fully revealed panel).
 */
class WelcomeBanner : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal splitProgress READ splitProgress WRITE setSplitProgress)

public:
    explicit WelcomeBanner(QWidget* parent = nullptr);
    ~WelcomeBanner() override = default;

    /// Reload background from SettingsManager (e.g. after changing banner in settings).
    void reloadFromSettings();

    /// Reveal the update panel (animated). @p version and @p description fill the
    /// panel message (description is the short release summary from GitHub).
    void showUpdatePanel(
        const QString& version = QString(), const QString& description = QString());
    /// Hide the update panel (animated).
    void hideUpdatePanel();
    bool isUpdatePanelVisible() const { return m_updatePanelRequested; }

    qreal splitProgress() const { return m_splitProgress; }
    void setSplitProgress(qreal progress);

signals:
    void createProjectClicked();
    void openProjectClicked();
    /// "Update" pressed in the update panel.
    void updateActionRequested();
    /// "No thanks" pressed in the update panel.
    void updateDismissed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void retranslateUi();
    void setupUI();
    void loadBackgroundImage();
    void applyBackgroundUiHints();
    void updateScaledSizes();
    void updateThemeColors();
    void applyBannerLabelColors();
    /// Auto-detected bright banner image, optionally flipped by user Basic/Inverted setting.
    bool effectiveLightBannerUi() const;
    /// Reposition the panel and recompute the banner card width from m_splitProgress.
    void updateSplitLayout();
    /// Width of the visible banner card (left region) for the current split progress.
    int bannerCardWidth() const;
    /// Start the split animation toward @p target (0 = hidden, 1 = revealed).
    void animateSplitTo(qreal target);
    /// Draws the banner card: background image, the glass plate under the Open
    /// button, and the rounded mask. @p plateRect is the button in card
    /// coordinates, empty when there is no button to put glass under.
    QImage composeBannerCard(int cardWidth, const QRect& plateRect, bool lightUi) const;
    /// (Re)start the delayed reveal countdown, anchored to the current on-screen show.
    void startRevealCountdown();

private slots:
    void onThemeChanged();

private:
    QVBoxLayout* m_mainLayout { nullptr };
    QHBoxLayout* m_buttonLayout { nullptr };
    QLabel* m_titleLabel { nullptr };
    QLabel* m_subtitleLabel { nullptr };
    QWidget* m_buttonContainer { nullptr };
    WelcomeBannerButton* m_createButton { nullptr };
    WelcomeBannerButton* m_openButton { nullptr };
    WelcomeUpdatePanel* m_updatePanel { nullptr };
    QPropertyAnimation* m_splitAnimation { nullptr };
    QTimer* m_revealTimer { nullptr };
    QPixmap m_backgroundImage;
    /// Region of m_backgroundImage to display, normalized to [0,1]. Full rect = whole image.
    QRectF m_backgroundCropNorm { 0.0, 0.0, 1.0, 1.0 };
    /// Bright banner asset uses textOnPrimary palette + inverted buttons.
    bool m_lightBackgroundImage { false };
    /// 0 = panel fully hidden, 1 = panel fully revealed.
    qreal m_splitProgress { 0.0 };
    /// Whether the panel is requested to be shown (target state of the animation).
    bool m_updatePanelRequested { false };
    /// True once the reveal has actually fired; stops it from replaying on re-show.
    bool m_updatePanelRevealed { false };
    /// Last composed card and the key describing what it was composed from.
    /// The glass is a GPU round trip, so repaints that change none of those
    /// inputs - every hover frame of the buttons drawn on top of it - reuse it.
    QImage m_cardCache;
    QString m_cardCacheKey;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_WELCOMEBANNER_H
