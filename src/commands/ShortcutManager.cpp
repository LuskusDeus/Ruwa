// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   C O M M A N D   S Y S T E M
// ======================================================================================

#include "ShortcutManager.h"
#include "commands/CommandRegistry.h"
#include "commands/CommandExecutor.h"
#include "commands/Command.h"

#include <QKeyEvent>
#include <QSettings>
#include <QShortcut>
#include <QtConcurrent>

namespace {
constexpr int MAX_LAST_USED = 5;
}

namespace ruwa::core {

ShortcutManager& ShortcutManager::instance()
{
    static ShortcutManager instance;
    return instance;
}

ShortcutManager::ShortcutManager()
    : QObject(nullptr)
{
}

ShortcutManager::~ShortcutManager() = default;

void ShortcutManager::setShortcutContext(QWidget* contextWidget)
{
    m_contextWidget = contextWidget;
}

void ShortcutManager::registerAllShortcuts()
{
    if (!m_contextWidget) {
        return;
    }

    // Clear existing shortcuts
    qDeleteAll(m_shortcuts);
    m_shortcuts.clear();
    m_shortcutToCommand.clear();

    // Register shortcuts for all commands
    for (Command* cmd : CommandRegistry::instance().allCommands()) {
        const QKeySequence seq = shortcutFor(cmd->id());
        if (!seq.isEmpty()) {
            createShortcut(cmd->id(), seq);
        }
    }
}

void ShortcutManager::createShortcut(const QString& commandId, const QKeySequence& sequence)
{
    if (sequence.isEmpty() || !m_contextWidget) {
        return;
    }

    QShortcut* shortcut = new QShortcut(sequence, m_contextWidget);
    shortcut->setContext(Qt::ApplicationShortcut);

    connect(shortcut, &QShortcut::activated, this, [this, commandId]() {
        const bool isUndoRedo
            = (commandId == QLatin1String("edit.undo") || commandId == QLatin1String("edit.redo"));
        QElapsedTimer dbgShortcutTimer;
        if (isUndoRedo) {
            dbgShortcutTimer.start();
        }
        recordShortcutUsed(commandId);
        if (isUndoRedo) { }
        CommandExecutor::instance().execute(commandId);
        if (isUndoRedo) { }
    });

    m_shortcuts.insert(commandId, shortcut);
    m_shortcutToCommand.insert(sequence, commandId);
}

void ShortcutManager::setShortcut(const QString& commandId, const QKeySequence& shortcut)
{
    // Check for conflicts
    if (!shortcut.isEmpty() && isShortcutInUse(shortcut, commandId)) {
        QString conflicting = commandForShortcut(shortcut);
        return;
    }

    m_customShortcuts.insert(commandId, shortcut);
    updateShortcut(commandId);

    emit shortcutChanged(commandId, shortcut);
}

void ShortcutManager::resetShortcut(const QString& commandId)
{
    m_customShortcuts.remove(commandId);
    updateShortcut(commandId);

    emit shortcutChanged(commandId, shortcutFor(commandId));
}

void ShortcutManager::clearShortcut(const QString& commandId)
{
    setShortcut(commandId, QKeySequence());
}

QKeySequence ShortcutManager::shortcutFor(const QString& commandId) const
{
    // Custom shortcut takes precedence
    if (m_customShortcuts.contains(commandId)) {
        return m_customShortcuts.value(commandId);
    }

    // Fall back to default
    return defaultShortcutFor(commandId);
}

QKeySequence ShortcutManager::defaultShortcutFor(const QString& commandId) const
{
    Command* cmd = CommandRegistry::instance().command(commandId);
    if (cmd) {
        return cmd->info().defaultShortcut;
    }
    return QKeySequence();
}

bool ShortcutManager::hasCustomShortcut(const QString& commandId) const
{
    return m_customShortcuts.contains(commandId);
}

QString ShortcutManager::commandForShortcut(const QKeySequence& shortcut) const
{
    return m_shortcutToCommand.value(shortcut);
}

QString ShortcutManager::commandForKeyEvent(const QKeyEvent* event) const
{
    if (!event)
        return {};

    // 1. Try the Qt key as-is (works when layout matches shortcut definition)
    const int key = event->key();
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    const int combined = key | static_cast<int>(mods);
    QString cmdId = m_shortcutToCommand.value(QKeySequence(combined));
    if (!cmdId.isEmpty())
        return cmdId;

    // 2. Fall back to physical key via native virtual key code (layout-independent)
    const quint32 nativeVK = event->nativeVirtualKey();
    if (nativeVK != 0) {
        const int physicalKey = qtKeyFromNativeVirtualKey(nativeVK);
        if (physicalKey != 0 && physicalKey != key) {
            const int physicalCombined = physicalKey | static_cast<int>(mods);
            cmdId = m_shortcutToCommand.value(QKeySequence(physicalCombined));
            if (!cmdId.isEmpty())
                return cmdId;
        }
    }

    return {};
}

int ShortcutManager::qtKeyFromNativeVirtualKey(quint32 nativeVirtualKey)
{
    // Letters A-Z: Windows VK codes 0x41-0x5A match Qt::Key_A-Qt::Key_Z
    if (nativeVirtualKey >= 0x41 && nativeVirtualKey <= 0x5A) {
        return Qt::Key_A + static_cast<int>(nativeVirtualKey - 0x41);
    }
    // Digits 0-9: Windows VK codes 0x30-0x39 match Qt::Key_0-Qt::Key_9
    if (nativeVirtualKey >= 0x30 && nativeVirtualKey <= 0x39) {
        return Qt::Key_0 + static_cast<int>(nativeVirtualKey - 0x30);
    }
    // OEM keys (US layout positions)
    switch (nativeVirtualKey) {
    case 0xBA:
        return Qt::Key_Semicolon; // VK_OEM_1
    case 0xBB:
        return Qt::Key_Equal; // VK_OEM_PLUS
    case 0xBC:
        return Qt::Key_Comma; // VK_OEM_COMMA
    case 0xBD:
        return Qt::Key_Minus; // VK_OEM_MINUS
    case 0xBE:
        return Qt::Key_Period; // VK_OEM_PERIOD
    case 0xBF:
        return Qt::Key_Slash; // VK_OEM_2
    case 0xC0:
        return Qt::Key_QuoteLeft; // VK_OEM_3  (backtick/tilde)
    case 0xDB:
        return Qt::Key_BracketLeft; // VK_OEM_4
    case 0xDC:
        return Qt::Key_Backslash; // VK_OEM_5
    case 0xDD:
        return Qt::Key_BracketRight; // VK_OEM_6
    case 0xDE:
        return Qt::Key_Apostrophe; // VK_OEM_7
    // Function keys
    case 0x70:
        return Qt::Key_F1;
    case 0x71:
        return Qt::Key_F2;
    case 0x72:
        return Qt::Key_F3;
    case 0x73:
        return Qt::Key_F4;
    case 0x74:
        return Qt::Key_F5;
    case 0x75:
        return Qt::Key_F6;
    case 0x76:
        return Qt::Key_F7;
    case 0x77:
        return Qt::Key_F8;
    case 0x78:
        return Qt::Key_F9;
    case 0x79:
        return Qt::Key_F10;
    case 0x7A:
        return Qt::Key_F11;
    case 0x7B:
        return Qt::Key_F12;
    // Common non-printable keys
    case 0x20:
        return Qt::Key_Space;
    case 0x09:
        return Qt::Key_Tab;
    case 0x0D:
        return Qt::Key_Return;
    case 0x1B:
        return Qt::Key_Escape;
    case 0x08:
        return Qt::Key_Backspace;
    case 0x2E:
        return Qt::Key_Delete;
    default:
        return 0;
    }
}

bool ShortcutManager::isShortcutInUse(
    const QKeySequence& shortcut, const QString& excludeCommandId) const
{
    QString existing = m_shortcutToCommand.value(shortcut);
    return !existing.isEmpty() && existing != excludeCommandId;
}

void ShortcutManager::pushShortcutsDisabled()
{
    const bool wasEnabled = (m_shortcutsDisableCount == 0);
    ++m_shortcutsDisableCount;
    if (wasEnabled) {
        for (auto it = m_shortcuts.constBegin(); it != m_shortcuts.constEnd(); ++it) {
            if (QShortcut* sc = it.value())
                sc->setEnabled(false);
        }
    }
}

void ShortcutManager::popShortcutsDisabled()
{
    if (m_shortcutsDisableCount <= 0) {
        return;
    }
    --m_shortcutsDisableCount;
    if (m_shortcutsDisableCount == 0) {
        for (auto it = m_shortcuts.constBegin(); it != m_shortcuts.constEnd(); ++it) {
            if (QShortcut* sc = it.value())
                sc->setEnabled(true);
        }
    }
}

QStringList ShortcutManager::lastUsedShortcuts() const
{
    return m_lastUsedShortcuts;
}

void ShortcutManager::updateShortcut(const QString& commandId)
{
    // Remove old shortcut
    if (m_shortcuts.contains(commandId)) {
        QShortcut* old = m_shortcuts.take(commandId);

        // Remove from reverse lookup
        QKeySequence oldSeq = old->key();
        if (m_shortcutToCommand.value(oldSeq) == commandId) {
            m_shortcutToCommand.remove(oldSeq);
        }

        delete old;
    }

    // Create new shortcut
    QKeySequence newSeq = shortcutFor(commandId);
    if (!newSeq.isEmpty()) {
        createShortcut(commandId, newSeq);
    }
}

void ShortcutManager::recordShortcutUsed(const QString& commandId)
{
    m_lastUsedShortcuts.removeAll(commandId);
    m_lastUsedShortcuts.prepend(commandId);
    while (m_lastUsedShortcuts.size() > MAX_LAST_USED) {
        m_lastUsedShortcuts.removeLast();
    }
    saveLastUsedToSettings();
    emit shortcutUsed(commandId);
}

void ShortcutManager::loadFromSettings()
{
    m_customShortcuts.clear();
    m_lastUsedShortcuts.clear();

    QSettings settings;
    settings.beginGroup("Shortcuts");

    const QStringList keys = settings.childKeys();
    for (const QString& key : keys) {
        if (key == "LastUsed")
            continue;
        QKeySequence seq(settings.value(key).toString());
        if (!seq.isEmpty()) {
            m_customShortcuts.insert(key, seq);
        }
    }

    m_lastUsedShortcuts = settings.value("LastUsed", QStringList()).toStringList();
    while (m_lastUsedShortcuts.size() > MAX_LAST_USED) {
        m_lastUsedShortcuts.removeLast();
    }

    settings.endGroup();
}

void ShortcutManager::saveLastUsedToSettings() const
{
    const QStringList snapshot = m_lastUsedShortcuts;
    QtConcurrent::run([snapshot]() {
        QSettings settings;
        settings.beginGroup("Shortcuts");
        settings.setValue("LastUsed", snapshot);
        settings.endGroup();
        settings.sync();
    });
}

void ShortcutManager::saveToSettings() const
{
    // Snapshot the state on the UI thread, then write on a worker thread.
    // QSettings::sync() can block the UI for seconds on Windows when the INI
    // file is large or contended, which is unacceptable during preset switches.
    QHash<QString, QString> customSnapshot;
    customSnapshot.reserve(m_customShortcuts.size());
    for (auto it = m_customShortcuts.constBegin(); it != m_customShortcuts.constEnd(); ++it) {
        customSnapshot.insert(it.key(), it.value().toString());
    }
    const QStringList lastUsedSnapshot = m_lastUsedShortcuts;

    QtConcurrent::run([customSnapshot, lastUsedSnapshot]() {
        QSettings settings;
        settings.beginGroup("Shortcuts");
        settings.remove("");
        for (auto it = customSnapshot.constBegin(); it != customSnapshot.constEnd(); ++it) {
            settings.setValue(it.key(), it.value());
        }
        settings.setValue("LastUsed", lastUsedSnapshot);
        settings.endGroup();
        settings.sync();
    });
}

} // namespace ruwa::core
