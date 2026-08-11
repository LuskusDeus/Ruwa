// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_CANVASMODIFIERSHORTCUTMANAGER_H
#define RUWA_FEATURES_CANVAS_CANVASMODIFIERSHORTCUTMANAGER_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

#include <optional>

namespace ruwa::features::canvas {

enum class CanvasModifierAction {
    MoveContent,
    Eyedropper,
    PanCanvas,
};

struct CanvasModifierShortcutDefinition {
    CanvasModifierAction action;
    QString id;
    int defaultKey;
};

/**
 * @brief Owns the configurable, single-key canvas tool shortcuts.
 *
 * These bindings cannot be represented by QShortcut: Alt/Ctrl/Shift are
 * modifier-only holds and all three actions are active only while the key is
 * held over the canvas. Keeping them in one manager gives the editor, presets,
 * and canvas input routing a single source of truth.
 */
class CanvasModifierShortcutManager : public QObject {
    Q_OBJECT

public:
    static CanvasModifierShortcutManager& instance();

    static const QVector<CanvasModifierShortcutDefinition>& definitions();
    static QVector<int> supportedKeys();
    static QString keyDisplayName(int key);

    int keyFor(CanvasModifierAction action) const;
    int keyForId(const QString& id) const;
    int defaultKeyForId(const QString& id) const;
    std::optional<CanvasModifierAction> actionForKey(int key) const;

    bool hasCustomShortcut(const QString& id) const;
    bool isShortcutConflicted(const QString& id) const;
    QHash<QString, int> customBindings() const;

    /// Assigns a key. Duplicate assignments remain stored but are inactive.
    void setShortcut(const QString& id, int key);
    void resetShortcut(const QString& id);
    void resetAllShortcuts();

    void saveToSettings() const;

signals:
    void shortcutChanged(const QString& id, int key);

private:
    explicit CanvasModifierShortcutManager(QObject* parent = nullptr);

    const CanvasModifierShortcutDefinition* definitionForId(const QString& id) const;
    const CanvasModifierShortcutDefinition* definitionForAction(CanvasModifierAction action) const;
    void setCustomOrDefault(const CanvasModifierShortcutDefinition& definition, int key);
    void loadFromSettings();
    void syncOrdinaryShortcutConflicts() const;

    QHash<QString, int> m_customShortcuts;
};

} // namespace ruwa::features::canvas

#endif // RUWA_FEATURES_CANVAS_CANVASMODIFIERSHORTCUTMANAGER_H
