// SPDX-License-Identifier: MPL-2.0

// SettingsChoice.cpp
#include "features/settings/SettingsChoice.h"
#include "shared/widgets/SegmentedOptionSelector.h"
#include "shared/style/WidgetStyleManager.h"

#include <QHBoxLayout>

namespace ruwa::ui::widgets {

namespace {
const int BASE_OPTIONS_SPACING = 8;
}

SettingsChoice::SettingsChoice(const QString& label, const QString& description,
    const QStringList& options, int defaultIndex, QWidget* parent)
    : BaseSettingsWidget(label, description, parent)
    , m_options(options)
    , m_selectedIndex(defaultIndex)
{
    setupContent();
}

void SettingsChoice::setupContent()
{
    auto& mgr = ruwa::ui::core::WidgetStyleManager::instance();

    QVector<SegmentedOptionSelector::Option> selectorOptions;
    selectorOptions.reserve(m_options.size());
    for (int i = 0; i < m_options.size(); ++i) {
        selectorOptions.append({ m_options[i], QIcon(), i });
    }

    m_selector = new SegmentedOptionSelector(selectorOptions, this);
    m_selector->setDisplayMode(SegmentedOptionSelector::DisplayMode::TextOnly);
    m_selector->setCurrentIndex(m_selectedIndex, false);

    connect(m_selector, &SegmentedOptionSelector::selectionChanged, this,
        [this](int index) { setSelectedIndex(index); });

    QHBoxLayout* rowLayout = new QHBoxLayout();
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(mgr.scaled(BASE_OPTIONS_SPACING));
    rowLayout->addStretch();
    rowLayout->addWidget(m_selector);
    mainLayout()->addLayout(rowLayout);
}

void SettingsChoice::setSelectedIndex(int index)
{
    if (index < 0 || index >= m_options.size() || index == m_selectedIndex) {
        return;
    }

    m_selectedIndex = index;

    if (m_selector && m_selector->currentIndex() != m_selectedIndex) {
        m_selector->setCurrentIndex(m_selectedIndex, true);
    }

    emit selectionChanged(index);
}

QString SettingsChoice::selectedOption() const
{
    if (m_selectedIndex >= 0 && m_selectedIndex < m_options.size()) {
        return m_options[m_selectedIndex];
    }
    return QString();
}

void SettingsChoice::setOptions(const QStringList& options)
{
    m_options = options;

    if (m_selectedIndex >= m_options.size()) {
        m_selectedIndex = m_options.isEmpty() ? -1 : (m_options.size() - 1);
    }

    QVector<SegmentedOptionSelector::Option> selectorOptions;
    selectorOptions.reserve(m_options.size());
    for (int i = 0; i < m_options.size(); ++i) {
        selectorOptions.append({ m_options[i], QIcon(), i });
    }

    if (m_selector) {
        m_selector->setOptions(selectorOptions);
    }
}

void SettingsChoice::retranslateUi(
    const QString& label, const QString& description, const QStringList& options)
{
    setLabel(label);
    setDescription(description);
    setOptions(options);
    update();
}

} // namespace ruwa::ui::widgets
