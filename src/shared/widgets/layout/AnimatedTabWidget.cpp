// SPDX-License-Identifier: MPL-2.0

#include "AnimatedTabWidget.h"
#include "shell/tab-system/TabManager.h"
#include "shell/tab-system/BaseTab.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/widgets/DotGridLoadingIndicator.h"
#include "shared/resources/IconProvider.h"

#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <utility>

namespace ruwa::ui::widgets {

AnimatedTabWidget::AnimatedTabWidget(QWidget* parent)
    : QWidget(parent)
{
}

AnimatedTabWidget::~AnimatedTabWidget()
{
    if (m_animation) {
        m_animation->stop();
        delete m_animation;
    }
}

void AnimatedTabWidget::setThemeLoadingVisible(bool visible)
{
    ensureThemeLoadingOverlay();
    if (!m_themeLoadingOverlay) {
        return;
    }

    updateThemeLoadingOverlayColors();
    m_themeLoadingOverlay->setGeometry(rect());

    // Reset any in-flight fade / confirmation state so the overlay starts clean.
    if (m_themeLoadingFade) {
        m_themeLoadingFade->stop();
    }
    if (m_themeLoadingOpacity) {
        m_themeLoadingOpacity->setOpacity(1.0);
    }
    if (m_themeLoadingCheck) {
        m_themeLoadingCheck->hide();
    }
    if (m_themeLoadingIndicator) {
        m_themeLoadingIndicator->show();
    }

    m_themeLoadingOverlay->setVisible(visible);

    if (visible) {
        if (m_themeLoadingIndicator && !m_themeLoadingIndicator->isRunning()) {
            m_themeLoadingIndicator->start();
        }
        m_themeLoadingOverlay->raise();
    } else if (m_themeLoadingIndicator && m_themeLoadingIndicator->isRunning()) {
        m_themeLoadingIndicator->stop();
    }
}

void AnimatedTabWidget::finishThemeLoadingWithConfirmation(std::function<void()> onHidden)
{
    if (!m_themeLoadingOverlay || !m_themeLoadingOverlay->isVisible()) {
        if (onHidden) {
            onHidden();
        }
        return;
    }

    // Swap the spinner for a confirmation checkmark.
    if (m_themeLoadingIndicator) {
        m_themeLoadingIndicator->stop();
        m_themeLoadingIndicator->hide();
    }
    if (m_themeLoadingCheck) {
        m_themeLoadingCheck->show();
    }

    // Hold the checkmark briefly, then fade the whole overlay out.
    QTimer::singleShot(220, this, [this, onHidden = std::move(onHidden)]() mutable {
        if (!m_themeLoadingOverlay || !m_themeLoadingOverlay->isVisible()
            || !m_themeLoadingOpacity) {
            if (m_themeLoadingOverlay) {
                m_themeLoadingOverlay->hide();
            }
            if (onHidden) {
                onHidden();
            }
            return;
        }

        if (!m_themeLoadingFade) {
            m_themeLoadingFade = new QPropertyAnimation(m_themeLoadingOpacity, "opacity", this);
        }
        m_themeLoadingFade->stop();
        QObject::disconnect(m_themeLoadingFade, nullptr, this, nullptr);
        m_themeLoadingFade->setDuration(260);
        m_themeLoadingFade->setEasingCurve(QEasingCurve::OutCubic);
        m_themeLoadingFade->setStartValue(1.0);
        m_themeLoadingFade->setEndValue(0.0);
        connect(m_themeLoadingFade, &QPropertyAnimation::finished, this,
            [this, onHidden = std::move(onHidden)]() mutable {
                if (m_themeLoadingOverlay) {
                    m_themeLoadingOverlay->hide();
                }
                // Reset for next time.
                if (m_themeLoadingOpacity) {
                    m_themeLoadingOpacity->setOpacity(1.0);
                }
                if (m_themeLoadingCheck) {
                    m_themeLoadingCheck->hide();
                }
                if (m_themeLoadingIndicator) {
                    m_themeLoadingIndicator->show();
                }
                if (onHidden) {
                    onHidden();
                }
            });
        m_themeLoadingFade->start();
    });
}

bool AnimatedTabWidget::isThemeLoadingVisible() const
{
    return m_themeLoadingOverlay && m_themeLoadingOverlay->isVisible();
}

void AnimatedTabWidget::ensureThemeLoadingOverlay()
{
    if (m_themeLoadingOverlay) {
        return;
    }

    m_themeLoadingOverlay = new QWidget(this);
    m_themeLoadingOverlay->setAttribute(Qt::WA_StyledBackground, true);
    m_themeLoadingOverlay->setGeometry(rect());
    m_themeLoadingOverlay->hide();

    // Opacity effect drives the fade-out when the refresh completes.
    m_themeLoadingOpacity = new QGraphicsOpacityEffect(m_themeLoadingOverlay);
    m_themeLoadingOpacity->setOpacity(1.0);
    m_themeLoadingOverlay->setGraphicsEffect(m_themeLoadingOpacity);

    auto* layout = new QVBoxLayout(m_themeLoadingOverlay);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(10);
    layout->setAlignment(Qt::AlignCenter);

    m_themeLoadingIndicator = new DotGridLoadingIndicator(m_themeLoadingOverlay);
    m_themeLoadingIndicator->setFixedSize(42, 42);

    // Confirmation checkmark shown in the indicator's place when finishing.
    m_themeLoadingCheck = new QLabel(m_themeLoadingOverlay);
    m_themeLoadingCheck->setFixedSize(42, 42);
    m_themeLoadingCheck->setAlignment(Qt::AlignCenter);
    m_themeLoadingCheck->setScaledContents(false);
    m_themeLoadingCheck->hide();

    m_themeLoadingLabel = new QLabel(tr("Applying theme..."), m_themeLoadingOverlay);
    m_themeLoadingLabel->setAlignment(Qt::AlignCenter);

    layout->addWidget(m_themeLoadingIndicator, 0, Qt::AlignCenter);
    layout->addWidget(m_themeLoadingCheck, 0, Qt::AlignCenter);
    layout->addWidget(m_themeLoadingLabel, 0, Qt::AlignCenter);
    updateThemeLoadingOverlayColors();
}

void AnimatedTabWidget::updateThemeLoadingOverlayColors()
{
    if (!m_themeLoadingOverlay) {
        return;
    }

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    m_themeLoadingOverlay->setStyleSheet(QString(R"(
        QWidget {
            background-color: %1;
        }
        QLabel {
            color: %2;
            font-size: 14px;
            font-weight: 500;
        }
    )")
            .arg(colors.background.name(), colors.text.name()));

    if (m_themeLoadingIndicator) {
        m_themeLoadingIndicator->setAccentColor(colors.primary);
    }

    if (m_themeLoadingCheck) {
        const QIcon check = ruwa::ui::core::IconProvider::instance().getColoredIcon(
            ruwa::ui::core::IconProvider::StandardIcon::Confirm, colors.primary);
        m_themeLoadingCheck->setPixmap(check.pixmap(QSize(42, 42)));
    }
}

void AnimatedTabWidget::setTabManager(ruwa::core::TabManager* manager)
{
    if (m_tabManager) {
        disconnect(m_tabManager, nullptr, this, nullptr);
    }

    m_tabManager = manager;

    if (m_tabManager) {
        connect(
            m_tabManager, &ruwa::core::TabManager::tabAdded, this, &AnimatedTabWidget::onTabAdded);
        connect(m_tabManager, &ruwa::core::TabManager::tabReplaced, this,
            &AnimatedTabWidget::onTabReplaced);
        connect(m_tabManager, &ruwa::core::TabManager::tabClosing, this,
            &AnimatedTabWidget::onTabClosing);
        connect(m_tabManager, &ruwa::core::TabManager::tabRemoved, this,
            &AnimatedTabWidget::onTabRemoved);
        connect(m_tabManager, &ruwa::core::TabManager::activeTabChanged, this,
            &AnimatedTabWidget::onActiveTabChanged);

        // Sync existing tabs
        for (ruwa::core::BaseTab* tab : m_tabManager->tabs()) {
            onTabAdded(tab);
        }

        // Add empty state tab (never in tabs(), shown when all tabs closed)
        if (ruwa::core::BaseTab* emptyTab = m_tabManager->emptyStateTab()) {
            emptyTab->setParent(this);
            m_displayedTabs.insert(emptyTab->id(), emptyTab);
            positionTab(emptyTab, width());
            emptyTab->hide();
        }

        // Show active tab
        if (ruwa::core::BaseTab* active = m_tabManager->activeTab()) {
            positionTab(active, 0);
            active->show();
            if (active == m_tabManager->emptyStateTab()) {
                active->initialize();
                active->activate();
            }
        }
    }
}

void AnimatedTabWidget::onTabAdded(ruwa::core::BaseTab* tab)
{
    if (!tab)
        return;

    const QUuid id = tab->id();

    if (m_displayedTabs.contains(id)) {
        return; // Already tracking this tab
    }

    // Take ownership for display
    tab->setParent(this);
    m_displayedTabs.insert(id, tab);

    // Position off-screen initially
    positionTab(tab, width());
    tab->hide();

    // If this is the first/active tab, defer init so window appears without blocking
    if (m_tabManager && m_tabManager->activeTab() == tab) {
        positionTab(tab, 0);
        tab->show();
        QTimer::singleShot(0, this, [this, tab]() {
            if (!m_displayedTabs.contains(tab->id()))
                return;
            tab->initialize();
            tab->activate();
            tab->onTransitionFinished();
            emit transitionFinished();
        });
    }
}

void AnimatedTabWidget::onTabReplaced(ruwa::core::BaseTab* oldTab, ruwa::core::BaseTab* newTab)
{
    if (!oldTab || !newTab) {
        return;
    }

    const QUuid tabId = oldTab->id();
    m_displayedTabs.remove(tabId);

    oldTab->hide();
    oldTab->setParent(nullptr);

    newTab->setParent(this);
    m_displayedTabs.insert(tabId, newTab);
    positionTab(newTab, width());
    newTab->hide();

    const bool isActive = m_tabManager && m_tabManager->activeTab() == newTab;
    if (!isActive) {
        return;
    }

    positionTab(newTab, 0);
    newTab->show();
    newTab->raise();
    // Keep the theme loading overlay above the freshly built tab so the rebuild
    // happens behind it (the coordinator hides the overlay once the new tab is
    // reported live).
    if (m_themeLoadingOverlay && m_themeLoadingOverlay->isVisible()) {
        m_themeLoadingOverlay->setGeometry(rect());
        m_themeLoadingOverlay->raise();
    }
    newTab->initialize();
    newTab->onTransitionFinished();
    emit transitionFinished();
}

void AnimatedTabWidget::onTabClosing(ruwa::core::BaseTab* tab, int direction)
{
    if (!tab)
        return;

    // Mark this tab as closing - the animation will be triggered by activeTabChanged
    // which comes AFTER this signal
    m_closingTabId = tab->id();
    m_closingDirection = direction;
    m_animationType = AnimationType::CloseOut;
}

void AnimatedTabWidget::onTabRemoved(const QUuid& tabId)
{
    // Tab has been fully removed from TabManager
    // Just clean up our tracking (tab will be deleted by TabManager)
    m_displayedTabs.remove(tabId);
    if (m_closingTabId == tabId) {
        m_closingTabId = QUuid();
        m_animationType = AnimationType::None;
    }
}

void AnimatedTabWidget::onActiveTabChanged(ruwa::core::BaseTab* newTab, ruwa::core::BaseTab* oldTab)
{
    if (oldTab) { }

    // Handle case when all tabs are closed
    if (!newTab) {
        for (auto* tab : m_displayedTabs) {
            tab->hide();
        }
        // If we were closing a tab, confirm it now
        if (!m_closingTabId.isNull() && m_tabManager) {
            m_tabManager->confirmTabClosed(m_closingTabId);
            m_closingTabId = QUuid();
            m_animationType = AnimationType::None;
        }
        return;
    }

    // Make sure new tab is in our display
    if (!m_displayedTabs.contains(newTab->id())) {
        onTabAdded(newTab);
    }

    // Determine the old tab for animation
    // If we're closing a tab, oldTab might be the closing tab (still in m_displayedTabs)
    ruwa::core::BaseTab* animateFrom = nullptr;

    if (oldTab && m_displayedTabs.contains(oldTab->id())) {
        animateFrom = oldTab;
    }

    // If new tab is not initialized, defer heavy work to next event loop tick.
    // This allows the tab to appear in topbar immediately without blocking UI.
    // Animation plays only after content is loaded.
    const bool needsDeferredInit = !newTab->isInitialized();

    if (needsDeferredInit) {
        QTimer::singleShot(0, this,
            [this, newTab, animateFrom]() { runDeferredInitAndSlide(newTab, animateFrom); });
        return;
    }

    // Tab already initialized - run immediately
    if (animateFrom && animateFrom != newTab) {
        slideToTab(newTab, animateFrom);
    } else {
        positionTab(newTab, 0);
        newTab->show();

        // If we were closing but no animation, confirm immediately
        if (!m_closingTabId.isNull() && m_tabManager) {
            m_tabManager->confirmTabClosed(m_closingTabId);
            m_closingTabId = QUuid();
            m_animationType = AnimationType::None;
        }

        newTab->onTransitionFinished();
        emit transitionFinished();
    }
}

void AnimatedTabWidget::runDeferredInitAndSlide(
    ruwa::core::BaseTab* newTab, ruwa::core::BaseTab* oldTab)
{
    if (!newTab || !m_tabManager)
        return;

    // Tab might have been removed while we were deferred
    if (!m_displayedTabs.contains(newTab->id()))
        return;

    // Heavy work: create tab content (widgets, layouts, etc.)
    newTab->initialize();

    if (oldTab && oldTab != newTab && m_displayedTabs.contains(oldTab->id())) {
        slideToTab(newTab, oldTab);
    } else {
        // No animation source - just show
        positionTab(newTab, 0);
        newTab->show();

        if (!m_closingTabId.isNull() && m_tabManager) {
            m_tabManager->confirmTabClosed(m_closingTabId);
            m_closingTabId = QUuid();
            m_animationType = AnimationType::None;
        }

        newTab->onTransitionFinished();
        emit transitionFinished();
    }
}

void AnimatedTabWidget::slideToTab(ruwa::core::BaseTab* newTab, ruwa::core::BaseTab* oldTab)
{
    if (!newTab || !oldTab || newTab == oldTab) {
        return;
    }

    // Stop any existing animation
    if (m_animation) {
        m_animation->stop();
        m_animation->deleteLater();
        m_animation = nullptr;
    }

    // Hide all displayed tabs except the two participating in this animation.
    // This prevents "silhouette" artifacts from interrupted animations where
    // the previous outgoing tab was left visible at a mid-slide position.
    for (auto it = m_displayedTabs.constBegin(); it != m_displayedTabs.constEnd(); ++it) {
        ruwa::core::BaseTab* tab = it.value();
        if (tab && tab != newTab && tab != oldTab) {
            tab->hide();
        }
    }

    // Initialize new tab if needed
    newTab->initialize();

    // Determine direction: +1 = slide left (new from right), -1 = slide right (new from left)
    int direction = determineDirection(oldTab, newTab);
    int offset = width() * direction;

    // Use current positions for seamless mid-animation interruption.
    // If a tab was already mid-slide, it continues from where it is instead of jumping.
    QPoint oldTabStartPos = oldTab->isVisible() ? oldTab->pos() : QPoint(0, 0);

    QPoint newTabStartPos;
    if (newTab->isVisible()) {
        // Tab was part of an interrupted animation — continue from current position
        newTabStartPos = newTab->pos();
    } else {
        // Fresh slide — start off-screen
        newTabStartPos = QPoint(offset, 0);
        positionTab(newTab, offset);
    }

    newTab->show();
    newTab->raise();
    oldTab->show();

    // When switching to a workspace tab that still needs a theme refresh, make
    // the loading overlay ride IN together with the incoming tab: show it, size it
    // to the tab, place it at the tab's start position and raise it. The slide
    // animation below moves it in lockstep with the new tab, so the user still
    // sees the transition while the overlay masks the incoming tab's
    // not-yet-refreshed (old-theme) content — and the overlay never pops in
    // abruptly when the slide ends. The coordinator hides it once the refresh
    // behind it completes.
    const bool slideThemeOverlay = newTab->needsThemeRefresh() && newTab->wantsThemeLoadingScreen();
    if (slideThemeOverlay) {
        setThemeLoadingVisible(true);
        if (m_themeLoadingOverlay) {
            m_themeLoadingOverlay->setGeometry(newTabStartPos.x(), 0, width(), height());
            m_themeLoadingOverlay->raise();
        }
    }

    // Scale duration proportionally to remaining distance so that
    // interrupted animations don't feel sluggish or overly fast.
    int adjustedDuration = m_duration;
    if (qAbs(offset) > 0) {
        qreal oldDist = qAbs(qreal(oldTabStartPos.x() - (-offset)));
        qreal newDist = qAbs(qreal(newTabStartPos.x()));
        qreal maxRatio = qMax(oldDist, newDist) / qreal(qAbs(offset));
        adjustedDuration = qMax(100, qRound(m_duration * qBound(0.3, maxRatio, 1.0)));
    }

    // Create animations using actual current positions
    auto* animOld = new QPropertyAnimation(oldTab, "pos", this);
    animOld->setDuration(adjustedDuration);
    animOld->setStartValue(oldTabStartPos);
    animOld->setEndValue(QPoint(-offset, 0));
    animOld->setEasingCurve(m_easingCurve);

    auto* animNew = new QPropertyAnimation(newTab, "pos", this);
    animNew->setDuration(adjustedDuration);
    animNew->setStartValue(newTabStartPos);
    animNew->setEndValue(QPoint(0, 0));
    animNew->setEasingCurve(m_easingCurve);

    // Group animations
    m_animation = new QParallelAnimationGroup(this);
    m_animation->addAnimation(animOld);
    m_animation->addAnimation(animNew);

    // Slide the theme-loading overlay in together with the incoming tab.
    if (slideThemeOverlay) {
        auto* animOverlay = new QPropertyAnimation(m_themeLoadingOverlay, "pos", this);
        animOverlay->setDuration(adjustedDuration);
        animOverlay->setStartValue(newTabStartPos);
        animOverlay->setEndValue(QPoint(0, 0));
        animOverlay->setEasingCurve(m_easingCurve);
        m_animation->addAnimation(animOverlay);
    }

    // Store UUIDs for safe lookup after animation (tabs might be deleted during animation)
    QUuid oldTabId = oldTab->id();
    QUuid newTabId = newTab->id();
    QUuid closingId = m_closingTabId; // Capture current closing tab ID

    connect(m_animation, &QParallelAnimationGroup::finished, this,
        [this, oldTabId, newTabId, closingId]() {
            // Look up tabs by UUID - they might have been deleted
            ruwa::core::BaseTab* oldT = m_displayedTabs.value(oldTabId, nullptr);
            ruwa::core::BaseTab* newT = m_displayedTabs.value(newTabId, nullptr);

            // Hide old tab if it still exists
            if (oldT) {
                oldT->hide();
                positionTab(oldT, width());
            }

            // Ensure new tab is properly positioned if it still exists
            if (newT) {
                positionTab(newT, 0);
            }

            // If we were closing a tab, confirm the close now
            // This will trigger tabRemoved which cleans up m_displayedTabs
            if (!closingId.isNull() && m_tabManager) {
                m_tabManager->confirmTabClosed(closingId);
            }

            if (newT) {
                newT->onTransitionFinished();
            }

            finishAnimation();
        });

    m_animation->start();
}

void AnimatedTabWidget::finishAnimation()
{
    m_animationType = AnimationType::None;
    m_closingTabId = QUuid();
    m_closingDirection = 1; // Reset to default

    if (m_animation) {
        m_animation->deleteLater();
        m_animation = nullptr;
    }

    emit transitionFinished();
}

void AnimatedTabWidget::positionTab(ruwa::core::BaseTab* tab, int xOffset)
{
    if (tab) {
        tab->setGeometry(xOffset, 0, width(), height());
    }
}

int AnimatedTabWidget::determineDirection(ruwa::core::BaseTab* from, ruwa::core::BaseTab* to) const
{
    // If we're in a close animation, use the pre-calculated direction
    if (m_animationType == AnimationType::CloseOut && !m_closingTabId.isNull()) {
        return m_closingDirection;
    }

    // Normal tab switch - determine direction from indices
    if (!m_tabManager || !from || !to) {
        return 1; // Default: slide left (new comes from right)
    }

    int fromIdx = m_tabManager->displayIndex(from);
    int toIdx = m_tabManager->displayIndex(to);

    // If indices can't be determined, default to right
    if (toIdx < 0 || fromIdx < 0) {
        return 1;
    }

    return (toIdx > fromIdx) ? 1 : -1;
}

void AnimatedTabWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    if (m_themeLoadingOverlay) {
        m_themeLoadingOverlay->setGeometry(rect());
        if (m_themeLoadingOverlay->isVisible()) {
            m_themeLoadingOverlay->raise();
        }
    }

    // During animation, resize all visible tabs to maintain correct dimensions.
    // The animation only drives the "pos" property, so we update width/height here.
    if (m_animation && m_animation->state() == QAbstractAnimation::Running) {
        for (auto it = m_displayedTabs.constBegin(); it != m_displayedTabs.constEnd(); ++it) {
            ruwa::core::BaseTab* tab = it.value();
            if (tab && tab->isVisible()) {
                tab->resize(width(), height());
            }
        }
        return;
    }

    // No animation — just position the active tab
    if (m_tabManager) {
        if (auto* active = m_tabManager->activeTab()) {
            if (m_displayedTabs.contains(active->id())) {
                positionTab(active, 0);
            }
        }
    }
}

} // namespace ruwa::ui::widgets
