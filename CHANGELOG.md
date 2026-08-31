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

## [0.3.4-alpha] — 2026-08-31 — "Strokes and details"

This release adds brush dynamics driven by pen tilt and stroke speed, copying a
selection to a new layer, and radius controls for effects on the canvas.

### Added

- Pen Tilt and Stroke Speed are new brush dynamics sources with editable response
  curves. Pen Tilt uses the direction of the tilted pen on supported tablets; speed is
  measured in screen space so zoom does not change the response to the same hand
  movement.
- A Smoothing page lets each brush parameter filter changes in its dynamics, with a
  strength control and a response curve. At zero strength the filter is disabled.
- The Brushes panel offers cards or a list with brush names, preview sizes from 50% to
  200%, and horizontal or vertical pack navigation. These options are available in the
  panel context menu and saved with the workspace layout.
- Drag brushes to reorder them within a pack or move them between packs, both in the
  Brushes panel and in the Brush Editor. Favorites keep a separate order without
  rearranging their source packs.
- Brush packs can also be reordered by dragging. Brush and pack order changes are saved
  and shared across workspace tabs.
- Layer via Copy (Ctrl+J) copies selected pixels from a raster layer to a new layer at
  the same position, without using the clipboard. With no pixel selection, it still
  duplicates the selected layers. Layer via Cut (Ctrl+Shift+J) moves the selected pixels
  to a new layer in one undo step.
- The Edit menu adds 90-degree and 180-degree rotations and direct access to Warp.
  Rotation and flip commands act on the pixel selection when one exists, or on the
  selected layer otherwise, and also work during an active transform.
- Canvas Resize snaps its frame to finite canvas edges and visible layer or group bounds
  while drawing, moving or resizing it. It uses the existing canvas and layer snap
  settings and displays alignment guides.
- Selecting Twirl, Pinch or Ripple displays a radius ring on the canvas. Drag the ring
  to change the radius with a live preview; the corresponding panel value stays in sync
  and the drag creates one undo step.
- The effect SDK advances to ABI 1.1 with optional declarative canvas controls. The host
  accepts ABI 1.0 descriptors through their struct_size prefix; plugins can opt into
  radius controls without implementing a separate widget.
- Regression coverage was added for brush blending, stroke input, selection extraction,
  multi-layer edits, transform actions, snapping, fill preflight and the shader
  catalogue.

### Changed

- Copy Merged (Ctrl+Shift+C) replaces the Camera command. It copies the visible result
  inside the selection, including layer effects and transparency, and respects soft
  selection edges. Custom shortcuts assigned to Camera are retained.
- Effect cards now highlight the selected effect. Cards have a distinct header and
  parameter area, and can be reordered by dragging the header as well as the grip.
- Settings and first-run setup now offer interface scales of 85%, 100%, 125%, 175% and
  225%.
- The theme editor places the theme name and favorite toggle beside Apply and Save.
  Names can be edited there, are limited to 32 characters, and receive a unique suffix
  when needed.
- Fill (Shift+F5) now opens a confirmation window before filling the selection with the
  current foreground colour.
- Large top-bar menus no longer use an extra opacity pass during their opening and
  closing animations.
- Glass has stronger colour dispersion, and the reset-settings action uses the theme
  colour for destructive actions.
- Canvas input, navigation, capture and presentation now use a renderer-neutral engine
  contract backed by the existing Aether/OpenGL renderer. This is the first stage of the
  separation: document/history facades and five renderer-dependent UI files remain
  transitional, as listed in docs/renderer-boundary-quarantine.md. No alternative
  renderer ships in this release.
- Shader discovery and startup warm-up validate the full runtime shader catalogue,
  including overlays and selections.

### Fixed

- At 0% stabilization, strokes no longer retain the extra geometry smoothing delay. With
  stabilization enabled, the stroke catches up to the pointer more evenly.
- Brush blend modes now blend against pixels in the target layer, rather than the merged
  visible image. Live previews and finished strokes use the same result, including on
  transparent areas and with opacity, soft selections or alpha lock.
- Fill and Delete Content now process all selected editable layers, including layers
  inside selected groups. Hidden or locked layers and their descendants are skipped;
  required rasterization is confirmed before any edits. The whole operation is one undo
  step. Fill follows mask focus, while Delete Content edits layer pixels.
- Delete no longer swallows text edits in input fields. In the Layers panel it deletes
  the selected layer or mask; on the canvas it deletes selected pixels.
- Interface fonts and controls now follow Ruwa's scale consistently. Sidebars and
  dropdowns keep the correct geometry after scaling.
- Floating panels are visible again after restoring a workspace layout.
- Top-bar menus close reliably on outside clicks, including on the frameless window
  border.
- The Welcome banner crop window displays liquid glass again.

### Removed

- Unused backdrop, blur and smudge shader files have been removed.

## [0.3.3-alpha] — 2026-08-24 — "Six months of Ruwa: export rebuilt, axis-locked strokes, and glass across the workspace"

Ruwa's first alpha was released six months ago today. Export as is now a
workspace mode with a settings panel, a frame on the canvas and a background
pipeline for resampling, encoding and writing. Painting gained axis-locked
strokes and a continuous brush-size drag, while the liquid-glass renderer now
serves panels across the interface.

The release also makes partial selections behave consistently across every fill
path, keeps blur inside a selection boundary, and restores live effect previews
below 100% zoom. Home can create a document from an image on the clipboard, the
splash screen has been rebuilt, and text editing, tabs, brush-pack state and
colour picking received focused fixes.

### Added
- Export as is a workspace mode instead of a file dialog. A docked settings
  panel sits beside a draggable canvas frame and exposes the destination,
  dimensions, scale, resampling filter, background handling and the options
  supported by PNG, JPEG and WebP. PNG can write 8- or 16-bit channels; JPEG and
  WebP expose quality, and formats without alpha use a chosen matte colour.
- Export capture renders the requested region in bounded chunks on the GL
  thread. Resampling, depth conversion, encoding and the atomic file write then
  run on a dedicated worker thread, with progress and cancellation. A sampled
  trial encode estimates the output size from the current canvas and settings.
- Holding Shift during a painting stroke locks it to the first clear horizontal
  or vertical direction. The projection happens before stabilization and
  applies to the brush, eraser, blur, smudge and liquify tools. Shift+Alt now
  resizes the brush by dragging sideways from its current size instead of
  snapping the radius to the pointer.
- When the brush ring becomes too small to read at the current zoom, the canvas
  draws a fixed-size inverted plus cursor in its place.
- Pasting an image while Home is active creates a project through the same
  import path as dropping an image onto the tab. Paste remains available to a
  focused text field.
- Seven decorative tab glyphs by medomij — frame, heart, leaf, spider, cat, bow
  and fire — are available for distinguishing similar tabs. Their permission,
  attribution and licensing are recorded with the other bundled assets.

### Changed
- The command palette, tooltips, layer preview, preset menu, first-run card,
  docking drop indicator and update card now use the same GPU liquid-glass
  renderer as the canvas overlays. Shared capture, padding, tint and fallback
  rules replace the separate blur implementations, and the Welcome banner's
  Open button uses the same renderer over its artwork.
- The splash screen is now an 800 × 500 card with a header image, logo and
  wordmark row, version pill, shared credits, status line and edge-aligned
  progress value. Its chrome fades as one composited layer and the card uses a
  dithered distance-field shadow instead of a border.
- New projects start with a `#D9D9D9` canvas background. Stored presets,
  restored sessions and older files keep their existing background or white
  fallback.
- Delete actions in Layers remove the mask when its thumbnail is the active
  paint target and otherwise remove the layer. The toolbar, swipe action,
  context menu and their labels all follow the same target, and right-clicking
  a thumbnail selects the target it will act on.
- A radial-menu seat is selected by its angular sector anywhere from the hub to
  twice the visible ring radius, so an overshot flick still lands on the seat.
- Tabs activate on release rather than press, and a reorder drag no longer also
  switches documents or panels. Press flashes were removed across buttons,
  rows, menus, sliders and switches; controls that act like buttons now commit
  only when released over the same target.
- The tab context menu offers a different set of icons. The first row keeps the
  workspace glyphs that say what a tab holds — home, file, folder, new file,
  brush, pencil, camera — and the second row is now a set of seven decorative
  marks (frame, heart, leaf, spider, cat, bow, fire) for telling look-alike tabs
  apart. Hovering an icon names it instead of showing its resource alias. Tabs
  already carrying one of the removed icons keep it; the icon simply no longer
  appears in the grid.
- Asset directories follow one naming convention: `resources/Layouts` is now
  `resources/layouts`, `resources/brushes/StandardBrushes` is now
  `resources/brushes/standard`, and the three icon files whose names drifted
  from the set (`Dock-layout.png`, `LiquifyTwirl_cw.png`, `LiquifyTwirl_ccw.png`)
  match their resource aliases. Resource aliases are unchanged, so nothing that
  reads an icon or a layout by name is affected.
- The tester list is shared by About and the splash screen. kira. and medomij
  join the credits; testers without a profile URL remain visible as
  non-clickable cards.

### Improved
- Dragging a Position coordinate moves the layer content live through one
  transform preview and commits a single undo step. Numeric scrubbing can wrap
  the mouse pointer from one window edge to the other, so a drag is not limited
  by the available screen width.
- Arrow keys move an active transform by one document pixel, or ten with Shift.
  Ctrl+Arrow moves between cells of the Position anchor grid. The same path now
  translates free quads and deform meshes, so typed coordinates and nudges also
  work in Distort and Warp.
- Brush-pack expansion records only the packs a user collapsed. New packs open
  by default, programmatic rebuilds no longer overwrite the choice, and every
  workspace tab shares the current state instead of saving competing copies.
- Panel content uses one scaled padding rule, aligning the first control with
  its title and balancing the side and top gaps. The Tools panel derives its
  minimum width from the actual button and margin sizes.
- Text selection highlighting steps aside while its colour is being previewed,
  so the canvas shows the colour itself rather than its inverted selection
  display. A whole colour-picker drag commits as one undo step.
- The liquid-glass composite and frost pyramid were refined, and the display
  pyramid gives the levels currently sampled on screen priority during a
  stroke.

### Fixed
- Fills now use one coverage rule for soft selections across Edit → Fill, the
  GPU bucket, clipped flood-fill results and the progressive preview. Repeated
  fills converge on the selection coverage instead of becoming opaque; partial
  fill colours stay premultiplied, and Fill Selection honours alpha lock.
- The blur brush treats a selection border as a sampling wall. Colour and
  coverage are filtered together, preventing transparent or unselected pixels
  from being averaged into content along the edge while preserving feathered
  selections.
- Drawing on a layer with effects updates live below 100% zoom. Dirty-tile work
  is budgeted per pyramid level, visible levels cannot be starved by the larger
  lower levels, and effect coverage uses the real bounds expansion instead of a
  square worst-case pad.
- Diagonal marching-ants outlines remain visible when zoomed out. The lasso
  overlay also validates the shaders currently bound before drawing.
- Fast pointer movement no longer leaves a tab or its close button highlighted,
  and the reorder ghost is rendered on transparency instead of as a black
  rectangle. Dock-tab close animations apply the configured motion speed once.
- A text session ends when its layer is deselected, hidden, locked, rasterized,
  removed, switched away from or given a mask target. The caret follows external
  text and transform changes in the same frame, and entering text no longer
  forces the Layer Properties panel open.
- Reopening the colour picker during its closing animation no longer lets a
  stale timer close the new picker or steal focus. Live text-colour previews
  keep their active target until the real close completes.
- The brush-size ring survives a simultaneous zoom animation and continues to
  recompute the drag from its anchored screen-space pointer.

### Removed
- The retired April Fools banner, command and settings entry have been removed.

## [0.3.2-alpha] — 2026-08-20 — "Themes you can build, motion you can dial, and a Layer Properties panel with real controls"

A theme is now something you make rather than something you pick. The theme
editor edits a preset live — its fourteen semantic colours, the two font
families and every size in the typographic scale, and how the application
animates — with a preview beside each page showing real widgets responding to
the values being edited, before Apply. Motion itself became a setting: every UI
animation now runs through one policy, so it can be turned off or scaled between
half and double speed, with the canvas kept separate from the interface. Themes
can be created, duplicated, imported and exported as JSON.

The Layer Properties panel was rebuilt around collapsible per-type groups and
docks in the base workspace beside Layer Effects. It replaces the floating text
formatting popup and reaches the parts of the text model no UI ever did — font
size, alignment, leading — while adding strikethrough, tracking, caps and
paragraph spacing. Scrolling was rebuilt on a continuous damping loop that runs
at the display refresh rate, document tabs and dock group tabs reorder with
animation, and a numeric field can be scrubbed by dragging across it.

Underneath: a resting pen no longer steals the cursor from the mouse, a stroke
no longer accumulates a second of input backlog on the native WinTab backend,
and an adjustment layer carrying a blur, glow or shadow no longer corrupts the
canvas.

### Added
- The theme editor is a real editor. A preset picker lists the built-in and
  custom themes with search, and can create a new theme, duplicate the current
  one, delete a custom one, and import or export a theme as JSON. Colors, Font
  and Animations are separate pages, each with its own Apply, and each edits the
  preset being worked on rather than the applied theme.
- Every theme carries font settings: a heading family, an interface family, and
  the sixteen sizes of the typographic scale (Display through Micro, plus Code).
  The scale is semantic now — a role rather than a pixel size — so changing one
  size reaches every widget that asked for that role.
- Every theme carries animation settings: interface animations and canvas
  animations toggle independently, and a speed multiplier from 0.5x to 2x scales
  what remains. Ambient motion (the marching ants, the canvas corner effect, the
  message popup glow) and anything whose duration is a timeout rather than an
  effect (loading indicators, progress bars, auto-hide timebars) stays outside
  the policy on purpose.
- The Colors, Font and Animations pages each preview themselves. The Animations
  banner is a live scene built from the real widgets — a sidebar walked by a
  synthetic cursor, a docked panel group sliding its members past each other, a
  settings column operating its own dropdown, toggle and switcher — driven by
  the preset being edited, so the pending speed and master switch are visible
  before Apply.
- The Layer Properties panel is a scrollable column of collapsible groups: an
  identity header with inline rename, a shared Position group, and groups that
  appear only for the layer type they describe. Position reads the top-left of
  what the layer actually draws and moves it the way the Move tool does, with a
  reusable anchor grid. The base workspace docks the panel beside Layer Effects
  as a tab group under Layers.
- Character and Paragraph groups replace the on-canvas text formatting popup and
  go well past what a floating strip could hold. Font size, alignment — justify
  included — and leading are reachable for the first time, and strikethrough,
  tracking, caps and paragraph spacing are new in the model, the renderer and
  the file format (`.rwf` v33). Every one of them is undoable, the typography
  travelling in the text command beside the style runs. Edits stream live and
  collapse into one undo step per interaction, so dragging a value or previewing
  a font on hover costs one entry rather than one per frame, and abandoning a
  preview puts the layer back. Which characters an edit lands on follows
  Photoshop: a selection scopes it, anything else means the whole layer.
- New Project has a Recent tab. It keeps the configurations actually used — size,
  canvas bounds, colour mode, background, tile format — one card per
  configuration: recreating the same setup floats its card to the front and
  bumps a use count rather than adding another. A card carries its custom name,
  or borrows the name of the built-in preset whose size it matches, and the
  list is capped at twelve.
- Dragging horizontally across a numeric field scrubs its value, counting up to
  the right and down to the left. Every field built on `NumericInputField` gains
  it: the theme editor's font sizes and animation speed, layer position and text
  metrics, and effect parameters that opted into the number editor. A whole
  sweep commits once on release, so a listener that turns one into an undo step
  records a single entry.
- Document tabs reorder by dragging, animated, and the dock compass gained an
  explicit button for dropping a panel into a tab group rather than relying on
  the centre of the panel alone.
- About shows the lifetime time spent in the application. The total is folded
  into the settings once a minute and on quit, so a crash costs at most one
  interval, and a settings reset keeps it — it is a statistic, not a preference.

### Improved
- Scrolling is one continuous motion instead of a series of shoves. Wheel,
  stylus inertia, scrollbar steps and track jumps share a frame-driven loop that
  eases a sub-pixel position toward an accumulating target, so an impulse
  arriving mid-flight raises the velocity rather than restarting a curve. The
  driving timer runs at the display refresh interval, which is visibly smoother
  above 60 Hz; rates are per input path, so a precision wheel or trackpad tracks
  nearly 1:1 while a notched wheel glides and repeated notches accelerate.
  Releasing a stylus swipe hands its velocity to the same loop with no seam at
  the release, and a pen that had come to rest no longer flings. The font
  dropdown scrolls this way too.
- The selection's marching ants are drawn from a cached instance buffer rebuilt
  only when the selection changes, with a level of detail chosen by zoom,
  instead of re-batching and culling every edge on the CPU each frame.
- The zoom info overlay joined the rest of the on-canvas glass: the same frosted
  backdrop, silhouette and border treatment, with the frost kept in step with
  the overlay's own fade in and out.
- The GL tool and eyedropper cursors no longer route the whole scene through an
  offscreen render and a full-surface blit. Each overlay reports the rectangle
  it covers and the frame copies only those pixels.
- Duplicating an already-duplicated layer numbers the copy instead of stacking
  suffixes: "Layer (copy)", "Layer (copy 2)", and so on, rather than
  "Layer (copy) (copy)".
- The colour input button was restyled, and the About page scrolls only its left
  column, so the Build Details panel stays put beside it.

### Fixed
- An adjustment layer with a blur, glow or shadow above other effect-carrying
  layers no longer paints a shrunken copy of the whole picture into the corner
  of the canvas. Building its padded source recomposites everything below, and
  any effect encountered down there started a second evaluation that shared the
  same padded scratch textures — clearing the half-assembled source and, when
  its own padding differed, resizing it, after which the chain read the result
  back at the wrong scale. Each evaluation now takes its own scratch, so nesting
  is safe however deep it goes. The corruption only surfaced once the nested
  effect actually re-ran instead of answering from its cache, which is why
  expanding or collapsing a group — a full recomposite — appeared to trigger it.
- A document holding even one clipping layer — a hidden one counted — made every
  bounds-expanding effect on an adjustment layer render as a hard 256px tile
  grid. Each re-entrant isolated composite owned one fixed texture pair and so
  could not nest, and the adjustment path answered that by refusing any stack
  below it that held a clipped layer, a nested adjustment or a bounds-expanding
  group — a refusal that dropped the padded-neighbour pipeline entirely.
  Isolation frames are handed out per nesting depth now, so nothing has to be
  refused: nested clip groups no longer clobber each other, an adjustment used
  as a clip base keeps its chain, an adjustment inside a clip group can rebuild
  its source at the neighbouring tiles, and the viewport preview no longer runs
  the adjustment over a background-baked source.
- A pen left resting inside the tablet's hover range no longer takes the system
  cursor away from the mouse — on one Huion driver, warping it to the top-left
  corner of the screen. Ownership was granted on any packet whose position
  differed from where the last stroke ended, a value that stays frozen while the
  mouse owns the pointer. The pen has to earn it back now: a button transition
  still grants it immediately, but otherwise the pen must travel 12 px and hold
  a coherent path for three consecutive packets, and packets sitting on the
  output origin with no pressure and no buttons are dropped before they can
  become a position. An opt-in WinTab trace (`RUWA_WINTAB_TRACE=1`, off by
  default) records the granted context, proximity, filtered packets, ownership
  changes and every cursor warp, since this class of fault reproduces only on
  particular driver builds.
- Dabs no longer land up to a second late on the native WinTab backend at 0%
  stabilization. The input queue capped the work a tick could do and left
  latency unbounded, so a device producing faster than the brush rasterizes
  built a backlog that grew with drawing time; the zero timer meant to run those
  ticks never fired under a 200-266 Hz packet stream, and what actually paced
  the queue was the repaint each batch asked for. A tick empties the queue now,
  decimating by significance (position, pressure and timing error) when what is
  queued exceeds what the tick can afford — which does not thin the stroke,
  since dabs are placed along arc length — and it is serviced from the frame it
  is scheduled against. Any non-zero stabilization used to mask this, because
  the stabilizer's catch-up timer drained the queue as a side effect.
- Lasso, lasso fill and the shape selections no longer drag a second, lagging
  cursor across the canvas. Those tools grab the mouse on the canvas panel,
  which is the GL widget's parent rather than a descendant, so the tablet filter
  read the grabber as ordinary UI and re-synthesized every stylus packet as a
  mouse event, pushing the panel's arrow over the canvas cursor as an
  application override. Packets now go to the grabbing panel's tablet handler,
  gated on an interaction actually running, and a selection drag counts as
  owning the pen.
- The tool cursor no longer freezes while undo is held. A held undo drains
  through posted events, which Windows serves ahead of queued mouse input, so no
  MouseMove arrived to carry a new position or to ask for the frame that would
  draw it. The pointer is sampled in `paintGL` now, a moved pointer asks for one
  more frame, and frames keep coming for 250 ms after each undo or redo.
  Overlays with an animation of their own — the marching ants, the brush
  ring — hid this completely.
- Dropping layers into a group is undoable. The drop went straight to the model
  without pushing anything, so the next undo reached past it to whatever created
  the group and removed the whole subtree, taking the dropped layers with it,
  while redo restored the empty clone captured when the group was created.
- Reordering tabs inside a dock group works: the group header owns that drag now
  instead of the panel title bar.
- The update installer keeps a readable log. The script opened it with
  `Set-Content`, so every attempt wiped the previous one and a failed update
  left no trace. The log is append-only now, shared by the application and the
  script and rotated at 512 KB, and it records the check, the download, the
  install plan and every reason the installer refused to start. "Restart and
  install" no longer fails silently either: a dismissed UAC prompt, a window
  that refused to close or a package that was never verified in this session now
  reaches a popup that can open the log folder. A rollback that had already
  succeeded is no longer hidden by an exception thrown while restarting the
  previous version.
- Closing a tab no longer risks a crash while its canvas is still handing out
  providers during teardown.

### Removed
- The on-canvas text formatting popup, replaced by the Character and Paragraph
  groups in the Layer Properties panel.

## [0.3.1-alpha] — 2026-08-13 — "A radial menu, real selection commands, and cursors drawn on the canvas"

Right-clicking the canvas now opens a configurable radial menu: pages of seats
around a hub, each one a command that carries its own title, icon, shortcut and
enabled state. The selection operations that only ever existed as a floating
popup became commands with a home in `Edit → Selection`, joined by Select All,
Invert and Reselect, and export moved into its own `File → Export` submenu. The
cursor is drawn by the canvas itself now — an eyedropper ring that shows the
sampled and the current colour side by side, tool badges on the arrow, a
crosshair for the shape selections — so it can no longer swim a frame behind
what it points at. The on-canvas glass refracts what is behind its bevel and
tints to the theme instead of multiplying towards black, the hold-keys on the
canvas became bindable, and a shortcut recorded under a Cyrillic layout now
fires.

### Added
- Right-clicking the canvas opens a radial menu. Seats sit on a ring around a
  hub, each resolved from a command id, so its title, icon, shortcut and enabled
  state can never drift from the command it points at. Seats open nested pages,
  the hub doubles as the back button, and the whole thing is the on-canvas glass:
  frosted and refracted per piece.
- `Edit → Selection` collects what the selection can already do. Transform, both
  flips, Select Layer Content and Select Layer Mask had no command at all and
  were therefore missing from the command palette and unbindable; they exist now,
  bindable but unbound. Every entry takes its enabled state from the command
  itself, so Select Layer Mask dims on a maskless layer.
- Select All (`Ctrl+A`), Invert Selection (`Ctrl+Shift+I`) and Reselect
  (`Ctrl+Shift+D`). Invert visits everything the document covers, not only the
  tiles the mask allocated, and keeps partial coverage. Reselect brings back the
  mask that a Deselect threw away. `Ctrl+A` routes to a focused text field first,
  the canvas text editor included.
- Export has its own `File → Export` submenu holding "Export as..." and "Fast
  Export as PNG".
- The canvas hold-keys — move content, eyedropper and pan — are bindable. They
  are held rather than pressed, so they cannot be plain shortcuts; they now have
  their own defaults, overrides and presets, and register as taken sequences so a
  command sharing one is reported.
- Assigning a sequence that is already taken no longer fails silently. Both sides
  are marked as conflicting and disabled until one of them changes.
- Right-clicking inside an active transform offers the two mirrors under the
  Classic/Deform modes. They play the same eased flip the selection popup uses
  and leave the session open, so a mirror combines with further edits. Free quads
  and deform meshes have no scale sign to negate and come through disabled.
- The eyedropper became a ring centred on the pointer: the sampled colour above,
  the current one below, both opaque so neither is tinted by what is behind them.
- The pointer itself is drawn by the canvas, with the tool badge hanging off it
  for fill, classic fill, move, magic wand and both lassos, and a crosshair for
  square and circle selection. Windows hands the system cursor over a frame later
  than the position the canvas renders at, so a system cursor visibly swims
  against on-canvas chrome; drawing it here locks the two together.

### Improved
- The on-canvas overlays gained a glass bevel that bends what is behind them,
  displacing the captured scene by what Snell gives for a quarter-round edge
  before the frost is applied. The welcome banner's open button got it too.
- The frost mixes towards the theme's surface colour instead of multiplying
  towards black, so dark artwork under a panel no longer crushes and disabled
  icons stay legible on the darker themes. Hover is one treatment for every
  control, and the brush-pack and zoom-to-fit buttons drop their standing plate.
- The canvas tool strip moves as one thing. Its width, height, position and the
  tool stack's size are driven off a single transition, so a canvas-mode switch
  is one movement instead of a stopped animation followed by a resize, and the
  animated sizes hold a fixed parity so the content cannot jump a pixel sideways
  mid-flight.
- Holding a button no longer flashes a wash over it. Toolbar buttons, layer
  toolbar and lock toggles, combo boxes and their popup rows, and preset rows all
  answer a click with the state it produces — the toggle that settles, the arrow
  that flips, the row that selects — which the press wash only ever sat on top of.
- Dropdown popups open without doing first-use work at the click: the popup is
  created lazily under the right window and warmed when the pointer reaches the
  combo, which pays for the style polish, the first layout and the glyph
  rasterization (the Cyrillic fallback in the language list is the visible case).
- Keyboard Shortcuts sections follow menu order rather than the alphabet, and
  Navigation and Tab merged into one "Tabs & Navigation". Going to the shortcuts
  page joined the other go-to-page commands. Command ids are untouched, so
  customised shortcuts survive.
- One keycap renderer for every place a shortcut is drawn. The caps are tighter
  (no `+` between the keys of a chord, less padding) and painted in opaque
  palette colours rather than a white film, so they no longer read as a hole on
  presets whose background is already near black.

### Fixed
- A stroke undone while zoomed in no longer leaves a frozen tile that fades back
  in on zoom-out. The display pyramid could derive a level tile from composition
  pixels the compositor had already been told were wrong, and dirt only ever
  walks up from level zero, so an ancestor built at an earlier zoom had no way to
  learn its source had moved. Tiles now carry the versions they were built from
  and an audit re-derives staleness from them.
- Toggling a layer's visibility, or undoing, no longer leaves ghosts at the
  screen edges or a band of the canvas at half alpha at certain zooms. Dirt is a
  standing request to reconsider now rather than a promise of work, so a tile
  whose source is unsettled keeps it and rebuilds from whatever is current.
- Holding undo no longer leaves a stroke visible in the Navigator after it is
  gone from the canvas. Overview tiles re-dirtied mid-fade were withheld with
  nothing scheduled to come back for them.
- A shortcut recorded under a Cyrillic (or any non-Latin) layout is stored by
  physical key instead of by the character the layout produces, so it can
  actually fire — and a binding that reaches the application filter is retried
  against the physical key, so existing ones work too.
- The slider value label no longer washes out on thin fonts. It was painted twice
  into the same pixels, so every partially covered pixel landed halfway between
  the two inks — at 8px a face such as Comfortaa has no fully covered pixel at
  all.
- The Settings panel shows the choices made on the first-run page instead of the
  values captured before it ran.

### Removed
- The brush quick popup, replaced by the radial menu on canvas right-click.

## [0.3.0-alpha] — 2026-08-10 — "Editable smart objects, tabbed panels, and one quality at every zoom"

A smart object is now a document you can open: double-click it and its layers,
groups, masks, and effects appear in their own tab, committed back to the parent
as a single undo step. Duplicates share those contents as instances, smart
layers accept masks and merge, a group or a whole multi-selection converts into
one object, and a filter can be told to run in content space so it follows the
object's placement. Panels group into tabs when dropped on one another, the
workspace arrangement is a preference again instead of something a project
carries into your window, and a new display pyramid keeps the canvas looking the
same whether you are zoomed in or drawing zoomed out.

### Added
- Smart objects hold a nested document. Opening one gives it a tab with the full
  editing environment — layers, groups, masks, effects, nested objects — and
  saving or closing the tab commits the result into the parent as one undo step.
- Duplicating a smart layer creates an instance: the contents are shared, so
  editing one updates them all, while transform, mask, and effects stay per
  layer. The Layers panel marks instances, and painting into one detaches it
  first.
- `Replace Contents` rebuilds an object's pixels from a file while keeping the
  placement already arranged; every instance follows. `New Smart Object via Copy`
  detaches on purpose.
- Smart layers accept layer masks and take part in merges. Merging rasterizes the
  object first, as expected, and undo unwinds the whole preparation.
- Converting to a smart object works from a group and from a multi-selection: the
  selected layers become one object holding them, not one object per layer.
- Each layer effect gained a space: document space, as before, or content space,
  where it is baked into the object's contents before its placement, so it
  rotates, scales, and deforms along with it.
- Right-clicking a multi-selection in the Layers panel keeps the selection and
  opens a menu for the whole set — visibility, locks, colour, group, merge,
  duplicate, delete, rasterize, clear, masks, and effects — each run as a single
  undo step.
- Dropping a panel on the centre of another groups them into tabs. The group's
  header strip behaves like the document tab strip at the top of the window,
  including its animations, and a group taken apart returns its panels to the
  layout.
- Objects imported into or placed in a project record where they came from, both
  relative to the project file and absolute, in preparation for linked contents.

### Improved
- Zooming out no longer changes image quality. Levels above the composition
  cache live in a persistent pyramid, so a stroke no longer turns the whole
  canvas aliased for a few frames and no longer draws a live map of dirty tiles.
  At low zoom the number of quads the canvas issues is roughly constant instead
  of growing with the visible tile count.
- Minified content is filtered everywhere, not only on the canvas: board layers,
  the export and overview previews, and the transform preview each got the
  filtering they were missing, and the pyramid replaced the old per-tile mip
  chains, so the memory cost is a wash.
- Drawing a lasso selection or lasso fill costs the same whether the outline is
  short or long. The screen mask is now extended as points arrive instead of
  being recomputed from the whole polygon every frame.
- Brush dynamics bound to the `Time` input advance evenly with a stylus. Dabs
  carry their own de-jittered clock instead of the coarse timestamps the pen
  driver stamps whole bursts of samples with, which used to turn a smooth
  time-driven gradient into hard bands.
- A smart object's flattened pixels are clipped to its own canvas, so the object
  shows the same picture on the parent canvas as inside its contents tab.
  Converting text carries the text model inside, so the contents tab opens on
  editable type and the object's box is the text's own box.
- Project files reached format v32, carrying nested documents, content identity,
  and per-grid pixel formats. Files written by earlier versions still open and
  are upgraded on save, and a file with ten instances of one object stores those
  pixels once.

### Fixed
- Closing or switching a tab while it is still animating no longer crashes. The
  tab strip tracked its tabs through raw pointers, which stayed non-null after a
  tab was destroyed, so the end of a slide or a pending deferred initialization
  could reach into a freed tab.
- The dock layout, the canvas overlay positions, and their visibility are user
  preferences again. Opening a project no longer rearranges the application into
  whatever arrangement that file happened to remember. Saved layout presets still
  carry overlay positions, which is the point of saving one.
- A smart object's contents tab closes together with the document it belongs to,
  in every order of open tabs, instead of leaving a stranded tab mid-animation.
- Projects with 8-bit imported images inside 16-bit or 32-bit documents now
  preserve each content grid's actual pixel format, so saving and reopening a
  mixed-format composition no longer reports a corrupted first layer.
- Affected RWF v27-v31 files are recovered by validating the exact payload size
  of every tile in a grid, then upgraded to the self-describing v32 layout on
  save. Ambiguous or internally conflicting payloads remain rejected.
- Fill operations now interpret snapshots using the target grid's actual
  format, including RGBA8 imported layers in higher-precision documents.
- A canvas size pasted with a group separator — `3 000` from a browser, a
  spreadsheet, or the calculator — is read as the number the field accepted
  instead of silently falling back, which could create a one-pixel project that
  hung on load. Such a file is now rejected rather than opened.
- Merging a masked layer no longer resurrects what the mask hid.
- Panel groups slide in with the rest of the workspace during the startup
  animation instead of having their tab strip appear on the last frame.

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
