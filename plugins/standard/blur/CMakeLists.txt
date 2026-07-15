# SPDX-License-Identifier: MPL-2.0

# Ruwa.Standard.Blur — the standard Blur effect package.
ruwa_add_effect_plugin(Ruwa.Standard.Blur
    "Ruwa.Standard.Blur"
    src/BlurPlugin.c
    src/BlurCommon.c
    src/effects/GaussianBlur.c
    src/effects/BoxBlur.c
    src/effects/MotionBlur.c
    src/effects/RadialBlur.c
    src/effects/Sharpen.c
    src/effects/UnsharpMask.c
)
