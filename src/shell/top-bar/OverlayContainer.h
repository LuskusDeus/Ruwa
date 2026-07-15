// SPDX-License-Identifier: MPL-2.0

// OverlayContainer.h
#ifndef RUWA_UI_WIDGETS_COMMON_OVERLAYCONTAINER_H
#define RUWA_UI_WIDGETS_COMMON_OVERLAYCONTAINER_H

#include <QWidget>
#include <QPointer>
#include <QList>
#include <QHash>

namespace ruwa::ui::widgets {

class MenuPopup;
class MessagePopup;
class LayoutPresetsPopup;

/**
 * @brief Transparent overlay container for popup widgets
 *
 * This widget covers the entire MainWindow and hosts popup widgets
 * that need to be rendered on top of everything else.
 *
 * Features:
 * - Hidden by default, shown when popups are active
 * - Catches clicks outside popups to close them
 * - Automatically resizes with parent
 */
class OverlayContainer : public QWidget {
    Q_OBJECT

public:
    explicit OverlayContainer(QWidget* parent = nullptr);
    ~OverlayContainer() override = default;

    /// Get the singleton-like instance for a parent widget
    static OverlayContainer* instance(QWidget* mainWindow);

    /// No-op (API kept for callers); overlay uses full window geometry and setMask for input.
    void setExclusionWidget(QWidget* widget);

    /// Register a popup widget (transfers ownership)
    void registerPopup(MenuPopup* popup);

    /// Dock layout presets dropdown (same overlay / mask behavior as MenuPopup).
    void registerLayoutPresetsPopup(LayoutPresetsPopup* popup);

    /// Remove a popup widget
    void unregisterPopup(MenuPopup* popup);

    /// Register message popup (for confirmations, etc.)
    void registerMessagePopup(MessagePopup* popup);

    /// Register an arbitrary popup widget (any QWidget) as an overlay child.
    /// The popup is reparented onto the overlay so it layers correctly above the
    /// GL canvas and is included in hit-test masking. The popup owns its own
    /// show/hide animations and positioning; call \ref refreshGenericPopups()
    /// after it shows or hides so the overlay updates its mask/visibility.
    void registerGenericPopup(QWidget* popup);
    void unregisterGenericPopup(QWidget* popup);

    /// Recompute overlay visibility + hit-test mask (call when a generic popup's
    /// visibility changes).
    void refreshGenericPopups();

    /// Get message popup (creates and registers if needed)
    MessagePopup* messagePopup();

    /// Y position (overlay coords) for the top edge of MessagePopup — below title bar when set from
    /// TopBar.
    void setMessagePopupAnchorY(int y);
    int messagePopupAnchorY() const { return m_messagePopupAnchorY; }

    /// Check if any popup is currently visible
    bool hasActivePopups() const;

    /// Show the overlay (call when showing a popup)
    void showOverlay();

    /// Hide the overlay (call when all popups are hidden)
    void hideOverlay();

    /// Close all active popups
    void closeAllPopups();

    /// Update mask so clicks outside menu pass through to widgets below
    void updateMaskForVisiblePopups();

private slots:
    void scheduleMaskUpdate();

signals:
    void clickedOutside();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QList<QPointer<MenuPopup>> m_popups;
    QList<QPointer<QWidget>> m_genericPopups;
    QPointer<LayoutPresetsPopup> m_layoutPresetsPopup;
    QPointer<MessagePopup> m_messagePopup;
    /// Default matches legacy offset when overlay matched content below top bar only.
    int m_messagePopupAnchorY = 12;

    static QHash<QWidget*, OverlayContainer*> s_instances;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_OVERLAYCONTAINER_H
