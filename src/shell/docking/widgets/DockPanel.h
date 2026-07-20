// SPDX-License-Identifier: MPL-2.0

// DockPanel.h
#ifndef RUWA_UI_DOCKING_WIDGETS_DOCKPANEL_H
#define RUWA_UI_DOCKING_WIDGETS_DOCKPANEL_H

#include "shell/docking/DockTypes.h"
#include "features/theme/manager/ThemeContext.h"
#include "features/theme/manager/ThemeColors.h"
#include "shared/resources/IconProvider.h"

#include <QFrame>
#include <optional>
#include <QString>
#include <QIcon>
#include <QMargins>
#include <QPointer>
#include <QJsonObject>

class QVariantAnimation;
class QVBoxLayout;

namespace ruwa::ui::docking {

class DockPanelTitleBar;
class DockFloatingContainer;

/**
 * @brief Base class for all dockable panels
 *
 * DockPanel is a self-contained widget that can be:
 * - Docked within the container's layout tree
 * - Floating freely inside the main window
 * - Closed and restored
 *
 * Subclasses implement createContent() to provide panel content.
 *
 * Usage:
 * @code
 * class MyPanel : public DockPanel {
 * public:
 *     MyPanel() : DockPanel("My Panel") {}
 * protected:
 *     QWidget* createContent() override {
 *         return new QLabel("Content");
 *     }
 * };
 * @endcode
 */
class DockPanel : public QFrame {
    Q_OBJECT
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(PanelState state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool floating READ isFloating NOTIFY stateChanged)

public:
    explicit DockPanel(const QString& title, QWidget* parent = nullptr);
    ~DockPanel() override;

    // === Identification ===

    DockPanelId id() const { return m_id; }

    QString title() const { return m_title; }
    void setTitle(const QString& title);
    QString persistentKey() const { return m_persistentKey.isEmpty() ? m_title : m_persistentKey; }
    void setPersistentKey(const QString& key);

    QIcon icon() const { return m_icon; }
    void setIcon(const QIcon& icon);

    /// Set icon from IconProvider (theme-aware, uses existing resources)
    void setIconType(ruwa::ui::core::IconProvider::StandardIcon iconType);
    std::optional<ruwa::ui::core::IconProvider::StandardIcon> iconType() const
    {
        return m_iconType;
    }

    // === State ===

    PanelState state() const { return m_state; }
    bool isFloating() const { return m_state == PanelState::Floating; }
    bool isDocked() const { return m_state == PanelState::Docked; }
    bool isHidden() const { return m_state == PanelState::Hidden; }

    // === Features ===

    PanelFeatures features() const { return m_features; }
    void setFeatures(PanelFeatures features);

    bool isClosable() const { return m_features.testFlag(PanelFeature::Closable); }
    void setClosable(bool closable);

    bool isMovable() const { return m_features.testFlag(PanelFeature::Movable); }
    void setMovable(bool movable);

    bool isFloatable() const { return m_features.testFlag(PanelFeature::Floatable); }
    void setFloatable(bool floatable);

    bool isResizable() const { return m_features.testFlag(PanelFeature::Resizable); }
    void setResizable(bool resizable);

    /// Floating-only: true = docking previews/zones are enabled during drag.
    bool isDockable() const { return m_features.testFlag(PanelFeature::Dockable); }
    void setDockable(bool dockable);

    bool isTitleBarVisible() const { return m_titleBarVisible; }
    void setTitleBarVisible(bool visible);

    // === Size Hints ===

    PanelSizeHints sizeHints() const { return m_sizeHints; }
    void setSizeHints(const PanelSizeHints& hints);

    void setMinimumPanelSize(int width, int height);
    void setMaximumPanelSize(int width, int height);
    void setPreferredPanelSize(int width, int height);

    // === User Preferred Sizes (remembered after manual resize) ===

    /**
     * @brief Set user's preferred width for horizontal docking (Left/Right)
     * Pass -1 to clear the remembered size
     */
    void setUserHorizontalDockedWidth(int width);

    /**
     * @brief Set user's preferred height for vertical docking (Top/Bottom)
     * Pass -1 to clear the remembered size
     */
    void setUserVerticalDockedHeight(int height);

    /**
     * @brief Set user's preferred docked size (legacy, sets both directions)
     * For horizontal split: sets width
     * For vertical split: sets height
     * Pass -1 to clear the remembered size
     */
    void setUserDockedSize(int width, int height);

    /**
     * @brief Set user's preferred floating size (called after floating container resize)
     * Pass -1 to clear the remembered size
     */
    void setUserFloatingSize(int width, int height);

    /**
     * @brief Get effective width for horizontal docking (Left/Right)
     */
    int effectiveHorizontalDockedWidth() const;

    /**
     * @brief Get effective height for vertical docking (Top/Bottom)
     */
    int effectiveVerticalDockedHeight() const;

    /**
     * @brief Get effective size for docked state (legacy)
     * Returns user size if set, otherwise default preferred size
     */
    QSize effectiveDockedSize() const;

    /**
     * @brief Get effective size for floating state
     * Returns user size if set, otherwise default preferred size
     */
    QSize effectiveFloatingSize() const;

    /**
     * @brief Check if panel has user-set horizontal docked size
     */
    bool hasUserHorizontalDockedSize() const { return m_sizeHints.hasUserHorizontalDockedSize(); }

    /**
     * @brief Check if panel has user-set vertical docked size
     */
    bool hasUserVerticalDockedSize() const { return m_sizeHints.hasUserVerticalDockedSize(); }

    /**
     * @brief Check if panel has user-set docked size (any direction)
     */
    bool hasUserDockedSize() const { return m_sizeHints.hasUserDockedSize(); }

    /**
     * @brief Check if panel has user-set floating size
     */
    bool hasUserFloatingSize() const { return m_sizeHints.hasUserFloatingSize(); }

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

    // === Persistent Panel State ===

    /// Optional panel-specific state persisted together with dock layout.
    virtual QJsonObject savePanelState() const;
    virtual void restorePanelState(const QJsonObject& state);

    // === Corner Radius ===

    /// Structure holding radius for each corner
    struct CornerRadii {
        int topLeft = 0;
        int topRight = 0;
        int bottomLeft = 0;
        int bottomRight = 0;

        bool operator==(const CornerRadii& other) const
        {
            return topLeft == other.topLeft && topRight == other.topRight
                && bottomLeft == other.bottomLeft && bottomRight == other.bottomRight;
        }
        bool operator!=(const CornerRadii& other) const { return !(*this == other); }
    };

    /// Get current corner radii
    CornerRadii cornerRadii() const { return m_cornerRadii; }

    /// Recalculate corner radii based on position in container
    void updateCornerRadii();

    /// Base radius for rounded corners (default 6px)
    void setBaseCornerRadius(int radius) { m_baseCornerRadius = radius; }
    int baseCornerRadius() const { return m_baseCornerRadius; }

    // === Subtitle (widget area below the title) ===

    /**
     * @brief Set a widget to display below the title bar text.
     *
     * The subtitle area visually extends the title bar: it shares the same
     * background colour and the bottom border moves below it. Any QWidget
     * can be used (buttons, sliders, complex layouts, etc.).
     *
     * Ownership is transferred to the DockPanel. Pass nullptr to remove.
     */
    void setSubtitleWidget(QWidget* widget);
    QWidget* subtitleWidget() const { return m_subtitleContent; }

    /// Apply margins to the root layout of the subtitle widget, if it has one.
    void setSubtitleContentMargins(const QMargins& margins);
    void setSubtitleContentMargins(int left, int top, int right, int bottom);
    std::optional<QMargins> subtitleContentMargins() const { return m_subtitleContentMargins; }

    /// Apply spacing to the root layout of the subtitle widget, if it has one.
    void setSubtitleContentSpacing(int spacing);
    std::optional<int> subtitleContentSpacing() const { return m_subtitleContentSpacing; }

    /// Override subtitle background colour. By default it matches the title bar
    /// (surfaceAlt). Pass an invalid QColor to reset to the default.
    void setSubtitleBackground(const QColor& color);
    QColor subtitleBackground() const { return m_subtitleBg; }

    // === Interactive Title Widgets ===

    /// Attach a small interactive widget to the left side of the title bar.
    /// Ownership is transferred to the DockPanel. Pass nullptr to remove.
    void setTitleLeadingWidget(QWidget* widget);
    QWidget* titleLeadingWidget() const;

    /// Attach a small interactive widget to the right side of the title bar.
    /// Ownership is transferred to the DockPanel. Pass nullptr to remove.
    void setTitleTrailingWidget(QWidget* widget);
    QWidget* titleTrailingWidget() const;

    /// Keep interactive title widgets visible when the panel uses floating chrome.
    void setTitleInteractiveWidgetsVisibleWhenFloating(bool visible);
    bool titleInteractiveWidgetsVisibleWhenFloating() const;

    // === Components ===

    DockPanelTitleBar* titleBar() const { return m_titleBar; }
    QWidget* contentWidget() const { return m_content; }

    // === Container Info ===

    DockFloatingContainer* floatingContainer() const { return m_floatingContainer; }

    // === Actions ===

    /// Toggle between docked and floating state
    void toggleFloating();

    /// Close the panel
    void closePanel();

    /// Bring panel to front (for floating panels)
    void raise();

    // === Docking Animation ===

    /**
     * @brief Animate panel from source geometry to current layout position
     * @param sourceGeom Starting geometry (floating container's geometry in parent coords)
     * @param targetGeom Target geometry (where panel will be docked)
     * @param duration Animation duration in ms (0 = use default)
     */
    void animateDocking(const QRect& sourceGeom, const QRect& targetGeom, int duration = 0);

    /**
     * @brief Check if docking animation is running
     */
    bool isAnimatingDocking() const { return m_animatingDocking; }

    /**
     * @brief Set default docking animation duration
     */
    void setDockingAnimationDuration(int ms) { m_dockingAnimationDuration = ms; }
    int dockingAnimationDuration() const { return m_dockingAnimationDuration; }

    /// Apply a theme change that was deferred while this panel was hidden
    /// (background tab). No-op if nothing is pending. Called by the owning
    /// WorkspaceTab during its deferred theme refresh, behind the loading
    /// overlay, so the heavy per-panel work is not done for every open project
    /// at theme-apply time.
    void flushPendingTheme();
    bool hasPendingTheme() const { return m_themePending; }

signals:
    void titleChanged(const QString& title);
    void iconChanged(const QIcon& icon);
    void stateChanged(PanelState state);
    void featuresChanged(PanelFeatures features);
    void closeRequested();
    void floatRequested();
    void dockRequested();
    void dockingAnimationFinished();
    void userHorizontalDockedWidthChanged(int width);
    void userVerticalDockedHeightChanged(int height);
    void userDockedSizeChanged(int width, int height); // Legacy
    void userFloatingSizeChanged(int width, int height);

protected:
    /// Override to create panel content (called once on first show)
    virtual QWidget* createContent() = 0;

    /// Called when theme changes
    virtual void onThemeChanged();

    /// Called on QEvent::LanguageChange so subclasses can re-apply tr() strings
    /// (title, tooltips, labels) live, without recreating the workspace.
    /// The default implementation re-applies the translatable title if one was
    /// registered via setTranslatableTitle(); overrides should call the base.
    virtual void retranslateUi();

    /// Register the panel's title as a translatable source string so the base
    /// class can re-translate it live on language change. Pass the same literal
    /// used with tr() in the constructor, e.g. setTranslatableTitle(QT_TR_NOOP("Color")).
    /// Also applies the current translation immediately.
    void setTranslatableTitle(const char* sourceText);

    /// Access to theme colors
    const ruwa::ui::core::ThemeColors& colors() const;

    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

private slots:
    void onDockingAnimationValueChanged(const QVariant& value);
    void onDockingAnimationFinished();
    /// themeChanged handler: defers the heavy applyTheme() work while hidden.
    void handleThemeChanged();

private:
    friend class DockManager;
    friend class DockContainerWidget;
    friend class DockFloatingContainer;
    friend class DockPanelTitleBar;

    void setupUI();
    void setupDockingAnimation();
    void applyTheme();
    void updatePanelStyle();
    void setState(PanelState state);
    void setFloatingContainer(DockFloatingContainer* container);
    void ensureContentCreated();
    CornerRadii calculateCornerRadii() const;
    void applyDockingAnimationFrame(double progress);
    void applySubtitleContentLayoutOptions();
    void updateBodyMask();
    void updateContentTransitionGeometry();
    void scheduleContentTransitionGeometryUpdate();
    void updateOverlayVisibility();
    void setOverlayAnimationSuspended(bool suspended);

private:
    // Identity
    DockPanelId m_id;
    QString m_title;
    const char* m_titleSource = nullptr; ///< untranslated title for live retranslation
    QString m_persistentKey;
    QIcon m_icon;
    std::optional<ruwa::ui::core::IconProvider::StandardIcon> m_iconType;

    // State
    PanelState m_state = PanelState::Docked;
    PanelFeatures m_features = PanelFeature::Default;
    PanelSizeHints m_sizeHints;

    // Components
    DockPanelTitleBar* m_titleBar = nullptr;
    QWidget* m_bodyContainer = nullptr;
    QVBoxLayout* m_bodyLayout = nullptr;
    QWidget* m_borderOverlay = nullptr;
    QWidget* m_contentTransition = nullptr;
    QWidget* m_subtitleContainer = nullptr; // wrapper with bg + border
    QWidget* m_subtitleContent = nullptr; // user-supplied widget
    std::optional<QMargins> m_subtitleContentMargins;
    std::optional<int> m_subtitleContentSpacing;
    QColor m_subtitleBg; // custom bg (invalid = default)
    QWidget* m_content = nullptr;
    bool m_contentCreated = false;
    bool m_titleBarVisible = true;
    bool m_overlayAnimationSuspended = false;
    bool m_contentTransitionUpdateQueued = false;

    // Containers
    DockFloatingContainer* m_floatingContainer = nullptr;

    // Corner radii
    CornerRadii m_cornerRadii;
    int m_baseCornerRadius = 6;

    // Theme
    ruwa::ui::core::ThemeContext m_theme;
    bool m_themePending = false; ///< themeChanged arrived while hidden; flush on activation

    // Docking animation
    QPointer<QVariantAnimation> m_dockingAnimation;
    bool m_animatingDocking = false;
    int m_dockingAnimationDuration = 250; // ms
    QRect m_dockingStartGeom;
    QRect m_dockingTargetGeom;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_WIDGETS_DOCKPANEL_H
