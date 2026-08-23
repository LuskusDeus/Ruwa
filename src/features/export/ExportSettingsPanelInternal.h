// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   E X P O R T   S E T T I N G S   P A N E L   ( I N T E R N A L )
// ==========================================================================
//
//   Metrics shared by the panel's two translation units — the shell
//   (ExportSettingsPanel.cpp) and the section builders
//   (ExportSettingsPanelSections.cpp). Not part of the panel's public surface.
//

#ifndef RUWA_UI_WORKSPACE_EXPORTSETTINGSPANELINTERNAL_H
#define RUWA_UI_WORKSPACE_EXPORTSETTINGSPANELINTERNAL_H

#include <QtGlobal>

namespace ruwa::ui::workspace::panel_metrics {

constexpr int kCornerRadius = 12;
constexpr int kBorderWidth = 1;
constexpr int kPanelPadding = 16;

constexpr int kExitButtonSize = 28;
constexpr int kExitIconSize = 16;
constexpr int kTitleIconSize = 18;

/// Gap between two named groups (FORMAT, SIZE, ...).
constexpr int kSectionSpacing = 18;
/// Gap between a group's caption and its first control.
constexpr int kCaptionSpacing = 8;
/// Gap between two rows inside one group.
constexpr int kRowSpacing = 8;

constexpr int kFieldHeight = 30;
/// Matte swatch capsule. Fixed rather than stretched: the capsule holds a
/// swatch and a hex string and nothing else, so extra width is just empty pill.
constexpr int kColorSwatchHeight = 30;
constexpr int kColorSwatchWidth = 116;
constexpr int kSliderHeight = 32;

constexpr int kExportButtonBaseH = 36;
constexpr int kFooterTopSpacing = 12;
constexpr qreal kFooterDividerHeight = 1.5;

} // namespace ruwa::ui::workspace::panel_metrics

#endif // RUWA_UI_WORKSPACE_EXPORTSETTINGSPANELINTERNAL_H
