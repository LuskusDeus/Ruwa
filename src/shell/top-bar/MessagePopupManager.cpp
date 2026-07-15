// SPDX-License-Identifier: MPL-2.0

// MessagePopupManager.cpp
#include "MessagePopupManager.h"
#include "OverlayContainer.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMouseEvent>
#include <QPointer>
#include <QTabletEvent>

#include <memory>

namespace ruwa::ui::widgets {

namespace {

class MessagePopupEventFilter : public QObject {
public:
    MessagePopupEventFilter(MessagePopup* popup, OverlayContainer* overlay, QWidget* triggerWidget,
        QObject* parent = nullptr)
        : QObject(parent)
        , m_popup(popup)
        , m_overlay(overlay)
        , m_triggerWidget(triggerWidget)
        , m_openGlobalPos(QCursor::pos())
    {
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        Q_UNUSED(watched);
        if (!m_popup || !m_popup->isPopupVisible()) {
            return false;
        }

        QPoint globalPos;
        if (!tryExtractPressGlobalPos(event, &globalPos)) {
            return false;
        }

        QWidget* clicked = QApplication::widgetAt(globalPos);
        if (!clicked || clicked == m_popup || m_popup->isAncestorOf(clicked)) {
            return false;
        }

        // Don't close when click is on the trigger widget (e.g. the button that opened the popup)
        if (m_triggerWidget
            && (clicked == m_triggerWidget || m_triggerWidget->isAncestorOf(clicked))) {
            return false;
        }

        if (shouldIgnoreInitialOutsidePress(globalPos)) {
            return true;
        }

        m_popup->hidePopup();
        return false;
    }

private:
    static bool tryExtractPressGlobalPos(QEvent* event, QPoint* outGlobalPos)
    {
        if (!event || !outGlobalPos) {
            return false;
        }

        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            *outGlobalPos = mouseEvent->globalPosition().toPoint();
            return true;
        }
        case QEvent::TabletPress: {
            auto* tabletEvent = static_cast<QTabletEvent*>(event);
            *outGlobalPos = tabletEvent->globalPosition().toPoint();
            return true;
        }
        default:
            return false;
        }
    }

    bool shouldIgnoreInitialOutsidePress(const QPoint& globalPos)
    {
        if (!m_openPressGuard.isValid()) {
            m_openPressGuard.start();
        }

        if (m_openPressGuard.elapsed() > 250) {
            return false;
        }

        return (globalPos - m_openGlobalPos).manhattanLength()
            <= qMax(8, QApplication::startDragDistance());
    }

    QPointer<MessagePopup> m_popup;
    QPointer<OverlayContainer> m_overlay;
    QPointer<QWidget> m_triggerWidget;
    QElapsedTimer m_openPressGuard;
    QPoint m_openGlobalPos;
};

} // anonymous namespace

void MessagePopupManager::showImageCopied(QWidget* context, const QImage& image)
{
    if (!context || image.isNull())
        return;

    QWidget* window = context->window();
    if (!window)
        return;

    OverlayContainer* overlay = OverlayContainer::instance(window);
    if (!overlay)
        return;

    MessagePopup* popup = overlay->messagePopup();
    if (!popup)
        return;

    const QString message
        = QCoreApplication::translate("MessagePopupManager", "Image copied to clipboard");
    popup->setImage(image);
    popup->setMessage(message);
    popup->setButtons({});
    popup->setAutoHideDuration(2500);
    popup->setPopupWidth(280);

    overlay->showOverlay();

    auto* filter = new MessagePopupEventFilter(popup, overlay, nullptr);
    QObject::connect(popup, &MessagePopup::hidden, filter, [filter, overlay]() {
        qApp->removeEventFilter(filter);
        if (overlay && !overlay->hasActivePopups()) {
            overlay->hideOverlay();
        }
        filter->deleteLater();
    });

    qApp->installEventFilter(filter);
    popup->showPopup();
}

void MessagePopupManager::show(QWidget* context, const QString& message,
    const QList<MessageButton>& buttons, int width, QWidget* triggerWidget)
{
    if (!context)
        return;

    QWidget* window = context->window();
    if (!window)
        return;

    OverlayContainer* overlay = OverlayContainer::instance(window);
    if (!overlay)
        return;

    MessagePopup* popup = overlay->messagePopup();
    if (!popup)
        return;

    popup->setImage(QImage());
    popup->setAutoHideDuration(0);
    popup->setMessage(message);
    popup->setButtons(buttons);
    popup->setPopupWidth(width);

    overlay->showOverlay();

    auto* filter = new MessagePopupEventFilter(popup, overlay, triggerWidget);
    QObject::connect(popup, &MessagePopup::hidden, filter, [filter, overlay]() {
        qApp->removeEventFilter(filter);
        if (overlay && !overlay->hasActivePopups()) {
            overlay->hideOverlay();
        }
        filter->deleteLater();
    });

    qApp->installEventFilter(filter);
    popup->showPopup();
}

bool MessagePopupManager::showBlocking(QWidget* context, const QString& message,
    const QString& confirmText, const QString& cancelText, int width, bool confirmIsPrimary)
{
    if (!context)
        return false;

    QWidget* window = context->window();
    if (!window)
        return false;

    OverlayContainer* overlay = OverlayContainer::instance(window);
    if (!overlay)
        return false;

    MessagePopup* popup = overlay->messagePopup();
    if (!popup)
        return false;

    struct State {
        bool result = false;
        bool responded = false;
    };
    auto state = std::make_shared<State>();
    QEventLoop loop;

    QList<MessageButton> buttons = { { cancelText, !confirmIsPrimary,
                                         [state, &loop]() {
                                             state->responded = true;
                                             state->result = false;
                                             loop.quit();
                                         } },
        { confirmText, confirmIsPrimary, [state, &loop]() {
             state->responded = true;
             state->result = true;
             loop.quit();
         } } };

    popup->setImage(QImage());
    popup->setAutoHideDuration(0);
    popup->setMessage(message);
    popup->setButtons(buttons);
    popup->setPopupWidth(width);

    QObject::connect(
        popup, &MessagePopup::hidden, popup,
        [state, &loop]() {
            if (!state->responded) {
                state->responded = true;
                state->result = false;
                loop.quit();
            }
        },
        Qt::SingleShotConnection);

    overlay->showOverlay();

    auto* filter = new MessagePopupEventFilter(popup, overlay, nullptr);
    QObject::connect(popup, &MessagePopup::hidden, filter, [filter, overlay]() {
        qApp->removeEventFilter(filter);
        if (overlay && !overlay->hasActivePopups()) {
            overlay->hideOverlay();
        }
        filter->deleteLater();
    });

    qApp->installEventFilter(filter);
    popup->showPopup();

    loop.exec();

    return state->result;
}

MessagePopupManager::SaveChangesResult MessagePopupManager::showSaveChangesBlocking(
    QWidget* context, const QString& projectNameOrPath, int width)
{
    if (!context)
        return SaveChangesResult::Cancel;

    QWidget* window = context->window();
    if (!window)
        return SaveChangesResult::Cancel;

    OverlayContainer* overlay = OverlayContainer::instance(window);
    if (!overlay)
        return SaveChangesResult::Cancel;

    MessagePopup* popup = overlay->messagePopup();
    if (!popup)
        return SaveChangesResult::Cancel;

    const QString displayName = projectNameOrPath.isEmpty()
        ? QCoreApplication::translate("MessagePopupManager", "Untitled Project")
        : projectNameOrPath;
    const QString message = QCoreApplication::translate(
        "MessagePopupManager", "Save changes to \"%1\" before closing?")
                                .arg(displayName);

    const QString saveText = QCoreApplication::translate("MessagePopupManager", "Save");
    const QString discardText = QCoreApplication::translate("MessagePopupManager", "Don't Save");
    const QString cancelText = QCoreApplication::translate("MessagePopupManager", "Cancel");

    struct State {
        SaveChangesResult result = SaveChangesResult::Cancel;
        bool responded = false;
    };
    auto state = std::make_shared<State>();
    QEventLoop loop;

    QList<MessageButton> buttons = { { saveText, true,
                                         [state]() {
                                             state->responded = true;
                                             state->result = SaveChangesResult::Save;
                                         } },
        { discardText, false,
            [state]() {
                state->responded = true;
                state->result = SaveChangesResult::Discard;
            } },
        { cancelText, false, [state]() {
             state->responded = true;
             state->result = SaveChangesResult::Cancel;
         } } };

    popup->setImage(QImage());
    popup->setAutoHideDuration(0);
    popup->setMessage(message);
    popup->setButtons(buttons);
    popup->setPopupWidth(width);

    // Quit only when popup has fully hidden (after animation). This allows the popup
    // to be shown again immediately for the next tab when closing the application.
    QObject::connect(
        popup, &MessagePopup::hidden, popup,
        [state, &loop]() {
            if (!state->responded) {
                state->responded = true;
                state->result = SaveChangesResult::Cancel;
            }
            loop.quit();
        },
        Qt::SingleShotConnection);

    overlay->showOverlay();

    auto* filter = new MessagePopupEventFilter(popup, overlay, nullptr);
    QObject::connect(popup, &MessagePopup::hidden, filter, [filter, overlay]() {
        qApp->removeEventFilter(filter);
        if (overlay && !overlay->hasActivePopups()) {
            overlay->hideOverlay();
        }
        filter->deleteLater();
    });

    qApp->installEventFilter(filter);
    popup->showPopup();

    loop.exec();

    return state->result;
}

} // namespace ruwa::ui::widgets
