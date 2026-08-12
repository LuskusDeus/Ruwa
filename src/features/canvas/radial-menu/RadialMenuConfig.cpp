// SPDX-License-Identifier: MPL-2.0

#include "RadialMenuConfig.h"

#include <QJsonDocument>
#include <QSettings>
#include <QtConcurrent>

namespace ruwa::ui::widgets {

namespace {

constexpr auto kSettingsGroup = "RadialMenu";
constexpr auto kSettingsKey = "Layout";

} // namespace

RadialMenuConfig& RadialMenuConfig::instance()
{
    static RadialMenuConfig instance;
    return instance;
}

RadialMenuConfig::RadialMenuConfig() = default;

const RadialMenuLayout& RadialMenuConfig::layout()
{
    ensureLoaded();
    return m_layout;
}

void RadialMenuConfig::setLayout(const RadialMenuLayout& layout)
{
    ensureLoaded();
    // An unusable layout would leave the user with no way back to the menu that
    // edits it, so fall back to the defaults instead of storing it.
    m_layout = layout.isValid() ? layout : RadialMenuLayout::defaults();
    save();
    emit layoutChanged();
}

void RadialMenuConfig::setPage(const RadialMenuPage& page)
{
    ensureLoaded();
    RadialMenuLayout updated = m_layout;
    updated.setPage(page);
    setLayout(updated);
}

void RadialMenuConfig::resetToDefaults()
{
    setLayout(RadialMenuLayout::defaults());
}

void RadialMenuConfig::ensureLoaded()
{
    if (m_loaded) {
        return;
    }
    m_loaded = true;

    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroup));
    const QString stored = settings.value(QLatin1String(kSettingsKey)).toString();
    settings.endGroup();

    if (!stored.isEmpty()) {
        const QJsonDocument document = QJsonDocument::fromJson(stored.toUtf8());
        if (document.isObject()) {
            m_layout = RadialMenuLayout::fromJson(document.object());
        }
    }

    if (!m_layout.isValid()) {
        m_layout = RadialMenuLayout::defaults();
    }
}

void RadialMenuConfig::save() const
{
    const QString serialized
        = QString::fromUtf8(QJsonDocument(m_layout.toJson()).toJson(QJsonDocument::Compact));

    QtConcurrent::run([serialized]() {
        QSettings settings;
        settings.beginGroup(QLatin1String(kSettingsGroup));
        settings.setValue(QLatin1String(kSettingsKey), serialized);
        settings.endGroup();
        settings.sync();
    });
}

} // namespace ruwa::ui::widgets
