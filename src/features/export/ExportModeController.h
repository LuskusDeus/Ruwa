// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   E X P O R T   M O D E   C O N T R O L L E R
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_EXPORTMODECONTROLLER_H
#define RUWA_UI_WORKSPACE_EXPORTMODECONTROLLER_H

#include <QObject>

class QEvent;
class QVariantAnimation;
class QWidget;

namespace ruwa::ui::workspace {

class CanvasPanel;
class ExportSettingsPanel;

/// Manages the export mode transition: animates layout, overlays, and canvas camera.
/// The animation is fully interruptible — toggling mid-transition reverses direction.
class ExportModeController : public QObject {
    Q_OBJECT

public:
    /// @param workspace   The WorkspaceTab widget (used for geometry)
    /// @param canvasPanel The canvas panel to control (overlays, interaction, camera)
    /// @param exportPanel The export settings panel (positioned by controller)
    explicit ExportModeController(QWidget* workspace, CanvasPanel* canvasPanel,
        ExportSettingsPanel* exportPanel, QObject* parent = nullptr);

    bool isExportMode() const { return m_targetActive; }
    bool isAnimating() const;

    /// Toggle export mode on/off. Interruptible — can be called mid-animation.
    void toggle();

    /// Enter export mode.
    void enter();

    /// Exit export mode.
    void exit();

    /// Update layout geometry (call from workspace resizeEvent).
    void updateLayout();

    /// Current animation progress (0.0 = normal, 1.0 = export mode).
    qreal progress() const { return m_progress; }

signals:
    void exportModeChanged(bool active);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void startAnimation(bool entering);
    void applyProgress(qreal progress);
    void saveCameraState();
    void restoreCameraState();
    int exportPanelTargetWidth() const;

    QWidget* m_workspace = nullptr;
    CanvasPanel* m_canvasPanel = nullptr;
    ExportSettingsPanel* m_exportPanel = nullptr;
    QVariantAnimation* m_animation = nullptr;

    qreal m_progress = 0.0; ///< Current interpolated progress
    bool m_targetActive = false; ///< Desired end state

    // Saved camera state for restore on exit
    float m_savedCameraZoom = 1.0f;
    float m_savedCameraPosX = 0.0f;
    float m_savedCameraPosY = 0.0f;
    float m_savedCameraRotation = 0.0f;
    bool m_cameraStateSaved = false;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_EXPORTMODECONTROLLER_H
