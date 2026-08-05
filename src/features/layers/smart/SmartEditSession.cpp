// SPDX-License-Identifier: MPL-2.0

// ============================================================================
//   R U W A   |   C O R E   |   S M A R T   E D I T   S E S S I O N
// ============================================================================

#include "features/layers/smart/SmartEditSession.h"

// SmartContent stays incomplete on purpose: the registry only stores and copies
// the shared_ptr (both work on an incomplete type, the deleter is captured where
// the content is created), so nothing here drags in TileGrid.

namespace ruwa::core::layers {

SmartEditSessionRegistry& SmartEditSessionRegistry::instance()
{
    static SmartEditSessionRegistry registry;
    return registry;
}

SmartEditSessionRegistry::SmartEditSessionRegistry()
{
    qRegisterMetaType<SmartEditSession>("ruwa::core::layers::SmartEditSession");
}

bool SmartEditSessionRegistry::openSession(const SmartEditSession& session)
{
    if (!session.isValid()) {
        return false;
    }
    // One tab per content: instances share the content, so a second tab would be
    // a second undo stack over the same pixels.
    if (m_sessions.contains(session.contentId)) {
        return false;
    }
    // A tab edits exactly one content.
    if (m_childTabToContent.contains(session.childTabId)) {
        return false;
    }

    m_sessions.insert(session.contentId, session);
    m_childTabToContent.insert(session.childTabId, session.contentId);

    emit sessionOpened(session);
    return true;
}

const SmartEditSession* SmartEditSessionRegistry::sessionForContent(const QUuid& contentId) const
{
    auto it = m_sessions.constFind(contentId);
    return it == m_sessions.constEnd() ? nullptr : &it.value();
}

const SmartEditSession* SmartEditSessionRegistry::sessionForChildTab(const QUuid& childTabId) const
{
    auto tabIt = m_childTabToContent.constFind(childTabId);
    if (tabIt == m_childTabToContent.constEnd()) {
        return nullptr;
    }
    return sessionForContent(tabIt.value());
}

QList<SmartEditSession> SmartEditSessionRegistry::sessionsForParentTab(
    const QUuid& parentTabId) const
{
    QList<SmartEditSession> result;
    if (parentTabId.isNull()) {
        return result;
    }
    for (const auto& session : m_sessions) {
        if (session.parentTabId == parentTabId) {
            result.append(session);
        }
    }
    return result;
}

bool SmartEditSessionRegistry::hasSessionForContent(const QUuid& contentId) const
{
    return m_sessions.contains(contentId);
}

bool SmartEditSessionRegistry::isChildTab(const QUuid& tabId) const
{
    return m_childTabToContent.contains(tabId);
}

void SmartEditSessionRegistry::setOriginLayer(const QUuid& contentId, const QUuid& layerId)
{
    auto it = m_sessions.find(contentId);
    if (it == m_sessions.end()) {
        return;
    }
    it.value().originLayerId = layerId;
}

SmartEditSession SmartEditSessionRegistry::takeSession(const QUuid& contentId)
{
    auto it = m_sessions.find(contentId);
    if (it == m_sessions.end()) {
        return {};
    }
    const SmartEditSession session = it.value();
    m_sessions.erase(it);
    m_childTabToContent.remove(session.childTabId);
    return session;
}

void SmartEditSessionRegistry::closeSession(const QUuid& contentId, CloseReason reason)
{
    const SmartEditSession session = takeSession(contentId);
    if (!session.isValid()) {
        return;
    }
    emit sessionClosed(session, reason);
}

void SmartEditSessionRegistry::closeSessionsForParentTab(
    const QUuid& parentTabId, CloseReason reason)
{
    if (parentTabId.isNull()) {
        return;
    }

    // Collect the keys first: closing a session emits, and a slot may re-enter.
    QList<QUuid> contentIds;
    for (const auto& session : m_sessions) {
        if (session.parentTabId == parentTabId) {
            contentIds.append(session.contentId);
        }
    }

    for (const QUuid& contentId : contentIds) {
        const SmartEditSession session = takeSession(contentId);
        if (!session.isValid()) {
            continue; // A re-entrant close got here first.
        }
        emit childTabCloseRequested(session.childTabId);
        emit sessionClosed(session, reason);
    }
}

void SmartEditSessionRegistry::notifyTabRemoved(const QUuid& tabId)
{
    if (tabId.isNull()) {
        return;
    }

    // A contents tab can be BOTH: it edits one content and hosts the tabs of the
    // smart objects nested inside it. Those go first — once this tab is gone they
    // have nothing left to commit into.
    closeSessionsForParentTab(tabId, CloseReason::ParentTabClosed);

    auto tabIt = m_childTabToContent.constFind(tabId);
    if (tabIt != m_childTabToContent.constEnd()) {
        // By value: closing removes that very hash entry.
        const QUuid contentId = tabIt.value();
        closeSession(contentId, CloseReason::ChildTabClosed);
    }
}

} // namespace ruwa::core::layers
