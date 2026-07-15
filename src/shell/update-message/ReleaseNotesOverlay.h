// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_UPDATEMESSAGE_RELEASENOTESOVERLAY_H
#define RUWA_UI_WIDGETS_UPDATEMESSAGE_RELEASENOTESOVERLAY_H

#include <QElapsedTimer>
#include <QPropertyAnimation>
#include <QWidget>

class QEvent;
class QKeyEvent;
class QLabel;
class QMouseEvent;
class QPaintEvent;
class QGraphicsOpacityEffect;
class QResizeEvent;

namespace ruwa::ui::widgets {

class CapsuleButton;
class SmoothScrollArea;

/**
 * @brief Release notes overlay with a scrollable update history layout
 */
class ReleaseNotesOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal dimProgress READ dimProgress WRITE setDimProgress)

public:
    explicit ReleaseNotesOverlay(QWidget* parent = nullptr);
    ~ReleaseNotesOverlay() override;

    void showOverlay();
    void hideOverlay(bool bypassCooldown = false);
    bool isActive() const;

    qreal dimProgress() const { return m_dimProgress; }
    void setDimProgress(qreal progress);

signals:
    void shown();
    void hidden();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    void onCloseRequested();
    void onDimAnimationFinished();
    void onCardHideAnimationFinished();

private:
    void setupUi();
    void setupAnimations();
    void rebuildEntries();
    void clearEntries();
    void updateCardPosition();
    void updateTexts();
    QPoint cardTargetPosition() const;

private:
    QWidget* m_card { nullptr };
    QLabel* m_titleLabel { nullptr };
    SmoothScrollArea* m_scrollArea { nullptr };
    QWidget* m_scrollContent { nullptr };
    CapsuleButton* m_closeButton { nullptr };
    QGraphicsOpacityEffect* m_cardOpacityEffect { nullptr };
    QPropertyAnimation* m_dimAnimation { nullptr };
    QPropertyAnimation* m_cardOpacityAnim { nullptr };
    QPropertyAnimation* m_cardPosAnim { nullptr };
    qreal m_dimProgress { 0.0 };
    bool m_isShowing { false };
    bool m_isHiding { false };
    bool m_shortcutsBlocked { false };
    bool m_entriesBuilt { false };
    QElapsedTimer m_dismissCooldownTimer;

    static constexpr int DimAnimationDuration = 180;
    static constexpr int CardAnimationDuration = 200;
    static constexpr int SlideOffset = 20;
    static constexpr qreal MaxDimOpacity = 0.5;
    static constexpr int DismissCooldownMs = 150;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_UPDATEMESSAGE_RELEASENOTESOVERLAY_H
