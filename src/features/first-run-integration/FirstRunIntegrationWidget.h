// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_FIRSTRUNINTEGRATION_WIDGET_H
#define RUWA_FEATURES_FIRSTRUNINTEGRATION_WIDGET_H

#include <QPointer>
#include <QWidget>

class QEvent;
class QFrame;
class QHideEvent;
class QLabel;
class QResizeEvent;
class QShowEvent;
class QVBoxLayout;

namespace ruwa::ui::widgets {
class SettingsChoice;
class SettingsToggle;
class SmoothScrollArea;
class ThemeSelectorWidget;
class WelcomeBannerButton;
class WidgetFadeInOverlay;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::first_run_integration {

/**
 * @brief Scrollable first-run personalization page.
 *
 * Presents the first-run hero and the application settings selected for the
 * onboarding flow.
 */
class FirstRunIntegrationWidget final : public QWidget {
    Q_OBJECT

public:
    explicit FirstRunIntegrationWidget(QWidget* parent = nullptr);
    ~FirstRunIntegrationWidget() override = default;

    void setContentSideMargin(int margin);

signals:
    void completedRequested();

protected:
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setupUi();
    void retranslateUi();
    void ensureAppearanceOverlay();
    void startAppearanceAnimation();
    void updateTheme();
    void updateContentMargins();
    void updateHeroHeight();

private:
    QVBoxLayout* m_pageLayout { nullptr };
    QVBoxLayout* m_contentLayout { nullptr };
    ruwa::ui::widgets::SmoothScrollArea* m_scrollArea { nullptr };
    QFrame* m_heroSection { nullptr };
    QWidget* m_heroGlassPanel { nullptr };
    QLabel* m_heroLogo { nullptr };
    QLabel* m_heroTitle { nullptr };
    QLabel* m_heroDescription { nullptr };
    ruwa::ui::widgets::WelcomeBannerButton* m_startCustomizationButton { nullptr };
    ruwa::ui::widgets::WelcomeBannerButton* m_skipCustomizationButton { nullptr };
    QLabel* m_appearanceTitle { nullptr };
    ruwa::ui::widgets::ThemeSelectorWidget* m_themeSelector { nullptr };
    ruwa::ui::widgets::SettingsChoice* m_uiScaleChoice { nullptr };
    ruwa::ui::widgets::SettingsChoice* m_topBarTabAlignmentChoice { nullptr };
    QLabel* m_editorTitle { nullptr };
    ruwa::ui::widgets::SettingsChoice* m_autoSaveChoice { nullptr };
    ruwa::ui::widgets::SettingsToggle* m_quickshapesToggle { nullptr };
    QLabel* m_performanceTitle { nullptr };
    ruwa::ui::widgets::SettingsChoice* m_undoMemoryChoice { nullptr };
    ruwa::ui::widgets::SettingsChoice* m_tabletBackendChoice { nullptr };
    QFrame* m_finishSection { nullptr };
    QLabel* m_finishDescription { nullptr };
    ruwa::ui::widgets::WelcomeBannerButton* m_finishButton { nullptr };
    QPointer<ruwa::ui::widgets::WidgetFadeInOverlay> m_appearanceOverlay;
    int m_contentSideMargin { 0 };
    bool m_appearanceAnimationStarted { false };
};

} // namespace ruwa::ui::first_run_integration

#endif // RUWA_FEATURES_FIRSTRUNINTEGRATION_WIDGET_H
