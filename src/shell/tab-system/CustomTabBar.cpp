// SPDX-License-Identifier: MPL-2.0

#include "CustomTabBar.h"
#include "shell/tab-system/TabManager.h"
#include "shell/tab-system/BaseTab.h"
#include "shell/tab-system/WorkspaceTab.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/settings/SettingsManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/PaintingUtils.h"
#include "shared/widgets/reorderlist/ListDragDrop.h"
#include "shell/top-bar/TopBar.h"
#include "shell/top-bar/UnsavedChangesHelper.h"

#include <QEvent>
#include <QApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QWindow>
#include <QCursor>
#include <QResizeEvent>
#include <QSet>
#include <QShowEvent>

#include <utility>

namespace {

/// Map tab type to a default icon alias from resources
qreal tabPopDistancePx()
{
    return ruwa::ui::core::ThemeManager::instance().scaled(10);
}

QString defaultIconForTabType(ruwa::core::BaseTab::TabType type)
{
    switch (type) {
    case ruwa::core::BaseTab::TabType::HomePage:
        return QStringLiteral("Home");
    case ruwa::core::BaseTab::TabType::Workspace:
        return QStringLiteral("Brush");
    case ruwa::core::BaseTab::TabType::Settings:
        return QStringLiteral("Settings");
    case ruwa::core::BaseTab::TabType::Plugin:
        return QStringLiteral("List");
    case ruwa::core::BaseTab::TabType::EmptyState:
    case ruwa::core::BaseTab::TabType::Custom:
    default:
        return QStringLiteral("BasicFile");
    }
}

} // anonymous namespace

namespace ruwa::ui::tabs {

CustomTabBar::CustomTabBar(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_layoutSlideAnim = new QVariantAnimation(this);
    m_layoutSlideAnim->setDuration(240);
    connect(m_layoutSlideAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        const qreal t = v.toReal();
        for (auto& item : m_items) {
            const qreal s = m_layoutSlideStartById.value(item.id, 0.0);
            item.slideOffsetX = s * t;
        }
        update();
    });
    connect(m_layoutSlideAnim, &QVariantAnimation::finished, this, [this]() {
        for (auto& item : m_items) {
            item.slideOffsetX = 0;
        }
        m_layoutSlideStartById.clear();
        refreshStripAlignment(true);
        update();
    });

    m_stripAlignAnim = new QVariantAnimation(this);
    m_stripAlignAnim->setDuration(280);
    m_stripAlignAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_stripAlignAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_stripAlignOffset = v.toReal();
        updateLayout();
    });
    connect(m_stripAlignAnim, &QVariantAnimation::finished, this, [this]() {
        if (m_stripAlignAnim) {
            m_stripAlignOffset = m_stripAlignAnim->endValue().toReal();
        }
        updateLayout();
    });

    connect(&ruwa::core::SettingsManager::instance(),
        &ruwa::core::SettingsManager::topBarTabAlignmentChanged, this,
        [this](int) { refreshStripAlignment(true); });

    // Apply initial scaled sizes
    updateScaledSizes();

    // Connect to theme changes
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &CustomTabBar::onThemeChanged);
}

CustomTabBar::~CustomTabBar()
{
    resetTabDragState();
    if (m_layoutSlideAnim) {
        m_layoutSlideAnim->stop();
        delete m_layoutSlideAnim;
        m_layoutSlideAnim = nullptr;
    }
    for (auto& item : m_items) {
        delete item.hoverAnim;
        delete item.fadeAnim;
        delete item.closeRevealAnim;
    }
}

void CustomTabBar::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && m_tabManager) {
        for (int i = 0; i < m_items.size(); ++i) {
            if (auto* tab = m_tabManager->tab(m_items[i].id))
                m_items[i].title = tab->title();
        }
        updateLayout();
        refreshStripAlignment(true);
        update();
    }
}

void CustomTabBar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_initialAlignDone) {
        refreshStripAlignment(true);
    }
}

void CustomTabBar::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_initialAlignDone) {
        m_initialAlignDone = true;
        refreshStripAlignment(false);
    }
}

qreal CustomTabBar::computeStripContentWidth() const
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    const int ICON_SIZE = theme.scaled(16);
    const int ICON_MARGIN = theme.scaled(8);
    const int CLOSE_SIZE = theme.scaled(14);
    const int CLOSE_MARGIN = theme.scaled(8);
    const int SEP_MARGIN = theme.scaled(12);
    const int TAB_PADDING = theme.scaled(12);
    const int TEXT_PADDING = theme.scaled(4);

    qreal x = TAB_PADDING;
    const QFont tabFont = theme.font(ruwa::ui::core::ThemeFontRole::Body);
    const QFontMetrics fm(tabFont);

    for (int i = 0; i < m_items.size(); ++i) {
        const TabItem& item = m_items[i];
        // Must mirror updateLayout(): smart object items carry no icon.
        qreal w = item.isSmartObject ? ICON_MARGIN : (ICON_MARGIN + ICON_SIZE + ICON_MARGIN);
        w += fm.horizontalAdvance(item.title) + TEXT_PADDING;
        w += CLOSE_MARGIN + CLOSE_SIZE;
        x += w;
        if (i < m_items.size() - 1) {
            x += SEP_MARGIN * 2;
        }
    }
    return x + TAB_PADDING;
}

qreal CustomTabBar::stripAlignmentTarget() const
{
    if (ruwa::core::SettingsManager::instance().settings().appearance.topBarTabAlignment != 1) {
        return 0;
    }
    const qreal cw = computeStripContentWidth();
    const int w = width();
    if (w <= 0 || cw <= 0) {
        return 0;
    }

    QWidget* container = parentWidget();
    auto* topBar
        = container ? qobject_cast<ruwa::ui::widgets::TopBar*>(container->parentWidget()) : nullptr;
    if (!topBar || topBar->width() <= 0) {
        return qMax(0.0, (static_cast<qreal>(w) - cw) / 2.0);
    }

    // Align strip center with TopBar center (not with tab container center).
    const QPoint originInTopBar = mapTo(topBar, QPoint(0, 0));
    const qreal topMid = static_cast<qreal>(topBar->width()) / 2.0;
    const qreal targetOffset = topMid - static_cast<qreal>(originInTopBar.x()) - cw / 2.0;

    const qreal maxOffset = qMax(0.0, static_cast<qreal>(w) - cw);
    return qBound(0.0, targetOffset, maxOffset);
}

void CustomTabBar::refreshStripAlignment(bool animated)
{
    const qreal target = stripAlignmentTarget();

    if (!animated) {
        if (m_stripAlignAnim) {
            m_stripAlignAnim->stop();
        }
        m_stripAlignOffset = target;
        updateLayout();
        return;
    }

    if (qAbs(target - m_stripAlignOffset) < 0.5) {
        if (m_stripAlignAnim) {
            m_stripAlignAnim->stop();
        }
        m_stripAlignOffset = target;
        updateLayout();
        return;
    }

    if (!m_stripAlignAnim) {
        m_stripAlignOffset = target;
        updateLayout();
        return;
    }

    m_stripAlignAnim->stop();
    m_stripAlignAnim->setStartValue(m_stripAlignOffset);
    m_stripAlignAnim->setEndValue(target);
    m_stripAlignAnim->start();
}

void CustomTabBar::setTabManager(ruwa::core::TabManager* manager)
{
    resetTabDragState();
    if (m_tabManager) {
        disconnect(m_tabManager, nullptr, this, nullptr);
    }

    m_tabManager = manager;

    if (m_tabManager) {
        connect(m_tabManager, &ruwa::core::TabManager::tabAdded, this, &CustomTabBar::onTabAdded);
        connect(
            m_tabManager, &ruwa::core::TabManager::tabReplaced, this, &CustomTabBar::onTabReplaced);
        connect(
            m_tabManager, &ruwa::core::TabManager::tabClosing, this, &CustomTabBar::onTabClosing);
        connect(
            m_tabManager, &ruwa::core::TabManager::tabRemoved, this, &CustomTabBar::onTabRemoved);
        connect(m_tabManager, &ruwa::core::TabManager::activeTabChanged, this,
            &CustomTabBar::onActiveTabChanged);
        connect(m_tabManager, &ruwa::core::TabManager::tabOrderChanged, this,
            &CustomTabBar::onTabOrderChanged);

        rebuildFromManager();
    }
}

bool CustomTabBar::isSmartObjectTab(ruwa::core::BaseTab* tab)
{
    auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab);
    return wsTab && wsTab->isSmartContentEditor();
}

CustomTabBar::TabItem CustomTabBar::makeItem(ruwa::core::BaseTab* tab)
{
    TabItem item;
    item.id = tab->id();
    item.title = tab->title();

    if (auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab);
        wsTab && wsTab->isSmartContentEditor()) {
        // Contents of a smart object: a breadcrumb child of its document, with no
        // identity of its own to show (no icon, no user-set name).
        item.isSmartObject = true;
        item.parentTabId = wsTab->smartEditDocumentTabId();
    } else {
        item.icon = tab->icon();
        item.iconAlias = defaultIconForTabType(tab->type());
        if (auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab)) {
            if (!wsTab->tabIconAlias().isEmpty()) {
                item.iconAlias = wsTab->tabIconAlias();
            }
        }
        if (item.icon.isNull()) {
            item.icon = ruwa::ui::core::IconProvider::instance().getIcon(item.iconAlias);
        }
    }

    item.hoverAnim = new QVariantAnimation(this);
    item.hoverAnim->setDuration(200);
    item.hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
    item.hoverAnim->setStartValue(0.0);
    item.hoverAnim->setEndValue(1.0);

    const QUuid itemId = item.id;
    connect(
        item.hoverAnim, &QVariantAnimation::valueChanged, this, [this, itemId](const QVariant& v) {
            int i = m_indexById.value(itemId, -1);
            if (i >= 0) {
                m_items[i].hoverProgress = v.toReal();
                update();
            }
        });

    item.fadeAnim = new QVariantAnimation(this);
    item.fadeAnim->setDuration(250); // Fade-in duration
    item.fadeAnim->setEasingCurve(QEasingCurve::OutCubic);
    item.fadeAnim->setStartValue(0.0);
    item.fadeAnim->setEndValue(1.0);

    connect(item.fadeAnim, &QVariantAnimation::valueChanged, this,
        [this, itemId](const QVariant& v) { applyTabVisibilityAnimFrame(itemId, v.toReal()); });

    item.closeRevealAnim = new QVariantAnimation(this);
    connect(item.closeRevealAnim, &QVariantAnimation::valueChanged, this,
        [this, itemId](const QVariant& v) {
            int i = m_indexById.value(itemId, -1);
            if (i >= 0) {
                m_items[i].closeRevealProgress = v.toReal();
                // Same as hoverAnim: repaint()+update() mix caused double paints / flicker
                update();
            }
        });

    bindTabDisplayTitleSignals(tab);
    return item;
}

void CustomTabBar::destroyItemAnimations(TabItem& item)
{
    if (item.hoverAnim) {
        item.hoverAnim->stop();
        item.hoverAnim->deleteLater();
        item.hoverAnim = nullptr;
    }
    if (item.fadeAnim) {
        item.fadeAnim->stop();
        item.fadeAnim->deleteLater();
        item.fadeAnim = nullptr;
    }
    if (item.closeRevealAnim) {
        item.closeRevealAnim->stop();
        item.closeRevealAnim->deleteLater();
        item.closeRevealAnim = nullptr;
    }
}

void CustomTabBar::reindexItems()
{
    m_indexById.clear();
    for (int i = 0; i < m_items.size(); ++i) {
        m_indexById.insert(m_items[i].id, i);
    }
}

QList<QUuid> CustomTabBar::smartObjectTabsForParent(const QUuid& parentTabId) const
{
    QList<QUuid> result;
    if (!m_tabManager || parentTabId.isNull()) {
        return result;
    }
    for (ruwa::core::BaseTab* tab : m_tabManager->tabs()) {
        auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab);
        // Keyed on the DOCUMENT at the top of the chain, not on the immediate
        // parent: a smart object nested inside another one has no strip item to
        // hang off, so every level shares the document's one breadcrumb slot.
        if (wsTab && wsTab->isSmartContentEditor()
            && wsTab->smartEditDocumentTabId() == parentTabId) {
            result.append(wsTab->id());
        }
    }
    return result;
}

QUuid CustomTabBar::shownSmartObjectTabForParent(const QUuid& parentTabId)
{
    const QList<QUuid> open = smartObjectTabsForParent(parentTabId);
    if (open.isEmpty()) {
        m_shownSmartByParent.remove(parentTabId);
        return QUuid();
    }

    const QUuid stored = m_shownSmartByParent.value(parentTabId);
    // The stored pick can point at a contents tab that was closed meanwhile.
    const QUuid resolved = open.contains(stored) ? stored : open.constFirst();
    m_shownSmartByParent.insert(parentTabId, resolved);
    return resolved;
}

int CustomTabBar::itemIndexOfSmartChild(const QUuid& parentTabId) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].isSmartObject && m_items[i].parentTabId == parentTabId) {
            return i;
        }
    }
    return -1;
}

void CustomTabBar::syncSmartSlotForParent(const QUuid& parentTabId, bool animated)
{
    if (!m_tabManager || parentTabId.isNull()) {
        return;
    }
    if (m_indexById.value(parentTabId, -1) < 0) {
        // The document itself is gone — its breadcrumb has nothing left to hang
        // off, so it goes too rather than being left dangling in the strip. (A
        // document takes its contents tabs with it, so their own items are
        // normally already fading; this is the case where one is not.)
        const int orphanIndex = itemIndexOfSmartChild(parentTabId);
        if (orphanIndex >= 0 && !m_items[orphanIndex].isClosing) {
            destroyItemAnimations(m_items[orphanIndex]);
            m_items.removeAt(orphanIndex);
            if (m_hoveredIndex == orphanIndex) {
                m_hoveredIndex = -1;
            } else if (m_hoveredIndex > orphanIndex) {
                --m_hoveredIndex;
            }
            reindexItems();
            updateLayout();
            refreshStripAlignment(m_initialAlignDone);
            update();
        }
        m_shownSmartByParent.remove(parentTabId);
        return;
    }

    const int existingIndex = itemIndexOfSmartChild(parentTabId);
    if (existingIndex >= 0 && m_items[existingIndex].isClosing) {
        // Let the fade-out own the slot; it re-syncs when it finishes.
        return;
    }

    const QUuid desiredId = shownSmartObjectTabForParent(parentTabId);
    const QUuid existingId = existingIndex >= 0 ? m_items[existingIndex].id : QUuid();
    if (existingId == desiredId) {
        return;
    }

    if (existingIndex >= 0) {
        // Swapped out, not closed: the tab stays open behind the slot, so it must
        // disappear at once rather than play a close animation.
        destroyItemAnimations(m_items[existingIndex]);
        m_items.removeAt(existingIndex);
        if (m_hoveredIndex == existingIndex) {
            m_hoveredIndex = -1;
        } else if (m_hoveredIndex > existingIndex) {
            --m_hoveredIndex;
        }
        reindexItems();
    }

    int insertedIndex = -1;
    if (auto* desiredTab = m_tabManager->tab(desiredId)) {
        const int parentIndex = m_indexById.value(parentTabId, -1);
        if (parentIndex >= 0) {
            insertedIndex = parentIndex + 1;
            m_items.insert(insertedIndex, makeItem(desiredTab));
            reindexItems();
        }
    }

    updateLayout();
    refreshStripAlignment(m_initialAlignDone);
    if (insertedIndex >= 0 && animated) {
        startFadeInAnimation(insertedIndex);
    }
    update();
}

void CustomTabBar::rebuildFromManager()
{
    if (m_layoutSlideAnim) {
        m_layoutSlideAnim->stop();
    }
    m_layoutSlideStartById.clear();

    // Clear existing
    for (auto& item : m_items) {
        delete item.hoverAnim;
        delete item.fadeAnim;
        delete item.closeRevealAnim;
    }
    m_items.clear();
    m_indexById.clear();
    m_smartParentByTab.clear();
    m_hoveredIndex = -1;

    if (!m_tabManager)
        return;

    // Smart object tabs never get a strip slot of their own: they are drawn as a
    // breadcrumb child of their document, and all of one document's contents
    // share that single slot.
    for (ruwa::core::BaseTab* tab : m_tabManager->tabs()) {
        if (isSmartObjectTab(tab)) {
            auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab);
            m_smartParentByTab.insert(tab->id(), wsTab->smartEditDocumentTabId());
            bindTabDisplayTitleSignals(tab);
            continue;
        }

        m_items.append(makeItem(tab));
        m_indexById.insert(tab->id(), m_items.size() - 1);

        const QUuid childId = shownSmartObjectTabForParent(tab->id());
        if (auto* childTab = m_tabManager->tab(childId)) {
            m_items.append(makeItem(childTab));
            m_indexById.insert(childId, m_items.size() - 1);
        }
    }

    // Track active
    if (auto* active = m_tabManager->activeTab()) {
        m_activeId = active->id();
    } else {
        m_activeId = QUuid();
    }

    updateLayout();
    refreshStripAlignment(m_initialAlignDone);
}

void CustomTabBar::onTabAdded(ruwa::core::BaseTab* tab)
{
    if (!tab)
        return;

    if (isSmartObjectTab(tab)) {
        auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab);
        const QUuid parentTabId = wsTab->smartEditDocumentTabId();
        m_smartParentByTab.insert(tab->id(), parentTabId);
        bindTabDisplayTitleSignals(tab);
        // A newly opened smart object takes over its document's slot; whatever was
        // there stays open, just not drawn.
        m_shownSmartByParent.insert(parentTabId, tab->id());
        syncSmartSlotForParent(parentTabId, true);
        return;
    }

    TabItem item = makeItem(tab);
    item.opacity = 0.0; // Start invisible for appear animation
    item.verticalOffset = tabPopDistancePx();

    // Add to list
    int idx = m_items.size();
    m_indexById.insert(item.id, idx);
    m_items.append(item);

    updateLayout();
    refreshStripAlignment(m_initialAlignDone);

    // Start fade-in animation
    startFadeInAnimation(idx);
}

void CustomTabBar::onTabReplaced(ruwa::core::BaseTab* oldTab, ruwa::core::BaseTab* newTab)
{
    if (!oldTab || !newTab) {
        return;
    }

    const QUuid tabId = oldTab->id();
    const int idx = m_indexById.value(tabId, -1);
    if (idx < 0 || idx >= m_items.size()) {
        return;
    }

    TabItem& item = m_items[idx];
    item.title = newTab->title();
    item.icon = newTab->icon();
    item.iconAlias = defaultIconForTabType(newTab->type());

    if (auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(newTab)) {
        if (!wsTab->tabIconAlias().isEmpty()) {
            item.iconAlias = wsTab->tabIconAlias();
        }
    }
    if (item.icon.isNull()) {
        item.icon = ruwa::ui::core::IconProvider::instance().getIcon(item.iconAlias);
    }

    bindTabDisplayTitleSignals(newTab);
    updateLayout();
    refreshStripAlignment(m_initialAlignDone);
    update();
}

void CustomTabBar::onTabClosing(ruwa::core::BaseTab* tab, int direction)
{
    Q_UNUSED(direction);

    if (!tab)
        return;

    if (m_dragActive) {
        finishTabDrag(false, QCursor::pos());
    }

    QUuid tabId = tab->id();

    if (!m_indexById.contains(tabId)) {
        // A smart object sharing a slot with another one has no item to fade, so
        // nobody would ever confirm its close and the tab would hang in the
        // manager's closing state. Confirm it once this signal has unwound.
        if (isSmartObjectTab(tab)) {
            QTimer::singleShot(0, this, [this, tabId]() {
                if (m_tabManager) {
                    m_tabManager->confirmTabClosed(tabId);
                }
            });
        }
        return;
    }

    int index = m_indexById.value(tabId);
    if (index < 0 || index >= m_items.size()) {
        return;
    }

    if (m_items[index].isClosing) {
        return; // Already fading out, avoid double-processing
    }

    const bool contentOwnsCloseConfirmation = m_tabManager && m_tabManager->activeTab() == tab;
    m_items[index].contentOwnsCloseConfirmation = contentOwnsCloseConfirmation;
    // Start fade-out animation instead of immediate removal
    startFadeOutAnimation(index);
}

void CustomTabBar::onTabRemoved(const QUuid& tabId)
{
    // Regular tabs were already taken out of the strip in onTabClosing. A smart
    // object is the exception: the slot it occupied may have to fall back to
    // another one still open for the same document. Its tab object is gone by
    // now, hence the cached parent.
    const QUuid parentTabId = m_smartParentByTab.take(tabId);
    if (parentTabId.isNull()) {
        // Not a smart object — but it may have been a document that had one, and
        // that breadcrumb must not outlive it. A sync rather than a bare erase:
        // it is the one place that knows whether the slot is mid-fade (leave it)
        // or still drawn for a document that is now gone (take it down).
        m_shownSmartByParent.remove(tabId);
        syncSmartSlotForParent(tabId, true);
        return;
    }
    if (m_shownSmartByParent.value(parentTabId) == tabId) {
        m_shownSmartByParent.remove(parentTabId);
    }
    syncSmartSlotForParent(parentTabId, true);
}

void CustomTabBar::onActiveTabChanged(ruwa::core::BaseTab* newTab, ruwa::core::BaseTab* oldTab)
{
    Q_UNUSED(oldTab);
    m_activeId = newTab ? newTab->id() : QUuid();

    // Keyboard navigation (and the auto-activation after a close) can land on a
    // smart object that is not the one drawn — the strip follows the focus.
    if (auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(newTab);
        wsTab && wsTab->isSmartContentEditor()) {
        const QUuid parentTabId = wsTab->smartEditDocumentTabId();
        if (!parentTabId.isNull() && m_shownSmartByParent.value(parentTabId) != newTab->id()) {
            m_shownSmartByParent.insert(parentTabId, newTab->id());
            syncSmartSlotForParent(parentTabId, true);
        }
    }

    update();
}

void CustomTabBar::onTabOrderChanged()
{
    // During our own settle the visual list already has this exact order. Rebuilding
    // would replace the animated items under the ghost and create a visible snap.
    if (!m_dragActive && !m_dragSettling) {
        rebuildFromManager();
    }
}

void CustomTabBar::updateLayout()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    const int ICON_SIZE = theme.scaled(16);
    const int ICON_MARGIN = theme.scaled(8);
    const int CLOSE_SIZE = theme.scaled(14);
    const int CLOSE_MARGIN = theme.scaled(8);
    const int SEP_MARGIN = theme.scaled(12);
    const int TAB_PADDING = theme.scaled(12);
    const int TEXT_PADDING = theme.scaled(4); // Extra buffer for different font metrics

    qreal x = TAB_PADDING + m_stripAlignOffset;
    const QFont tabFont = theme.font(ruwa::ui::core::ThemeFontRole::Body);
    const QFontMetrics fm(tabFont);

    for (int i = 0; i < m_items.size(); ++i) {
        TabItem& item = m_items[i];

        qreal w = item.isSmartObject ? ICON_MARGIN : (ICON_MARGIN + ICON_SIZE + ICON_MARGIN);
        w += fm.horizontalAdvance(item.title) + TEXT_PADDING;
        w += CLOSE_MARGIN + CLOSE_SIZE;

        item.rect = QRectF(x, 0, w, height());

        qreal closeX = x + w - CLOSE_SIZE - 4;
        qreal closeY = (height() - CLOSE_SIZE) / 2.0;
        item.closeRect = QRectF(closeX, closeY, CLOSE_SIZE, CLOSE_SIZE);

        x += w;

        if (i < m_items.size() - 1) {
            x += SEP_MARGIN * 2;
        }
    }

    setMinimumWidth(0);
    update();
}

void CustomTabBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    int SEP_MARGIN = theme.scaled(12);

    // No background fill - transparent to show TopBar's border

    for (int i = 0; i < m_items.size(); ++i) {
        const TabItem& item = m_items[i];
        bool isActive = (item.id == m_activeId);
        bool isHovered = (i == m_hoveredIndex);

        drawTab(p, item, isActive, isHovered);

        if (i < m_items.size() - 1) {
            const TabItem& rightTab = m_items[i + 1];
            qreal sepX = item.rect.right() + SEP_MARGIN + item.slideOffsetX + rightTab.enterOffsetX;
            // Slash exits with the left tab when it closes, enters with the right tab when it
            // appears
            const TabItem& sepAnim = item.isClosing ? item : rightTab;
            // A smart object hangs off its document, so that step reads as a
            // breadcrumb arrow rather than a sibling slash.
            const QString glyph
                = rightTab.isSmartObject ? QStringLiteral(">") : QStringLiteral("/");
            const qreal dragOpacity = item.isDragSource && rightTab.isDragSource ? 0.25 : 1.0;
            drawSeparator(p, sepX, height() / 2.0, sepAnim, glyph, dragOpacity);
        }
    }
}

void CustomTabBar::drawTab(QPainter& painter, const TabItem& item, bool isActive, bool isHovered)
{
    painter.save();
    painter.setOpacity(item.opacity * (item.isDragSource ? 0.25 : 1.0));
    painter.translate(item.slideOffsetX + item.enterOffsetX, item.verticalOffset);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    int ICON_SIZE = theme.scaled(16);
    int ICON_MARGIN = theme.scaled(8);
    int CLOSE_SIZE = theme.scaled(14);
    int CLOSE_MARGIN = theme.scaled(8);

    qreal x = item.rect.x();
    qreal progress = item.hoverProgress;

    // Hover: semi-transparent pill behind the full tab row
    if (progress > 0.001) {
        const int vInset = theme.scaled(6);
        const qreal rad = static_cast<qreal>(theme.scaled(6));
        QColor bg = colors.overlayHover();
        constexpr qreal kHoverPillAlphaFactor = 0.42; // softer than raw overlayHover
        bg.setAlpha(
            static_cast<int>(qBound(0.0, progress, 1.0) * bg.alpha() * kHoverPillAlphaFactor));
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        QRectF pill = item.rect.adjusted(0.5, vInset, -0.5, -vInset);
        painter.drawRoundedRect(pill, rad, rad);
    }

    // Text color
    QColor textColor = isActive ? colors.text : colors.textMuted;
    if (!isActive && progress > 0.0) {
        textColor = ruwa::ui::core::ThemeColors::interpolate(
            colors.textMuted, colors.text, progress * 0.5);
    }

    // Icon — a smart object's contents deliberately have none: they are a view of
    // the document next to them, not a document of their own.
    const qreal iconX = x + ICON_MARGIN;
    if (!item.isSmartObject) {
        const qreal iconY = (height() - ICON_SIZE) / 2.0;
        QRectF iconRect(iconX, iconY, ICON_SIZE, ICON_SIZE);

        // Determine icon tint color based on state
        QColor iconTint = isActive ? colors.primary : colors.textMuted;
        if (!isActive && progress > 0.0) {
            iconTint
                = ruwa::ui::core::ThemeColors::interpolate(colors.textMuted, colors.text, progress);
        }

        // Draw the actual icon, tinted to match the theme
        if (!item.icon.isNull()) {
            QPixmap pix = item.icon.pixmap(ICON_SIZE, ICON_SIZE);
            QPixmap tinted = ruwa::ui::painting::tintedPixmap(pix, iconTint);
            painter.drawPixmap(iconRect.toRect(), tinted);
        } else {
            // Fallback: colored rectangle if icon is missing
            painter.setPen(Qt::NoPen);
            painter.setBrush(iconTint);
            painter.drawRoundedRect(iconRect, 2, 2);
        }
    }

    // Title
    qreal textX = item.isSmartObject ? iconX : (iconX + ICON_SIZE + ICON_MARGIN);
    const QFont textFont = theme.font(ruwa::ui::core::ThemeFontRole::Body);
    // No bold on active - avoids text size jump and clipping with different fonts
    painter.setFont(textFont);
    painter.setPen(textColor);

    QRectF textRect(
        textX, 0, item.rect.width() - (textX - x) - CLOSE_MARGIN - CLOSE_SIZE, height());
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, item.title);

    // Close (×): alpha on pen only (avoids nested setOpacity vs tab opacity fights / flicker)
    if (item.closeRevealProgress > 0.001) {
        const qreal hotMix
            = item.closeHovered ? qBound(0.0, (item.closeRevealProgress - 0.12) / 0.45, 1.0) : 0.0;
        QColor closeColor
            = ruwa::ui::core::ThemeColors::interpolate(colors.textMuted, colors.text, hotMix);
        closeColor.setAlphaF(closeColor.alphaF() * item.closeRevealProgress);
        painter.setPen(QPen(closeColor, 1.5, Qt::SolidLine, Qt::RoundCap));

        const qreal cx = item.closeRect.center().x();
        const qreal cy = item.closeRect.center().y();
        const qreal sz = theme.scaled(6);

        painter.drawLine(QPointF(cx - sz / 2, cy - sz / 2), QPointF(cx + sz / 2, cy + sz / 2));
        painter.drawLine(QPointF(cx + sz / 2, cy - sz / 2), QPointF(cx - sz / 2, cy + sz / 2));
    }

    painter.restore();
}

void CustomTabBar::drawSeparator(QPainter& painter, qreal x, qreal y, const TabItem& anim,
    const QString& glyph, qreal opacityFactor)
{
    painter.save();
    painter.setOpacity(anim.opacity * opacityFactor);
    painter.translate(0, anim.verticalOffset);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    painter.setFont(theme.font(ruwa::ui::core::ThemeFontRole::Body));
    painter.setPen(colors.textMuted);
    painter.drawText(QPointF(x - 6, y + 4), glyph);
    painter.restore();
}

QUuid CustomTabBar::rootTabIdForItem(int index) const
{
    if (index < 0 || index >= m_items.size()) {
        return {};
    }
    const TabItem& item = m_items[index];
    return item.isSmartObject ? item.parentTabId : item.id;
}

QList<QUuid> CustomTabBar::visibleRootOrder() const
{
    QList<QUuid> order;
    order.reserve(m_items.size());
    for (const TabItem& item : m_items) {
        if (!item.isSmartObject) {
            order.append(item.id);
        }
    }
    return order;
}

QList<QUuid> CustomTabBar::managerOrderForVisibleRoots() const
{
    QList<QUuid> order;
    if (!m_tabManager) {
        return order;
    }

    QSet<QUuid> appended;
    for (const QUuid& rootId : visibleRootOrder()) {
        if (m_tabManager->hasTab(rootId)) {
            order.append(rootId);
            appended.insert(rootId);
        }
        // Every open contents editor belonging to the document moves with it,
        // including nested/hidden editors that share the one breadcrumb slot.
        for (const QUuid& smartId : smartObjectTabsForParent(rootId)) {
            if (!appended.contains(smartId)) {
                order.append(smartId);
                appended.insert(smartId);
            }
        }
    }

    // Preserve any future non-visual tab kind until it gets an explicit grouping
    // rule instead of silently dropping it from the manager order.
    for (ruwa::core::BaseTab* tab : m_tabManager->tabs()) {
        if (tab && !appended.contains(tab->id())) {
            order.append(tab->id());
            appended.insert(tab->id());
        }
    }
    return order;
}

QRectF CustomTabBar::visualGroupBounds(const QUuid& rootTabId, bool includeVisualOffsets) const
{
    QRectF bounds;
    for (const TabItem& item : m_items) {
        if (item.id != rootTabId && !(item.isSmartObject && item.parentTabId == rootTabId)) {
            continue;
        }

        QRectF itemRect = item.rect;
        if (includeVisualOffsets) {
            itemRect.translate(item.slideOffsetX + item.enterOffsetX, item.verticalOffset);
        }
        bounds = bounds.isNull() ? itemRect : bounds.united(itemRect);
    }
    return bounds;
}

int CustomTabBar::tabInsertIndexAt(const QPoint& localPos) const
{
    struct Placement {
        int stationaryIndex = 0;
        QRectF rect;
    };

    QList<Placement> placements;
    int stationaryIndex = 0;
    for (const QUuid& rootId : visibleRootOrder()) {
        if (rootId == m_draggedRootId) {
            continue;
        }
        const QRectF targetRect = visualGroupBounds(rootId, false);
        if (targetRect.isValid()) {
            placements.append({ stationaryIndex, targetRect });
        }
        ++stationaryIndex;
    }

    if (placements.isEmpty()) {
        return 0;
    }
    for (const Placement& placement : placements) {
        if (localPos.x() < placement.rect.center().x()) {
            return placement.stationaryIndex;
        }
    }
    return placements.constLast().stationaryIndex + 1;
}

void CustomTabBar::applyVisibleRootOrder(const QList<QUuid>& rootOrder, bool animated)
{
    const QList<QUuid> currentOrder = visibleRootOrder();
    if (rootOrder == currentOrder || rootOrder.size() != currentOrder.size()) {
        return;
    }

    QSet<QUuid> expected;
    QSet<QUuid> requested;
    for (const QUuid& rootId : currentOrder) {
        expected.insert(rootId);
    }
    for (const QUuid& rootId : rootOrder) {
        requested.insert(rootId);
    }
    if (requested != expected) {
        return;
    }

    QHash<QUuid, qreal> visualLeftBefore;
    for (const TabItem& item : m_items) {
        visualLeftBefore.insert(item.id, item.rect.x() + item.slideOffsetX + item.enterOffsetX);
    }

    if (m_layoutSlideAnim) {
        m_layoutSlideAnim->stop();
    }

    QList<TabItem> reordered;
    reordered.reserve(m_items.size());
    for (const QUuid& rootId : rootOrder) {
        const int rootIndex = m_indexById.value(rootId, -1);
        if (rootIndex < 0 || rootIndex >= m_items.size() || m_items[rootIndex].isSmartObject) {
            return;
        }
        reordered.append(m_items[rootIndex]);
        for (const TabItem& item : m_items) {
            if (item.isSmartObject && item.parentTabId == rootId) {
                reordered.append(item);
            }
        }
    }

    if (reordered.size() != m_items.size()) {
        return;
    }

    m_items = reordered;
    reindexItems();
    for (TabItem& item : m_items) {
        item.slideOffsetX = 0.0;
    }
    updateLayout();

    m_layoutSlideStartById.clear();
    bool anyShift = false;
    if (animated) {
        for (TabItem& item : m_items) {
            const qreal delta = visualLeftBefore.value(item.id, item.rect.x()) - item.rect.x()
                - item.enterOffsetX;
            item.slideOffsetX = delta;
            m_layoutSlideStartById.insert(item.id, delta);
            anyShift = anyShift || !qFuzzyIsNull(delta);
        }
    }

    m_hoveredIndex = -1;
    if (animated && anyShift && m_layoutSlideAnim) {
        m_layoutSlideAnim->setStartValue(1.0);
        m_layoutSlideAnim->setEndValue(0.0);
        m_layoutSlideAnim->setDuration(240);
        m_layoutSlideAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_layoutSlideAnim->start();
    } else {
        for (TabItem& item : m_items) {
            item.slideOffsetX = 0.0;
        }
        m_layoutSlideStartById.clear();
        update();
    }
}

void CustomTabBar::moveDraggedGroupTo(int insertIndex)
{
    QList<QUuid> reordered = visibleRootOrder();
    if (!reordered.removeOne(m_draggedRootId)) {
        return;
    }
    reordered.insert(qBound(0, insertIndex, static_cast<int>(reordered.size())), m_draggedRootId);
    applyVisibleRootOrder(reordered, true);
}

void CustomTabBar::startTabDrag(const QUuid& rootTabId, const QPoint& globalPos)
{
    if (rootTabId.isNull() || m_dragActive || m_dragSettling || visibleRootOrder().size() < 2) {
        cancelTabDragCandidate();
        return;
    }

    for (const TabItem& item : m_items) {
        // A closing tab is already absent from TabManager's order while it is
        // still fading in this view, so no complete reorder can be committed yet.
        if (item.isClosing) {
            cancelTabDragCandidate();
            return;
        }
    }

    const QRect snapshotRect
        = visualGroupBounds(rootTabId, true).toAlignedRect().intersected(rect());
    if (!snapshotRect.isValid()) {
        cancelTabDragCandidate();
        return;
    }
    const QPixmap snapshot = grab(snapshotRect);
    if (snapshot.isNull()) {
        cancelTabDragCandidate();
        return;
    }

    m_dragActive = true;
    m_draggedRootId = rootTabId;
    m_dragStartRootOrder = visibleRootOrder();
    m_dragOffset = mapToGlobal(snapshotRect.topLeft()) - globalPos;
    cancelTabDragCandidate();

    for (TabItem& item : m_items) {
        item.isDragSource
            = item.id == rootTabId || (item.isSmartObject && item.parentTabId == rootTabId);
        if (item.isDragSource) {
            item.closeHovered = false;
        }
    }

    QWidget* topLevel = window();
    m_dragGhost = new ruwa::ui::widgets::DragGhostWidget(topLevel);
    m_dragGhost->setSnapshot(snapshot);
    const QPoint sourcePosition = topLevel->mapFromGlobal(mapToGlobal(snapshotRect.topLeft()))
        - m_dragGhost->contentTopLeft();
    m_dragGhost->startFollowing(sourcePosition);
    m_dragGhost->captureBackdrop(topLevel);
    m_dragGhost->show();
    m_dragGhost->raise();
    m_dragGhost->setFollowTarget(ghostTargetPosition(globalPos));

    qApp->installEventFilter(this);
    if (!m_dragCursorOverride) {
        QApplication::setOverrideCursor(Qt::ClosedHandCursor);
        m_dragCursorOverride = true;
    }
    update();
}

void CustomTabBar::updateTabDrag(const QPoint& globalPos)
{
    if (!m_dragActive || !m_dragGhost) {
        return;
    }
    m_dragGhost->setFollowTarget(ghostTargetPosition(globalPos));
    const QPoint localPos = mapFromGlobal(globalPos);
    const QPoint clampedPos(qBound(0, localPos.x(), qMax(0, width() - 1)), height() / 2);
    moveDraggedGroupTo(tabInsertIndexAt(clampedPos));
}

void CustomTabBar::finishTabDrag(bool accepted, const QPoint& globalPos)
{
    if (!m_dragActive) {
        return;
    }
    updateTabDrag(globalPos);

    qApp->removeEventFilter(this);
    if (m_dragCursorOverride) {
        QApplication::restoreOverrideCursor();
        m_dragCursorOverride = false;
    }
    m_dragActive = false;
    m_dragSettling = true;

    if (!accepted) {
        applyVisibleRootOrder(m_dragStartRootOrder, true);
    } else if (!m_tabManager || !m_tabManager->reorderTabs(managerOrderForVisibleRoots())) {
        accepted = false;
        applyVisibleRootOrder(m_dragStartRootOrder, true);
    }

    const QRectF targetBounds = visualGroupBounds(m_draggedRootId, false);
    QPoint targetPosition;
    if (m_dragGhost && targetBounds.isValid()) {
        targetPosition = window()->mapFromGlobal(mapToGlobal(targetBounds.topLeft().toPoint()))
            - m_dragGhost->contentTopLeft();
    }

    QPointer<CustomTabBar> guard(this);
    auto finish = [guard]() {
        if (!guard) {
            return;
        }
        for (TabItem& item : guard->m_items) {
            item.isDragSource = false;
        }
        if (guard->m_dragGhost) {
            guard->m_dragGhost->deleteLater();
            guard->m_dragGhost = nullptr;
        }
        guard->m_draggedRootId = QUuid();
        guard->m_dragStartRootOrder.clear();
        guard->m_dragSettling = false;
        guard->cancelTabDragCandidate();
        guard->update();
    };

    if (!m_dragGhost || !targetBounds.isValid()) {
        finish();
        return;
    }
    m_dragGhost->animateTo(targetPosition,
        accepted ? ruwa::ui::widgets::DragGhostWidget::Transition::Settle
                 : ruwa::ui::widgets::DragGhostWidget::Transition::Return,
        std::move(finish));
}

void CustomTabBar::cancelTabDragCandidate()
{
    m_dragCandidateRootId = QUuid();
    m_dragPressGlobalPosition = {};
}

void CustomTabBar::resetTabDragState()
{
    if (m_dragActive) {
        qApp->removeEventFilter(this);
    }
    if (m_dragCursorOverride) {
        QApplication::restoreOverrideCursor();
        m_dragCursorOverride = false;
    }
    if (m_dragGhost) {
        delete m_dragGhost.data();
        m_dragGhost = nullptr;
    }
    for (TabItem& item : m_items) {
        item.isDragSource = false;
    }
    m_dragActive = false;
    m_dragSettling = false;
    m_draggedRootId = QUuid();
    m_dragStartRootOrder.clear();
    cancelTabDragCandidate();
}

QPoint CustomTabBar::ghostTargetPosition(const QPoint& globalPos) const
{
    if (!m_dragGhost) {
        return {};
    }
    return window()->mapFromGlobal(globalPos + m_dragOffset) - m_dragGhost->contentTopLeft();
}

void CustomTabBar::mousePressEvent(QMouseEvent* event)
{
    // Let event filter handle right-click for context menu
    if (event->button() == Qt::RightButton) {
        // Don't consume the event - let it propagate to event filter
        QWidget::mousePressEvent(event);
        return;
    }

    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    int idx = tabIndexAt(event->pos());

    // If click is not on any tab, start window drag
    if (idx < 0 || idx >= m_items.size()) {
        // Start system window move (Qt 5.15+ / Qt 6)
        if (QWidget* topLevel = window()) {
            if (auto* winHandle = topLevel->windowHandle()) {
                winHandle->startSystemMove();
            }
        }
        return;
    }

    if (!m_tabManager) {
        QWidget::mousePressEvent(event);
        return;
    }

    const TabItem& item = m_items[idx];

    if (isCloseButtonAt(idx, event->pos())) {
        cancelTabDragCandidate();
        ruwa::core::BaseTab* tab = m_tabManager->tab(item.id);
        auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab);
        QWidget* context = window();
        if (wsTab && context && !ruwa::ui::widgets::prepareWorkspaceTabForClose(wsTab, context)) {
            return; // User cancelled
        }
        m_tabManager->requestCloseTab(item.id);
    } else {
        m_dragCandidateRootId = rootTabIdForItem(idx);
        m_dragPressGlobalPosition = event->globalPosition().toPoint();
        m_tabManager->activateTab(item.id);
    }
}

void CustomTabBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        cancelTabDragCandidate();
    }
    QWidget::mouseReleaseEvent(event);
}

void CustomTabBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    int idx = tabIndexAt(event->pos());

    // Double-click on empty area toggles maximize/restore
    if (idx < 0 || idx >= m_items.size()) {
        if (QWidget* topLevel = window()) {
            if (topLevel->isMaximized()) {
                topLevel->showNormal();
            } else {
                topLevel->showMaximized();
            }
        }
        return;
    }

    // Double-click on tab - could be used for rename, etc.
    QWidget::mouseDoubleClickEvent(event);
}

void CustomTabBar::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragCandidateRootId.isNull() && !m_dragActive && (event->buttons() & Qt::LeftButton)
        && (event->globalPosition().toPoint() - m_dragPressGlobalPosition).manhattanLength()
            >= QApplication::startDragDistance()) {
        startTabDrag(m_dragCandidateRootId, event->globalPosition().toPoint());
        return;
    }

    int oldHovered = m_hoveredIndex;
    m_hoveredIndex = tabIndexAt(event->pos());

    // Update cursor based on position
    if (m_hoveredIndex >= 0) {
        setCursor(Qt::PointingHandCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }

    // Close hover: geometry only — do not tie to closeReveal threshold (that caused a snap at
    // ~0.12)
    bool needPaint = false;
    for (int i = 0; i < m_items.size(); ++i) {
        const TabItem& hi = m_items[i];
        const bool overClose = (i == m_hoveredIndex)
            && hi.closeRect.translated(hi.slideOffsetX + hi.enterOffsetX, hi.verticalOffset)
                   .contains(event->pos());
        if (m_items[i].closeHovered != overClose) {
            m_items[i].closeHovered = overClose;
            needPaint = true;
        }
    }

    // Handle hover animations + close reveal (matches BaseAnimatedButton::setHovered pattern)
    if (oldHovered != m_hoveredIndex) {
        if (oldHovered >= 0 && oldHovered < m_items.size()) {
            startHoverAnimation(oldHovered, false);
            startCloseRevealAnimation(oldHovered, false);
        }
        if (m_hoveredIndex >= 0 && m_hoveredIndex < m_items.size()) {
            startHoverAnimation(m_hoveredIndex, true);
            startCloseRevealAnimation(m_hoveredIndex, true);
        }
        needPaint = true;
    }

    if (needPaint) {
        update();
    }

    QWidget::mouseMoveEvent(event);
}

bool CustomTabBar::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);
    if (m_dragActive && m_dragGhost) {
        switch (event->type()) {
        case QEvent::MouseMove: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            updateTabDrag(mouseEvent->globalPosition().toPoint());
            return true;
        }
        case QEvent::MouseButtonRelease: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                finishTabDrag(true, mouseEvent->globalPosition().toPoint());
                return true;
            }
            break;
        }
        case QEvent::KeyPress: {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                finishTabDrag(false, QCursor::pos());
                return true;
            }
            break;
        }
        case QEvent::ApplicationDeactivate:
            finishTabDrag(false, QCursor::pos());
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CustomTabBar::leaveEvent(QEvent* event)
{
    const int wasHovered = m_hoveredIndex;
    for (int i = 0; i < m_items.size(); ++i) {
        m_items[i].closeHovered = false;
    }
    m_hoveredIndex = -1;
    // Only the tab that was hovered may have hoverProgress > 0 — animating all tabs backward
    // restarts every QVariantAnimation and causes a brief false “hover” flash on everyone.
    if (wasHovered >= 0 && wasHovered < m_items.size()) {
        startHoverAnimation(wasHovered, false);
        startCloseRevealAnimation(wasHovered, false);
    }
    unsetCursor();
    update();
    QWidget::leaveEvent(event);
}

int CustomTabBar::tabIndexAt(const QPointF& pos) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        const TabItem& it = m_items[i];
        if (it.rect.translated(it.slideOffsetX + it.enterOffsetX, it.verticalOffset)
                .contains(pos)) {
            return i;
        }
    }
    return -1;
}

bool CustomTabBar::isCloseButtonAt(int index, const QPointF& pos) const
{
    if (index < 0 || index >= m_items.size())
        return false;
    const TabItem& it = m_items[index];
    if (it.closeRevealProgress < 0.18)
        return false;
    return it.closeRect.translated(it.slideOffsetX + it.enterOffsetX, it.verticalOffset)
        .contains(pos);
}

void CustomTabBar::startHoverAnimation(int index, bool hovering)
{
    if (index < 0 || index >= m_items.size())
        return;

    if (m_items[index].isClosing)
        return;

    auto* anim = m_items[index].hoverAnim;
    if (!anim)
        return;

    anim->stop();
    anim->setDirection(hovering ? QAbstractAnimation::Forward : QAbstractAnimation::Backward);
    anim->start();
}

void CustomTabBar::startCloseRevealAnimation(int index, bool reveal)
{
    if (index < 0 || index >= m_items.size())
        return;

    TabItem& item = m_items[index];
    auto* anim = item.closeRevealAnim;
    if (!anim)
        return;

    if (reveal && qFuzzyCompare(item.closeRevealProgress, 1.0))
        return;
    if (!reveal && qFuzzyIsNull(item.closeRevealProgress))
        return;

    anim->stop();
    anim->setStartValue(item.closeRevealProgress);
    anim->setEndValue(reveal ? 1.0 : 0.0);
    anim->setDuration(reveal ? 170 : 150);
    anim->setEasingCurve(reveal ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    anim->start();
}

void CustomTabBar::startFadeInAnimation(int index)
{
    if (index < 0 || index >= m_items.size())
        return;

    TabItem& item = m_items[index];
    auto* anim = item.fadeAnim;
    if (!anim)
        return;

    anim->stop();
    item.isClosing = false;
    const qreal dist = tabPopDistancePx();
    item.opacity = 0.0;
    item.verticalOffset = dist;
    item.enterSlideDistance = ruwa::ui::core::ThemeManager::instance().scaled(22);
    item.enterOffsetX = item.enterSlideDistance;

    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setDuration(340);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start();
}

void CustomTabBar::applyTabVisibilityAnimFrame(const QUuid& itemId, qreal raw)
{
    int i = m_indexById.value(itemId, -1);
    if (i < 0)
        return;

    TabItem& item = m_items[i];
    const qreal dist = tabPopDistancePx();

    if (item.isClosing) {
        const qreal p = raw;
        item.opacity = item.fadeOutStartOpacity * (1.0 - p);
        item.verticalOffset = item.fadeOutStartOffset + p * (dist - item.fadeOutStartOffset);
    } else {
        const qreal t = raw;
        item.opacity = qBound(0.0, t, 1.0);
        item.verticalOffset = (1.0 - t) * dist;
        item.enterOffsetX = (1.0 - t) * item.enterSlideDistance;
    }
    update();
}

void CustomTabBar::runPostRemoveLayoutSlide(const QHash<QUuid, qreal>& visualLeftBeforeRemove)
{
    if (m_layoutSlideAnim) {
        m_layoutSlideAnim->stop();
    }

    for (auto& it : m_items) {
        it.slideOffsetX = 0;
    }

    updateLayout();

    m_layoutSlideStartById.clear();
    bool anyShift = false;
    for (auto& it : m_items) {
        const qreal vx = visualLeftBeforeRemove.value(it.id);
        const qreal delta = vx - it.rect.x();
        m_layoutSlideStartById.insert(it.id, delta);
        it.slideOffsetX = delta;
        if (!qFuzzyIsNull(delta)) {
            anyShift = true;
        }
    }

    if (!anyShift) {
        m_layoutSlideStartById.clear();
        for (auto& it : m_items) {
            it.slideOffsetX = 0;
        }
        update();
        // Content width changed (e.g. 2 → 1 tab) but remaining tab's x already matches layout — no
        // slide animation runs, so finished() never fires; still need to re-apply centered strip
        // offset.
        refreshStripAlignment(m_initialAlignDone);
        return;
    }

    m_layoutSlideAnim->setStartValue(1.0);
    m_layoutSlideAnim->setEndValue(0.0);
    m_layoutSlideAnim->setDuration(240);
    m_layoutSlideAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_layoutSlideAnim->start();
}

void CustomTabBar::startFadeOutAnimation(int index)
{
    if (index < 0 || index >= m_items.size())
        return;

    TabItem& item = m_items[index];
    auto* anim = item.fadeAnim;
    if (!anim)
        return;

    item.isClosing = true;
    item.fadeOutStartOpacity = item.opacity;
    item.fadeOutStartOffset = item.verticalOffset;

    // Disconnect any previous finished handler (e.g. from restart)
    disconnect(anim, &QVariantAnimation::finished, this, nullptr);

    anim->stop();
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setDuration(240);
    anim->setEasingCurve(QEasingCurve::InCubic);

    // Use tabId for lookup - index may change when other tabs are removed
    QUuid tabId = item.id;
    connect(
        anim, &QVariantAnimation::finished, this,
        [this, tabId]() {
            int idx = m_indexById.value(tabId, -1);
            if (idx < 0)
                return; // Already removed or invalid

            if (idx >= m_items.size() || m_items[idx].id != tabId)
                return;

            QHash<QUuid, qreal> visualLeft;
            for (const TabItem& tab : m_items) {
                visualLeft.insert(tab.id, tab.rect.x() + tab.slideOffsetX);
            }

            // Clean up animations
            TabItem& it = m_items[idx];
            const bool contentOwnsCloseConfirmation = it.contentOwnsCloseConfirmation;
            const QUuid smartParentTabId = it.isSmartObject ? it.parentTabId : QUuid();
            if (it.hoverAnim) {
                it.hoverAnim->deleteLater();
                it.hoverAnim = nullptr;
            }
            if (it.fadeAnim) {
                it.fadeAnim->deleteLater();
                it.fadeAnim = nullptr;
            }
            if (it.closeRevealAnim) {
                it.closeRevealAnim->deleteLater();
                it.closeRevealAnim = nullptr;
            }

            m_items.removeAt(idx);

            // Rebuild index map
            m_indexById.clear();
            for (int i = 0; i < m_items.size(); ++i) {
                m_indexById.insert(m_items[i].id, i);
            }

            // Update hovered index
            if (m_hoveredIndex == idx) {
                m_hoveredIndex = -1;
            } else if (m_hoveredIndex > idx) {
                m_hoveredIndex--;
            }

            runPostRemoveLayoutSlide(visualLeft);

            if (m_tabManager && !contentOwnsCloseConfirmation) {
                m_tabManager->confirmTabClosed(tabId);
            }

            // The slot is free now: another smart object of the same document may
            // be waiting behind it. (onTabRemoved covers the paths where the tab
            // is confirmed elsewhere; whichever runs first, the other is a no-op.)
            if (!smartParentTabId.isNull()) {
                m_smartParentByTab.remove(tabId);
                if (m_shownSmartByParent.value(smartParentTabId) == tabId) {
                    m_shownSmartByParent.remove(smartParentTabId);
                }
                syncSmartSlotForParent(smartParentTabId, true);
            } else {
                // A document just left the strip: its breadcrumb has lost the item
                // it hangs off and is swept here.
                syncSmartSlotForParent(tabId, false);
            }
        },
        Qt::SingleShotConnection);

    anim->start();
}

void CustomTabBar::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    setFixedHeight(theme.scaled(36));
    updateLayout();
    refreshStripAlignment(m_initialAlignDone);
}

void CustomTabBar::onThemeChanged()
{
    updateScaledSizes();
    update();
}

void CustomTabBar::bindTabDisplayTitleSignals(ruwa::core::BaseTab* tab)
{
    if (!tab) {
        return;
    }
    connect(tab, &ruwa::core::BaseTab::titleChanged, this,
        &CustomTabBar::refreshManagedTabItemTitle, Qt::UniqueConnection);
    connect(tab, &ruwa::core::BaseTab::modifiedChanged, this,
        &CustomTabBar::refreshManagedTabItemTitle, Qt::UniqueConnection);
}

void CustomTabBar::refreshManagedTabItemTitle()
{
    auto* tab = qobject_cast<ruwa::core::BaseTab*>(sender());
    if (!tab || !m_tabManager) {
        return;
    }
    const QUuid tabId = tab->id();
    // Tab is removed from TabManager while closing but the QObject may still emit;
    // do not call virtual title() in that state (can crash during teardown).
    if (!m_tabManager->hasTab(tabId)) {
        return;
    }
    const int idx = m_indexById.value(tabId, -1);
    if (idx < 0 || idx >= m_items.size()) {
        return;
    }
    m_items[idx].title = tab->title();
    updateLayout();
    refreshStripAlignment(m_initialAlignDone);
    update();
}

// =============================================================================
// IContextMenuProvider Implementation
// =============================================================================

ruwa::ui::widgets::ContextMenuType CustomTabBar::contextMenuType() const
{
    // Get current mouse position relative to this widget
    QPoint mousePos = mapFromGlobal(QCursor::pos());
    int tabIndex = tabIndexAt(mousePos);

    // Only show context menu if we have a valid tab under cursor
    if (tabIndex >= 0 && tabIndex < m_items.size()) {
        // A smart object cannot be renamed or re-iconed; its menu switches
        // between the contents open for the same document instead.
        return m_items[tabIndex].isSmartObject ? ruwa::ui::widgets::ContextMenuType::SmartObjectTab
                                               : ruwa::ui::widgets::ContextMenuType::TabBar;
    }

    return ruwa::ui::widgets::ContextMenuType::None;
}

QVariantMap CustomTabBar::contextMenuContext() const
{
    QVariantMap context;

    // Get current mouse position relative to this widget
    QPoint mousePos = mapFromGlobal(QCursor::pos());
    int tabIndex = tabIndexAt(mousePos);

    if (tabIndex >= 0 && tabIndex < m_items.size()) {
        const TabItem& item = m_items[tabIndex];
        context["tabId"] = item.id;
        context["tabTitle"] = item.title;
        context["tabIndex"] = tabIndex;
        context["tabIconAlias"] = item.iconAlias;
        const QRect tabRect
            = item.rect.translated(item.slideOffsetX + item.enterOffsetX, item.verticalOffset)
                  .toAlignedRect();
        context["tabGlobalRect"] = QRect(mapToGlobal(tabRect.topLeft()), tabRect.size());

        if (m_tabManager) {
            if (ruwa::core::BaseTab* tab = m_tabManager->tab(item.id)) {
                context["tabKindLabel"] = tab->tabKindLabel();
                if (auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab)) {
                    context["tabTitle"] = wsTab->baseTitle();
                }
            }
        }

        if (item.isSmartObject && m_tabManager) {
            // Every contents tab of this document, drawn or not — the menu is the
            // only way to reach the ones sharing this slot.
            context["parentTabId"] = item.parentTabId;
            QVariantList ids;
            QStringList titles;
            for (const QUuid& smartTabId : smartObjectTabsForParent(item.parentTabId)) {
                auto* smartTab = m_tabManager->tab(smartTabId);
                if (!smartTab) {
                    continue;
                }
                ids.append(smartTabId);
                auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(smartTab);
                titles.append(wsTab ? wsTab->baseTitle() : smartTab->title());
            }
            context["smartTabIds"] = ids;
            context["smartTabTitles"] = titles;
        }
    }

    return context;
}

// =============================================================================
// Context Menu Handlers
// =============================================================================

void CustomTabBar::onRenameRequested(const QUuid& tabId)
{
    // TODO: Show rename dialog
    // For now, just log
}

void CustomTabBar::onChangeIconRequested(const QUuid& tabId)
{
    // TODO: Show icon picker dialog
    // For now, just log
}

void CustomTabBar::onCloseTabRequested(const QUuid& tabId)
{
    if (!m_tabManager)
        return;

    ruwa::core::BaseTab* tab = m_tabManager->tab(tabId);
    auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab);
    QWidget* context = window();
    if (wsTab && context && !ruwa::ui::widgets::prepareWorkspaceTabForClose(wsTab, context)) {
        return; // User cancelled
    }
    m_tabManager->requestCloseTab(tabId);
}

void CustomTabBar::onCloseOtherTabsRequested(const QUuid& tabId)
{
    if (!m_tabManager)
        return;

    QWidget* context = window();
    if (!context)
        return;

    // From the manager, not from m_items: smart objects sharing a slot are open
    // tabs too, and "close the others" has to mean all of them.
    QList<QUuid> tabsToClose;
    for (ruwa::core::BaseTab* tab : m_tabManager->tabs()) {
        if (tab->id() != tabId) {
            tabsToClose.append(tab->id());
        }
    }

    for (const QUuid& id : tabsToClose) {
        ruwa::core::BaseTab* tab = m_tabManager->tab(id);
        if (!tab) {
            continue; // Already closed as a side effect (contents follow their document).
        }
        auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab);
        if (wsTab && !ruwa::ui::widgets::prepareWorkspaceTabForClose(wsTab, context)) {
            return; // User cancelled
        }
        m_tabManager->requestCloseTab(id);
    }
}

void CustomTabBar::onCloseAllTabsRequested()
{
    if (!m_tabManager)
        return;

    QWidget* context = window();
    if (!context)
        return;

    const QList<ruwa::core::BaseTab*> tabs = m_tabManager->tabs();
    for (ruwa::core::BaseTab* tab : tabs) {
        auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab);
        if (wsTab && !ruwa::ui::widgets::prepareWorkspaceTabForClose(wsTab, context)) {
            return; // User cancelled
        }
    }

    QList<QUuid> tabsToClose;
    for (ruwa::core::BaseTab* tab : tabs) {
        tabsToClose.append(tab->id());
    }
    for (const QUuid& id : tabsToClose) {
        if (m_tabManager->hasTab(id)) {
            m_tabManager->requestCloseTab(id);
        }
    }
}

void CustomTabBar::onSmartObjectTabActivated(const QUuid& tabId)
{
    if (!m_tabManager) {
        return;
    }
    auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(m_tabManager->tab(tabId));
    if (!wsTab || !wsTab->isSmartContentEditor()) {
        return;
    }

    // activateTab() alone would move the strip through onActiveTabChanged, but do
    // it explicitly so the slot is right even if the tab was already active.
    const QUuid parentTabId = wsTab->smartEditDocumentTabId();
    m_shownSmartByParent.insert(parentTabId, tabId);
    syncSmartSlotForParent(parentTabId, true);
    m_tabManager->activateTab(tabId);
}

void CustomTabBar::onCloseAllSmartObjectTabsRequested(const QUuid& parentTabId)
{
    if (!m_tabManager) {
        return;
    }
    QWidget* context = window();
    for (const QUuid& tabId : smartObjectTabsForParent(parentTabId)) {
        // Contents carry edits that only exist in their tab until committed, so
        // closing them in bulk asks the same question closing one does.
        auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(m_tabManager->tab(tabId));
        if (wsTab && context && !ruwa::ui::widgets::prepareWorkspaceTabForClose(wsTab, context)) {
            return; // User cancelled
        }
        m_tabManager->requestCloseTab(tabId);
    }
}

void CustomTabBar::onTabRenamed(const QUuid& tabId, const QString& newName)
{
    // A smart object is named by its layer; its tab is not the user's to rename.
    if (m_tabManager && isSmartObjectTab(m_tabManager->tab(tabId))) {
        return;
    }
    if (m_tabManager) {
        if (auto* tab = m_tabManager->tab(tabId)) {
            if (auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab)) {
                const QString resolved
                    = ruwa::ui::tabs::WorkspaceTab::sanitizedRenameInput(newName);
                if (resolved.isEmpty()) {
                    return;
                }
                tab->setTitle(resolved);
                wsTab->setTabTitle(resolved);
            } else {
                tab->setTitle(newName);
            }
        }
    }

    // Update local item (use tab->title() so unsaved marker stays in sync for workspaces)
    if (m_indexById.contains(tabId)) {
        int idx = m_indexById.value(tabId);
        if (idx >= 0 && idx < m_items.size()) {
            if (ruwa::core::BaseTab* tab = m_tabManager ? m_tabManager->tab(tabId) : nullptr) {
                m_items[idx].title = tab->title();
            } else {
                m_items[idx].title = newName;
            }
            updateLayout();
        }
    }
}

void CustomTabBar::onTabIconChanged(const QUuid& tabId, const QString& iconAlias)
{
    // Smart objects are drawn without an icon on purpose — never give them one.
    if (m_tabManager && isSmartObjectTab(m_tabManager->tab(tabId))) {
        return;
    }

    QIcon newIcon = ruwa::ui::core::IconProvider::instance().getIcon(iconAlias);

    if (m_tabManager) {
        if (auto* tab = m_tabManager->tab(tabId)) {
            tab->setIcon(newIcon);

            // Also update WorkspaceTab's persistent icon alias
            if (auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab)) {
                wsTab->setTabIconAlias(iconAlias);
            }
        }
    }

    // Update local item
    if (m_indexById.contains(tabId)) {
        int idx = m_indexById.value(tabId);
        if (idx >= 0 && idx < m_items.size()) {
            m_items[idx].icon = newIcon;
            m_items[idx].iconAlias = iconAlias;
            update();
        }
    }
}

} // namespace ruwa::ui::tabs
