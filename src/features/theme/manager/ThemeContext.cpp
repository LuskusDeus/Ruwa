// SPDX-License-Identifier: MPL-2.0

// ThemeContext.cpp
#include "ThemeContext.h"
#include "ThemeManager.h"

namespace ruwa::ui::core {

ThemeContext::ThemeContext(QWidget* widget)
    : m_widget(widget)
{
    subscribeToThemeChanges();
    applyTheme();
}

ThemeContext::~ThemeContext()
{
    unsubscribeFromThemeChanges();
}

void ThemeContext::applyTheme()
{
    if (!m_widget)
        return;

    // Stylesheet is already applied by ThemeManager::applyStyleSheet() to all widgets.
    // Only trigger repaint - avoid redundant unpolish/polish which causes lag.
    m_widget->update();
}

void ThemeContext::subscribeToThemeChanges()
{
    if (m_subscribed || !m_widget)
        return;

    QObject::connect(&ThemeManager::instance(), &ThemeManager::themeChanged, m_widget,
        [this]() { applyTheme(); });

    m_subscribed = true;
}

void ThemeContext::unsubscribeFromThemeChanges()
{
    if (!m_subscribed || !m_widget)
        return;

    QObject::disconnect(&ThemeManager::instance(), &ThemeManager::themeChanged, m_widget, nullptr);

    m_subscribed = false;
}

} // namespace ruwa::ui::core
