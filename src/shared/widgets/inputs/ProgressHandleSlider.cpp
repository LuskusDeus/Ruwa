// SPDX-License-Identifier: MPL-2.0

#include "ProgressHandleSlider.h"

#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"

#include <QEnterEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTabletEvent>
#include <QVariantAnimation>

namespace ruwa::ui::widgets {

ProgressHandleSlider::ProgressHandleSlider(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setTabletTracking(true);
    setAttribute(Qt::WA_Hover, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_hoverAnimation = new QVariantAnimation(this);
    m_hoverAnimation->setDuration(160);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(
        m_hoverAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_hoverProgress = qBound(0.0, value.toReal(), 1.0);
            update();
        });

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &ProgressHandleSlider::onThemeChanged);
}

void ProgressHandleSlider::setRange(int minimum, int maximum)
{
    if (minimum > maximum) {
        qSwap(minimum, maximum);
    }

    if (m_minimum == minimum && m_maximum == maximum) {
        return;
    }

    m_minimum = minimum;
    m_maximum = maximum;
    setValue(qBound(m_minimum, m_value, m_maximum));
    update();
}

void ProgressHandleSlider::setValue(int value)
{
    const int boundedValue = qBound(m_minimum, value, m_maximum);
    if (m_value == boundedValue) {
        return;
    }

    m_value = boundedValue;
    emit valueChanged(m_value);
    update();
}

void ProgressHandleSlider::setOrientation(Qt::Orientation orientation)
{
    if (m_orientation == orientation) {
        return;
    }
    m_orientation = orientation;
    update();
}

void ProgressHandleSlider::setFillInset(qreal inset)
{
    const qreal bounded = inset < 0.0 ? -1.0 : inset;
    if (qFuzzyCompare(m_fillInset + 2.0, bounded + 2.0)) {
        return;
    }
    m_fillInset = bounded;
    update();
}

void ProgressHandleSlider::setBackgroundOpacity(qreal surfaceOpacity, qreal trackOpacity)
{
    const qreal boundedSurface = qBound(0.0, surfaceOpacity, 1.0);
    const qreal boundedTrack = qBound(0.0, trackOpacity, 1.0);
    if (qFuzzyCompare(m_surfaceOpacity, boundedSurface)
        && qFuzzyCompare(m_trackOpacity, boundedTrack)) {
        return;
    }

    m_surfaceOpacity = boundedSurface;
    m_trackOpacity = boundedTrack;
    update();
}

void ProgressHandleSlider::setProgressFillOpacity(qreal opacity)
{
    const qreal bounded = qBound(0.0, opacity, 1.0);
    if (qFuzzyCompare(m_progressStartOpacity, bounded)
        && qFuzzyCompare(m_progressFinishOpacity, bounded)) {
        return;
    }

    m_progressStartOpacity = bounded;
    m_progressFinishOpacity = bounded;
    update();
}

qreal ProgressHandleSlider::progressFillOpacity() const
{
    if (qFuzzyCompare(m_progressStartOpacity, m_progressFinishOpacity)) {
        return m_progressStartOpacity;
    }

    return (m_progressStartOpacity + m_progressFinishOpacity) * 0.5;
}

void ProgressHandleSlider::setShowValueText(bool show)
{
    if (m_showValueText == show) {
        return;
    }
    m_showValueText = show;
    update();
}

void ProgressHandleSlider::setValueDisplayMode(ValueDisplayMode mode)
{
    if (m_valueDisplayMode == mode) {
        return;
    }
    m_valueDisplayMode = mode;
    update();
}

void ProgressHandleSlider::setValueTextPrefix(const QString& prefix)
{
    if (m_valueTextPrefix == prefix) {
        return;
    }
    m_valueTextPrefix = prefix;
    update();
}

void ProgressHandleSlider::setValueTextSuffix(const QString& suffix)
{
    if (m_valueTextSuffix == suffix) {
        return;
    }
    m_valueTextSuffix = suffix;
    update();
}

void ProgressHandleSlider::setCustomDisplayText(const QString& text)
{
    if (m_customDisplayText == text) {
        return;
    }
    m_customDisplayText = text;
    update();
}

void ProgressHandleSlider::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const auto& tm = ruwa::ui::core::ThemeManager::instance();

    const QRectF outer = contentRect();
    const qreal outerRadius = qMax<qreal>(2.0, tm.scaled(3.0));

    p.setPen(Qt::NoPen);
    QColor surface = colors.surface;
    surface.setAlphaF(surface.alphaF() * m_surfaceOpacity);
    p.setBrush(surface);
    p.drawRoundedRect(outer, outerRadius, outerRadius);

    if (m_hoverProgress > 0.001) {
        QColor overlay = colors.overlayHover();
        overlay.setAlphaF(overlay.alphaF() * (0.2 + 0.4 * m_hoverProgress));
        p.setBrush(overlay);
        p.drawRoundedRect(outer, outerRadius, outerRadius);
    }

    const QRectF borderRect = outer.adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath borderPath;
    borderPath.addRoundedRect(
        borderRect, qMax<qreal>(1.5, outerRadius - 0.6), qMax<qreal>(1.5, outerRadius - 0.6));

    QColor top = colors.borderSubtleHover();
    QColor bottom = colors.borderSubtle();
    if (m_hoverProgress > 0.001) {
        QColor accent = colors.primary;
        accent.setAlpha(120);
        top = ruwa::ui::core::ThemeColors::interpolate(top, accent, 0.35 * m_hoverProgress);
    }

    QLinearGradient borderGradient(borderRect.topLeft(), borderRect.bottomLeft());
    borderGradient.setColorAt(0.0, top);
    borderGradient.setColorAt(1.0, bottom);

    QPen borderPen;
    borderPen.setBrush(borderGradient);
    borderPen.setWidthF(1.0);
    borderPen.setCosmetic(true);
    p.setPen(borderPen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(borderPath);

    const QRectF track = trackRect(outer);
    const qreal trackRadius = qMax<qreal>(1.5, tm.scaled(2.0));

    QColor trackBg = colors.surfaceAlt;
    trackBg.setAlphaF(trackBg.alphaF() * m_trackOpacity);
    p.setPen(Qt::NoPen);
    p.setBrush(trackBg);
    p.drawRoundedRect(track, trackRadius, trackRadius);

    const QRectF progress = progressRect(track);
    const bool hasProgress
        = (m_orientation == Qt::Horizontal) ? (progress.width() > 0.0) : (progress.height() > 0.0);
    if (hasProgress) {
        QColor start = colors.primary;
        start.setAlphaF(m_progressStartOpacity);
        QColor finish = colors.primaryHover();
        finish.setAlphaF(m_progressFinishOpacity);
        QLinearGradient progressGradient(m_orientation == Qt::Horizontal
                ? QPointF(track.left(), track.center().y())
                : QPointF(track.center().x(), track.bottom()),
            m_orientation == Qt::Horizontal ? QPointF(track.right(), track.center().y())
                                            : QPointF(track.center().x(), track.top()));
        progressGradient.setColorAt(0.0, start);
        progressGradient.setColorAt(1.0, finish);

        p.setBrush(progressGradient);
        p.drawRoundedRect(progress, trackRadius, trackRadius);
    }

    const QRectF handle = handleRect(track, progress);
    const qreal handleRadius = qMax<qreal>(1.2, qMin(handle.width(), handle.height()) * 0.45);

    QColor handleOnFilled = colors.textOnPrimary();
    QColor handleOnEmpty = colors.text;
    if (m_orientation == Qt::Vertical) {
        qSwap(handleOnFilled, handleOnEmpty);
    }
    qreal transitionCenter = 0.0;
    qreal handleCenter = 0.0;
    qreal transitionWidth = 1.0;
    if (m_orientation == Qt::Horizontal) {
        transitionCenter = progress.right();
        handleCenter = handle.center().x();
        transitionWidth = qMax<qreal>(1.0, handle.width() * 0.75);
    } else {
        // Vertical slider fills bottom -> top, so compare against progress top edge.
        transitionCenter = progress.top();
        handleCenter = handle.center().y();
        transitionWidth = qMax<qreal>(1.0, handle.height() * 0.75);
    }
    const qreal transition = qBound(
        0.0, (handleCenter - (transitionCenter - transitionWidth)) / (transitionWidth * 2.0), 1.0);
    QColor handleColor
        = ruwa::ui::core::ThemeColors::interpolate(handleOnFilled, handleOnEmpty, transition);

    p.setBrush(handleColor);
    p.setPen(QPen(colors.shadow(70), 1.0));
    p.drawRoundedRect(handle, handleRadius, handleRadius);

    if (m_showValueText && m_orientation == Qt::Horizontal) {
        QFont valueFont = font();
        valueFont.setFamily(colors.fonts.uiFont);
        valueFont.setPixelSize(tm.scaledFontSize(8));
        p.setFont(valueFont);

        const qreal textLeftPad = qMax<qreal>(6.0, tm.scaled(8.0));
        const QRectF textRect(track.left() + textLeftPad, track.top(),
            qMax<qreal>(0.0, track.width() - textLeftPad * 2.0), track.height());
        const QString text = displayText();

        QColor textOnFilled = colors.textOnPrimary();
        QColor textOnEmpty = colors.text;
        if (m_hoverProgress > 0.001) {
            textOnEmpty = ruwa::ui::core::ThemeColors::interpolate(
                textOnEmpty, colors.textMuted, 0.15 * (1.0 - m_hoverProgress));
        }

        // Draw text color for empty part of the track.
        p.save();
        p.setClipRect(QRectF(progress.right(), track.top(),
            qMax<qreal>(0.0, track.right() - progress.right()), track.height()));
        p.setPen(textOnEmpty);
        p.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, text);
        p.restore();

        // Draw text color for filled primary part of the track.
        p.save();
        p.setClipRect(QRectF(track.left(), track.top(),
            qMax<qreal>(0.0, progress.right() - track.left()), track.height()));
        p.setPen(textOnFilled);
        p.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, text);
        p.restore();
    }
}

void ProgressHandleSlider::tabletEvent(QTabletEvent* event)
{
    switch (event->type()) {
    case QEvent::TabletPress:
        m_dragging = true;
        m_tabletDragActive = true;
        setFocus(Qt::OtherFocusReason);
        emit sliderPressed();
        setValueFromPosition(event->position());
        event->accept();
        return;
    case QEvent::TabletMove:
        if (m_tabletDragActive && m_dragging) {
            setValueFromPosition(event->position());
            event->accept();
            return;
        }
        break;
    case QEvent::TabletRelease:
        if (m_tabletDragActive && m_dragging) {
            m_dragging = false;
            m_tabletDragActive = false;
            setValueFromPosition(event->position());
            emit sliderReleased();
            update();
            event->accept();
            return;
        }
        m_tabletDragActive = false;
        break;
    default:
        break;
    }

    QWidget::tabletEvent(event);
}

void ProgressHandleSlider::mousePressEvent(QMouseEvent* event)
{
    if (m_tabletDragActive) {
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        setFocus(Qt::MouseFocusReason);
        emit sliderPressed();
        setValueFromPosition(event->position());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ProgressHandleSlider::mouseMoveEvent(QMouseEvent* event)
{
    if (m_tabletDragActive) {
        event->accept();
        return;
    }
    if (m_dragging) {
        setValueFromPosition(event->position());
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ProgressHandleSlider::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_tabletDragActive) {
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        setValueFromPosition(event->position());
        emit sliderReleased();
        update();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ProgressHandleSlider::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(1.0);
    m_hoverAnimation->start();
    QWidget::enterEvent(event);
}

void ProgressHandleSlider::leaveEvent(QEvent* event)
{
    m_hovered = false;
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(0.0);
    m_hoverAnimation->start();
    QWidget::leaveEvent(event);
}

void ProgressHandleSlider::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Down:
        setValue(m_value - 1);
        event->accept();
        return;
    case Qt::Key_Right:
    case Qt::Key_Up:
        setValue(m_value + 1);
        event->accept();
        return;
    case Qt::Key_Home:
        setValue(m_minimum);
        event->accept();
        return;
    case Qt::Key_End:
        setValue(m_maximum);
        event->accept();
        return;
    default:
        break;
    }

    QWidget::keyPressEvent(event);
}

void ProgressHandleSlider::onThemeChanged()
{
    update();
}

QRectF ProgressHandleSlider::contentRect() const
{
    return rect().adjusted(0.5, 0.5, -0.5, -0.5);
}

QRectF ProgressHandleSlider::trackRect(const QRectF& content) const
{
    const auto& tm = ruwa::ui::core::ThemeManager::instance();
    const qreal uniformPad = m_fillInset >= 0.0 ? m_fillInset : qMax<qreal>(3.0, tm.scaled(4.0));
    return content.adjusted(uniformPad, uniformPad, -uniformPad, -uniformPad);
}

QRectF ProgressHandleSlider::progressRect(const QRectF& track) const
{
    const qreal ratio = valueToRatio(m_value);
    if (m_orientation == Qt::Horizontal) {
        return QRectF(track.left(), track.top(), track.width() * ratio, track.height());
    }

    const qreal h = track.height() * ratio;
    return QRectF(track.left(), track.bottom() - h, track.width(), h);
}

QRectF ProgressHandleSlider::handleRect(const QRectF& track, const QRectF& progress) const
{
    const auto& tm = ruwa::ui::core::ThemeManager::instance();
    const qreal inset = qMax<qreal>(2.0, tm.scaled(2.0));
    const qreal progressGap = inset;
    if (m_orientation == Qt::Horizontal) {
        const qreal handleWidth
            = qBound<qreal>(tm.scaled(2.0), track.height() * 0.20, tm.scaled(4.0));
        const qreal minLeft = track.left() + inset;
        const qreal maxLeft = track.right() - inset - handleWidth;
        const qreal preferredLeft = progress.right() - progressGap - handleWidth;
        const qreal handleLeft = qBound(minLeft, preferredLeft, maxLeft);
        const qreal handleHeight = qMax<qreal>(handleWidth * 2.6, track.height() - inset * 2.0);

        return QRectF(
            handleLeft, track.center().y() - handleHeight * 0.5, handleWidth, handleHeight);
    }

    const qreal handleHeight = qBound<qreal>(tm.scaled(2.0), track.width() * 0.20, tm.scaled(4.0));
    const qreal minTop = track.top() + inset;
    const qreal maxTop = track.bottom() - inset - handleHeight;
    const qreal preferredTop = progress.top() + progressGap;
    const qreal handleTop = qBound(minTop, preferredTop, maxTop);
    const qreal handleWidth = qMax<qreal>(handleHeight * 2.6, track.width() - inset * 2.0);

    return QRectF(track.center().x() - handleWidth * 0.5, handleTop, handleWidth, handleHeight);
}

qreal ProgressHandleSlider::valueToRatio(int value) const
{
    const int span = m_maximum - m_minimum;
    if (span <= 0) {
        return 0.0;
    }
    return qBound(0.0, (value - m_minimum) / static_cast<qreal>(span), 1.0);
}

int ProgressHandleSlider::ratioToValue(qreal ratio) const
{
    const int span = m_maximum - m_minimum;
    if (span <= 0) {
        return m_minimum;
    }
    return m_minimum + qRound(qBound(0.0, ratio, 1.0) * span);
}

QString ProgressHandleSlider::displayText() const
{
    if (!m_customDisplayText.isEmpty()) {
        return m_customDisplayText;
    }

    int displayedValue = m_value;
    if (m_valueDisplayMode == ValueDisplayMode::Percent) {
        const int span = m_maximum - m_minimum;
        if (span > 0) {
            displayedValue = qRound(((m_value - m_minimum) / static_cast<qreal>(span)) * 100.0);
        } else {
            displayedValue = 0;
        }
    }

    return QString("%1%2%3").arg(m_valueTextPrefix).arg(displayedValue).arg(m_valueTextSuffix);
}

void ProgressHandleSlider::setValueFromPosition(const QPointF& position)
{
    const QRectF track = trackRect(contentRect());
    if (track.width() <= 0.0 || track.height() <= 0.0) {
        return;
    }

    qreal ratio = 0.0;
    if (m_orientation == Qt::Horizontal) {
        ratio = (position.x() - track.left()) / track.width();
    } else {
        ratio = (track.bottom() - position.y()) / track.height();
    }
    setValue(ratioToValue(ratio));
}

} // namespace ruwa::ui::widgets
