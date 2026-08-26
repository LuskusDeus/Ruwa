// SPDX-License-Identifier: MPL-2.0

#include "shared/rendering/RuntimeShaderCatalog.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <set>
#include <string>

TEST_CASE("Runtime shader catalog owns every shipped GLSL file", "[rendering][glsl]")
{
    std::set<std::string> catalogFiles;
    std::set<std::string> programDefinitions;

    for (const auto& program : aether::kRuntimeShaderPrograms) {
        REQUIRE_FALSE(program.name.empty());
        REQUIRE(programDefinitions.emplace(program.name).second);

        if (program.type == aether::RuntimeShaderProgramType::Graphics) {
            REQUIRE_FALSE(program.vertexShader.empty());
            REQUIRE_FALSE(program.fragmentShader.empty());
            REQUIRE(program.computeShader.empty());
            catalogFiles.emplace(program.vertexShader);
            catalogFiles.emplace(program.fragmentShader);
        } else {
            REQUIRE(program.vertexShader.empty());
            REQUIRE(program.fragmentShader.empty());
            REQUIRE_FALSE(program.computeShader.empty());
            catalogFiles.emplace(program.computeShader);
        }
    }

    std::set<std::string> shippedFiles;
    const std::filesystem::path shaderDirectory(RUWA_SOURCE_SHADER_DIR);
    REQUIRE(std::filesystem::is_directory(shaderDirectory));
    for (const auto& entry : std::filesystem::directory_iterator(shaderDirectory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".glsl") {
            shippedFiles.emplace(entry.path().filename().string());
        }
    }

    REQUIRE(shippedFiles == catalogFiles);
}
