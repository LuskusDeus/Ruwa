// SPDX-License-Identifier: MPL-2.0

// CustomThemesNavigatorWidget.h
#ifndef RUWA_UI_WIDGETS_SETTINGS_CUSTOMTHEMESNAVIGATORWIDGET_H
#define RUWA_UI_WIDGETS_SETTINGS_CUSTOMTHEMESNAVIGATORWIDGET_H

#include "shared/widgets/BaseStyledWidget.h"

class QEvent;

namespace ruwa::ui::widgets {

/**
 * @brief Custom themes navigator widget
 *
 * A clickable widget that opens the theme editor.
 * Uses BaseStyledWidget with "ThemePreview" style for consistency.
 * Shows an icon/illustration and "Custom Themes" text.
 */
class CustomThemesNavigatorWidget : public BaseStyledWidget {
    Q_OBJECT

public:
    explicit CustomThemesNavigatorWidget(QWidget* parent = nullptr);
    ~CustomThemesNavigatorWidget() override = default;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void clicked();

protected:
    /// Custom content: draws arrow icon and "Custom Themes" text
    void drawContentLayer(QPainter& painter, const QRectF& rect) override;
    void changeEvent(QEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QSize previewSizeHint() const;
    bool m_pressed { false };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_SETTINGS_CUSTOMTHEMESNAVIGATORWIDGET_H
