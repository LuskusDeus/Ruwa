// SPDX-License-Identifier: MPL-2.0

// DockLayoutPresetStore.cpp
#include "DockLayoutPresetStore.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QSettings>

namespace ruwa::ui::docking {

namespace {

bool isLegacyStarterLayoutId(const QUuid& id)
{
    return id == QUuid(QStringLiteral("{99e6b786-d961-4409-ac1b-65b86f0cbca2}"))
        || id == QUuid(QStringLiteral("{39356a67-6126-4a82-9c46-22f04a51e6a5}"));
}

} // namespace

DockLayoutPresetStore& DockLayoutPresetStore::instance()
{
    static DockLayoutPresetStore inst;
    return inst;
}

DockLayoutPresetStore::DockLayoutPresetStore(QObject* parent)
    : QObject(parent)
{
    load();
}

QVector<DockLayoutPreset> DockLayoutPresetStore::allPresets() const
{
    QVector<DockLayoutPreset> out = DockLayoutPreset::builtInPresets();
    out.append(m_custom);
    return out;
}

std::optional<DockLayoutPreset> DockLayoutPresetStore::presetById(const QUuid& id) const
{
    if (id.isNull()) {
        return std::nullopt;
    }
    for (const auto& p : m_custom) {
        if (p.id == id) {
            return p;
        }
    }
    for (const auto& p : DockLayoutPreset::builtInPresets()) {
        if (p.id == id) {
            return p;
        }
    }
    return std::nullopt;
}

QString DockLayoutPresetStore::suggestUniqueName(const QString& base) const
{
    const QString root = base.trimmed().isEmpty() ? QStringLiteral("Layout") : base.trimmed();
    QString name = root;
    int n = 2;
    auto taken = [&](const QString& q) {
        for (const auto& p : m_custom) {
            if (p.name == q) {
                return true;
            }
        }
        for (const auto& p : DockLayoutPreset::builtInPresets()) {
            if (p.name == q) {
                return true;
            }
        }
        return false;
    };
    while (taken(name)) {
        name = QStringLiteral("%1 %2").arg(root).arg(n++);
    }
    return name;
}

void DockLayoutPresetStore::addCustomPreset(const DockLayoutPreset& preset)
{
    if (preset.id.isNull() || preset.name.trimmed().isEmpty()) {
        return;
    }
    m_custom.append(preset);
    save();
    emit changed();
}

void DockLayoutPresetStore::updateCustomPreset(const DockLayoutPreset& preset)
{
    for (int i = 0; i < m_custom.size(); ++i) {
        if (m_custom[i].id == preset.id) {
            m_custom[i] = preset;
            m_custom[i].isBuiltIn = false;
            save();
            emit changed();
            return;
        }
    }
}

void DockLayoutPresetStore::removeCustomPreset(const QUuid& id)
{
    for (int i = 0; i < m_custom.size(); ++i) {
        if (m_custom[i].id == id) {
            m_custom.removeAt(i);
            save();
            emit changed();
            return;
        }
    }
}

void DockLayoutPresetStore::load()
{
    m_custom.clear();
    bool removedLegacyStarterLayout = false;

    QSettings settings;
    settings.beginGroup(QStringLiteral("LayoutPresets/Custom"));
    const int count = settings.value(QStringLiteral("count"), 0).toInt();
    for (int i = 0; i < count; ++i) {
        settings.beginGroup(QString::number(i));
        const QByteArray raw = settings.value(QStringLiteral("data")).toByteArray();
        settings.endGroup();
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }
        DockLayoutPreset p = DockLayoutPreset::fromJson(doc.object());
        p.isBuiltIn = false;
        if (isLegacyStarterLayoutId(p.id)) {
            removedLegacyStarterLayout = true;
            continue;
        }
        if (!p.id.isNull() && !p.name.trimmed().isEmpty()) {
            m_custom.append(p);
        }
    }
    settings.endGroup();

    if (removedLegacyStarterLayout) {
        save();
    }
}

void DockLayoutPresetStore::save()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("LayoutPresets/Custom"));
    settings.remove(QString());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("LayoutPresets/Custom"));
    settings.setValue(QStringLiteral("count"), m_custom.size());
    for (int i = 0; i < m_custom.size(); ++i) {
        settings.beginGroup(QString::number(i));
        const QJsonDocument doc(m_custom[i].toJson());
        settings.setValue(QStringLiteral("data"), doc.toJson(QJsonDocument::Compact));
        settings.endGroup();
    }
    settings.endGroup();
}

} // namespace ruwa::ui::docking
