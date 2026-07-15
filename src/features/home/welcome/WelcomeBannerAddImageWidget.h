// SPDX-License-Identifier: MPL-2.0

// WelcomeBannerAddImageWidget.h
#ifndef RUWA_UI_WIDGETS_WELCOME_WELCOMEBANNERADDIMAGEWIDGET_H
#define RUWA_UI_WIDGETS_WELCOME_WELCOMEBANNERADDIMAGEWIDGET_H

#include "shared/widgets/BaseStyledWidget.h"

class QEvent;
class QMouseEvent;

namespace ruwa::ui::widgets {

/**
 * @brief Tile that opens an image picker for custom welcome banner images.
 */
class WelcomeBannerAddImageWidget : public BaseStyledWidget {
    Q_OBJECT

public:
    explicit WelcomeBannerAddImageWidget(QWidget* parent = nullptr);
    ~WelcomeBannerAddImageWidget() override = default;

    QSize sizeHint() const override;

signals:
    void clicked();

protected:
    void drawContentLayer(QPainter& painter, const QRectF& rect) override;
    void changeEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    bool m_pressed { false };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_WELCOME_WELCOMEBANNERADDIMAGEWIDGET_H
