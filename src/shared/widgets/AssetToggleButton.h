// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SHARED_WIDGETS_ASSETTOGGLEBUTTON_H
#define RUWA_SHARED_WIDGETS_ASSETTOGGLEBUTTON_H

#include "shared/resources/IconProvider.h"
#include "shared/widgets/BaseAnimatedButton.h"

class QTimer;
class QPainter;
class QRectF;

namespace ruwa::ui::widgets {

class AssetToggleButton : public BaseAnimatedButton {
    Q_OBJECT

public:
    explicit AssetToggleButton(QWidget* parent = nullptr);
    ~AssetToggleButton() override = default;

    void setIconType(ruwa::ui::core::IconProvider::StandardIcon iconType);
    void setBaseSize(int size);
    void setBaseIconSize(int size);
    void setHoverLeaveDebounceMs(int ms);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void updateScaledSize();
    void drawIcon(QPainter& painter, const QRectF& rect);

private slots:
    void onThemeChanged();

private:
    ruwa::ui::core::IconProvider::StandardIcon m_iconType {
        ruwa::ui::core::IconProvider::StandardIcon::Link
    };
    QTimer* m_hoverLeaveDebounce { nullptr };
    int m_baseSize { 30 };
    int m_baseIconSize { 14 };
    int m_baseBorderRadius { 6 };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_SHARED_WIDGETS_ASSETTOGGLEBUTTON_H
