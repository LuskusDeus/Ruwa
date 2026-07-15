// SPDX-License-Identifier: MPL-2.0

// MessagePopupManager.h
#ifndef RUWA_UI_WIDGETS_TOPBAR_MESSAGEPOPUPMANAGER_H
#define RUWA_UI_WIDGETS_TOPBAR_MESSAGEPOPUPMANAGER_H

#include "MessagePopup.h"
#include <QImage>
#include <QWidget>

namespace ruwa::ui::widgets {

/**
 * @brief Global API for showing message/confirmation popups
 *
 * Usage:
 *   MessagePopupManager::show(settingsWidget, "Are you sure?",
 *       {{"Cancel", false, []{}}, {"Reset", true, []{ doReset(); }}}, 320);
 */
class MessagePopupManager {
public:
    /// Show "image copied to clipboard" toast: image preview + message, auto-hides.
    static void showImageCopied(QWidget* context, const QImage& image);

    /// Show message popup. context->window() is used to find the overlay.
    /// triggerWidget: if set, clicks on it (or its descendants) won't close the popup (avoids
    /// close-then-reopen when clicking the same button again).
    static void show(QWidget* context, const QString& message, const QList<MessageButton>& buttons,
        int width = 320, QWidget* triggerWidget = nullptr);

    /// Blocking confirmation dialog. Returns true if user clicked confirm, false if cancel or
    /// closed.
    static bool showBlocking(QWidget* context, const QString& message, const QString& confirmText,
        const QString& cancelText, int width = 320, bool confirmIsPrimary = true);

    /// Result of "Save changes?" dialog: Save (0), Discard (1), Cancel (2)
    enum class SaveChangesResult { Save = 0, Discard = 1, Cancel = 2 };

    /// Blocking "Save changes before closing?" dialog with 3 buttons.
    /// Returns Save, Discard, or Cancel. Cancel also when closed without clicking.
    static SaveChangesResult showSaveChangesBlocking(
        QWidget* context, const QString& projectNameOrPath, int width = 360);
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_TOPBAR_MESSAGEPOPUPMANAGER_H
