// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   C O R E   S Y S T E M
// ======================================================================================
//   File        : Application.h
//   Description : Main Qt application wrapper and singleton root.
//   Subsystems  : ThemeManager, FontManager, Global Settings
// ======================================================================================

#ifndef RUWA_APPLICATION_H
#define RUWA_APPLICATION_H

#include <QApplication>
#include <QStringList>

class QEvent;
class QOpenGLContext;
class QOffscreenSurface;
class QOpenGLWidget;

namespace ruwa {

class Application : public QApplication {
    Q_OBJECT

public:
    // ----------------------------------------------------------------------
    //   L I F E C Y C L E
    // ----------------------------------------------------------------------
    Application(int& argc, char** argv);
    ~Application() override;

    // ----------------------------------------------------------------------
    //   A C C E S S
    // ----------------------------------------------------------------------
    static Application* instance();

    // Returns shared OpenGL context for resource sharing
    QOpenGLContext* sharedGLContext();

    // Initialize managers (fonts, themes, commands)
    // Called by StartupController during startup sequence
    void initializeManagers();
    void warmUpOpenGLShaders();

    // Restart current application process preserving command line arguments
    static bool restart();

    /// Restart current application and request a full factory reset before startup.
    static bool restartWithFactoryReset();

    /// True while the current process is shutting down in favor of a factory-reset restart.
    static bool isFactoryResetRestartInProgress();

    /// Restart with update: close window (unsaved check), then run update script and quit.
    static bool restartWithUpdate();

    /// Get the currently active tablet backend (the one running right now)
    /// 0 = WinTab (Qt), 1 = Windows Ink, 2 = WinTab (Ruwa)
    static int currentTabletBackend();

    /// Set the currently active tablet backend (called from main.cpp during startup)
    static void setCurrentTabletBackend(int backend);

    /// Clear persisted application state when the internal reset flag is present.
    /// Call before reading any startup settings.
    static bool runFactoryResetIfRequested(const QStringList& arguments);

signals:
    /// Emitted during initialization with loading progress
    /// Used by StartupController to update splash screen
    void loadingProgress(const QString& message, int percentage);

    /// Emitted when the OS asks this process to open a local file.
    void fileOpenRequested(const QString& filePath);

protected:
    bool event(QEvent* event) override;

private:
    // ----------------------------------------------------------------------
    //   I N T E R N A L   S E T U P
    // ----------------------------------------------------------------------
    void setupDefaultSettings();
    void initializeOpenGL();

    // OpenGL context for early initialization
    QOpenGLContext* m_glContext = nullptr;
    QOffscreenSurface* m_glSurface = nullptr;

    // Hidden OpenGL widget to pre-warm Qt's OpenGL subsystem
    // This prevents window recreation when first OpenGL widget is shown
    class QOpenGLWidget* m_glWarmup = nullptr;
};

} // namespace ruwa

#endif // RUWA_APPLICATION_H
