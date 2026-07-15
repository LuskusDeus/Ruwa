# SPDX-License-Identifier: MPL-2.0

# Ruwa.Standard.Stylize — the standard Stylize effect package.
ruwa_add_effect_plugin(Ruwa.Standard.Stylize
    "Ruwa.Standard.Stylize"
    src/StylizePlugin.c
    src/StylizeCommon.c
    src/effects/Glow.c
    src/effects/Stroke.c
    src/effects/GradientOverlay.c
    src/effects/Halftone.c
    src/effects/LayerStyles.c
)
