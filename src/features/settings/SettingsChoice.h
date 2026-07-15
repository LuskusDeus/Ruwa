// SPDX-License-Identifier: MPL-2.0

// SettingsChoice.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_SETTINGSCHOICE_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_SETTINGSCHOICE_H

#include "features/settings/BaseSettingsWidget.h"
#include <QStringList>

namespace ruwa::ui::widgets {

class SegmentedOptionSelector;

/**
 * @brief Choice widget for selecting one option from multiple
 *
 * Combines BaseSettingsWidget with ChoiceButtons.
 * Only one option can be selected at a time.
 */
class SettingsChoice : public BaseSettingsWidget {
    Q_OBJECT

public:
    explicit SettingsChoice(const QString& label, const QString& description,
        const QStringList& options, int defaultIndex = 0, QWidget* parent = nullptr);
    ~SettingsChoice() override = default;

    /// Get/Set selected option
    void setSelectedIndex(int index);
    int selectedIndex() const { return m_selectedIndex; }
    QString selectedOption() const;

    void setOptions(const QStringList& options);
    void retranslateUi(
        const QString& label, const QString& description, const QStringList& options);

signals:
    void selectionChanged(int index);

protected:
    void setupContent() override;

private:
    QStringList m_options;
    int m_selectedIndex = 0;

    SegmentedOptionSelector* m_selector { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_SETTINGSCHOICE_H
