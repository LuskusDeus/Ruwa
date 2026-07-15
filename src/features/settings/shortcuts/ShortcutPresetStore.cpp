// SPDX-License-Identifier: MPL-2.0

// ShortcutPresetStore.cpp
#include "features/settings/shortcuts/ShortcutPresetStore.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QtConcurrent>

namespace ruwa::features::settings::shortcuts {

namespace {
constexpr const char* SETTINGS_GROUP = "ShortcutPresets/Custom";
constexpr const char* SETTINGS_KEY_LEGACY = "shortcuts/customPresets";
const QUuid kDefaultPresetId { QStringLiteral("{22222222-2222-4000-8000-000000000001}") };
} // namespace

QJsonObject ShortcutPreset::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id.toString());
    obj.insert(QStringLiteral("name"), name);
    obj.insert(QStringLiteral("isBuiltIn"), isBuiltIn);

    QJsonObject bindingsObj;
    for (auto it = bindings.begin(); it != bindings.end(); ++it) {
        bindingsObj.insert(it.key(), it.value().toString(QKeySequence::PortableText));
    }
    obj.insert(QStringLiteral("bindings"), bindingsObj);
    return obj;
}

ShortcutPreset ShortcutPreset::fromJson(const QJsonObject& obj)
{
    ShortcutPreset p;
    p.id = QUuid(obj.value(QStringLiteral("id")).toString());
    p.name = obj.value(QStringLiteral("name")).toString();
    p.isBuiltIn = obj.value(QStringLiteral("isBuiltIn")).toBool(false);
    const QJsonObject bindingsObj = obj.value(QStringLiteral("bindings")).toObject();
    for (auto it = bindingsObj.begin(); it != bindingsObj.end(); ++it) {
        p.bindings.insert(
            it.key(), QKeySequence(it.value().toString(), QKeySequence::PortableText));
    }
    return p;
}

ShortcutPresetStore& ShortcutPresetStore::instance()
{
    static ShortcutPresetStore s;
    return s;
}

ShortcutPresetStore::ShortcutPresetStore(QObject* parent)
    : QObject(parent)
{
    load();
}

ShortcutPreset ShortcutPresetStore::defaultPreset() const
{
    ShortcutPreset p;
    p.id = kDefaultPresetId;
    p.name = QCoreApplication::translate("ShortcutPreset", "Default");
    p.isBuiltIn = true;
    return p;
}

QVector<ShortcutPreset> ShortcutPresetStore::allPresets() const
{
    QVector<ShortcutPreset> all;
    all.append(defaultPreset());
    all.append(m_custom);
    return all;
}

std::optional<ShortcutPreset> ShortcutPresetStore::presetById(const QUuid& id) const
{
    if (id == kDefaultPresetId) {
        return defaultPreset();
    }
    for (const auto& p : m_custom) {
        if (p.id == id)
            return p;
    }
    return std::nullopt;
}

QString ShortcutPresetStore::suggestUniqueName(const QString& base) const
{
    QString candidate = base;
    int counter = 2;
    auto isTaken = [this](const QString& name) {
        if (name == defaultPreset().name)
            return true;
        for (const auto& p : m_custom) {
            if (p.name.compare(name, Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
    };
    while (isTaken(candidate)) {
        candidate = QStringLiteral("%1 %2").arg(base).arg(counter++);
    }
    return candidate;
}

void ShortcutPresetStore::addCustomPreset(const ShortcutPreset& preset)
{
    ShortcutPreset p = preset;
    if (p.id.isNull()) {
        p.id = QUuid::createUuid();
    }
    p.isBuiltIn = false;
    m_custom.append(p);
    save();
    emit changed();
}

void ShortcutPresetStore::updateCustomPreset(const ShortcutPreset& preset)
{
    for (int i = 0; i < m_custom.size(); ++i) {
        if (m_custom[i].id == preset.id) {
            ShortcutPreset p = preset;
            p.isBuiltIn = false;
            m_custom[i] = p;
            save();
            emit changed();
            return;
        }
    }
}

void ShortcutPresetStore::removeCustomPreset(const QUuid& id)
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

void ShortcutPresetStore::load()
{
    m_custom.clear();

    QSettings settings;
    settings.beginGroup(QString::fromLatin1(SETTINGS_GROUP));
    const int count = settings.value(QStringLiteral("count"), 0).toInt();
    for (int i = 0; i < count; ++i) {
        settings.beginGroup(QString::number(i));
        const QByteArray raw = settings.value(QStringLiteral("data")).toByteArray();
        settings.endGroup();
        QJsonParseError err {};
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            continue;
        ShortcutPreset p = ShortcutPreset::fromJson(doc.object());
        if (p.id.isNull() || p.name.trimmed().isEmpty())
            continue;
        p.isBuiltIn = false;
        m_custom.append(p);
    }
    settings.endGroup();

    // Migrate legacy single-blob format (if it ever wrote anything readable).
    if (m_custom.isEmpty()) {
        const QByteArray legacy
            = settings.value(QString::fromLatin1(SETTINGS_KEY_LEGACY)).toByteArray();
        if (!legacy.isEmpty()) {
            QJsonParseError err {};
            const QJsonDocument doc = QJsonDocument::fromJson(legacy, &err);
            if (err.error == QJsonParseError::NoError && doc.isArray()) {
                for (const auto& v : doc.array()) {
                    if (!v.isObject())
                        continue;
                    ShortcutPreset p = ShortcutPreset::fromJson(v.toObject());
                    if (p.id.isNull() || p.name.trimmed().isEmpty())
                        continue;
                    p.isBuiltIn = false;
                    m_custom.append(p);
                }
                settings.remove(QString::fromLatin1(SETTINGS_KEY_LEGACY));
                if (!m_custom.isEmpty()) {
                    save();
                }
            }
        }
    }
}

void ShortcutPresetStore::save() const
{
    // Serialize on the UI thread (cheap), then write/sync on a worker thread.
    QVector<QByteArray> blobs;
    blobs.reserve(m_custom.size());
    for (const auto& p : m_custom) {
        blobs.append(QJsonDocument(p.toJson()).toJson(QJsonDocument::Compact));
    }

    QtConcurrent::run([blobs]() {
        QSettings settings;
        settings.beginGroup(QString::fromLatin1(SETTINGS_GROUP));
        settings.remove(QString());
        settings.setValue(QStringLiteral("count"), blobs.size());
        for (int i = 0; i < blobs.size(); ++i) {
            settings.beginGroup(QString::number(i));
            settings.setValue(QStringLiteral("data"), blobs[i]);
            settings.endGroup();
        }
        settings.endGroup();
        settings.sync();
    });
}

} // namespace ruwa::features::settings::shortcuts
