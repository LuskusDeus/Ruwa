// SPDX-License-Identifier: MPL-2.0

// MessagePopup.h
#ifndef RUWA_UI_WIDGETS_TOPBAR_MESSAGEPOPUP_H
#define RUWA_UI_WIDGETS_TOPBAR_MESSAGEPOPUP_H

#include <QWidget>
#include <QList>
#include <QPropertyAnimation>
#include <QVBoxLayout>
#include <QImage>
#include <QPixmap>
#include <functional>

namespace ruwa::ui::widgets {

/**
 * @brief Button descriptor for MessagePopup
 */
struct MessageButton {
    QString text;
    bool primary = false; // true = AnimatedButton style, false = secondary
    std::function<void()> callback;
};

/**
 * @brief Popup for user confirmation/messages
 *
 * Appears attached below the topbar and reveals
 * by animating height from the seam.
 * API: set message text, add buttons (any count), set width.

 */
class MessagePopup : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int revealHeight READ revealHeight WRITE setRevealHeight)
    Q_PROPERTY(qreal borderGlowProgress READ borderGlowProgress WRITE setBorderGlowProgress)
    Q_PROPERTY(qreal timebarProgress READ timebarProgress WRITE setTimebarProgress)

public:
    explicit MessagePopup(QWidget* parent = nullptr);
    ~MessagePopup() override;

    /// Set message text
    void setMessage(const QString& text);

    /// Set optional image (shown above text). Empty image clears.
    void setImage(const QImage& image);

    /// Auto-hide after ms (0 = disabled). Used for toast-style notifications without buttons.
    void setAutoHideDuration(int ms);

    /// Set buttons (replaces existing). Call before show().
    void setButtons(const QList<MessageButton>& buttons);

    /// Set popup width (API)
    void setPopupWidth(int width);

    /// Show popup below topbar with height reveal animation.
    void showPopup();

    /// Hide with animation
    void hidePopup();

    bool isPopupVisible() const { return m_isVisible; }
    bool isHiding() const { return m_isHiding; }

    int revealHeight() const { return m_revealHeight; }
    void setRevealHeight(int height);

    /// Local rect of the painted popup body, excluding shadow-only padding.
    QRectF bodyRect() const;

    qreal borderGlowProgress() const { return m_borderGlowProgress; }
    void setBorderGlowProgress(qreal progress);

    qreal timebarProgress() const { return m_timebarProgress; }
    void setTimebarProgress(qreal progress);

signals:
    void aboutToHide();
    void hidden();
    void shown();
    void contentChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void rebuildContent();
    void updateButtonCallbacks();
    void startShowAnimation();
    void startHideAnimation();
    QPoint calculatePosition() const;
    QString contentSignature() const;

private:
    QString m_message;
    QPixmap m_image;
    int m_autoHideDuration = 0;
    QList<MessageButton> m_buttons;
    int m_width = 320;

    QPropertyAnimation* m_heightAnim = nullptr;
    int m_revealHeight = 0;
    int m_targetHeight = 0;
    bool m_isVisible = false;
    bool m_isHiding = false;

    QList<QWidget*> m_buttonWidgets;
    QVBoxLayout* m_layout = nullptr;

    qreal m_borderGlowProgress = 0.0;
    QPropertyAnimation* m_borderGlowAnim = nullptr;

    qreal m_timebarProgress = 1.0;
    QPropertyAnimation* m_timebarAnim = nullptr;

    QString m_displayedContentSignature;

    static constexpr int TOP_OFFSET = 12;
    static constexpr int SHOW_DURATION = 260;
    static constexpr int HIDE_DURATION = 180;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_TOPBAR_MESSAGEPOPUP_H
