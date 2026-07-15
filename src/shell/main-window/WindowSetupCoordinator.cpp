// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   W I N D O W   S E T U P   C O O R D I N A T O R
// ======================================================================================

#include "WindowSetupCoordinator.h"
#include "shell/top-bar/TopBar.h"
#include "shell/tab-system/CustomTabBar.h"

#include <QWKWidgets/widgetwindowagent.h>

#include <QMainWindow>
#include <QSettings>
#include <QScreen>
#include <QGuiApplication>
#include <QOpenGLWidget>

namespace ruwa::ui::windows {

WindowSetupCoordinator::WindowSetupCoordinator(QObject* parent)
    : QObject(parent)
{
}

WindowSetupCoordinator::~WindowSetupCoordinator()
{
    // Cleanup handled by Qt parent-child ownership
}

void WindowSetupCoordinator::setupWindowAgent(
    QMainWindow* mainWindow, widgets::TopBar* topBar, tabs::CustomTabBar* tabBar)
{
    if (!mainWindow || !topBar)
        return;

    // Create window agent for frameless window with native features
    m_windowAgent = new QWK::WidgetWindowAgent(this);
    m_windowAgent->setup(mainWindow);
    mainWindow->setAttribute(Qt::WA_TranslucentBackground, false);

    // Enable Windows 11 snap layouts support
#ifdef Q_OS_WIN
    m_windowAgent->setWindowAttribute(QStringLiteral("dwm-blur"), QStringLiteral("none"));
#endif

    m_windowAgent->setTitleBar(topBar);

    // Set system buttons for proper hit testing
    m_windowAgent->setSystemButton(QWK::WidgetWindowAgent::Minimize, topBar->minimizeButton());
    m_windowAgent->setSystemButton(QWK::WidgetWindowAgent::Maximize, topBar->maximizeButton());
    m_windowAgent->setSystemButton(QWK::WidgetWindowAgent::Close, topBar->closeButton());

    // Set hit test visible widgets (these receive mouse events instead of dragging)
    m_windowAgent->setHitTestVisible(topBar->menuButtonContainer(), true);
    if (QWidget* w = topBar->layoutSwitchButton()) {
        m_windowAgent->setHitTestVisible(w, true);
    }
    if (QWidget* sep = topBar->layoutSwitchSeparator()) {
        m_windowAgent->setHitTestVisible(sep, true);
    }

    if (tabBar) {
        m_windowAgent->setHitTestVisible(tabBar, true); // CustomTabBar handles dragging internally
    }

    for (QWidget* w : topBar->qwkExtraHitTestWidgets()) {
        if (w) {
            m_windowAgent->setHitTestVisible(w, true);
        }
    }
}

void WindowSetupCoordinator::setupOpenGLWarmup(QMainWindow* parent)
{
    if (!parent)
        return;

    // Pre-warm OpenGL by creating a hidden widget
    // This forces Qt to initialize the OpenGL subsystem BEFORE any visible
    // widgets are created, preventing window recreation when canvas is shown
    m_glWarmup = new QOpenGLWidget(parent);
    m_glWarmup->setFixedSize(1, 1);
    m_glWarmup->setAttribute(Qt::WA_DontShowOnScreen);
    m_glWarmup->show(); // Triggers initializeGL()
}

void WindowSetupCoordinator::restoreWindowState(QMainWindow* mainWindow)
{
    if (!mainWindow)
        return;

    QSettings settings;

    if (settings.contains("MainWindow/geometry")) {
        mainWindow->restoreGeometry(settings.value("MainWindow/geometry").toByteArray());
        mainWindow->restoreState(settings.value("MainWindow/state").toByteArray());
    } else {
        // Default: centered, 1200x800
        if (QScreen* screen = QGuiApplication::primaryScreen()) {
            QRect geo = screen->availableGeometry();
            mainWindow->resize(1200, 800);
            mainWindow->move(
                (geo.width() - mainWindow->width()) / 2, (geo.height() - mainWindow->height()) / 2);
        }
    }
}

void WindowSetupCoordinator::saveWindowState(QMainWindow* mainWindow)
{
    if (!mainWindow)
        return;

    QSettings settings;
    settings.setValue("MainWindow/geometry", mainWindow->saveGeometry());
    settings.setValue("MainWindow/state", mainWindow->saveState());
}

} // namespace ruwa::ui::windows
