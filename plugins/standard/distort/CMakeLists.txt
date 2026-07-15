# SPDX-License-Identifier: MPL-2.0

# Ruwa.Standard.Distort — the standard Distort effect package.
ruwa_add_effect_plugin(Ruwa.Standard.Distort
    "Ruwa.Standard.Distort"
    src/DistortPlugin.c
    src/DistortCommon.c
    src/effects/Twirl.c
    src/effects/Pinch.c
    src/effects/Ripple.c
    src/effects/Wave.c
)
