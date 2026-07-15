// SPDX-License-Identifier: MPL-2.0

// UpdateMessageOverlay.h
#ifndef RUWA_UI_WIDGETS_UPDATEMESSAGE_UPDATEMESSAGEOVERLAY_H
#define RUWA_UI_WIDGETS_UPDATEMESSAGE_UPDATEMESSAGEOVERLAY_H

#include <QWidget>
#include <QPropertyAnimation>
#include <QElapsedTimer>
#include <QPointer>

class QGraphicsOpacityEffect;

namespace ruwa::ui::widgets {

/**
 * @brief Overlay for first-launch update message (like CommandPaletteOverlay)
 *
 * Covers the content area with dimming and shows a centered card with
 * update info. Click-outside or Escape to close.
 */
class UpdateMessageOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal dimProgress READ dimProgress WRITE setDimProgress)

public:
    explicit UpdateMessageOverlay(QWidget* parent = nullptr, QWidget* geometrySource = nullptr);
    ~UpdateMessageOverlay() override;

    /// Show overlay with update message card
    void showMessage();

    /// Hide overlay. If \a bypassCooldown is true, closes immediately even during the initial
    /// cooldown (e.g. when user clicks "Got it").
    void hideMessage(bool bypassCooldown = false);

    /// Check if active
    bool isActive() const;

    qreal dimProgress() const { return m_dimProgress; }
    void setDimProgress(qreal progress);

signals:
    void hidden();
    void shown();
    void releaseNotesRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    void onCardDismissed();
    void onReleaseNotesRequested();
    void onDimAnimationFinished();
    void onCardHideAnimationFinished();

private:
    void setupUI();
    void setupAnimations();
    void rebuildCard();
    void refreshCardBackdrop();
    void updateCardPosition();
    QPoint cardTargetPosition() const;
    void syncOverlayGeometry();

private:
    QWidget* m_card = nullptr;
    QGraphicsOpacityEffect* m_cardOpacityEffect = nullptr;
    QPointer<QWidget> m_geometrySource;

    qreal m_dimProgress = 0.0;
    bool m_isShowing = false;
    bool m_isHiding = false;
    bool m_shortcutsBlocked = false;

    QPropertyAnimation* m_dimAnimation = nullptr;
    QPropertyAnimation* m_cardOpacityAnim = nullptr;
    QPropertyAnimation* m_cardPosAnim = nullptr;

    static constexpr int DimAnimationDuration = 180;
    static constexpr int CardAnimationDuration = 200;
    static constexpr int SlideOffset = 20;
    static constexpr qreal MaxDimOpacity = 0.5;
    static constexpr int DismissCooldownMs = 1500;

    QElapsedTimer m_dismissCooldownTimer;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_UPDATEMESSAGE_UPDATEMESSAGEOVERLAY_H
