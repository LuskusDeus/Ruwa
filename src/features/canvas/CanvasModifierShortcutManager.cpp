// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/CanvasModifierShortcutManager.h"
#include "commands/ShortcutManager.h"

#include <QKeySequence>
#include <QSettings>
#include <QSet>
#include <Qt>
#include <QtConcurrent>

namespace ruwa::features::canvas {

namespace {
constexpr auto kSettingsGroup = "CanvasModifierShortcuts";
}

CanvasModifierShortcutManager& CanvasModifierShortcutManager::instance()
{
    static CanvasModifierShortcutManager manager;
    return manager;
}

const QVector<CanvasModifierShortcutDefinition>& CanvasModifierShortcutManager::definitions()
{
    static const QVector<CanvasModifierShortcutDefinition> values {
        { CanvasModifierAction::MoveContent, QStringLiteral("canvas.move-content"),
            Qt::Key_Control },
        { CanvasModifierAction::Eyedropper, QStringLiteral("canvas.eyedropper"), Qt::Key_Alt },
        { CanvasModifierAction::PanCanvas, QStringLiteral("canvas.pan"), Qt::Key_Space },
    };
    return values;
}

QVector<int> CanvasModifierShortcutManager::supportedKeys()
{
    return { Qt::Key_Alt, Qt::Key_Control, Qt::Key_Shift, Qt::Key_Space };
}

QString CanvasModifierShortcutManager::keyDisplayName(int key)
{
    switch (key) {
    case Qt::Key_Alt:
        return QStringLiteral("Alt");
    case Qt::Key_Control:
        return QStringLiteral("Ctrl");
    case Qt::Key_Shift:
        return QStringLiteral("Shift");
    case Qt::Key_Space:
        return QStringLiteral("Space");
    default:
        return {};
    }
}

CanvasModifierShortcutManager::CanvasModifierShortcutManager(QObject* parent)
    : QObject(parent)
{
    loadFromSettings();
    syncOrdinaryShortcutConflicts();
}

const CanvasModifierShortcutDefinition* CanvasModifierShortcutManager::definitionForId(
    const QString& id) const
{
    for (const auto& definition : definitions()) {
        if (definition.id == id) {
            return &definition;
        }
    }
    return nullptr;
}

const CanvasModifierShortcutDefinition* CanvasModifierShortcutManager::definitionForAction(
    CanvasModifierAction action) const
{
    for (const auto& definition : definitions()) {
        if (definition.action == action) {
            return &definition;
        }
    }
    return nullptr;
}

int CanvasModifierShortcutManager::keyFor(CanvasModifierAction action) const
{
    const auto* definition = definitionForAction(action);
    return definition ? keyForId(definition->id) : 0;
}

int CanvasModifierShortcutManager::keyForId(const QString& id) const
{
    const auto* definition = definitionForId(id);
    if (!definition) {
        return 0;
    }
    return m_customShortcuts.value(id, definition->defaultKey);
}

int CanvasModifierShortcutManager::defaultKeyForId(const QString& id) const
{
    const auto* definition = definitionForId(id);
    return definition ? definition->defaultKey : 0;
}

std::optional<CanvasModifierAction> CanvasModifierShortcutManager::actionForKey(int key) const
{
    std::optional<CanvasModifierAction> result;
    for (const auto& definition : definitions()) {
        if (keyFor(definition.action) == key) {
            if (result) {
                return std::nullopt;
            }
            result = definition.action;
        }
    }
    if (result && ruwa::core::ShortcutManager::instance().isShortcutInUse(QKeySequence(key))) {
        return std::nullopt;
    }
    return result;
}

bool CanvasModifierShortcutManager::hasCustomShortcut(const QString& id) const
{
    const auto* definition = definitionForId(id);
    return definition && keyForId(id) != definition->defaultKey;
}

bool CanvasModifierShortcutManager::isShortcutConflicted(const QString& id) const
{
    const auto* definition = definitionForId(id);
    if (!definition) {
        return false;
    }

    const int key = keyFor(definition->action);
    int canvasBindingCount = 0;
    for (const auto& candidate : definitions()) {
        if (keyFor(candidate.action) == key) {
            ++canvasBindingCount;
        }
    }
    return canvasBindingCount > 1
        || ruwa::core::ShortcutManager::instance().isShortcutInUse(QKeySequence(key));
}

QHash<QString, int> CanvasModifierShortcutManager::customBindings() const
{
    QHash<QString, int> bindings;
    for (const auto& definition : definitions()) {
        if (hasCustomShortcut(definition.id)) {
            bindings.insert(definition.id, keyFor(definition.action));
        }
    }
    return bindings;
}

void CanvasModifierShortcutManager::setCustomOrDefault(
    const CanvasModifierShortcutDefinition& definition, int key)
{
    if (key == definition.defaultKey) {
        m_customShortcuts.remove(definition.id);
    } else {
        m_customShortcuts.insert(definition.id, key);
    }
}

void CanvasModifierShortcutManager::setShortcut(const QString& id, int key)
{
    const auto* definition = definitionForId(id);
    if (!definition || !supportedKeys().contains(key)) {
        return;
    }

    if (keyFor(definition->action) == key) {
        return;
    }

    setCustomOrDefault(*definition, key);
    syncOrdinaryShortcutConflicts();
    emit shortcutChanged(id, key);
}

void CanvasModifierShortcutManager::resetShortcut(const QString& id)
{
    const auto* definition = definitionForId(id);
    if (definition) {
        setShortcut(id, definition->defaultKey);
    }
}

void CanvasModifierShortcutManager::resetAllShortcuts()
{
    bool changed = false;
    for (const auto& definition : definitions()) {
        changed = changed || hasCustomShortcut(definition.id);
    }
    if (!changed) {
        return;
    }

    m_customShortcuts.clear();
    syncOrdinaryShortcutConflicts();
    for (const auto& definition : definitions()) {
        emit shortcutChanged(definition.id, definition.defaultKey);
    }
}

void CanvasModifierShortcutManager::loadFromSettings()
{
    m_customShortcuts.clear();
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    for (const auto& definition : definitions()) {
        if (!settings.contains(definition.id)) {
            continue;
        }
        const int key = settings.value(definition.id).toInt();
        if (supportedKeys().contains(key)) {
            m_customShortcuts.insert(definition.id, key);
        }
    }
    settings.endGroup();

}

void CanvasModifierShortcutManager::syncOrdinaryShortcutConflicts() const
{
    QSet<QKeySequence> canvasSequences;
    for (const auto& definition : definitions()) {
        canvasSequences.insert(QKeySequence(keyFor(definition.action)));
    }
    ruwa::core::ShortcutManager::instance().setExternallyConflictedShortcuts(canvasSequences);
}

void CanvasModifierShortcutManager::saveToSettings() const
{
    const QHash<QString, int> snapshot = customBindings();
    QtConcurrent::run([snapshot]() {
        QSettings settings;
        settings.beginGroup(QString::fromLatin1(kSettingsGroup));
        settings.remove(QString());
        for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it) {
            settings.setValue(it.key(), it.value());
        }
        settings.endGroup();
        settings.sync();
    });
}

} // namespace ruwa::features::canvas
