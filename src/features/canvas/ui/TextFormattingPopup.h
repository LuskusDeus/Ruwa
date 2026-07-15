// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_TEXTFORMATTINGPOPUP_H
#define RUWA_UI_WIDGETS_TEXTFORMATTINGPOPUP_H

#include <QColor>
#include <QString>
#include <QWidget>

class QPropertyAnimation;
class QTabletEvent;

namespace ruwa::ui::widgets {
class BaseAnimatedButton;
class FontDropdownSelector;

class TextFormattingPopup : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal popupOpacity READ popupOpacity WRITE setPopupOpacity)
    Q_PROPERTY(qreal slideOffset READ slideOffset WRITE setSlideOffset)
    Q_PROPERTY(qreal anchorX READ anchorX WRITE setAnchorX)
    Q_PROPERTY(qreal anchorY READ anchorY WRITE setAnchorY)

public:
    explicit TextFormattingPopup(QWidget* parent = nullptr);

    void showAt(const QPoint& topLeft, bool animateShow, bool animateMove);
    void hideAnimated();
    void hideImmediate();
    bool isPopupVisible() const { return m_isVisible; }
    void setEffectStates(bool bold, bool italic, bool underline);
    void setCurrentFontFamily(const QString& family);
    void setTextColor(const QColor& color);

    qreal popupOpacity() const { return m_opacity; }
    void setPopupOpacity(qreal opacity);
    qreal slideOffset() const { return m_slideOffset; }
    void setSlideOffset(qreal offset);
    qreal anchorX() const { return m_anchorX; }
    void setAnchorX(qreal x);
    qreal anchorY() const { return m_anchorY; }
    void setAnchorY(qreal y);

signals:
    void fontFamilyActivated(const QString& family);
    void textColorClicked(QWidget* anchor);
    void boldClicked();
    void italicClicked();
    void underlineClicked();

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
    FontDropdownSelector* m_fontDropdown = nullptr;
    BaseAnimatedButton* m_boldButton = nullptr;
    BaseAnimatedButton* m_italicButton = nullptr;
    BaseAnimatedButton* m_underlineButton = nullptr;
    BaseAnimatedButton* m_colorButton = nullptr;
    BaseAnimatedButton* m_tabletHoveredButton = nullptr;
    QPropertyAnimation* m_opacityAnim = nullptr;
    QPropertyAnimation* m_slideAnim = nullptr;
    QPropertyAnimation* m_anchorXAnim = nullptr;
    QPropertyAnimation* m_anchorYAnim = nullptr;
    qreal m_opacity = 0.0;
    qreal m_slideOffset = 0.0;
    qreal m_anchorX = 0.0;
    qreal m_anchorY = 0.0;
    QColor m_textColor = QColor(0, 0, 0, 255);
    bool m_isVisible = false;
    bool m_isHiding = false;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_TEXTFORMATTINGPOPUP_H
