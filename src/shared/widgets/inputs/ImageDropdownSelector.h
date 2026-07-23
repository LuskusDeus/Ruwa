// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_COMMON_IMAGEDROPDOWNSELECTOR_H
#define RUWA_UI_WIDGETS_COMMON_IMAGEDROPDOWNSELECTOR_H

#include "shared/widgets/inputs/AnimatedComboBox.h"

namespace ruwa::ui::widgets {

struct ImageDropdownItem {
    QString text;
    QString subtitle;
    QVariant userData;
    QImage previewImage;
    QColor previewTint;
    bool tintPreview = false;
    bool enabled = true;
};

/**
 * Card-and-preview presentation of AnimatedComboBox.
 *
 * Popup animation, focus handling, keyboard navigation, positioning, scrolling,
 * categories and separators are owned by AnimatedComboBox. This class only
 * provides the image-oriented item API and preview-grid defaults.
 */
class ImageDropdownSelector : public AnimatedComboBox {
    Q_OBJECT

public:
    explicit ImageDropdownSelector(QWidget* parent = nullptr);

    void addItem(const ImageDropdownItem& item);
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_IMAGEDROPDOWNSELECTOR_H
