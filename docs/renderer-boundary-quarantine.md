# Renderer boundary quarantine & implementation status (plan v0.6)

Status: Stage 1 of the renderer decoupling plan
(`Ruwa_Renderer_Decoupling_Plan_Stage1_v0.6_Corrected.md`).

Stage 1 introduced the renderer-neutral application contract:

```text
Application (composition root: only place naming the concrete engine)
        |
        v
CanvasEngineQtRuntime   (features/canvas/engine/CanvasEngineQtRuntime.h)
        |  process bootstrap: surface-format policy, offscreen GL context/surface,
        |  hidden-widget warm-up, Aether shader warm-up, per-window preparation
        `-- createBinding(createInfo)
                  |
                  v
CanvasEngineQtBinding  (features/canvas/engine/CanvasEngineSession.h, plan 7.31.2)
        +--> QWidget* viewportHostWidget()
        +--> CanvasEngineSession (view / painting / editing / transform /
        |                          presentation / capture capabilities)
        +--> CanvasEngineQtEvents (view snapshots, history events, engine events)
        +--> CanvasHistoryFacade  (TRANSITIONAL, features/canvas/document/)
        +--> CanvasDocumentFacade (TRANSITIONAL, features/canvas/document/)
        +--> ICanvasBackdropSource
        |
        v
AetherCanvasEngineQtRuntime / AetherCanvasEngineQtBinding
        (features/canvas/engine/aether/ — disposable Aether compatibility)
        |
        v
aether::OpenGLCanvasWidget (legacy Aether/OpenGL implementation)
```

Everything listed below is a deliberate transitional dependency. The
enforcement script (`tools/check_renderer_boundary.py`, CMake target
`check_renderer_boundary`) fails on any NEW dependency outside these lists and
fails on this whole list under `--strict`.

## Implementation status by plan commit group (§7.34)

| Group | Scope | Status |
| --- | --- | --- |
| A | Neutral types/contracts (`engine/CanvasEngineTypes.h`, session, Qt events, binding, runtime interfaces) | **Done** |
| B | Aether runtime/binding/session adapters (`engine/aether/`) | **Done** |
| C | Process/bootstrap cut: `Application`, `StartupController`, `WindowSetupCoordinator`, `MainWindow` GL warm-up moved behind `CanvasEngineQtRuntime` | **Done** |
| D | `CanvasPanel` host/lifecycle cut: binding via runtime, generic host widget, neutral lifecycle names, semantic view/history API | **Done** |
| E | View cut (part): semantic view capability, full view snapshots/revisions (`viewStateChanged`, `presentationSyncRequested(snapshot)`), Navigator/ExportMode/selection popup/view controller migrated | **Done** (residual view math still via widget inside quarantined files) |
| F | Painting/input cut: painting capability (brush state, stroke ingestion/translation/timing, liquify); `CanvasMouseInputHandler`, `CanvasTabletHandler` and `CanvasKeyEventHandler` migrated onto capabilities; `CanvasInputHost::inputGlWidget()` removed (plan 7.6.17-7.6.22) | **Done** |
| G | Selection/fill/sampling/hit-testing/transform cut: **Done** — fill preflight (`CanvasFillRequestResult` across the boundary, `showFillRadiusLimitPopup`/UI deps out of `FillProgressivePolicy`, popup mapping in `CanvasPanel`); transform pointer protocol (landed with the group F handler migration); `CanvasHitTesting::movableContentLayerAt` capability; `CanvasTransformSnapPolicy` pushed at session creation + on `settingsChanged`, renderer no longer queries `SettingsManager` | **Done** |
| H | Presentation/reverse-dependency cut: **Done** — transform metric QWidget overlays moved to the CanvasPanel-side `CanvasTransformMetricPresenter` (engine publishes `TransformPresentationState` via `transformPresentationChanged`); fill progress QWidget moved to the CanvasPanel-side `CanvasFillProgressPopup` with `fillActivityChanged(state)` as the source of truth (the transitional `fillProcessingLayerChanged` is derived by the binding from the activity snapshot); rasterization confirmation injected via `CanvasEngineCreateInfo::rasterizationDecisionProvider`, no `QMessageBox` fallback; shader/init failure reports `Failed` + `CanvasEngineDiagnostic` through `engineFailed`, CanvasPanel presents; viewport-local `CanvasPointerSource` (system + rendered) injected by the binding — the renderer no longer reads `QCursor`/`StylusInputManager`; `TransformPresentationStyle` (chrome colours + rotation-corner `PixelSurface`) pushed to `TransformOverlay`, which no longer reads `ThemeManager`/`IconProvider`; `CanvasMotionPolicy` pushed instead of the shared animation policy; presentation reshape per plan 7.28: `CanvasDisplayStyle`, `CanvasUiColor` presentation payloads, semantic `ToolId` tool-cursor badge (the QRC mapping moved into the Aether binding), document-geometry `TextEditOverlayState` (selection quads + caret axis) | **Done** |
| I | Capture cut: **Done** — the capture capability is the plan 7.29 contract: `CanvasResult<T>` (neutral operation result, plan 7.21.3) + `captureDocumentRegion` / `renderDocumentRegion` / transactional `capturePresentedView` (returns the presented frame plus the exact view snapshot of the captured presentation) + `exportContentBounds` / `navigatorContentBounds`. `CanvasPanelExport.cpp` consumes only the capability: no `grabFramebuffer`, no temporary renderer-flag sequences, no raw viewport crop math; the thumbnail crop is derived from the returned snapshot via the pure `canvasSurfacePointFromDocument` mapping. The old nested-options type is gone, replaced by `CanvasDocumentCaptureRequest` with an explicit alpha-mode field — the export pipeline keeps receiving premultiplied readback and converts exactly once at its end (ExportEncoder) | **Done** |
| J | Document/history quarantine: `CanvasHistoryFacade`/`CanvasDocumentFacade` exist and back undo/redo/push/transactions/memory limit; `CanvasResizeCommand` decoupled from the widget; **pending**: migrate remaining panel wrappers onto `document()` facade, `resizeCanvas` request path | Partial |
| K | Global routing + enforcement: TabletFilter/StylusInputManager on UI-semantic viewport-host discovery; enforcement script + CMake target active; **pending** fake-runtime tests, event-order/coordinate tests | Partial |

## 1. Files still including the legacy renderer header

| File | Remaining use | Migration step |
| --- | --- | --- |
| `features/canvas/ui/CanvasPanel.cpp` | ~230 internal call sites across view/brush/selection/transform/editing/cursor/geometry; `activeUndoManagerOrNull` active-routing | Convert domain by domain onto session capabilities / facades; then delete the `m_glWidget` quarantine member |
| `features/canvas/ui/CanvasPanelRenderContentCreation.cpp` | layer-model application, `applyToolPaintModes`/`applyBrushSettings` internals (the rasterization-confirm callback moved into `CanvasEngineCreateInfo` in group H) | Layer model moves to binding attach-document configuration (plan 7.30.3); brush state to the painting capability |
| `features/canvas/ui/CanvasViewController.cpp` | internal camera math via the widget | Convert to session `view()` internally; public API is already semantic |
| `features/canvas-resize/CanvasResizeController.cpp` | readiness, mapping, overlay state, snap math via `Viewport`, command creation via widget canvas | Narrow resize backend (view mapper + presentation sink + `document().resizeCanvas`); snap pipeline needs its own split |
| `features/export/ExportAreaController.cpp` | same shape as the resize controller | Same treatment as the resize controller |

Sanctioned (not quarantine): `OpenGLCanvasWidget.cpp` (the renderer itself),
`engine/aether/AetherCanvasEngineQtBinding.cpp` (the adapter),
`rendering/PaintGLCameraFrameState.cpp` (Aether-internal helper).

The input handlers (`CanvasMouseInputHandler`/`CanvasTabletHandler`/
`CanvasKeyEventHandler`) consume the engine only through the
`CanvasInputHost` capability accessors (`inputRenderReady`, `inputView`,
`inputPainting`, `inputEditing`, `inputTransform`, `inputPresentation`,
`inputViewportHostWidget`); they no longer include the legacy renderer header
(plan 7.6.17-7.6.22). `CanvasPanel::undoManagerOrNull()/
activeUndoManagerOrNull()` — transitional raw manager access;
`activeUndoManagerOrNull` backs the command-registry undo/redo routing until
it is converted to semantic panel history operations.

## 2. Transitional contract surface

* **`CanvasHistoryFacade` / `CanvasDocumentFacade`**
  (`features/canvas/document/`) — explicitly transitional application facades
  (plan 7.30). `pushLegacyCommand` is intentionally named; the resize undo
  remapping stays an internal detail of the Aether-backed facade/command, not
  facade API.
* **`aetherLegacyRenderer()`** — declared on the neutral runtime header as the
  single Stage 1 quarantine seam; returns the concrete widget for call sites in
  table 1 only.
* **Shared value types crossing the boundary** (plan 7.16.3) — re-homed to
  neutral namespaces, `aether` namespace cleanup complete: `StrokeInputDevice`,
  `TransformHandle`/`TransformHitResult`/`TransformInteractionMode`,
  `TransformSnapVisualState`, `ToolCursorStyle`, `ParameterCircleOverlayState`,
  `CursorOverlayState` and `TextEditOverlayState` are defined in
  `ruwa::ui::workspace`, and `CanvasBackdropRegion` in
  `ruwa::shared::rendering`. The legacy engine keeps internal `aether`
  using-aliases, so its internals build unchanged while the application
  contract no longer names `aether::`. Remaining `aether`-named shared values:
  `Vector2`/`Rect` (the geometry vocabulary used by mapping paths) and
  `ruwa::core::brushes::BrushInputDynamics` (stroke ingestion) — both
  sanctioned by plan 7.16.3 until the replacement engine's own types arrive.
* **Qt value types** in session signatures (`QPointF`, `QRectF`, `QImage`) —
  permitted while the application is Qt-hosted (plan 7.21.5). `CanvasColorValue`
  is in place for brush colour and the eyedropper sample; presentation chrome
  colours cross as `CanvasUiColor` since group H (plan 7.28).
* **`presentationSyncRequested(snapshot)`** — the sanctioned semantic
  pre-presentation hook (plan 7.14.3). In the Aether binding it is sourced from
  the legacy pre-compose phase; may be removed only after Qt chrome updates
  correctly from ordinary state invalidation.
* **`viewZoomChanged`/`viewRotationChanged`** — compatibility events alongside
  the normative `viewStateChanged(snapshot, flags)`; new consumers must prefer
  the snapshot.
* **`fillProcessingLayerChanged`** — transitional compatibility event, now
  derived by the Aether binding from `fillActivityChanged(state)` (group H).
  The renderer widget no longer emits it.
* **`setToolCursorState(..., ToolId)`** — the badge is a semantic tool id
  since group H; the Aether binding maps it to its QRC asset internally.
* **Capture contract (plan 7.29, group I)** — `CanvasResult<T>`
  (`CanvasOperationError` taxonomy) plus
  `CanvasDocumentCaptureRequest`/`CanvasResampledCaptureRequest`/
  `CanvasPresentedViewCaptureRequest`. `CanvasDocumentCaptureRequest.alphaMode`
  is explicit so the export pipeline keeps its premultiplied readback while
  UI consumers get straight images; `canvasSurfacePointFromDocument` is the
  normative pure mapping a `CanvasViewSnapshot` defines (document point ->
  presented-surface pixel). The presented-view capture transaction lives in
  the Aether adapter; UI never toggles presentation flags.
* **`CanvasPaintingCapability::setLiquifyToolMode(int)`** — explicitly
  transitional paint-mode field (plan 7.24.1).
* **`CanvasPresentationCapability::setToolCursorState(... toolIconResource)`** —
  still a QRC asset string; semantic tool-cursor identifier lands with the
  presentation reshape (plan 7.12.6/7.28).
* **`CanvasEngineSession::diagnostic()`** currently returns `nullopt` — real
  failure diagnostics arrive with the renderer failure extraction (plan
  7.15.5).

## 3. Reverse dependencies (renderer -> application) — extracted in group H

1. ~~`OpenGLCanvasWidget` constructs `CanvasMetricLabelOverlay` QWidgets for
   transform metric labels~~ — extracted: the widget publishes
   `TransformPresentationState` (snap labels in document space, drag readout
   segments, viewport-local drag anchor sampled from the injected pointer
   source); `CanvasTransformMetricPresenter` owns the QWidget capsules.
2. ~~`OpenGLCanvasWidget` owns the fill progress popup widget~~ — extracted:
   the widget publishes `CanvasFillActivityState`; `CanvasFillProgressPopup`
   (CanvasPanel-side) owns the 2-second classic wait delay, wording, theme,
   animation and anchor clamping. The never-called "Done!" morph state of the
   old renderer popup was not carried over.
3. ~~`OpenGLCanvasWidget` falls back to `QMessageBox` for rasterization
   decisions~~ — extracted: the decision provider is injected through
   `CanvasEngineCreateInfo::rasterizationDecisionProvider`; with none the
   operation is declined. Shader/init failures report
   `rendererFailed`/`CanvasEngineDiagnostic`; CanvasPanel presents them.
4. ~~`OpenGLCanvasWidget` reads `QCursor::pos()` / `StylusInputManager`~~ —
   extracted: the binding injects a viewport-local `CanvasPointerSource`
   (system pointer for frame-sampled pan + metric anchor, rendered pointer
   with over-canvas/active-window rules for the GL cursor).
5. ~~`TransformOverlay` reads `ThemeManager`/`IconProvider`~~ — extracted:
   `TransformPresentationStyle` (primary/accent chrome colours + rotation-
   corner icon as a `PixelSurface`) is pushed by CanvasPanel on creation and
   theme changes; the overlay uploads/renders supplied data only.
6. Windows Ink feedback in `showEvent()` stays classified as a
   Qt-host/platform concern inside the legacy widget (plan 7.15.9); it must
   not enter `CanvasEngineSession`. `AnimationPolicy` usage was extracted in
   group H via the pushed `CanvasMotionPolicy` (plan 7.15.10); widget-internal
   animation-policy reads are gone.

## 4. Enforcement

* `tools/check_renderer_boundary.py` — fails on renderer-header includes
  outside the sanctioned engine adapter, direct
  `QOpenGLWidget`/`QOpenGLFunctions_*` includes outside GL-internal code,
  `GLShaderWarmup.h` includes from application/shell code, and cast/`findChild`
  escapes. Quarantine entries are reported; `--strict` turns them into
  failures.
* CMake target `check_renderer_boundary` runs the script. CI can run it with
  `--strict` once the quarantine is empty.
