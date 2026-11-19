#ifndef PNGINFO_H
#define PNGINFO_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace gamedata {

inline constexpr uint32_t kMaxIconDimension = 64;

struct PngInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t bit_depth = 0;
    uint8_t colour_type = 0;
};

struct PngParseResult {
    bool ok = false;
    PngInfo info;
    std::string error;
};

// Reads PNG signature and IHDR metadata (width, height, bit_depth, colour_type)
// without decoding pixel data.
PngParseResult readPngInfoFromMemory(const uint8_t* data, size_t size);
PngParseResult readPngInfoFromMemory(std::string_view bytes);
PngParseResult readPngInfoFromFile(const std::filesystem::path& path);

// Validates that an icon satisfies V-ICON-1 dimensions: non-zero, square, and <= 64x64.
inline bool isValidIconDimensions(uint32_t width, uint32_t height) {
    return width > 0 && height > 0 && width == height && width <= kMaxIconDimension;
}

// Validates PNG signature, IHDR, chunk framing, chunk CRC-32s, and trailing IEND chunk.
bool validatePngStructure(const uint8_t* data, size_t size, PngInfo* outInfo = nullptr, std::string* outError = nullptr);

} // namespace gamedata

#endif // PNGINFO_H
