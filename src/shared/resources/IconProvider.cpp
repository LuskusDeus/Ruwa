// SPDX-License-Identifier: MPL-2.0

#include "IconProvider.h"
#include "ResourceManager.h"
#include "features/theme/manager/ThemeManager.h"

#include <QPainter>
#include <QFile>

namespace ruwa::ui::core {

IconProvider::IconProvider()
    : QObject(nullptr)
{
}

IconProvider::~IconProvider() = default;

IconProvider& IconProvider::instance()
{
    static IconProvider instance;
    return instance;
}

QIcon IconProvider::getIcon(StandardIcon icon) const
{
    // Check cache first
    if (m_standardIconCache.contains(icon)) {
        return m_standardIconCache[icon];
    }

    QString iconPath = getStandardIconPath(icon);
    QIcon result(iconPath);

    // Cache the result
    m_standardIconCache[icon] = result;

    return result;
}

QIcon IconProvider::getIcon(const QString& name) const
{
    // Check cache first
    if (m_iconCache.contains(name)) {
        return m_iconCache[name];
    }

    QString iconPath = getThemedIconPath(name);
    QIcon result(iconPath);

    // Cache the result
    m_iconCache[name] = result;

    return result;
}

QIcon IconProvider::getColoredIcon(StandardIcon icon, const QColor& color) const
{
    QPixmap pixmap = getPixmap(icon, QSize(24, 24));
    return createColoredIcon(pixmap, color);
}

QIcon IconProvider::getColoredIcon(const QString& name, const QColor& color) const
{
    QPixmap pixmap = getPixmap(name, QSize(24, 24));
    return createColoredIcon(pixmap, color);
}

QPixmap IconProvider::getPixmap(StandardIcon icon, const QSize& size) const
{
    QString iconPath = getStandardIconPath(icon);
    QPixmap pixmap(iconPath);

    if (!pixmap.isNull() && size.isValid() && !size.isEmpty() && pixmap.size() != size) {
        pixmap = pixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    return pixmap;
}

QPixmap IconProvider::getPixmap(const QString& name, const QSize& size) const
{
    QString iconPath = getThemedIconPath(name);
    QPixmap pixmap(iconPath);

    if (!pixmap.isNull() && size.isValid() && !size.isEmpty() && pixmap.size() != size) {
        pixmap = pixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    return pixmap;
}

QPixmap IconProvider::getApplicationLogoPixmap(const QSize& size) const
{
    QPixmap pixmap = getPixmap(StandardIcon::TransparentLogoIcon, size);
    if (!pixmap.isNull()) {
        return pixmap;
    }

    return getPixmap(StandardIcon::OpaqueLogoIcon, size);
}

void IconProvider::reloadIcons()
{
    m_iconCache.clear();
    m_standardIconCache.clear();
    emit iconsReloaded();
}

QString IconProvider::getStandardIconPath(StandardIcon icon) const
{
    QString baseName = getStandardIconName(icon);
    return getThemedIconPath(baseName);
}

QString IconProvider::getStandardIconName(StandardIcon icon) const
{
    switch (icon) {
    // File operations
    case StandardIcon::FileNew:
        return "file-new";
    case StandardIcon::Save:
        return "Save";

    // Edit operations
    case StandardIcon::Edit:
        return "Edit";
    case StandardIcon::UndoArrow:
        return "UndoArrow";

    // Tools
    case StandardIcon::Hand:
        return "Hand";
    case StandardIcon::Brush:
        return "Brush";
    case StandardIcon::Blur:
        return "Blur";
    case StandardIcon::Smudge:
        return "Smudge";
    case StandardIcon::Eraser:
        return "Eraser";
    case StandardIcon::Pencil:
        return "Pencil";
    case StandardIcon::FillColor:
        return "FillColor";
    case StandardIcon::SmartFillColor:
        return "SmartFillColor";
    case StandardIcon::Curve:
        return "Curve";
    case StandardIcon::Zoom:
        return "Zoom";
    case StandardIcon::Text:
        return "Text";
    case StandardIcon::Bold:
        return "Bold";
    case StandardIcon::Italic:
        return "Italic";
    case StandardIcon::Underline:
        return "Underline";
    case StandardIcon::Eyedropper:
        return "Eyedropper";
    case StandardIcon::Move:
        return "Move";
    case StandardIcon::Lasso:
        return "Lasso";
    case StandardIcon::LassoFill:
        return "LassoFill";
    case StandardIcon::SquareSelection:
        return "SquareSelection";
    case StandardIcon::CircleSelection:
        return "CircleSelection";
    case StandardIcon::RotateView:
        return "RotateView";
    case StandardIcon::CanvasResize:
        return "CanvasResize";
    case StandardIcon::Crop:
        return "Crop";
    case StandardIcon::Camera:
        return "Camera";

    // Navigation
    case StandardIcon::Home:
        return "Home";
    case StandardIcon::Settings:
        return "Settings";

    // UI elements
    case StandardIcon::Close:
        return "Close";
    case StandardIcon::Minimize:
        return "Minimize";
    case StandardIcon::Maximize:
        return "Maximize";
    case StandardIcon::MinimizeWindow:
        return "MinimizeWindow";
    case StandardIcon::ArrowDown:
        return "ArrowDown";
    case StandardIcon::ArrowUp:
        return "ArrowUp";
    case StandardIcon::Confirm:
        return "Confirm";
    case StandardIcon::Appearance:
        return "Appearance";
    case StandardIcon::Performance:
        return "Performance";
    case StandardIcon::Find:
        return "Find";
    case StandardIcon::List:
        return "List";
    case StandardIcon::CardView:
        return "CardView";
    case StandardIcon::BasicFile:
        return "BasicFile";
    case StandardIcon::Import:
        return "Import";
    case StandardIcon::Export:
        return "Export";
    case StandardIcon::OpenedFolder:
        return "OpenedFolder";
    case StandardIcon::Brushpack:
        return "Brushpack";

    // Layers
    case StandardIcon::Folder:
        return "Folder";
    case StandardIcon::Trash:
        return "Remove";
    case StandardIcon::Duplicate:
        return "Duplicate";
    case StandardIcon::Eye:
        return "Eye";
    case StandardIcon::EyeDeactivated:
        return "EyeDeactivated";
    case StandardIcon::LayerMask:
        return "LayerMask";
    case StandardIcon::AdjustmentLayer:
        return "AdjustmentLayer";

    // Panels
    case StandardIcon::ColorPanel:
        return "ColorPanel";
    case StandardIcon::ComposerPanel:
        return "ComposerPanel";
    case StandardIcon::LayerEffectsPanel:
        return "LayerEffectsPanel";
    case StandardIcon::LayersPanel:
        return "LayersPanel";
    case StandardIcon::ToolsPanel:
        return "ToolsPanel";

    // Social
    case StandardIcon::Discord:
        return "Discord";
    case StandardIcon::Telegram:
        return "Telegram";
    case StandardIcon::Github:
        return "Github";

    // Logo
    case StandardIcon::OpaqueLogoIcon:
        return "OpaqueLogoIcon";
    case StandardIcon::TransparentLogoIcon:
        return "TransparentLogoIcon";

    // Misc
    case StandardIcon::Alpha:
        return "Alpha";
    case StandardIcon::ZeroAlphaBrush:
        return "ZeroAlphaBrush";
    case StandardIcon::Lock:
        return "Lock";
    case StandardIcon::Link:
        return "Link";
    case StandardIcon::FlipVertical:
        return "FlipVertical";
    case StandardIcon::FlipHorizontal:
        return "FlipHorizontal";

    // Docking / panels
    case StandardIcon::DockLayout:
        return "DockLayout";
    case StandardIcon::LayoutSwitch:
        return "LayoutSwitch";
    case StandardIcon::DetachPanel:
        return "DetachPanel";
    case StandardIcon::Resize:
        return "Resize";
    case StandardIcon::RotationCorner:
        return "RotationCorner";

    // Color
    case StandardIcon::Swap:
        return "Swap";

    // Overflow / "more" menu
    case StandardIcon::Dots:
        return "DotsIcon";

    // Liquify (tool + modes)
    case StandardIcon::Liquify:
        return "Liquify";
    case StandardIcon::LiquifyPush:
        return "LiquifyPush";
    case StandardIcon::LiquifyTwirlCW:
        return "LiquifyTwirlCW";
    case StandardIcon::LiquifyTwirlCCW:
        return "LiquifyTwirlCCW";
    case StandardIcon::LiquifyGrow:
        return "LiquifyGrow";
    case StandardIcon::LiquifyShrink:
        return "LiquifyShrink";
    }
    return QString();
}

QIcon IconProvider::createColoredIcon(const QPixmap& source, const QColor& color) const
{
    if (source.isNull()) {
        return QIcon();
    }

    QPixmap colored = source;
    QPainter painter(&colored);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(colored.rect(), color);
    painter.end();

    return QIcon(colored);
}

bool IconProvider::shouldUseLightVariant() const
{
    // Use light icons on dark backgrounds
    const auto& colors = ThemeManager::instance().colors();

    // Calculate luminance of window background
    int r = colors.background.red();
    int g = colors.background.green();
    int b = colors.background.blue();

    // Relative luminance calculation
    double luminance = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;

    // If background is dark (luminance < 0.5), use light icons
    return luminance < 0.5;
}

QString IconProvider::getThemedIconPath(const QString& baseName) const
{
    // FileNew uses "file-new" as name but the QRC alias is "NewFile"
    if (baseName == "file-new") {
        return QStringLiteral(":/icons/NewFile");
    }

    // Try direct QRC path first (most icons use their alias name directly)
    QString directPath = QString(":/icons/%1").arg(baseName);
    if (QFile::exists(directPath)) {
        return directPath;
    }

    QString variant = shouldUseLightVariant() ? "light" : "dark";

    // Try variant-specific icon first
    QString variantPath = QString(":/icons/%1/%2.svg").arg(variant, baseName);
    if (QFile::exists(variantPath)) {
        return variantPath;
    }

    // Try PNG variant
    variantPath = QString(":/icons/%1/%2.png").arg(variant, baseName);
    if (QFile::exists(variantPath)) {
        return variantPath;
    }

    // Fallback to common icons folder
    QString commonPath = QString(":/icons/common/%1.svg").arg(baseName);
    if (QFile::exists(commonPath)) {
        return commonPath;
    }

    commonPath = QString(":/icons/common/%1.png").arg(baseName);
    if (QFile::exists(commonPath)) {
        return commonPath;
    }

    // Try root icons folder with lowercase
    QString rootPath = QString(":/icons/%1.svg").arg(baseName);
    if (QFile::exists(rootPath)) {
        return rootPath;
    }

    rootPath = QString(":/icons/%1.png").arg(baseName);
    if (QFile::exists(rootPath)) {
        return rootPath;
    }

    // Try with capitalized first letter (Home, Settings, etc.)
    QString capitalizedName = baseName;
    if (!capitalizedName.isEmpty()) {
        capitalizedName[0] = capitalizedName[0].toUpper();
    }

    rootPath = QString(":/icons/%1").arg(capitalizedName);
    if (QFile::exists(rootPath)) {
        return rootPath;
    }

    return QString();
}

} // namespace ruwa::ui::core
