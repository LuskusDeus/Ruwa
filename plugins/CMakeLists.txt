# SPDX-License-Identifier: MPL-2.0

# ============================================================================
#   STANDARD EFFECT PLUGINS
# ============================================================================
# Each standard package (Color, Blur, Distort, Stylize, Texture) is an ordinary
# SDK plugin, on exactly the same footing as any third-party effect DLL: a plain
# module that sees ONLY sdk/include, links neither Qt nor any src/ code, and is
# dropped into the "effects" folder beside Ruwa.exe, where EffectPluginManager
# discovers it. No standard package receives a privileged registration path.

# ruwa_add_effect_plugin(<target> <output-name> <sources...>)
#   Defines a self-contained C ABI v1 effect plugin DLL.
function(ruwa_add_effect_plugin target output_name)
    # MODULE: loaded at runtime via QLibrary, never linked against another
    # target and producing no import library.
    add_library(${target} MODULE ${ARGN})

    # The public SDK headers are the ONLY include path: no src/, no Qt, no
    # generated build headers. This is what proves the SDK is self-sufficient.
    target_include_directories(${target} PRIVATE
        "${CMAKE_SOURCE_DIR}/sdk/include"
    )

    # A plugin links no Qt and no Ruwa code. Fold the C/C++ runtime into the DLL
    # on MinGW so it carries no libgcc/libstdc++ load-order dependency next to
    # the executable.
    if (MINGW)
        target_link_options(${target} PRIVATE -static-libgcc -static-libstdc++)
    endif()

    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${output_name}"
        PREFIX ""                                     # exactly <name>.dll, no "lib"
        RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:Ruwa>/effects"
        LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:Ruwa>/effects"
    )

    # Build alongside the executable and after it, so the exe directory (and thus
    # its "effects" subfolder that the loader scans) already exists.
    add_dependencies(${target} Ruwa)
endfunction()

add_subdirectory(standard/color)
add_subdirectory(standard/distort)
add_subdirectory(standard/blur)
add_subdirectory(standard/stylize)
add_subdirectory(standard/texture)
