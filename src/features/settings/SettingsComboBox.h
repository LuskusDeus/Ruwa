// SPDX-License-Identifier: MPL-2.0

// SettingsComboBox.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_SETTINGSCOMBOBOX_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_SETTINGSCOMBOBOX_H

#include "features/settings/BaseSettingsWidget.h"
#include <QStringList>

namespace ruwa::ui::widgets {

class AnimatedComboBox;

/**
 * @brief Settings widget using AnimatedComboBox for selecting one option from multiple
 *
 * Combines BaseSettingsWidget with AnimatedComboBox.
 * Provides same API as SettingsChoice for consistency.
 */
class SettingsComboBox : public BaseSettingsWidget {
    Q_OBJECT

public:
    explicit SettingsComboBox(const QString& label, const QString& description,
        const QStringList& options, int defaultIndex = 0, QWidget* parent = nullptr);
    ~SettingsComboBox() override = default;

    /// Get/Set selected option
    void setSelectedIndex(int index);
    int selectedIndex() const;

    void setOptions(const QStringList& options);

signals:
    void selectionChanged(int index);

protected:
    void setupContent() override;

private:
    QStringList m_options;
    AnimatedComboBox* m_combo = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_SETTINGSCOMBOBOX_H
