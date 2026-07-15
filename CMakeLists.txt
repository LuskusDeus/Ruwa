# SPDX-License-Identifier: MPL-2.0

cmake_minimum_required(VERSION 3.16)

project(Ruwa
    VERSION 0.2.4
    LANGUAGES CXX C
)

set(RUWA_VERSION_SUFFIX "alpha")
set(RUWA_APPLICATION_VERSION "${PROJECT_VERSION}")
if (NOT RUWA_VERSION_SUFFIX STREQUAL "")
    string(APPEND RUWA_APPLICATION_VERSION "-${RUWA_VERSION_SUFFIX}")
endif()

# ============================================================================
# C++ SETTINGS
# ============================================================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Suppress warnings about Qt private headers usage (required for QWindowKit)
set(QT_NO_PRIVATE_MODULE_WARNING ON)

# ============================================================================
# STATIC ANALYSIS (clang-tidy / clazy)
# ============================================================================
# Exports compile_commands.json and defines the RUWA_ENABLE_CLANG_TIDY option and
# the target-registration helper. Inline clang-tidy is attached to the Ruwa
# target itself (see ruwa_register_static_analysis_targets below), so fetched
# dependencies such as QWindowKit are never linted.

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(StaticAnalysis)

# ============================================================================
# DEPENDENCIES
# ============================================================================

include(FetchContent)

include(CTest)

if (BUILD_TESTING)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.8.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)
endif()

FetchContent_Declare(
    QWindowKit
    GIT_REPOSITORY https://github.com/stdware/qwindowkit.git
    GIT_TAG        1.5.0
)
FetchContent_MakeAvailable(QWindowKit)

# ============================================================================
# QT SETTINGS
# ============================================================================

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    OpenGL
    OpenGLWidgets
    Concurrent
    Network
    LinguistTools
)

# ============================================================================
# DISCORD RICH PRESENCE
# ============================================================================
#
# Rich Presence is implemented directly over Discord's local IPC socket in
# src/services/discord (Qt only, via Qt6::Network). It has no third-party SDK,
# no proprietary binary, and no external dependency, so it ships enabled in
# public builds. Set RUWA_ENABLE_DISCORD=OFF to compile it out. When Discord is
# not running the service simply stays idle and retries in the background.

option(RUWA_ENABLE_DISCORD "Enable Discord Rich Presence (Qt IPC, no external SDK)" ON)
set(RUWA_WITH_DISCORD ${RUWA_ENABLE_DISCORD})

# ============================================================================
# SOURCES
# ============================================================================

file(GLOB_RECURSE Ruwa_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(FILTER Ruwa_SOURCES EXCLUDE REGEX "/\\.claude/")

qt_add_resources(Ruwa_RESOURCES
    resources.qrc
)

# ============================================================================
# PLATFORM RESOURCES
# ============================================================================

if (WIN32)
    set(Ruwa_PLATFORM_RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
endif()

# ============================================================================
# EXECUTABLE
# ============================================================================

add_executable(Ruwa
    ${Ruwa_SOURCES}
    ${Ruwa_RESOURCES}
    ${Ruwa_PLATFORM_RESOURCES}
)

# Register the out-of-band 'clang-tidy' and 'clazy' convenience targets that run
# the analyzers over this build's compile_commands.json.
ruwa_register_static_analysis_targets(Ruwa)

# ============================================================================
# BUILD METADATA
# ============================================================================

set(RUWA_BUILD_INFO_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(RUWA_BUILD_INFO_HEADER "${RUWA_BUILD_INFO_DIR}/RuwaBuildInfo.h")
set(RUWA_BUILD_COUNTER_FILE "${CMAKE_CURRENT_BINARY_DIR}/RuwaBuildNumber.txt")
set(RUWA_RELEASE_DATE "2026-07-16" CACHE STRING "Release date shown in Ruwa About")
set(RUWA_BUILD_INFO_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/UpdateRuwaBuildInfo.cmake")
option(RUWA_ENABLE_UPDATES "Enable signed built-in update checks and patch downloads" ON)
set(RUWA_UPDATE_MANIFEST_URL
    "https://github.com/LuskusDeus/Ruwa-releases/releases/latest/download/latest.json"
    CACHE STRING "HTTPS URL of the signed Ruwa update manifest")
set(RUWA_UPDATE_ALLOWED_HOSTS
    "github.com;release-assets.githubusercontent.com"
    CACHE STRING "Exact HTTPS hosts allowed for update manifests, signatures and archives")
set(RUWA_UPDATE_SIGNER_CERT_SHA256
    ""
    CACHE STRING "SHA-256 fingerprint of the trusted DER update-signing certificate")
if (RUWA_UPDATE_SIGNER_CERT_SHA256 STREQUAL "")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/RuwaUpdateTrust.cmake" OPTIONAL)
endif()
set(RUWA_UPDATE_MAX_ARCHIVE_BYTES
    "67108864"
    CACHE STRING "Maximum accepted signed update archive size in bytes")

set(RUWA_ENABLE_UPDATES_VALUE 0)
if (RUWA_ENABLE_UPDATES)
    set(RUWA_ENABLE_UPDATES_VALUE 1)
endif()

file(MAKE_DIRECTORY "${RUWA_BUILD_INFO_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RuwaBuildConfig.h.in"
    "${RUWA_BUILD_INFO_DIR}/RuwaBuildConfig.h"
    @ONLY
)
file(GENERATE
    OUTPUT "${RUWA_BUILD_INFO_DIR}/RuwaVersion.txt"
    CONTENT "${RUWA_APPLICATION_VERSION}\n"
)

file(WRITE "${RUWA_BUILD_INFO_SCRIPT}" [=[
if (EXISTS "${COUNTER_FILE}")
    file(READ "${COUNTER_FILE}" CURRENT_BUILD_NUMBER)
    string(STRIP "${CURRENT_BUILD_NUMBER}" CURRENT_BUILD_NUMBER)
else()
    set(CURRENT_BUILD_NUMBER "0")
endif()

if (NOT CURRENT_BUILD_NUMBER MATCHES "^[0-9]+$")
    set(CURRENT_BUILD_NUMBER "0")
endif()

math(EXPR NEXT_BUILD_NUMBER "${CURRENT_BUILD_NUMBER} + 1")
file(WRITE "${COUNTER_FILE}" "${NEXT_BUILD_NUMBER}\n")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(WRITE "${OUTPUT_FILE}"
"#ifndef RUWA_BUILD_INFO_H\n"
"#define RUWA_BUILD_INFO_H\n"
"\n"
"#define RUWA_BUILD_NUMBER \"${NEXT_BUILD_NUMBER}\"\n"
"#define RUWA_RELEASE_DATE \"${RELEASE_DATE}\"\n"
"\n"
"#endif // RUWA_BUILD_INFO_H\n"
)
]=])

add_custom_target(RuwaBuildInfo
    COMMAND ${CMAKE_COMMAND}
        -DCOUNTER_FILE:FILEPATH=${RUWA_BUILD_COUNTER_FILE}
        -DOUTPUT_DIR:PATH=${RUWA_BUILD_INFO_DIR}
        -DOUTPUT_FILE:FILEPATH=${RUWA_BUILD_INFO_HEADER}
        -DRELEASE_DATE:STRING=${RUWA_RELEASE_DATE}
        -P "${RUWA_BUILD_INFO_SCRIPT}"
    BYPRODUCTS "${RUWA_BUILD_INFO_HEADER}"
    COMMENT "Updating Ruwa build metadata"
    VERBATIM
)

add_dependencies(Ruwa RuwaBuildInfo)

# ============================================================================
# INCLUDE PATHS
# ============================================================================

target_include_directories(Ruwa PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include
    ${RUWA_BUILD_INFO_DIR}
)

# ============================================================================
# LINK LIBRARIES
# ============================================================================

target_link_libraries(Ruwa PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Concurrent
    Qt6::Network
    QWindowKit::Widgets
)

if (WIN32)
    target_link_libraries(Ruwa PRIVATE ole32 advapi32 crypt32)
endif()

# ============================================================================
# SHADER DIRECTORY
# ============================================================================

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/shared/shaders")
target_compile_definitions(Ruwa PRIVATE
    SHADER_DIR="shaders"
    RUWA_WITH_DISCORD=$<BOOL:${RUWA_WITH_DISCORD}>
)

# Copy shaders to build directory for runtime access (at build time, not just configure time)
file(GLOB SHADER_FILES CONFIGURE_DEPENDS "${SHADER_SOURCE_DIR}/*.glsl")
add_custom_command(TARGET Ruwa POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SHADER_FILES} "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMENT "Copying shader files to build directory"
)

# ============================================================================
# TRANSLATIONS
# ============================================================================

set(TRANSLATIONS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/translations")
set(TS_FILES
    "${TRANSLATIONS_DIR}/ruwa_en.ts"
    "${TRANSLATIONS_DIR}/ruwa_ru.ts"
)

qt_add_lupdate(Ruwa TS_FILES ${TS_FILES})

qt_add_lrelease(Ruwa TS_FILES ${TS_FILES}
    QM_FILES_OUTPUT_VARIABLE QM_FILES
)

foreach(QM_FILE IN LISTS QM_FILES)
    get_filename_component(QM_FILE_NAME "${QM_FILE}" NAME)
    set_source_files_properties("${QM_FILE}"
        PROPERTIES QT_RESOURCE_ALIAS "${QM_FILE_NAME}"
    )
endforeach()

qt_add_resources(Ruwa RuwaTranslationResources
    PREFIX "/i18n"
    FILES ${QM_FILES}
)

# Ensure translations are built before the main target
add_dependencies(Ruwa Ruwa_lrelease)

# ============================================================================
# PLATFORM SETTINGS
# ============================================================================

if (WIN32)
    set_target_properties(Ruwa PROPERTIES
        WIN32_EXECUTABLE ON
    )
endif()

if (BUILD_TESTING)
    add_subdirectory(tests)
endif()

# ============================================================================
# STANDARD EFFECT PLUGINS
# ============================================================================
# Standard packages build as ordinary SDK plugins (sdk/include only, no Qt/src
# linkage) into the "effects" folder beside Ruwa.exe. Added after the Ruwa
# target so their output directory can resolve $<TARGET_FILE_DIR:Ruwa>.

add_subdirectory(plugins)

add_executable(RuwaPigmentLutGenerator EXCLUDE_FROM_ALL
    tools/pigment-lut-generator/main.cpp
    src/features/brush/color/PigmentLut.cpp
    src/features/brush/color/PigmentModel.cpp
)

target_include_directories(RuwaPigmentLutGenerator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if (MINGW)
    target_link_options(RuwaPigmentLutGenerator PRIVATE
        -static-libgcc
        -static-libstdc++
    )
endif()

# The runtime samples the serialized LUT, not PigmentModel::encode() directly.
# Keep the checked-in resource synchronized with the model automatically, while
# generating into a verified temporary file so a failed generator never damages
# the last valid resource. The OUTPUT dependency makes this run only after the
# model/LUT implementation changes, not on every normal application build.
set(RUWA_PIGMENT_LUT_RESOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/pigments/ruwa-pigments-v1.lut")
set(RUWA_PIGMENT_LUT_TEMP
    "${CMAKE_CURRENT_BINARY_DIR}/ruwa-pigments-v1.generated.lut")

add_custom_command(
    OUTPUT "${RUWA_PIGMENT_LUT_RESOURCE}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${RUWA_PIGMENT_LUT_TEMP}"
    COMMAND $<TARGET_FILE:RuwaPigmentLutGenerator> "${RUWA_PIGMENT_LUT_TEMP}" 33
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${RUWA_PIGMENT_LUT_TEMP}" "${RUWA_PIGMENT_LUT_RESOURCE}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${RUWA_PIGMENT_LUT_TEMP}"
    DEPENDS
        RuwaPigmentLutGenerator
        "${CMAKE_CURRENT_SOURCE_DIR}/src/features/brush/color/PigmentLut.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/features/brush/color/PigmentLut.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/features/brush/color/PigmentModel.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/features/brush/color/PigmentModel.h"
    COMMENT "Regenerating the built-in pigment LUT"
    VERBATIM
)

add_custom_target(RuwaPigmentLutResource DEPENDS "${RUWA_PIGMENT_LUT_RESOURCE}")
add_dependencies(Ruwa RuwaPigmentLutResource)
