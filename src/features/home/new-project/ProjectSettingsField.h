// SPDX-License-Identifier: MPL-2.0

// ProjectSettingsField.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_PROJECTSETTINGSFIELD_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_PROJECTSETTINGSFIELD_H

#include "shared/widgets/inputs/StyledInputField.h"

namespace ruwa::ui::widgets {

/**
 * @brief Labeled input field for New Project settings.
 *
 * Thin subclass of StyledInputField — adds semantic identity so
 * New Project code can forward-declare it without pulling in the
 * shared widget header everywhere.
 */
class ProjectSettingsField : public StyledInputField {
    Q_OBJECT

public:
    using FieldType = StyledInputField::FieldType;

    explicit ProjectSettingsField(const QString& label, FieldType type, QWidget* parent = nullptr)
        : StyledInputField(label, type, parent)
    {
    }

    ~ProjectSettingsField() override = default;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_PROJECTSETTINGSFIELD_H
