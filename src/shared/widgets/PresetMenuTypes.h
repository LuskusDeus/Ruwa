// SPDX-License-Identifier: MPL-2.0

// PresetMenuTypes.h
#ifndef RUWA_UI_WIDGETS_PRESETMENUTYPES_H
#define RUWA_UI_WIDGETS_PRESETMENUTYPES_H

#include "shared/resources/IconProvider.h"

#include <QColor>
#include <QImage>
#include <QVector>
#include <QVariant>
#include <QString>

namespace ruwa::ui::widgets {

/**
 * @brief Extra icon action on a preset row (in addition to rename/delete).
 * Placed to the left of the rename button. Order in the vector is left→right
 * (first entry sits closest to the title text).
 */
struct PresetMenuExtraAction {
    /// Passed to extraActionTriggered(userData, id).
    int id = 0;
    QString text;
    ruwa::ui::core::IconProvider::StandardIcon icon
        = ruwa::ui::core::IconProvider::StandardIcon::Confirm;
    bool checked = false;
    bool dangerHover = false;
    /// When true, draws a star (filled if checked) instead of @ref icon.
    bool useStarToggle = false;
};

struct PresetMenuHeaderAction {
    /// Passed to headerActionTriggered(id).
    int id = 0;
    QString toolTip;
    QString text;
    ruwa::ui::core::IconProvider::StandardIcon icon
        = ruwa::ui::core::IconProvider::StandardIcon::BasicFile;
    bool accent = false;
    bool visible = true;
};

struct PresetMenuItem {
    QString title;
    QString subtitle;
    QString badgeText;
    QColor badgeTint;
    QVector<QColor> previewColors;
    QImage previewImage;
    ruwa::ui::core::IconProvider::StandardIcon previewIcon
        = ruwa::ui::core::IconProvider::StandardIcon::Appearance;
    ruwa::ui::core::IconProvider::StandardIcon titleTrailingIcon
        = ruwa::ui::core::IconProvider::StandardIcon::Confirm;
    bool fillPreviewBackground = false;
    bool previewWide = false;
    bool previewFrameless = false;
    bool hasTitleTrailingIcon = false;
    QString searchText;
    QVariant userData;
    bool deletable = true;
    bool renamable = true;
    /// When true, row is a non-interactive horizontal rule (ignore other fields).
    bool isDivider = false;
    QVector<PresetMenuExtraAction> extraActions;
};

} // namespace ruwa::ui::widgets

#endif
