// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   C O M M A N D   C O O R D I N A T O R
// ======================================================================================

#ifndef RUWA_UI_WINDOWS_MAINWINDOW_COMMANDCOORDINATOR_H
#define RUWA_UI_WINDOWS_MAINWINDOW_COMMANDCOORDINATOR_H

#include <QObject>

namespace ruwa::core {
class TabManager;
}

namespace ruwa::ui::windows {

class MainWindow;

class CommandCoordinator : public QObject {
    Q_OBJECT

public:
    explicit CommandCoordinator(QObject* parent = nullptr);

    void initialize(ruwa::core::TabManager* tabManager, MainWindow* mainWindow);

private:
    void setupCommandSystem();
    void setupShortcuts(MainWindow* mainWindow);

    ruwa::core::TabManager* m_tabManager = nullptr;
    MainWindow* m_mainWindow = nullptr;
};

} // namespace ruwa::ui::windows

#endif
