// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   W I N D O W   S E T U P   C O O R D I N A T O R
// ======================================================================================

#ifndef RUWA_UI_WINDOWS_MAINWINDOW_WINDOWSETUPCOORDINATOR_H
#define RUWA_UI_WINDOWS_MAINWINDOW_WINDOWSETUPCOORDINATOR_H

#include <QObject>

class QMainWindow;
class QOpenGLWidget;

namespace QWK {
class WidgetWindowAgent;
}

namespace ruwa::ui::widgets {
class TopBar;
}

namespace ruwa::ui::tabs {
class CustomTabBar;
}

namespace ruwa::ui::windows {

class WindowSetupCoordinator : public QObject {
    Q_OBJECT

public:
    explicit WindowSetupCoordinator(QObject* parent = nullptr);
    ~WindowSetupCoordinator() override;

    void setupWindowAgent(
        QMainWindow* mainWindow, widgets::TopBar* topBar, tabs::CustomTabBar* tabBar);

    void setupOpenGLWarmup(QMainWindow* parent);
    void restoreWindowState(QMainWindow* mainWindow);
    void saveWindowState(QMainWindow* mainWindow);

    QWK::WidgetWindowAgent* windowAgent() const { return m_windowAgent; }

private:
    QWK::WidgetWindowAgent* m_windowAgent = nullptr;
    QOpenGLWidget* m_glWarmup = nullptr;
};

} // namespace ruwa::ui::windows

#endif
