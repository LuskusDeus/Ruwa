// SPDX-License-Identifier: MPL-2.0

#include "features/brush/color/PigmentLut.h"

#include <charconv>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr std::size_t kDefaultSize = 33;
constexpr std::size_t kMaximumSize = 65;

std::size_t parseSize(std::string_view text)
{
    std::size_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc {} || result.ptr != text.data() + text.size() || value < 2
        || value > kMaximumSize) {
        throw std::invalid_argument("LUT size must be an integer between 2 and 65");
    }
    return value;
}

void writeFile(const std::filesystem::path& path, std::span<const std::uint8_t> bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("Cannot open temporary LUT file for writing");
    output.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output)
        throw std::runtime_error("Failed while writing temporary LUT file");
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("Cannot reopen temporary LUT file");
    const auto length = input.tellg();
    if (length < 0)
        throw std::runtime_error("Cannot determine temporary LUT file size");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length));
    if (!input)
        throw std::runtime_error("Cannot verify temporary LUT file");
    return bytes;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const std::filesystem::path outputPath = argc >= 2
                ? std::filesystem::path(argv[1])
                : std::filesystem::path("resources/pigments/ruwa-pigments-v1.lut");
        const std::size_t size = argc >= 3 ? parseSize(argv[2]) : kDefaultSize;
        if (argc > 3) {
            std::cerr << "Usage: RuwaPigmentLutGenerator [output-path] [size]\n";
            return 2;
        }
        if (std::filesystem::exists(outputPath)) {
            std::cerr << "Refusing to overwrite existing LUT: " << outputPath << '\n';
            return 3;
        }

        const auto parent = outputPath.parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);
        const std::filesystem::path temporaryPath = outputPath.string() + ".tmp";
        std::error_code removeError;
        std::filesystem::remove(temporaryPath, removeError);

        std::cout << "Generating " << size << "x" << size << "x" << size << " pigment LUT...\n";
        const auto lut = ruwa::core::brushes::PigmentLut::generate(size);
        const auto bytes = lut.serialize();
        writeFile(temporaryPath, bytes);

        const auto verificationBytes = readFile(temporaryPath);
        const auto verified = ruwa::core::brushes::PigmentLut::deserialize(verificationBytes);
        if (verified.size() != size)
            throw std::runtime_error("Generated LUT verification returned a different size");

        std::filesystem::rename(temporaryPath, outputPath);
        std::cout << "Wrote " << bytes.size() << " bytes to " << outputPath << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Pigment LUT generation failed: " << error.what() << '\n';
        return 1;
    }
}
