// SPDX-License-Identifier: MPL-2.0

// ThemePreviewWidget.h
#ifndef RUWA_UI_WIDGETS_SETTINGS_THEMEPREVIEWWIDGET_H
#define RUWA_UI_WIDGETS_SETTINGS_THEMEPREVIEWWIDGET_H

#include "shared/widgets/BaseStyledWidget.h"
#include "features/theme/manager/ThemePreset.h"
#include "shell/context-menu/IContextMenuProvider.h"

class QEvent;

namespace ruwa::ui::widgets {

/**
 * @brief Theme preview card widget
 *
 * Uses BaseStyledWidget with "ThemePreview" style.
 * Shows a visual preview of a theme with mock window.
 */
class ThemePreviewWidget : public BaseStyledWidget, public IContextMenuProvider {
    Q_OBJECT

public:
    explicit ThemePreviewWidget(
        const ruwa::ui::core::ThemePreset& preset, QWidget* parent = nullptr);
    ~ThemePreviewWidget() override = default;

    const ruwa::ui::core::ThemePreset& preset() const { return m_preset; }

    bool isSelected() const { return isActive(); }
    void setSelected(bool selected);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // IContextMenuProvider
    ContextMenuType contextMenuType() const override { return ContextMenuType::SimpleActions; }
    QVariantMap contextMenuContext() const override;

public slots:
    void onSimpleContextAction(int actionId);

signals:
    void selected(const ruwa::ui::core::ThemePreset& preset);

protected:
    void changeEvent(QEvent* event) override;

    /// Custom content: draws mock window and theme name
    void drawContentLayer(QPainter& painter, const QRectF& rect) override;

    /// Custom active background: uses preset's background color
    void drawActiveBackgroundLayer(QPainter& painter, const QRectF& rect) override;

    /// Custom active border: uses preset's primary color
    void drawActiveBorderLayer(QPainter& painter, const QRectF& rect) override;

private:
    QSize previewSizeHint() const;
    void drawMockWindow(QPainter& painter, const QRectF& rect);

private:
    ruwa::ui::core::ThemePreset m_preset;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_SETTINGS_THEMEPREVIEWWIDGET_H
