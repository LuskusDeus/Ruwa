// SPDX-License-Identifier: MPL-2.0

// RecentProjectCard.cpp
#include "RecentProjectCard.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"

#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVariantList>
#include <QVBoxLayout>

namespace {
QPixmap roundedThumbnail(const QPixmap& source, int maxW, int maxH, int radius)
{
    QPixmap scaled = source.scaled(maxW, maxH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const int r = qMin(radius, qMin(scaled.width(), scaled.height()) / 2);
    QPixmap result(scaled.size());
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addRoundedRect(QRect(0, 0, scaled.width(), scaled.height()), r, r);
    p.setClipPath(path);
    p.drawPixmap(0, 0, scaled);
    p.end();
    return result;
}
} // namespace

namespace ruwa::ui::widgets {

namespace {
const int BASE_CARD_WIDTH = 180;
const int BASE_CARD_HEIGHT = 220;
const int BASE_LAYOUT_PADDING = 10;
const int BASE_LAYOUT_SPACING = 8;
const int BASE_THUMBNAIL_WIDTH = 160;
const int BASE_THUMBNAIL_HEIGHT = 120;
const int BASE_THUMB_BORDER_RADIUS = 6;
const int BASE_NAME_FONT_SIZE = 9;
const int BASE_DATE_FONT_SIZE = 8;
const int BASE_NAME_MAX_HEIGHT = 36;
const int BASE_PLACEHOLDER_FONT_SIZE = 32;
} // namespace

RecentProjectCard::RecentProjectCard(const QString& projectName, const QString& filePath,
    const QString& lastModified, bool previewEnabled, const QPixmap& thumbnail, const QPixmap& icon,
    QWidget* parent)
    : CardButton(CardButton::LayoutVariant::Card, parent)
    , m_projectName(projectName)
    , m_filePath(filePath)
    , m_lastModified(lastModified)
    , m_previewEnabled(previewEnabled)
    , m_thumbnail(thumbnail)
    , m_icon(icon)
{
    m_mainLayout = new QVBoxLayout(this);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    m_thumbnailLabel = new QLabel(this);
    m_thumbnailLabel->setAttribute(Qt::WA_TranslucentBackground);
    m_thumbnailLabel->setAlignment(Qt::AlignCenter);

    if (!m_previewEnabled || (m_thumbnail.isNull() && m_icon.isNull())) {
        m_thumbnailLabel->setStyleSheet(QString(R"(
            QLabel {
                background-color: %1;
                border-radius: %2px;
                color: %3;
            }
        )")
                .arg(ruwa::ui::core::ThemeColors::adjustBrightness(colors.surface, 1.05).name())
                .arg(BASE_THUMB_BORDER_RADIUS)
                .arg(colors.textMuted.name()));
        m_thumbnailLabel->setText(
            m_previewEnabled ? QStringLiteral("\xF0\x9F\x93\x84") : QString());
    } else {
        m_thumbnailLabel->setStyleSheet(QString());
        m_thumbnailLabel->setText(QString());
    }

    m_mainLayout->addWidget(m_thumbnailLabel);

    m_nameLabel = new QLabel(m_projectName, this);
    m_nameLabel->setAttribute(Qt::WA_TranslucentBackground);
    m_nameLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(colors.textMuted.name()));
    m_nameLabel->setWordWrap(true);
    m_mainLayout->addWidget(m_nameLabel);

    m_dateLabel = new QLabel(m_lastModified, this);
    m_dateLabel->setAttribute(Qt::WA_TranslucentBackground);
    m_dateLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(colors.textMuted.name()));
    m_mainLayout->addWidget(m_dateLabel);

    m_mainLayout->addStretch();

    updateScaledSizes();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &RecentProjectCard::onThemeChanged);
}

void RecentProjectCard::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    setFixedSize(theme.scaled(BASE_CARD_WIDTH), theme.scaled(BASE_CARD_HEIGHT));

    const int padding = theme.scaled(BASE_LAYOUT_PADDING);
    const int spacing = theme.scaled(BASE_LAYOUT_SPACING);
    m_mainLayout->setContentsMargins(padding, padding, padding, padding);
    m_mainLayout->setSpacing(spacing);

    const int thumbW = theme.scaled(BASE_THUMBNAIL_WIDTH);
    const int thumbH = theme.scaled(BASE_THUMBNAIL_HEIGHT);
    const int thumbRadius = theme.scaled(BASE_THUMB_BORDER_RADIUS);
    m_thumbnailLabel->setFixedSize(thumbW, thumbH);

    if (!m_previewEnabled) {
        const int iconSize = qMin(thumbW, thumbH) / 3;
        m_thumbnailLabel->setPixmap(ruwa::ui::core::IconProvider::instance()
                .getColoredIcon(
                    ruwa::ui::core::IconProvider::StandardIcon::EyeDeactivated, colors.textMuted)
                .pixmap(iconSize, iconSize));
        m_thumbnailLabel->setAlignment(Qt::AlignCenter);
        m_thumbnailLabel->setText(QString());
    } else if (!m_thumbnail.isNull()) {
        m_thumbnailLabel->setPixmap(roundedThumbnail(m_thumbnail, thumbW, thumbH, thumbRadius));
        m_thumbnailLabel->setAlignment(Qt::AlignCenter);
        m_thumbnailLabel->setStyleSheet(QString());
    } else if (!m_icon.isNull()) {
        m_thumbnailLabel->setPixmap(roundedThumbnail(m_icon, thumbW, thumbH, thumbRadius));
        m_thumbnailLabel->setAlignment(Qt::AlignCenter);
        m_thumbnailLabel->setStyleSheet(QString());
    } else {
        m_thumbnailLabel->setPixmap(QPixmap());
        QFont thumbFont = m_thumbnailLabel->font();
        thumbFont.setPointSize(theme.scaledFontSize(BASE_PLACEHOLDER_FONT_SIZE));
        m_thumbnailLabel->setFont(thumbFont);
        m_thumbnailLabel->setText(QStringLiteral("\xF0\x9F\x93\x84"));
    }

    if (!m_previewEnabled || (m_thumbnail.isNull() && m_icon.isNull())) {
        m_thumbnailLabel->setStyleSheet(QString(R"(
            QLabel {
                background-color: %1;
                border-radius: %2px;
                color: %3;
            }
        )")
                .arg(ruwa::ui::core::ThemeColors::adjustBrightness(colors.surface, 1.05).name())
                .arg(thumbRadius)
                .arg(colors.textMuted.name()));
    }

    QFont nameFont = m_nameLabel->font();
    nameFont.setPointSize(theme.scaledFontSize(BASE_NAME_FONT_SIZE));
    nameFont.setBold(true);
    m_nameLabel->setFont(nameFont);
    m_nameLabel->setMaximumHeight(theme.scaled(BASE_NAME_MAX_HEIGHT));

    QFont dateFont = m_dateLabel->font();
    dateFont.setPointSize(theme.scaledFontSize(BASE_DATE_FONT_SIZE));
    m_dateLabel->setFont(dateFont);
}

void RecentProjectCard::paintEvent(QPaintEvent* event)
{
    CardButton::paintEvent(event);

    const QColor textColor = currentPrimaryTextColor();
    if (m_nameLabel) {
        m_nameLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(textColor.name()));
    }
}

QVariantMap RecentProjectCard::contextMenuContext() const
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

void RecentProjectCard::handleContextMenuAction(int actionId)
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

void RecentProjectCard::updateThemeColors()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    if (m_nameLabel) {
        m_nameLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(colors.textMuted.name()));
    }

    if (m_dateLabel) {
        m_dateLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(colors.textMuted.name()));
    }

    const int thumbRadius = theme.scaled(BASE_THUMB_BORDER_RADIUS);
    if (m_thumbnailLabel) {
        if (!m_previewEnabled || (m_thumbnail.isNull() && m_icon.isNull())) {
            m_thumbnailLabel->setStyleSheet(QString(R"(
                QLabel {
                    background-color: %1;
                    border-radius: %2px;
                    color: %3;
                }
            )")
                    .arg(ruwa::ui::core::ThemeColors::adjustBrightness(colors.surface, 1.05).name())
                    .arg(thumbRadius)
                    .arg(colors.textMuted.name()));
        } else {
            m_thumbnailLabel->setStyleSheet(QString());
        }
    }

    update();
}

void RecentProjectCard::onThemeChanged()
{
    updateScaledSizes();
    updateThemeColors();
}

} // namespace ruwa::ui::widgets
