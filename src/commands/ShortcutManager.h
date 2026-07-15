// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   C O M M A N D   S Y S T E M
// ======================================================================================
//   File        : ShortcutManager.h
//   Description : Global keyboard shortcut management and customization.
// ======================================================================================

#ifndef RUWA_CORE_COMMANDS_SHORTCUTMANAGER_H
#define RUWA_CORE_COMMANDS_SHORTCUTMANAGER_H

#include <QObject>
#include <QHash>
#include <QKeySequence>
#include <QShortcut>
#include <QStringList>

class QKeyEvent;
class QWidget;

namespace ruwa::core {

/**
 * @brief Manages keyboard shortcuts for commands
 *
 * Features:
 * - Automatic registration of default shortcuts from commands
 * - User customization with persistence
 * - Conflict detection
 * - Context-aware activation
 */
class ShortcutManager : public QObject {
    Q_OBJECT

public:
    // === Singleton ===

    static ShortcutManager& instance();

    // === Setup ===

    /// Set the widget that will receive shortcuts (usually MainWindow)
    void setShortcutContext(QWidget* contextWidget);

    /// Register all shortcuts from registered commands
    void registerAllShortcuts();

    // === Customization ===

    /// Set custom shortcut for a command (overrides default)
    void setShortcut(const QString& commandId, const QKeySequence& shortcut);

    /// Remove custom shortcut, revert to default
    void resetShortcut(const QString& commandId);

    /// Remove all shortcuts for a command
    void clearShortcut(const QString& commandId);

    /// Get current shortcut for a command
    QKeySequence shortcutFor(const QString& commandId) const;

    /// Get default shortcut for a command
    QKeySequence defaultShortcutFor(const QString& commandId) const;

    /// Check if command has custom shortcut
    bool hasCustomShortcut(const QString& commandId) const;

    // === Conflict Detection ===

    /// Find command that uses this shortcut (empty string if none)
    QString commandForShortcut(const QKeySequence& shortcut) const;

    /// Find command for a key event, handling keyboard layout mapping.
    /// Tries the Qt key first, then falls back to the physical key via nativeVirtualKey.
    QString commandForKeyEvent(const QKeyEvent* event) const;

    /// Map a native virtual key code to the corresponding Qt::Key for the
    /// US-Latin physical layout.  Returns 0 if the mapping is unknown.
    static int qtKeyFromNativeVirtualKey(quint32 nativeVirtualKey);

    /// Check if shortcut is already in use
    bool isShortcutInUse(const QKeySequence& shortcut, const QString& excludeCommandId = {}) const;

    // === Global Blocking (reference-counted) ===

    /// Push a disable request (increments counter).
    /// Shortcuts are disabled while any requester holds a block.
    void pushShortcutsDisabled();

    /// Pop a disable request (decrements counter).
    /// Shortcuts are re-enabled only when all blocks are released.
    void popShortcutsDisabled();

    /// Whether shortcuts are currently enabled (no outstanding blocks)
    bool shortcutsEnabled() const { return m_shortcutsDisableCount == 0; }

    // === Last Used Shortcuts ===

    /// Get last 5 used shortcuts (command IDs, most recent first).
    QStringList lastUsedShortcuts() const;

    // === Persistence ===

    /// Load custom shortcuts from settings
    void loadFromSettings();

    /// Save custom shortcuts to settings
    void saveToSettings() const;

signals:
    /// Emitted when a shortcut is changed
    void shortcutChanged(const QString& commandId, const QKeySequence& newShortcut);

    /// Emitted when a shortcut is used (for UI like last-used list)
    void shortcutUsed(const QString& commandId);

private:
    ShortcutManager();
    ~ShortcutManager() override;

    Q_DISABLE_COPY_MOVE(ShortcutManager)

    void createShortcut(const QString& commandId, const QKeySequence& sequence);
    void updateShortcut(const QString& commandId);
    void recordShortcutUsed(const QString& commandId);
    void saveLastUsedToSettings() const;

private:
    QWidget* m_contextWidget = nullptr;
    QHash<QString, QShortcut*> m_shortcuts; // commandId -> QShortcut
    int m_shortcutsDisableCount = 0; // >0 when overlays block shortcuts
    QHash<QString, QKeySequence> m_customShortcuts; // commandId -> custom sequence
    QHash<QKeySequence, QString> m_shortcutToCommand; // reverse lookup
    QStringList m_lastUsedShortcuts; // last 5 used (most recent first)
};

} // namespace ruwa::core

#endif // RUWA_CORE_COMMANDS_SHORTCUTMANAGER_H
