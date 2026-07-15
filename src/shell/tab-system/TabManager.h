// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_TABS_TABMANAGER_H
#define RUWA_CORE_TABS_TABMANAGER_H

#include "shell/tab-system/BaseTab.h"

#include <QObject>
#include <QHash>
#include <QList>
#include <QTimer>
#include <QUuid>
#include <functional>

namespace ruwa::core {

/**
 * @brief Central manager for tab lifecycle - THE SINGLE SOURCE OF TRUTH
 *
 * Key principles:
 * - All tab state lives here. UI widgets merely reflect this state.
 * - Tabs are identified ONLY by UUID, never by index.
 * - Indices are a display concern, computed when needed.
 * - Tab closing is a two-phase operation: switch away, then delete.
 *
 * Closing flow:
 * 1. requestCloseTab(id) is called
 * 2. If tab can close, TabManager switches to another tab (emits activeTabChanged)
 * 3. TabManager emits tabClosing(tab) - UI starts any animations
 * 4. UI calls confirmTabClosed(id) when animation completes
 * 5. TabManager actually removes the tab and emits tabRemoved(id)
 */
class TabManager : public QObject {
    Q_OBJECT

public:
    explicit TabManager(QObject* parent = nullptr);
    ~TabManager() override;

    // === Tab Management ===

    /// Add a tab. Returns true on success.
    bool addTab(BaseTab* tab);
    bool replaceTabInPlace(BaseTab* oldTab, BaseTab* newTab);

    /// Request to close a tab. This starts the close flow.
    /// Returns true if close was initiated (canClose returned true).
    bool requestCloseTab(const QUuid& tabId);
    bool requestCloseTab(BaseTab* tab);

    /// Called by UI when close animation is complete.
    /// This removes the tab from manager state and queues deferred destruction.
    void confirmTabClosed(const QUuid& tabId);

    /// Close all tabs (validates canClose for each)
    bool closeAllTabs();

    // === Navigation ===

    /// Activate a tab by UUID
    bool activateTab(const QUuid& tabId);
    bool activateTab(BaseTab* tab);
    void setActivationBlocked(bool blocked);
    bool activationBlocked() const { return m_activationBlocked; }

    /// Navigate to next/previous tab
    void activateNextTab();
    void activatePreviousTab();

    // === Query Methods (UUID-based) ===

    BaseTab* activeTab() const { return m_activeTab; }
    QUuid activeTabId() const { return m_activeTab ? m_activeTab->id() : QUuid(); }

    BaseTab* tab(const QUuid& tabId) const;
    bool hasTab(const QUuid& tabId) const;
    bool hasTab(BaseTab* tab) const;

    /// Get ordered list of all tab IDs (for display order)
    QList<QUuid> tabIds() const;

    /// Get all tabs in order
    QList<BaseTab*> tabs() const { return m_tabOrder; }

    /// Number of tabs (excludes empty state tab)
    int count() const { return m_tabOrder.size(); }

    /// Empty state tab shown when all tabs are closed (not in tab list)
    BaseTab* emptyStateTab() const { return m_emptyStateTab; }
    void setEmptyStateTab(BaseTab* tab);

    /// Get display index of a tab (for UI only)
    int displayIndex(const QUuid& tabId) const;
    int displayIndex(BaseTab* tab) const;

    /// Get tab at display index (for UI convenience)
    BaseTab* tabAtIndex(int index) const;

    // === Tab Factory System ===
    using TabFactory = std::function<BaseTab*(const QVariantMap&)>;
    void registerTabFactory(BaseTab::TabType type, TabFactory factory);

    // === Serialization ===
    QVariantList serializeAllTabs() const;
    bool deserializeAllTabs(const QVariantList& data);

signals:
    /// Tab was added
    void tabAdded(BaseTab* tab);
    void tabReplaced(BaseTab* oldTab, BaseTab* newTab);

    /// Tab is about to be closed (UI should animate)
    /// direction: 1 = new tab comes from right (close slides left), -1 = from left
    void tabClosing(BaseTab* tab, int direction);

    /// Tab was removed from manager state; destruction may be deferred briefly
    void tabRemoved(const QUuid& tabId);

    /// Active tab changed
    void activeTabChanged(BaseTab* newTab, BaseTab* oldTab);

private:
    BaseTab* findNextTabAfterClose(BaseTab* closingTab) const;
    BaseTab* createTabFromData(const QVariantMap& data);
    void scheduleDeferredTabCleanup();
    void cleanupNextClosedTab();

private:
    QList<BaseTab*> m_tabOrder; // Display order (excludes empty state)
    QHash<QUuid, BaseTab*> m_tabsById; // Fast lookup
    BaseTab* m_activeTab = nullptr;
    BaseTab* m_emptyStateTab = nullptr; // "Zero" tab when no tabs (not in m_tabOrder)
    QHash<QUuid, BaseTab*> m_closingTabs; // Tabs in the process of closing
    QList<BaseTab*> m_tabsPendingDeletion; // Deferred cleanup queue for heavy tabs
    QHash<BaseTab::TabType, TabFactory> m_factories;
    QTimer m_deferredCleanupTimer;
    bool m_activationBlocked = false;
};

} // namespace ruwa::core

#endif // RUWA_CORE_TABS_TABMANAGER_H
