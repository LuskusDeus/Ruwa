// SPDX-License-Identifier: MPL-2.0

// SettingsComboBox.cpp
#include "features/settings/SettingsComboBox.h"
#include "shared/widgets/inputs/AnimatedComboBox.h"
#include "shared/style/WidgetStyleManager.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

namespace ruwa::ui::widgets {

namespace {
const int BASE_COMBO_MIN_WIDTH = 140;
}

SettingsComboBox::SettingsComboBox(const QString& label, const QString& description,
    const QStringList& options, int defaultIndex, QWidget* parent)
    : BaseSettingsWidget(label, description, parent)
    , m_options(options)
{
    setupContent();

    if (defaultIndex >= 0 && defaultIndex < m_options.size()) {
        m_combo->setCurrentIndex(defaultIndex);
    }
}

void SettingsComboBox::setupContent()
{
    auto& mgr = ruwa::ui::core::WidgetStyleManager::instance();

    m_combo = new AnimatedComboBox(this);
    m_combo->setPlaceholderText(tr("Select"));
    m_combo->setMinimumWidth(mgr.scaled(BASE_COMBO_MIN_WIDTH));
    m_combo->setPopupMinWidth(mgr.scaled(BASE_COMBO_MIN_WIDTH));

    for (int i = 0; i < m_options.size(); ++i) {
        m_combo->addItem(m_options[i], i);
    }

    connect(m_combo, &AnimatedComboBox::currentIndexChanged, this,
        [this](int index) { emit selectionChanged(index); });

    QHBoxLayout* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addStretch();
    row->addWidget(m_combo);
    mainLayout()->addLayout(row);
}

void SettingsComboBox::setSelectedIndex(int index)
{
    if (m_combo && index >= 0 && index < m_options.size()) {
        m_combo->setCurrentIndex(index);
    }
}

int SettingsComboBox::selectedIndex() const
{
    return m_combo ? m_combo->currentIndex() : -1;
}

void SettingsComboBox::setOptions(const QStringList& options)
{
    m_options = options;
    if (!m_combo)
        return;

    const int oldIndex = m_combo->currentIndex();
    m_combo->clear();
    for (int i = 0; i < m_options.size(); ++i) {
        m_combo->addItem(m_options[i], i);
    }
    if (oldIndex >= 0 && oldIndex < m_options.size()) {
        m_combo->setCurrentIndex(oldIndex);
    }
}

} // namespace ruwa::ui::widgets
