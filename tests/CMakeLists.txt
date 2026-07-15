# SPDX-License-Identifier: MPL-2.0

add_executable(RuwaTests
    ../src/features/brush/color/PigmentLut.cpp
    ../src/features/brush/color/PigmentModel.cpp
    ../src/features/project/ProjectSerializer.cpp
    ../src/services/updates/SemanticVersion.cpp
    ../src/services/updates/UpdateManifest.cpp
    ../src/services/updates/UpdateSecurity.cpp
    pigment_lut_test.cpp
    pigment_model_test.cpp
    project_serializer_test.cpp
    smoke_test.cpp
    update_manifest_test.cpp
    update_security_test.cpp
    update_semantic_version_test.cpp
    wet_pigment_glsl_test.cpp
    wet_pigment_gpu_layout_test.cpp
)

target_include_directories(RuwaTests PRIVATE ../src)
# ProjectSerializer (and its untrusted-input tests) pull in Qt Core only —
# QDataStream / QByteArray / QUuid / QSize etc. No Gui/Widgets, no moc.
target_link_libraries(RuwaTests PRIVATE Catch2::Catch2WithMain Qt6::Core)
if (WIN32)
    target_link_libraries(RuwaTests PRIVATE crypt32)
endif()

include(Catch)

# RuwaTests now links Qt6::Core, so the executable needs Qt6Core.dll (and the
# MinGW runtime DLLs) on PATH when catch_discover_tests runs it to enumerate
# tests at build time. Qt Creator's build-step environment does not add Qt's
# bin dir to PATH, which made discovery fail with 0xc0000135 (DLL not found).
# Hand the DLL directories to Catch2's DL_PATHS instead of relying on PATH:
# the directory of the imported Qt6::Core target (its bin/) plus the compiler's
# own bin/ (libstdc++ / libgcc / libwinpthread).
get_filename_component(RUWA_MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
catch_discover_tests(RuwaTests
    DL_PATHS
        "$<TARGET_FILE_DIR:Qt6::Core>"
        "${RUWA_MINGW_BIN_DIR}"
)
