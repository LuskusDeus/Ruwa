// SPDX-License-Identifier: MPL-2.0

#include "CustomTabBar.h"
#include "shell/tab-system/TabManager.h"
#include "shell/tab-system/BaseTab.h"
#include "shell/tab-system/WorkspaceTab.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/settings/SettingsManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/PaintingUtils.h"
#include "shell/top-bar/TopBar.h"
#include "shell/top-bar/UnsavedChangesHelper.h"

#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QWindow>
#include <QCursor>
#include <QResizeEvent>
#include <QShowEvent>

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
    QFont tabFont = font();
    tabFont.setPointSize(9);
    const QFontMetrics fm(tabFont);

    for (int i = 0; i < m_items.size(); ++i) {
        const TabItem& item = m_items[i];
        qreal w = ICON_MARGIN + ICON_SIZE + ICON_MARGIN;
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

        rebuildFromManager();
    }
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
    m_hoveredIndex = -1;

    if (!m_tabManager)
        return;

    // Build from manager's tab list
    for (ruwa::core::BaseTab* tab : m_tabManager->tabs()) {
        TabItem item;
        item.id = tab->id();
        item.title = tab->title();
        item.icon = tab->icon();
        item.iconAlias = defaultIconForTabType(tab->type());

        // Get persisted alias from WorkspaceTab if available
        if (auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab)) {
            if (!wsTab->tabIconAlias().isEmpty()) {
                item.iconAlias = wsTab->tabIconAlias();
            }
        }

        // Fallback to default icon if tab has none
        if (item.icon.isNull()) {
            item.icon = ruwa::ui::core::IconProvider::instance().getIcon(item.iconAlias);
        }

        // Create hover animation
        item.hoverAnim = new QVariantAnimation(this);
        item.hoverAnim->setDuration(200);
        item.hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
        item.hoverAnim->setStartValue(0.0);
        item.hoverAnim->setEndValue(1.0);

        QUuid itemId = item.id;
        connect(item.hoverAnim, &QVariantAnimation::valueChanged, this,
            [this, itemId](const QVariant& v) {
                int i = m_indexById.value(itemId, -1);
                if (i >= 0) {
                    m_items[i].hoverProgress = v.toReal();
                    update();
                }
            });

        // Create fade animation
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

        m_indexById.insert(item.id, m_items.size());
        m_items.append(item);

        bindTabDisplayTitleSignals(tab);
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

    // Create new tab item
    TabItem item;
    item.id = tab->id();
    item.title = tab->title();
    item.icon = tab->icon();
    item.iconAlias = defaultIconForTabType(tab->type());
    item.opacity = 0.0; // Start invisible for appear animation
    item.verticalOffset = tabPopDistancePx();

    // Get persisted alias from WorkspaceTab if available
    if (auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab)) {
        if (!wsTab->tabIconAlias().isEmpty()) {
            item.iconAlias = wsTab->tabIconAlias();
        }
    }

    // Fallback to default icon if tab has none
    if (item.icon.isNull()) {
        item.icon = ruwa::ui::core::IconProvider::instance().getIcon(item.iconAlias);
    }

    // Create hover animation
    item.hoverAnim = new QVariantAnimation(this);
    item.hoverAnim->setDuration(200);
    item.hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
    item.hoverAnim->setStartValue(0.0);
    item.hoverAnim->setEndValue(1.0);

    QUuid itemId = item.id;
    connect(
        item.hoverAnim, &QVariantAnimation::valueChanged, this, [this, itemId](const QVariant& v) {
            int i = m_indexById.value(itemId, -1);
            if (i >= 0) {
                m_items[i].hoverProgress = v.toReal();
                update();
            }
        });

    // Create fade animation
    item.fadeAnim = new QVariantAnimation(this);
    item.fadeAnim->setDuration(250);
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
                update();
            }
        });

    // Add to list
    int idx = m_items.size();
    m_indexById.insert(item.id, idx);
    m_items.append(item);

    bindTabDisplayTitleSignals(tab);

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

    QUuid tabId = tab->id();

    if (!m_indexById.contains(tabId)) {
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
    // Tab was already removed from display in onTabClosing
    // This is just for cleanup - nothing to do here
    Q_UNUSED(tabId);
}

void CustomTabBar::onActiveTabChanged(ruwa::core::BaseTab* newTab, ruwa::core::BaseTab* oldTab)
{
    Q_UNUSED(oldTab);
    m_activeId = newTab ? newTab->id() : QUuid();
    update();
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
    QFont tabFont = font();
    tabFont.setPointSize(9);
    const QFontMetrics fm(tabFont);

    for (int i = 0; i < m_items.size(); ++i) {
        TabItem& item = m_items[i];

        qreal w = ICON_MARGIN + ICON_SIZE + ICON_MARGIN;
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
            drawSeparator(p, sepX, height() / 2.0, sepAnim);
        }
    }
}

void CustomTabBar::drawTab(QPainter& painter, const TabItem& item, bool isActive, bool isHovered)
{
    painter.save();
    painter.setOpacity(item.opacity);
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

    // Icon
    qreal iconX = x + ICON_MARGIN;
    qreal iconY = (height() - ICON_SIZE) / 2.0;
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

    // Title
    qreal textX = iconX + ICON_SIZE + ICON_MARGIN;
    QFont textFont = font();
    textFont.setPointSize(9);
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

void CustomTabBar::drawSeparator(QPainter& painter, qreal x, qreal y, const TabItem& anim)
{
    painter.save();
    painter.setOpacity(anim.opacity);
    painter.translate(0, anim.verticalOffset);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    QFont sepFont = font();
    sepFont.setPointSize(9);
    painter.setFont(sepFont);
    painter.setPen(colors.textMuted);
    painter.drawText(QPointF(x - 6, y + 4), "/");
    painter.restore();
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
        ruwa::core::BaseTab* tab = m_tabManager->tab(item.id);
        auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab);
        QWidget* context = window();
        if (wsTab && context && !ruwa::ui::widgets::prepareWorkspaceTabForClose(wsTab, context)) {
            return; // User cancelled
        }
        m_tabManager->requestCloseTab(item.id);
    } else {
        m_tabManager->activateTab(item.id);
    }
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
        return ruwa::ui::widgets::ContextMenuType::TabBar;
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

    QList<QUuid> tabsToClose;
    for (const TabItem& item : m_items) {
        if (item.id != tabId) {
            tabsToClose.append(item.id);
        }
    }

    for (const QUuid& id : tabsToClose) {
        ruwa::core::BaseTab* tab = m_tabManager->tab(id);
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

    for (const TabItem& item : m_items) {
        ruwa::core::BaseTab* tab = m_tabManager->tab(item.id);
        auto* wsTab = qobject_cast<ruwa::ui::tabs::WorkspaceTab*>(tab);
        if (wsTab && !ruwa::ui::widgets::prepareWorkspaceTabForClose(wsTab, context)) {
            return; // User cancelled
        }
    }

    QList<QUuid> tabsToClose;
    for (const TabItem& item : m_items) {
        tabsToClose.append(item.id);
    }
    for (const QUuid& id : tabsToClose) {
        m_tabManager->requestCloseTab(id);
    }
}

void CustomTabBar::onTabRenamed(const QUuid& tabId, const QString& newName)
{
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
