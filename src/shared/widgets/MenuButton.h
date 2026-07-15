// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SHARED_WIDGETS_MENUBUTTON_H
#define RUWA_SHARED_WIDGETS_MENUBUTTON_H

#include "shared/widgets/BaseAnimatedButton.h"

#include <QPointer>

namespace ruwa::ui::widgets {

class MenuButton : public BaseAnimatedButton {
    Q_OBJECT

public:
    explicit MenuButton(const QString& text, QWidget* parent = nullptr);
    ~MenuButton() override = default;

    void setPopup(QWidget* popup);
    QWidget* popup() const { return m_popup; }

    void setMenuActive(bool active);
    bool isMenuActive() const { return m_menuActive; }

    void setText(const QString& text);
    void setBackgroundInsets(int horizontal, int vertical);

signals:
    void hoverEntered();

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;

private:
    void updateMetrics();

private slots:
    void onThemeChanged();

private:
    QPointer<QWidget> m_popup;
    bool m_menuActive { false };
    int m_baseHorizontalInset { 6 };
    int m_baseVerticalInset { 3 };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_SHARED_WIDGETS_MENUBUTTON_H
