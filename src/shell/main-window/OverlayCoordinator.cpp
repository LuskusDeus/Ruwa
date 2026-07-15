// SPDX-License-Identifier: MPL-2.0

// OverlayCoordinator.cpp
#include "OverlayCoordinator.h"
#include "shared/widgets/layout/AnimatedTabWidget.h"
#include "shell/top-bar/TopBar.h"
#include "shell/command-palette/CommandPaletteOverlay.h"
#include "shell/update-message/UpdateMessageOverlay.h"
#include "shell/update-message/ReleaseNotesOverlay.h"
#include "features/color/ColorPickerOverlay.h"
#include "shared/widgets/inputs/ColorInputButton.h"
namespace ruwa::ui::windows {

OverlayCoordinator::OverlayCoordinator(QObject* parent)
    : QObject(parent)
{
}

void OverlayCoordinator::initialize(
    widgets::AnimatedTabWidget* tabContent, QWidget* centralWidget, widgets::TopBar* topBar)
{
    m_tabContent = tabContent;
    m_centralWidget = centralWidget;

    if (topBar && centralWidget) {
        topBar->initOverlay(centralWidget);
    }

    if (m_tabContent) {
        m_paletteOverlay = new widgets::CommandPaletteOverlay(m_tabContent);
        ensureUpdateMessageOverlay();
    }

    // Create ColorPickerOverlay with centralWidget as container (not tabContent)
    // This ensures it's always on top and not affected by tab switching
    if (m_centralWidget) {
        m_colorPickerOverlay = new widgets::ColorPickerOverlay(m_centralWidget);

        connect(m_colorPickerOverlay, &widgets::ColorPickerOverlay::colorSelected, this,
            [this](const QColor& color) {
                if (m_activeColorButton) {
                    m_activeColorButton->setColor(color);
                }
            });

        connect(m_colorPickerOverlay, &widgets::ColorPickerOverlay::hidden, this,
            [this]() { m_activeColorButton = nullptr; });
    }
}

void OverlayCoordinator::showCommandPalette()
{
    if (!m_paletteOverlay) {
        m_paletteOverlay = new widgets::CommandPaletteOverlay(m_tabContent);
    }

    if (m_paletteOverlay->isActive())
        return;
    m_paletteOverlay->showPalette();
}

void OverlayCoordinator::showFirstLaunchUpdateMessage()
{
    ensureUpdateMessageOverlay();

    if (!m_updateMessageOverlay)
        return;
    if (m_updateMessageOverlay->isActive())
        return;
    m_updateMessageOverlay->showMessage();
}

void OverlayCoordinator::ensureUpdateMessageOverlay()
{
    if (m_updateMessageOverlay || !m_tabContent) {
        return;
    }

    QWidget* overlayParent
        = m_centralWidget ? m_centralWidget : static_cast<QWidget*>(m_tabContent);
    m_updateMessageOverlay = new widgets::UpdateMessageOverlay(overlayParent, m_tabContent);
    connect(m_updateMessageOverlay, &widgets::UpdateMessageOverlay::hidden, this, [this]() {
        emit firstLaunchUpdateMessageDismissed();

        if (!m_showReleaseNotesAfterUpdateMessage) {
            return;
        }

        m_showReleaseNotesAfterUpdateMessage = false;
        showReleaseNotesOverlay();
    });
    connect(m_updateMessageOverlay, &widgets::UpdateMessageOverlay::releaseNotesRequested, this,
        [this]() { m_showReleaseNotesAfterUpdateMessage = true; });
}

void OverlayCoordinator::showReleaseNotesOverlay()
{
    if (!m_releaseNotesOverlay) {
        m_releaseNotesOverlay = new widgets::ReleaseNotesOverlay(m_tabContent);
    }

    if (m_releaseNotesOverlay->isActive()) {
        return;
    }

    m_releaseNotesOverlay->showOverlay();
}

void OverlayCoordinator::showColorPicker(const QColor& initialColor, QWidget* sourceButton)
{
    if (!m_colorPickerOverlay) {
        return;
    }

    if (auto* colorButton = qobject_cast<widgets::ColorInputButton*>(sourceButton)) {
        m_activeColorButton = colorButton;
    }

    m_colorPickerOverlay->showPicker(initialColor, sourceButton);
}

} // namespace ruwa::ui::windows
