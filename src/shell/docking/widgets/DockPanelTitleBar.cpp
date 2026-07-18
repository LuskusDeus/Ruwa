// SPDX-License-Identifier: MPL-2.0

// DockPanelTitleBar.cpp
#include "DockPanelTitleBar.h"
#include "DockPanel.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/FontManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/WidgetStyleManager.h"

#include <QApplication>
#include <QEasingCurve>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QShowEvent>
#include <QVariantAnimation>

namespace ruwa::ui::docking {

namespace {

constexpr int kTitleSidePadding = 6;
constexpr int kInteractiveGap = 6;
constexpr int kInteractiveVerticalPadding = 1;

// Match BrushPackPanel / floating overlays: pill-shaped drag strip
constexpr int kBaseHandleLineWidth = 96;
constexpr int kBaseHandleLineHeight = 3;
constexpr int kBaseHandleSideMargin = 6;

/// Parabolic in p: 0 at p=0 and p=1, peak at p=0.5 — drives in-bar slide (height stays fixed).
inline qreal slideBump(qreal p)
{
    p = qBound(0.0, p, 1.0);
    return 4.0 * p * (1.0 - p);
}

/// Downward shift of title row inside the fixed title bar (clipped; does not change widget height).
inline int titleSlideOffsetPx(const DockPanel* panel, int titleRowH, int scaledExtra, qreal p)
{
    if (!panel || !panel->isFloating()) {
        return 0;
    }
    const int raw = qRound(qreal(scaledExtra) * slideBump(p));
    const int cap = qMax(0, titleRowH - 6);
    return qMin(raw, cap);
}

/// Title stays readable while the row slides; handle ramps in later (p³ vs (1−p)³).
inline qreal titleCrossfadeOpacity(qreal p)
{
    p = qBound(0.0, p, 1.0);
    const qreal t = 1.0 - p;
    return t * t * t;
}

inline qreal handleCrossfadeOpacity(qreal p)
{
    p = qBound(0.0, p, 1.0);
    return p * p * p;
}

/// Drag pill moves from vertical center toward the top as the title slides down (fixed bar height).
inline qreal handleCenterY(int barH, int slideOffset, int scaledExtra)
{
    if (barH <= 0) {
        return 0.0;
    }
    const qreal denom = qMax(1, scaledExtra);
    const qreal norm = qBound(0.0, qreal(slideOffset) / denom, 1.0);
    const qreal midY = qreal(barH) * 0.5;
    const qreal topY = qreal(barH) * 0.28; // stable upper slot, no theme call here
    return (1.0 - norm) * midY + norm * topY;
}

inline int handleLineWidthForBar(
    int barWidth, int baseLineW, int lineH, const ruwa::ui::core::ThemeManager& theme)
{
    const int sidePad = theme.scaled(kBaseHandleSideMargin);
    const int inner = barWidth - 2 * sidePad;
    if (inner <= lineH) {
        return qMax(1, barWidth - 2 * qMax(2, sidePad / 2));
    }
    const int desired = theme.scaled(baseLineW);
    return qBound(lineH, qMin(desired, inner), inner);
}

} // namespace

DockPanelTitleBar::DockPanelTitleBar(DockPanel* panel)
    : QWidget(panel)
    , m_panel(panel)
{
    setupUI();
    setupFloatingLayoutAnimation();

    // Repaint when application focus changes so active/inactive title colors stay in sync.
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget*) { update(); });
}

DockPanelTitleBar::~DockPanelTitleBar() = default;

// ============================================================================
// Configuration
// ============================================================================

void DockPanelTitleBar::setBarHeight(int height)
{
    m_height = height;
    setFixedHeight(m_height);
    updateInteractiveWidgetGeometries();
    notifyPanelLayoutGeometry();
}

void DockPanelTitleBar::setLeadingWidget(QWidget* widget)
{
    m_leadingWidget = replaceInteractiveWidget(m_leadingWidget, widget, m_leadingHost);
    updateInteractiveWidgetGeometries();
    update();
}

void DockPanelTitleBar::setTrailingWidget(QWidget* widget)
{
    m_trailingWidget = replaceInteractiveWidget(m_trailingWidget, widget, m_trailingHost);
    updateInteractiveWidgetGeometries();
    update();
}

void DockPanelTitleBar::setInteractiveWidgetsVisibleWhenFloating(bool visible)
{
    if (m_interactiveWidgetsVisibleWhenFloating == visible) {
        return;
    }

    m_interactiveWidgetsVisibleWhenFloating = visible;
    updateInteractiveWidgetGeometries();
    update();
}

void DockPanelTitleBar::setDrawBottomBorder(bool draw)
{
    if (m_drawBottomBorder != draw) {
        m_drawBottomBorder = draw;
        update();
    }
}

// ============================================================================
// Updates
// ============================================================================

void DockPanelTitleBar::updateTitle()
{
    update();
}

void DockPanelTitleBar::updateIcon()
{
    // Icons are resolved during painting so theme-aware icon types stay current.
    update();
}

void DockPanelTitleBar::updateButtons()
{
    update();
}

void DockPanelTitleBar::applyTheme(const ruwa::ui::core::ThemeColors& c)
{
    m_iconPlaceholderColor = c.textMuted;
    m_textColor = c.text;
    m_backgroundColor = c.surfaceAlt;
    m_borderColor = c.border;
    m_titleFont = ruwa::ui::core::FontManager::instance().getFont(
        ruwa::ui::core::FontManager::FontType::UI);

    m_scaledSlideExtra = ruwa::ui::core::ThemeManager::instance().scaled(10);
    setFixedHeight(m_height);
    updateInteractiveWidgetGeometries();
    notifyPanelLayoutGeometry();

    update();
}

void DockPanelTitleBar::syncFloatingLayout(bool floating)
{
    if (!m_panel) {
        return;
    }

    const qreal target = floating ? 1.0 : 0.0;
    if (qFuzzyCompare(m_floatingLayoutProgress, target)) {
        return;
    }

    if (!m_floatingLayoutAnim) {
        setFloatingLayoutProgress(target);
        return;
    }

    if (!ruwa::ui::core::WidgetStyleManager::instance().animationsEnabled()) {
        m_floatingLayoutAnim->stop();
        setFloatingLayoutProgress(target);
        return;
    }

    m_floatingLayoutAnim->stop();
    m_floatingLayoutAnim->setStartValue(m_floatingLayoutProgress);
    m_floatingLayoutAnim->setEndValue(target);
    m_floatingLayoutAnim->start();
}

void DockPanelTitleBar::setFloatingLayoutProgress(qreal progress)
{
    const qreal p = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_floatingLayoutProgress, p)) {
        return;
    }

    m_floatingLayoutProgress = p;
    setFixedHeight(m_height);
    updateInteractiveWidgetGeometries();
    notifyPanelLayoutGeometry();
    update();
}

ruwa::ui::widgets::ContextMenuType DockPanelTitleBar::contextMenuType() const
{
    return ruwa::ui::widgets::ContextMenuType::DockPanelTitle;
}

QVariantMap DockPanelTitleBar::contextMenuContext() const
{
    QVariantMap ctx;
    if (m_panel) {
        ctx["dockPanelPtr"] = static_cast<qulonglong>(reinterpret_cast<quintptr>(m_panel));
        ctx["dockPanelTitle"] = m_panel->title();
    }
    return ctx;
}

// ============================================================================
// Events
// ============================================================================

void DockPanelTitleBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->globalPosition().toPoint();
        m_dragging = false;

        if (m_panel) {
            m_panel->setFocus(Qt::MouseFocusReason);
        }

        // Check if panel is movable
        if (m_panel && m_panel->isMovable()) {
            setCursor(Qt::SizeAllCursor);
        }
    }

    QWidget::mousePressEvent(event);
}

void DockPanelTitleBar::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton) {
        QPoint globalPos = event->globalPosition().toPoint();
        int distance = (globalPos - m_dragStartPos).manhattanLength();

        if (!m_dragging && distance >= DragThreshold) {
            // Start drag
            if (m_panel && m_panel->isMovable()) {
                m_dragging = true;
                emit dragStarted(m_dragStartPos);
            }
        }

        if (m_dragging) {
            emit dragging(globalPos);
        }
    }

    QWidget::mouseMoveEvent(event);
}

void DockPanelTitleBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        setCursor(Qt::ArrowCursor);

        if (m_dragging) {
            m_dragging = false;
            emit dragFinished(event->globalPosition().toPoint());
        }
    }

    QWidget::mouseReleaseEvent(event);
}

void DockPanelTitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked();

        // Toggle floating on double click
        if (m_panel && m_panel->isFloatable()) {
            m_panel->toggleFloating();
        }
    }

    QWidget::mouseDoubleClickEvent(event);
}

void DockPanelTitleBar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateInteractiveWidgetGeometries();
}

void DockPanelTitleBar::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_panel) {
        syncFloatingLayout(m_panel->isFloating());
    }
}

void DockPanelTitleBar::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Rounded top while floating or mid transition (p away from 0)
    const bool isFloating = m_panel && m_panel->isFloating();
    const qreal p = m_floatingLayoutProgress;
    const int slideOffset = titleSlideOffsetPx(m_panel, m_height, m_scaledSlideExtra, p);
    const int radius = (isFloating || p > 0.02) ? 5 : 0;

    // Background with optional top rounded corners
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_backgroundColor);

    if (radius > 0) {
        // Path with only top corners rounded
        QPainterPath path;
        path.moveTo(0, height());
        path.lineTo(0, radius);
        path.quadTo(0, 0, radius, 0);
        path.lineTo(width() - radius, 0);
        path.quadTo(width(), 0, width(), radius);
        path.lineTo(width(), height());
        path.closeSubpath();
        painter.drawPath(path);
    } else {
        painter.drawRect(rect());
    }

    if (m_drawBottomBorder) {
        painter.setPen(m_borderColor);
        painter.drawLine(0, height() - 1, width(), height() - 1);
    }

    // Active panel keeps normal colors; inactive panels are muted.
    const QWidget* focusWidget = QApplication::focusWidget();
    const bool panelFocused
        = m_panel && focusWidget && (focusWidget == m_panel || m_panel->isAncestorOf(focusWidget));
    const QColor effectiveTextColor = panelFocused ? m_textColor : m_textColor.darker(130);
    const QColor effectiveIconPlaceholderColor
        = panelFocused ? m_iconPlaceholderColor : m_iconPlaceholderColor.darker(130);
    const qreal iconOpacity = panelFocused ? 1.0 : 0.55;

    const qreal titlePaintOpacity = isFloating ? titleCrossfadeOpacity(p) : 1.0;
    const qreal handlePaintOpacity = isFloating ? handleCrossfadeOpacity(p) : 0.0;

    // Title + icon: slide down inside fixed m_height; overflow clipped.
    if (titlePaintOpacity > 0.001 && m_panel) {
        const int titleY = slideOffset;
        int iconSize = m_height - 6; // 2px padding top/bottom + 1px for balance
        int iconY = titleY + (m_height - iconSize) / 2;
        int iconRadius = 3;
        constexpr int iconTextSpacing = 6;

        QIcon drawIcon;
        if (m_panel->iconType()) {
            auto& icons = ruwa::ui::core::IconProvider::instance();
            drawIcon = icons.getColoredIcon(*m_panel->iconType(), effectiveTextColor);
        } else if (!m_panel->icon().isNull()) {
            drawIcon = m_panel->icon();
        }

        painter.setFont(m_titleFont);
        painter.setPen(effectiveTextColor);

        QString title = m_panel ? m_panel->title() : QString();
        QFontMetrics fm(m_titleFont);
        const QRect contentRect = titleContentRect();
        int maxTextWidth = qMax(0, contentRect.width() - iconSize - iconTextSpacing);
        QString elidedTitle = fm.elidedText(title, Qt::ElideRight, maxTextWidth);
        int titleWidth = fm.horizontalAdvance(elidedTitle);
        int contentWidth = iconSize + iconTextSpacing + titleWidth;
        const int centeredContentX = (width() - contentWidth) / 2;
        const int minContentX = contentRect.left();
        const int maxContentX = contentRect.right() - contentWidth + 1;
        int contentX = centeredContentX;
        if (maxContentX >= minContentX) {
            contentX = qBound(minContentX, centeredContentX, maxContentX);
        } else {
            contentX = minContentX;
        }
        int iconX = contentX;
        int textX = iconX + iconSize + iconTextSpacing;
        int textY = titleY + (m_height + fm.ascent() - fm.descent()) / 2;

        painter.save();
        painter.setClipRect(0, 0, width(), m_height);
        painter.setOpacity(titlePaintOpacity);

        if (!drawIcon.isNull()) {
            painter.save();
            painter.setOpacity(iconOpacity * titlePaintOpacity);
            QPixmap pix = drawIcon.pixmap(iconSize, iconSize);
            if (!pix.isNull()) {
                painter.drawPixmap(iconX, iconY, pix);
            } else {
                painter.setPen(Qt::NoPen);
                painter.setBrush(effectiveIconPlaceholderColor);
                painter.drawRoundedRect(iconX, iconY, iconSize, iconSize, iconRadius, iconRadius);
            }
            painter.restore();
        } else {
            painter.setPen(Qt::NoPen);
            painter.setBrush(effectiveIconPlaceholderColor);
            painter.drawRoundedRect(iconX, iconY, iconSize, iconSize, iconRadius, iconRadius);
        }

        painter.setPen(effectiveTextColor);
        painter.drawText(textX, textY, elidedTitle);
        painter.restore();
    }

    if (handlePaintOpacity > 0.001) {
        painter.save();
        painter.setOpacity(handlePaintOpacity);
        auto& theme = ruwa::ui::core::ThemeManager::instance();
        auto& mgr = ruwa::ui::core::WidgetStyleManager::instance();
        const int lineH = theme.scaled(kBaseHandleLineHeight);
        const QRect handleBounds
            = m_interactiveWidgetsVisibleWhenFloating ? titleContentRect() : rect();
        const int lineW
            = handleLineWidthForBar(handleBounds.width(), kBaseHandleLineWidth, lineH, theme);
        const qreal handleCy = handleCenterY(m_height, slideOffset, m_scaledSlideExtra);
        QRectF lineRect(
            handleBounds.center().x() - lineW / 2.0, handleCy - lineH / 2.0, lineW, lineH);
        const qreal lineRadius = lineRect.height() / 2.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(mgr.colors().textMuted);
        painter.drawRoundedRect(lineRect, lineRadius, lineRadius);
        painter.restore();
    }
}

// ============================================================================
// Private
// ============================================================================

void DockPanelTitleBar::setupFloatingLayoutAnimation()
{
    m_floatingLayoutAnim = new QVariantAnimation(this);
    m_floatingLayoutAnim->setDuration(400);
    m_floatingLayoutAnim->setEasingCurve(QEasingCurve::InOutSine);
    connect(m_floatingLayoutAnim, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& v) { setFloatingLayoutProgress(v.toDouble()); });
}

void DockPanelTitleBar::notifyPanelLayoutGeometry()
{
    if (!m_panel) {
        return;
    }
    m_panel->updateContentTransitionGeometry();
}

void DockPanelTitleBar::setupUI()
{
    setFixedHeight(m_height);
    setCursor(Qt::ArrowCursor);

    m_leadingHost = new QWidget(this);
    m_leadingHost->setAttribute(Qt::WA_TranslucentBackground);
    m_leadingHost->hide();

    m_trailingHost = new QWidget(this);
    m_trailingHost->setAttribute(Qt::WA_TranslucentBackground);
    m_trailingHost->hide();
}

void DockPanelTitleBar::updateInteractiveWidgetGeometries()
{
    const bool hideChrome = !m_interactiveWidgetsVisibleWhenFloating && m_panel
        && m_panel->isFloating()
        && handleCrossfadeOpacity(m_floatingLayoutProgress) > 0.35;

    auto updateHost = [this, hideChrome](
                          QWidget* host, QWidget* content, int x, Qt::Alignment alignment) {
        if (!host) {
            return 0;
        }

        if (!content) {
            host->hide();
            return 0;
        }

        if (hideChrome) {
            host->hide();
            return 0;
        }

        QSize size = content->sizeHint();
        if (!size.isValid()) {
            size = content->minimumSizeHint();
        }
        if (!size.isValid()) {
            size = content->size();
        }

        size.setHeight(qMin(size.height(), qMax(0, m_height - (kInteractiveVerticalPadding * 2))));
        content->resize(size);
        host->resize(size);

        const int slide = m_interactiveWidgetsVisibleWhenFloating
            ? 0
            : titleSlideOffsetPx(
                  m_panel, m_height, m_scaledSlideExtra, m_floatingLayoutProgress);
        const int y = slide + qMax(0, (m_height - size.height()) / 2);
        if (alignment == Qt::AlignLeft) {
            host->move(x, y);
        } else {
            host->move(x - size.width(), y);
        }

        content->move(0, 0);
        host->show();
        return size.width();
    };

    const int leftWidth
        = updateHost(m_leadingHost, m_leadingWidget, kTitleSidePadding, Qt::AlignLeft);
    Q_UNUSED(leftWidth);
    updateHost(m_trailingHost, m_trailingWidget, width() - kTitleSidePadding, Qt::AlignRight);
}

QWidget* DockPanelTitleBar::replaceInteractiveWidget(
    QWidget* currentWidget, QWidget* newWidget, QWidget* host)
{
    if (currentWidget == newWidget) {
        return currentWidget;
    }

    if (currentWidget) {
        currentWidget->hide();
        currentWidget->deleteLater();
    }

    if (!newWidget || !host) {
        return nullptr;
    }

    newWidget->setParent(host);
    newWidget->show();
    return newWidget;
}

QRect DockPanelTitleBar::titleContentRect() const
{
    int left = kTitleSidePadding;
    int right = width() - kTitleSidePadding;

    if (m_leadingHost && m_leadingWidget && m_leadingHost->isVisible()) {
        left = m_leadingHost->geometry().right() + 1 + kInteractiveGap;
    }

    if (m_trailingHost && m_trailingWidget && m_trailingHost->isVisible()) {
        right = m_trailingHost->geometry().left() - kInteractiveGap;
    }

    if (right < left) {
        return QRect(left, 0, 0, m_height);
    }

    return QRect(left, 0, right - left, m_height);
}

} // namespace ruwa::ui::docking
