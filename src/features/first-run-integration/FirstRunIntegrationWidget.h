// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_FIRSTRUNINTEGRATION_WIDGET_H
#define RUWA_FEATURES_FIRSTRUNINTEGRATION_WIDGET_H

#include <QList>
#include <QPoint>
#include <QVector>
#include <QWidget>

#include <functional>

class QGraphicsOpacityEffect;
class QEvent;
class QHideEvent;
class QLabel;
class QShowEvent;
class QVariantAnimation;
class QVBoxLayout;

namespace ruwa::ui::widgets {
class ThemePreviewWidget;
class WelcomeBannerButton;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::first_run_integration {

class FirstRunIntegrationBackgroundWidget;
class FirstRunIntegrationHeroBodyWidget;
class FirstRunIntegrationHeroTitleWidget;
class FirstRunIntegrationSetupTitleWidget;

class FirstRunIntegrationWidget final : public QWidget {
    Q_OBJECT

public:
    explicit FirstRunIntegrationWidget(QWidget* parent = nullptr);
    ~FirstRunIntegrationWidget() override = default;

    void startPreview();
    void setContentSideMargin(int margin);

signals:
    void completedRequested();
    void customizeRequested();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void setupUi();
    void retranslateUi();
    void updateTheme();
    void updateOverlayMargins();
    void startIntroAnimations();
    void stopIntroAnimations();
    void resetIntroAnimationState();
    void animateIntroProgress(int delayMs, std::function<void(qreal)> applyProgress);
    void animateButtonsIntro(int delayMs);
    void startSetupSectionTransition();
    void startWelcomeSectionTransition();
    void startFinishSectionTransition();
    void startSetupFromFinishTransition();
    void stopPageTransitionAnimations();
    void animatePageTransitionProgress(int delayMs, std::function<void(qreal)> applyProgress);

private:
    FirstRunIntegrationBackgroundWidget* m_background { nullptr };
    QVBoxLayout* m_overlayLayout { nullptr };
    QVBoxLayout* m_nextPageLayout { nullptr };
    QVBoxLayout* m_finishPageLayout { nullptr };
    QWidget* m_welcomePage { nullptr };
    QWidget* m_nextPage { nullptr };
    QWidget* m_finishPage { nullptr };
    QGraphicsOpacityEffect* m_nextPageOpacityEffect { nullptr };
    QGraphicsOpacityEffect* m_finishPageOpacityEffect { nullptr };
    FirstRunIntegrationHeroTitleWidget* m_heroTitle { nullptr };
    FirstRunIntegrationHeroBodyWidget* m_bodyText { nullptr };
    FirstRunIntegrationSetupTitleWidget* m_setupTitle { nullptr };
    FirstRunIntegrationSetupTitleWidget* m_finishTitle { nullptr };
    QLabel* m_languageLabel { nullptr };
    QLabel* m_themeLabel { nullptr };
    QLabel* m_themeDescription { nullptr };
    QLabel* m_finishBody { nullptr };
    QLabel* m_finishAlphaBody { nullptr };
    QWidget* m_actionsSlot { nullptr };
    QWidget* m_actionsContainer { nullptr };
    QGraphicsOpacityEffect* m_actionsOpacityEffect { nullptr };
    ruwa::ui::widgets::WelcomeBannerButton* m_skipSetupButton { nullptr };
    ruwa::ui::widgets::WelcomeBannerButton* m_getStartedButton { nullptr };
    ruwa::ui::widgets::WelcomeBannerButton* m_setupBackButton { nullptr };
    ruwa::ui::widgets::WelcomeBannerButton* m_setupContinueButton { nullptr };
    ruwa::ui::widgets::WelcomeBannerButton* m_finishBackButton { nullptr };
    ruwa::ui::widgets::WelcomeBannerButton* m_startCreatingButton { nullptr };
    QVector<ruwa::ui::widgets::ThemePreviewWidget*> m_setupThemePreviews;
    QList<QVariantAnimation*> m_introAnimations;
    QList<QVariantAnimation*> m_pageTransitionAnimations;
    QPoint m_actionsFinalPosition;
    QPoint m_welcomePageFinalPosition;
    QPoint m_nextPageFinalPosition;
    QPoint m_finishPageFinalPosition;
    int m_contentSideMargin { 0 };
    int m_introGeneration { 0 };
    int m_pageTransitionGeneration { 0 };
    bool m_actionsFinalPositionKnown { false };
    bool m_pageTransitionRunning { false };
};

} // namespace ruwa::ui::first_run_integration

#endif // RUWA_FEATURES_FIRSTRUNINTEGRATION_WIDGET_H
