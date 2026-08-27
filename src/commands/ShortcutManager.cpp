// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   C O M M A N D   S Y S T E M
// ======================================================================================

#include "ShortcutManager.h"
#include "commands/CommandRegistry.h"
#include "commands/CommandExecutor.h"
#include "commands/Command.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QShortcut>
#include <QTextEdit>
#include <QWidget>
#include <QtConcurrent>

namespace {
constexpr int MAX_LAST_USED = 5;

bool isTextInputWidget(const QWidget* widget)
{
    for (const QWidget* current = widget; current; current = current->parentWidget()) {
        if (qobject_cast<const QLineEdit*>(current) || qobject_cast<const QTextEdit*>(current)
            || qobject_cast<const QPlainTextEdit*>(current)
            || qobject_cast<const QAbstractSpinBox*>(current)) {
            return true;
        }
    }
    return false;
}

/// Whether the key is on its way to a text editor, either as the event's target or
/// as the focus widget: a layout-remapped key must never be stolen from typing.
bool isTextInputTarget(const QObject* watched)
{
    return isTextInputWidget(qobject_cast<const QWidget*>(watched))
        || isTextInputWidget(QApplication::focusWidget());
}
} // namespace

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

    // Installed on the application so it sees key presses no QShortcut consumed.
    // Filters run most-recently-installed first, so anything that grabs the keyboard
    // later - the shortcut recorder, for one - still gets the event before we do.
    if (m_contextWidget && !m_layoutFilterInstalled) {
        if (QCoreApplication* app = QCoreApplication::instance()) {
            app->installEventFilter(this);
            m_layoutFilterInstalled = true;
        }
    }
}

bool ShortcutManager::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        const QString commandId = layoutFallbackCommand(watched, static_cast<QKeyEvent*>(event));
        if (!commandId.isEmpty()) {
            activateShortcut(commandId);
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

QString ShortcutManager::layoutFallbackCommand(const QObject* watched, const QKeyEvent* event) const
{
    if (!event || !m_contextWidget || !shortcutsEnabled()) {
        return {};
    }

    // Only step in when the active layout disagrees with the physical key. Under a
    // Latin layout Qt's own matching has already had its say - it either fired the
    // QShortcut (and then no KeyPress reaches us at all) or deliberately did not -
    // and re-deciding here would fire commands twice.
    const int physicalKey = qtKeyFromNativeVirtualKey(event->nativeVirtualKey());
    if (physicalKey == 0 || physicalKey == event->key()) {
        return {};
    }

    // Whatever an editor is about to receive is text, not a command.
    if (isTextInputTarget(watched)) {
        return {};
    }

    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    const QString commandId
        = commandForShortcut(QKeySequence(physicalKey | static_cast<int>(mods)));
    if (commandId.isEmpty()) {
        return {};
    }

    // Gate on the real QShortcut so conflicts, cleared bindings and the disabled
    // states from refreshShortcutEnabledStates() apply to this path identically.
    const QShortcut* shortcut = m_shortcuts.value(commandId);
    return (shortcut && shortcut->isEnabled()) ? commandId : QString();
}

void ShortcutManager::activateShortcut(const QString& commandId)
{
    recordShortcutUsed(commandId);
    CommandExecutor::instance().execute(commandId);
}

void ShortcutManager::registerAllShortcuts()
{
    if (!m_contextWidget) {
        return;
    }

    // Clear existing shortcuts
    qDeleteAll(m_shortcuts);
    m_shortcuts.clear();
    m_shortcutToCommands.clear();

    // Register shortcuts for all commands
    for (Command* cmd : CommandRegistry::instance().allCommands()) {
        const QKeySequence seq = shortcutFor(cmd->id());
        if (!seq.isEmpty()) {
            createShortcut(cmd->id(), seq);
        }
    }
    refreshShortcutEnabledStates();
}

void ShortcutManager::createShortcut(const QString& commandId, const QKeySequence& sequence)
{
    if (sequence.isEmpty() || !m_contextWidget) {
        return;
    }

    QShortcut* shortcut = new QShortcut(sequence, m_contextWidget);
    shortcut->setContext(Qt::ApplicationShortcut);

    connect(shortcut, &QShortcut::activated, this,
        [this, commandId]() { activateShortcut(commandId); });

    m_shortcuts.insert(commandId, shortcut);
    m_shortcutToCommands.insert(sequence, commandId);
}

void ShortcutManager::setShortcut(const QString& commandId, const QKeySequence& shortcut)
{
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

bool ShortcutManager::isShortcutConflicted(const QString& commandId) const
{
    const QKeySequence shortcut = shortcutFor(commandId);
    if (shortcut.isEmpty()) {
        return false;
    }
    return m_externallyConflictedShortcuts.contains(shortcut)
        || m_shortcutToCommands.values(shortcut).size() > 1;
}

QString ShortcutManager::commandForShortcut(const QKeySequence& shortcut) const
{
    if (shortcut.isEmpty() || m_externallyConflictedShortcuts.contains(shortcut)) {
        return {};
    }
    const auto commands = m_shortcutToCommands.values(shortcut);
    return commands.size() == 1 ? commands.first() : QString();
}

QString ShortcutManager::commandForKeyEvent(const QKeyEvent* event) const
{
    if (!event)
        return {};

    // 1. Try the Qt key as-is (works when layout matches shortcut definition)
    const int key = event->key();
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    const int combined = key | static_cast<int>(mods);
    QString cmdId = commandForShortcut(QKeySequence(combined));
    if (!cmdId.isEmpty())
        return cmdId;

    // 2. Fall back to physical key via native virtual key code (layout-independent)
    const quint32 nativeVK = event->nativeVirtualKey();
    if (nativeVK != 0) {
        const int physicalKey = qtKeyFromNativeVirtualKey(nativeVK);
        if (physicalKey != 0 && physicalKey != key) {
            const int physicalCombined = physicalKey | static_cast<int>(mods);
            cmdId = commandForShortcut(QKeySequence(physicalCombined));
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
    const auto commands = m_shortcutToCommands.values(shortcut);
    for (const QString& commandId : commands) {
        if (commandId != excludeCommandId) {
            return true;
        }
    }
    return false;
}

void ShortcutManager::setExternallyConflictedShortcuts(const QSet<QKeySequence>& shortcuts)
{
    if (m_externallyConflictedShortcuts == shortcuts) {
        return;
    }
    m_externallyConflictedShortcuts = shortcuts;
    refreshShortcutEnabledStates();
    emit shortcutConflictsChanged();
}

void ShortcutManager::pushShortcutsDisabled()
{
    const bool wasEnabled = (m_shortcutsDisableCount == 0);
    ++m_shortcutsDisableCount;
    if (wasEnabled) {
        refreshShortcutEnabledStates();
    }
}

void ShortcutManager::popShortcutsDisabled()
{
    if (m_shortcutsDisableCount <= 0) {
        return;
    }
    --m_shortcutsDisableCount;
    if (m_shortcutsDisableCount == 0) {
        refreshShortcutEnabledStates();
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
        m_shortcutToCommands.remove(oldSeq, commandId);

        delete old;
    }

    // Create new shortcut
    QKeySequence newSeq = shortcutFor(commandId);
    if (!newSeq.isEmpty()) {
        createShortcut(commandId, newSeq);
    }
    refreshShortcutEnabledStates();
}

void ShortcutManager::refreshShortcutEnabledStates()
{
    const bool globallyEnabled = shortcutsEnabled();
    for (auto it = m_shortcuts.constBegin(); it != m_shortcuts.constEnd(); ++it) {
        if (QShortcut* shortcut = it.value()) {
            shortcut->setEnabled(globallyEnabled && !isShortcutConflicted(it.key()));
        }
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
        if (key == "LastUsed") {
            continue;
        }

        const QString commandId = CommandRegistry::instance().canonicalCommandId(key);
        // A canonical entry written by a newer version wins over its legacy key.
        if (commandId != key && keys.contains(commandId)) {
            continue;
        }

        const QString storedShortcut = settings.value(key).toString();
        // An existing key with an empty value is an explicit override: the user
        // cleared the default shortcut and expects it to remain unassigned.
        if (storedShortcut.isEmpty()) {
            m_customShortcuts.insert(commandId, QKeySequence());
            continue;
        }

        QKeySequence seq = QKeySequence::fromString(storedShortcut, QKeySequence::PortableText);
        if (seq.isEmpty()) {
            seq = QKeySequence::fromString(storedShortcut, QKeySequence::NativeText);
        }
        if (!seq.isEmpty()) {
            m_customShortcuts.insert(commandId, seq);
        }
    }

    m_lastUsedShortcuts = settings.value("LastUsed", QStringList()).toStringList();
    for (QString& commandId : m_lastUsedShortcuts) {
        commandId = CommandRegistry::instance().canonicalCommandId(commandId);
    }
    m_lastUsedShortcuts.removeDuplicates();
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
        customSnapshot.insert(it.key(), it.value().toString(QKeySequence::PortableText));
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
