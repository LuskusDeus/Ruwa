// SPDX-License-Identifier: MPL-2.0

// IconProvider.h
#ifndef RUWA_UI_CORE_RESOURCES_ICONPROVIDER_H
#define RUWA_UI_CORE_RESOURCES_ICONPROVIDER_H

#include <QObject>
#include <QIcon>
#include <QPixmap>
#include <QColor>
#include <QMap>
#include <QString>

namespace ruwa::ui::core {

/**
 * @brief Provides theme-aware icons for the application
 *
 * Features:
 * - Automatic icon variants (light/dark) based on theme
 * - Icon colorization for monochrome icons
 * - Caching for performance
 * - Standard icon library
 */
class IconProvider : public QObject {
    Q_OBJECT

public:
    /// Standard application icons
    enum class StandardIcon {
        // File operations
        FileNew,
        Save,

        // Edit operations
        Edit,
        UndoArrow,

        // Tools
        Hand,
        Brush,
        Blur,
        Smudge,
        Eraser,
        Pencil,
        FillColor,
        SmartFillColor,
        Curve,
        Zoom,
        Text,
        Bold,
        Italic,
        Underline,
        Eyedropper,
        Move,
        Lasso,
        LassoFill,
        SquareSelection,
        CircleSelection,
        RotateView,
        CanvasResize,
        Crop,
        Camera,

        // Navigation
        Home,
        Settings,

        // UI elements
        Close,
        Minimize,
        Maximize,
        /// Restore / un-maximize (window chrome)
        MinimizeWindow,
        ArrowDown,
        ArrowUp,
        Confirm,
        Appearance,
        Performance,
        Find,
        List,
        CardView,
        BasicFile,
        Import,
        Export,
        OpenedFolder,
        Brushpack,

        // Layers
        Folder,
        Trash,
        Duplicate,
        Eye,
        EyeDeactivated,
        LayerMask,
        AdjustmentLayer,

        // Panels
        ColorPanel,
        NavigatorPanel,
        LayerEffectsPanel,
        LayersPanel,
        ToolsPanel,

        // Social
        Discord,
        Telegram,
        Github,

        // Logo
        OpaqueLogoIcon,
        TransparentLogoIcon,

        // Misc
        Alpha,
        ZeroAlphaBrush,
        Lock,
        Link,
        FlipVertical,
        FlipHorizontal,

        // Docking / panels (context menus)
        DockLayout,
        LayoutSwitch,
        DetachPanel,
        Resize,
        RotationCorner,

        // Color
        Swap,

        // Overflow / "more" menu (horizontal ellipsis)
        Dots,

        // Liquify (tool + modes)
        Liquify,
        LiquifyPush,
        LiquifyTwirlCW,
        LiquifyTwirlCCW,
        LiquifyGrow,
        LiquifyShrink
    };

    static IconProvider& instance();

    /// Get icon by standard type (automatically selects theme variant)
    QIcon getIcon(StandardIcon icon) const;

    /// Get icon by name from resources
    QIcon getIcon(const QString& name) const;

    /// Get icon with specific color overlay
    QIcon getColoredIcon(StandardIcon icon, const QColor& color) const;
    QIcon getColoredIcon(const QString& name, const QColor& color) const;

    /// Get pixmap for icon
    QPixmap getPixmap(StandardIcon icon, const QSize& size = QSize(24, 24)) const;
    QPixmap getPixmap(const QString& name, const QSize& size = QSize(24, 24)) const;

    /// Application logo with transparent variant preferred, opaque as fallback.
    QPixmap getApplicationLogoPixmap(const QSize& size = QSize()) const;

    /// Reload icons (call when theme changes)
    void reloadIcons();

signals:
    void iconsReloaded();

private:
    IconProvider();
    ~IconProvider() override;

    IconProvider(const IconProvider&) = delete;
    IconProvider& operator=(const IconProvider&) = delete;

    QString getStandardIconPath(StandardIcon icon) const;
    QString getStandardIconName(StandardIcon icon) const;
    QIcon createColoredIcon(const QPixmap& source, const QColor& color) const;

    bool shouldUseLightVariant() const;
    QString getThemedIconPath(const QString& baseName) const;

private:
    mutable QMap<QString, QIcon> m_iconCache;
    mutable QMap<StandardIcon, QIcon> m_standardIconCache;
};

} // namespace ruwa::ui::core

#endif // RUWA_UI_CORE_RESOURCES_ICONPROVIDER_H
