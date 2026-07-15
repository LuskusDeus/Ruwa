// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_WORKSPACE_CONFIRMATION_POPUP_H
#define RUWA_UI_WIDGETS_WORKSPACE_CONFIRMATION_POPUP_H

#include <QWidget>

class QPropertyAnimation;
class QTabletEvent;

namespace ruwa::ui::widgets {
class BaseAnimatedButton;

/**
 * @brief Universal confirmation popup with confirm (checkmark) and cancel (cross) buttons.
 *
 * Reuses the same appearance animations and behavior as SelectionActionPopup.
 * Can be used for transform confirmation, or any other confirm/cancel flow.
 */
class ConfirmationPopup : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal popupOpacity READ popupOpacity WRITE setPopupOpacity)
    Q_PROPERTY(qreal slideOffset READ slideOffset WRITE setSlideOffset)
    Q_PROPERTY(qreal anchorX READ anchorX WRITE setAnchorX)
    Q_PROPERTY(qreal anchorY READ anchorY WRITE setAnchorY)

public:
    explicit ConfirmationPopup(QWidget* parent = nullptr);

    void showAt(const QPoint& topLeft, bool animateShow, bool animateMove);
    void hideAnimated();
    void hideImmediate();
    bool isPopupVisible() const { return m_isVisible; }

    qreal popupOpacity() const { return m_opacity; }
    void setPopupOpacity(qreal opacity);
    qreal slideOffset() const { return m_slideOffset; }
    void setSlideOffset(qreal offset);
    qreal anchorX() const { return m_anchorX; }
    void setAnchorX(qreal x);
    qreal anchorY() const { return m_anchorY; }
    void setAnchorY(qreal y);

signals:
    void confirmed();
    void cancelled();

protected:
    void paintEvent(QPaintEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QWidget* createSection(QWidget* parent) const;
    void updateIcons();
    void startShowAnimation(bool animateSlide);
    void startHideAnimation();
    void updateWidgetPosition();

private:
    QWidget* m_section = nullptr;
    BaseAnimatedButton* m_confirmButton = nullptr;
    BaseAnimatedButton* m_cancelButton = nullptr;
    BaseAnimatedButton* m_tabletHoveredButton = nullptr;

    QPropertyAnimation* m_opacityAnim = nullptr;
    QPropertyAnimation* m_slideAnim = nullptr;
    QPropertyAnimation* m_anchorXAnim = nullptr;
    QPropertyAnimation* m_anchorYAnim = nullptr;
    qreal m_opacity = 0.0;
    qreal m_slideOffset = 0.0;
    qreal m_anchorX = 0.0;
    qreal m_anchorY = 0.0;
    bool m_isVisible = false;
    bool m_isHiding = false;

    static constexpr int SHOW_DURATION = 120;
    static constexpr int HIDE_DURATION = 200;
    static constexpr int SLIDE_OFFSET = 14;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_WORKSPACE_CONFIRMATION_POPUP_H
