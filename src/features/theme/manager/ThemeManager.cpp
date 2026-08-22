// SPDX-License-Identifier: MPL-2.0

// ThemeManager.cpp
#include "ThemeManager.h"
#include "shared/resources/FontFamilyNames.h"
#include "shared/resources/ResourceManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/resources/FontManager.h"
#include "shared/style/WidgetStyleManager.h"
#include "features/settings/SettingsManager.h"

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QTimer>
#include <QWidget>
#include <QElapsedTimer> // [ThemeProfile] temporary

namespace ruwa::ui::core {

thread_local const ThemeColors* ThemeManager::s_colorOverride = nullptr;

// Delay before coalesced apply fires (ms).
// Short enough to feel instant, long enough to collapse rapid changes
// (e.g. dragging a color slider in theme editor).
static constexpr int kCoalesceDelayMs = 30;

static ThemeFonts normalizedThemeFonts(ThemeFonts fonts)
{
    fonts.uiFont = FontFamilyNames::migrateLegacyFamilyName(fonts.uiFont);
    fonts.codeFont = FontFamilyNames::migrateLegacyFamilyName(fonts.codeFont);
    fonts.titleFont = FontFamilyNames::migrateLegacyFamilyName(fonts.titleFont);
    fonts.sizes.normalize();
    return fonts;
}

static ThemeFontSizes loadThemeFontSizes(QSettings& settings)
{
    const int legacyUiSize = settings.value(QStringLiteral("fonts/uiSize"), 9).toInt();
    const int legacyCodeSize = settings.value(QStringLiteral("fonts/codeSize"), 9).toInt();
    const int legacyTitleSize = settings.value(QStringLiteral("fonts/titleSize"), 16).toInt();
    const ThemeFontSizes fallback
        = ThemeFontSizes::fromLegacy(legacyUiSize, legacyCodeSize, legacyTitleSize);

    ThemeFontSizes sizes;
    sizes.display = settings.value(QStringLiteral("fonts/sizes/display"), fallback.display).toInt();
    sizes.h0 = settings.value(QStringLiteral("fonts/sizes/h0"), fallback.h0).toInt();
    sizes.h1 = settings.value(QStringLiteral("fonts/sizes/h1"), fallback.h1).toInt();
    sizes.h2 = settings.value(QStringLiteral("fonts/sizes/h2"), fallback.h2).toInt();
    sizes.h3 = settings.value(QStringLiteral("fonts/sizes/h3"), fallback.h3).toInt();
    sizes.h4 = settings.value(QStringLiteral("fonts/sizes/h4"), fallback.h4).toInt();
    sizes.h5 = settings.value(QStringLiteral("fonts/sizes/h5"), fallback.h5).toInt();
    sizes.h6 = settings.value(QStringLiteral("fonts/sizes/h6"), fallback.h6).toInt();
    sizes.subtitle
        = settings.value(QStringLiteral("fonts/sizes/subtitle"), fallback.subtitle).toInt();
    sizes.bodyLarge
        = settings.value(QStringLiteral("fonts/sizes/bodyLarge"), fallback.bodyLarge).toInt();
    sizes.label = settings.value(QStringLiteral("fonts/sizes/label"), fallback.label).toInt();
    sizes.body = settings.value(QStringLiteral("fonts/sizes/body"), fallback.body).toInt();
    sizes.small = settings.value(QStringLiteral("fonts/sizes/small"), fallback.small).toInt();
    sizes.caption = settings.value(QStringLiteral("fonts/sizes/caption"), fallback.caption).toInt();
    sizes.micro = settings.value(QStringLiteral("fonts/sizes/micro"), fallback.micro).toInt();
    sizes.code = settings.value(QStringLiteral("fonts/sizes/code"), fallback.code).toInt();
    sizes.normalize();
    return sizes;
}

static void saveThemeFontSizes(QSettings& settings, const ThemeFontSizes& sizes)
{
    settings.setValue(QStringLiteral("fonts/sizes/display"), sizes.display);
    settings.setValue(QStringLiteral("fonts/sizes/h0"), sizes.h0);
    settings.setValue(QStringLiteral("fonts/sizes/h1"), sizes.h1);
    settings.setValue(QStringLiteral("fonts/sizes/h2"), sizes.h2);
    settings.setValue(QStringLiteral("fonts/sizes/h3"), sizes.h3);
    settings.setValue(QStringLiteral("fonts/sizes/h4"), sizes.h4);
    settings.setValue(QStringLiteral("fonts/sizes/h5"), sizes.h5);
    settings.setValue(QStringLiteral("fonts/sizes/h6"), sizes.h6);
    settings.setValue(QStringLiteral("fonts/sizes/subtitle"), sizes.subtitle);
    settings.setValue(QStringLiteral("fonts/sizes/bodyLarge"), sizes.bodyLarge);
    settings.setValue(QStringLiteral("fonts/sizes/label"), sizes.label);
    settings.setValue(QStringLiteral("fonts/sizes/body"), sizes.body);
    settings.setValue(QStringLiteral("fonts/sizes/small"), sizes.small);
    settings.setValue(QStringLiteral("fonts/sizes/caption"), sizes.caption);
    settings.setValue(QStringLiteral("fonts/sizes/micro"), sizes.micro);
    settings.setValue(QStringLiteral("fonts/sizes/code"), sizes.code);

    // Keep legacy anchors so older Ruwa versions can still open a saved theme.
    settings.setValue(QStringLiteral("fonts/uiSize"), sizes.body);
    settings.setValue(QStringLiteral("fonts/codeSize"), sizes.code);
    settings.setValue(QStringLiteral("fonts/titleSize"), sizes.h4);
}

ThemeManager::ThemeManager()
    : QObject(nullptr)
{
}

ThemeManager::~ThemeManager()
{
    delete m_coalesceTimer;
}

ThemeManager& ThemeManager::instance()
{
    static ThemeManager instance;
    return instance;
}

void ThemeManager::initialize()
{
    if (m_initialized) {
        return;
    }

    // Setup coalescing timer (single-shot, fires executePendingApply)
    m_coalesceTimer = new QTimer(this);
    m_coalesceTimer->setSingleShot(true);
    m_coalesceTimer->setInterval(kCoalesceDelayMs);
    connect(m_coalesceTimer, &QTimer::timeout, this, &ThemeManager::executePendingApply);

    // Drive visibility-gated per-widget handlers off the same themeChanged signal.
    connect(this, &ThemeManager::themeChanged, this, &ThemeManager::dispatchThemeHandlers);

    // Initialize resource managers
    ResourceManager::instance().initialize();
    FontManager::instance().initialize();
    // Load presets
    loadBuiltInPresets();
    loadCustomPresets();

    // Load UI scale from settings
    const auto& settings = ruwa::core::SettingsManager::instance().settings();
    m_scaleIndex = qBound(0, settings.appearance.uiScale, 2);
    static const qreal scaleFactors[] = { 0.85, 1.0, 1.15 };
    m_scaleFactor = scaleFactors[m_scaleIndex];
    // Connect to scale changes from SettingsManager
    connect(&ruwa::core::SettingsManager::instance(), &ruwa::core::SettingsManager::uiScaleChanged,
        this, &ThemeManager::setScaleIndex);

    // Load and apply saved theme (or default if not found)
    QSettings qsettings;
    QString savedThemeId = qsettings.value("Themes/CurrentTheme", QString()).toString();

    QUuid themeToApply;
    bool themeFound = false;

    if (!savedThemeId.isEmpty()) {
        themeToApply = QUuid::fromString(savedThemeId);

        for (const auto& preset : m_presets) {
            if (preset.id == themeToApply) {
                themeFound = true;
                break;
            }
        }
    }

    // During init, apply directly (no coalescing — nothing to coalesce yet)
    if (themeFound) {
        applyPreset(themeToApply);
    } else if (!m_presets.isEmpty()) {
        applyPreset(m_presets.first().id);
    }

    // The theme stylesheet is now STATIC (palette-driven) and is applied to qApp
    // exactly once, here. applyPreset() above already set the palette, so the
    // palette(...) references resolve. Subsequent theme changes only swap the
    // palette (cheap) — they never re-set the app stylesheet.
    generateStyleSheet();
    applyStyleSheet();

    m_initialized = true;
}

void ThemeManager::loadBuiltInPresets()
{
    m_presets = ThemePreset::builtInThemes();

    // Load favorite states for built-in themes from QSettings
    QSettings settings;
    settings.beginGroup("Themes/Favorites");

    for (auto& preset : m_presets) {
        bool isFavorite = settings.value(preset.id.toString(), true).toBool();
        preset.isFavorite = isFavorite;
    }

    settings.endGroup();
}

void ThemeManager::loadCustomPresets()
{
    QSettings settings;
    settings.beginGroup("Themes/Custom");

    int count = settings.value("count", 0).toInt();

    for (int i = 0; i < count; ++i) {
        settings.beginGroup(QString::number(i));

        ThemePreset preset;
        preset.id = QUuid::fromString(settings.value("id").toString());
        preset.name = settings.value("name").toString();
        preset.description = settings.value("description").toString();
        preset.isBuiltIn = false;
        preset.isDark = settings.value("isDark", true).toBool();
        preset.isFavorite = settings.value("isFavorite", false).toBool();

        // Load colors
        preset.primary = QColor(settings.value("primary").toString());
        preset.background = QColor(settings.value("background").toString());
        preset.surface = QColor(settings.value("surface").toString());
        preset.surfaceAlt = QColor(settings.value("surfaceAlt").toString());
        preset.border = QColor(settings.value("border").toString());

        preset.text = QColor(settings.value("text").toString());
        preset.textMuted = QColor(settings.value("textMuted").toString());
        preset.textOnPrimary = QColor(settings.value("textOnPrimary").toString());

        preset.overlayColor = QColor(settings.value("overlayColor").toString());

        preset.success = QColor(settings.value("success").toString());
        preset.warning = QColor(settings.value("warning").toString());
        preset.error = QColor(settings.value("error").toString());
        preset.info = QColor(settings.value("info").toString());
        preset.accent = QColor(settings.value("accent", QColor(124, 92, 252).name()).toString());

        // Load fonts (with defaults if not saved)
        preset.fonts.uiFont = settings.value("fonts/ui", FontFamilyNames::JetBrainsMono).toString();
        preset.fonts.codeFont
            = settings.value("fonts/code", FontFamilyNames::JetBrainsMono).toString();
        preset.fonts.titleFont
            = settings.value("fonts/title", FontFamilyNames::IBMPlexSansCondensed).toString();
        preset.fonts.sizes = loadThemeFontSizes(settings);
        preset.fonts = normalizedThemeFonts(preset.fonts);

        preset.animations.enabled = settings.value("animations/enabled", true).toBool();
        preset.animations.canvasEnabled = settings.value("animations/canvasEnabled", true).toBool();
        preset.animations.speed = qBound(WidgetStyleManager::kMinAnimationSpeed,
            settings.value("animations/speed", 1.0).toReal(),
            WidgetStyleManager::kMaxAnimationSpeed);

        settings.endGroup();

        // Validate and add
        if (!preset.id.isNull() && !preset.name.isEmpty()) {
            m_presets.append(preset);
        }
    }

    settings.endGroup();
}

void ThemeManager::saveCustomPresets()
{
    QSettings settings;

    // Get custom presets only
    QVector<ThemePreset> custom;
    for (const auto& preset : m_presets) {
        if (!preset.isBuiltIn) {
            custom.append(preset);
        }
    }

    // Clear old custom themes
    settings.beginGroup("Themes/Custom");
    settings.remove("");
    settings.endGroup();

    // Save new custom themes
    settings.beginGroup("Themes/Custom");
    settings.setValue("count", custom.size());

    for (int i = 0; i < custom.size(); ++i) {
        const ThemePreset& preset = custom[i];
        settings.beginGroup(QString::number(i));

        settings.setValue("id", preset.id.toString());
        settings.setValue("name", preset.name);
        settings.setValue("description", preset.description);
        settings.setValue("isDark", preset.isDark);
        settings.setValue("isFavorite", preset.isFavorite);

        // Save colors
        settings.setValue("primary", preset.primary.name());
        settings.setValue("background", preset.background.name());
        settings.setValue("surface", preset.surface.name());
        settings.setValue("surfaceAlt", preset.surfaceAlt.name());
        settings.setValue("border", preset.border.name());

        settings.setValue("text", preset.text.name());
        settings.setValue("textMuted", preset.textMuted.name());
        settings.setValue("textOnPrimary", preset.textOnPrimary.name());

        settings.setValue("overlayColor", preset.overlayColor.name());

        settings.setValue("success", preset.success.name());
        settings.setValue("warning", preset.warning.name());
        settings.setValue("error", preset.error.name());
        settings.setValue("info", preset.info.name());
        settings.setValue("accent", preset.accent.name());

        // Save fonts
        settings.setValue("fonts/ui", preset.fonts.uiFont);
        settings.setValue("fonts/code", preset.fonts.codeFont);
        settings.setValue("fonts/title", preset.fonts.titleFont);
        saveThemeFontSizes(settings, preset.fonts.sizes);

        settings.setValue("animations/enabled", preset.animations.enabled);
        settings.setValue("animations/canvasEnabled", preset.animations.canvasEnabled);
        settings.setValue("animations/speed", preset.animations.speed);

        settings.endGroup();
    }

    settings.endGroup();
    settings.sync();
}

QVector<ThemePreset> ThemeManager::builtInPresets() const
{
    QVector<ThemePreset> result;
    for (const auto& preset : m_presets) {
        if (preset.isBuiltIn) {
            result.append(preset);
        }
    }
    return result;
}

QVector<ThemePreset> ThemeManager::customPresets() const
{
    QVector<ThemePreset> result;
    for (const auto& preset : m_presets) {
        if (!preset.isBuiltIn) {
            result.append(preset);
        }
    }
    return result;
}

const ThemePreset* ThemeManager::currentPreset() const
{
    for (const auto& preset : m_presets) {
        if (preset.id == m_currentPresetId) {
            return &preset;
        }
    }
    return nullptr;
}

bool ThemeManager::applyPreset(const QUuid& id)
{
    for (const auto& preset : m_presets) {
        if (preset.id == id) {
            return applyPreset(preset);
        }
    }

    return false;
}

bool ThemeManager::applyPreset(const ThemePreset& preset)
{
    m_currentPresetId = preset.id;

    auto& styleManager = WidgetStyleManager::instance();
    styleManager.setAnimationsEnabled(preset.animations.enabled);
    styleManager.setCanvasAnimationsEnabled(preset.animations.canvasEnabled);
    styleManager.setAnimationSpeed(preset.animations.speed);

    // Phase 1 (immediate): Update colors + palette for instant visual feedback.
    // QPalette propagation is fast — Qt doesn't re-parse stylesheets.
    applyColorsFromPreset(preset);
    applyPalette();

    // Emit lightweight signal — widgets that only need color values
    // (e.g. custom-painted widgets) can update immediately.
    emit colorsChanged();
    emit themeApplyStarted();

    // Phase 2 (coalesced): Heavy work — QSS, icons, themeChanged.
    // If another applyPreset arrives within kCoalesceDelayMs, only
    // the last one actually runs the expensive path.
    m_pendingPreset = preset;
    m_hasPendingApply = true;

    if (m_coalesceTimer) {
        m_coalesceTimer->start(); // (re)start timer
    } else {
        // During init (timer not yet created), execute immediately
        executePendingApply();
    }

    return true;
}

void ThemeManager::executePendingApply()
{
    if (!m_hasPendingApply) {
        return;
    }
    m_hasPendingApply = false;

    const ThemePreset& preset = m_pendingPreset;

    // [ThemeProfile] temporary timing — remove once tuned.
    QElapsedTimer profTimer;
    profTimer.start();
    const int profWidgetCount = qApp->allWidgets().size();

    // Freeze top-level widgets to batch repaints
    QWidgetList topLevelWidgets = qApp->topLevelWidgets();
    for (QWidget* w : topLevelWidgets) {
        if (w && w->isVisible()) {
            w->setUpdatesEnabled(false);
        }
    }

    // NOTE: the app stylesheet is static + palette-driven and is set once at
    // startup (see initialize()). A theme change only swapped the palette in
    // applyPreset()/applyColorsFromPreset() -> applyPalette() above, so there is
    // nothing expensive to do here. (Was: generateStyleSheet()+applyStyleSheet(),
    // which re-matched the whole QSS against every widget — seconds with many
    // open projects.)
    // Defer heavy work: icon reload + themeChanged + unfreeze
    const QUuid presetId = preset.id;
    QTimer::singleShot(0, this, [this, presetId, topLevelWidgets]() {
        QElapsedTimer t;
        t.start(); // [ThemeProfile]
        IconProvider::instance().reloadIcons();
        const qint64 profIcons = t.elapsed();

        QSettings qsettings;
        qsettings.setValue("Themes/CurrentTheme", presetId.toString());
        // (no sync() — QSettings persists on its own; avoids a possible blocking
        //  registry flush on the theme-apply path)
        const qint64 profBeforeUnfreeze = t.elapsed(); // [ThemeProfile]

        // Unfreeze BEFORE emitting themeChanged. Custom-painted chrome (top bar,
        // dock container background / gaps, panel title bars) refreshes by calling
        // update() from its themeChanged handler — but update() is a no-op while a
        // widget is frozen, which left that chrome showing the OLD colours until an
        // unrelated repaint or an app restart. With updates enabled first, those
        // update() calls actually schedule a repaint.
        for (QWidget* w : topLevelWidgets) {
            if (w) {
                w->setUpdatesEnabled(true);
            }
        }

        const qint64 profBeforeEmit = t.elapsed(); // [ThemeProfile]
        const qint64 profUnfreeze = profBeforeEmit - profBeforeUnfreeze;
        emit themeChanged();
        const qint64 profEmit = t.elapsed() - profBeforeEmit;

        // Force a repaint of every top level so any custom-painted surface that
        // reads colours live is guaranteed to redraw with the new palette.
        for (QWidget* w : topLevelWidgets) {
            if (w) {
                w->update();
            }
        }
    });
}

void ThemeManager::applyColorsFromPreset(const ThemePreset& preset)
{
    m_colors = colorsForPreset(preset);

    // Apply fonts from preset
    FontManager::instance().setUIFontFamily(m_colors.fonts.uiFont);
    FontManager::instance().setCodeFontFamily(m_colors.fonts.codeFont);
    FontManager::instance().setTitleFontFamily(m_colors.fonts.titleFont);
    FontManager::instance().applyToApplication(
        scaledFontSize(m_colors.fonts.sizes.value(ThemeFontRole::Body)));
}

ThemeColors ThemeManager::colorsForPreset(const ThemePreset& preset)
{
    ThemeColors colors;
    colors.primary = preset.primary;
    colors.background = preset.background;
    colors.surface = preset.surface;
    colors.surfaceAlt = preset.surfaceAlt;
    colors.border = preset.border;
    colors.text = preset.text;
    colors.textMuted = preset.textMuted;
    colors._textOnPrimary = preset.textOnPrimary;
    colors.overlayColor = preset.overlayColor;
    colors.success = preset.success;
    colors.warning = preset.warning;
    colors.error = preset.error;
    colors.info = preset.info;
    colors.accent = preset.accent.isValid() ? preset.accent : QColor(124, 92, 252);
    colors.isDark = preset.isDark;
    colors.fonts = normalizedThemeFonts(preset.fonts);
    return colors;
}

void ThemeManager::withColorOverride(const ThemeColors& colors, const std::function<void()>& render)
{
    if (!render) {
        return;
    }

    const ThemeColors* previous = s_colorOverride;
    s_colorOverride = &colors;
    struct OverrideRestorer {
        const ThemeColors* previous;
        ~OverrideRestorer() { ThemeManager::s_colorOverride = previous; }
    } restorer { previous };

    render();
}

void ThemeManager::applyTheme()
{
    // Mark tabs for per-tab refresh so background tabs defer their (gated) theme
    // handlers and flush on activation, same as a preset change.
    emit themeApplyStarted();

    QWidgetList topLevelWidgets = qApp->topLevelWidgets();
    for (QWidget* w : topLevelWidgets) {
        if (w && w->isVisible())
            w->setUpdatesEnabled(false);
    }

    applyPalette(); // stylesheet is static (palette-driven) — no re-set needed

    QTimer::singleShot(0, this, [this, topLevelWidgets]() {
        // Unfreeze before emitting so custom-painted chrome's update() isn't a
        // no-op (see executePendingApply), then force a repaint.
        for (QWidget* w : topLevelWidgets) {
            if (w)
                w->setUpdatesEnabled(true);
        }
        emit themeChanged();
        for (QWidget* w : topLevelWidgets) {
            if (w)
                w->update();
        }
    });
}

void ThemeManager::setScaleIndex(int index)
{
    index = qBound(0, index, 2);

    if (m_scaleIndex == index) {
        return;
    }

    m_scaleIndex = index;

    static const qreal scaleFactors[] = { 0.85, 1.0, 1.15 };
    m_scaleFactor = scaleFactors[index];
    FontManager::instance().applyToApplication(fontSize(ThemeFontRole::Body));

    // Scale affects per-tab layout/sizing too; route through the same per-tab
    // refresh path so background tabs defer + flush on activation.
    emit themeApplyStarted();

    QWidgetList topLevelWidgets = qApp->topLevelWidgets();
    for (QWidget* w : topLevelWidgets) {
        if (w && w->isVisible())
            w->setUpdatesEnabled(false);
    }

    applyPalette(); // stylesheet is static (palette-driven) — no re-set needed

    QTimer::singleShot(0, this, [this, topLevelWidgets]() {
        for (QWidget* w : topLevelWidgets) {
            if (w)
                w->setUpdatesEnabled(true);
        }
        emit themeChanged();
        for (QWidget* w : topLevelWidgets) {
            if (w)
                w->update();
        }
    });
}

// === Custom Theme Management ===

void ThemeManager::addCustomPreset(const ThemePreset& preset)
{
    for (const auto& p : m_presets) {
        if (p.id == preset.id) {
            return;
        }
    }

    ThemePreset newPreset = preset;
    newPreset.isBuiltIn = false;
    newPreset.fonts = normalizedThemeFonts(newPreset.fonts);

    m_presets.append(newPreset);
    saveCustomPresets();

    emit presetsChanged();
}

void ThemeManager::updateCustomPreset(const ThemePreset& preset)
{
    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets[i].id == preset.id && !m_presets[i].isBuiltIn) {
            ThemePreset updatedPreset = preset;
            updatedPreset.fonts = normalizedThemeFonts(updatedPreset.fonts);
            m_presets[i] = updatedPreset;
            m_presets[i].isBuiltIn = false;

            saveCustomPresets();

            if (m_currentPresetId == updatedPreset.id) {
                applyPreset(updatedPreset);
            }

            emit presetsChanged();
            return;
        }
    }
}

void ThemeManager::removeCustomPreset(const QUuid& id)
{
    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets[i].id == id && !m_presets[i].isBuiltIn) {
            QString name = m_presets[i].name;
            m_presets.removeAt(i);

            saveCustomPresets();

            if (m_currentPresetId == id && !m_presets.isEmpty()) {
                applyPreset(m_presets.first().id);
            }

            emit presetsChanged();
            return;
        }
    }
}

void ThemeManager::updatePresetFavorite(const QUuid& id, bool isFavorite)
{
    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets[i].id == id) {
            m_presets[i].isFavorite = isFavorite;

            if (m_presets[i].isBuiltIn) {
                QSettings settings;
                settings.setValue(QString("Themes/Favorites/%1").arg(id.toString()), isFavorite);
                settings.sync();
            } else {
                saveCustomPresets();
            }

            emit presetsChanged();
            return;
        }
    }
}

// === Resource Access ===

ResourceManager& ThemeManager::resources()
{
    return ResourceManager::instance();
}

const ResourceManager& ThemeManager::resources() const
{
    return ResourceManager::instance();
}

IconProvider& ThemeManager::icons()
{
    return IconProvider::instance();
}

const IconProvider& ThemeManager::icons() const
{
    return IconProvider::instance();
}

FontManager& ThemeManager::fonts()
{
    return FontManager::instance();
}

const FontManager& ThemeManager::fonts() const
{
    return FontManager::instance();
}

// ==========================================================================
// Core styling: QPalette + scoped QSS
// ==========================================================================

void ThemeManager::applyPalette()
{
    // QPalette is Qt's native color propagation mechanism.
    // Setting it on QApplication instantly updates all widgets that don't
    // have an explicit stylesheet overriding these roles — no re-parsing needed.
    const QPalette pal = paletteForColors(m_colors, qApp->palette());

    QElapsedTimer profPal;
    profPal.start(); // [ThemeProfile]
    qApp->setPalette(pal);
}

QPalette ThemeManager::paletteForColors(const ThemeColors& colors, QPalette palette)
{
    palette.setColor(QPalette::Window, colors.background);
    palette.setColor(QPalette::WindowText, colors.text);
    palette.setColor(QPalette::Base, colors.surfaceAlt);
    palette.setColor(QPalette::AlternateBase, colors.surface);
    palette.setColor(QPalette::Text, colors.text);
    palette.setColor(QPalette::BrightText, colors._textOnPrimary);
    palette.setColor(QPalette::Button, colors.surface);
    palette.setColor(QPalette::ButtonText, colors.text);
    palette.setColor(QPalette::Highlight, colors.primary);
    palette.setColor(QPalette::HighlightedText, colors._textOnPrimary);
    palette.setColor(QPalette::ToolTipBase, colors.surface);
    palette.setColor(QPalette::ToolTipText, colors.text);
    palette.setColor(QPalette::PlaceholderText, colors.textMuted);
    palette.setColor(QPalette::Mid, colors.border);
    palette.setColor(QPalette::Dark, colors.border);
    palette.setColor(QPalette::Shadow, colors.border);

    palette.setColor(QPalette::Disabled, QPalette::WindowText, colors.textDisabled());
    palette.setColor(QPalette::Disabled, QPalette::Text, colors.textDisabled());
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, colors.textDisabled());

    // Repurposed roles used by the application's static palette-driven QSS.
    palette.setColor(QPalette::Link, colors.surfaceHover());
    palette.setColor(QPalette::LinkVisited, colors.primaryPressed());
    return palette;
}

void ThemeManager::generateStyleSheet()
{
    // PERF: this QSS is now STATIC and uses palette(...) references instead of
    // baked-in colours, so it is set on qApp exactly ONCE (at startup). A theme
    // change only calls applyPalette() (qApp->setPalette) which is ~free and does
    // NOT re-match the whole stylesheet against every widget — re-setting the app
    // stylesheet was O(all widgets) and cost seconds with many open projects.
    //
    // Palette role mapping (see applyPalette()):
    //   surface        -> palette(button)
    //   text           -> palette(button-text)
    //   border         -> palette(mid)
    //   surfaceHover   -> palette(link)            [repurposed, native-safe]
    //   primaryPressed -> palette(link-visited)    [repurposed, native-safe]
    //   textMuted      -> palette(placeholder-text)
    //   borderLight    -> palette(mid)             [folded onto border]
    //   primary        -> palette(highlight)
    //   surfaceAlt     -> palette(base)
    //   textDisabled   -> palette(button-text) resolved in the :disabled group
    m_styleSheet = QStringLiteral(R"(
        QMainWindow { background-color: palette(button); }
        QMenuBar {
            background-color: palette(button);
            color: palette(button-text);
            border-bottom: 1px solid palette(mid);
            padding: 2px;
        }
        QMenuBar::item:selected { background-color: palette(link); }
        QMenu {
            background-color: palette(button);
            color: palette(button-text);
            border: 1px solid palette(mid);
        }
        QMenu::item:selected { background-color: palette(link); }
        QMenu::separator { height: 1px; background-color: palette(mid); margin: 4px 0; }
        QStatusBar {
            background-color: palette(button);
            color: palette(placeholder-text);
            border-top: 1px solid palette(mid);
        }
        QToolButton:hover { background-color: palette(link); border-color: palette(mid); }
        QToolButton:checked { background-color: palette(highlight); border-color: palette(highlight); }
        QScrollBar:vertical {
            background-color: palette(button);
            width: 12px;
            border: none;
        }
        QScrollBar::handle:vertical {
            background-color: palette(mid);
            border-radius: 6px;
            margin: 2px;
            min-height: 20px;
        }
        QScrollBar::handle:vertical:hover { background-color: palette(mid); }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QScrollBar:horizontal {
            background-color: palette(button);
            height: 12px;
            border: none;
        }
        QScrollBar::handle:horizontal {
            background-color: palette(mid);
            border-radius: 6px;
            margin: 2px;
            min-width: 20px;
        }
        QScrollBar::handle:horizontal:hover { background-color: palette(mid); }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0px;
        }
        QLineEdit {
            background-color: palette(base);
            border: 1px solid palette(mid);
            border-radius: 4px;
            padding: 6px 8px;
            color: palette(button-text);
            selection-background-color: palette(highlight);
        }
        QLineEdit:focus {
            border-color: palette(highlight);
        }
        QPushButton {
            background-color: palette(base);
            border: 1px solid palette(mid);
            border-radius: 4px;
            padding: 6px 16px;
            color: palette(button-text);
        }
        QPushButton:hover {
            background-color: palette(link);
            border-color: palette(mid);
        }
        QPushButton:disabled {
            background-color: palette(base);
            color: palette(button-text);
        }
    )");
}

void ThemeManager::registerThemeHandler(QWidget* widget, std::function<void()> handler)
{
    if (!widget || !handler) {
        return;
    }
    m_themeHandlers.push_back({ QPointer<QWidget>(widget), std::move(handler), false });
}

void ThemeManager::dispatchThemeHandlers()
{
    // Decide + mark first, THEN run. A handler may rebuild a subtree and register
    // new widgets (push_back into m_themeHandlers), which would reallocate and
    // invalidate any iterator/reference held across the call.
    QVector<std::function<void()>> toRun;
    toRun.reserve(m_themeHandlers.size());
    int profDeferred = 0; // [ThemeProfile]
    for (int i = 0; i < m_themeHandlers.size();) {
        ThemeHandlerEntry& e = m_themeHandlers[i];
        if (!e.widget) {
            m_themeHandlers.removeAt(i); // widget destroyed — drop the entry
            continue;
        }
        if (e.widget->isVisible()) {
            e.dirty = false;
            toRun.push_back(e.handler);
        } else {
            e.dirty = true; // background tab — defer until flush
            ++profDeferred;
        }
        ++i;
    }
    QElapsedTimer t;
    t.start(); // [ThemeProfile]
    for (const std::function<void()>& fn : toRun) {
        fn();
    }
}

void ThemeManager::flushThemeHandlers(QWidget* root)
{
    if (!root) {
        return;
    }
    QVector<std::function<void()>> toRun;
    for (ThemeHandlerEntry& e : m_themeHandlers) {
        if (!e.widget || !e.dirty) {
            continue;
        }
        QWidget* w = e.widget.data();
        if (w == root || root->isAncestorOf(w)) {
            e.dirty = false;
            toRun.push_back(e.handler);
        }
    }
    QElapsedTimer t;
    t.start(); // [ThemeProfile]
    for (const std::function<void()>& fn : toRun) {
        fn();
    }
}

void ThemeManager::applyStyleSheet()
{
    qApp->setStyleSheet(m_styleSheet);
}

} // namespace ruwa::ui::core
