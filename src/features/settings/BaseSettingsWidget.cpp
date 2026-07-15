// SPDX-License-Identifier: MPL-2.0

// BaseSettingsWidget.cpp
#include "features/settings/BaseSettingsWidget.h"
#include "shared/style/WidgetStyleManager.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLayout>
#include <QLabel>
#include <QSizePolicy>

namespace ruwa::ui::widgets {

namespace {
const int BASE_SPACING = 8;
const int BASE_TEXT_CONTROL_SPACING = 16;
} // namespace

BaseSettingsWidget::BaseSettingsWidget(
    const QString& label, const QString& description, QWidget* parent)
    : BaseStyledPanel("SettingsPanel", parent)
    , m_label(label)
    , m_description(description)
{
    // Disable hover for settings panels (they're containers, not interactive)
    setHoverEnabled(false);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    setupUI();
    updateScaledSizes();

    // Connect to theme changes for label updates
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &BaseSettingsWidget::onThemeChanged);
}

void BaseSettingsWidget::setupUI()
{
    auto& mgr = ruwa::ui::core::WidgetStyleManager::instance();
    const auto& colors = mgr.colors();

    // Main layout: horizontal — text on left, controls on right
    auto* row = new QHBoxLayout(this);
    m_mainLayout = row;

    // Apply padding from style
    QMargins padding = contentPadding();
    m_mainLayout->setContentsMargins(padding);
    row->setSpacing(mgr.scaled(BASE_TEXT_CONTROL_SPACING));

    // Left: text block (label + description), vertically centered
    m_textContainer = new QWidget(this);
    m_textContainer->setAttribute(Qt::WA_TranslucentBackground);
    m_textLayout = new QVBoxLayout(m_textContainer);
    m_textLayout->setContentsMargins(0, 0, 0, 0);
    m_textLayout->setSpacing(mgr.scaled(BASE_SPACING));

    // Label
    m_labelWidget = new QLabel(m_label, m_textContainer);
    m_labelWidget->setAttribute(Qt::WA_TranslucentBackground);
    QFont labelFont = m_labelWidget->font();
    labelFont.setBold(true);
    labelFont.setPointSize(mgr.scaledFontSize(style().content.baseFontSize));
    m_labelWidget->setFont(labelFont);
    m_labelWidget->setStyleSheet(
        QString("QLabel { color: %1; background: transparent; }").arg(colors.text.name()));
    m_textLayout->addWidget(m_labelWidget);

    // Description (optional)
    if (!m_description.isEmpty()) {
        m_descriptionWidget = new QLabel(m_description, m_textContainer);
        m_descriptionWidget->setAttribute(Qt::WA_TranslucentBackground);
        m_descriptionWidget->setWordWrap(true);
        QFont descFont = m_descriptionWidget->font();
        descFont.setPointSize(mgr.scaledFontSize(8));
        m_descriptionWidget->setFont(descFont);
        m_descriptionWidget->setStyleSheet(
            QString("QLabel { color: %1; background: transparent; }").arg(colors.textMuted.name()));
        m_textLayout->addWidget(m_descriptionWidget);
    }

    row->addWidget(m_textContainer, 1, Qt::AlignVCenter);

    // Right: control container — derived classes add widgets here via mainLayout()
    m_controlContainer = new QWidget(this);
    m_controlContainer->setAttribute(Qt::WA_TranslucentBackground);
    m_controlLayout = new QVBoxLayout(m_controlContainer);
    m_controlLayout->setContentsMargins(0, 0, 0, 0);
    m_controlLayout->setSpacing(0);
    m_controlLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    row->addWidget(m_controlContainer, 0, Qt::AlignVCenter);
}

void BaseSettingsWidget::updateScaledSizes()
{
    auto& mgr = ruwa::ui::core::WidgetStyleManager::instance();

    // Update layout
    QMargins padding = contentPadding();
    m_mainLayout->setContentsMargins(padding);
    if (auto* row = qobject_cast<QHBoxLayout*>(m_mainLayout)) {
        row->setSpacing(mgr.scaled(BASE_TEXT_CONTROL_SPACING));
    } else if (auto* grid = qobject_cast<QGridLayout*>(m_mainLayout)) {
        const int sp = mgr.scaled(BASE_TEXT_CONTROL_SPACING);
        grid->setHorizontalSpacing(sp);
        grid->setVerticalSpacing(0);
    }
    if (m_textLayout) {
        m_textLayout->setSpacing(mgr.scaled(BASE_SPACING));
    }

    // Update font sizes
    if (m_labelWidget) {
        QFont labelFont = m_labelWidget->font();
        labelFont.setPointSize(mgr.scaledFontSize(style().content.baseFontSize));
        m_labelWidget->setFont(labelFont);
    }

    if (m_descriptionWidget) {
        QFont descFont = m_descriptionWidget->font();
        descFont.setPointSize(mgr.scaledFontSize(8));
        m_descriptionWidget->setFont(descFont);
    }

    refreshLayoutGeometry();
}

void BaseSettingsWidget::updateThemeColors()
{
    const auto& colors = ruwa::ui::core::WidgetStyleManager::instance().colors();

    if (m_labelWidget) {
        m_labelWidget->setStyleSheet(
            QString("QLabel { color: %1; background: transparent; }").arg(colors.text.name()));
    }

    if (m_descriptionWidget) {
        m_descriptionWidget->setStyleSheet(
            QString("QLabel { color: %1; background: transparent; }").arg(colors.textMuted.name()));
    }

    update();
}

void BaseSettingsWidget::onThemeChanged()
{
    updateScaledSizes();
    updateThemeColors();
}

void BaseSettingsWidget::refreshLayoutGeometry()
{
    if (m_textContainer) {
        m_textContainer->updateGeometry();
    }
    if (m_controlContainer) {
        m_controlContainer->updateGeometry();
    }
    if (m_mainLayout) {
        m_mainLayout->activate();
    }

    // Let the parent layout resolve the final row height from the current width.
    // For settings rows with word-wrapped descriptions, forcing adjustSize() here
    // can lock in an oversized height from an intermediate geometry pass.
    updateGeometry();

    QWidget* ancestor = parentWidget();
    while (ancestor) {
        if (ancestor->layout()) {
            ancestor->layout()->invalidate();
            ancestor->layout()->activate();
        }
        ancestor->updateGeometry();
        ancestor = ancestor->parentWidget();
    }
}

void BaseSettingsWidget::setLabel(const QString& label)
{
    m_label = label;
    if (m_labelWidget)
        m_labelWidget->setText(label);
    refreshLayoutGeometry();
}

void BaseSettingsWidget::setDescription(const QString& description)
{
    m_description = description;
    if (!m_descriptionWidget && !description.isEmpty() && m_textLayout && m_textContainer
        && m_labelWidget) {
        auto& mgr = ruwa::ui::core::WidgetStyleManager::instance();
        const auto& colors = mgr.colors();
        m_descriptionWidget = new QLabel(description, m_textContainer);
        m_descriptionWidget->setAttribute(Qt::WA_TranslucentBackground);
        m_descriptionWidget->setWordWrap(true);
        QFont descFont = m_descriptionWidget->font();
        descFont.setPointSize(mgr.scaledFontSize(8));
        m_descriptionWidget->setFont(descFont);
        m_descriptionWidget->setStyleSheet(
            QStringLiteral("QLabel { color: %1; background: transparent; }")
                .arg(colors.textMuted.name()));
        m_textLayout->addWidget(m_descriptionWidget);
    }
    if (m_descriptionWidget) {
        m_descriptionWidget->setText(description);
        m_descriptionWidget->setVisible(!description.isEmpty());
    }
    refreshLayoutGeometry();
}

} // namespace ruwa::ui::widgets
