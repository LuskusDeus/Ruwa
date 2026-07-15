// SPDX-License-Identifier: MPL-2.0

#include "shell/tab-system/TabManager.h"
namespace ruwa::core {

TabManager::TabManager(QObject* parent)
    : QObject(parent)
{
    m_deferredCleanupTimer.setSingleShot(true);
    m_deferredCleanupTimer.setInterval(180);
    connect(&m_deferredCleanupTimer, &QTimer::timeout, this, &TabManager::cleanupNextClosedTab);
}

TabManager::~TabManager()
{
    m_deferredCleanupTimer.stop();

    // Clean up all tabs
    for (BaseTab* tab : m_tabOrder) {
        delete tab;
    }
    for (BaseTab* tab : m_closingTabs) {
        delete tab;
    }
    for (BaseTab* tab : m_tabsPendingDeletion) {
        delete tab;
    }
}

bool TabManager::addTab(BaseTab* tab)
{
    if (!tab) {
        return false;
    }

    const QUuid id = tab->id();

    if (m_tabsById.contains(id)) {
        return false;
    }

    // Add to collections
    m_tabOrder.append(tab);
    m_tabsById.insert(id, tab);
    tab->setTabManager(this);

    emit tabAdded(tab);

    // Auto-activate new tab
    activateTab(tab);

    return true;
}

bool TabManager::replaceTabInPlace(BaseTab* oldTab, BaseTab* newTab)
{
    if (!oldTab || !newTab) {
        return false;
    }

    if (!hasTab(oldTab)) {
        return false;
    }

    if (oldTab == m_emptyStateTab) {
        return false;
    }

    const QUuid tabId = oldTab->id();
    if (newTab->id() != tabId) {
        return false;
    }

    const int index = m_tabOrder.indexOf(oldTab);
    if (index < 0) {
        return false;
    }

    const bool wasActive = (m_activeTab == oldTab);
    if (wasActive) {
        oldTab->deactivate();
    }

    oldTab->setTabManager(nullptr);
    newTab->setTabManager(this);
    m_tabOrder[index] = newTab;
    m_tabsById.insert(tabId, newTab);

    if (wasActive) {
        m_activeTab = newTab;
        newTab->activate();
    }

    emit tabReplaced(oldTab, newTab);

    oldTab->hide();
    m_tabsPendingDeletion.append(oldTab);
    scheduleDeferredTabCleanup();

    return true;
}

bool TabManager::requestCloseTab(const QUuid& tabId)
{
    return requestCloseTab(tab(tabId));
}

bool TabManager::requestCloseTab(BaseTab* tab)
{
    if (!tab) {
        return false;
    }

    // Empty state tab cannot be closed
    if (tab == m_emptyStateTab) {
        return false;
    }

    if (!hasTab(tab)) {
        return false;
    }

    if (!tab->canClose()) {
        return false;
    }

    const QUuid tabId = tab->id();
    const int closingIndex = m_tabOrder.indexOf(tab);
    const bool wasActive = (m_activeTab == tab);

    // Find replacement tab BEFORE removing from collections
    BaseTab* replacement = findNextTabAfterClose(tab);
    // Determine animation direction based on which tab will be activated
    int direction = 1; // Default: slide left
    if (replacement) {
        int replacementIndex = m_tabOrder.indexOf(replacement);
        direction = (replacementIndex > closingIndex) ? 1 : -1;
    }

    // Move tab to closing state (remove from active lists, but keep alive)
    m_tabOrder.removeOne(tab);
    m_tabsById.remove(tabId);
    m_closingTabs.insert(tabId, tab);

    // IMPORTANT: Emit tabClosing FIRST so UI can prepare with direction info
    emit tabClosing(tab, direction);

    // Then handle activation change
    if (wasActive) {
        if (replacement) {
            // Deactivate closing tab
            tab->deactivate();

            // Activate replacement
            m_activeTab = replacement;
            replacement->activate();

            // Emit with closing tab as oldTab so animation knows what to animate from
            emit activeTabChanged(replacement, tab);
        } else {
            // No replacement - just deactivate and emit
            tab->deactivate();
            m_activeTab = nullptr;
            emit activeTabChanged(nullptr, tab);
        }
    }

    // If no tabs left and no replacement (e.g. no empty state tab), complete immediately
    // When replacement exists (incl. empty state tab), UI will call confirmTabClosed when animation
    // finishes
    if (m_tabOrder.isEmpty() && !replacement) {
        confirmTabClosed(tabId);
    }

    return true;
}

void TabManager::confirmTabClosed(const QUuid& tabId)
{
    BaseTab* tab = m_closingTabs.take(tabId);
    if (!tab) {
        return;
    }

    emit tabRemoved(tabId);

    tab->setTabManager(nullptr);
    tab->hide();
    m_tabsPendingDeletion.append(tab);
    scheduleDeferredTabCleanup();
}

void TabManager::scheduleDeferredTabCleanup()
{
    if (m_tabsPendingDeletion.isEmpty() || m_deferredCleanupTimer.isActive()) {
        return;
    }

    m_deferredCleanupTimer.start();
}

void TabManager::cleanupNextClosedTab()
{
    if (m_tabsPendingDeletion.isEmpty()) {
        return;
    }

    BaseTab* tab = m_tabsPendingDeletion.takeFirst();
    const QUuid tabId = tab ? tab->id() : QUuid();
    delete tab;

    if (!m_tabsPendingDeletion.isEmpty()) {
        m_deferredCleanupTimer.start();
    }
}

bool TabManager::closeAllTabs()
{
    // First validate all tabs can be closed
    for (BaseTab* tab : m_tabOrder) {
        if (!tab->canClose()) {
            return false;
        }
    }

    // Close all tabs (in reverse order to handle animations properly)
    while (!m_tabOrder.isEmpty()) {
        BaseTab* tab = m_tabOrder.last();
        if (!requestCloseTab(tab)) {
            return false;
        }
        // For closeAllTabs, we confirm immediately (no animation needed)
        confirmTabClosed(tab->id());
    }

    return true;
}

bool TabManager::activateTab(const QUuid& tabId)
{
    return activateTab(tab(tabId));
}

bool TabManager::activateTab(BaseTab* tab)
{
    if (m_activationBlocked) {
        return false;
    }

    if (!tab) {
        return false;
    }

    // Empty state tab can only be activated when there are no other tabs
    if (tab == m_emptyStateTab) {
        if (!m_tabOrder.isEmpty()) {
            return false;
        }
    } else {
        // Check if tab is in our collections (not closing)
        if (!hasTab(tab)) {
            return false;
        }
    }

    if (tab == m_activeTab) {
        return true;
    }

    BaseTab* oldTab = m_activeTab;

    // Deactivate old tab
    if (oldTab) {
        oldTab->deactivate();
    }

    // Activate new tab
    m_activeTab = tab;
    tab->activate();

    emit activeTabChanged(tab, oldTab);

    return true;
}

void TabManager::setActivationBlocked(bool blocked)
{
    if (m_activationBlocked == blocked) {
        return;
    }

    m_activationBlocked = blocked;
}

void TabManager::activateNextTab()
{
    if (m_tabOrder.isEmpty() || !m_activeTab) {
        return;
    }

    int currentIdx = m_tabOrder.indexOf(m_activeTab);
    int nextIdx = (currentIdx + 1) % m_tabOrder.size();

    activateTab(m_tabOrder[nextIdx]);
}

void TabManager::activatePreviousTab()
{
    if (m_tabOrder.isEmpty() || !m_activeTab) {
        return;
    }

    int currentIdx = m_tabOrder.indexOf(m_activeTab);
    int prevIdx = (currentIdx - 1 + m_tabOrder.size()) % m_tabOrder.size();

    activateTab(m_tabOrder[prevIdx]);
}

BaseTab* TabManager::tab(const QUuid& tabId) const
{
    return m_tabsById.value(tabId, nullptr);
}

bool TabManager::hasTab(const QUuid& tabId) const
{
    return m_tabsById.contains(tabId);
}

bool TabManager::hasTab(BaseTab* tab) const
{
    return tab && m_tabsById.contains(tab->id());
}

QList<QUuid> TabManager::tabIds() const
{
    QList<QUuid> ids;
    ids.reserve(m_tabOrder.size());
    for (BaseTab* tab : m_tabOrder) {
        ids.append(tab->id());
    }
    return ids;
}

int TabManager::displayIndex(const QUuid& tabId) const
{
    for (int i = 0; i < m_tabOrder.size(); ++i) {
        if (m_tabOrder[i]->id() == tabId) {
            return i;
        }
    }
    return -1;
}

int TabManager::displayIndex(BaseTab* tab) const
{
    return tab ? m_tabOrder.indexOf(tab) : -1;
}

BaseTab* TabManager::tabAtIndex(int index) const
{
    if (index >= 0 && index < m_tabOrder.size()) {
        return m_tabOrder[index];
    }
    return nullptr;
}

void TabManager::registerTabFactory(BaseTab::TabType type, TabFactory factory)
{
    m_factories[type] = factory;
}

QVariantList TabManager::serializeAllTabs() const
{
    QVariantList result;
    for (const BaseTab* tab : m_tabOrder) {
        result.append(tab->serialize());
    }
    return result;
}

bool TabManager::deserializeAllTabs(const QVariantList& data)
{
    for (const QVariant& item : data) {
        QVariantMap tabData = item.toMap();
        BaseTab* tab = createTabFromData(tabData);
        if (tab) {
            addTab(tab);
            tab->deserialize(tabData);
        }
    }
    return true;
}

void TabManager::setEmptyStateTab(BaseTab* tab)
{
    if (m_emptyStateTab == tab) {
        return;
    }

    if (m_emptyStateTab) {
        m_emptyStateTab->setTabManager(nullptr);
        m_emptyStateTab->setParent(nullptr);
    }

    m_emptyStateTab = tab;

    if (m_emptyStateTab) {
        m_emptyStateTab->setTabManager(this);
    }
}

BaseTab* TabManager::findNextTabAfterClose(BaseTab* closingTab) const
{
    // When closing the last tab, switch to empty state (if available)
    if (m_tabOrder.size() <= 1) {
        return m_emptyStateTab;
    }

    int idx = m_tabOrder.indexOf(closingTab);
    if (idx < 0) {
        return nullptr;
    }

    // Prefer the next tab, fall back to previous
    if (idx + 1 < m_tabOrder.size()) {
        return m_tabOrder[idx + 1];
    } else if (idx > 0) {
        return m_tabOrder[idx - 1];
    }

    return nullptr;
}

BaseTab* TabManager::createTabFromData(const QVariantMap& data)
{
    if (!data.contains("type")) {
        return nullptr;
    }

    auto type = static_cast<BaseTab::TabType>(data["type"].toInt());

    if (!m_factories.contains(type)) {
        return nullptr;
    }

    return m_factories[type](data);
}

} // namespace ruwa::core
