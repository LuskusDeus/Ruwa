// SPDX-License-Identifier: MPL-2.0

// RecentProjectItem.cpp
#include "RecentProjectItem.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/PaintingUtils.h"

#include <QPainter>
#include <QPainterPath>
#include <QVariantList>

namespace ruwa::ui::widgets {

namespace {
const int BASE_HEIGHT = 80;
const int BASE_LEFT_PADDING = 12;
const int BASE_RIGHT_PADDING = 16;
const int BASE_PREVIEW_SIZE = 56;
const int BASE_ICON_SIZE = 14;
const int BASE_SPACING = 10;
const int BASE_NAME_FONT_SIZE = 10;
const int BASE_SIZE_FONT_SIZE = 8;
const int BASE_DATE_FONT_SIZE = 8;
} // namespace

RecentProjectItem::RecentProjectItem(const QString& projectName, const QString& filePath,
    const QString& lastModified, const QString& fileSize, bool previewEnabled, const QPixmap& icon,
    const QPixmap& thumbnail, QWidget* parent)
    : CardButton(CardButton::LayoutVariant::Row, parent)
    , m_projectName(projectName)
    , m_filePath(filePath)
    , m_lastModified(lastModified)
    , m_fileSize(fileSize)
    , m_previewEnabled(previewEnabled)
    , m_icon(icon)
    , m_thumbnail(thumbnail)
{
    // Apply initial scaled sizes
    updateScaledSizes();

    // Connect to theme changes
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &RecentProjectItem::onThemeChanged);
}

void RecentProjectItem::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    setFixedHeight(theme.scaled(BASE_HEIGHT));

    QFont f = font();
    f.setPointSize(theme.scaledFontSize(BASE_NAME_FONT_SIZE));
    setFont(f);
}

void RecentProjectItem::drawCardContent(QPainter& painter, const QRectF& rect)
{
    Q_UNUSED(rect);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    // Layout: [Preview square] [Small icon] [Name]
    //                              [File size]     [Date]
    const int leftPadding = theme.scaled(BASE_LEFT_PADDING);
    const int rightPadding = theme.scaled(BASE_RIGHT_PADDING);
    const int previewSize = theme.scaled(BASE_PREVIEW_SIZE);
    const int iconSize = theme.scaled(BASE_ICON_SIZE);
    const int spacing = theme.scaled(BASE_SPACING);

    int currentX = leftPadding;

    // Text color (muted -> full on hover, like LinkButton)
    QColor textColor
        = ruwa::ui::core::ThemeColors::interpolate(colors.textMuted, colors.text, hoverProgress());
    QColor dateColor = colors.textMuted;
    QColor sizeColor = colors.textMuted;

    // 1. Draw preview (thumbnail with rounded corners, or placeholder)
    const int previewY = (height() - previewSize) / 2;
    QRect previewRect(currentX, previewY, previewSize, previewSize);
    const int previewRadius = theme.scaled(4);
    if (m_previewEnabled && !m_thumbnail.isNull()) {
        QPixmap scaled = m_thumbnail.scaled(
            previewSize, previewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const int dx = (previewSize - scaled.width()) / 2;
        const int dy = (previewSize - scaled.height()) / 2;
        const QRect imageRect(
            previewRect.x() + dx, previewRect.y() + dy, scaled.width(), scaled.height());
        const int imageRadius = qMin(previewRadius, qMin(scaled.width(), scaled.height()) / 2);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::NoBrush);
        QPainterPath clipPath;
        clipPath.addRoundedRect(imageRect, imageRadius, imageRadius);
        painter.setClipPath(clipPath);
        painter.drawPixmap(imageRect.topLeft(), scaled);
        painter.setClipping(false);
    } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.surfaceElevated());
        painter.drawRoundedRect(previewRect, previewRadius, previewRadius);
        if (!m_previewEnabled) {
            const int overlayIconSize = theme.scaled(18);
            const QPoint iconTopLeft(previewRect.center().x() - overlayIconSize / 2,
                previewRect.center().y() - overlayIconSize / 2);
            QIcon eyeOffIcon = ruwa::ui::core::IconProvider::instance().getColoredIcon(
                ruwa::ui::core::IconProvider::StandardIcon::EyeDeactivated, colors.textMuted);
            eyeOffIcon.paint(&painter, QRect(iconTopLeft, QSize(overlayIconSize, overlayIconSize)));
        }
    }

    currentX += previewSize + spacing;

    // 2. Draw small icon (text-height sized)
    QRect iconRect(currentX, 0, iconSize, height());
    const int iconY = (height() - iconSize) / 2;

    QPixmap iconPixmap;
    if (!m_icon.isNull()) {
        iconPixmap
            = m_icon.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else {
        auto& iconProvider = theme.icons();
        QIcon fileIcon = iconProvider.getIcon(ruwa::ui::core::IconProvider::StandardIcon::FileNew);
        iconPixmap = fileIcon.pixmap(iconSize, iconSize, QIcon::Active);
    }
    if (!iconPixmap.isNull()) {
        painter.drawPixmap(
            currentX, iconY, ruwa::ui::painting::tintedPixmap(iconPixmap, textColor));
    }

    currentX += iconSize + spacing;

    // 3. Draw text block: name on top, file size below
    QFont nameFont = font();
    nameFont.setBold(false);
    painter.setFont(nameFont);
    QFontMetrics fmName(nameFont);
    const int lineHeight = fmName.height();
    const int textBlockHeight = lineHeight * 2 + theme.scaled(2);

    QFont sizeFont = font();
    sizeFont.setPointSize(theme.scaledFontSize(BASE_SIZE_FONT_SIZE));
    QFontMetrics fmSize(sizeFont);

    QFont dateFont = font();
    dateFont.setPointSize(theme.scaledFontSize(BASE_DATE_FONT_SIZE));
    QFontMetrics fmDate(dateFont);
    int dateWidth = fmDate.horizontalAdvance(m_lastModified);
    int availableTextWidth = width() - currentX - rightPadding - dateWidth - theme.scaled(24);

    int textBlockY = (height() - textBlockHeight) / 2;

    painter.setPen(textColor);
    QRect nameRect(currentX, textBlockY, availableTextWidth, lineHeight);
    QString elidedName = fmName.elidedText(m_projectName, Qt::ElideRight, availableTextWidth);
    painter.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);

    painter.setPen(sizeColor);
    painter.setFont(sizeFont);
    QRect sizeRect(
        currentX, textBlockY + lineHeight + theme.scaled(2), availableTextWidth, fmSize.height());
    painter.drawText(sizeRect, Qt::AlignLeft | Qt::AlignVCenter, m_fileSize);

    // 4. Draw date (right-aligned)
    painter.setPen(dateColor);
    painter.setFont(dateFont);
    QRect dateRect(width() - rightPadding - dateWidth, 0, dateWidth, height());
    painter.drawText(dateRect, Qt::AlignRight | Qt::AlignVCenter, m_lastModified);
}

QVariantMap RecentProjectItem::contextMenuContext() const
{
    QVariantList actions;

    QVariantMap openAction;
    openAction.insert(QStringLiteral("id"), recent_project_context_actions::Open);
    openAction.insert(QStringLiteral("text"), tr("Open"));
    openAction.insert(QStringLiteral("standardIcon"),
        static_cast<int>(ruwa::ui::core::IconProvider::StandardIcon::OpenedFolder));
    actions.append(openAction);

    QVariantMap editAction;
    editAction.insert(QStringLiteral("id"), recent_project_context_actions::Edit);
    editAction.insert(QStringLiteral("text"), tr("Edit"));
    editAction.insert(QStringLiteral("standardIcon"),
        static_cast<int>(ruwa::ui::core::IconProvider::StandardIcon::Edit));
    actions.append(editAction);

    actions.append(QVariantMap { { QStringLiteral("separator"), true } });

    QVariantMap forgetAction;
    forgetAction.insert(QStringLiteral("id"), recent_project_context_actions::Forget);
    forgetAction.insert(QStringLiteral("text"), tr("Forget"));
    forgetAction.insert(QStringLiteral("standardIcon"),
        static_cast<int>(ruwa::ui::core::IconProvider::StandardIcon::Close));
    actions.append(forgetAction);

    QVariantMap deleteAction;
    deleteAction.insert(QStringLiteral("id"), recent_project_context_actions::Delete);
    deleteAction.insert(QStringLiteral("text"), tr("Delete"));
    deleteAction.insert(QStringLiteral("standardIcon"),
        static_cast<int>(ruwa::ui::core::IconProvider::StandardIcon::Trash));
    deleteAction.insert(QStringLiteral("danger"), true);
    actions.append(deleteAction);

    return { { QStringLiteral("simpleActions"), actions } };
}

void RecentProjectItem::handleContextMenuAction(int actionId)
{
    switch (actionId) {
    case recent_project_context_actions::Open:
        click();
        break;
    case recent_project_context_actions::Edit:
        emit editRequested();
        break;
    case recent_project_context_actions::Forget:
        emit forgetRequested();
        break;
    case recent_project_context_actions::Delete:
        emit deleteRequested();
        break;
    default:
        break;
    }
}

void RecentProjectItem::onThemeChanged()
{
    updateScaledSizes();
    update();
}

} // namespace ruwa::ui::widgets
