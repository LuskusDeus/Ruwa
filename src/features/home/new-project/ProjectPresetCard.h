// SPDX-License-Identifier: MPL-2.0

// ProjectPresetCard.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_PROJECTPRESETCARD_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_PROJECTPRESETCARD_H

#include "shared/widgets/BaseStyledWidget.h"
#include <QSize>

class QEvent;

namespace ruwa::ui::widgets {

/**
 * @brief Card widget for project presets/templates
 *
 * Uses BaseStyledWidget with "PresetCard" (faint overlay fill; subtle border + hover plate).
 * Features:
 * - Name + dimensions on the left, aspect preview on the right
 * - Selection state with smooth transitions
 */
class ProjectPresetCard : public BaseStyledWidget {
    Q_OBJECT

public:
    explicit ProjectPresetCard(
        const QString& nameKey, const QSize& dimensions, QWidget* parent = nullptr);
    ~ProjectPresetCard() override = default;

    /// Set whether this preset is selected
    void setSelected(bool selected) { setActive(selected); }
    bool isSelected() const { return isActive(); }

    /// Get preset info (stable English key used for maps and translation lookup)
    QString presetName() const { return m_nameKey; }
    QSize dimensions() const { return m_dimensions; }

protected:
    void changeEvent(QEvent* event) override;

    /// Custom content: draws aspect ratio preview and text
    void drawContentLayer(QPainter& painter, const QRectF& rect) override;

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    void drawAspectRatioPreview(QPainter& painter, const QRectF& previewRect);
    int layoutContentHeightForInnerWidth(int innerWidth) const;

private:
    QString m_nameKey;
    QSize m_dimensions;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_PROJECTPRESETCARD_H
