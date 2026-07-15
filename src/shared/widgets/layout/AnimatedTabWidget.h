// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_ANIMATEDTABWIDGET_H
#define RUWA_UI_WIDGETS_ANIMATEDTABWIDGET_H

#include <QWidget>
#include <QHash>
#include <QUuid>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QEasingCurve>

#include <functional>

class QLabel;
class QGraphicsOpacityEffect;

namespace ruwa::core {
class TabManager;
class BaseTab;
} // namespace ruwa::core

namespace ruwa::ui::widgets {

class DotGridLoadingIndicator;

/**
 * @brief Widget displaying tab content with slide animations
 *
 * This widget is a PASSIVE VIEW - it simply reflects the state of TabManager.
 * All navigation logic is in TabManager; this widget only handles display.
 *
 * Key behaviors:
 * - Listens to TabManager signals
 * - Animates transitions when active tab changes
 * - Handles the visual part of tab closing
 * - Calls TabManager::confirmTabClosed() when close animation finishes
 */
class AnimatedTabWidget : public QWidget {
    Q_OBJECT

public:
    explicit AnimatedTabWidget(QWidget* parent = nullptr);
    ~AnimatedTabWidget() override;

    /// Connect to TabManager - this is required
    void setTabManager(ruwa::core::TabManager* manager);

    /// Animation settings
    void setAnimationDuration(int msec) { m_duration = msec; }
    int animationDuration() const { return m_duration; }
    void setAnimationEasing(QEasingCurve::Type easing) { m_easingCurve = easing; }
    void setThemeLoadingVisible(bool visible);
    bool isThemeLoadingVisible() const;
    /// Swap the spinner for a confirmation checkmark, then fade the overlay out
    /// and hide it. @p onHidden runs once the overlay is fully gone (or
    /// immediately if it was not visible).
    void finishThemeLoadingWithConfirmation(std::function<void()> onHidden);

signals:
    /// Emitted when transition animation completes
    void transitionFinished();

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onTabAdded(ruwa::core::BaseTab* tab);
    void onTabReplaced(ruwa::core::BaseTab* oldTab, ruwa::core::BaseTab* newTab);
    void onTabClosing(ruwa::core::BaseTab* tab, int direction);
    void onTabRemoved(const QUuid& tabId);
    void onActiveTabChanged(ruwa::core::BaseTab* newTab, ruwa::core::BaseTab* oldTab);

private:
    enum class AnimationType {
        None,
        Switch, // Normal tab switch
        CloseOut // Tab being closed slides out
    };

    void slideToTab(ruwa::core::BaseTab* newTab, ruwa::core::BaseTab* oldTab);
    void slideOutClosingTab(ruwa::core::BaseTab* closingTab, ruwa::core::BaseTab* newActiveTab);
    void finishAnimation();
    void positionTab(ruwa::core::BaseTab* tab, int xOffset);
    int determineDirection(ruwa::core::BaseTab* from, ruwa::core::BaseTab* to) const;
    void runDeferredInitAndSlide(ruwa::core::BaseTab* newTab, ruwa::core::BaseTab* oldTab);
    void ensureThemeLoadingOverlay();
    void updateThemeLoadingOverlayColors();

private:
    ruwa::core::TabManager* m_tabManager = nullptr;

    // Track which tabs we're displaying (by UUID)
    QHash<QUuid, ruwa::core::BaseTab*> m_displayedTabs;

    // Animation state
    QParallelAnimationGroup* m_animation = nullptr;
    AnimationType m_animationType = AnimationType::None;
    QUuid m_closingTabId; // Tab being closed (if CloseOut animation)
    int m_closingDirection = 1; // Direction for close animation: 1 = left, -1 = right

    int m_duration = 350;
    QEasingCurve::Type m_easingCurve = QEasingCurve::InOutCubic;
    QWidget* m_themeLoadingOverlay = nullptr;
    DotGridLoadingIndicator* m_themeLoadingIndicator = nullptr;
    QLabel* m_themeLoadingCheck = nullptr; ///< confirmation checkmark (shown on finish)
    QLabel* m_themeLoadingLabel = nullptr;
    QGraphicsOpacityEffect* m_themeLoadingOpacity = nullptr;
    QPropertyAnimation* m_themeLoadingFade = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_ANIMATEDTABWIDGET_H
