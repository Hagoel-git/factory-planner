#include "core/data/PngInfo.h"

#include "common/FilesystemUtils.h"

#include <array>
#include <cstring>
#include <fstream>

namespace gamedata {

namespace {

constexpr std::array<uint8_t, 8> kPngSignature = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
};

constexpr size_t kMinPngHeaderBytes = 33; // 8 sig + 4 len + 4 "IHDR" + 13 IHDR + 4 CRC

uint32_t readU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

uint32_t computeCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint32_t>(data[i]);
        for (int b = 0; b < 8; ++b) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

bool isValidColorAndBitDepth(uint8_t colourType, uint8_t bitDepth) {
    switch (colourType) {
        case 0: // Grayscale
            return bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8 || bitDepth == 16;
        case 2: // Truecolor (RGB)
            return bitDepth == 8 || bitDepth == 16;
        case 3: // Indexed-colour
            return bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8;
        case 4: // Grayscale with alpha
            return bitDepth == 8 || bitDepth == 16;
        case 6: // Truecolor with alpha (RGBA)
            return bitDepth == 8 || bitDepth == 16;
        default:
            return false;
    }
}

} // namespace

PngParseResult readPngInfoFromMemory(const uint8_t* data, size_t size) {
    PngParseResult res;
    if (data == nullptr || size < kMinPngHeaderBytes) {
        res.error = "file is smaller than minimum PNG header (33 bytes)";
        return res;
    }

    if (std::memcmp(data, kPngSignature.data(), kPngSignature.size()) != 0) {
        res.error = "invalid PNG signature";
        return res;
    }

    const uint32_t ihdrLen = readU32BE(data + 8);
    if (ihdrLen != 13) {
        res.error = "invalid IHDR chunk length";
        return res;
    }

    if (std::memcmp(data + 12, "IHDR", 4) != 0) {
        res.error = "first PNG chunk is not IHDR";
        return res;
    }

    const uint32_t width = readU32BE(data + 16);
    const uint32_t height = readU32BE(data + 20);
    if (width == 0 || height == 0) {
        res.error = "PNG has zero width or height";
        return res;
    }

    const uint8_t bitDepth = data[24];
    const uint8_t colourType = data[25];
    if (!isValidColorAndBitDepth(colourType, bitDepth)) {
        res.error = "invalid PNG colour type or bit depth";
        return res;
    }

    res.ok = true;
    res.info.width = width;
    res.info.height = height;
    res.info.bit_depth = bitDepth;
    res.info.colour_type = colourType;
    return res;
}

PngParseResult readPngInfoFromMemory(std::string_view bytes) {
    return readPngInfoFromMemory(
        reinterpret_cast<const uint8_t*>(bytes.data()),
        bytes.size());
}

PngParseResult readPngInfoFromFile(const std::filesystem::path& path) {
    PngParseResult res;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        res.error = "cannot open file '" + pathToUtf8(path) + "'";
        return res;
    }

    std::array<uint8_t, kMinPngHeaderBytes> buf{};
    ifs.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    const std::streamsize bytesRead = ifs.gcount();
    if (bytesRead < static_cast<std::streamsize>(kMinPngHeaderBytes)) {
        res.error = "file is smaller than minimum PNG header (33 bytes)";
        return res;
    }

    return readPngInfoFromMemory(buf.data(), buf.size());
}

bool validatePngStructure(
    const uint8_t* data,
    size_t size,
    PngInfo* outInfo,
    std::string* outError) {
    if (outError) outError->clear();

    PngParseResult header = readPngInfoFromMemory(data, size);
    if (!header.ok) {
        if (outError) *outError = header.error;
        return false;
    }

    size_t pos = 8;
    bool seenIend = false;
    while (pos < size) {
        if (size - pos < 12) {
            if (outError) *outError = "truncated PNG chunk header";
            return false;
        }
        const uint32_t chunkLen = readU32BE(data + pos);
        const uint8_t* chunkType = data + pos + 4;
        if (chunkLen > size - pos - 12) {
            if (outError) *outError = "truncated PNG chunk payload";
            return false;
        }

        const uint32_t expectedCrc = readU32BE(data + pos + 8 + chunkLen);
        const uint32_t actualCrc = computeCrc32(chunkType, static_cast<size_t>(chunkLen) + 4);
        if (actualCrc != expectedCrc) {
            if (outError) *outError = "PNG chunk CRC mismatch";
            return false;
        }

        const bool isIend = (std::memcmp(chunkType, "IEND", 4) == 0);
        pos += 12 + static_cast<size_t>(chunkLen);
        if (isIend) {
            seenIend = true;
            break;
        }
    }

    if (!seenIend) {
        if (outError) *outError = "missing IEND chunk";
        return false;
    }

    if (outInfo) {
        *outInfo = header.info;
    }
    return true;
}

} // namespace gamedata
