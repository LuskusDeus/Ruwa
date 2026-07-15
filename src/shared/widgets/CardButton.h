// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SHARED_WIDGETS_CARDBUTTON_H
#define RUWA_SHARED_WIDGETS_CARDBUTTON_H

#include "shared/widgets/BaseAnimatedButton.h"

#include <QColor>

class QPainter;
class QRectF;

namespace ruwa::ui::widgets {

class CardButton : public BaseAnimatedButton {
    Q_OBJECT

public:
    enum class LayoutVariant { Card, Row };

    explicit CardButton(LayoutVariant variant, QWidget* parent = nullptr);
    ~CardButton() override = default;

    void setBaseCornerRadius(int radius);
    LayoutVariant layoutVariant() const { return m_layoutVariant; }

protected:
    void paintEvent(QPaintEvent* event) override;
    virtual void drawCardContent(QPainter& painter, const QRectF& rect);

    void drawButtonChrome(QPainter& painter, const QRectF& rect);
    QColor currentPrimaryTextColor() const;

private:
    LayoutVariant m_layoutVariant;
    int m_baseCornerRadius { 6 };
    int m_hoverPlateAlpha { 90 };
    int m_pressShadowAlpha { 20 };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_SHARED_WIDGETS_CARDBUTTON_H
