// SPDX-License-Identifier: MPL-2.0

#include "shell/tab-system/BaseTab.h"
#include "shell/tab-system/TabManager.h"
#include <QPointer>
#include <QTimer>
#include <QStyle>

namespace ruwa::core {

namespace {

// With an app-wide stylesheet active, QStyleSheetStyle caches each widget's
// resolved palette and only re-resolves it on a (re)polish. So a theme change
// that only swaps qApp's palette updates QSS `palette(...)` references (buttons,
// etc.) but NOT C++ reads like widget->palette().window() (e.g. SmoothScrollArea
// backgrounds) — those stay frozen until the widget is repolished. Repolishing
// just the active tab's subtree fixes that cheaply (one tab, not the whole app).
void repolishTabThemeRecursive(QWidget* widget)
{
    if (!widget) {
        return;
    }
    if (QStyle* style = widget->style()) {
        style->unpolish(widget);
        style->polish(widget);
    }
    widget->update();
    const QList<QWidget*> children = widget->findChildren<QWidget*>(Qt::FindDirectChildrenOnly);
    for (QWidget* child : children) {
        repolishTabThemeRecursive(child);
    }
}

} // namespace

BaseTab::BaseTab(QWidget* parent)
    : QWidget(parent)
    , m_id(QUuid::createUuid()) // UUID is created once and never changes
    , m_state(TabState::Uninitialized)
    , m_isModified(false)
    , m_isInitialized(false)
    , m_needsThemeRefresh(false)
{
}

BaseTab::BaseTab(const QUuid& id, QWidget* parent)
    : QWidget(parent)
    , m_id(id.isNull() ? QUuid::createUuid() : id)
    , m_state(TabState::Uninitialized)
    , m_isModified(false)
    , m_isInitialized(false)
    , m_needsThemeRefresh(false)
{
}

BaseTab::~BaseTab()
{
    onClose();
}

void BaseTab::initialize()
{
    if (m_isInitialized) {
        return;
    }

    onInitialize();
    m_isInitialized = true;
    m_state = TabState::Initialized;
}

void BaseTab::activate()
{
    if (!m_isInitialized) {
        initialize();
    }

    m_state = TabState::Active;
    onActivate();
    emit activated();
}

void BaseTab::deactivate()
{
    if (m_state != TabState::Active) {
        return;
    }

    m_state = TabState::Background;
    onDeactivate();
    emit deactivated();
}

bool BaseTab::canClose()
{
    if (m_isModified) {
        return false;
    }
    return true;
}

void BaseTab::setNeedsThemeRefresh(bool needsRefresh)
{
    if (m_needsThemeRefresh == needsRefresh) {
        return;
    }

    m_needsThemeRefresh = needsRefresh;
    emit themeRefreshStateChanged(needsRefresh);
}

void BaseTab::applyThemeRefresh(std::function<void()> finished, bool showLoading)
{
    if (!m_needsThemeRefresh) {
        if (finished) {
            finished();
        }
        return;
    }

    QPointer<BaseTab> guard(this);
    onApplyThemeRefresh(
        [guard, finished = std::move(finished)]() mutable {
            if (guard) {
                guard->setNeedsThemeRefresh(false);
            }
            if (finished) {
                finished();
            }
        },
        showLoading);
}

void BaseTab::onApplyThemeRefresh(std::function<void()> finished, bool showLoading)
{
    Q_UNUSED(showLoading);
    // Re-resolve this tab's widget palettes against the new theme palette (see
    // helper note). Without this, custom widgets that read palette() in C++ keep
    // the previous theme's colours until the app restarts.
    repolishTabThemeRecursive(this);
    QTimer::singleShot(0, this, [finished = std::move(finished)]() mutable {
        if (finished) {
            finished();
        }
    });
}

void BaseTab::recreateForThemeRefresh(
    std::function<BaseTab*()> makeReplacement, std::function<void()> finished)
{
    TabManager* manager = m_tabManager;
    if (!manager || !makeReplacement) {
        if (finished) {
            finished();
        }
        return;
    }

    BaseTab* replacement = makeReplacement();
    if (!replacement) {
        if (finished) {
            finished();
        }
        return;
    }

    // Carry over the modified flag; recreation preserves the conceptual tab.
    replacement->m_isModified = m_isModified;
    replacement->setNeedsThemeRefresh(false);

    const bool replaced = manager->replaceTabInPlace(this, replacement);
    if (!replaced) {
        replacement->deleteLater();
        if (finished) {
            finished();
        }
        return;
    }

    // replaceTabInPlace synchronously activates the replacement and (via the tab
    // content widget) shows + initializes it. Report completion on the next event
    // loop turn so any loading overlay stays up for at least a frame.
    QPointer<BaseTab> guard(replacement);
    QTimer::singleShot(0, replacement, [guard, finished = std::move(finished)]() mutable {
        Q_UNUSED(guard);
        if (finished) {
            finished();
        }
    });
}

QString BaseTab::tabKindLabel() const
{
    switch (type()) {
    case TabType::HomePage:
        return tr("Home");
    case TabType::Workspace:
        return tr("Workspace");
    case TabType::Settings:
        return tr("Settings");
    case TabType::Plugin:
        return tr("Plugin");
    case TabType::EmptyState:
        return tr("Empty");
    case TabType::Custom:
    default:
        return tr("Custom");
    }
}

QVariantMap BaseTab::serialize() const
{
    QVariantMap data;
    data["id"] = m_id.toString();
    data["type"] = static_cast<int>(type());
    data["title"] = title();
    data["modified"] = m_isModified;
    return data;
}

bool BaseTab::deserialize(const QVariantMap& data)
{
    if (!data.contains("id") || !data.contains("type")) {
        return false;
    }
    return true;
}

void BaseTab::setModified(bool modified)
{
    if (m_isModified == modified) {
        return;
    }
    m_isModified = modified;
    emit modifiedChanged(modified);
}

void BaseTab::setTitle(const QString& title)
{
    if (m_cachedTitle == title) {
        return;
    }
    m_cachedTitle = title;
    emit titleChanged(title);
}

void BaseTab::setIcon(const QIcon& icon)
{
    m_cachedIcon = icon;
    emit iconChanged(icon);
}

void BaseTab::onTransitionFinished()
{
    onTransitionFinishedImpl();
}

} // namespace ruwa::core
