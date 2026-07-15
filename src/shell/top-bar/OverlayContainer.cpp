// SPDX-License-Identifier: MPL-2.0

// OverlayContainer.cpp
#include "OverlayContainer.h"
#include "LayoutPresetsPopup.h"
#include "MenuPopup.h"
#include "MessagePopup.h"

#include <QMouseEvent>
#include <QApplication>
#include <QPainter>
#include <QRegion>
#include <QTimer>
#include <QVariant>
namespace ruwa::ui::widgets {

QHash<QWidget*, OverlayContainer*> OverlayContainer::s_instances;

OverlayContainer::OverlayContainer(QWidget* parent)
    : QWidget(parent)
{
    // Transparent background
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);

    // Cover entire parent
    if (parent) {
        setGeometry(parent->rect());
        parent->installEventFilter(this);
    }

    // Start hidden - only show when popups are active
    hide();
}

OverlayContainer* OverlayContainer::instance(QWidget* mainWindow)
{
    if (!mainWindow)
        return nullptr;

    if (!s_instances.contains(mainWindow)) {
        auto* container = new OverlayContainer(mainWindow);
        s_instances.insert(mainWindow, container);

        QObject::connect(
            mainWindow, &QObject::destroyed, [mainWindow]() { s_instances.remove(mainWindow); });
    }

    return s_instances.value(mainWindow);
}

void OverlayContainer::setExclusionWidget(QWidget* widget)
{
    Q_UNUSED(widget);
}

void OverlayContainer::registerPopup(MenuPopup* popup)
{
    if (!popup || m_popups.contains(popup))
        return;

    popup->setParent(this);
    m_popups.append(popup);
    connect(popup, &MenuPopup::shown, this, &OverlayContainer::scheduleMaskUpdate);
    connect(popup, &MenuPopup::contentChanged, this, &OverlayContainer::scheduleMaskUpdate);
}

void OverlayContainer::registerLayoutPresetsPopup(LayoutPresetsPopup* popup)
{
    if (!popup || m_layoutPresetsPopup == popup) {
        return;
    }

    m_layoutPresetsPopup = popup;
    popup->setParent(this);
    connect(popup, &LayoutPresetsPopup::shown, this, &OverlayContainer::scheduleMaskUpdate);
    connect(
        popup, &LayoutPresetsPopup::contentChanged, this, &OverlayContainer::scheduleMaskUpdate);
    connect(popup, &LayoutPresetsPopup::hidden, this, &OverlayContainer::scheduleMaskUpdate);
}

void OverlayContainer::unregisterPopup(MenuPopup* popup)
{
    if (popup) {
        disconnect(popup, &MenuPopup::shown, this, &OverlayContainer::scheduleMaskUpdate);
        disconnect(popup, &MenuPopup::contentChanged, this, &OverlayContainer::scheduleMaskUpdate);
    }
    m_popups.removeAll(popup);
}

void OverlayContainer::registerMessagePopup(MessagePopup* popup)
{
    if (!popup || m_messagePopup == popup)
        return;

    if (m_messagePopup) {
        disconnect(
            m_messagePopup, &MessagePopup::shown, this, &OverlayContainer::scheduleMaskUpdate);
        disconnect(m_messagePopup, &MessagePopup::contentChanged, this,
            &OverlayContainer::scheduleMaskUpdate);
    }

    m_messagePopup = popup;
    popup->setParent(this);
    connect(popup, &MessagePopup::shown, this, &OverlayContainer::scheduleMaskUpdate);
    connect(popup, &MessagePopup::contentChanged, this, &OverlayContainer::scheduleMaskUpdate);
}

void OverlayContainer::registerGenericPopup(QWidget* popup)
{
    if (!popup || m_genericPopups.contains(popup))
        return;
    popup->setParent(this);
    m_genericPopups.append(popup);
}

void OverlayContainer::unregisterGenericPopup(QWidget* popup)
{
    m_genericPopups.removeAll(popup);
}

void OverlayContainer::refreshGenericPopups()
{
    if (hasActivePopups()) {
        showOverlay();
    } else {
        hideOverlay();
    }
    updateMaskForVisiblePopups();
}

MessagePopup* OverlayContainer::messagePopup()
{
    if (!m_messagePopup) {
        auto* popup = new MessagePopup(this);
        registerMessagePopup(popup);
    }
    return m_messagePopup;
}

void OverlayContainer::setMessagePopupAnchorY(int y)
{
    m_messagePopupAnchorY = qMax(0, y);
}

void OverlayContainer::scheduleMaskUpdate()
{
    QTimer::singleShot(20, this, &OverlayContainer::updateMaskForVisiblePopups);
}

bool OverlayContainer::hasActivePopups() const
{
    for (const auto& popup : m_popups) {
        if (popup && popup->isPopupVisible()) {
            return true;
        }
    }
    if (m_layoutPresetsPopup && m_layoutPresetsPopup->isPopupVisible()) {
        return true;
    }
    if (m_messagePopup && m_messagePopup->isPopupVisible()) {
        return true;
    }
    for (const auto& popup : m_genericPopups) {
        if (popup && popup->isVisible()) {
            return true;
        }
    }
    return false;
}

void OverlayContainer::showOverlay()
{
    if (auto* p = parentWidget()) {
        // Full window client rect so popups (e.g. menu slide-in from above) are not clipped by a
        // shortened overlay. Hit-testing outside popup hulls uses setMask — clicks still reach
        // TopBar.
        setGeometry(p->rect());
    }
    show();
    raise();
    updateMaskForVisiblePopups();
}

void OverlayContainer::hideOverlay()
{
    // Don't hide if MessagePopup (or other popups) are still visible
    if (hasActivePopups())
        return;

    setMask(QRegion());
    hide();
}

void OverlayContainer::closeAllPopups()
{
    for (auto& popup : m_popups) {
        if (popup && popup->isPopupVisible()) {
            popup->hidePopup();
        }
    }
    if (m_layoutPresetsPopup && m_layoutPresetsPopup->isPopupVisible()) {
        m_layoutPresetsPopup->hidePopup();
    }
    if (m_messagePopup && m_messagePopup->isPopupVisible()) {
        m_messagePopup->hidePopup();
    }
    for (auto& popup : m_genericPopups) {
        if (popup && popup->isVisible()) {
            popup->hide();
        }
    }
}

bool OverlayContainer::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parent() && event->type() == QEvent::Resize) {
        if (isVisible() && parentWidget()) {
            setGeometry(parentWidget()->rect());
        }
    }
    return QWidget::eventFilter(watched, event);
}

void OverlayContainer::updateMaskForVisiblePopups()
{
    constexpr int Padding = 120; // Extra padding to avoid clipping during resize/switch/animation
    QRegion mask;
    for (const auto& popup : m_popups) {
        if (popup && popup->isVisible() && (popup->isPopupVisible() || popup->isHiding())) {
            QRect r = popup->geometry();
            r = r.adjusted(-Padding, -Padding, Padding, Padding);
            mask = mask.united(r);
        }
    }
    if (m_layoutPresetsPopup && m_layoutPresetsPopup->isVisible()
        && (m_layoutPresetsPopup->isPopupVisible() || m_layoutPresetsPopup->isHiding())) {
        QRect r = m_layoutPresetsPopup->geometry();
        r = r.adjusted(-Padding, -Padding, Padding, Padding);
        mask = mask.united(r);
    }
    if (m_messagePopup && m_messagePopup->isVisible()
        && (m_messagePopup->isPopupVisible() || m_messagePopup->isHiding())) {
        QRect r = m_messagePopup->geometry();
        r = r.adjusted(-Padding, -Padding, Padding, Padding);
        mask = mask.united(r);
    }
    for (const auto& popup : m_genericPopups) {
        if (popup && popup->isVisible()) {
            // A generic popup may carry an invisible shadow margin that overlaps
            // neighbouring widgets (e.g. the button that opened it). Masking the
            // full padded geometry would let that margin swallow their hover /
            // cursor events, so honour a popup-supplied mask rect (in the popup's
            // local coords) when present. Fall back to the padded geometry.
            const QVariant maskProp = popup->property("ruwaOverlayMaskRect");
            if (maskProp.canConvert<QRect>() && maskProp.toRect().isValid()) {
                mask = mask.united(maskProp.toRect().translated(popup->geometry().topLeft()));
            } else {
                mask
                    = mask.united(popup->geometry().adjusted(-Padding, -Padding, Padding, Padding));
            }
        }
    }
    setMask(mask);
}

void OverlayContainer::mousePressEvent(QMouseEvent* event)
{
    // With setMask, we only receive events inside menu area - pass through to menu
    QWidget::mousePressEvent(event);
}

void OverlayContainer::mouseReleaseEvent(QMouseEvent* event)
{
    QWidget::mouseReleaseEvent(event);
}

void OverlayContainer::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);
}

void OverlayContainer::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
}

} // namespace ruwa::ui::widgets
