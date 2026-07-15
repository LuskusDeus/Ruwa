// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_THEMEEDITOR_DETAILEDTHEMEPREVIEW_H
#define RUWA_UI_WIDGETS_THEMEEDITOR_DETAILEDTHEMEPREVIEW_H

#include <QWidget>
#include <QPainter>
#include "features/theme/manager/ThemePreset.h"

namespace ruwa::ui::widgets {

class DetailedThemePreview : public QWidget {
    Q_OBJECT

public:
    explicit DetailedThemePreview(QWidget* parent = nullptr);
    void setPreviewTheme(const ruwa::ui::core::ThemePreset& preset);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    ruwa::ui::core::ThemePreset m_theme;
    qreal m_scale { 1.0 }; // Current scale factor for responsive sizing

    // --- Pointer Types ---
    enum class PointerType {
        Full, // Dot + Line + Label (default for UI elements)
        LineOnly, // Line + Label (for borders/edges)
        LabelOnly // Label only (for backgrounds/large areas)
    };

    // --- Drawing Primitives ---
    void drawDotGrid(QPainter& painter, const QRect& rect);

    // --- UI Components ---
    void drawHeroCard(QPainter& painter, const QRect& rect);
    void drawFloatingListPanel(QPainter& painter, const QRect& rect);

    // --- Design Annotations (Pointers) ---
    void drawPointer(QPainter& painter, QPoint target, QString label, QPoint offset,
        PointerType type = PointerType::Full);

    // --- Scaling ---
    void calculateScale();
    int scaled(int value) const { return qRound(value * m_scale); }
    QPoint scaled(const QPoint& pt) const { return QPoint(scaled(pt.x()), scaled(pt.y())); }
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_THEMEEDITOR_DETAILEDTHEMEPREVIEW_H
