// SPDX-License-Identifier: MPL-2.0

#include "ImageDropdownSelector.h"

namespace ruwa::ui::widgets {

ImageDropdownSelector::ImageDropdownSelector(QWidget* parent)
    : AnimatedComboBox(parent)
{
    setFixedHeight(30);
    setPopupPresentation(PopupPresentation::PreviewGrid);
    setPopupMinWidth(120);
    setPopupColumns(2);
    setPopupCardSize(QSize(136, 104));
    setPopupMaxHeight(420);
}

void ImageDropdownSelector::addItem(const ImageDropdownItem& item)
{
    AnimatedComboItem comboItem;
    comboItem.text = item.text;
    comboItem.subtitle = item.subtitle;
    comboItem.userData = item.userData;
    comboItem.previewImage = item.previewImage;
    comboItem.previewTint = item.previewTint;
    comboItem.tintPreview = item.tintPreview;
    comboItem.enabled = item.enabled;
    AnimatedComboBox::addItem(comboItem);
}

} // namespace ruwa::ui::widgets
