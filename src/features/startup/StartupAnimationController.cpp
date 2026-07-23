// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   S T A R T U P   A N I M A T I O N   I M P L E M E N T A T I O N
// ======================================================================================

#include "features/startup/StartupAnimationController.h"
#include "features/home/HomePageSidebar.h"
#include "features/home/welcome/WelcomeContent.h"
#include "shell/main-window/SplashScreen.h"
#include "shell/main-window/MainWindow.h"
#include "shell/top-bar/TopBar.h"
#include "platform/Platform.h"

#include <QAbstractAnimation>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QPropertyAnimation>
#include <QScreen>
#include <QTimer>
#include <QWindow>
namespace ruwa::core {

StartupAnimationController::StartupAnimationController(QObject* parent)
    : QObject(parent)
{
}

void StartupAnimationController::expandSplashToWindow(ruwa::ui::windows::SplashScreen* splash,
    ruwa::ui::windows::MainWindow* mainWindow, int durationMs)
{
    if (!splash || !mainWindow)
        return;

    // === 1. P R E P A R E   M A I N   W I N D O W ===

    // Startup should always land in the same system-maximized mode as the
    // top-right maximize button, not Qt fullscreen and not a floating window.
    QRect targetGeometry;
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        targetGeometry = screen->availableGeometry();
    }
    if (!targetGeometry.isNull()) {
        mainWindow->setGeometry(targetGeometry);
    }

    // Establish the native owner relationship before MainWindow is shown for the first time.
    // This keeps the splash above MainWindow without making either window globally topmost and
    // avoids a one-frame stacking race during the final reveal.
    if (QWindow* splashHandle = splash->windowHandle()) {
        if (QWindow* mainWindowHandle = mainWindow->windowHandle()) {
            splashHandle->setTransientParent(mainWindowHandle);
        }
    }

    // === 2. P L A T F O R M - S P E C I F I C   S E T U P ===

    auto* platform = ruwa::platform::Platform::create();

    // Keep one native MainWindow instance visible-but-transparent behind the transient splash.
    // Hiding it here and showing it again during the handoff lets the window system expose its
    // default surface for one frame before Qt presents the prepared backing store.
    platform->disableWindowAnimations(mainWindow);
    mainWindow->setWindowOpacity(0.0);
    mainWindow->setAttribute(Qt::WA_ShowWithoutActivating, true);
    mainWindow->showMaximized();
    QCoreApplication::processEvents();
    mainWindow->setAttribute(Qt::WA_ShowWithoutActivating, false);

    QWidget* topBar = mainWindow->topBar();
    QWidget* topBarClip = mainWindow->topBarClip();
    auto* sidebar = mainWindow->findChild<ruwa::ui::widgets::HomePageSidebar*>();
    auto* welcomeContent = mainWindow->findChild<ruwa::ui::widgets::WelcomeContent*>();
    QWidget* sidebarClip = sidebar ? sidebar->parentWidget() : nullptr;

    if (topBar && topBarClip) {
        mainWindow->relayoutTopBarInset();
        const int barH = topBar->height() > 0 ? topBar->height() : topBar->sizeHint().height();
        if (barH > 0) {
            const QPoint endPos = topBar->pos();
            topBar->move(endPos.x(), endPos.y() - barH);
            topBar->hide();
        }
    }

    if (sidebar && sidebarClip) {
        const int sidebarWidth = sidebarClip->width() > 0
            ? sidebarClip->width()
            : (sidebar->width() > 0 ? sidebar->width() : sidebar->sizeHint().width());
        if (sidebarWidth > 0) {
            sidebar->resize(sidebarWidth, sidebarClip->height());
            sidebar->move(-sidebarWidth, 0);
            sidebar->hide();
        }
    }

    if (welcomeContent) {
        const QList<QWidget*> sections = welcomeContent->startupSections();
        for (QWidget* sectionClip : sections) {
            if (!sectionClip) {
                continue;
            }

            auto directChildren
                = sectionClip->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
            QWidget* sectionContent = directChildren.isEmpty() ? nullptr : directChildren.first();
            if (!sectionContent) {
                continue;
            }

            const int sectionWidth = sectionClip->width() > 0 ? sectionClip->width()
                                                              : sectionContent->sizeHint().width();
            const int sectionHeight = sectionClip->height() > 0
                ? sectionClip->height()
                : sectionContent->sizeHint().height();

            sectionContent->resize(sectionWidth, sectionHeight);
            sectionContent->move(0, -sectionHeight);
            sectionContent->hide();
        }
    }

    // === 4. S T A R T   E X P A N S I O N ===

    splash->expandToMainWindow(durationMs);

    // === 5. W H E N   E X P A N S I O N   F I N I S H E S ===

    connect(splash, &ruwa::ui::windows::SplashScreen::expansionFinished, this,
        [this, splash, mainWindow, platform]() {
            const bool shouldActivateMainWindow
                = QGuiApplication::applicationState() == Qt::ApplicationActive;

            QWidget* topBar = mainWindow->topBar();
            QWidget* topBarClip = mainWindow->topBarClip();
            auto* sidebar = mainWindow->findChild<ruwa::ui::widgets::HomePageSidebar*>();
            auto* welcomeContent = mainWindow->findChild<ruwa::ui::widgets::WelcomeContent*>();
            QWidget* sidebarClip = sidebar ? sidebar->parentWidget() : nullptr;
            QPropertyAnimation* topBarAnim = nullptr;
            QPropertyAnimation* sidebarAnim = nullptr;
            int startupSettleDelay = 100;

            if (topBar && topBarClip) {
                mainWindow->relayoutTopBarInset();
                const int barH
                    = topBar->height() > 0 ? topBar->height() : topBar->sizeHint().height();
                if (barH > 0) {
                    const QPoint endPos = topBar->pos();
                    const QPoint startPos(endPos.x(), endPos.y() - barH);
                    topBar->move(startPos);
                    topBar->show();
                    topBarAnim = new QPropertyAnimation(topBar, "pos", this);
                    topBarAnim->setDuration(420);
                    topBarAnim->setStartValue(startPos);
                    topBarAnim->setEndValue(endPos);
                    topBarAnim->setEasingCurve(QEasingCurve::OutCubic);
                    startupSettleDelay = 470;
                }
            }

            if (sidebar && sidebarClip) {
                const int sidebarWidth = sidebarClip->width() > 0
                    ? sidebarClip->width()
                    : (sidebar->width() > 0 ? sidebar->width() : sidebar->sizeHint().width());
                if (sidebarWidth > 0) {
                    sidebar->show();
                    sidebarAnim = new QPropertyAnimation(sidebar, "pos", this);
                    sidebarAnim->setDuration(420);
                    sidebarAnim->setStartValue(QPoint(-sidebarWidth, 0));
                    sidebarAnim->setEndValue(QPoint(0, 0));
                    sidebarAnim->setEasingCurve(QEasingCurve::OutCubic);
                    startupSettleDelay = qMax(startupSettleDelay, 470);
                }
            }

            if (welcomeContent) {
                const QList<QWidget*> sections = welcomeContent->startupSections();
                const int sectionDuration = 420;
                const int sectionDelayStep = 105;

                for (int i = 0; i < sections.size(); ++i) {
                    QWidget* sectionClip = sections[i];
                    if (!sectionClip) {
                        continue;
                    }

                    auto directChildren = sectionClip->findChildren<QWidget*>(
                        QString(), Qt::FindDirectChildrenOnly);
                    QWidget* sectionContent
                        = directChildren.isEmpty() ? nullptr : directChildren.first();
                    if (!sectionContent) {
                        continue;
                    }

                    const int sectionWidth = sectionClip->width() > 0
                        ? sectionClip->width()
                        : sectionContent->sizeHint().width();
                    const int sectionHeight = sectionClip->height() > 0
                        ? sectionClip->height()
                        : sectionContent->sizeHint().height();
                    const int delay = i * sectionDelayStep;

                    auto* sectionAnim = new QPropertyAnimation(sectionContent, "pos", this);
                    sectionAnim->setDuration(sectionDuration);
                    sectionAnim->setStartValue(QPoint(0, -sectionHeight));
                    sectionAnim->setEndValue(QPoint(0, 0));
                    sectionAnim->setEasingCurve(QEasingCurve::OutCubic);

                    QTimer::singleShot(delay, this, [sectionAnim, sectionContent]() {
                        sectionContent->show();
                        sectionAnim->start(QAbstractAnimation::DeleteWhenStopped);
                    });

                    startupSettleDelay = qMax(startupSettleDelay, delay + sectionDuration + 50);
                }
            }

            // Paint the complete initial state while MainWindow is still transparent, then
            // reveal that existing native surface behind the transient splash. No second show
            // operation or uninitialized backing-store frame is involved in the handoff.
            mainWindow->repaint();
            mainWindow->setWindowOpacity(1.0);
            platform->synchronizeWindowPresentation(mainWindow);

            // NOW safe to hide splash - the main window is visible and fully painted behind it.
            splash->hide();

            // Preserve the user's application switch during startup instead of pulling Ruwa
            // back to the foreground when the animation finishes.
            if (shouldActivateMainWindow) {
                mainWindow->raise();
                mainWindow->activateWindow();
            }

            if (topBarAnim) {
                topBarAnim->start(QAbstractAnimation::DeleteWhenStopped);
            }
            if (sidebarAnim) {
                sidebarAnim->start(QAbstractAnimation::DeleteWhenStopped);
            }

            // Re-enable platform animations after window is shown
            QTimer::singleShot(100, [platform, mainWindow]() {
                platform->enableWindowAnimations(mainWindow);
                delete platform; // Cleanup
            });

            // Delete splash
            splash->close();

            // Emit completion signal after the window reveal settles.
            QTimer::singleShot(startupSettleDelay, this, [this]() { emit animationsCompleted(); });
        });
}

} // namespace ruwa::core
