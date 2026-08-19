// SPDX-License-Identifier: MPL-2.0

// ProjectPresets.cpp
#include "ProjectPresets.h"

#include <QCoreApplication>

namespace ruwa::ui::widgets {

namespace {
// UI strings for these keys live in the NewProjectContent context (that is where they
// have always been translated from) — keep the literal so lupdate keeps the same context.
const char kNewProjectPresetCtx[] = "ruwa::ui::widgets::NewProjectContent";
} // namespace

const QList<PresetCategory>& ProjectPresets::categories()
{
    static const QList<PresetCategory> kCategories = {
        { QStringLiteral("Basics"),
            {
                { QStringLiteral("Quick Sketch"), QSize(2048, 2048) },
                { QStringLiteral("Square Canvas"), QSize(3000, 3000) },
                { QStringLiteral("Landscape Canvas"), QSize(4000, 3000) },
                { QStringLiteral("Portrait Canvas"), QSize(3000, 4000) },
                { QStringLiteral("Large Square"), QSize(4096, 4096) },
            } },
        { QStringLiteral("Screen"),
            {
                { QStringLiteral("Full HD"), QSize(1920, 1080) },
                { QStringLiteral("QHD"), QSize(2560, 1440) },
                { QStringLiteral("4K UHD"), QSize(3840, 2160) },
                { QStringLiteral("Ultrawide"), QSize(3440, 1440) },
                { QStringLiteral("Vertical Screen"), QSize(2160, 3840) },
            } },
        { QStringLiteral("Illustration"),
            {
                { QStringLiteral("Illustration Portrait"), QSize(4000, 5000) },
                { QStringLiteral("Illustration Large Portrait"), QSize(5000, 7000) },
                { QStringLiteral("Illustration Landscape"), QSize(6000, 4000) },
                { QStringLiteral("Cinematic Matte"), QSize(6000, 3375) },
                { QStringLiteral("Concept Sheet"), QSize(7000, 7000) },
            } },
        { QStringLiteral("Comics & Manga"),
            {
                { QStringLiteral("Manga A4"), QSize(2480, 3508) },
                { QStringLiteral("Manga B5"), QSize(2079, 2953) },
                { QStringLiteral("US Comic Page"), QSize(2550, 3900) },
                { QStringLiteral("Comic Spread"), QSize(5100, 3900) },
                { QStringLiteral("Webtoon Episode"), QSize(1600, 12000) },
            } },
        { QStringLiteral("Print"),
            {
                { QStringLiteral("A5"), QSize(1748, 2480) },
                { QStringLiteral("A4"), QSize(2480, 3508) },
                { QStringLiteral("A3"), QSize(3508, 4961) },
                { QStringLiteral("Letter"), QSize(2550, 3300) },
                { QStringLiteral("Poster A2"), QSize(4961, 7016) },
                { QStringLiteral("Poster A1"), QSize(7016, 9933) },
            } },
        { QStringLiteral("Covers & Posters"),
            {
                { QStringLiteral("Book Cover"), QSize(3000, 4500) },
                { QStringLiteral("Album Cover"), QSize(3000, 3000) },
                { QStringLiteral("Vertical Poster"), QSize(4000, 6000) },
                { QStringLiteral("Landscape Poster"), QSize(6000, 4000) },
                { QStringLiteral("Square Cover"), QSize(4000, 4000) },
            } },
        { QStringLiteral("Pixel Art"),
            {
                { QStringLiteral("Sprite 64"), QSize(64, 64) },
                { QStringLiteral("Sprite 128"), QSize(128, 128) },
                { QStringLiteral("Tile Set 256"), QSize(256, 256) },
                { QStringLiteral("Pixel Scene"), QSize(512, 512) },
                { QStringLiteral("Pixel HD"), QSize(1024, 1024) },
            } },
    };
    return kCategories;
}

QString ProjectPresets::matchingNameKey(const QSize& size)
{
    if (!size.isValid() || size.isEmpty()) {
        return {};
    }

    for (const PresetCategory& category : categories()) {
        for (const Preset& preset : category.presets) {
            if (preset.size == size) {
                return preset.nameKey;
            }
        }
    }
    return {};
}

QString ProjectPresets::translated(const QString& nameKey)
{
    if (nameKey.isEmpty()) {
        return {};
    }
    const QByteArray key = nameKey.toUtf8();
    return QCoreApplication::translate(kNewProjectPresetCtx, key.constData());
}

} // namespace ruwa::ui::widgets
