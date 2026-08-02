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
#include <QWindow>

namespace ruwa::ui::windows {

namespace {

// Placement is remembered explicitly instead of through Qt's opaque saveGeometry()
// blob: that blob identifies the display by index, which silently points at another
// monitor as soon as the display order changes.
constexpr auto kLegacyGeometryKey = "MainWindow/geometry";
constexpr auto kDockStateKey = "MainWindow/state";
constexpr auto kNormalGeometryKey = "MainWindow/normalGeometry";
constexpr auto kScreenNameKey = "MainWindow/screenName";
constexpr auto kScreenGeometryKey = "MainWindow/screenGeometry";

const QSize kFallbackWindowSize(1200, 800);

/// Finds the monitor a previous session was closed on.
///
/// A screen name alone is not an identity: two identical panels report the same
/// name and Windows can report a different one for the same panel after a
/// reconnect. The saved virtual-desktop rectangle disambiguates both cases.
QScreen* resolveScreen(const QString& name, const QRect& geometry)
{
    const auto screens = QGuiApplication::screens();

    // Nothing changed since the last session.
    if (!name.isEmpty() && geometry.isValid()) {
        for (QScreen* screen : screens) {
            if (screen && screen->name() == name && screen->geometry() == geometry) {
                return screen;
            }
        }
    }

    // The monitor is still connected and its name is unambiguous - trust the name
    // even if the desktop was rearranged around it.
    if (!name.isEmpty()) {
        QScreen* named = nullptr;
        int matches = 0;
        for (QScreen* screen : screens) {
            if (screen && screen->name() == name) {
                named = screen;
                ++matches;
            }
        }
        if (matches == 1) {
            return named;
        }
    }

    // Renamed, replaced or duplicated name: take whatever now covers that area.
    if (geometry.isValid()) {
        QScreen* best = nullptr;
        qint64 bestOverlap = 0;
        for (QScreen* screen : screens) {
            if (!screen) {
                continue;
            }
            const QRect overlap = screen->geometry().intersected(geometry);
            const qint64 area = static_cast<qint64>(overlap.width()) * overlap.height();
            if (area > bestOverlap) {
                bestOverlap = area;
                best = screen;
            }
        }
        if (best) {
            return best;
        }
    }

    return nullptr;
}

/// Restored-size geometry for `screen`, keeping the remembered position when it
/// still lands on that monitor and centering otherwise.
QRect placeOnScreen(const QRect& savedNormal, QScreen* screen)
{
    const QRect available = screen->availableGeometry();

    QSize size = (savedNormal.isValid() && !savedNormal.isEmpty()) ? savedNormal.size()
                                                                   : kFallbackWindowSize;
    size = size.boundedTo(available.size());

    QRect placed(QPoint(0, 0), size);
    if (savedNormal.isValid() && available.intersects(savedNormal)) {
        placed.moveTopLeft(savedNormal.topLeft());
        placed.moveLeft(
            qBound(available.left(), placed.left(), available.right() - placed.width() + 1));
        placed.moveTop(
            qBound(available.top(), placed.top(), available.bottom() - placed.height() + 1));
    } else {
        placed.moveCenter(available.center());
    }
    return placed;
}

} // namespace

WindowSetupCoordinator::WindowSetupCoordinator(QObject* parent)
    : QObject(parent)
{
}

WindowSetupCoordinator::~WindowSetupCoordinator()
{
    // Cleanup handled by Qt parent-child ownership
}

QScreen* WindowSetupCoordinator::savedScreen()
{
    QSettings settings;
    return resolveScreen(settings.value(kScreenNameKey).toString(),
        settings.value(kScreenGeometryKey).toRect());
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

    if (settings.contains(kDockStateKey)) {
        mainWindow->restoreState(settings.value(kDockStateKey).toByteArray());
    }

    QRect savedNormal = settings.value(kNormalGeometryKey).toRect();
    if (!savedNormal.isValid() && settings.contains(kLegacyGeometryKey)) {
        // Installations from before this rewrite only left Qt's blob behind. Unpack it
        // once so the window size survives the upgrade; the screen comes from the keys
        // below, never from the blob.
        mainWindow->restoreGeometry(settings.value(kLegacyGeometryKey).toByteArray());
        savedNormal = mainWindow->normalGeometry();
    }

    QScreen* targetScreen = savedScreen();
    if (!targetScreen) {
        targetScreen = mainWindow->screen();
    }
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    if (!targetScreen) {
        mainWindow->resize(kFallbackWindowSize);
        mainWindow->setWindowState(Qt::WindowMaximized);
        return;
    }

    const QRect placed = placeOnScreen(savedNormal, targetScreen);

    // MainWindow forces its native window into existence in the constructor, so this
    // physically moves the HWND onto the remembered monitor. That matters: the maximize
    // below - and the showMaximized() the startup animation issues later - resolves
    // against the monitor the window currently sits on. Without the move a still-hidden
    // window keeps the screen it was created on and startup always landed on the primary
    // display no matter what was saved.
    mainWindow->setWindowState(Qt::WindowNoState);
    mainWindow->setGeometry(placed);
    if (QWindow* handle = mainWindow->windowHandle()) {
        handle->setGeometry(placed);
    }

    // From here on the window is on its own: minimize, restore, maximize and moving
    // between monitors are left entirely to the window manager.
    mainWindow->setWindowState(Qt::WindowMaximized);
}

void WindowSetupCoordinator::saveWindowState(QMainWindow* mainWindow)
{
    if (!mainWindow)
        return;

    QSettings settings;
    settings.setValue(kDockStateKey, mainWindow->saveState());

    QRect normal = mainWindow->normalGeometry();
    if (!normal.isValid() || normal.isEmpty()) {
        normal = mainWindow->geometry();
    }
    if (normal.isValid() && !normal.isEmpty()) {
        settings.setValue(kNormalGeometryKey, normal);
    }

    // The monitor the window is actually seen on. A minimized window sits at
    // off-desktop coordinates, so fall back to the screen Qt associates it with.
    QScreen* screen = nullptr;
    if (!mainWindow->isMinimized()) {
        screen = QGuiApplication::screenAt(mainWindow->frameGeometry().center());
    }
    if (!screen) {
        if (QWindow* handle = mainWindow->windowHandle()) {
            screen = handle->screen();
        }
    }
    // Deliberately no primaryScreen() fallback: keeping the previous value beats
    // overwriting a correct monitor with a guess.
    if (screen) {
        settings.setValue(kScreenNameKey, screen->name());
        settings.setValue(kScreenGeometryKey, screen->geometry());
    }

    settings.remove(kLegacyGeometryKey);
}

} // namespace ruwa::ui::windows
