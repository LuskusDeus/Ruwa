// SPDX-License-Identifier: MPL-2.0

// ThemeContext.h
#ifndef RUWA_UI_CORE_THEME_THEMECONTEXT_H
#define RUWA_UI_CORE_THEME_THEMECONTEXT_H

#include <QWidget>

namespace ruwa::ui::core {

class ThemeManager;

/**
 * @brief Helper class to manage widget theming
 *
 * ThemeContext automatically applies the current theme to a widget
 * and updates it when the theme changes.
 */
class ThemeContext {
public:
    explicit ThemeContext(QWidget* widget);
    ~ThemeContext();

    /// Apply the current theme to the associated widget
    void applyTheme();

    /// Subscribe to theme change notifications
    void subscribeToThemeChanges();

    /// Unsubscribe from theme change notifications
    void unsubscribeFromThemeChanges();

private:
    QWidget* m_widget { nullptr }; ///< The widget being themed
    bool m_subscribed { false }; ///< Tracks whether subscription is active
};

} // namespace ruwa::ui::core

#endif // RUWA_UI_CORE_THEME_THEMECONTEXT_H
