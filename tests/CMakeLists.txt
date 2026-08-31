# SPDX-License-Identifier: MPL-2.0

add_executable(RuwaTests
    ../src/features/brush/color/PigmentLut.cpp
    ../src/features/brush/color/PigmentModel.cpp
    ../src/features/fill/FloodFill.cpp
    ../src/features/fill/FillProgressivePolicy.cpp
    ../src/features/fill/FillRawTileOps.cpp
    ../src/features/layers/model/BlendModeUtils.cpp
    ../src/features/project/ProjectSerializer.cpp
    ../src/services/updates/SemanticVersion.cpp
    ../src/services/updates/UpdateManifest.cpp
    ../src/services/updates/UpdateSecurity.cpp
    ../src/features/transform/TransformSnapSolver.cpp
    ../src/features/transform/TransformSnapSession.cpp
    ../src/features/canvas/scene/Camera2D.cpp
    ../src/features/canvas/scene/Viewport.cpp
    ../src/features/canvas/clipboard/MergedSelectionExtractor.cpp
    ../src/shared/imaging/PixelSurface.cpp
    ../src/shell/docking/layout/DockLayoutCalculator.cpp
    ../src/shell/docking/layout/DockSplitNode.cpp
    camera_rotation_test.cpp
    merged_selection_extractor_test.cpp
    brush_click_dab_test.cpp
    brush_blend_mode_test.cpp
    dab_shape_falloff_test.cpp
    dock_split_push_drag_test.cpp
    flood_fill_alpha_lock_test.cpp
    fill_radius_preflight_test.cpp
    pigment_lut_test.cpp
    pigment_model_test.cpp
    project_serializer_test.cpp
    runtime_shader_catalog_test.cpp
    smoke_test.cpp
    stroke_input_queue_test.cpp
    taper_state_test.cpp
    transform_discrete_action_test.cpp
    transform_snap_solver_test.cpp
    texture_replacement_math_test.cpp
    update_manifest_test.cpp
    update_security_test.cpp
    update_semantic_version_test.cpp
    wet_pigment_glsl_test.cpp
    wet_pigment_gpu_layout_test.cpp
)

target_include_directories(RuwaTests PRIVATE ../src)
target_compile_definitions(RuwaTests PRIVATE
    RUWA_SOURCE_SHADER_DIR="${PROJECT_SOURCE_DIR}/src/shared/shaders"
)
# ProjectSerializer uses Qt Core; the TileBrush click-dab regression test also
# includes LayerData and therefore needs Qt Gui for QPixmap. No Widgets or moc.
target_link_libraries(RuwaTests PRIVATE Catch2::Catch2WithMain Qt6::Core Qt6::Gui)
if (WIN32)
    target_link_libraries(RuwaTests PRIVATE crypt32)
endif()

include(Catch)

# RuwaTests links Qt, so the executable needs the Qt DLLs (and the MinGW runtime
# DLLs) on PATH when catch_discover_tests runs it to enumerate tests. Qt
# Creator's build-step environment does not add Qt's bin dir to PATH, which
# made discovery fail with 0xc0000135 (DLL not found).
# Hand the DLL directories to Catch2's DL_PATHS instead of relying on PATH:
# the directory of the imported Qt6::Core target (its bin/) plus the compiler's
# own bin/ (libstdc++ / libgcc / libwinpthread).
get_filename_component(RUWA_MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
catch_discover_tests(RuwaTests
    DL_PATHS
        "$<TARGET_FILE_DIR:Qt6::Core>"
        "${RUWA_MINGW_BIN_DIR}"
)
