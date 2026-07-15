// SPDX-License-Identifier: MPL-2.0

// MessagePopup.cpp
#include "MessagePopup.h"
#include "OverlayContainer.h"
#include "shared/widgets/CapsuleButton.h"
#include "shared/style/PaintingUtils.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QAbstractButton>
#include <QApplication>
#include <QImage>
#include <QLabel>
#include <QtMath>
#include <cmath>

namespace ruwa::ui::widgets {

namespace {

// Attached-popup silhouette, border and shadow are shared with MenuPopup — see
// ruwa::ui::painting in shared/style/PaintingUtils.h.
using ruwa::ui::painting::attachedPopupBorderPath;
using ruwa::ui::painting::attachedPopupPath;
using ruwa::ui::painting::drawAttachedPopupShadow;
using ruwa::ui::painting::kAttachedCornerRadius;
using ruwa::ui::painting::kAttachedOuterCornerRadiusBase;
using ruwa::ui::painting::kAttachedShadowExtentBase;
using ruwa::ui::painting::kAttachedShadowSideExtentBase;

constexpr qreal kPopupGlowContourPhase = 0.68;
constexpr qreal kHalfPi = 1.57079632679489661923;

void addPopupGlowSide(QPainterPath& path, const QRectF& rect, qreal sideInset, qreal radius,
    bool rightSide, qreal progress)
{
    if (progress <= 0.0) {
        return;
    }

    const qreal topR = qMin(sideInset, rect.height() / 2.0);
    const qreal bottomR = qMin(radius, qMin(rect.width(), rect.height()) / 2.0);
    const qreal left = rect.left() + sideInset;
    const qreal right = rect.right() - sideInset;
    const qreal centerX = (left + right) / 2.0;
    const qreal sideX = rightSide ? right : left;
    const qreal bottomArcEndX = rightSide ? right - bottomR : left + bottomR;

    const qreal bottomStraightLen = qAbs(bottomArcEndX - centerX);
    const qreal bottomArcLen = bottomR * kHalfPi;
    const qreal verticalLen = qMax(0.0, rect.height() - topR - bottomR);
    const qreal topArcLen = topR * kHalfPi;
    const qreal totalLen = bottomStraightLen + bottomArcLen + verticalLen + topArcLen;
    qreal remaining = totalLen * qBound(0.0, progress, 1.0);

    path.moveTo(centerX, rect.bottom());

    const auto consume = [&remaining](qreal segmentLen) {
        const qreal used = qMin(remaining, segmentLen);
        remaining -= used;
        return used;
    };

    const qreal bottomStraightUsed = consume(bottomStraightLen);
    if (bottomStraightLen > 0.001) {
        const qreal t = bottomStraightUsed / bottomStraightLen;
        const qreal x = centerX + (bottomArcEndX - centerX) * t;
        path.lineTo(x, rect.bottom());
    }
    if (remaining <= 0.0) {
        return;
    }

    const qreal bottomArcUsed = consume(bottomArcLen);
    if (bottomArcLen > 0.001) {
        const qreal t = bottomArcUsed / bottomArcLen;
        const qreal controlX = rightSide ? sideX : sideX;
        const QPointF arcEnd(bottomArcEndX + (sideX - bottomArcEndX) * t,
            rect.bottom() + (rect.bottom() - bottomR - rect.bottom()) * t);
        path.quadTo(QPointF(controlX, rect.bottom()), arcEnd);
    }
    if (remaining <= 0.0) {
        return;
    }

    const qreal verticalUsed = consume(verticalLen);
    if (verticalLen > 0.001) {
        const qreal t = verticalUsed / verticalLen;
        const qreal y
            = (rect.bottom() - bottomR) + ((rect.top() + topR) - (rect.bottom() - bottomR)) * t;
        path.lineTo(sideX, y);
    }
    if (remaining <= 0.0) {
        return;
    }

    const qreal topArcUsed = consume(topArcLen);
    if (topArcLen > 0.001) {
        const qreal t = topArcUsed / topArcLen;
        const qreal endX = sideX + ((rightSide ? rect.right() : rect.left()) - sideX) * t;
        const qreal endY = (rect.top() + topR) + (rect.top() - (rect.top() + topR)) * t;
        path.quadTo(QPointF(sideX, rect.top()), QPointF(endX, endY));
    }
}

QPainterPath popupGlowPath(const QRectF& rect, qreal sideInset, qreal radius, qreal progress)
{
    QPainterPath path;
    addPopupGlowSide(path, rect, sideInset, radius, false, progress);
    addPopupGlowSide(path, rect, sideInset, radius, true, progress);
    return path;
}

} // namespace

// ============================================================================
// MessagePopup Implementation
// ============================================================================

MessagePopup::MessagePopup(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Widget);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);

    hide();

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(16, 16, 16, 16);
    m_layout->setSpacing(12);

    m_heightAnim = new QPropertyAnimation(this, "revealHeight", this);
    m_heightAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_borderGlowAnim = new QPropertyAnimation(this, "borderGlowProgress", this);
    m_borderGlowAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_timebarAnim = new QPropertyAnimation(this, "timebarProgress", this);
    m_timebarAnim->setEasingCurve(QEasingCurve::Linear);
}

MessagePopup::~MessagePopup()
{
    qDeleteAll(m_buttonWidgets);
}

void MessagePopup::setMessage(const QString& text)
{
    m_message = text;
}

void MessagePopup::setImage(const QImage& image)
{
    m_image = image.isNull() ? QPixmap() : QPixmap::fromImage(image);
}

void MessagePopup::setAutoHideDuration(int ms)
{
    m_autoHideDuration = qMax(0, ms);
}

void MessagePopup::setButtons(const QList<MessageButton>& buttons)
{
    m_buttons = buttons;
}

void MessagePopup::setPopupWidth(int width)
{
    m_width = width;
}

void MessagePopup::showPopup()
{
    if (!parentWidget())
        return;

    const QString sig = contentSignature();
    const bool skipRebuild
        = !m_displayedContentSignature.isEmpty() && sig == m_displayedContentSignature;

    if (!skipRebuild) {
        rebuildContent();
        m_displayedContentSignature = sig;
    } else {
        updateButtonCallbacks();
    }

    m_isVisible = true;
    QPoint targetPos = calculatePosition();
    move(targetPos);

    if (skipRebuild) {
        if (m_heightAnim) {
            m_heightAnim->stop();
        }
        setRevealHeight(m_targetHeight);
        show();
        raise();
    } else {
        setRevealHeight(0);
        show();
        raise();
        startShowAnimation();
    }

    m_borderGlowProgress = 0.0;
    m_borderGlowAnim->stop();
    m_borderGlowAnim->setDuration(600);
    m_borderGlowAnim->setStartValue(0.0);
    m_borderGlowAnim->setEndValue(1.0);
    m_borderGlowAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_borderGlowAnim->start();

    // Auto-hide for toast-style notifications (no buttons) - timebar shrinks, hide when done
    if (m_timebarAnim) {
        m_timebarAnim->stop();
        disconnect(m_timebarAnim, &QPropertyAnimation::finished, this, nullptr);
    }
    if (m_autoHideDuration > 0 && m_buttons.isEmpty()) {
        m_timebarProgress = 1.0;
        m_timebarAnim->setDuration(m_autoHideDuration);
        m_timebarAnim->setStartValue(1.0);
        m_timebarAnim->setEndValue(0.0);
        connect(
            m_timebarAnim, &QPropertyAnimation::finished, this, [this]() { hidePopup(); },
            Qt::SingleShotConnection);
        m_timebarAnim->start();
    } else {
        m_timebarProgress = 1.0;
    }

    emit shown();
}

void MessagePopup::hidePopup()
{
    if (!m_isVisible)
        return;

    if (m_timebarAnim) {
        m_timebarAnim->stop();
        disconnect(m_timebarAnim, &QPropertyAnimation::finished, this, nullptr);
    }

    emit aboutToHide();
    startHideAnimation();
}

void MessagePopup::setBorderGlowProgress(qreal progress)
{
    m_borderGlowProgress = qBound(0.0, progress, 1.0);
    update();
    emit contentChanged();
}

void MessagePopup::setTimebarProgress(qreal progress)
{
    m_timebarProgress = qBound(0.0, progress, 1.0);
    update();
}

void MessagePopup::setRevealHeight(int height)
{
    const int boundedHeight = qBound(0, height, qMax(0, m_targetHeight));
    if (m_revealHeight == boundedHeight && this->height() == boundedHeight) {
        return;
    }

    m_revealHeight = boundedHeight;
    setFixedHeight(m_revealHeight);
    update();
    emit contentChanged();
}

QRectF MessagePopup::bodyRect() const
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int shadowExtent = theme.scaled(kAttachedShadowExtentBase);
    const int shadowSideExtent = theme.scaled(kAttachedShadowSideExtentBase);
    if (height() <= shadowExtent + 1 || width() <= shadowSideExtent * 2 + 1) {
        return {};
    }

    return QRectF(rect()).adjusted(
        shadowSideExtent, 0.0, -shadowSideExtent - 0.5, -shadowExtent - 0.5);
}

void MessagePopup::rebuildContent()
{
    // Clear old content
    for (QWidget* w : m_buttonWidgets) {
        if (w) {
            w->setParent(nullptr);
            delete w;
        }
    }
    m_buttonWidgets.clear();

    while (m_layout->count() > 0) {
        QLayoutItem* item = m_layout->takeAt(0);
        if (QWidget* w = item->widget()) {
            w->setParent(nullptr);
            delete w;
        } else if (QLayout* sub = item->layout()) {
            while (sub->count() > 0) {
                QLayoutItem* subItem = sub->takeAt(0);
                if (QWidget* sw = subItem->widget()) {
                    sw->setParent(nullptr);
                    delete sw;
                }
                delete subItem;
            }
        }
        delete item;
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    const int outerCornerRadius = theme.scaled(kAttachedOuterCornerRadiusBase);
    const int shadowExtent = theme.scaled(kAttachedShadowExtentBase);
    const int shadowSideExtent = theme.scaled(kAttachedShadowSideExtentBase);

    // The existing bottom plate strip already houses the timebar (drawn at the body
    // bottom in paintEvent), so no extra reserve is needed for the auto-hide case.
    m_layout->setContentsMargins(16 + outerCornerRadius + shadowSideExtent, 16,
        16 + outerCornerRadius + shadowSideExtent, 16 + shadowExtent);

    // Optional image (e.g. for "image copied" toast) - clip to rounded corners like
    // RecentProjectCard
    constexpr int maxImageSize = 240;
    constexpr int imageRadius = 8;
    if (!m_image.isNull()) {
        QPixmap scaled = m_image.scaled(
            maxImageSize, maxImageSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const int r = qMin(imageRadius, qMin(scaled.width(), scaled.height()) / 2);
        QPixmap rounded(scaled.size());
        rounded.fill(Qt::transparent);
        QPainter p(&rounded);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath path;
        path.addRoundedRect(QRect(0, 0, scaled.width(), scaled.height()), r, r);
        p.setClipPath(path);
        p.drawPixmap(0, 0, scaled);
        p.end();

        QLabel* imageLabel = new QLabel(this);
        imageLabel->setPixmap(rounded);
        imageLabel->setAlignment(Qt::AlignCenter);
        m_layout->addWidget(imageLabel, 0, Qt::AlignHCenter);
    }

    // Message text - use QLabel for word wrap
    QLabel* label = new QLabel(m_message, this);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    QFont textFont = label->font();
    textFont.setPointSize(theme.scaledFontSize(9));
    label->setFont(textFont);
    label->setStyleSheet(QString("color: %1;").arg(colors.text.name()));
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    m_layout->addWidget(label);

    // Buttons row
    if (!m_buttons.isEmpty()) {
        QHBoxLayout* btnLayout = new QHBoxLayout();
        btnLayout->setSpacing(8);
        btnLayout->addStretch();

        for (int i = 0; i < m_buttons.size(); ++i) {
            const auto& btn = m_buttons[i];
            auto* button = new CapsuleButton(btn.text,
                btn.primary ? CapsuleButton::Variant::Primary : CapsuleButton::Variant::Secondary,
                this);
            button->setCheckable(false);
            button->setBaseMinimumWidth(80);
            button->setBannerBaseHeight(32);
            button->syncSizeToText();

            auto callback = btn.callback;
            connect(button, &QAbstractButton::clicked, this, [this, callback]() {
                if (callback)
                    callback();
                hidePopup();
            });

            btnLayout->addWidget(button);
            m_buttonWidgets.append(button);
        }

        m_layout->addLayout(btnLayout);
    }

    m_layout->invalidate();
    m_layout->activate();
    setFixedWidth(m_width + outerCornerRadius * 2 + shadowSideExtent * 2);
    // Force layout to compute proper sizes; sizeHint can be wrong when widget was previously hidden
    adjustSize();
    const int minHeight = 80 + shadowExtent;
    int h = m_layout->sizeHint().height();
    m_targetHeight = qMax(h, minHeight);
    setRevealHeight(m_targetHeight);
}

void MessagePopup::updateButtonCallbacks()
{
    for (int i = 0; i < m_buttonWidgets.size() && i < m_buttons.size(); ++i) {
        if (auto* btn = qobject_cast<QAbstractButton*>(m_buttonWidgets[i])) {
            disconnect(btn, &QAbstractButton::clicked, this, nullptr);
            auto callback = m_buttons[i].callback;
            connect(btn, &QAbstractButton::clicked, this, [this, callback]() {
                if (callback)
                    callback();
                hidePopup();
            });
        }
    }
}

QString MessagePopup::contentSignature() const
{
    QString sig = m_message + "|" + QString::number(m_width);
    if (!m_image.isNull()) {
        sig += "|img:" + QString::number(m_image.width()) + "x" + QString::number(m_image.height());
    }
    for (const auto& b : m_buttons) {
        sig += "|" + b.text + (b.primary ? ":1" : ":0");
    }
    return sig;
}

QPoint MessagePopup::calculatePosition() const
{
    if (!parentWidget())
        return QPoint();

    QWidget* overlay = parentWidget();
    int x = (overlay->width() - width()) / 2;
    int y = TOP_OFFSET;
    if (auto* oc = qobject_cast<OverlayContainer*>(overlay)) {
        y = oc->messagePopupAnchorY();
    }

    return QPoint(qMax(0, x), y);
}

void MessagePopup::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int outerCornerRadius = theme.scaled(kAttachedOuterCornerRadiusBase);
    const int shadowExtent = theme.scaled(kAttachedShadowExtentBase);
    const int shadowSideExtent = theme.scaled(kAttachedShadowSideExtentBase);

    QRectF rect = bodyRect();
    if (!rect.isValid()) {
        return;
    }

    constexpr int radius = kAttachedCornerRadius;
    const QPainterPath shape = attachedPopupPath(rect, outerCornerRadius, radius);

    // The visible body is inset by outerCornerRadius on the sides (see attachedPopupPath),
    // so the shadow must hug that inset rect — not the raw bodyRect — to avoid a side gap.
    const QRectF shadowBody = rect.adjusted(outerCornerRadius, 0.0, -outerCornerRadius, 0.0);
    drawAttachedPopupShadow(
        painter, shadowBody, shadowSideExtent, shadowExtent, colors.shadow(255), colors.isDark);

    painter.setPen(Qt::NoPen);
    QLinearGradient fillGradient(rect.topLeft(), rect.bottomLeft());
    fillGradient.setColorAt(0.0, colors.surface);
    fillGradient.setColorAt(
        1.0, ruwa::ui::core::ThemeColors::adjustBrightness(colors.surface, 100.0 / 102));
    painter.setBrush(fillGradient);
    painter.drawPath(shape);

    // Border path (reused by glow effect below)
    const QRectF borderRect = rect.adjusted(0.5, 0.0, -0.5, -0.5);
    QPainterPath borderPath = attachedPopupBorderPath(borderRect, outerCornerRadius, radius - 0.5);
    {
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
    }

    // Timebar for auto-hide toasts: thick accent line at the body bottom, shrinks over time
    if (m_autoHideDuration > 0 && m_buttons.isEmpty() && m_timebarProgress > 0.001) {
        const int barHeight = 4;
        const qreal barMargin = outerCornerRadius + 12;
        const qreal barMaxWidth = rect.width() - 2 * barMargin;
        const qreal barWidth = qMax(0.0, m_timebarProgress * barMaxWidth);
        if (barWidth > 0.0) {
            const qreal barY = rect.bottom() - barHeight - 8;
            QRectF barRect(rect.left() + barMargin, barY, barWidth, barHeight);
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.primary);
            painter.drawRoundedRect(barRect, barHeight / 2.0, barHeight / 2.0);
        }
    }
    painter.setBrush(Qt::NoBrush); // Reset: drawPath for glow fills with brush otherwise

    // Accent glow continues from the TopBar seam down the attached side/bottom outline.
    // First phase starts at the popup bottom center and follows the popup outline outward.
    if (m_borderGlowProgress > 0.001) {
        const int alpha = static_cast<int>(220 * (1.0 - m_borderGlowProgress));
        QColor accentColor = ruwa::ui::core::ThemeColors::withAlpha(colors.primary, alpha);
        const qreal popupContourProgress
            = qBound(0.0, m_borderGlowProgress / kPopupGlowContourPhase, 1.0);

        QPen glowPen;
        glowPen.setColor(accentColor);
        glowPen.setWidth(2);
        glowPen.setCapStyle(Qt::RoundCap);
        glowPen.setJoinStyle(Qt::RoundJoin);

        painter.setPen(glowPen);
        painter.drawPath(
            popupGlowPath(borderRect, outerCornerRadius, radius - 0.5, popupContourProgress));
    }
}

void MessagePopup::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
}

void MessagePopup::startShowAnimation()
{
    if (!m_heightAnim) {
        setRevealHeight(m_targetHeight);
        return;
    }

    disconnect(m_heightAnim, &QPropertyAnimation::finished, this, nullptr);

    m_heightAnim->stop();
    m_heightAnim->setDuration(SHOW_DURATION);
    m_heightAnim->setStartValue(m_revealHeight);
    m_heightAnim->setEndValue(m_targetHeight);
    m_heightAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_heightAnim->start();
}

void MessagePopup::startHideAnimation()
{
    m_isVisible = false;
    m_isHiding = true;

    if (m_heightAnim) {
        m_heightAnim->stop();
    }
    m_borderGlowAnim->stop();
    setBorderGlowProgress(0.0);

    if (!m_heightAnim) {
        if (m_isHiding) {
            m_isHiding = false;
            m_displayedContentSignature.clear();
            setRevealHeight(0);
            hide();
            emit hidden();
        }
        return;
    }

    disconnect(m_heightAnim, &QPropertyAnimation::finished, this, nullptr);
    connect(m_heightAnim, &QPropertyAnimation::finished, this, [this]() {
        if (!m_isHiding) {
            return;
        }
        m_isHiding = false;
        m_displayedContentSignature.clear();
        setRevealHeight(0);
        hide();
        emit hidden();
    });

    m_heightAnim->setDuration(HIDE_DURATION);
    m_heightAnim->setStartValue(m_revealHeight);
    m_heightAnim->setEndValue(0);
    m_heightAnim->setEasingCurve(QEasingCurve::InCubic);
    m_heightAnim->start();
}

} // namespace ruwa::ui::widgets
