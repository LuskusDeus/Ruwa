# SPDX-License-Identifier: MPL-2.0

# ============================================================================
# Static analysis integration (clang-tidy + clazy)
# ============================================================================
#
# This module wires optional static analysis into the build without making it a
# hard dependency. Nothing here runs unless a contributor opts in.
#
# Two entry points are provided:
#
#   1. In-build clang-tidy   -DRUWA_ENABLE_CLANG_TIDY=ON
#      Runs clang-tidy on every Ruwa translation unit as it compiles. Applied as
#      a property on the Ruwa target only, so fetched dependencies (QWindowKit)
#      and vendored code are never linted. Works with Clang/GCC/MinGW drivers;
#      skipped for MSVC.
#
#   2. Standalone targets     cmake --build <dir> --target clang-tidy
#                             cmake --build <dir> --target clazy
#      Run the analyzers over the generated compile_commands.json out of band
#      (via scripts/run-clang-tidy.ps1 / scripts/run-clazy.ps1). This is the
#      recommended workflow: it does not slow ordinary builds and matches what
#      CI's `static-analysis` job runs.
#
# compile_commands.json is always exported so IDEs (Qt Creator's Clang Tools)
# and the standalone tools can find the compilation database.

# The compilation database powers both the standalone runners and IDE tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json" FORCE)

option(RUWA_ENABLE_CLANG_TIDY "Run clang-tidy inline during the build" OFF)

# Qt Creator ships clang-tidy/clazy-standalone under its bundled LLVM. Search the
# PATH first, then fall back to that location so contributors do not have to add
# it to PATH by hand.
set(_ruwa_qtc_clang_hints
    "$ENV{ProgramFiles}/Qt/Tools/QtCreator/bin/clang/bin"
    "C:/Qt/Tools/QtCreator/bin/clang/bin"
    "/opt/qt/Tools/QtCreator/bin/clang/bin"
)

# `ruwa_register_static_analysis_targets(<target>)` applies inline clang-tidy to
# the given target (when enabled) and adds the out-of-band 'clang-tidy'/'clazy'
# convenience targets. Call it after the main target is defined so its build
# directory is known and the property lands on the right target.
function(ruwa_register_static_analysis_targets _main_target)
    # --- Inline clang-tidy, scoped to the main target only ------------------
    if (RUWA_ENABLE_CLANG_TIDY)
        if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
            message(WARNING
                "RUWA_ENABLE_CLANG_TIDY is ON but the compiler is MSVC. Inline "
                "clang-tidy is only wired for Clang/GCC drivers; use the "
                "'clang-tidy' target with a Clang/MinGW build instead. Skipping.")
        else()
            find_program(RUWA_CLANG_TIDY_EXE
                NAMES clang-tidy
                HINTS ${_ruwa_qtc_clang_hints}
                DOC "clang-tidy executable used for inline analysis")

            if (RUWA_CLANG_TIDY_EXE)
                message(STATUS "Inline clang-tidy enabled: ${RUWA_CLANG_TIDY_EXE}")
                # The .clang-tidy file supplies checks, header filter and format
                # style; --quiet keeps the build log readable.
                set_target_properties(${_main_target} PROPERTIES
                    CXX_CLANG_TIDY "${RUWA_CLANG_TIDY_EXE};--quiet;--use-color")
            else()
                message(WARNING
                    "RUWA_ENABLE_CLANG_TIDY is ON but clang-tidy was not found. "
                    "Set RUWA_CLANG_TIDY_EXE or add clang-tidy to PATH.")
            endif()
        endif()
    endif()

    # --- Out-of-band convenience targets ------------------------------------
    find_program(RUWA_POWERSHELL_EXE
        NAMES pwsh powershell
        DOC "PowerShell used to drive the static-analysis runner scripts")

    if (NOT RUWA_POWERSHELL_EXE)
        message(STATUS
            "PowerShell not found; 'clang-tidy'/'clazy' convenience targets are "
            "unavailable. Run scripts/run-clang-tidy.ps1 manually instead.")
        return()
    endif()

    set(_scripts_dir "${CMAKE_CURRENT_SOURCE_DIR}/scripts")

    add_custom_target(clang-tidy
        COMMAND "${RUWA_POWERSHELL_EXE}" -NoProfile -ExecutionPolicy Bypass
            -File "${_scripts_dir}/run-clang-tidy.ps1"
            -BuildDir "${CMAKE_BINARY_DIR}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Running clang-tidy over compile_commands.json"
        USES_TERMINAL
        VERBATIM)

    add_custom_target(clazy
        COMMAND "${RUWA_POWERSHELL_EXE}" -NoProfile -ExecutionPolicy Bypass
            -File "${_scripts_dir}/run-clazy.ps1"
            -BuildDir "${CMAKE_BINARY_DIR}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Running clazy-standalone over compile_commands.json"
        USES_TERMINAL
        VERBATIM)
endfunction()
