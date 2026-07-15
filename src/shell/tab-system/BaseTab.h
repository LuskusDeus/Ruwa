// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_TABS_BASETAB_H
#define RUWA_CORE_TABS_BASETAB_H

#include <QWidget>
#include <QString>
#include <QIcon>
#include <QUuid>
#include <functional>

namespace ruwa::core {

class TabManager;

/**
 * @brief Abstract base class for all tabs in Ruwa
 *
 * Each tab has a stable UUID that never changes during its lifetime.
 * This UUID is the primary identifier used throughout the tab system.
 */
class BaseTab : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QUuid tabId READ id CONSTANT)

public:
    enum class TabType {
        HomePage,
        Workspace,
        Settings,
        Plugin,
        Custom,
        EmptyState ///< Special "zero" tab shown when all tabs are closed (not in tab list)
    };

    enum class TabState { Uninitialized, Initialized, Active, Background };

    explicit BaseTab(QWidget* parent = nullptr);
    ~BaseTab() override;

    // === Identification (immutable) ===
    QUuid id() const { return m_id; }
    virtual TabType type() const = 0;

    // === Display properties ===
    virtual QString title() const = 0;
    virtual QIcon icon() const { return QIcon(); }
    virtual QString tooltip() const { return title(); }

    /// Short label for tab *kind* (context menus, etc.) — not the user-editable tab title.
    virtual QString tabKindLabel() const;

    // === State ===
    TabState state() const { return m_state; }
    bool isInitialized() const { return m_isInitialized; }
    bool isModified() const { return m_isModified; }
    bool isActive() const { return m_state == TabState::Active; }
    bool needsThemeRefresh() const { return m_needsThemeRefresh; }
    TabManager* tabManager() const { return m_tabManager; }

    // === Lifecycle ===
    void initialize();
    void activate();
    void deactivate();
    virtual bool canClose();
    void setNeedsThemeRefresh(bool needsRefresh);
    void applyThemeRefresh(std::function<void()> finished = {}, bool showLoading = true);

    /// Whether the theme-apply loading overlay should be shown while this tab is
    /// refreshed (heavy refresh / full rebuild). Lightweight tabs return false.
    virtual bool wantsThemeLoadingScreen() const { return false; }

    /// Called when tab transition animation completes (or immediately if no animation).
    /// Override to defer heavy initialization (e.g. OpenGL) until after the slide animation.
    void onTransitionFinished();

    // === Serialization ===
    virtual QVariantMap serialize() const;
    virtual bool deserialize(const QVariantMap& data);

    // === Mutators ===
    void setModified(bool modified);
    void setTitle(const QString& title);
    void setIcon(const QIcon& icon);

signals:
    void titleChanged(const QString& newTitle);
    void iconChanged(const QIcon& newIcon);
    void modifiedChanged(bool modified);
    void themeRefreshStateChanged(bool needsRefresh);
    void closeRequested();
    void activated();
    void deactivated();

protected:
    explicit BaseTab(const QUuid& id, QWidget* parent = nullptr);

    virtual void onInitialize() = 0;
    virtual void onActivate() { }
    virtual void onDeactivate() { }
    virtual void onClose() { }
    virtual void onApplyThemeRefresh(std::function<void()> finished, bool showLoading);

    /// Helper for tabs that fully rebuild themselves on theme refresh (no cached
    /// colours/explicit palettes survive). @p makeReplacement must construct a
    /// fresh tab with the SAME id(); it is swapped in via TabManager and @p
    /// finished is invoked once it is live. GL-backed tabs must NOT use this
    /// (recreating a QOpenGLWidget blacks the window) — see WorkspaceTab.
    void recreateForThemeRefresh(
        std::function<BaseTab*()> makeReplacement, std::function<void()> finished);

    /// Override to defer heavy initialization (e.g. OpenGL) until after the slide animation.
    virtual void onTransitionFinishedImpl() { }

private:
    friend class TabManager;

    void setTabManager(TabManager* manager) { m_tabManager = manager; }

private:
    const QUuid m_id; // Immutable, set at construction
    TabManager* m_tabManager = nullptr;
    TabState m_state;
    bool m_isModified;
    bool m_isInitialized;
    bool m_needsThemeRefresh;
    QString m_cachedTitle;
    QIcon m_cachedIcon;
};

} // namespace ruwa::core

#endif // RUWA_CORE_TABS_BASETAB_H
