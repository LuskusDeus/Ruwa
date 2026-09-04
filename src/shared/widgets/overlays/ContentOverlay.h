// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_CONTENTOVERLAY_H
#define RUWA_UI_WIDGETS_CONTENTOVERLAY_H

#include <QPointer>
#include <QWidget>

class QGraphicsOpacityEffect;
class QPropertyAnimation;

namespace ruwa::ui::widgets {

/// Shared dimmed overlay shell extracted from the image import selector.
/// Owns the panel, entrance/exit animation and outside-click/Escape dismissal.
class ContentOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal overlayOpacity READ overlayOpacity WRITE setOverlayOpacity)
    Q_PROPERTY(qreal panelOffset READ panelOffset WRITE setPanelOffset)

public:
    explicit ContentOverlay(QWidget* parent = nullptr);
    ContentOverlay(QWidget* content, const QString& title, QWidget* parent);
    ~ContentOverlay() override;

    void showOverlay();
    void hideOverlay();
    bool isOverlayVisible() const { return isVisible(); }
    qreal overlayOpacity() const { return m_overlayOpacity; }
    void setOverlayOpacity(qreal opacity);
    qreal panelOffset() const { return m_panelOffset; }
    void setPanelOffset(qreal offset);

signals:
    void cancelled();
    void hidden();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    QWidget* m_panel = nullptr;

private:
    void updatePanelGeometry();
    void updateStyle();
    void hideImmediate();
    void releaseInput();

    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QPropertyAnimation* m_opacityAnimation = nullptr;
    QPropertyAnimation* m_panelOffsetAnimation = nullptr;
    QPointer<QWidget> m_previousFocus;
    QWidget* m_content = nullptr;
    bool m_isHiding = false;
    bool m_shortcutsBlocked = false;
    qreal m_overlayOpacity = 0.0;
    qreal m_panelOffset = 0.0;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_CONTENTOVERLAY_H
