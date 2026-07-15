// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   O V E R L A Y   C O O R D I N A T O R
// ======================================================================================

#ifndef RUWA_UI_WINDOWS_MAINWINDOW_OVERLAYCOORDINATOR_H
#define RUWA_UI_WINDOWS_MAINWINDOW_OVERLAYCOORDINATOR_H

#include <QObject>
#include <QColor>
#include <QPointer>

// Forward declarations
class QWidget;

namespace ruwa::ui::widgets {
class AnimatedTabWidget;
class CommandPaletteOverlay;
class UpdateMessageOverlay;
class ReleaseNotesOverlay;
class ColorPickerOverlay;
class ColorInputButton;
class TopBar;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::windows {

class OverlayCoordinator : public QObject {
    Q_OBJECT

public:
    explicit OverlayCoordinator(QObject* parent = nullptr);

    void initialize(
        widgets::AnimatedTabWidget* tabContent, QWidget* centralWidget, widgets::TopBar* topBar);

    void showCommandPalette();
    void showFirstLaunchUpdateMessage();
    void showReleaseNotesOverlay();
    void showColorPicker(const QColor& initialColor = Qt::white, QWidget* sourceButton = nullptr);

signals:
    void firstLaunchUpdateMessageDismissed();

private:
    void ensureUpdateMessageOverlay();

    QPointer<widgets::CommandPaletteOverlay> m_paletteOverlay;
    QPointer<widgets::UpdateMessageOverlay> m_updateMessageOverlay;
    QPointer<widgets::ReleaseNotesOverlay> m_releaseNotesOverlay;
    QPointer<widgets::ColorPickerOverlay> m_colorPickerOverlay;
    QPointer<widgets::ColorInputButton> m_activeColorButton;
    widgets::AnimatedTabWidget* m_tabContent = nullptr;
    QWidget* m_centralWidget = nullptr;
    bool m_showReleaseNotesAfterUpdateMessage = false;
};

} // namespace ruwa::ui::windows

#endif
