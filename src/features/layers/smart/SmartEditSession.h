// SPDX-License-Identifier: MPL-2.0

// ============================================================================
//   R U W A   |   C O R E   |   S M A R T   E D I T   S E S S I O N
// ============================================================================

#ifndef RUWA_CORE_LAYERS_SMARTEDITSESSION_H
#define RUWA_CORE_LAYERS_SMARTEDITSESSION_H

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QUuid>
#include <memory>

namespace ruwa::core::layers {

struct SmartContent;

/**
 * @brief One open "edit contents" session: a child tab editing a smart object's
 *        content on behalf of the document that hosts it.
 *
 * The session is keyed by CONTENT, not by layer. Instances share one
 * `SmartContent` (see `LayerData::smartContent`), so double-clicking any of them
 * must land in the same editing tab — opening two tabs on one content would give
 * two undo stacks writing the same pixels.
 *
 * `originLayerId` is the layer the session was opened from. It is a HINT (tab
 * title, where to focus back), never the identity of the session: that layer can
 * be deleted, undone or rasterized while the session stays perfectly valid on
 * another instance.
 */
struct SmartEditSession {
    /// Identity of the edited content — the session key.
    QUuid contentId;

    /// The child tab doing the editing.
    QUuid childTabId;

    /// The document tab the content was opened from.
    QUuid parentTabId;

    /// Layer the session was opened from; a hint only (see class docs).
    QUuid originLayerId;

    /**
     * @brief Strong reference to the edited content.
     *
     * Deliberately a `shared_ptr`, not a `weak_ptr`: while a session is open the
     * child tab is editing these very pixels, so the content must not die under
     * it when the origin layer is deleted (the layer can come back through undo,
     * with the same content, and the open tab keeps working).
     *
     * This is also why a session must be closed explicitly rather than left to
     * expire: the registry is what keeps the content alive.
     *
     * Never derive UI state from `use_count()` — undo snapshots share the
     * content too.
     */
    std::shared_ptr<SmartContent> content;

    bool isValid() const
    {
        return !contentId.isNull() && !childTabId.isNull() && content != nullptr;
    }
};

/**
 * @brief Process-wide index of open smart-object editing sessions.
 *
 * Owns no tabs and no documents — it only answers "is this content already being
 * edited, and by which tab", and it holds the content alive for the duration.
 * Tab lifetime stays with `TabManager`; the shell feeds tab removals in through
 * `notifyTabRemoved()` and closes child tabs in response to
 * `childTabCloseRequested()`.
 *
 * Content ids are globally unique, so one registry can serve every document.
 */
class SmartEditSessionRegistry : public QObject {
    Q_OBJECT

public:
    enum class CloseReason {
        Explicit, ///< Committed / cancelled by the user.
        ChildTabClosed, ///< The editing tab went away.
        ParentTabClosed, ///< The document that hosts the content went away.
        ContentReplaced ///< The layer no longer points at this content.
    };
    Q_ENUM(CloseReason)

    static SmartEditSessionRegistry& instance();

    /**
     * @brief Register a freshly opened session.
     * @return false if the session is incomplete or its content is already open —
     *         the caller should activate `sessionForContent()->childTabId` instead
     *         of creating a second tab.
     */
    bool openSession(const SmartEditSession& session);

    /// Null when the content is not being edited.
    const SmartEditSession* sessionForContent(const QUuid& contentId) const;
    /// Null when the tab is not a smart-content editing tab.
    const SmartEditSession* sessionForChildTab(const QUuid& childTabId) const;

    QList<SmartEditSession> sessionsForParentTab(const QUuid& parentTabId) const;

    bool hasSessionForContent(const QUuid& contentId) const;
    bool isChildTab(const QUuid& tabId) const;
    int count() const { return static_cast<int>(m_sessions.size()); }

    /// Re-point a session at another instance of the same content (the layer it
    /// was opened from was deleted, rasterized or detached). No-op if unknown.
    void setOriginLayer(const QUuid& contentId, const QUuid& layerId);

    /// Drop a session. The child tab is NOT closed by this call (the caller owns
    /// tabs); use it after the tab is gone or when closing it yourself.
    void closeSession(const QUuid& contentId, CloseReason reason = CloseReason::Explicit);

    /// Ask for every session hosted by @p parentTabId to be closed, emitting
    /// `childTabCloseRequested()` for each child tab still alive.
    void closeSessionsForParentTab(const QUuid& parentTabId, CloseReason reason);

    /**
     * @brief Feed every tab removal in here, whatever the tab is.
     *
     * A child tab closing ends its session; a document tab closing ends every
     * session it hosts and asks for those child tabs to be closed.
     */
    void notifyTabRemoved(const QUuid& tabId);

signals:
    void sessionOpened(const ruwa::core::layers::SmartEditSession& session);
    void sessionClosed(const ruwa::core::layers::SmartEditSession& session,
        ruwa::core::layers::SmartEditSessionRegistry::CloseReason reason);

    /// The session is already gone from the registry when this fires; the
    /// receiver is only asked to dispose of the tab widget.
    void childTabCloseRequested(const QUuid& childTabId);

private:
    SmartEditSessionRegistry();

    /// Erase first, emit second — a slot may re-enter through notifyTabRemoved().
    SmartEditSession takeSession(const QUuid& contentId);

    QHash<QUuid, SmartEditSession> m_sessions; ///< contentId -> session
    QHash<QUuid, QUuid> m_childTabToContent; ///< childTabId -> contentId
};

} // namespace ruwa::core::layers

Q_DECLARE_METATYPE(ruwa::core::layers::SmartEditSession)

#endif // RUWA_CORE_LAYERS_SMARTEDITSESSION_H
