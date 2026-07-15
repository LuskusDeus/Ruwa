// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   BASE CONTEXT MENU - IMPLEMENTATION
// ======================================================================================

#include "BaseContextMenu.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "shared/widgets/BaseStyledWidget.h"
#include "shared/style/WidgetStyle.h"
#include "shared/style/PaintingUtils.h"

#include <QPainter>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QGraphicsOpacityEffect>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QApplication>
#include <QScreen>
#include <QTimer>

namespace ruwa::ui::widgets {

BaseContextMenu::BaseContextMenu(QWidget* parent)
    : QWidget(parent)
{
    // Frameless transparent window
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);

    m_ctxAppearEase.setType(QEasingCurve::BezierSpline);
    m_ctxAppearEase.addCubicBezierSegment(QPointF(0.16, 1.0), QPointF(0.3, 1.0), QPointF(1.0, 1.0));

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);
    setGraphicsEffect(m_opacityEffect);

    m_showProgressAnim = new QVariantAnimation(this);
    m_showProgressAnim->setEasingCurve(QEasingCurve::Linear);
    connect(
        m_showProgressAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            const qreal t = value.toReal();
            qreal p = 0.0;
            if (m_isHiding) {
                p = m_hideStartProgress * m_ctxAppearEase.valueForProgress(1.0 - t);
            } else {
                p = m_showStartProgress
                    + (1.0 - m_showStartProgress) * m_ctxAppearEase.valueForProgress(t);
            }
            setShowProgress(p);
        });
    connect(m_showProgressAnim, &QVariantAnimation::finished, this, [this]() {
        if (m_isHiding) {
            m_isHiding = false;
            qApp->removeEventFilter(this);
            QWidget::hide();
            deleteLater();
        } else if (m_isShowing) {
            m_isShowing = false;
        }
    });

    // Setup position animation
    m_posAnimation = new QPropertyAnimation(this, "menuPos", this);
    m_posAnimation->setDuration(RepositionDuration);
    m_posAnimation->setEasingCurve(QEasingCurve::InOutExpo);

    connect(m_posAnimation, &QPropertyAnimation::finished, this,
        [this]() { m_isRepositioning = false; });

    connect(m_posAnimation, &QPropertyAnimation::stateChanged, this,
        [this](QAbstractAnimation::State newState, QAbstractAnimation::State oldState) { });

    qApp->installEventFilter(this);
}

BaseContextMenu::~BaseContextMenu()
{
    qApp->removeEventFilter(this);
}

void BaseContextMenu::setContext(const QVariantMap& context)
{
    m_context = context;
    onContextChanged();
    update();
}

void BaseContextMenu::showAt(
    const QPoint& globalPos, const QVariantMap& context, QWidget* sourceWidget)
{
    Q_UNUSED(sourceWidget);

    if (isVisible() && !m_isHiding) {
        if (m_isShowing) {
            m_showProgressAnim->stop();
            m_isShowing = false;
            setShowProgress(1.0);
        }

        QPoint targetPosition = calculateMenuPosition(globalPos, size(), sourceWidget);
        animateToPosition(targetPosition);
        setContext(context);

        QPoint finalPosition = calculateMenuPosition(globalPos, size(), sourceWidget);
        if (finalPosition != targetPosition) {
            m_posAnimation->stop();
            m_isRepositioning = true;
            m_posAnimation->setStartValue(m_restPos);
            m_posAnimation->setEndValue(finalPosition);
            m_posAnimation->start();
        }

        return;
    }

    if (m_isHiding) {
        m_showProgressAnim->stop();
        m_isHiding = false;
    }

    m_isShowing = true;

    setContext(context);

    // Anchor-aware placement: when the caller supplies an "anchorRect" (the global
    // rect of the trigger button), position the panel relative to it and flip above
    // when there is no room below. Otherwise fall back to cursor-based placement.
    m_hasAnchor = false;
    m_placedAbove = false;
    if (m_context.contains(QStringLiteral("anchorRect"))) {
        const QRect r = m_context.value(QStringLiteral("anchorRect")).toRect();
        if (r.isValid()) {
            m_anchorRectGlobal = r;
            m_hasAnchor = true;
        }
    }

    QPoint targetPosition = m_hasAnchor ? calculateAnchoredPosition()
                                        : calculateMenuPosition(globalPos, size(), sourceWidget);
    m_restPos = targetPosition;
    updateSlideMetrics();
    setShowProgress(0.0);

    QWidget::show();
    raise();

    m_showStartProgress = m_showProgress;
    m_showProgressAnim->stop();
    m_showProgressAnim->setDuration(ShowDurationMs);
    m_showProgressAnim->setStartValue(0.0);
    m_showProgressAnim->setEndValue(1.0);
    m_showProgressAnim->start();

    QTimer::singleShot(0, this, [this] {
        if (!isVisible()) {
            return;
        }
        for (QObject* o : children()) {
            if (auto* w = qobject_cast<QWidget*>(o)) {
                w->update();
            }
        }
        update();
    });
}

void BaseContextMenu::hideAnimated()
{
    if (m_isHiding || !isVisible()) {
        return;
    }

    m_isHiding = true;
    bool wasShowing = m_isShowing;
    m_isShowing = false;

    m_showProgressAnim->stop();

    if (wasShowing && m_showProgress < 0.5) {
        setShowProgress(0.0);
        hide();
        deleteLater();
        return;
    }

    m_hideStartProgress = m_showProgress;
    m_showProgressAnim->setDuration(HideDurationMs);
    m_showProgressAnim->setStartValue(0.0);
    m_showProgressAnim->setEndValue(1.0);
    m_showProgressAnim->start();
}

void BaseContextMenu::hide()
{
    qApp->removeEventFilter(this);

    if (m_showProgressAnim) {
        m_showProgressAnim->stop();
    }
    if (m_posAnimation) {
        m_posAnimation->stop();
    }

    m_isHiding = false;
    m_isShowing = false;
    m_isRepositioning = false;

    QWidget::hide();
}

void BaseContextMenu::setShowProgress(qreal progress)
{
    m_showProgress = qBound(0.0, progress, 1.0);
    applyPresentationLayout(m_showProgress);
    if (m_opacityEffect) {
        m_opacityEffect->setOpacity(presentationOpacity(m_showProgress));
    }
    syncVisualPosition();
    update();
}

void BaseContextMenu::applyPresentationLayout(qreal progress)
{
    Q_UNUSED(progress);
}

void BaseContextMenu::syncVisualPosition()
{
    // Slide direction follows placement: a menu below its anchor eases downward
    // into place (starts a few px above), one placed above eases upward.
    const qreal dir = m_placedAbove ? 1.0 : -1.0;
    const int dy = static_cast<int>(qRound(dir * m_slidePx * (1.0 - m_showProgress)));

    // Keep the anchored corner fixed while the panel scales up, so the menu
    // appears to grow out of the button (top-right below it / bottom-right above)
    // instead of from the widget's top-left origin.
    QPoint pivotComp;
    if (m_hasAnchor) {
        const qreal u = presentationContentScale();
        pivotComp = QPoint(static_cast<int>(qRound(m_growPivot.x() * (1.0 - u))),
            static_cast<int>(qRound(m_growPivot.y() * (1.0 - u))));
    }

    QWidget::move(m_restPos + pivotComp + QPoint(0, dy));
}

qreal BaseContextMenu::presentationContentScale() const
{
    return 1.0;
}

void BaseContextMenu::updateSlideMetrics()
{
    m_slidePx = presentationSlideDistancePx();
}

qreal BaseContextMenu::presentationSlideDistancePx() const
{
    return ruwa::ui::core::ThemeManager::instance().scaled(6);
}

qreal BaseContextMenu::presentationOpacity(qreal progress) const
{
    return progress;
}

bool BaseContextMenu::isActive() const
{
    return isVisible() && !m_isHiding;
}

QPoint BaseContextMenu::menuPos() const
{
    return m_restPos;
}

void BaseContextMenu::setMenuPos(const QPoint& pos)
{
    m_restPos = pos;
    syncVisualPosition();
}

void BaseContextMenu::animateToPosition(const QPoint& targetPos)
{
    if (m_isRepositioning) {
        m_posAnimation->stop();
    }

    m_isRepositioning = true;

    m_posAnimation->setStartValue(m_restPos);
    m_posAnimation->setEndValue(targetPos);

    m_posAnimation->start();
}

QPoint BaseContextMenu::calculateMenuPosition(
    const QPoint& globalPos, const QSize& menuSize, QWidget* sourceWidget) const
{
    Q_UNUSED(sourceWidget);

    QScreen* screen = QGuiApplication::screenAt(globalPos);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    QRect screenGeometry = screen->availableGeometry();

    QPoint pos = globalPos;

    pos += QPoint(4, 4);

    if (pos.x() + menuSize.width() > screenGeometry.right()) {
        pos.setX(screenGeometry.right() - menuSize.width());
    }
    if (pos.y() + menuSize.height() > screenGeometry.bottom()) {
        pos.setY(screenGeometry.bottom() - menuSize.height());
    }

    pos.setX(qMax(pos.x(), screenGeometry.left()));
    pos.setY(qMax(pos.y(), screenGeometry.top()));

    return pos;
}

QRect BaseContextMenu::fullContentPanelRect() const
{
    return QRect(QPoint(0, 0), size());
}

QPoint BaseContextMenu::calculateAnchoredPosition()
{
    const QRect anchor = m_anchorRectGlobal;
    const QRect panel = fullContentPanelRect(); // visible panel inside the widget

    QScreen* screen = QGuiApplication::screenAt(anchor.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    const QRect scr = screen->availableGeometry();

    const int insetX = panel.left();
    const int insetTop = panel.top();
    const int panelW = panel.width();
    const int panelH = panel.height();
    constexpr int gap = 4;

    // Horizontal: right-align the panel to the anchor's right edge, clamp to screen.
    int panelLeft = anchor.right() - panelW + 1;
    if (panelLeft + panelW > scr.right()) {
        panelLeft = scr.right() - panelW;
    }
    if (panelLeft < scr.left()) {
        panelLeft = scr.left();
    }
    const int wx = panelLeft - insetX;

    // Vertical: prefer below the anchor; flip above when it would overflow the
    // screen bottom and there is room above.
    const int panelTopBelow = anchor.bottom() + gap;
    const bool above
        = (panelTopBelow + panelH > scr.bottom()) && (anchor.top() - gap - panelH >= scr.top());

    const int panelTop = above ? (anchor.top() - gap - panelH) : panelTopBelow;
    m_placedAbove = above;

    // Grow pivot = the panel corner nearest the button (right edge always, since
    // the panel is right-aligned to the anchor; bottom when flipped above).
    m_growPivot = QPoint(panel.right(), above ? panel.bottom() : panel.top());

    return QPoint(wx, panelTop - insetTop);
}

bool BaseContextMenu::isClickInsideContent(const QPoint& globalPos) const
{
    QRect content = contentRect();
    QPoint localPos = mapFromGlobal(globalPos);
    return content.contains(localPos);
}

void BaseContextMenu::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    drawContent(painter);
}

void BaseContextMenu::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updateSlideMetrics();
    syncVisualPosition();
}

bool BaseContextMenu::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);
    if (!isVisible() || m_isRepositioning) {
        return false;
    }

    if (event->type() == QEvent::ApplicationDeactivate) {
        hideAnimated();
        return false;
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);

        if (mouseEvent->button() == Qt::RightButton) {
            return false;
        }

        QPoint globalPos = mouseEvent->globalPosition().toPoint();

        if (isClickInsideContent(globalPos)) {
            return false;
        }

        // A press on the trigger button must not auto-close the menu here: let the
        // button's own handler perform a clean open/close toggle instead.
        if (m_hasAnchor && m_anchorRectGlobal.contains(globalPos)) {
            return false;
        }

        hideAnimated();
        return false;
    }

    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            hideAnimated();
            return true;
        }
    }

    return false;
}

void BaseContextMenu::paintMenuDropShadow(QPainter& painter, const QRectF& rect, qreal radius)
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    painter.setPen(Qt::NoPen);
    constexpr int kLayers = 7;
    for (int pass = 0; pass < kLayers; ++pass) {
        const qreal u = (pass + 1) / qreal(kLayers);
        const qreal spread = theme.scaled(0.7 + 10.0 * u);
        const qreal drop = theme.scaled(1.2 + 8.0 * u);
        // ~30% lighter than before (multiply alpha by 0.7)
        const int alpha = qBound(2, static_cast<int>((9 + 16 * (1.0 - u * 0.9)) * 0.7), 25);
        QRectF sh = rect.translated(0, drop * 0.22);
        sh.adjust(-spread, -spread, spread, spread + drop * 0.5);
        painter.setBrush(QColor(0, 0, 0, alpha));
        const qreal rr = radius + spread * 0.5;
        painter.drawRoundedRect(sh, rr, rr);
    }
}

StandardContextMenu::StandardContextMenu(QWidget* parent)
    : BaseContextMenu(parent)
{
    m_contentWidget = new QWidget(this);
    m_contentWidget->setAttribute(Qt::WA_TranslucentBackground);
    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(m_contentMargins);
    m_contentLayout->setSpacing(4);
}

QRect StandardContextMenu::contentRect() const
{
    if (usesAttachedTopBarSurface()) {
        return attachedBodyRect().toAlignedRect();
    }
    return m_panelRect;
}

void StandardContextMenu::drawContent(QPainter& painter)
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    if (usesAttachedTopBarSurface()) {
        const QRectF body = attachedBodyRect();
        if (!body.isValid()) {
            return;
        }

        const int outerCornerRadius
            = theme.scaled(ruwa::ui::painting::kAttachedOuterCornerRadiusBase);
        const int shadowExtent = theme.scaled(ruwa::ui::painting::kAttachedShadowExtentBase);
        const int shadowSideExtent
            = theme.scaled(ruwa::ui::painting::kAttachedShadowSideExtentBase);
        constexpr int radius = ruwa::ui::painting::kAttachedCornerRadius;

        const QPainterPath shape
            = ruwa::ui::painting::attachedPopupPath(body, outerCornerRadius, radius);

        const QRectF shadowBody = body.adjusted(outerCornerRadius, 0.0, -outerCornerRadius, 0.0);
        ruwa::ui::painting::drawAttachedPopupShadow(
            painter, shadowBody, shadowSideExtent, shadowExtent, colors.shadow(255), colors.isDark);

        painter.setPen(Qt::NoPen);
        QLinearGradient fillGradient(body.topLeft(), body.bottomLeft());
        fillGradient.setColorAt(0.0, colors.surface);
        fillGradient.setColorAt(
            1.0, ruwa::ui::core::ThemeColors::adjustBrightness(colors.surface, 100.0 / 102));
        painter.setBrush(fillGradient);
        painter.drawPath(shape);

        const QRectF borderRect = body.adjusted(0.5, 0.0, -0.5, -0.5);
        const QPainterPath borderPath = ruwa::ui::painting::attachedPopupBorderPath(
            borderRect, outerCornerRadius, radius - 0.5);

        QLinearGradient borderGradient(borderRect.topLeft(), borderRect.bottomLeft());
        borderGradient.setColorAt(0.0, colors.border);
        borderGradient.setColorAt(
            1.0, ruwa::ui::core::ThemeColors::withAlpha(colors.border, colors.border.alpha() / 2));

        QPen borderPen;
        borderPen.setBrush(borderGradient);
        borderPen.setWidthF(1.0);
        borderPen.setCosmetic(true);
        borderPen.setCapStyle(Qt::SquareCap);
        borderPen.setJoinStyle(Qt::RoundJoin);

        painter.setPen(borderPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(borderPath);
        return;
    }

    const int radius = theme.scaled(12);

    const QRectF rect = m_panelRect.adjusted(0.5, 0.5, -0.5, -0.5);
    if (rect.isEmpty()) {
        return;
    }

    paintMenuDropShadow(painter, rect, radius);

    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.surfaceElevated());
    painter.drawRoundedRect(rect, radius, radius);

    QLinearGradient borderGradient(rect.topLeft(), rect.bottomLeft());
    borderGradient.setColorAt(0.0, colors.borderSubtleHover());
    borderGradient.setColorAt(1.0, colors.borderSubtle());
    painter.setPen(QPen(borderGradient, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect, radius, radius);

    // Inset highlight (~ HTML: 0 0 0 1px #ffffff06 inset)
    const QRectF inner = rect.adjusted(1.5, 1.5, -1.5, -1.5);
    if (inner.width() > 1 && inner.height() > 1) {
        painter.setPen(QPen(QColor(255, 255, 255, 6), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(inner, qMax(1, radius - 1), qMax(1, radius - 1));
    }
}

void StandardContextMenu::onContextChanged()
{
    rebuildStandardMenu();
    updateMenuSize();
}

void StandardContextMenu::setContentMargins(const QMargins& margins)
{
    m_contentMargins = margins;
    if (m_contentLayout) {
        m_contentLayout->setContentsMargins(m_contentMargins);
    }
    updateMenuSize();
}

void StandardContextMenu::applyPresentationLayout(qreal progress)
{
    if (m_sizeFull.isEmpty()) {
        return;
    }

    if (usesAttachedTopBarSurface()) {
        const int revealHeight = qMax(0, qRound(m_sizeFull.height() * qBound(0.0, progress, 1.0)));
        setFixedSize(m_sizeFull.width(), revealHeight);
        m_panelRect = m_panelFull;
        m_contentWidget->setGeometry(m_panelRect);
        return;
    }

    const qreal u = 0.92 + 0.08 * progress;
    if (progress >= 1.0 - 1e-5) {
        setFixedSize(m_sizeFull);
        m_panelRect = m_panelFull;
        m_contentWidget->setGeometry(m_panelRect);
        return;
    }
    const int w = qMax(1, qRound(m_sizeFull.width() * u));
    const int h = qMax(1, qRound(m_sizeFull.height() * u));
    setFixedSize(w, h);
    m_panelRect = QRect(qRound(m_panelFull.x() * u), qRound(m_panelFull.y() * u),
        qMax(1, qRound(m_panelFull.width() * u)), qMax(1, qRound(m_panelFull.height() * u)));
    m_contentWidget->setGeometry(m_panelRect);
}

qreal StandardContextMenu::presentationContentScale() const
{
    // Mirror the scale applied in applyPresentationLayout() (non-attached path).
    const qreal p = showProgress();
    if (p >= 1.0 - 1e-5) {
        return 1.0;
    }
    return 0.92 + 0.08 * p;
}

QSize StandardContextMenu::expandMenuContentHint(const QSize& hint) const
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    return hint.expandedTo(QSize(theme.scaled(180), theme.scaled(90)));
}

QRectF StandardContextMenu::attachedBodyRect() const
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int shadowExtent = theme.scaled(ruwa::ui::painting::kAttachedShadowExtentBase);
    const int shadowSideExtent = theme.scaled(ruwa::ui::painting::kAttachedShadowSideExtentBase);
    if (height() <= shadowExtent + 1 || width() <= shadowSideExtent * 2 + 1) {
        return {};
    }

    return QRectF(rect()).adjusted(
        shadowSideExtent, 0.0, -shadowSideExtent - 0.5, -shadowExtent - 0.5);
}

void StandardContextMenu::updateMenuSize()
{
    if (!m_contentWidget || !m_contentLayout) {
        return;
    }

    m_contentLayout->activate();
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const QSize hint = expandMenuContentHint(m_contentWidget->sizeHint());

    if (usesAttachedTopBarSurface()) {
        const int shadowSide = theme.scaled(ruwa::ui::painting::kAttachedShadowSideExtentBase);
        const int outerCornerRadius
            = theme.scaled(ruwa::ui::painting::kAttachedOuterCornerRadiusBase);
        const int shadowBottom = theme.scaled(ruwa::ui::painting::kAttachedShadowExtentBase);

        m_panelRect = QRect(shadowSide + outerCornerRadius, 0, hint.width(), hint.height());
        m_contentWidget->setGeometry(m_panelRect);
        setFixedSize(QSize(
            shadowSide * 2 + outerCornerRadius * 2 + hint.width(), hint.height() + shadowBottom));
        m_panelFull = m_panelRect;
        m_sizeFull = size();
        updateSlideMetrics();
        applyPresentationLayout(showProgress());
        syncVisualPosition();
        update();
        return;
    }

    const int side = theme.scaled(22);
    const int top = theme.scaled(10);
    const int bottom = theme.scaled(42);

    m_panelRect = QRect(side, top, hint.width(), hint.height());
    m_contentWidget->setGeometry(m_panelRect);
    setFixedSize(QSize(side * 2 + hint.width(), top + hint.height() + bottom));
    m_panelFull = m_panelRect;
    m_sizeFull = size();
    updateSlideMetrics();
    applyPresentationLayout(showProgress());
    syncVisualPosition();
    update();
}

BaseStyledWidget* StandardContextMenu::addStandardMenuActionRow(
    const QIcon& icon, const QString& text, bool danger, QVBoxLayout* addToLayout)
{
    using namespace ruwa::ui::core;

    auto style = WidgetStyle::defaultButtonStyle();
    style.name = QStringLiteral("StandardContextMenuAction");
    style.metrics.fixedHeight = true;
    style.metrics.fixedWidth = false;
    style.metrics.baseHeight = 30;
    style.metrics.baseCornerRadius = 4;
    style.background.color = ColorSource::Transparent;
    style.border.enabled = false;
    style.activeBackground.enabled = false;
    style.activeBorder.enabled = false;
    style.hover.enabled = true;
    if (danger) {
        style.hover.color = ColorSource::Error;
        style.hover.maxOpacity = 0.15;
        style.content.textColor = ColorSource::Error;
        style.content.textHoverColor = ColorSource::Error;
        style.content.textActiveColor = ColorSource::Error;
    } else {
        style.hover.color = ColorSource::OverlayHover;
        style.hover.maxOpacity = 1.0;
        style.content.textColor = ColorSource::Text;
        style.content.textHoverColor = style.content.textColor;
        style.content.textActiveColor = style.content.textColor;
    }
    style.press.enabled = true;
    style.press.color = ColorSource::OverlayHover;
    style.content.iconPosition = icon.isNull() ? IconPosition::None : IconPosition::Left;
    style.content.textAlignment = ContentAlignment::Left;
    style.content.baseIconSize = 16;
    style.content.baseIconTextGap = 6;
    style.content.basePadding = { 10, 0, 10, 0 };
    style.content.colorizeIcon = true;

    auto* button = new BaseStyledWidget(style, contentWidget());
    button->setIcon(icon);
    button->setText(text);
    button->setProperty("danger", danger);
    if (addToLayout) {
        addToLayout->addWidget(button);
    } else {
        contentLayout()->addWidget(button);
    }
    return button;
}

CustomContextMenu::CustomContextMenu(QWidget* parent)
    : BaseContextMenu(parent)
{
}

QRect CustomContextMenu::contentRect() const
{
    return m_contentRect;
}

void CustomContextMenu::drawContent(QPainter& painter)
{
    Q_UNUSED(painter);
}

void CustomContextMenu::onContextChanged()
{
    rebuildCustomMenu();
    updateCustomSize();
}

void CustomContextMenu::setCustomContentRoot(QWidget* rootWidget)
{
    m_customContentRoot = rootWidget;
    if (m_customContentRoot) {
        m_customContentRoot->setParent(this);
    }
    updateCustomSize();
}

void CustomContextMenu::applyPresentationLayout(qreal progress)
{
    if (m_sizeFull.isEmpty() || !m_customContentRoot) {
        return;
    }
    const qreal u = 0.92 + 0.08 * progress;
    if (progress >= 1.0 - 1e-5) {
        setFixedSize(m_sizeFull);
        m_contentRect = m_contentRectFull;
        m_customContentRoot->setGeometry(m_contentRect);
        return;
    }
    const int w = qMax(1, qRound(m_sizeFull.width() * u));
    const int h = qMax(1, qRound(m_sizeFull.height() * u));
    setFixedSize(w, h);
    m_contentRect = QRect(qRound(m_contentRectFull.x() * u), qRound(m_contentRectFull.y() * u),
        qMax(1, qRound(m_contentRectFull.width() * u)),
        qMax(1, qRound(m_contentRectFull.height() * u)));
    m_customContentRoot->setGeometry(m_contentRect);
}

void CustomContextMenu::updateCustomSize()
{
    if (!m_customContentRoot) {
        return;
    }

    const QSize hint = m_customContentRoot->sizeHint().expandedTo(QSize(40, 40));
    m_contentRect = QRect(QPoint(0, 0), hint);
    m_customContentRoot->setGeometry(m_contentRect);
    setFixedSize(hint);
    m_contentRectFull = m_contentRect;
    m_sizeFull = size();
    updateSlideMetrics();
    applyPresentationLayout(showProgress());
    syncVisualPosition();
    update();
}

} // namespace ruwa::ui::widgets
