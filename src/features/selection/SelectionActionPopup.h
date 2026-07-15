// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_WORKSPACE_SELECTION_ACTION_POPUP_H
#define RUWA_UI_WIDGETS_WORKSPACE_SELECTION_ACTION_POPUP_H

#include <QColor>
#include <QWidget>

class QPropertyAnimation;
class QTabletEvent;

namespace ruwa::ui::widgets {
class BaseAnimatedButton;

class SelectionActionPopup : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal popupOpacity READ popupOpacity WRITE setPopupOpacity)
    Q_PROPERTY(qreal slideOffset READ slideOffset WRITE setSlideOffset)
    Q_PROPERTY(qreal anchorX READ anchorX WRITE setAnchorX)
    Q_PROPERTY(qreal anchorY READ anchorY WRITE setAnchorY)

public:
    explicit SelectionActionPopup(QWidget* parent = nullptr);

    void setFillColor(const QColor& color);
    QColor fillColor() const { return m_fillColor; }

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
    void fillColorClicked(QWidget* anchor);
    void fillRequested();
    void transformRequested();
    void deleteRequested();
    void flipVerticalRequested();
    void flipHorizontalRequested();
    void dismissRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QWidget* createSection(QWidget* parent) const;
    void updateSwatchStyle();
    void updateIcons();
    void startShowAnimation(bool animateSlide);
    void startHideAnimation();
    void updateWidgetPosition();

private:
    QWidget* m_firstSection = nullptr;
    QWidget* m_secondSection = nullptr;
    BaseAnimatedButton* m_swatchButton = nullptr;
    BaseAnimatedButton* m_fillButton = nullptr;
    BaseAnimatedButton* m_transformButton = nullptr;
    BaseAnimatedButton* m_deleteButton = nullptr;
    BaseAnimatedButton* m_flipVerticalButton = nullptr;
    BaseAnimatedButton* m_flipHorizontalButton = nullptr;
    BaseAnimatedButton* m_closeButton = nullptr;
    BaseAnimatedButton* m_tabletHoveredButton = nullptr;
    QWidget* m_closeSeparator = nullptr;

    QColor m_fillColor = QColor(255, 255, 255, 255);

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

#endif // RUWA_UI_WIDGETS_WORKSPACE_SELECTION_ACTION_POPUP_H
