// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_BRUSHES_BRUSHDYNAMICSSLOTUTILS_H
#define RUWA_CORE_BRUSHES_BRUSHDYNAMICSSLOTUTILS_H

#include "features/brush/manager/BrushSettingDefs.h"
#include "features/brush/manager/BrushSettings.h"

#include <QString>

#include <optional>

namespace ruwa::core::brushes {

struct BrushEngineDescriptor;

/// Shared plumbing between every UI that edits parameter dynamics (brush editor
/// overlay, brush settings panel popup): how a slot is read out of the settings,
/// which sources a setting exposes, and how an edited slot is written back.

BrushDynamicsSlot defaultDynamicsSlotForSetting(const QString& settingKey);

/// Pressure binding reconstructed from the legacy min/max pressure fields, used
/// when a brush has no stored dynamics yet.
BrushDynamicsBinding legacyPressureBindingForSetting(
    const BrushSettingsData& settings, BrushDynamicsSettingKey setting);

/// Slot to show in an editor: stored bindings, else the legacy pressure state,
/// else the default slot.
BrushDynamicsSlot dynamicsSlotForSetting(
    const BrushSettingsData& settings, const QString& settingKey);

std::optional<BrushSettingDef> findSettingDef(
    const BrushEngineDescriptor& descriptor, const QString& settingKey);

BrushDynamicTargetDef dynamicsTargetForSetting(const QString& engineId, const QString& settingKey);

/// Mirrors an edited slot back into the legacy min/max pressure fields.
void syncLegacyPressureState(BrushSettingsData& settings, const BrushDynamicsSlot& slot);

/// Normalizes and stores the slot in `settings`. Returns false for settings that
/// do not support dynamics.
bool applyDynamicsSlotForSetting(
    BrushSettingsData& settings, const QString& settingKey, const BrushDynamicsSlot& slot);

bool hasEffectiveDynamics(const BrushSettingsData& settings, BrushDynamicsSettingKey setting);

/// Current (static) value of the setting the dynamics are bound to; the curve
/// editor uses it as the top of the value axis.
float baseValueForDynamicsSetting(
    const BrushSettingsData& settings, BrushDynamicsSettingKey setting);

} // namespace ruwa::core::brushes

#endif // RUWA_CORE_BRUSHES_BRUSHDYNAMICSSLOTUTILS_H
