# Changelog

All notable changes to Ruwa are recorded here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
alpha versioning (`0.x.y-alpha`) until the first stable release.

The canonical application version is composed from `project(VERSION)` and
`RUWA_VERSION_SUFFIX` in [`CMakeLists.txt`](CMakeLists.txt). Runtime consumers
receive that value through the generated `RuwaBuildConfig.h`; do not add another
runtime version constant. Historical entries intentionally keep their own
version labels in
[`ReleaseNotesOverlay`](src/shell/update-message/ReleaseNotesOverlay.cpp). Keep
the CMake version, this file, and the newest overlay entry in sync when cutting
a release.

## [Unreleased]

## [0.2.5-alpha] — 2026-07-18 — "Personalization, colour controls, and input fixes"

This update introduces a redesigned first-run personalization flow and compact
RGB/HSV controls, improves brush startup and custom dab rendering, and fixes
WinTab input, transform safety, and several canvas interaction issues.

### Added
- A redesigned first-run flow for choosing appearance, editor, performance, and
  tablet-input settings.
- Compact RGB and HSV channel controls in the Color panel.

### Improved
- Brush is now the default startup tool, brush packs start expanded, and the
  first available brush is selected consistently.
- The default library now includes nine additional brush presets, with refreshed
  brush and dab assets.
- Favorite brush parameters now survive pack import and export.
- Custom dab hardness and brush cursor previews are smoother and more accurate.
- The Composer panel is now named Navigator.

### Fixed
- Fixed undo handling in the Brush Editor.
- Fixed phantom and interrupted strokes in the custom WinTab backend and
  improved mouse/pen pointer handoff across the UI and canvas.
- Active transforms are now safely committed before layer, selection, import,
  or canvas-geometry changes.
- Fixed alpha-lock handling for Lasso Fill, mirrored selection previews,
  Navigator refresh after effect removal, and fixed-soft-brush behavior for
  Blur.

## [0.2.4-alpha] — 2026-07-16 — "Open-source release"

Ruwa is now open source. This release opens the source code and contribution
process on a fresh public repository, ships alongside a brand-new project
website, removes the last proprietary runtime dependency, resolves all
outstanding third-party licensing issues, and completes the security,
governance, CI, and release infrastructure required for public development.

### Added
- Public source repository at <https://github.com/LuskusDeus/Ruwa> and a new
  project website at <https://www.accretion.pro/>.
- Open-source governance documents: `CONTRIBUTING.md` (with DCO sign-off),
  `CODE_OF_CONDUCT.md` (Contributor Covenant 2.1), `SECURITY.md`,
  `GOVERNANCE.md`, and GitHub issue/PR templates plus `CODEOWNERS`.
- Vendored the exact Apache-2.0 licence and provenance record for QWindowKit
  1.5.0.
- Documented the boundary between compatibility CI and the pinned release
  toolchain.
- Added an idempotent MPL-2.0 header manager and a required CI check for
  Ruwa-owned source, build, and supported configuration files.

### Changed
- Centralized the current application version in CMake-generated build metadata
  used by the application, effect host API, and release packaging instructions.
- Replaced the proprietary Discord Game SDK with a first-party Discord Rich
  Presence implementation over Discord's local IPC socket (Qt only, no external
  SDK or bundled binary). The feature now ships enabled by default in
  open-source builds; disable it with `RUWA_ENABLE_DISCORD=OFF`.

### Removed
- The Discord Game SDK dependency, its `third-party/discord` integration, and
  the bundled `discord_game_sdk.dll`.

### Fixed
- Resolved the outstanding issues in the binary installer release so
  distributable builds package and install correctly.
- Fixed an event-handling bug in the Layers panel.

## [0.2.3-alpha] — 2026-07-14 — "Non-destructive effects, adjustment layers, and deeper colour"

A major update. It introduces a non-destructive effects system with real-time
previews, adds adjustment layers and pixel-perfect layer picking, and rebuilds
the tile core so documents can use any colour depth from 8-bit to 32-bit float.

### Added
- **Non-destructive effects** — add any number of effects to a layer, reorder
  and edit them, and see the result on the canvas in real time. Effects can also
  be applied to groups, and the whole chain can be baked into the layer at any
  time.
- **Adjustment layers** for applying corrections across the layers beneath them.
- **Pixel-perfect layer picking** — the Move tool now identifies which layer
  owns the pixel under the cursor and moves exactly that layer.
- The rectangular selection tool now shows the size of the selection on a small
  badge next to the cursor.

### Reworked
- The tile system was rebuilt from the ground up; documents can now use any
  colour depth from 8-bit up to 32-bit float.
- Groups now isolate blend modes correctly, so effects composite over them the
  way they should.
- A new, in-house pigment-mixing system replaces the previous one — it mixes
  better and carries no third-party licensing.
- The old layer-picking system based on content bounds was inaccurate and has
  been removed entirely in favour of pixel-perfect picking.

### Improved
- A brand-new icon set across the whole application, adopted to resolve
  licensing on the previous icons.
- Nearly every panel now has its own dedicated icon instead of a placeholder
  tool icon.
- The custom WinTab backend is more accurate and less buggy.
- Undo now covers layer rasterisation, so it can be undone like any other
  action.

### Fixed
- Board layers now flip correctly when the canvas is mirrored.
- Smudge no longer clips dabs or leaves white streaks on brushes that have
  jitter enabled.
- Fixed an undo bug where moving a masked layer region could roll the selection
  mask back several steps ahead of its contents.
- Fixed a visual glitch in the curve editor of the brush engine.
- Fixed odd cursor behaviour on a monitor positioned to the left of the primary
  display.
- Text layers no longer turn low-poly after a warp or free transform.

## [0.2.0-alpha] — 2026-06-14 — "Liquify, layer masks, and a canvas redesign"

A major update. It introduces the Liquify tool and layer masks, gives every
canvas widget a new frosted-glass look backed by a much more reliable layout
system, and completely reworks the wet brush mechanics.

### Added
- **Liquify** tool for warping the canvas, with Push, Rotate CW/CCW, Bloat, and
  Pucker modes.
- **Layer masks** — add and edit layer masks, transform them correctly, and
  invert a mask from the context menu.
- Many more brush-editor parameters for tuning wet brushes.

### Reworked
- Wet brush mechanics completely reworked.
- Canvas widgets now use a new layout system and serialization that are far more
  reliable.
- Every canvas widget redesigned with a frosted-glass background; the Brush
  Control widget is more compact and the tool bar has a new capsule look.

### Redesigned
- Top-bar popups (File, Edit, View, Help menus, Layouts, and the tab context
  menu).
- The Color Picker popup.
- Cleaner brush-settings context menu on the canvas.

### Improved
- Floating panel performance.
- Canvas Resize performance.

### Fixed
- A large number of UI bugs.
- Custom brush cursor on the canvas and floating-panel bugs.
- Many additional visual fixes across the application.

## [0.1.75-alpha] — 2026-06-09 — "Default brush and brush editor fixes"

### Fixed
- Bugs with the default brush presets.
- Bugs in the brush editor.

## [0.1.7-alpha] — 2026-06-07 — "Wet brushes and reworked input"

### Added
- Wet, color-mixing brushes in the brush engine.

### Reworked
- Complete rework of the input system — drawing feels significantly better.
- The Deform transform is now complete.
- Reworked and expanded the set of default brushes.

### Improved
- Much faster theme switching.

### Fixed
- Stabilizer bug that caused broken, jagged lines.
- The custom Ruwa WinTab backend.
- Many additional visual and performance fixes.

## [0.1.6-alpha] — 2026-05-28 — "Smudge, deform, and shortcut presets"

### Added
- Early Deform transform mode.
- Shortcut presets (save and switch between shortcut sets).
- Brush cursor that matches the shape of the current dab.

### Reworked
- Reworked Smudge tool.
- Redesigned Keyboard Shortcuts tab, Color panel, and Layout Presets popup.

### Improved
- Improved ABR parser.

### Fixed
- Visual bugs in Lasso Fill and the transform preview, plus many others.

## [0.1.5-alpha] — 2026-05-14 — "Color, multi-layer transform, and alt-copy"

### Added
- Color tab in the brush engine with full HSL control and brush blend modes.
- Alt-copy: hold Alt while moving layers to duplicate them.
- Early ABR parser (dab textures only).
- Zoom overlay.
- Completely redesigned installer.

### Improved
- Completely reworked brush stabilizer.
- Full multi-layer transform support, including whole groups with subgroups.

### Fixed
- Bugs across the application.

## [0.1.4-alpha] — 2026-05-03 — "Text tool, Auto Snap, and brush sharing"

### Added
- Fully featured Text tool with formatting.
- Auto Snap axis-snapping system.
- Brush export/import from the brush editor.
- New About section.
- New input sources: Random (replaces Jitter) and Stroke Direction.

### Improved
- Reworked brush stabilization in the brush editor.
- Significant optimization across several scenarios.

### Removed
- The Jitter section (replaced by the Random input source).

## [0.1.3-alpha] — 2026-04-25 — "Layers, dabs, and smoother canvas"

### Added
- In-app release notes panel.
- New update banner and first-run integration.
- New default start banners.
- Custom image dabs for brushes.
- Custom layer colors and Merge Down for layers.

### Improved
- Reworked Layers panel, brush stabilization, and smoother canvas movement.
- Significantly improved Canvas Resize performance.

## [0.1.2-alpha] — 2026-04-06 — "Brush engine foundations"

### Added
- New brush engine and Brushes panel.
- About section (tester list still growing).
- Curves in the brush editor (most parameters can react to pressure / stroke age).

### Fixed
- Many bugs and optimization issues.

## [0.1.1-alpha] — 2026-04-02 — "Brush workspace refresh"

### Redesigned
- Brush panels and brush editor.

### Reworked
- Lasso Fill, now optimized for infinite areas.

### Fixed
- Many bugs across the application.

## [0.1.0-alpha] — 2026-03-27 — "Infinite canvas preview"

### Added
- New, cleaner UI design.
- Infinite canvas and canvas mirroring.
- New Warp transform mode.
- New blur brush.
- Quick-actions canvas overlay.
- New Board layer type.
- First-launch startup setup and layout presets.
