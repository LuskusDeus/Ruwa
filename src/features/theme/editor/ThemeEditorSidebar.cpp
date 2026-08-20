// SPDX-License-Identifier: MPL-2.0

#include "ThemeEditorSidebar.h"
#include "ThemeEditorThemeDropdown.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/i18n/TranslationManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/widgets/SidebarButton.h"

#include <QEvent>
#include <QPainter>
#include <QPalette>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace ruwa::ui::widgets {

ThemeEditorSidebar::ThemeEditorSidebar(QWidget* parent)
    : QWidget(parent)
{
    setupUi();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &ThemeEditorSidebar::onThemeChanged);
    connect(&ruwa::ui::core::TranslationManager::instance(),
        &ruwa::ui::core::TranslationManager::languageChanged, this,
        &ThemeEditorSidebar::retranslateUi);

    onThemeChanged();
}

int ThemeEditorSidebar::sectionToIndex(Section section)
{
    switch (section) {
    case Section::Themes:
        return 0;
    case Section::Animations:
        return 1;
    default:
        return 0;
    }
}

void ThemeEditorSidebar::setActiveSection(Section section)
{
    if (m_activeSection == section || section == Section::None) {
        return;
    }

    if (m_buttons.contains(m_activeSection)) {
        m_buttons[m_activeSection]->setActive(false);
    }

    m_activeSection = section;
    if (m_buttons.contains(section)) {
        m_buttons[section]->setActive(true);
    }

    emit sectionChanged(section);
}

bool ThemeEditorSidebar::setEditingThemeById(const QUuid& id)
{
    return m_themeDropdown && m_themeDropdown->setEditingThemeById(id);
}

const ruwa::ui::core::ThemePreset& ThemeEditorSidebar::editingTheme() const
{
    return m_themeDropdown->editingTheme();
}

ruwa::ui::core::ThemePreset ThemeEditorSidebar::saveEditingTheme(
    const ruwa::ui::core::ThemePreset& preset)
{
    return m_themeDropdown->saveEditingTheme(preset);
}

void ThemeEditorSidebar::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    // Retranslation is driven by TranslationManager after the translator is installed.
}

void ThemeEditorSidebar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), ruwa::ui::core::ThemeManager::instance().colors().background);
}

void ThemeEditorSidebar::setupUi()
{
    m_layout = new QVBoxLayout(this);
    m_layout->setSpacing(0);
    updateScaledSizes();

    m_themeDropdown = new ThemeEditorThemeDropdown(this);
    connect(m_themeDropdown, &ThemeEditorThemeDropdown::editingThemeChanged, this,
        &ThemeEditorSidebar::editingThemeChanged);
    m_layout->addWidget(m_themeDropdown);

    m_dropdownDivider = new QWidget(this);
    m_dropdownDivider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_layout->addWidget(m_dropdownDivider);

    auto& icons = ruwa::ui::core::ThemeManager::instance().icons();
    auto* themesButton = new SidebarButton(
        tr("Themes"), icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::Appearance), this);
    auto* animationsButton = new SidebarButton(tr("Animations"),
        icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::Performance), this);

    m_buttons.insert(Section::Themes, themesButton);
    m_buttons.insert(Section::Animations, animationsButton);

    connect(
        themesButton, &QPushButton::clicked, this, [this]() { setActiveSection(Section::Themes); });
    connect(animationsButton, &QPushButton::clicked, this,
        [this]() { setActiveSection(Section::Animations); });

    m_layout->addWidget(themesButton);
    m_layout->addWidget(animationsButton);
    m_layout->addStretch();
}

void ThemeEditorSidebar::retranslateUi()
{
    m_buttons.value(Section::Themes)->setText(tr("Themes"));
    m_buttons.value(Section::Animations)->setText(tr("Animations"));
}

void ThemeEditorSidebar::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    setFixedWidth(theme.scaled(220));
    m_layout->setSpacing(theme.scaled(8));
    m_layout->setContentsMargins(theme.scaled(8), 0, theme.scaled(8), theme.scaled(20));

    if (m_dropdownDivider) {
        m_dropdownDivider->setFixedHeight(qMax(1, theme.scaled(1)));
    }
}

void ThemeEditorSidebar::onThemeChanged()
{
    updateScaledSizes();
    if (m_dropdownDivider) {
        m_dropdownDivider->setAutoFillBackground(true);
        QPalette palette = m_dropdownDivider->palette();
        palette.setColor(
            QPalette::Window, ruwa::ui::core::ThemeManager::instance().colors().border);
        m_dropdownDivider->setPalette(palette);
    }
    update();
}

} // namespace ruwa::ui::widgets
