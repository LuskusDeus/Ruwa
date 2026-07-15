// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_COMMON_POSITIONINPUTFIELD_H
#define RUWA_UI_WIDGETS_COMMON_POSITIONINPUTFIELD_H

#include "shared/widgets/BaseStyledWidget.h"
#include <QPointF>

class QPropertyAnimation;

namespace ruwa::ui::widgets {

/**
 * @brief Capsule input for an on-canvas position (X + Y as one value), styled
 * like ColorInputButton/HexColorInput. Display-only — no typing; clicking
 * anywhere on it requests "position picking" mode, where the next click on the
 * canvas writes its document-pixel coordinate back via setPosition().
 *
 * Generic over any effect param (or future feature) that needs a document
 * point rather than two independent numbers: the caller owns what "picking"
 * means and just calls setPosition() with the result.
 */
class PositionInputField : public BaseStyledWidget {
    Q_OBJECT
    Q_PROPERTY(qreal hoverAlpha READ hoverAlpha WRITE setHoverAlpha)

public:
    explicit PositionInputField(QWidget* parent = nullptr);

    QPointF position() const { return m_position; }
    /// Sets the displayed value. Emits positionChanged only when the value
    /// actually differs, same contract as ColorInputButton::setColor.
    void setPosition(const QPointF& pos);

    /// 0 = integer display (matches the pixel-coordinate params this pairs
    /// with); >0 = that many decimal places.
    void setDecimals(int decimals);
    int decimals() const { return m_decimals; }

    /// Highlights the capsule while this field is the active target of an
    /// in-progress position-picking session.
    void setActive(bool active);
    bool isActive() const { return m_active; }

    qreal hoverAlpha() const { return m_hoverAlpha; }
    void setHoverAlpha(qreal alpha);

signals:
    void positionChanged(const QPointF& pos);
    /// The capsule was clicked — the caller should enter position-picking
    /// mode and eventually call setPosition() with the chosen point.
    void pickRequested();

protected:
    void drawContentLayer(QPainter& painter, const QRectF& rect) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void onThemeChanged();

private:
    QPointF m_position;
    int m_decimals = 0;
    bool m_active = false;
    qreal m_hoverAlpha { 0.0 };
    QPropertyAnimation* m_hoverAnimation { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_POSITIONINPUTFIELD_H
