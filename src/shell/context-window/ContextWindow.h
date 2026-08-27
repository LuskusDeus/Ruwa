// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WINDOWS_CONTEXTWINDOW_H
#define RUWA_UI_WINDOWS_CONTEXTWINDOW_H

#include <QPointer>
#include <QWidget>

class QAbstractButton;
class QCloseEvent;
class QHideEvent;
class QKeyEvent;
class QParallelAnimationGroup;
class QPropertyAnimation;
class QVBoxLayout;

namespace QWK {
class WidgetWindowAgent;
}

namespace ruwa::ui::windows {

/**
 * @brief Modal, content-driven tool window used for effect and operation settings.
 *
 * ContextWindow owns the supplied content widget and deliberately knows nothing
 * about the operation represented by it. It provides the common Ruwa window
 * chrome, QWindowKit integration, application interaction blocking and the
 * slide-in/slide-out lifecycle.
 */
class ContextWindow final : public QWidget {
    Q_OBJECT

public:
    ContextWindow(QWidget* owner, const QString& title, QWidget* contentWidget);
    ~ContextWindow() override;

    QWidget* contentWidget() const { return m_contentWidget; }
    bool isClosing() const { return m_closing; }

public slots:
    void showAnimated();
    void dismiss();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void setupUi(const QString& title, QWidget* contentWidget);
    void setupWindowAgent();
    void applyWindowEffects();
    void setShortcutBlocking(bool blocked);
    void updateContentSize();
    QPoint visiblePosition() const;
    QPoint entrancePosition() const;

    QPointer<QWidget> m_owner;
    QWidget* m_topBar = nullptr;
    QAbstractButton* m_closeButton = nullptr;
    QWidget* m_contentWidget = nullptr;
    QVBoxLayout* m_mainLayout = nullptr;
    QWK::WidgetWindowAgent* m_windowAgent = nullptr;
    QParallelAnimationGroup* m_transitionAnimation = nullptr;
    QPropertyAnimation* m_slideAnimation = nullptr;
    QPropertyAnimation* m_opacityAnimation = nullptr;
    bool m_closing = false;
    bool m_closeAllowed = false;
    bool m_shortcutsBlocked = false;

    static constexpr int BaseBottomInset = 24;
    static constexpr int ShowDuration = 260;
    static constexpr int HideDuration = 210;
};

} // namespace ruwa::ui::windows

#endif // RUWA_UI_WINDOWS_CONTEXTWINDOW_H
