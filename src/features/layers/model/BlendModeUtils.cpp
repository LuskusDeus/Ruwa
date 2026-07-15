// SPDX-License-Identifier: MPL-2.0

#include "features/layers/model/BlendModeUtils.h"

#include <QCoreApplication>
#include <QtGlobal>

namespace ruwa::core::layers {

bool isValidBlendModeValue(int value)
{
    return value >= 0 && value < kBlendModeCount;
}

BlendMode blendModeFromValue(int value, BlendMode fallback)
{
    if (!isValidBlendModeValue(value)) {
        return fallback;
    }

    return static_cast<BlendMode>(value);
}

const QVector<BlendModeCategoryDef>& blendModeCategoryDefs()
{
    static const QVector<BlendModeCategoryDef> categories = {
        { QT_TR_NOOP("Basic"),
            {
                BlendMode::Normal,
                BlendMode::Dissolve,
            } },
        { QT_TR_NOOP("Darken"),
            {
                BlendMode::Darken,
                BlendMode::Multiply,
                BlendMode::ColorBurn,
                BlendMode::LinearBurn,
                BlendMode::DarkerColor,
            } },
        { QT_TR_NOOP("Lighten"),
            {
                BlendMode::Lighten,
                BlendMode::Screen,
                BlendMode::ColorDodge,
                BlendMode::LinearDodge,
                BlendMode::LighterColor,
            } },
        { QT_TR_NOOP("Contrast"),
            {
                BlendMode::Overlay,
                BlendMode::SoftLight,
                BlendMode::HardLight,
                BlendMode::VividLight,
                BlendMode::LinearLight,
                BlendMode::PinLight,
                BlendMode::HardMix,
            } },
        { QT_TR_NOOP("Comparison"),
            {
                BlendMode::Difference,
                BlendMode::Exclusion,
                BlendMode::Subtract,
                BlendMode::Divide,
            } },
        { QT_TR_NOOP("Component"),
            {
                BlendMode::Hue,
                BlendMode::Saturation,
                BlendMode::Color,
                BlendMode::Luminosity,
            } },
    };
    return categories;
}

QString blendModeDisplayName(BlendMode mode, const char* context)
{
    switch (mode) {
    case BlendMode::Normal:
        return QCoreApplication::translate(context, "Normal");
    case BlendMode::Multiply:
        return QCoreApplication::translate(context, "Multiply");
    case BlendMode::Screen:
        return QCoreApplication::translate(context, "Screen");
    case BlendMode::Overlay:
        return QCoreApplication::translate(context, "Overlay");
    case BlendMode::SoftLight:
        return QCoreApplication::translate(context, "Soft Light");
    case BlendMode::HardLight:
        return QCoreApplication::translate(context, "Hard Light");
    case BlendMode::ColorDodge:
        return QCoreApplication::translate(context, "Color Dodge");
    case BlendMode::ColorBurn:
        return QCoreApplication::translate(context, "Color Burn");
    case BlendMode::Darken:
        return QCoreApplication::translate(context, "Darken");
    case BlendMode::Lighten:
        return QCoreApplication::translate(context, "Lighten");
    case BlendMode::Difference:
        return QCoreApplication::translate(context, "Difference");
    case BlendMode::Exclusion:
        return QCoreApplication::translate(context, "Exclusion");
    case BlendMode::Dissolve:
        return QCoreApplication::translate(context, "Dissolve");
    case BlendMode::LinearBurn:
        return QCoreApplication::translate(context, "Linear Burn");
    case BlendMode::DarkerColor:
        return QCoreApplication::translate(context, "Darker Color");
    case BlendMode::LinearDodge:
        return QCoreApplication::translate(context, "Linear Dodge (Add)");
    case BlendMode::LighterColor:
        return QCoreApplication::translate(context, "Lighter Color");
    case BlendMode::VividLight:
        return QCoreApplication::translate(context, "Vivid Light");
    case BlendMode::LinearLight:
        return QCoreApplication::translate(context, "Linear Light");
    case BlendMode::PinLight:
        return QCoreApplication::translate(context, "Pin Light");
    case BlendMode::HardMix:
        return QCoreApplication::translate(context, "Hard Mix");
    case BlendMode::Subtract:
        return QCoreApplication::translate(context, "Subtract");
    case BlendMode::Divide:
        return QCoreApplication::translate(context, "Divide");
    case BlendMode::Hue:
        return QCoreApplication::translate(context, "Hue");
    case BlendMode::Saturation:
        return QCoreApplication::translate(context, "Saturation");
    case BlendMode::Color:
        return QCoreApplication::translate(context, "Color");
    case BlendMode::Luminosity:
        return QCoreApplication::translate(context, "Luminosity");
    }

    return QCoreApplication::translate(context, "Normal");
}

} // namespace ruwa::core::layers
