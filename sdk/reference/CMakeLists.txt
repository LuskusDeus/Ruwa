# SPDX-License-Identifier: MPL-2.0

# ============================================================================
#   Ruwa Reference Effect Plugin — STANDALONE build
# ============================================================================
# This is a SELF-CONTAINED project, intentionally NOT part of the Ruwa build.
# Nothing in the repository's root CMakeLists.txt references it, so a normal
# application build never sees it. Its purpose is to prove — and let you verify
# on demand — that a plugin compiles against the public SDK headers ALONE.
#
# It sees only ../include (the SDK), links neither Qt nor any src/ code, and
# produces exactly <name>.dll with no import library, byte-for-byte the shape
# EffectPluginManager loads.
#
# Build it on its own, out-of-tree:
#
#     cmake -S sdk/reference -B build-reference
#     cmake --build build-reference
#
# Then drop the resulting reference_tint.dll into the "effects" folder the host
# scans (see README.md) and it appears in the effects picker under "Example".

cmake_minimum_required(VERSION 3.16)

project(RuwaReferenceEffect LANGUAGES C)

if (NOT CMAKE_C_STANDARD)
    set(CMAKE_C_STANDARD 99)          # designated initializers; matches the SDK
    set(CMAKE_C_STANDARD_REQUIRED ON)
endif()

# MODULE: loaded at runtime via the host's QLibrary, never linked against
# another target and producing no import library — same as every standard
# package.
add_library(reference_tint MODULE
    src/reference_tint.c
)

# The public SDK headers are the ONE and only include path. If this builds, the
# SDK is self-sufficient. `../include` resolves to <repo>/sdk/include whether
# this project is configured in place or copied elsewhere alongside the headers.
target_include_directories(reference_tint PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include"
)

# Exactly reference_tint.dll — no "lib" prefix — so the loader's *.dll filter
# picks it up unchanged.
set_target_properties(reference_tint PROPERTIES
    OUTPUT_NAME "reference_tint"
    PREFIX ""
)

# On MinGW fold the C runtime into the DLL so it carries no libgcc load-order
# dependency next to the executable — matching the standard packages.
if (MINGW)
    target_link_options(reference_tint PRIVATE -static-libgcc)
endif()
