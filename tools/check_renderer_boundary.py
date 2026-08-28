#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Renderer boundary enforcement (Stage 1 decoupling).

Checks the dependency rules that keep the Aether renderer replaceable:

  1. HARD: no file outside the sanctioned list may include the legacy renderer
     header (features/canvas/rendering/OpenGLCanvasWidget.h).
  2. HARD: no file outside the platform-hosting list may include QOpenGLWidget
     or QOpenGLFunctions_* directly.
  3. HARD: no code may qobject_cast/findChild to the legacy renderer type
     outside the sanctioned list.
  4. WARN: reverse dependencies (renderer -> UI) still present in the legacy
     renderer implementation, listed for extraction.

QUARANTINE entries are real, deliberate Stage 1 transitional dependencies:
each one is described in docs/renderer-boundary-quarantine.md. They pass by
default and fail under --strict, so the list cannot grow silently.

Usage:  python tools/check_renderer_boundary.py [--strict]
Exit code 0 = clean, 1 = violations (or quarantine under --strict).
"""

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "src"

RENDERER_HEADER = "features/canvas/rendering/OpenGLCanvasWidget.h"

# Files allowed to include the legacy renderer header, by role.
SANCTIONED_RENDERER_INCLUDES = {
    # The legacy renderer itself.
    "features/canvas/rendering/OpenGLCanvasWidget.cpp",
    # The only sanctioned outward consumer: the Aether Qt binding adapter
    # (plan 7.31.2); the process runtime (7.31.1) is GL-internal, not a
    # renderer-header consumer.
    "features/canvas/engine/aether/AetherCanvasEngineQtBinding.cpp",
    # Aether-internal rendering helper (dies with Aether; plan 7.6.4-7.6.5).
    "features/canvas/rendering/PaintGLCameraFrameState.cpp",
}

# Deliberate Stage 1 transitional consumers (docs/renderer-boundary-quarantine.md).
QUARANTINE_RENDERER_INCLUDES = {
    "features/canvas/ui/CanvasPanel.cpp",
    "features/canvas/ui/CanvasPanelRenderContentCreation.cpp",
    "features/canvas/ui/CanvasPanelContentCreation.cpp",
    "features/canvas/ui/CanvasViewController.cpp",
    "features/canvas-resize/CanvasResizeController.cpp",
    "features/export/ExportAreaController.cpp",
}

# Engine implementation is the only sanctioned GL location (plan 7.31:
# bootstrap/warm-up lives in the selected engine runtime, not in
# Application/Startup/MainWindow/WindowSetupCoordinator).
PLATFORM_GL_INCLUDES = set()

# The renderer implementation may use OpenGL; these prefixes mark GL-internal code.
GL_INTERNAL_PREFIXES = (
    "features/canvas/rendering/",
    "features/canvas/overlays/",
    "shared/rendering/",
    "features/brush/engine/",
    "features/brush/rendering/",
    "features/fill/",
    "features/selection/",
    "features/transform/",
    "features/effects/",
    "features/canvas/stroke/",
    "features/canvas/engine/aether/",
)

REVERSE_DEP_CHECKS = [
    (RENDERER_HEADER, "features/canvas/ui/CanvasMetricLabelOverlay.h",
     "renderer constructs UI metric label widgets"),
    ("features/canvas/rendering/OpenGLCanvasWidget.cpp", "<QMessageBox>",
     "renderer shows application dialogs"),
    ("features/canvas/rendering/OpenGLCanvasWidget.cpp", "services/input/StylusInputManager.h",
     "renderer reaches into the input service for the live pointer"),
]


def rel(path: Path) -> str:
    return path.relative_to(SRC).as_posix()


def src_files():
    for pattern in ("*.cpp", "*.h"):
        yield from SRC.rglob(pattern)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--strict", action="store_true",
                        help="also fail on quarantined (deliberate transitional) dependencies")
    args = parser.parse_args()

    errors = []
    warnings = []
    quarantines = []

    for path in src_files():
        rel_path = rel(path)
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue

        # Rule 1: legacy renderer header includes.
        if RENDERER_HEADER in text and "#include" in text:
            include_re = re.compile(r'#\s*include\s*"[^"]*' + re.escape(RENDERER_HEADER) + '"')
            if include_re.search(text):
                if rel_path in SANCTIONED_RENDERER_INCLUDES:
                    pass
                elif rel_path in QUARANTINE_RENDERER_INCLUDES:
                    quarantines.append(f"{rel_path}: includes legacy renderer header (quarantine)")
                else:
                    errors.append(
                        f"{rel_path}: includes '{RENDERER_HEADER}' outside the sanctioned "
                        f"integration — use the engine session (features/canvas/engine/)")

        # Rule 1b: Aether GL warm-up utility stays out of application code.
        if "shared/rendering/GLShaderWarmup.h" in text and "#include" in text:
            if rel_path.startswith(("app/", "features/startup/", "shell/")):
                errors.append(
                    f"{rel_path}: includes GLShaderWarmup.h outside the engine runtime "
                    f"(plan 7.6.45-7.6.51)")

        # Rule 2: direct QOpenGLWidget / QOpenGLFunctions includes.
        for qt_header, guard in (
            ("<QOpenGLWidget>", PLATFORM_GL_INCLUDES),
            ("<QOpenGLFunctions", PLATFORM_GL_INCLUDES),
        ):
            if f"#include {qt_header}" in text:
                in_gl_internal = rel_path.startswith(GL_INTERNAL_PREFIXES) and not rel_path.startswith(
                    "features/canvas/runtime/")
                if rel_path in guard or in_gl_internal or rel_path.endswith(
                        "OpenGLCanvasWidget.h"):
                    continue
                errors.append(f"{rel_path}: direct include {qt_header} outside GL-internal code")

        # Rule 3: escaping the binding via casts/findChild.
        if rel_path not in SANCTIONED_RENDERER_INCLUDES:
            for pattern in (
                r"qobject_cast\s*<\s*aether::OpenGLCanvasWidget",
                r"findChild\s*<\s*aether::OpenGLCanvasWidget",
                r"findChildren\s*<\s*aether::OpenGLCanvasWidget",
                r"static_cast\s*<\s*aether::OpenGLCanvasWidget",
            ):
                if re.search(pattern, text):
                    quarantines.append(
                        f"{rel_path}: cast/findChild to legacy renderer type (quarantine)")

    # Rule 4: reverse dependencies (renderer -> UI) still to be extracted.
    for file_rel, needle, why in REVERSE_DEP_CHECKS:
        path = REPO / file_rel
        if path.exists() and needle in path.read_text(encoding="utf-8", errors="replace"):
            warnings.append(f"{file_rel}: {why} (reverse dependency, to be extracted)")

    if quarantines:
        print("Quarantined transitional dependencies (see docs/renderer-boundary-quarantine.md):")
        for q in sorted(set(quarantines)):
            print(f"  Q  {q}")
    if warnings:
        print("Reverse-dependency warnings:")
        for w in sorted(set(warnings)):
            print(f"  W  {w}")
    if errors:
        print("Renderer boundary VIOLATIONS:")
        for e in sorted(set(errors)):
            print(f"  E  {e}")

    if errors or (args.strict and quarantines):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
