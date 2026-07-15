// SPDX-License-Identifier: MPL-2.0

#include "features/settings/SettingsCategory.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>

namespace ruwa::ui::widgets {

namespace {
const int BASE_MAIN_SPACING = 16;
const int BASE_HEADER_MARGIN_BOTTOM = 8;
const int BASE_HEADER_SPACING = 12;
const int BASE_ICON_SIZE = 24;
const int BASE_CONTENT_SPACING = 8;
} // namespace

SettingsCategory::SettingsCategory(const QString& title, QWidget* parent)
    : QWidget(parent)
    , m_title(title)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    setupUI(title);
    updateScaledSizes();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &SettingsCategory::onThemeChanged);
}

void SettingsCategory::setIcon(const QIcon& icon)
{
    m_icon = icon;
    updateIconDisplay();
}

void SettingsCategory::setTitle(const QString& title)
{
    m_title = title;
    if (m_titleLabel)
        m_titleLabel->setText(title);
}

void SettingsCategory::setHeaderVisible(bool visible)
{
    if (m_headerVisible == visible) {
        return;
    }

    m_headerVisible = visible;
    if (m_headerWidget) {
        m_headerWidget->setVisible(visible);
    }
}

void SettingsCategory::setupUI(const QString& title)
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    m_headerWidget = new QWidget(this);
    m_headerLayout = new QHBoxLayout(m_headerWidget);
    m_headerLayout->setContentsMargins(0, 0, 0, 0);

    m_iconLabel = new QLabel(m_headerWidget);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_headerLayout->addWidget(m_iconLabel);

    m_titleLabel = new QLabel(title, m_headerWidget);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(colors.text.name()));
    m_headerLayout->addWidget(m_titleLabel);

    m_headerLayout->addStretch();

    m_mainLayout->addWidget(m_headerWidget);

    m_contentWidget = new QWidget(this);
    m_contentWidget->setAttribute(Qt::WA_TranslucentBackground);
    m_contentWidget->setAutoFillBackground(false);
    m_contentWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setAlignment(Qt::AlignTop);

    m_mainLayout->addWidget(m_contentWidget);
}

void SettingsCategory::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    if (m_mainLayout) {
        m_mainLayout->setSpacing(theme.scaled(BASE_MAIN_SPACING));
    }

    if (m_headerLayout) {
        const int headerMarginBottom = theme.scaled(BASE_HEADER_MARGIN_BOTTOM);
        m_headerLayout->setContentsMargins(0, 0, 0, headerMarginBottom);
        m_headerLayout->setSpacing(theme.scaled(BASE_HEADER_SPACING));
    }

    if (m_iconLabel) {
        const int iconSize = theme.scaled(BASE_ICON_SIZE);
        m_iconLabel->setFixedSize(iconSize, iconSize);
    }

    if (m_titleLabel) {
        QFont titleFont = m_titleLabel->font();
        titleFont.setPointSize(theme.scaledFontSize(12));
        m_titleLabel->setFont(titleFont);
    }

    if (m_contentLayout) {
        m_contentLayout->setSpacing(theme.scaled(BASE_CONTENT_SPACING));
    }

    updateIconDisplay();
}

void SettingsCategory::addSettingsWidget(QWidget* widget)
{
    m_settingsWidgets.append(widget);
    m_contentLayout->addWidget(widget);
}

void SettingsCategory::updateThemeColors()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    if (m_titleLabel) {
        m_titleLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(colors.text.name()));
    }

    updateIconDisplay();
}

void SettingsCategory::updateIconDisplay()
{
    if (m_icon.isNull() || !m_iconLabel) {
        return;
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int size = theme.scaled(BASE_ICON_SIZE);

    const QPixmap pixmap = m_icon.pixmap(size, size);

    m_iconLabel->setPixmap(ruwa::ui::painting::tintedPixmap(pixmap, theme.colors().text));
}

void SettingsCategory::onThemeChanged()
{
    updateScaledSizes();
    updateThemeColors();
}

} // namespace ruwa::ui::widgets
