// SPDX-License-Identifier: MPL-2.0

#include "shell/update-message/ReleaseNotesOverlay.h"

#include "commands/ShortcutManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "shared/style/PaintingUtils.h"
#include "shared/widgets/CapsuleButton.h"
#include "shared/widgets/layout/SmoothScrollArea.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTextDocument>
#include <QVector>
#include <QVBoxLayout>
#include <QtMath>

namespace ruwa::ui::widgets {

namespace {

constexpr int CardWidth = 1100;
constexpr int CardHeight = 840;
constexpr int CardRadius = 12;
constexpr int CardPadding = 28;
constexpr int CardSpacing = 16;
constexpr int TitleFontSize = 20;
constexpr int EntryBadgeFontSize = 10;
constexpr int EntryBodyFontSize = 11;
constexpr int CloseButtonMinWidth = 110;
constexpr int EntrySpacing = 36;
constexpr int EntryBodySpacing = 8;
constexpr int EntryBadgeRadius = 8;
constexpr int EntryBadgePaddingH = 12;
constexpr int EntryBadgePaddingV = 4;

struct ReleaseNoteEntry {
    QString name;
    QString version;
    QString date;
    QString body;
};

QVector<ReleaseNoteEntry> releaseNoteEntries()
{
    const char* ctx = "ReleaseNotesOverlay";

    return {
        { QCoreApplication::translate(
              ctx, "Personalization, colour controls, and input fixes"),
            QStringLiteral("0.2.5-alpha"), QStringLiteral("18.07.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>This update introduces a redesigned first-run personalization flow and "
                "compact RGB/HSV controls, improves brush startup and custom dab rendering, and "
                "fixes WinTab input, transform safety, and several canvas interaction "
                "issues.</b></p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>A redesigned first-run flow for choosing appearance, editor, performance, "
                "and tablet-input settings.</li>"
                "<li>Compact RGB and HSV channel controls in the Color panel.</li>"
                "</ul>"
                "<p><b>Improved</b></p>"
                "<ul>"
                "<li>Nine additional default brush presets, improved startup selection, and "
                "expanded packs by default.</li>"
                "<li>Favorite brush parameters stored in imported and exported packs.</li>"
                "<li>Custom dab hardness and brush cursor previews.</li>"
                "<li>The Composer panel is now named Navigator.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>More reliable undo handling in the Brush Editor.</li>"
                "<li>Phantom and interrupted WinTab strokes, including mouse/pen handoff across "
                "the UI and canvas.</li>"
                "<li>Transform finalization and several issues involving selections, alpha lock, "
                "Blur, and Navigator refreshes.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Open-source release"), QStringLiteral("0.2.4-alpha"),
            QStringLiteral("16.07.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>Ruwa is now open source.</b> The source code and contribution process are "
                "public on a fresh repository, the release ships alongside a brand-new website, "
                "and the licensing, security, governance, CI, and release infrastructure needed "
                "for public development are complete.</p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>A public source repository at github.com/LuskusDeus/Ruwa and a new project "
                "website at accretion.pro.</li>"
                "</ul>"
                "<p><b>Changed</b></p>"
                "<ul>"
                "<li>The proprietary Discord Game SDK has been replaced with a first-party "
                "Discord Rich Presence implementation over local IPC using Qt only.</li>"
                "<li>The public contribution process now includes governance, security, code of "
                "conduct, DCO sign-off, issue templates, and pull request guidance.</li>"
                "<li>Dependency and asset provenance, licence notices, and CI checks are "
                "documented in the repository, and all outstanding licensing issues have been "
                "resolved.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>The binary installer release now packages and installs correctly.</li>"
                "<li>Fixed an event-handling bug in the Layers panel.</li>"
                "</ul>") },
        { QCoreApplication::translate(
              ctx, "Non-destructive effects, adjustment layers, and deeper colour"),
            QStringLiteral("0.2.3-alpha"), QStringLiteral("14.07.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>A major update. It introduces a non-destructive effects system with "
                "real-time previews, adds adjustment layers and pixel-perfect layer picking, and "
                "rebuilds the tile core so documents can use any colour depth from 8-bit to 32-bit "
                "float.</b> It also ships a completely new icon set, a custom pigment-mixing "
                "engine, a more accurate WinTab backend, and a long list of fixes.</p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>Non-destructive effects. Add any number of effects to a layer, reorder and "
                "edit them, and see the result on the canvas in real time. Effects can also be "
                "applied to groups, and the whole chain can be baked into the layer at any "
                "time.</li>"
                "<li>Adjustment layers for applying corrections across the layers beneath "
                "them.</li>"
                "<li>Pixel-perfect layer picking. The Move tool now identifies which layer owns "
                "the pixel under the cursor and moves exactly that layer.</li>"
                "<li>The rectangular selection tool now shows the size of the selection on a small "
                "badge next to the cursor.</li>"
                "</ul>"
                "<p><b>Reworked</b></p>"
                "<ul>"
                "<li>The tile system was rebuilt from the ground up. Documents can now use any "
                "colour depth from 8-bit up to 32-bit float.</li>"
                "<li>Groups now isolate blend modes correctly, so effects composite over them the "
                "way they should.</li>"
                "<li>A new, in-house pigment-mixing system replaces the previous one — it mixes "
                "better and carries no third-party licensing.</li>"
                "<li>The old layer-picking system based on content bounds was inaccurate and has "
                "been removed entirely in favour of pixel-perfect picking.</li>"
                "</ul>"
                "<p><b>Improved</b></p>"
                "<ul>"
                "<li>A brand-new icon set across the whole application, adopted to resolve "
                "licensing on the previous icons.</li>"
                "<li>Nearly every panel now has its own dedicated icon instead of a placeholder "
                "tool icon.</li>"
                "<li>The custom WinTab backend is more accurate and less buggy.</li>"
                "<li>Undo now covers layer rasterisation, so it can be undone like any other "
                "action.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>Board layers now flip correctly when the canvas is mirrored.</li>"
                "<li>Smudge no longer clips dabs or leaves white streaks on brushes that have "
                "jitter enabled.</li>"
                "<li>Fixed an undo bug where moving a masked layer region could roll the selection "
                "mask back several steps ahead of its contents.</li>"
                "<li>Fixed a visual glitch in the curve editor of the brush engine.</li>"
                "<li>Fixed odd cursor behaviour on a monitor positioned to the left of the primary "
                "display.</li>"
                "<li>Text layers no longer turn low-poly after a warp or free transform.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Liquify, layer masks, and a canvas redesign"),
            QStringLiteral("0.2.0-alpha"), QStringLiteral("14.06.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>A major update. It introduces the Liquify tool and layer masks, gives every "
                "canvas widget a new frosted-glass look backed by a much more reliable layout "
                "system, and completely reworks the wet brush mechanics.</b> It also refreshes the "
                "top bar popups and the color picker, improves floating panel and Canvas Resize "
                "performance, and fixes a large number of UI and visual bugs.</p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>Liquify. A new tool for warping the canvas, with Push, Rotate CW/CCW, Bloat, "
                "and Pucker modes.</li>"
                "<li>Layer masks. You can now add and edit layer masks, transform them correctly, "
                "and invert a mask from the context menu.</li>"
                "<li>The brush editor now has many more parameters for tuning wet brushes.</li>"
                "</ul>"
                "<p><b>Reworked</b></p>"
                "<ul>"
                "<li>The wet brush mechanics have been completely reworked.</li>"
                "<li>Canvas widgets now use a new layout system and serialization that work far "
                "more reliably.</li>"
                "<li>Every canvas widget was redesigned with a frosted-glass background. The Brush "
                "Control widget is now more compact, and the tool bar has a new capsule look.</li>"
                "</ul>"
                "<p><b>Redesigned</b></p>"
                "<ul>"
                "<li>New design for the top bar popups (the File, Edit, View, and Help menus, "
                "Layouts, and the tab context menu).</li>"
                "<li>New design for the Color Picker popup.</li>"
                "<li>The brush settings context menu on the canvas is now cleaner and easier to "
                "use.</li>"
                "</ul>"
                "<p><b>Improved</b></p>"
                "<ul>"
                "<li>Floating panel performance has been improved.</li>"
                "<li>Canvas Resize performance has been improved.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>Fixed a large number of UI bugs.</li>"
                "<li>Fixed bugs with the custom brush cursor on the canvas and with floating "
                "panels.</li>"
                "<li>A large number of additional visual fixes across the application.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Default brush and brush editor fixes"),
            QStringLiteral("0.1.75-alpha"), QStringLiteral("09.06.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>This update fixes a number of bugs with the default brush presets and the "
                "brush editor.</b></p>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>Fixed bugs with the default brush presets.</li>"
                "<li>Fixed bugs in the brush editor.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Wet brushes and reworked input"),
            QStringLiteral("0.1.7-alpha"), QStringLiteral("07.06.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>This update adds wet brushes, completely reworks the input system so "
                "drawing feels far better, completes the Deform transform, and greatly expands the "
                "set of default brushes.</b> Theme switching is now much faster, the stabilizer no "
                "longer produces broken lines, and a large number of visual and performance bugs "
                "have been fixed.</p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>Wet brushes. The brush engine now supports wet, color-mixing brushes.</li>"
                "</ul>"
                "<p><b>Reworked</b></p>"
                "<ul>"
                "<li>The input system has been completely reworked. Drawing now feels "
                "significantly better.</li>"
                "<li>The Deform transform is now complete. It was introduced as an early version "
                "in the previous update.</li>"
                "<li>Reworked the default brushes, with many more now included.</li>"
                "</ul>"
                "<p><b>Improved</b></p>"
                "<ul>"
                "<li>Theme switching is now much faster.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>Fixed a stabilizer bug that caused broken, jagged lines.</li>"
                "<li>Fixed the custom Ruwa WinTab backend.</li>"
                "<li>A large number of additional visual and performance fixes across the "
                "application.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Smudge, deform, and shortcut presets"),
            QStringLiteral("0.1.6-alpha"), QStringLiteral("28.05.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>This update reworks the Smudge tool, introduces an early Deform transform, "
                "and adds shortcut presets.</b> The brush cursor now follows the shape of the "
                "current dab, the Color panel and Keyboard Shortcuts tab have been redesigned, and "
                "a large number of bugs have been fixed.</p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>New Deform transform mode. It is still in progress and will be expanded in "
                "future updates.</li>"
                "<li>Shortcuts can now be saved and switched between as presets.</li>"
                "<li>The brush cursor now matches the shape of the current dab.</li>"
                "</ul>"
                "<p><b>Reworked</b></p>"
                "<ul>"
                "<li>The Smudge tool has been reworked.</li>"
                "<li>Redesigned Keyboard Shortcuts tab.</li>"
                "<li>New design for the Color panel.</li>"
                "<li>Redesigned Layout Presets popup.</li>"
                "<li>Small design refinements across the canvas widgets.</li>"
                "</ul>"
                "<p><b>Improved</b></p>"
                "<ul>"
                "<li>The ABR parser has been improved.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>Fixed visual bugs in Lasso Fill and the transform preview.</li>"
                "<li>A large number of additional bug fixes across the application.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Color, multi-layer transform, and alt-copy"),
            QStringLiteral("0.1.5-alpha"), QStringLiteral("14.05.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>This update expands brush color control, makes multi-layer work much more "
                "complete, and adds alt-copy for faster layer duplication.</b> It also fully "
                "reworks the brush stabilizer and includes bug fixes, an early ABR parser, a zoom "
                "overlay, and a redesigned installer.</p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>New Color tab in the brush engine with full HSL control and blend modes for "
                "brushes.</li>"
                "<li>Alt-copy now works: hold Alt while moving layers on the canvas or in the "
                "Layers panel to duplicate the selected layers.</li>"
                "<li>Added an early ABR parser. For now, it only parses dab textures.</li>"
                "<li>Added an overlay that shows the current zoom.</li>"
                "<li>The app now has a completely redesigned installer.</li>"
                "</ul>"
                "<p><b>Improved</b></p>"
                "<ul>"
                "<li>The brush stabilizer has been completely reworked.</li>"
                "<li>Full multi-layer support: selected layers can now be transformed together, "
                "including whole groups with subgroups.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>Fixed bugs across the application.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Text tool, Auto Snap, and brush sharing"),
            QStringLiteral("0.1.4-alpha"), QStringLiteral("03.05.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>This update introduces a fully featured Text tool, Auto Snap for precise "
                "canvas placement, and brush sharing through the brush editor.</b> Brush dynamics "
                "have been reworked with new input sources, and the About section has been "
                "redesigned.</p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>New Text tool with full text formatting support.</li>"
                "<li>Auto Snap — a powerful axis snapping system that makes placing elements on "
                "the canvas much easier.</li>"
                "<li>Brushes can now be exported and imported directly from the brush editor.</li>"
                "<li>New About section with information about the program, its testers, and the "
                "tools used to build it.</li>"
                "<li>New input source: Random (replaces the Jitter section, which has been removed "
                "entirely).</li>"
                "<li>New input source: Stroke Direction.</li>"
                "</ul>"
                "<p><b>Improved</b></p>"
                "<ul>"
                "<li>Reworked brush stabilization in the brush editor.</li>"
                "<li>Significant optimization improvements across several scenarios.</li>"
                "</ul>"
                "<p><b>Removed</b></p>"
                "<ul>"
                "<li>The Jitter section in the brush editor has been removed and replaced by the "
                "new Random input source.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>Fixed a large number of bugs across the application.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Layers, dabs, and smoother canvas"),
            QStringLiteral("0.1.3-alpha"), QStringLiteral("25.04.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>This update improves the layer workflow, adds custom image dabs for "
                "brushes, and makes canvas interaction smoother.</b> It also introduces the in-app "
                "release notes panel and refreshes the first-launch experience for new users.</p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>Added an in-app release notes panel, available for the first time in this "
                "version.</li>"
                "<li>Added a new update banner.</li>"
                "<li>Added a new first-run integration for new users.</li>"
                "<li>Added new default start banners.</li>"
                "<li>Brushes can now use imported images as custom dabs.</li>"
                "<li>Layers can now be marked with custom colors for visual organization.</li>"
                "<li>Added Merge Down for layers.</li>"
                "</ul>"
                "<p><b>Improved</b></p>"
                "<ul>"
                "<li>Reworked the Layers panel so its main actions are now grouped in the top "
                "area.</li>"
                "<li>Reworked brush stabilization.</li>"
                "<li>Canvas movement now feels smoother.</li>"
                "<li>Canvas Resize performance has been significantly improved.</li>"
                "</ul>"
                "<p><b>Fixes and small changes</b></p>"
                "<ul>"
                "<li>Stabilization settings are now saved for lasso-like tools.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Brush engine foundations"),
            QStringLiteral("0.1.2-alpha"), QStringLiteral("06.04.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>This update expands brush control and adds faster access to brush "
                "selection.</b> Brush parameters can now use curves, while the new panel and About "
                "section add more structure around the drawing workspace.</p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>New brush engine.</li>"
                "<li>Added a Brushes panel for quicker access to brushes.</li>"
                "<li>Added an About section with app information. The tester list is not complete "
                "yet and will be expanded later.</li>"
                "</ul>"
                "<p><b>Expanded</b></p>"
                "<ul>"
                "<li>Curves in the brush editor. Most parameters can now react to pen pressure or "
                "stroke age over time.</li>"
                "<li>More input sources for brush dynamics will be added later.</li>"
                "</ul>"
                "<p><b>Fixes and optimization</b></p>"
                "<ul>"
                "<li>Fixed a large number of bugs and optimization issues.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Brush workspace refresh"),
            QStringLiteral("0.1.1-alpha"), QStringLiteral("02.04.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>This update refreshes the brush panels and brush editor, and reworks Lasso "
                "Fill for infinite areas.</b> It also includes a round of bug fixes across the "
                "application.</p>"
                "<p><b>Redesigned</b></p>"
                "<ul>"
                "<li>New design for the brush panels and the brush editor.</li>"
                "</ul>"
                "<p><b>Reworked</b></p>"
                "<ul>"
                "<li>Reworked Lasso Fill. It is now optimized for comfortable use on infinite "
                "areas.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>Fixed many bugs across the application.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Infinite canvas preview"),
            QStringLiteral("0.1.0-alpha"), QStringLiteral("27.03.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>This update brings a cleaner UI style, infinite canvas support, and new "
                "canvas actions.</b> It also expands customization, context menus, transforms, "
                "brush options, and project setup.</p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>New UI design with a cleaner visual style.</li>"
                "<li>Infinite canvas.</li>"
                "<li>Canvas mirroring.</li>"
                "<li>New transform mode: Warp.</li>"
                "<li>New blur brush.</li>"
                "<li>New quick-actions canvas overlay.</li>"
                "<li>New Board layer type.</li>"
                "<li>Startup setup on first launch.</li>"
                "<li>Layout presets.</li>"
                "</ul>"
                "<p><b>Expanded</b></p>"
                "<ul>"
                "<li>More customization options.</li>"
                "<li>More context menus, including quick-action menus for the layer panel.</li>"
                "<li>Axis-constrained movement for layer content with Shift.</li>"
                "</ul>"
                "<p><b>Reworked</b></p>"
                "<ul>"
                "<li>Reworked lasso tool with much better behavior.</li>"
                "<li>Project file format changed from .uwa to .rwf.</li>"
                "</ul>"
                "<p><b>Improved</b></p>"
                "<ul>"
                "<li>More work on the Compositor panel.</li>"
                "<li>Improved Russian localization.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>Various bug fixes.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Export and tablet polish"),
            QStringLiteral("0.0.9-alpha"), QStringLiteral("19.03.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>This update adds image export and improves several tablet, mask, color, and "
                "workspace systems.</b> It also softens workspace visuals and includes small "
                "fixes.</p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>Image export now works. You can export to PNG, JPEG, and WebP.</li>"
                "<li>Selected areas can now be flipped horizontally or vertically.</li>"
                "<li>The color wheel panel now has new modes: hue ring, square, and triangle.</li>"
                "</ul>"
                "<p><b>Reworked</b></p>"
                "<ul>"
                "<li>Part of the input pipeline has been rewritten. Edge artifacts in masks should "
                "now either disappear or become much less noticeable. If you still see them, try "
                "switching the tablet backend in Settings - some backends are still unstable.</li>"
                "</ul>"
                "<p><b>Improved</b></p>"
                "<ul>"
                "<li>The custom Ruwa WinTab backend has been significantly improved.</li>"
                "<li>Workspace serialization for saving and loading has been improved.</li>"
                "<li>The workspace now looks cleaner visually: panel outlines are softer and no "
                "longer clip on soft edges.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>Small fixes.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Tools and tablet backend"),
            QStringLiteral("0.0.8-alpha"), QStringLiteral("13.03.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>This update improves the layer panel, adds a custom Windows WinTab backend, "
                "and reorganizes tools into folders.</b> Flood Fill performance and transform "
                "snapping were also improved.</p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>Added a custom Windows WinTab backend for tablets, improving compatibility "
                "with supported devices.</li>"
                "<li>Tools are now grouped into folders.</li>"
                "<li>In transform mode, holding Shift now snaps the rotation angle in 15-degree "
                "steps.</li>"
                "</ul>"
                "<p><b>Reworked</b></p>"
                "<ul>"
                "<li>Reworked the startup panel appearance animation, which used to be bugged.</li>"
                "</ul>"
                "<p><b>Improved</b></p>"
                "<ul>"
                "<li>Improved layer panel animations.</li>"
                "<li>Flood Fill performance has been significantly improved, with up to 10x faster "
                "processing.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>Fixed visual bugs in the layer panel.</li>"
                "<li>Minor bug fixes.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Async app flow"), QStringLiteral("0.0.7-alpha"),
            QStringLiteral("10.03.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>This update moves a significant part of the app to asynchronous behavior "
                "and rewrites the update manager.</b> It also addresses Flood Fill issues and "
                "improves pressure detection.</p>"
                "<p><b>Reworked</b></p>"
                "<ul>"
                "<li>A significant part of the app is now asynchronous, which makes the program "
                "feel smoother and reduces UI freezes.</li>"
                "<li>The update manager has been fully rewritten, so update-related errors should "
                "now be gone. It may ask for admin permissions.</li>"
                "</ul>"
                "<p><b>Improved</b></p>"
                "<ul>"
                "<li>Pressure detection has been significantly improved.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>Fixed critical issues with Flood Fill. Its calculations were moved to the "
                "CPU, which made it slower and introduced a maximum radius limit. This will be "
                "improved in future updates.</li>"
                "<li>Minor bug fixes.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Fill and quick shapes"), QStringLiteral("0.0.6-alpha"),
            QStringLiteral("09.03.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>This update reworks Flood Fill and Quick Shapes, and adds Smart Fill for "
                "semi-transparent pixels.</b> It also includes minor bug fixes.</p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>Added Smart Fill. This is a separate tool designed for working with "
                "semi-transparent pixels, especially with soft brushes. It is quite demanding.</li>"
                "<li>New shapes are now available: square and triangle variants for all "
                "directions.</li>"
                "</ul>"
                "<p><b>Reworked</b></p>"
                "<ul>"
                "<li>Flood Fill has been reworked. It is not fully finished yet and may still have "
                "bugs, but it should no longer crash the app and should work at a basic usable "
                "level.</li>"
                "<li>Quick Shapes has also been reworked.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>Fixed minor bugs.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Layout reliability pass"),
            QStringLiteral("0.0.51-alpha"), QStringLiteral("07.03.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>This update focuses on layout reliability, stylus interaction, and brush "
                "stabilization.</b> It also adds Brush Feather and stylus swipe scrolling for "
                "lists.</p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>Added the Brush Feather parameter for brushes, which controls edge "
                "smoothing.</li>"
                "<li>Lists can now be scrolled with stylus swipes.</li>"
                "</ul>"
                "<p><b>Improved</b></p>"
                "<ul>"
                "<li>Layout saving and loading are now more reliable.</li>"
                "<li>Significantly improved UI interaction with the stylus.</li>"
                "<li>Significantly improved brush stabilization.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>Fixed many layout-related bugs, including panel positioning, docking, and "
                "general behavior. If you already have layout issues, this update may not fix "
                "existing ones - it mainly prevents them from happening again. A complete "
                "reinstall is recommended.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Layer workflow update"), QStringLiteral("0.0.5-alpha"),
            QStringLiteral("06.03.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>The biggest update so far.</b> This update expands layer operations, "
                "selection-mask undo, canvas navigation, and layout serialization.</p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>Added layer gestures: swipe on the right side of a layer for the actions "
                "menu, and on the left side to toggle the clipping mask.</li>"
                "<li>Added layer locking and alpha-channel locking.</li>"
                "<li>Added a movable camera joystick.</li>"
                "<li>Added a new Lasso Fill tool.</li>"
                "</ul>"
                "<p><b>Expanded</b></p>"
                "<ul>"
                "<li>Undo now works for both layers and selection masks.</li>"
                "<li>Improved workflow for handling multiple layers in the layer panel.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>Fixed layout serialization for the canvas and floating panels.</li>"
                "<li>Various small fixes.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Lightweight updates"), QStringLiteral("0.0.4-alpha"),
            QStringLiteral("01.03.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>This update adds direct website downloads and lightweight app updates "
                "through Settings.</b> It also includes a rename, small quality-of-life changes, "
                "and bug fixes.</p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>The latest version of the app can now be downloaded directly from the "
                "website.</li>"
                "<li>Added the ability to update the app without downloading a new installer every "
                "time. To get a new version, open Settings and install the available update "
                "there.</li>"
                "</ul>"
                "<p><b>Changed</b></p>"
                "<ul>"
                "<li>Small rename: chanterelle -> accretion.</li>"
                "<li>Updates are lightweight, around 5-10 MB, so they download quickly.</li>"
                "</ul>"
                "<p><b>Quality of life</b></p>"
                "<ul>"
                "<li>Added a few small quality-of-life features.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>Fixed bugs.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Camera tools"), QStringLiteral("0.0.3-alpha"),
            QStringLiteral("26.02.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>This update adds camera capture tools and expands brush settings.</b> It "
                "also refreshes the default brush list and fixes small issues.</p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>Added a camera tool that instantly copies the image to the clipboard when "
                "clicked.</li>"
                "<li>Added an overlay for easier camera control.</li>"
                "<li>Added a new setting to the brush engine: Dab.</li>"
                "</ul>"
                "<p><b>Changed</b></p>"
                "<ul>"
                "<li>The list of default brushes has been updated. If you already installed the "
                "app and want to see the new default brushes, press Reset at the very bottom of "
                "Settings.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>Fixed small bugs and issues.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Stability and functionality"),
            QStringLiteral("0.0.2-alpha"), QStringLiteral("24.02.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>This update fixes bugs that were seriously affecting the app and adds new "
                "functionality.</b></p>"
                "<p><b>New</b></p>"
                "<ul>"
                "<li>Added new functionality.</li>"
                "</ul>"
                "<p><b>Fixes</b></p>"
                "<ul>"
                "<li>Fixed bugs that were seriously affecting the app.</li>"
                "</ul>") },
        { QCoreApplication::translate(ctx, "Public testing"), QStringLiteral("0.0.1-alpha"),
            QStringLiteral("24.02.2026"),
            QCoreApplication::translate(ctx,
                "<p><b>Ruwa is now available for anyone who wants to download it and help test "
                "it.</b></p>"
                "<p><b>Available</b></p>"
                "<ul>"
                "<li>The app is available to download and test.</li>"
                "</ul>"
                "<p><b>Testing feedback</b></p>"
                "<ul>"
                "<li>If you find any bugs, please report them in the appropriate channels.</li>"
                "<li>Feedback, suggestions, and ideas are welcome.</li>"
                "</ul>") }
    };
}

QString bodyDocumentHtml(const QString& body, const QColor& textColor)
{
    return QStringLiteral("<html><head><style>"
                          "body { margin: 0; color: %1; }"
                          "p { margin: 0 0 8px 0; }"
                          "ul { margin: 0 0 12px 20px; padding: 0; }"
                          "li { margin: 0 0 4px 0; }"
                          "</style></head><body>%2</body></html>")
        .arg(textColor.name(), body);
}

class ReleaseNotesCard : public QWidget {
public:
    explicit ReleaseNotesCard(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAutoFillBackground(false);
        setAttribute(Qt::WA_TranslucentBackground);
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);

        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()), theme.scaled(CardRadius), theme.scaled(CardRadius));
        setMask(QRegion(path.toFillPolygon().toPolygon()));
    }

    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const auto& colors = theme.colors();
        const int radius = theme.scaled(CardRadius);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.surface);
        painter.drawRoundedRect(rect(), radius, radius);

        ruwa::ui::painting::drawGradientBorder(painter, rect(), radius, colors.borderSubtleHover(),
            ruwa::ui::core::ThemeColors::withAlpha(
                colors.borderSubtle(), colors.borderSubtle().alpha() / 2));
    }
};

class ReleaseNotesContentWidget : public QWidget {
public:
    explicit ReleaseNotesContentWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAutoFillBackground(false);
        setAttribute(Qt::WA_TranslucentBackground);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }

    ~ReleaseNotesContentWidget() override { qDeleteAll(m_bodyDocuments); }

    void setEntries(const QVector<ReleaseNoteEntry>& entries, const QFont& bodyFont,
        const QFont& badgeFont, const QColor& bodyColor, int entrySpacing, int bodySpacing,
        int badgeRadius, int badgePaddingH, int badgePaddingV)
    {
        qDeleteAll(m_bodyDocuments);
        m_bodyDocuments.clear();

        m_entries = entries;
        m_bodyFont = bodyFont;
        m_badgeFont = badgeFont;
        m_bodyColor = bodyColor;
        m_entrySpacing = entrySpacing;
        m_bodySpacing = bodySpacing;
        m_badgeRadius = badgeRadius;
        m_badgePaddingH = badgePaddingH;
        m_badgePaddingV = badgePaddingV;

        for (const ReleaseNoteEntry& entry : m_entries) {
            auto* document = new QTextDocument(this);
            document->setDocumentMargin(0.0);
            document->setDefaultFont(m_bodyFont);
            document->setHtml(bodyDocumentHtml(entry.body, m_bodyColor));
            m_bodyDocuments.append(document);
        }

        invalidateLayout();
        updateGeometry();
        update();
    }

    void clear()
    {
        m_entries.clear();
        qDeleteAll(m_bodyDocuments);
        m_bodyDocuments.clear();
        invalidateLayout();
        updateGeometry();
        update();
    }

    QSize sizeHint() const override { return QSize(width() > 0 ? width() : 1, 0); }

    bool hasHeightForWidth() const override { return true; }

    int heightForWidth(int width) const override
    {
        if (width <= 0) {
            return 0;
        }

        ensureLayout(width);
        return m_cachedHeight + contentsMargins().top() + contentsMargins().bottom();
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        ensureLayout(event->size().width());
        update();
    }

    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        ensureLayout(width());
        const QMargins margins = contentsMargins();
        painter.translate(margins.left(), margins.top());

        const QFont previousFont = painter.font();
        const QPen previousPen = painter.pen();

        painter.setRenderHint(QPainter::Antialiasing);
        painter.setFont(m_badgeFont);

        const QFontMetrics badgeMetrics(m_badgeFont);
        const int count = qMin(m_entries.size(), m_bodyDocuments.size());
        for (int i = 0; i < count; ++i) {
            const ReleaseNoteEntry& entry = m_entries.at(i);
            const EntryLayout& layout = m_layouts.at(i);
            const QString badgeText
                = QStringLiteral("%1 %2 %3 %2 %4")
                      .arg(entry.name, QString(QChar(0x2022)), entry.version, entry.date);

            const QRect badgeRect(0, layout.top, layout.badgeWidth, layout.badgeHeight);
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::white);
            painter.drawRoundedRect(badgeRect, m_badgeRadius, m_badgeRadius);

            painter.setPen(Qt::black);
            painter.drawText(badgeRect.adjusted(m_badgePaddingH, 0, -m_badgePaddingH, 0),
                Qt::AlignVCenter | Qt::AlignLeft,
                badgeMetrics.elidedText(
                    badgeText, Qt::ElideRight, qMax(0, badgeRect.width() - (m_badgePaddingH * 2))));

            painter.save();
            painter.translate(0, layout.bodyTop);
            m_bodyDocuments.at(i)->drawContents(
                &painter, QRectF(QPointF(0.0, 0.0), QSizeF(layout.bodyWidth, layout.bodyHeight)));
            painter.restore();
        }

        painter.setFont(previousFont);
        painter.setPen(previousPen);
    }

private:
    struct EntryLayout {
        int top { 0 };
        int badgeWidth { 0 };
        int badgeHeight { 0 };
        int bodyTop { 0 };
        int bodyWidth { 0 };
        int bodyHeight { 0 };
    };

    void invalidateLayout()
    {
        m_cachedWidth = -1;
        m_cachedHeight = 0;
        m_layouts.clear();
    }

    void ensureLayout(int width) const
    {
        const QMargins margins = contentsMargins();
        const int contentWidth = qMax(0, width - margins.left() - margins.right());
        if (contentWidth <= 0 || contentWidth == m_cachedWidth) {
            return;
        }

        m_cachedWidth = contentWidth;
        m_cachedHeight = 0;
        m_layouts.clear();

        const QFontMetrics badgeMetrics(m_badgeFont);
        int y = 0;

        const int count = qMin(m_entries.size(), m_bodyDocuments.size());
        for (int i = 0; i < count; ++i) {
            const ReleaseNoteEntry& entry = m_entries.at(i);
            QTextDocument* document = m_bodyDocuments.at(i);
            document->setTextWidth(contentWidth);

            const QString badgeText
                = QStringLiteral("%1 %2 %3 %2 %4")
                      .arg(entry.name, QString(QChar(0x2022)), entry.version, entry.date);
            const int badgeWidth = qMin(
                contentWidth, badgeMetrics.horizontalAdvance(badgeText) + (m_badgePaddingH * 2));
            const int badgeHeight = badgeMetrics.height() + (m_badgePaddingV * 2);
            const int bodyTop = y + badgeHeight + m_bodySpacing;
            const int bodyHeight = qCeil(document->size().height());

            m_layouts.append(
                EntryLayout { y, badgeWidth, badgeHeight, bodyTop, contentWidth, bodyHeight });
            y = bodyTop + bodyHeight + m_entrySpacing;
        }

        if (!m_layouts.isEmpty()) {
            y -= m_entrySpacing;
        }
        m_cachedHeight = qMax(0, y);
    }

private:
    QVector<ReleaseNoteEntry> m_entries;
    QVector<QTextDocument*> m_bodyDocuments;
    QFont m_bodyFont;
    QFont m_badgeFont;
    QColor m_bodyColor;
    int m_entrySpacing { 0 };
    int m_bodySpacing { 0 };
    int m_badgeRadius { 0 };
    int m_badgePaddingH { 0 };
    int m_badgePaddingV { 0 };
    mutable QVector<EntryLayout> m_layouts;
    mutable int m_cachedWidth { -1 };
    mutable int m_cachedHeight { 0 };
};

} // namespace

ReleaseNotesOverlay::ReleaseNotesOverlay(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    setupAnimations();

    QWidget::hide();
    m_dimProgress = 0.0;
}

ReleaseNotesOverlay::~ReleaseNotesOverlay()
{
    if (m_shortcutsBlocked) {
        m_shortcutsBlocked = false;
        ruwa::core::ShortcutManager::instance().popShortcutsDisabled();
    }
}

void ReleaseNotesOverlay::setupUi()
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    m_card = new ReleaseNotesCard(this);
    m_card->setFixedSize(theme.scaled(CardWidth), theme.scaled(CardHeight));

    m_cardOpacityEffect = new QGraphicsOpacityEffect(m_card);
    m_cardOpacityEffect->setOpacity(0.0);
    m_card->setGraphicsEffect(m_cardOpacityEffect);

    auto* layout = new QVBoxLayout(m_card);
    layout->setContentsMargins(theme.scaled(CardPadding), theme.scaled(CardPadding),
        theme.scaled(CardPadding), theme.scaled(CardPadding));
    layout->setSpacing(theme.scaled(CardSpacing));

    m_titleLabel = new QLabel(m_card);
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setStyleSheet(
        QStringLiteral("QLabel { background: transparent; color: %1; }").arg(colors.text.name()));
    m_titleLabel->setFont(colors.fonts.getTitleFont(theme.scaledFontSize(TitleFontSize)));
    layout->addWidget(m_titleLabel);

    m_scrollArea = new SmoothScrollArea(m_card);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setFillBackground(false);
    m_scrollArea->setScrollBarTransparentTrack(true);
    m_scrollArea->setAttribute(Qt::WA_TranslucentBackground);
    m_scrollArea->setAutoFillBackground(false);
    QPalette scrollPalette = m_scrollArea->palette();
    scrollPalette.setColor(QPalette::Window, Qt::transparent);
    scrollPalette.setColor(QPalette::Base, Qt::transparent);
    m_scrollArea->setPalette(scrollPalette);
    m_scrollArea->viewport()->setAutoFillBackground(false);
    m_scrollArea->viewport()->setAttribute(Qt::WA_TranslucentBackground);
    m_scrollArea->viewport()->setPalette(scrollPalette);

    m_scrollContent = new ReleaseNotesContentWidget(m_card);
    m_scrollContent->setAutoFillBackground(false);
    m_scrollContent->setAttribute(Qt::WA_TranslucentBackground);
    m_scrollContent->setPalette(scrollPalette);
    m_scrollArea->setWidget(m_scrollContent);
    layout->addWidget(m_scrollArea, 1);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch();

    m_closeButton = new CapsuleButton(QString(), CapsuleButton::Variant::Primary, m_card);
    m_closeButton->setBaseMinimumWidth(CloseButtonMinWidth);
    m_closeButton->setBannerBaseHeight(36);
    m_closeButton->setSizeScale(0.82);
    connect(m_closeButton, &QAbstractButton::clicked, this, &ReleaseNotesOverlay::onCloseRequested);
    buttonRow->addWidget(m_closeButton);
    layout->addLayout(buttonRow);

    updateTexts();

    if (parentWidget()) {
        parentWidget()->installEventFilter(this);
        resize(parentWidget()->size());
    }
}

void ReleaseNotesOverlay::setupAnimations()
{
    m_dimAnimation = new QPropertyAnimation(this, "dimProgress", this);
    m_dimAnimation->setDuration(DimAnimationDuration);

    m_cardOpacityAnim = new QPropertyAnimation(m_cardOpacityEffect, "opacity", this);
    m_cardOpacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_cardPosAnim = new QPropertyAnimation(m_card, "pos", this);
    m_cardPosAnim->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_dimAnimation, &QPropertyAnimation::finished, this,
        &ReleaseNotesOverlay::onDimAnimationFinished);
}

void ReleaseNotesOverlay::updateTexts()
{
    const char* ctx = "ReleaseNotesOverlay";

    if (m_titleLabel) {
        m_titleLabel->setText(QCoreApplication::translate(ctx, "Release notes"));
    }
    if (m_closeButton) {
        m_closeButton->setText(QCoreApplication::translate(ctx, "Close"));
        m_closeButton->syncSizeToText();
    }

    if (m_entriesBuilt) {
        rebuildEntries();
    }
}

void ReleaseNotesOverlay::rebuildEntries()
{
    if (!m_scrollContent) {
        return;
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    const QVector<ReleaseNoteEntry> entries = releaseNoteEntries();
    auto* contentWidget = static_cast<ReleaseNotesContentWidget*>(m_scrollContent);
    contentWidget->setContentsMargins(0, 0, theme.scaled(8), 0);
    contentWidget->setEntries(entries,
        colors.fonts.getUIFont(theme.scaledFontSize(EntryBodyFontSize)),
        colors.fonts.getUIFont(theme.scaledFontSize(EntryBadgeFontSize)), colors.textMuted,
        theme.scaled(EntrySpacing), theme.scaled(EntryBodySpacing), theme.scaled(EntryBadgeRadius),
        theme.scaled(EntryBadgePaddingH), theme.scaled(EntryBadgePaddingV));

    m_scrollArea->refreshScrollGeometry();
    m_scrollArea->scrollTo(0, false);
    m_entriesBuilt = true;
}

void ReleaseNotesOverlay::clearEntries()
{
    if (!m_scrollContent || !m_entriesBuilt) {
        return;
    }

    auto* contentWidget = static_cast<ReleaseNotesContentWidget*>(m_scrollContent);
    contentWidget->clear();

    m_entriesBuilt = false;
    if (m_scrollArea) {
        m_scrollArea->refreshScrollGeometry();
        m_scrollArea->scrollTo(0, false);
    }
}

void ReleaseNotesOverlay::showOverlay()
{
    if (m_isShowing || (isVisible() && !m_isHiding)) {
        return;
    }

    if (!m_entriesBuilt) {
        rebuildEntries();
    }

    m_isShowing = true;
    m_isHiding = false;

    if (parentWidget()) {
        resize(parentWidget()->size());
    }

    QWidget::show();
    raise();
    setFocus();

    if (!m_shortcutsBlocked) {
        ruwa::core::ShortcutManager::instance().pushShortcutsDisabled();
        m_shortcutsBlocked = true;
    }

    const QPoint targetPos = cardTargetPosition();
    const QPoint startPos = targetPos + QPoint(0, SlideOffset);
    m_card->move(startPos);
    m_cardOpacityEffect->setOpacity(0.0);
    m_dismissCooldownTimer.start();

    m_dimAnimation->stop();
    m_dimAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_dimAnimation->setStartValue(m_dimProgress);
    m_dimAnimation->setEndValue(1.0);
    m_dimAnimation->start();

    m_cardOpacityAnim->stop();
    disconnect(m_cardOpacityAnim, &QPropertyAnimation::finished, this, nullptr);
    m_cardOpacityAnim->setDuration(CardAnimationDuration);
    m_cardOpacityAnim->setStartValue(0.0);
    m_cardOpacityAnim->setEndValue(1.0);
    m_cardOpacityAnim->start();

    m_cardPosAnim->stop();
    disconnect(m_cardPosAnim, &QPropertyAnimation::finished, this,
        &ReleaseNotesOverlay::onCardHideAnimationFinished);
    m_cardPosAnim->setDuration(CardAnimationDuration);
    m_cardPosAnim->setStartValue(startPos);
    m_cardPosAnim->setEndValue(targetPos);
    m_cardPosAnim->start();
}

void ReleaseNotesOverlay::hideOverlay(bool bypassCooldown)
{
    if (m_isHiding || !isVisible()) {
        return;
    }
    if (!bypassCooldown && m_dismissCooldownTimer.elapsed() < DismissCooldownMs) {
        return;
    }

    m_isHiding = true;
    m_isShowing = false;

    disconnect(m_cardOpacityAnim, &QPropertyAnimation::finished, this, nullptr);
    connect(m_cardOpacityAnim, &QPropertyAnimation::finished, this,
        &ReleaseNotesOverlay::onCardHideAnimationFinished);

    m_dimAnimation->stop();
    m_dimAnimation->setEasingCurve(QEasingCurve::InCubic);
    m_dimAnimation->setStartValue(m_dimProgress);
    m_dimAnimation->setEndValue(0.0);
    m_dimAnimation->start();

    const QPoint currentPos = m_card->pos();
    const QPoint endPos = currentPos + QPoint(0, SlideOffset);

    m_cardOpacityAnim->stop();
    m_cardOpacityAnim->setDuration(CardAnimationDuration);
    m_cardOpacityAnim->setStartValue(m_cardOpacityEffect->opacity());
    m_cardOpacityAnim->setEndValue(0.0);
    m_cardOpacityAnim->start();

    m_cardPosAnim->stop();
    m_cardPosAnim->setDuration(CardAnimationDuration);
    m_cardPosAnim->setStartValue(currentPos);
    m_cardPosAnim->setEndValue(endPos);
    m_cardPosAnim->start();
}

bool ReleaseNotesOverlay::isActive() const
{
    return isVisible() && !m_isHiding;
}

void ReleaseNotesOverlay::setDimProgress(qreal progress)
{
    if (qFuzzyCompare(m_dimProgress, progress)) {
        return;
    }

    m_dimProgress = progress;
    update();
}

void ReleaseNotesOverlay::onCloseRequested()
{
    hideOverlay(true);
}

void ReleaseNotesOverlay::onDimAnimationFinished()
{
    if (m_isShowing) {
        m_isShowing = false;
        emit shown();
    }
}

void ReleaseNotesOverlay::onCardHideAnimationFinished()
{
    if (!m_isHiding) {
        return;
    }

    m_isHiding = false;
    if (m_shortcutsBlocked) {
        m_shortcutsBlocked = false;
        ruwa::core::ShortcutManager::instance().popShortcutsDisabled();
    }
    QWidget::hide();
    clearEntries();
    emit hidden();
}

QPoint ReleaseNotesOverlay::cardTargetPosition() const
{
    if (!m_card) {
        return {};
    }

    const int x = (width() - m_card->width()) / 2;
    const int y = (height() - m_card->height()) / 2;
    return QPoint(qMax(0, x), qMax(0, y));
}

void ReleaseNotesOverlay::updateCardPosition()
{
    if (m_card) {
        m_card->move(cardTargetPosition());
    }
}

void ReleaseNotesOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (m_dimProgress <= 0.001) {
        return;
    }

    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, int(MaxDimOpacity * 255 * m_dimProgress)));
}

void ReleaseNotesOverlay::mousePressEvent(QMouseEvent* event)
{
    if (m_card && !m_card->geometry().contains(event->pos())) {
        hideOverlay();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void ReleaseNotesOverlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateCardPosition();
}

void ReleaseNotesOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hideOverlay();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

bool ReleaseNotesOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        auto* resizeEvent = static_cast<QResizeEvent*>(event);
        resize(resizeEvent->size());
        updateCardPosition();
    }

    return QWidget::eventFilter(watched, event);
}

void ReleaseNotesOverlay::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        updateTexts();
    }
}

} // namespace ruwa::ui::widgets
