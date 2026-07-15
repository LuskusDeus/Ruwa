// SPDX-License-Identifier: MPL-2.0

// SettingsToggle.cpp
#include "features/settings/SettingsToggle.h"
#include "shared/widgets/inputs/ToggleSwitch.h"

#include <QVBoxLayout>

namespace ruwa::ui::widgets {

SettingsToggle::SettingsToggle(
    const QString& label, const QString& description, bool defaultValue, QWidget* parent)
    : BaseSettingsWidget(label, description, parent)
{
    setupContent();
    m_toggleSwitch->setChecked(defaultValue, ToggleSwitch::TransitionMode::Instant);
}

void SettingsToggle::setupContent()
{
    m_toggleSwitch = new ToggleSwitch(ToggleSwitch::InitOptions { false, true }, this);

    // Forward the signal
    connect(m_toggleSwitch, &ToggleSwitch::toggled, this, &SettingsToggle::toggled);

    mainLayout()->addWidget(m_toggleSwitch);
}

void SettingsToggle::setChecked(bool checked)
{
    m_toggleSwitch->setChecked(checked);
}

bool SettingsToggle::isChecked() const
{
    return m_toggleSwitch->isChecked();
}

} // namespace ruwa::ui::widgets
