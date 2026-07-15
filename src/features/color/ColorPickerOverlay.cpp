// SPDX-License-Identifier: MPL-2.0

// ColorPickerOverlay.cpp
#include "ColorPickerOverlay.h"
#include "ColorPicker.h"
#include "shared/widgets/inputs/ColorInputButton.h"

#include <QWidget>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QApplication>
namespace ruwa::ui::widgets {

ColorPickerOverlay::ColorPickerOverlay(QWidget* container)
    : QObject(container)
    , m_container(container)
{
    setupPicker();
    setupAnimations();

    m_container->installEventFilter(this);
}

ColorPickerOverlay::~ColorPickerOverlay() = default;

void ColorPickerOverlay::setupPicker()
{
    m_picker = new ColorPicker(m_container);
    m_picker->setFocusPolicy(Qt::StrongFocus);
    m_picker->setMouseTracking(true);
    m_picker->hide();

    connect(m_picker, &ColorPicker::colorChanged, this, &ColorPickerOverlay::colorSelected);

    connect(m_picker, &ColorPicker::canceled, this, &ColorPickerOverlay::onPickerCanceled);
}

void ColorPickerOverlay::setupAnimations()
{
    m_posAnimation = new QPropertyAnimation(this, "pickerPos", this);
    m_posAnimation->setDuration(PositionAnimationDuration);
    m_posAnimation->setEasingCurve(QEasingCurve::OutCubic);
}

QPoint ColorPickerOverlay::pickerPos() const
{
    return m_picker ? m_picker->pos() : QPoint();
}

void ColorPickerOverlay::setPickerPos(const QPoint& pos)
{
    if (m_picker) {
        m_picker->move(pos);
    }
}

void ColorPickerOverlay::showPicker(const QColor& initialColor, QWidget* sourceButton)
{
    // Toggle
    if (m_picker->isVisible() && !m_isHiding && m_sourceButton == sourceButton
        && sourceButton != nullptr) {
        hidePicker();
        return;
    }

    // Opacity slider is opt-in per source (e.g. gradient overlay colours). Toggle
    // it before setColor/positioning so the picker samples alpha and sizes right.
    bool wantAlpha = false;
    if (auto* colorButton = qobject_cast<ColorInputButton*>(sourceButton)) {
        wantAlpha = colorButton->alphaEnabled();
    }
    m_picker->setAlphaSliderEnabled(wantAlpha);

    // Switch
    if (m_picker->isVisible() && !m_isHiding) {
        m_sourceButton = sourceButton;
        m_picker->setColor(initialColor);
        m_picker->setPopupTitle(pickerTitleForSource(sourceButton));
        QPoint targetPos = calculatePickerPosition(sourceButton);
        animatePickerTo(targetPos);
        return;
    }

    if (m_isShowing || m_isHiding) {
        forceHide();
    }

    m_isShowing = true;
    m_isHiding = false;
    m_sourceButton = sourceButton;
    m_picker->setColor(initialColor);
    m_picker->setPopupTitle(pickerTitleForSource(sourceButton));

    QPoint targetPos = calculatePickerPosition(sourceButton);

    // Slide-in start position
    QPoint startPos = targetPos;
    if (sourceButton) {
        QPoint buttonInContainer = sourceButton->mapTo(m_container, QPoint(0, 0));
        if (targetPos.y() < buttonInContainer.y()) {
            startPos.setY(targetPos.y() + SlideOffset);
        } else {
            startPos.setY(targetPos.y() - SlideOffset);
        }
    } else {
        startPos.setY(targetPos.y() + SlideOffset);
    }

    m_picker->move(startPos);
    m_picker->raise();

    // Install global event filter when showing
    qApp->installEventFilter(this);

    animatePickerTo(targetPos);
    m_picker->showAnimated();

    QTimer::singleShot(PositionAnimationDuration, this, [this]() {
        m_isShowing = false;
        emit shown();
    });

    QTimer::singleShot(50, this, [this]() {
        if (m_picker && m_picker->isVisible()) {
            m_picker->setFocus();
        }
    });
}

void ColorPickerOverlay::hidePicker()
{
    if (m_isHiding || !m_picker->isVisible()) {
        return;
    }

    m_isHiding = true;
    m_isShowing = false;

    // Remove global event filter
    qApp->removeEventFilter(this);

    // Slide out
    if (m_picker && m_posAnimation) {
        QPoint currentPos = m_picker->pos();
        QPoint endPos = currentPos;

        if (m_sourceButton) {
            QPoint buttonInContainer = m_sourceButton->mapTo(m_container, QPoint(0, 0));
            if (currentPos.y() < buttonInContainer.y()) {
                endPos.setY(currentPos.y() - SlideOffset);
            } else {
                endPos.setY(currentPos.y() + SlideOffset);
            }
        } else {
            endPos.setY(currentPos.y() + SlideOffset);
        }

        m_posAnimation->stop();
        m_posAnimation->setStartValue(currentPos);
        m_posAnimation->setEndValue(endPos);
        m_posAnimation->start();
    }

    m_picker->hideAnimated();

    QTimer::singleShot(PositionAnimationDuration, this, [this]() {
        m_isHiding = false;
        m_sourceButton = nullptr;
        emit hidden();
    });
}

void ColorPickerOverlay::forceHide()
{
    qApp->removeEventFilter(this);

    if (m_picker) {
        m_picker->hide();
    }
    if (m_posAnimation) {
        m_posAnimation->stop();
    }
    m_isShowing = false;
    m_isHiding = false;
    m_sourceButton = nullptr;
}

bool ColorPickerOverlay::isActive() const
{
    return m_picker && m_picker->isVisible() && !m_isHiding;
}

void ColorPickerOverlay::onPickerCanceled()
{
    hidePicker();
}

QPoint ColorPickerOverlay::calculatePickerPosition(QWidget* sourceButton) const
{
    if (!m_picker || !m_container)
        return QPoint();

    int containerWidth = m_container->width();
    int containerHeight = m_container->height();
    const int shadow = m_picker->popupShadowMarginPixels();
    int pickerWidth = m_picker->width() - shadow * 2;
    int pickerHeight = m_picker->height() - shadow * 2;

    if (!sourceButton) {
        return QPoint((containerWidth - pickerWidth) / 2 - shadow,
            (containerHeight - pickerHeight) / 2 - shadow);
    }

    QPoint buttonInContainer = sourceButton->mapTo(m_container, QPoint(0, 0));
    QSize buttonSize = sourceButton->size();

    int x = buttonInContainer.x();
    int y = buttonInContainer.y() + buttonSize.height() + OffsetFromButton;

    if (y + pickerHeight > containerHeight - OffsetFromButton) {
        y = buttonInContainer.y() - pickerHeight - OffsetFromButton;
    }

    x = qBound(OffsetFromButton, x, containerWidth - pickerWidth - OffsetFromButton);
    y = qBound(OffsetFromButton, y, containerHeight - pickerHeight - OffsetFromButton);

    return QPoint(x - shadow, y - shadow);
}

QString ColorPickerOverlay::pickerTitleForSource(QWidget* sourceButton) const
{
    if (auto* colorButton = qobject_cast<ColorInputButton*>(sourceButton)) {
        const QString label = colorButton->label().trimmed();
        if (!label.isEmpty()) {
            return label;
        }
    }
    return tr("Color");
}

void ColorPickerOverlay::animatePickerTo(const QPoint& targetPos)
{
    if (!m_picker || !m_posAnimation)
        return;

    m_posAnimation->stop();
    m_posAnimation->setStartValue(m_picker->pos());
    m_posAnimation->setEndValue(targetPos);
    m_posAnimation->start();
}

bool ColorPickerOverlay::isColorInputButton(QWidget* widget) const
{
    if (!widget)
        return false;
    QString className = QString::fromLatin1(widget->metaObject()->className());
    return className.contains("ColorInputButton");
}

bool ColorPickerOverlay::isPickerOrChild(QWidget* widget, const QPoint& globalPos) const
{
    if (!widget || !m_picker)
        return false;
    if (widget == m_picker) {
        return m_picker->containsVisiblePopupPoint(m_picker->mapFromGlobal(globalPos));
    }
    return m_picker->isAncestorOf(widget);
}

bool ColorPickerOverlay::eventFilter(QObject* watched, QEvent* event)
{
    // Container resize
    if (watched == m_container && event->type() == QEvent::Resize) {
        if (m_picker && m_picker->isVisible() && m_sourceButton && !m_isShowing && !m_isHiding) {
            m_picker->move(calculatePickerPosition(m_sourceButton));
        }
        return false;
    }

    // Only process when picker is visible
    if (!m_picker || !m_picker->isVisible() || m_isHiding || m_isShowing) {
        return false;
    }

    // Mouse press - check if outside picker
    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        QPoint globalPos = mouseEvent->globalPosition().toPoint();
        QWidget* clickedWidget = QApplication::widgetAt(globalPos);

        // Click on picker or its children - don't close
        if (isPickerOrChild(clickedWidget, globalPos)) {
            return false;
        }

        // Click on the same source button - let button click handler toggle picker.
        if (m_sourceButton && clickedWidget
            && (clickedWidget == m_sourceButton || m_sourceButton->isAncestorOf(clickedWidget)
                || clickedWidget->isAncestorOf(m_sourceButton))) {
            return false;
        }

        // Click on ColorInputButton - don't close (let toggle/switch work)
        if (isColorInputButton(clickedWidget)) {
            return false;
        }

        // Click anywhere else - close picker
        hidePicker();
        return false; // Don't block the event
    }

    // Escape key
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            hidePicker();
            return true; // Consume escape
        }
    }

    return false;
}

} // namespace ruwa::ui::widgets
