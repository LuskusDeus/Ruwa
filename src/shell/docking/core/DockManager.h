// SPDX-License-Identifier: MPL-2.0

// DockManager.h
#ifndef RUWA_UI_DOCKING_CORE_DOCKMANAGER_H
#define RUWA_UI_DOCKING_CORE_DOCKMANAGER_H

#include "shell/docking/DockTypes.h"
#include "DockContainerWidget.h"

#include <QObject>
#include <QPointer>
#include <QPoint>
#include <QMap>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QSet>

namespace ruwa::ui::docking {

class DockPanel;

/**
 * @brief RAII guard for operation state
 */
class OperationGuard {
public:
    explicit OperationGuard(bool& flag)
        : m_flag(flag)
        , m_acquired(false)
    {
        if (!m_flag) {
            m_flag = true;
            m_acquired = true;
        }
    }
    ~OperationGuard()
    {
        if (m_acquired) {
            m_flag = false;
        }
    }
    bool acquired() const { return m_acquired; }
    operator bool() const { return m_acquired; }

private:
    bool& m_flag;
    bool m_acquired;
};

/**
 * @brief Central manager for the docking system
 *
 * DockManager handles:
 * - Panel registration and lifetime
 * - Drag & drop coordination
 * - State serialization/deserialization
 * - Layout presets
 *
 * Thread-safety: Not thread-safe, must be used from main thread only.
 * All operations are protected against re-entrancy and double-calls.
 */
class DockManager : public QObject {
    Q_OBJECT

public:
    explicit DockManager(QObject* parent = nullptr);
    ~DockManager() override;

    // === Container ===

    void setContainer(DockContainerWidget* container);
    DockContainerWidget* container() const { return m_container; }

    // === Panel Registration ===

    void registerPanel(DockPanel* panel);
    void unregisterPanel(DockPanel* panel);
    QList<DockPanel*> panels() const
    {
        QList<DockPanel*> result;
        for (auto p : m_panels)
            if (p)
                result << p.data();
        return result;
    }

    DockPanel* panel(const DockPanelId& id) const;
    DockPanel* panelByTitle(const QString& title) const;
    DockPanel* panelByPersistentKey(const QString& key) const;

    /// Check if panel is registered and valid
    bool isPanelValid(DockPanel* panel) const;

    // === Panel Operations ===

    void addPanel(DockPanel* panel, DockPosition position);
    void addPanelRelativeTo(DockPanel* panel, DockPanel* relativeTo, DockPosition position);
    void removePanel(DockPanel* panel);
    void closePanel(DockPanel* panel);
    void showPanel(DockPanel* panel, DockPosition position = DockPosition::Right);
    void floatPanel(DockPanel* panel, const QPoint& globalPos, bool exactPosition = false);
    void dockPanel(DockPanel* panel, DockPosition position);

    // === Drag & Drop ===

    void startDrag(DockPanel* panel, const QPoint& globalPos);
    void updateDrag(const QPoint& globalPos);
    void endDrag(const QPoint& globalPos);
    void cancelDrag();

    bool isDragging() const { return m_dragState.active; }
    DockPanel* draggedPanel() const { return m_dragState.panel; }

    // === State Serialization ===

    QJsonObject saveState() const;
    bool restoreState(const QJsonObject& state);
    void resetLayout();

    // === Configuration ===

    /// Minimum time between drag updates (ms), default 16 (~60fps)
    void setDragUpdateInterval(int ms) { m_dragUpdateIntervalMs = ms; }
    int dragUpdateInterval() const { return m_dragUpdateIntervalMs; }

signals:
    void panelRegistered(DockPanel* panel);
    void panelUnregistered(DockPanel* panel);
    void panelClosed(DockPanel* panel);
    void panelShown(DockPanel* panel);
    void panelFloated(DockPanel* panel);
    void panelDocked(DockPanel* panel);
    void layoutChanged();

    void dragStarted(DockPanel* panel);
    void dragFinished(DockPanel* panel, bool dropped);

private slots:
    void onPanelCloseRequested();
    void onPanelFloatRequested();
    void onPanelDockRequested();
    void onTitleBarDragStarted(const QPoint& globalPos);
    void onTitleBarDragging(const QPoint& globalPos);
    void onTitleBarDragFinished(const QPoint& globalPos);
    void onPanelDestroyed(QObject* obj);

private:
    void connectPanel(DockPanel* panel);
    void disconnectPanel(DockPanel* panel);
    void cleanupDragState();
    bool validateDragOperation() const;
    bool shouldIgnoreDockTargets() const;

private:
    QPointer<DockContainerWidget> m_container;

    // Panel registry with QPointer for safety
    QMap<DockPanelId, QPointer<DockPanel>> m_panels;

    // Panels being modified (prevent re-entrant modifications)
    QSet<DockPanelId> m_panelsInOperation;

    // Drag state - consolidated in struct for atomic operations
    struct DragState {
        bool active = false;
        QPointer<DockPanel> panel;
        QPoint startPos;
        bool startedFromDocked = false;
        QElapsedTimer updateTimer;
        QPoint lastUpdatePos;

        // Overlay cooldown (prevents overlay spam during layout animation)
        QElapsedTimer overlayCooldownTimer;
        bool overlaysEnabled = false;

        void reset()
        {
            active = false;
            panel = nullptr;
            startPos = QPoint();
            startedFromDocked = false;
            lastUpdatePos = QPoint();
            overlaysEnabled = false;
        }
    };
    DragState m_dragState;

    // Throttling
    int m_dragUpdateIntervalMs = 16; // ~60fps
    int m_overlayCooldownMs = 400; // Delay before showing overlays after undock

    // Re-entrancy guards
    bool m_inLayoutChange = false;
    bool m_inDragOperation = false;
    bool m_inPanelOperation = false;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_CORE_DOCKMANAGER_H
