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

### Fixed
- Projects with 8-bit imported images inside 16-bit or 32-bit documents now
  preserve each content grid's actual pixel format, so saving and reopening a
  mixed-format composition no longer reports a corrupted first layer.
- Affected RWF v27-v31 files are recovered by validating the exact payload size
  of every tile in a grid, then upgraded to the self-describing v32 layout on
  save. Ambiguous or internally conflicting payloads remain rejected.
- Fill operations now interpret snapshots using the target grid's actual
  format, including RGBA8 imported layers in higher-precision documents.

## [0.2.9-alpha] — 2026-08-03 — "Rearrangeable panels, richer tooltips, and cleaner gradients"

This update makes the workspace yours to arrange: tools and layer actions
reorder by drag and drop and can be hidden individually, every panel gets a
close button, and thumbnails open a large hover preview. Ruwa now draws its own
tooltips with the assigned shortcut in them, reports what a transform drag is
doing right at the cursor, and moves zoom controls into the Navigator. On the
rendering side, 8-bit writes are dithered and stroke buffers widened, which
removes gradient banding and the hue drift that soft low-flow strokes used to
accumulate.

### Added
- Tools reorder by drag and drop in the Tools panel, and individual tools can be
  switched off from the panel's title context menu under `Visible tools`.
- The action buttons of the Layers panel toolbar (add layer, adjustment, group,
  mask, duplicate, merge, delete) reorder and hide the same way. The alpha-lock
  and layer-lock toggles stay pinned and are deliberately left out of both.
- Ruwa draws its own themed glass tooltips instead of the system ones, and they
  show the keyboard shortcut assigned to the action next to its name.
- Every dockable panel that can be closed now carries a close cross at the right
  end of its title bar, including while it floats.
- Resting on a layer or mask thumbnail opens a large preview popup with the
  layer rendered at a readable size next to its properties.
- The Navigator panel gained zoom controls: a zoom slider with a percentage
  readout and a fit-to-view button.
- A live readout beside the cursor while transforming — offset in pixels while
  moving, degrees while rotating, and percentage of the starting size while
  scaling.
- The Brush Settings panel now shows a curve button next to every setting that
  supports parameter dynamics. Clicking it opens a popup with the same dynamics
  editor as the brush editor — input sources, blend mode, and the curve itself.
- Releasing a Rotate View drag within 2.5° of a quarter turn animates the view
  onto that exact angle instead of leaving it slightly off.

### Improved
- The four on-canvas overlays gained a liquid-glass pass over their existing
  backdrop blur: a tinted specular sweep along the outline and a pair of inner
  shadows.
- The Layer Effects panel was reorganized: the `Add effect` button moved into
  the panel's subtitle bar and its separate search field was dropped, since the
  effect picker has its own search.
- Compositor state setup was streamlined, cutting redundant GL state changes per
  drawn tile.
- The Rotate View tool has a clearer icon.
- Liquify is now a proper command, so it can be given a shortcut and found in
  the command palette, and top-bar menu entries update their shortcut text the
  moment a shortcut is rebound.

### Fixed
- Gradients and soft strokes no longer band or drift in hue on 8-bit documents:
  8-bit writes are dithered, in-progress src-over stroke buffers are kept at
  16-bit float, and values already sitting on the 8-bit grid are rounded instead
  of dithered, which removes the isolated off-by-one pixels that dithering
  scattered across flat areas.
- Zoomed-out strokes no longer draw a live map of which tiles are dirty: the
  choice between mipmapped and unfiltered sampling is now made once per frame
  instead of per tile.
- A fast undo burst no longer crashes: a pending stroke is finalized before undo
  runs, and commands are never destroyed or reused while a background prefetch
  is still reading them.
- Picking a colour is no longer undone later: the per-tool colour state is kept
  in step with it, so a tool switch or a brush-settings edit no longer resurrects
  the previous colour.
- Windows reopen on the monitor they were last used on in the cases the previous
  attempt missed, splash screen included.
- A stylus tap now moves keyboard focus to UI panels the same way a mouse click
  does.
- The main window no longer flickers under DWM composition when it is opaque.
- Filling inside a selection invalidates the affected tiles, so the result shows
  up immediately.
- Auto snapping prefers exact relations over approximate ones, so a drag settles
  on the alignment it is closest to.
- The update installer runs from outside the installation directory, so it can
  replace every file it needs to.
- Toolbar drag-reorder in the Tools and Layers panels keeps tracking once the
  cursor leaves the panel bounds instead of snapping the button back.
- The Navigator's zoom strip paints its own surface background instead of
  relying on whatever is behind it.

## [0.2.8-alpha] — 2026-07-31 — "Smarter snapping, smoother zoom, and a faster paint loop"

This update rebuilds auto snapping around other layers and equal spacing, adds
unified range controls for brush dynamics, and makes canvas rendering smooth at
every zoom level. Painting, selections, Liquify, and floating panels were made
measurably faster, and stylus contact, the hue ring, and the updater's recovery
behaviour were fixed.

### Added
- Auto snapping was rebuilt on a new solver. It previously aligned content to
  the canvas centre and edges only; it now also aligns to the edges and centres
  of other visible layers and groups, and to equal spacing between neighbouring
  objects, with live guides and an on-canvas measurement capsule. Hold `Alt`
  while dragging content to suppress snapping for that move; `Ctrl+Alt` pressed
  at drag start still begins a copy-move, and rotation is never snapped.
- Four Editor settings for the new system: `Snap to canvas`, `Snap to layers`,
  `Snap to equal spacing`, and `Pixel-align raster moves`. All are on by
  default.
- A unified range control for brush dynamics. Dynamics that previously exposed a
  single amount now use a two-handle range slider, and the random and
  stroke-direction sources accept the same blend modes as the other sources.
- Four additional built-in welcome banners, and a smaller re-encode of the
  existing one.

### Improved
- The window now reopens on the monitor it was last used on, and the splash
  screen and startup animation follow it.
- Canvas tiles are rendered with mipmapped sampling, so zoomed-out views stay
  smooth instead of aliasing, and mipmap generation no longer stalls live edits.
- The navigator animates its viewport frame and tile updates instead of jumping,
  and its cache is populated correctly on the first frame.
- Theme and welcome-banner selectors expand, collapse, and cross-fade smoothly
  instead of snapping between states.
- The animated fill preview is now rendered in screen space, matching the rest
  of the live preview path.
- Painting is faster: per-dab and per-event work in the paint loop was reduced,
  large-stroke commits no longer stall, and dynamic taper previews are rebuilt
  far less often.
- Selection commits no longer pay per-pixel and whole-mask overhead, and Liquify
  reuses displacement-field textures and advects only the area under the dab.
- Multi-layer transform previews keep a stable source size, so cached renders are
  reused across the drag.
- Floating panels are no longer repainted on every canvas frame.

### Fixed
- A pen no longer loses contact mid-stroke when the cursor crosses the window
  boundary: context-only WinTab proximity events are separated from real
  hardware transitions.
- The hue ring and the vertical hue bar now use the intended colour layout.
- The Blur brush drives its strength from a per-pixel sigma instead of an
  opacity cross-fade, so soft brushes no longer ghost.
- Magic Wand region detection now matches Classic Fill.
- Stationary pressure packets no longer accumulate stacked src-over dabs.
- A tool switch made an instant before drawing is honoured by the stroke that
  follows.
- Smart layers are no longer reprojected while their effects are being edited.
- Transform preview pixel sampling is aligned with the applied result.
- Dock panel push resizing is reversible again: pushing a neighbouring panel and
  dragging back restores the original sizes.
- Recursive scroll layout refreshes no longer re-enter while a list is being laid
  out.
- A failed cleanup step after a successful update no longer rolls the healthy
  installation back; the startup health check is now the commit point, and the
  updater refuses to proceed while another instance is running from the same
  installation.

## [0.2.7-alpha] — 2026-07-26 — "Magic Wand, procedural textures, and smarter selections"

This update adds a Magic Wand selection tool and a procedural texture editor for
brushes, extends copy, cut, paste, and Delete to layer masks and selection
pixels, reorganizes the tool bar into clearer groups, and gives the workspace an
animated entrance on startup. The canvas renderer and effect caches were also
modernized for smoother drawing, previews, and compositing.

### Added
- A Magic Wand selection tool (`W`) that selects a contiguous region of similar
  colour on the active layer. It follows the layer's effect-processed shape,
  supports add and subtract modifiers, and computes large selections in the
  background instead of blocking the canvas.
- A procedural texture editor in the Brush Editor with six generators — pencil
  grain, fractal noise, Perlin noise, dots, lines, and checkerboard — each with
  its own parameters and a live preview. The Texture tab now has a mode
  selector; image-based textures are announced there as a future addition.
- Copy, cut, and paste for layer masks and for the pixels inside a canvas
  selection. Pasted pixels arrive as a new `Pasted` layer at their original
  document position and go straight into transform mode.
- Ctrl+click on a mask thumbnail loads that mask as a canvas selection.
- A contextual `Delete`: it removes the selected layer or its mask when the
  Layers panel has focus, and the pixels inside the active selection on the
  canvas. The separate delete-layer command no longer binds `Delete` by default.
- Brush deletion directly from the Brushes panel.
- A new `Stylized Brushes` default brush pack.
- An animated workspace entrance: docked panels slide into their final places on
  startup and hand over to the real layout without a visible seam.

### Improved
- Tool groups were reorganized. Blur, Smudge, and Liquify now share one grouped
  slot, and the bar is split into navigation, drawing, selection and movement,
  and everything else.
- Adjustment layers are now applied in screen-space compositing as well, so they
  stay correct during brush strokes, transform previews, and lasso fill.
- Alt now activates the eyedropper while Lasso Fill is the active tool.
- Group and adjustment-layer effects now reuse composites and effect results
  while their source content is unchanged, avoiding repeated work during
  painting, panning, and effect editing.
- The GPU rendering path now uses OpenGL 4.5 direct-state access, immutable
  texture storage, and persistent asynchronous readback buffers across brushes,
  canvas previews, effects, selections, and transforms.

### Fixed
- Layer drag & drop and group undo no longer drop clipping masks partway through
  a multi-layer move.
- Hiding the base of a clipping group now also hides its clipped layers on the
  canvas, in live previews, and in exported content.
- Copy, cut, paste, and Delete no longer edit the document while a text field
  (inline layer rename, search fields) has focus; the keystroke goes to the
  field instead.
- The `[` and `]` brush size and opacity shortcuts no longer swallow typed
  characters in text input.
- The first-run integration tab no longer stays blank at startup until it is
  recreated.
- The startup zoom-in appearance animation no longer freezes mid-flight when an
  existing project's recent-projects thumbnail is captured.
- Fill and lasso fill now refresh layer-effect caches on commit, so tiles on
  layers with effects no longer revert to a stale cached composite.
- Frosted canvas-widget backgrounds are rendered after transform and lasso
  previews, so those previews no longer erase the blur later in the same frame.

## [0.2.6-alpha] — 2026-07-21 — "Brush favorites, live settings, and smoother drawing"

This update makes brushes faster to organize and tune with favorites, pack
filters, and a new Brush Settings panel, improves WinTab stroke smoothness and
canvas performance, and restores crisp live blur across canvas widgets.

### Added
- Favorite brushes, available from each brush's context menu and collected in
  their own filter. Favorites persist across sessions.
- Pack filters in the Brushes panel for quickly switching between favorites,
  all brushes, and individual brush packs.
- A dockable Brush Settings panel with a live dab preview, direct access to the
  full Brush Editor, and responsive controls for the current brush's starred
  settings.
- A one-click action for copying the current HEX color from the Color panel.

### Improved
- Brush rows now use clearer selection styling, favorite markers, and sharper
  supersampled stroke previews.
- Valid three- and six-digit HEX values now update the selected color as they
  are typed.
- WinTab packet buffering, hardware timing, multi-device pressure ranges, and
  event routing now produce smoother strokes and behave better during heavy
  frames.
- Reduced canvas frame cost in maximized windows by removing redundant surface
  multisampling and limiting brush-cursor capture to the region it needs.

### Reworked
- Canvas-widget blur is now composited from same-frame canvas regions on the
  GPU, keeping the Brush Control, tool-state indicator, and stylus joystick
  sharp and synchronized while they move or fade.

### Fixed
- Smudge and other effect strokes are fully finalized before switching tools,
  preventing queued samples from being committed with the next tool's state.
- Fixed stale, smeared, or missing frosted-glass backgrounds on canvas widgets.

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
