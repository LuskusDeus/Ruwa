// SPDX-License-Identifier: MPL-2.0

// TopBar.h
#ifndef RUWA_UI_WIDGETS_TOPBAR_TOPBAR_H
#define RUWA_UI_WIDGETS_TOPBAR_TOPBAR_H

#include <QWidget>
#include <QList>
#include <QPointer>
#include <QTimer>
#include "MenuPopup.h"
#include "shell/docking/state/DockLayoutPreset.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "shared/widgets/MenuButton.h"
#include "shared/widgets/Separator.h"
#include "shared/widgets/ToolButton.h"

class QHBoxLayout;
class QHideEvent;
class QMouseEvent;
class QResizeEvent;

namespace ruwa::ui::widgets {

struct MenuItem;
class TopBarGutterBand;
class LayoutPresetsPopup;

// ============================================================================
// LogoButton - Clickable app logo button
// ============================================================================

class LogoButton : public ruwa::ui::workspace::ToolButton {
    Q_OBJECT

public:
    explicit LogoButton(QWidget* parent = nullptr);
};

// ============================================================================
// WindowControlButton - Minimize/Maximize/Close buttons
// ============================================================================

class WindowControlButton : public BaseAnimatedButton {
    Q_OBJECT

public:
    enum class Type { Minimize, Maximize, Close };

    explicit WindowControlButton(Type type, QWidget* parent = nullptr);

    void setMaximized(bool maximized);
    Type buttonType() const { return m_type; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void drawMinimizeIcon(QPainter& painter, const QRectF& rect, const QColor& color);
    void drawMaximizeIcon(QPainter& painter, const QRectF& rect, const QColor& color);
    void drawRestoreIcon(QPainter& painter, const QRectF& rect, const QColor& color);
    void drawCloseIcon(QPainter& painter, const QRectF& rect, const QColor& color);

    Type m_type;
    bool m_isMaximized = false;
};

// ============================================================================
// LayoutSwitchBarButton - Title bar layout action (icon from IconProvider)
// ============================================================================

class LayoutSwitchBarButton : public ruwa::ui::workspace::ToolButton {
    Q_OBJECT

public:
    explicit LayoutSwitchBarButton(QWidget* parent = nullptr);
};

// ============================================================================
// TopBar - Custom title bar with menu, tabs, and window controls
// ============================================================================

class TopBar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal borderGlowProgress READ borderGlowProgress WRITE setBorderGlowProgress)

    friend class TopBarGutterBand;

public:
    explicit TopBar(QWidget* parent = nullptr);
    ~TopBar() override = default;

    // Accessors for QWindowKit hit testing
    QWidget* menuButtonContainer() const { return m_buttonContainer; }
    QWidget* tabBarContainer() const { return m_tabBarContainer; }

    // Window control buttons for QWindowKit
    QWidget* minimizeButton() const { return m_minimizeBtn; }
    QWidget* maximizeButton() const { return m_maximizeBtn; }
    QWidget* closeButton() const { return m_closeBtn; }
    QWidget* layoutSwitchButton() const { return m_layoutSwitchBtn; }
    QWidget* layoutSwitchSeparator() const { return m_windowControlsSeparator; }

    void setCompactMode(bool compact);

    /// Enable layout switch only when a workspace tab is active (MainWindow syncs on tab change).
    void setLayoutSwitchEnabled(bool enabled);

    /// Initialize overlay system (call after adding to MainWindow)
    void initOverlay(QWidget* mainWindow);

    qreal borderGlowProgress() const { return m_borderGlowProgress; }
    void setBorderGlowProgress(qreal progress);

    /// Extra title-bar regions for QWindowKit hit-test (gutter bands; must not use
    /// WA_TransparentForMouseEvents).
    QList<QWidget*> qwkExtraHitTestWidgets() const;

public slots:
    /// Called when window state changes (to update maximize/restore icon)
    void onWindowStateChanged(Qt::WindowState state);

    /// Sync panel visibility state from active workspace (for menu checkmarks)
    void setPanelsVisibilityState(bool toolsVisible, bool brushesVisible, bool layersVisible,
        bool layerPropertiesVisible, bool layerEffectsVisible, bool colorVisible,
        bool composerVisible = true);

    /// Sync canvas widgets visibility state from active workspace (for menu checkmarks)
    void setCanvasWidgetsVisibilityState(
        bool joystickVisible, bool brushControlVisible, bool toolStateOverlayVisible);

signals:
    // Logo
    void homeRequested();

    // Window controls
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();
    /// User picked a dock layout preset from the title-bar dropdown.
    void dockLayoutPresetChosen(const ruwa::ui::docking::DockLayoutPreset& preset);
    /// User chose to save the active workspace layout as a new preset ("+" in layout menu).
    void dockLayoutNewPresetFromCurrentRequested();
    /// User chose to export the active workspace layout to a JSON file.
    void dockLayoutExportRequested();
    /// User chose to import a workspace layout from a JSON file.
    void dockLayoutImportRequested();

    // File menu
    void fileNewRequested();
    void fileOpenRequested();
    void fileSaveRequested();
    void fileSaveAsRequested();
    void fileImportImagesRequested();
    void fileCloseRequested();
    void fileExitRequested();

    // Edit menu
    void editUndoRequested();
    void editRedoRequested();
    void editPreferencesRequested();

    // View menu
    void viewZoomInRequested();
    void viewZoomOutRequested();
    void viewZoomFitRequested();

    // Help menu
    void helpAboutRequested();
    void helpDocumentationRequested();

    // Panels visibility (View → Panels submenu)
    void panelsToolsVisibilityChanged(bool visible);
    void panelsBrushesVisibilityChanged(bool visible);
    void panelsLayersVisibilityChanged(bool visible);
    void panelsLayerPropertiesVisibilityChanged(bool visible);
    void panelsLayerEffectsVisibilityChanged(bool visible);
    void panelsColorVisibilityChanged(bool visible);
    void panelsComposerVisibilityChanged(bool visible);

    // Canvas widgets visibility (View → Canvas widgets submenu)
    void canvasWidgetsJoystickVisibilityChanged(bool visible);
    void canvasWidgetsBrushControlVisibilityChanged(bool visible);
    void canvasWidgetsToolStateOverlayVisibilityChanged(bool visible);

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUI();
    void createMenuButtons();
    void createWindowControls();
    MenuButton* createMenuButton(const QString& text);

    void setupFileMenu();
    void setupEditMenu();
    void setupViewMenu();
    void setupHelpMenu();
    void retranslateUi();

    void showPopupForButton(MenuButton* button);
    void hideAllPopups();
    void onPopupHidden();
    void syncChromeOverlayState();
    void onLayoutSwitchClicked();
    bool isLayoutPresetsChromeWidget(const QWidget* widget) const;
    void onLeaveCloseTimer();
    void startMessagePopupBorderGlow();
    void updateButtonStates();
    QList<MenuItem> getItemsForButton(MenuButton* button);
    bool isInWorkspace() const;
    bool isActiveWorkspaceInExportMode() const;
    QList<MenuItem> fileItemsWithEnabledState() const;
    QList<MenuItem> editItemsWithEnabledState() const;
    QList<MenuItem> viewItemsWithEnabledState();
    MenuItem buildPanelsMenuItem();
    MenuItem buildCanvasWidgetsMenuItem();
    MenuButton* menuButtonAt(const QPoint& globalPos) const;
    MenuButton* gutterMenuButtonAt(const QPoint& globalPos) const;
    bool isMenuPopupSessionOpen() const;
    bool isCursorOverMenuChrome(const QPoint& globalPos) const;
    bool isMenuOrButton(QWidget* widget) const;
    void armMenuCloseWatchdog(int delayMs = 120);
    void updateScaledSizes();
    void updateOverlayMessageAnchor();
    void updateMessagePopupGlowGeometry();
    /// Theme-scaled outer gutter (matches painted card inset).
    int visualInsetPx() const;
    void updateGutterBandGeometries();
    void deliverPendingGutterPressRelease(QMouseEvent* event);
    bool isGutterBandWidget(const QWidget* w) const;
    QWidget* resolveGutterEventTarget(const QPoint& topBarLocal) const;
    bool computeGutterForwardedInnerPoint(const QPoint& topBarPos, QPoint* outInner) const;
    bool tryForwardFrameGutterMouseEvent(QMouseEvent* event);
    bool routeMarginMouseFromGutterBand(TopBarGutterBand* band, QMouseEvent* sourceEvent);
    void routeMarginReleaseFromGutterBand(TopBarGutterBand* band, QMouseEvent* sourceEvent);

    bool mapGutterToContentLocal(const QPoint& topBarLocal, QPoint* outContentLocal) const;
    void clearGutterSyntheticHover();
    void armGutterSyntheticHover(QWidget* w);
    void refreshGutterSyntheticHover();
    void onGutterHoverPollTimeout();

private slots:
    void onThemeChanged();
    void onLanguageChanged();

private:
    QHBoxLayout* m_mainLayout = nullptr;

    // Left side: logo + menu buttons
    LogoButton* m_logoBtn = nullptr;
    QWidget* m_buttonContainer = nullptr;
    MenuButton* m_fileBtn = nullptr;
    MenuButton* m_editBtn = nullptr;
    MenuButton* m_viewBtn = nullptr;
    MenuButton* m_helpBtn = nullptr;

    // Center: separator + tab bar
    Separator* m_separator = nullptr;
    QWidget* m_tabBarContainer = nullptr;

    // Right side: window controls
    QWidget* m_windowControlsContainer = nullptr;
    LayoutSwitchBarButton* m_layoutSwitchBtn = nullptr;
    Separator* m_windowControlsSeparator = nullptr;
    WindowControlButton* m_minimizeBtn = nullptr;
    WindowControlButton* m_maximizeBtn = nullptr;
    WindowControlButton* m_closeBtn = nullptr;

    // Popup menu (single shared instance, like ColorPicker)
    MenuPopup* m_menuPopup = nullptr;
    LayoutPresetsPopup* m_layoutPresetsPopup = nullptr;
    MenuButton* m_currentMenuButton = nullptr;

    QList<MenuItem> m_fileItems;
    QList<MenuItem> m_editItems;
    QList<MenuItem> m_viewItems;
    QList<MenuItem> m_helpItems;

    bool m_panelsToolsVisible = true;
    bool m_panelsBrushesVisible = true;
    bool m_panelsLayersVisible = true;
    bool m_panelsLayerPropertiesVisible = true;
    bool m_panelsLayerEffectsVisible = false;
    bool m_panelsColorVisible = true;
    bool m_panelsComposerVisible = true;

    bool m_canvasWidgetsJoystickVisible = true;
    bool m_canvasWidgetsBrushControlVisible = true;
    bool m_canvasWidgetsToolStateOverlayVisible = true;

    QWidget* m_menuContainer = nullptr;

    bool m_compactMode = false;
    bool m_anyMenuOpen = false;
    QTimer* m_leaveCloseTimer = nullptr;

    qreal m_borderGlowProgress = 0.0;
    QRectF m_messagePopupGlowRect;

    QPointer<QWidget> m_gutterHoverTarget;
    QPointer<QWidget> m_gutterPressGrab;
    QTimer* m_gutterClearPoll = nullptr;

    TopBarGutterBand* m_topGutterBand = nullptr;
    TopBarGutterBand* m_leftGutterBand = nullptr;
    TopBarGutterBand* m_rightGutterBand = nullptr;

    /// Prevents nested gutter forward (sendEvent → gutter → stack overflow).
    bool m_inGutterForward = false;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_TOPBAR_TOPBAR_H
