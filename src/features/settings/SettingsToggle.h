// SPDX-License-Identifier: MPL-2.0

// SettingsToggle.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_SETTINGSTOGGLE_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_SETTINGSTOGGLE_H

#include "features/settings/BaseSettingsWidget.h"

namespace ruwa::ui::widgets {

class ToggleSwitch;

/**
 * @brief Toggle switch widget for boolean settings
 *
 * Combines BaseSettingsWidget (label + description) with ToggleSwitch.
 * The switch is positioned at the bottom-left of the widget.
 */
class SettingsToggle : public BaseSettingsWidget {
    Q_OBJECT

public:
    explicit SettingsToggle(const QString& label, const QString& description = QString(),
        bool defaultValue = false, QWidget* parent = nullptr);
    ~SettingsToggle() override = default;

    /// Get/Set toggle state
    void setChecked(bool checked);
    bool isChecked() const;

signals:
    void toggled(bool checked);

protected:
    void setupContent() override;

private:
    ToggleSwitch* m_toggleSwitch = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_SETTINGSTOGGLE_H
