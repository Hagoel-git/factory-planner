#include <gtest/gtest.h>

#include "common/FilesystemUtils.h"
#include "core/data/IconPak.h"
#include "core/data/PngInfo.h"
#include "tools/gamedata_tool/Commands.h"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace gamedata_tool {
namespace {

class TempDirGuard {
public:
    explicit TempDirGuard(const std::string& prefix) {
        static std::atomic<uint64_t> counter{0};
        static const uint64_t processSalt = []() {
            std::random_device rd;
            uint64_t r = (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
            uint64_t t = static_cast<uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count());
            return r ^ t;
        }();
        const uint64_t id = ++counter;
        m_path = std::filesystem::temp_directory_path() /
                 ("fp_icons_test_" + prefix + "_" +
                  std::to_string(processSalt) + "_" + std::to_string(id));
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
        std::filesystem::create_directories(m_path, ec);
    }

    ~TempDirGuard() {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

struct CliRunResult {
    int code = 0;
    std::string out;
    std::string err;
};

CliRunResult runCli(const std::vector<std::string>& args) {
    std::ostringstream out;
    std::ostringstream err;
    CliRunResult r;
    r.code = runCommand(args, out, err);
    r.out = out.str();
    r.err = err.str();
    return r;
}

void appendU32BE(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    buf.push_back(static_cast<uint8_t>(v & 0xFFu));
}

void writeU32LE(std::vector<uint8_t>& buf, size_t offset, uint32_t v) {
    buf[offset + 0] = static_cast<uint8_t>(v & 0xFFu);
    buf[offset + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    buf[offset + 2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    buf[offset + 3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

void writeU64LE(std::vector<uint8_t>& buf, size_t offset, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        buf[offset + static_cast<size_t>(i)] = static_cast<uint8_t>((v >> (8 * i)) & 0xFFu);
    }
}

uint32_t computeCrc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint32_t>(data[i]);
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t computeAdler32(const uint8_t* data, size_t len) {
    uint32_t s1 = 1;
    uint32_t s2 = 0;
    for (size_t i = 0; i < len; ++i) {
        s1 = (s1 + data[i]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    return (s2 << 16) | s1;
}

void appendPngChunk(std::vector<uint8_t>& png, const char type[4], const std::vector<uint8_t>& payload) {
    appendU32BE(png, static_cast<uint32_t>(payload.size()));
    const size_t crcStart = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), payload.begin(), payload.end());
    const uint32_t crc = computeCrc32(png.data() + crcStart, png.size() - crcStart);
    appendU32BE(png, crc);
}

// Creates a valid, deterministic PNG of arbitrary dimensions (w x h) and color.
std::vector<uint8_t> makeTestPng(
    uint32_t w,
    uint32_t h,
    uint8_t r = 100,
    uint8_t g = 150,
    uint8_t b = 200,
    uint8_t colourType = 6,
    uint8_t bitDepth = 8) {
    std::vector<uint8_t> png;
    static constexpr std::array<uint8_t, 8> kSig = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
    };
    png.insert(png.end(), kSig.begin(), kSig.end());

    std::vector<uint8_t> ihdr;
    appendU32BE(ihdr, w);
    appendU32BE(ihdr, h);
    ihdr.push_back(bitDepth);
    ihdr.push_back(colourType);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    appendPngChunk(png, "IHDR", ihdr);

    if (colourType == 3) {
        // Minimal 1-entry PLTE for indexed colour type 3
        appendPngChunk(png, "PLTE", {r, g, b});
    }

    const uint32_t bpp = (colourType == 6) ? 4 : ((colourType == 2) ? 3 : 1);
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(h) * (1 + static_cast<size_t>(w) * bpp));
    for (uint32_t y = 0; y < h; ++y) {
        raw.push_back(0);
        for (uint32_t x = 0; x < w; ++x) {
            if (colourType == 6) {
                raw.push_back(r);
                raw.push_back(g);
                raw.push_back(b);
                raw.push_back(255);
            } else if (colourType == 2) {
                raw.push_back(r);
                raw.push_back(g);
                raw.push_back(b);
            } else {
                raw.push_back(r);
            }
        }
    }

    // Stored DEFLATE block (valid for up to 65535 raw bytes; split if larger)
    std::vector<uint8_t> zlib;
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    size_t offset = 0;
    while (offset < raw.size()) {
        const size_t chunk = std::min<size_t>(raw.size() - offset, 65535);
        const bool isFinal = (offset + chunk == raw.size());
        zlib.push_back(isFinal ? 0x01 : 0x00);
        const uint16_t len = static_cast<uint16_t>(chunk);
        const uint16_t nlen = static_cast<uint16_t>(~len);
        zlib.push_back(static_cast<uint8_t>(len & 0xFF));
        zlib.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        zlib.push_back(static_cast<uint8_t>(nlen & 0xFF));
        zlib.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
        offset += chunk;
    }
    const uint32_t adler = computeAdler32(raw.data(), raw.size());
    appendU32BE(zlib, adler);

    appendPngChunk(png, "IDAT", zlib);
    appendPngChunk(png, "IEND", {});
    return png;
}

std::string bytesToString(const std::vector<uint8_t>& b) {
    return std::string(reinterpret_cast<const char*>(b.data()), b.size());
}

std::vector<uint8_t> readFileBytes(const std::filesystem::path& p) {
    std::ifstream ifs(p, std::ios::binary);
    std::vector<uint8_t> out((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return out;
}

void writeFileBytes(const std::filesystem::path& p, const std::vector<uint8_t>& bytes) {
    ASSERT_TRUE(atomicWriteFile(p, bytesToString(bytes)));
}

std::filesystem::path writeMinimalTestPackage(
    const std::filesystem::path& dir,
    const std::string& iconRoot = "") {
    nlohmann::json j = {
        {"schema_version", 4},
        {"game", {{"id", "icon-test"}, {"name", "Icon Test"}}},
        {"units", {{"recipe_time", "s"}, {"default_rate", "s"}}},
        {"option_sets", {
            {"tiers", {
                {"options", {
                    {{"key", "lv"}, {"label", "Low Voltage"}}
                }}
            }}
        }},
        {"parameters", {
            {"tier", {{"type", "enum"}, {"option_set", "tiers"}, {"default", "lv"}}}
        }},
        {"profiles", {
            {"p", {{"params", {"tier"}}}}
        }},
        {"resources", {
            {{"id", "iron-ore"}, {"name", "Iron Ore"}},
            {{"id", "copper-plate"}, {"name", "Copper Plate"}}
        }},
        {"machines", {
            {{"id", "arc-furnace"}, {"name", "Arc Furnace + MK1"}, {"profile", "p"}, {"categories", {"smelt"}}}
        }},
        {"recipes", {
            {
                {"id", "r1"},
                {"category", "smelt"},
                {"time", 1},
                {"in", nlohmann::json::array({nlohmann::json::array({"iron-ore", 1})})},
                {"out", nlohmann::json::array({nlohmann::json::array({"copper-plate", 1})})}
            }
        }}
    };
    if (!iconRoot.empty()) {
        j["icon_root"] = iconRoot;
    }
    EXPECT_TRUE(atomicWriteFile(dir / "game.json", j.dump(2) + "\n"));
    return dir;
}

// ============================================================================
// 1. PngInfo tests (§9: valid, non-square, oversize, non-PNG)
// ============================================================================

TEST(PngInfoTest, ParsesValidSquareAndBoundaryDimensions) {
    for (uint32_t sz : {1u, 16u, 32u, 64u}) {
        const auto png = makeTestPng(sz, sz, 10, 20, 30, 6, 8);
        const auto res = gamedata::readPngInfoFromMemory(png.data(), png.size());
        ASSERT_TRUE(res.ok) << res.error;
        EXPECT_EQ(res.info.width, sz);
        EXPECT_EQ(res.info.height, sz);
        EXPECT_EQ(res.info.bit_depth, 8);
        EXPECT_EQ(res.info.colour_type, 6);
        EXPECT_TRUE(gamedata::isValidIconDimensions(res.info.width, res.info.height));
        EXPECT_TRUE(gamedata::validatePngStructure(png.data(), png.size()));
    }

    // Also verify RGB (colour_type=2), Grayscale (0), and Indexed (3)
    for (uint8_t ct : {uint8_t(0), uint8_t(2), uint8_t(3)}) {
        const auto png = makeTestPng(16, 16, 40, 50, 60, ct, 8);
        const auto res = gamedata::readPngInfoFromMemory(png.data(), png.size());
        ASSERT_TRUE(res.ok) << res.error;
        EXPECT_EQ(res.info.colour_type, ct);
    }
}

TEST(PngInfoTest, DetectsNonSquareAndOversizeDimensions) {
    const auto nonSquare = makeTestPng(32, 16);
    const auto nsRes = gamedata::readPngInfoFromMemory(nonSquare.data(), nonSquare.size());
    ASSERT_TRUE(nsRes.ok) << nsRes.error;
    EXPECT_EQ(nsRes.info.width, 32u);
    EXPECT_EQ(nsRes.info.height, 16u);
    EXPECT_FALSE(gamedata::isValidIconDimensions(nsRes.info.width, nsRes.info.height));

    const auto oversize65 = makeTestPng(65, 65);
    const auto osRes = gamedata::readPngInfoFromMemory(oversize65.data(), oversize65.size());
    ASSERT_TRUE(osRes.ok) << osRes.error;
    EXPECT_EQ(osRes.info.width, 65u);
    EXPECT_EQ(osRes.info.height, 65u);
    EXPECT_FALSE(gamedata::isValidIconDimensions(osRes.info.width, osRes.info.height));

    const auto oversize128 = makeTestPng(128, 96);
    const auto os2Res = gamedata::readPngInfoFromMemory(oversize128.data(), oversize128.size());
    ASSERT_TRUE(os2Res.ok) << os2Res.error;
    EXPECT_FALSE(gamedata::isValidIconDimensions(os2Res.info.width, os2Res.info.height));
}

TEST(PngInfoTest, RejectsNonPngAndCorruptHeaders) {
    const std::string text = "not a png file at all, just some arbitrary ASCII text";
    EXPECT_FALSE(gamedata::readPngInfoFromMemory(text).ok);

    // JPEG header
    const std::vector<uint8_t> jpeg(40, 0xFF);
    EXPECT_FALSE(gamedata::readPngInfoFromMemory(jpeg.data(), jpeg.size()).ok);

    // Truncated (< 33 bytes)
    const auto valid = makeTestPng(16, 16);
    EXPECT_FALSE(gamedata::readPngInfoFromMemory(valid.data(), 20).ok);

    // Non-IHDR first chunk
    auto badChunk = valid;
    badChunk[12] = 'X';
    EXPECT_FALSE(gamedata::readPngInfoFromMemory(badChunk.data(), badChunk.size()).ok);

    // Zero width
    auto zeroW = valid;
    zeroW[16] = 0; zeroW[17] = 0; zeroW[18] = 0; zeroW[19] = 0;
    EXPECT_FALSE(gamedata::readPngInfoFromMemory(zeroW.data(), zeroW.size()).ok);

    // File helper on valid and missing paths
    TempDirGuard tmp("pnginfo_file");
    const auto validPath = tmp.path() / "ok.png";
    writeFileBytes(validPath, valid);
    const auto fileOk = gamedata::readPngInfoFromFile(validPath);
    ASSERT_TRUE(fileOk.ok) << fileOk.error;
    EXPECT_EQ(fileOk.info.width, 16u);
    EXPECT_EQ(fileOk.info.height, 16u);

    const auto missingRes = gamedata::readPngInfoFromFile(tmp.path() / "missing.png");
    EXPECT_FALSE(missingRes.ok);
}

// ============================================================================
// 2. IconPak tests (§9: round trip, corrupt header/index/blob, dup keys, truncated)
// ============================================================================

TEST(IconPakTest, RoundTripSortedWithNameSlugAndEmptyPak) {
    TempDirGuard tmp("iconpak_rt");
    const std::filesystem::path pakPath = tmp.path() / "icons.pak";

    const auto pngA = makeTestPng(16, 16, 10, 20, 30);
    const auto pngB = makeTestPng(32, 32, 40, 50, 60);
    const auto pngC = makeTestPng(64, 64, 70, 80, 90);

    // Intentionally insert unsorted keys to verify writer sorts deterministically by key bytes
    std::vector<gamedata::IconPakInput> inputs = {
        {"resources/iron-ore", "iron-ore", {}, pngB, 32, 32},
        {"options/tiers/lv", "", {}, pngC, 64, 64},
        {"machines/arc-furnace", "arc-furnace-plus-mk1", {}, pngA, 16, 16}
    };

    std::string err;
    ASSERT_TRUE(gamedata::IconPakWriter::write(pakPath, inputs, &err)) << err;

    gamedata::IconPakReader reader;
    ASSERT_TRUE(gamedata::IconPakReader::open(pakPath, reader, &err)) << err;
    EXPECT_EQ(reader.version(), 1u);
    ASSERT_EQ(reader.size(), 3u);

    // Sorted order: machines/... < options/... < resources/...
    EXPECT_EQ(reader.entries()[0].key, "machines/arc-furnace");
    EXPECT_EQ(reader.entries()[0].name_slug, "arc-furnace-plus-mk1");
    EXPECT_EQ(reader.entries()[0].width, 16u);
    EXPECT_EQ(reader.entries()[0].height, 16u);

    EXPECT_EQ(reader.entries()[1].key, "options/tiers/lv");
    EXPECT_EQ(reader.entries()[1].name_slug, "");
    EXPECT_EQ(reader.entries()[1].width, 64u);
    EXPECT_EQ(reader.entries()[1].height, 64u);

    EXPECT_EQ(reader.entries()[2].key, "resources/iron-ore");
    EXPECT_EQ(reader.entries()[2].name_slug, "iron-ore");
    EXPECT_EQ(reader.entries()[2].width, 32u);
    EXPECT_EQ(reader.entries()[2].height, 32u);

    EXPECT_TRUE(reader.contains("resources/iron-ore"));
    EXPECT_FALSE(reader.contains("resources/nonexistent"));

    auto blobA = reader.readBlob("machines/arc-furnace");
    ASSERT_TRUE(blobA.has_value());
    EXPECT_EQ(*blobA, pngA);

    auto blobB = reader.readBlob("resources/iron-ore");
    ASSERT_TRUE(blobB.has_value());
    EXPECT_EQ(*blobB, pngB);

    // Reusable per-thread stream
    std::ifstream stream = reader.openStream();
    ASSERT_TRUE(stream.is_open());
    const auto* entryC = reader.find("options/tiers/lv");
    ASSERT_NE(entryC, nullptr);
    auto blobC = reader.readBlob(*entryC, stream);
    ASSERT_TRUE(blobC.has_value());
    EXPECT_EQ(*blobC, pngC);

    // Empty pak (0 entries) round trip
    const std::filesystem::path emptyPakPath = tmp.path() / "empty.pak";
    ASSERT_TRUE(gamedata::IconPakWriter::write(emptyPakPath, {}, &err)) << err;
    gamedata::IconPakReader emptyReader;
    ASSERT_TRUE(gamedata::IconPakReader::open(emptyPakPath, emptyReader, &err)) << err;
    EXPECT_TRUE(emptyReader.empty());
    EXPECT_EQ(emptyReader.size(), 0u);
}

TEST(IconPakTest, RejectsCorruptHeaderCorruptIndexDuplicateKeysAndTruncatedFile) {
    TempDirGuard tmp("iconpak_corrupt");
    const std::filesystem::path validPath = tmp.path() / "valid.pak";
    const auto png = makeTestPng(16, 16, 12, 34, 56);

    std::string err;
    ASSERT_TRUE(gamedata::IconPakWriter::write(
        validPath,
        {
            {"machines/smelter", "smelter", {}, png, 16, 16},
            {"resources/iron", "iron", {}, png, 16, 16}
        },
        &err)) << err;

    const std::vector<uint8_t> goodBytes = readFileBytes(validPath);
    ASSERT_GT(goodBytes.size(), 64u);

    auto expectOpenFails = [&](const std::string& label, const std::vector<uint8_t>& data) {
        const auto p = tmp.path() / (label + ".pak");
        writeFileBytes(p, data);
        gamedata::IconPakReader r;
        std::string e;
        EXPECT_FALSE(gamedata::IconPakReader::open(p, r, &e)) << "Expected failure for " << label;
    };

    // 1. Corrupt magic
    {
        auto b = goodBytes;
        b[0] = 'X';
        expectOpenFails("bad_magic", b);
    }

    // 2. Bad version
    {
        auto b = goodBytes;
        writeU32LE(b, 8, 99);
        expectOpenFails("bad_version", b);
    }

    // 3. index_offset < 32
    {
        auto b = goodBytes;
        writeU64LE(b, 16, 16);
        expectOpenFails("index_offset_in_header", b);
    }

    // 4. index_offset past EOF
    {
        auto b = goodBytes;
        writeU64LE(b, 16, b.size() + 100);
        expectOpenFails("index_past_eof", b);
    }

    // 5. index_size extends past EOF
    {
        auto b = goodBytes;
        writeU64LE(b, 24, 999999);
        expectOpenFails("index_size_past_eof", b);
    }

    // 6. Truncated header (< 32 bytes)
    {
        std::vector<uint8_t> b(goodBytes.begin(), goodBytes.begin() + 20);
        expectOpenFails("truncated_header", b);
    }

    // 7. Truncated mid-file
    {
        std::vector<uint8_t> b(goodBytes.begin(), goodBytes.begin() + static_cast<std::ptrdiff_t>(goodBytes.size() - 5));
        expectOpenFails("truncated_index", b);
    }

    // 8. Corrupt index: offset + size beyond data section (pointing into index)
    {
        gamedata::IconPakReader r;
        ASSERT_TRUE(gamedata::IconPakReader::open(validPath, r, &err));
        const uint64_t idxOff = 32 + png.size() * 2;
        auto b = goodBytes;
        // Entry 0 key is "machines/smelter" (16B), name_slug is "smelter" (7B).
        // Offset field sits at idxOff + 2 + 16 + 2 + 7 = idxOff + 27.
        writeU64LE(b, static_cast<size_t>(idxOff + 27), idxOff);
        expectOpenFails("blob_past_data_section", b);
    }

    // 9. Corrupt index: trailing garbage bytes in index
    {
        auto b = goodBytes;
        const uint64_t idxSize = static_cast<uint64_t>(b.size()) - (32 + png.size() * 2);
        b.push_back(0x00);
        writeU64LE(b, 24, idxSize + 1);
        expectOpenFails("trailing_index_bytes", b);
    }

    // 10. Duplicate keys: rejected by writer and reader
    {
        std::string werr;
        EXPECT_FALSE(gamedata::IconPakWriter::write(
            tmp.path() / "dup_writer.pak",
            {
                {"resources/dup", "dup", {}, png, 16, 16},
                {"resources/dup", "dup", {}, png, 16, 16}
            },
            &werr));

        // Hand-craft pak with two identical keys "resources/same"
        std::vector<uint8_t> dupPak;
        dupPak.resize(32, 0);
        dupPak.insert(dupPak.end(), png.begin(), png.end());
        const uint64_t idxOff = dupPak.size();
        auto appendEntry = [&](const std::string& k) {
            dupPak.push_back(static_cast<uint8_t>(k.size()));
            dupPak.push_back(0);
            dupPak.insert(dupPak.end(), k.begin(), k.end());
            dupPak.push_back(0);
            dupPak.push_back(0); // empty name_slug
            const size_t offPos = dupPak.size();
            dupPak.resize(dupPak.size() + 16, 0);
            writeU64LE(dupPak, offPos, 32);
            writeU32LE(dupPak, offPos + 8, static_cast<uint32_t>(png.size()));
            dupPak[offPos + 12] = 16;
            dupPak[offPos + 14] = 16;
        };
        appendEntry("resources/same");
        appendEntry("resources/same");
        const uint64_t idxSize = dupPak.size() - idxOff;
        std::memcpy(dupPak.data(), gamedata::kIconPakMagic, 8);
        writeU32LE(dupPak, 8, 1);
        writeU32LE(dupPak, 12, 2);
        writeU64LE(dupPak, 16, idxOff);
        writeU64LE(dupPak, 24, idxSize);
        expectOpenFails("duplicate_keys_reader", dupPak);
    }
}

TEST(IconPakTest, CorruptBlobMakesOnlyThatEntryMissing) {
    TempDirGuard tmp("iconpak_corrupt_blob");
    const std::filesystem::path pakPath = tmp.path() / "blob_corrupt.pak";

    const auto png0 = makeTestPng(16, 16, 10, 20, 30);
    const auto png1 = makeTestPng(16, 16, 200, 100, 50);

    std::string err;
    ASSERT_TRUE(gamedata::IconPakWriter::write(
        pakPath,
        {
            {"resources/corrupt-sig", "corrupt-sig", {}, png0, 16, 16},
            {"resources/corrupt-idat", "corrupt-idat", {}, png0, 16, 16},
            {"resources/healthy", "healthy", {}, png1, 16, 16}
        },
        &err)) << err;

    gamedata::IconPakReader initialReader;
    ASSERT_TRUE(gamedata::IconPakReader::open(pakPath, initialReader, &err)) << err;
    const auto* eSig = initialReader.find("resources/corrupt-sig");
    const auto* eIdat = initialReader.find("resources/corrupt-idat");
    ASSERT_NE(eSig, nullptr);
    ASSERT_NE(eIdat, nullptr);

    // Corrupt signature of first entry and IDAT byte of second entry, keeping header + index valid
    std::vector<uint8_t> bytes = readFileBytes(pakPath);
    bytes[static_cast<size_t>(eSig->offset)] = 0x00;
    bytes[static_cast<size_t>(eIdat->offset + eIdat->size - 15)] ^= 0xFF;
    writeFileBytes(pakPath, bytes);

    gamedata::IconPakReader reader;
    ASSERT_TRUE(gamedata::IconPakReader::open(pakPath, reader, &err)) << err;
    EXPECT_EQ(reader.size(), 3u);
    EXPECT_FALSE(reader.readBlob("resources/corrupt-sig").has_value());
    EXPECT_FALSE(reader.readBlob("resources/corrupt-idat").has_value());
    const auto healthyBlob = reader.readBlob("resources/healthy");
    ASSERT_TRUE(healthyBlob.has_value());
    EXPECT_EQ(*healthyBlob, png1);
}

// ============================================================================
// 3. `validate` icon checks (T8 lookup order, V-ICON-1 loose & pak, V-W-1, no icons)
// ============================================================================

TEST(GameDataToolIconsTest, ValidateChecksT8OrderVIcon1AndVW1) {
    TempDirGuard tmp("val_t8");
    const std::filesystem::path gameDir = tmp.path() / "mygame";
    const std::filesystem::path pkgDir = gameDir / "v1";
    writeMinimalTestPackage(pkgDir, "../shared_icons");

    const auto png16 = makeTestPng(16, 16, 50, 100, 150);
    const auto png32 = makeTestPng(32, 32, 60, 110, 160);
    const auto pngOversize = makeTestPng(128, 96);

    // 1. No icons mode -> info note, passes --werror
    {
        auto res = runCli({"validate", pathToUtf8(pkgDir), "--werror", "--json"});
        ASSERT_EQ(res.code, kExitOk) << res.out << res.err;
        nlohmann::json j = nlohmann::json::parse(res.out);
        EXPECT_EQ(j["counts"]["errors"], 0);
        EXPECT_EQ(j["counts"]["warnings"], 0);
        EXPECT_EQ(j["counts"]["notes"], 1);
    }

    // 2. Loose icons under ../shared_icons with one invalid (oversize) and one missing -> V-ICON-1 + V-W-1
    const std::filesystem::path sharedIcons = gameDir / "shared_icons";
    writeFileBytes(sharedIcons / "resources" / "iron-ore.png", pngOversize);
    writeFileBytes(sharedIcons / "resources" / "copper-plate.png", png16);
    writeFileBytes(sharedIcons / "machines" / "arc-furnace.png", png16);
    // Missing: options/tiers/lv.png -> V-W-1
    {
        auto res = runCli({"validate", pathToUtf8(pkgDir), "--json"});
        EXPECT_EQ(res.code, kExitError);
        nlohmann::json j = nlohmann::json::parse(res.out);
        EXPECT_EQ(j["counts"]["errors"], 1);
        EXPECT_EQ(j["counts"]["warnings"], 1);
        EXPECT_EQ(j["counts"]["notes"], 0);
        EXPECT_EQ(j["groups"][0]["rule"], "V-ICON-1");
        EXPECT_EQ(j["groups"][1]["rule"], "V-W-1");

        // --no-icons suppresses both
        auto noIcons = runCli({"validate", pathToUtf8(pkgDir), "--no-icons", "--werror", "--json"});
        EXPECT_EQ(noIcons.code, kExitOk) << noIcons.out;
    }

    // 3. T8 priority: <icon_root>/icons.pak overrides invalid loose PNG; <package_root>/icons.pak overrides <icon_root>/icons.pak
    std::string err;
    ASSERT_TRUE(gamedata::IconPakWriter::write(
        sharedIcons / "icons.pak",
        {
            {"resources/iron-ore", "iron-ore", {}, png32, 32, 32},
            {"options/tiers/lv", "", {}, png16, 16, 16}
        },
        &err)) << err;
    // Now iron-ore & lv come from shared_icons/icons.pak (valid!), copper-plate & arc-furnace fall back to loose (valid!)
    {
        auto res = runCli({"validate", pathToUtf8(pkgDir), "--werror", "--json"});
        ASSERT_EQ(res.code, kExitOk) << res.out << res.err;
        nlohmann::json j = nlohmann::json::parse(res.out);
        EXPECT_EQ(j["counts"]["errors"], 0);
        EXPECT_EQ(j["counts"]["warnings"], 0);
    }

    // 4. V-ICON-1 for a pak entry (invalid 128x96 entry inside <package_root>/icons.pak overrides valid <icon_root>/icons.pak)
    ASSERT_TRUE(gamedata::IconPakWriter::write(
        pkgDir / "icons.pak",
        {
            {"resources/iron-ore", "iron-ore", {}, pngOversize, 128, 96}
        },
        &err)) << err;
    {
        auto res = runCli({"validate", pathToUtf8(pkgDir), "--json"});
        EXPECT_EQ(res.code, kExitError);
        nlohmann::json j = nlohmann::json::parse(res.out);
        EXPECT_EQ(j["counts"]["errors"], 1);
        EXPECT_EQ(j["groups"][0]["rule"], "V-ICON-1");
        EXPECT_EQ(j["groups"][0]["subject"], "resources/iron-ore");
    }
}

// ============================================================================
// 4. `pack-icons`, `check-icons`, `prune-icons`, and `stats` T8 (§9)
// ============================================================================

TEST(GameDataToolIconsTest, PackIconsDeterministicVerifyFromListAndNoIcons) {
    TempDirGuard tmp("pack_icons");
    const std::filesystem::path pkgDir = tmp.path() / "pkg";
    writeMinimalTestPackage(pkgDir);

    // No icons present -> writes nothing, exits 0 with note
    {
        auto noIcons = runCli({"pack-icons", pathToUtf8(pkgDir), "--json"});
        ASSERT_EQ(noIcons.code, kExitOk) << noIcons.err;
        nlohmann::json j = nlohmann::json::parse(noIcons.out);
        EXPECT_FALSE(j["written"].get<bool>());
        EXPECT_FALSE(std::filesystem::exists(pkgDir / "icons.pak"));
        ASSERT_EQ(j["notes"].size(), 1u);
    }

    // Put valid referenced icons + one unreferenced oversize icon in .tmp_icons/
    const std::filesystem::path scratchIcons = tmp.path() / ".tmp_icons";
    const auto png16 = makeTestPng(16, 16, 10, 20, 30);
    const auto png32 = makeTestPng(32, 32, 40, 50, 60);
    const auto png64 = makeTestPng(64, 64, 70, 80, 90);
    const auto pngBad = makeTestPng(96, 96);

    writeFileBytes(scratchIcons / "resources" / "iron-ore.png", png16);
    writeFileBytes(scratchIcons / "resources" / "copper-plate.png", png32);
    writeFileBytes(scratchIcons / "machines" / "arc-furnace.png", png64);
    writeFileBytes(scratchIcons / "options" / "tiers" / "lv.png", png16);
    // Unreferenced icon must be ignored by pack-icons (even if oversize!)
    writeFileBytes(scratchIcons / "resources" / "unreferenced-item.png", pngBad);

    const std::filesystem::path outPak1 = pkgDir / "icons.pak";
    const std::filesystem::path outPak2 = tmp.path() / "second.pak";

    auto pack1 = runCli({
        "pack-icons", pathToUtf8(pkgDir),
        "--from", pathToUtf8(scratchIcons),
        "--verify",
        "--json"
    });
    ASSERT_EQ(pack1.code, kExitOk) << pack1.out << pack1.err;
    nlohmann::json jp1 = nlohmann::json::parse(pack1.out);
    EXPECT_TRUE(jp1["written"].get<bool>());
    EXPECT_TRUE(jp1["verified"].get<bool>());
    EXPECT_EQ(jp1["count"], 4);

    // Second run to -o FILE must produce byte-identical output
    auto pack2 = runCli({
        "pack-icons", pathToUtf8(pkgDir),
        "--from", pathToUtf8(scratchIcons),
        "-o", pathToUtf8(outPak2),
        "--verify"
    });
    ASSERT_EQ(pack2.code, kExitOk) << pack2.out << pack2.err;
    EXPECT_EQ(readFileBytes(outPak1), readFileBytes(outPak2));

    // Verify name_slugs and --list FILE
    auto listRes = runCli({"pack-icons", "--list", pathToUtf8(outPak1), "--json"});
    ASSERT_EQ(listRes.code, kExitOk) << listRes.err;
    nlohmann::json jl = nlohmann::json::parse(listRes.out);
    EXPECT_EQ(jl["version"], 1);
    EXPECT_EQ(jl["count"], 4);
    EXPECT_EQ(jl["entries"][0]["key"], "machines/arc-furnace");
    EXPECT_EQ(jl["entries"][0]["name_slug"], "arc-furnace-plus-mk1");
    EXPECT_EQ(jl["entries"][1]["key"], "options/tiers/lv");
    EXPECT_EQ(jl["entries"][1]["name_slug"], "");
    EXPECT_EQ(jl["entries"][2]["key"], "resources/copper-plate");
    EXPECT_EQ(jl["entries"][2]["name_slug"], "copper-plate");
    EXPECT_EQ(jl["entries"][3]["key"], "resources/iron-ore");
    EXPECT_EQ(jl["entries"][3]["name_slug"], "iron-ore");

    // `stats` reports pak coverage in T8 order
    auto statsRes = runCli({"stats", pathToUtf8(pkgDir), "--json"});
    ASSERT_EQ(statsRes.code, kExitOk) << statsRes.err;
    nlohmann::json js = nlohmann::json::parse(statsRes.out);
    EXPECT_EQ(js["icons"]["mode"], "pak");
    EXPECT_EQ(js["icons"]["resources"]["present"], 2);
    EXPECT_EQ(js["icons"]["resources"]["total"], 2);
    EXPECT_EQ(js["icons"]["machines"]["present"], 1);
    EXPECT_EQ(js["icons"]["machines"]["total"], 1);
    EXPECT_EQ(js["icons"]["options"]["present"], 1);
    EXPECT_EQ(js["icons"]["options"]["total"], 1);

    // If a referenced icon violates V-ICON-1, pack-icons fails and does not overwrite output
    const std::filesystem::path badTargetPak = tmp.path() / "should_not_exist.pak";
    writeFileBytes(scratchIcons / "resources" / "iron-ore.png", pngBad);
    auto packFail = runCli({
        "pack-icons", pathToUtf8(pkgDir),
        "--from", pathToUtf8(scratchIcons),
        "-o", pathToUtf8(badTargetPak)
    });
    EXPECT_EQ(packFail.code, kExitError);
    EXPECT_FALSE(std::filesystem::exists(badTargetPak));
}

TEST(GameDataToolIconsTest, CheckIconsDetectsMissingUnreferencedInvalidCaseAndStalePak) {
    TempDirGuard tmp("check_icons");
    const std::filesystem::path pkgDir = tmp.path() / "pkg";
    writeMinimalTestPackage(pkgDir);

    // 1. "No icons" mode -> note, exit 0 even with --werror
    {
        auto noIcons = runCli({"check-icons", pathToUtf8(pkgDir), "--werror", "--json"});
        ASSERT_EQ(noIcons.code, kExitOk) << noIcons.err;
        nlohmann::json j = nlohmann::json::parse(noIcons.out);
        EXPECT_EQ(j["mode"], "none");
        ASSERT_EQ(j["notes"].size(), 1u);
    }

    const auto png16 = makeTestPng(16, 16, 10, 20, 30);
    const auto pngNonSquare = makeTestPng(32, 16);
    const std::filesystem::path iconsDir = pkgDir / "icons";

    // Create:
    // - resources/Iron-Ore.png (case mismatch!)
    // - resources/copper-plate.png (non-square V-ICON-1!)
    // - machines/arc-furnace.png (valid)
    // - resources/extra-unused.png (unreferenced!)
    // - missing: options/tiers/lv.png (V-W-1!)
    writeFileBytes(iconsDir / "resources" / "Iron-Ore.png", png16);
    writeFileBytes(iconsDir / "resources" / "copper-plate.png", pngNonSquare);
    writeFileBytes(iconsDir / "machines" / "arc-furnace.png", png16);
    writeFileBytes(iconsDir / "resources" / "extra-unused.png", png16);

    auto chk = runCli({"check-icons", pathToUtf8(pkgDir), "--json"});
    EXPECT_EQ(chk.code, kExitError); // V-ICON-1 always exits 1
    nlohmann::json j = nlohmann::json::parse(chk.out);
    EXPECT_EQ(j["counts"]["invalid"], 1);
    EXPECT_EQ(j["counts"]["missing"], 1);
    EXPECT_EQ(j["counts"]["unreferenced"], 1);
    EXPECT_EQ(j["counts"]["case_mismatches"], 1);
    EXPECT_EQ(j["case_mismatches"][0]["expected"], "resources/iron-ore.png");
    EXPECT_EQ(j["case_mismatches"][0]["actual"], "resources/Iron-Ore.png");

    // Fix invalid + case mismatch + unreferenced, leave 1 missing -> exit 0 without --werror, exit 1 with --werror
    std::error_code ec;
    std::filesystem::remove(iconsDir / "resources" / "Iron-Ore.png", ec);
    std::filesystem::remove(iconsDir / "resources" / "extra-unused.png", ec);
    writeFileBytes(iconsDir / "resources" / "iron-ore.png", png16);
    writeFileBytes(iconsDir / "resources" / "copper-plate.png", png16);

    EXPECT_EQ(runCli({"check-icons", pathToUtf8(pkgDir)}).code, kExitOk);
    EXPECT_EQ(runCli({"check-icons", pathToUtf8(pkgDir), "--werror"}).code, kExitError);

    // Add missing option icon, pack into icons.pak, then verify clean
    writeFileBytes(iconsDir / "options" / "tiers" / "lv.png", png16);
    ASSERT_EQ(runCli({"pack-icons", pathToUtf8(pkgDir)}).code, kExitOk);
    EXPECT_EQ(runCli({"check-icons", pathToUtf8(pkgDir), "--werror"}).code, kExitOk);

    // Make loose file newer than icons.pak -> check-icons detects stale pak!
    const auto pakTime = std::filesystem::last_write_time(pkgDir / "icons.pak", ec);
    ASSERT_FALSE(ec);
    std::filesystem::last_write_time(
        iconsDir / "resources" / "iron-ore.png",
        pakTime + std::chrono::seconds(5),
        ec);
    ASSERT_FALSE(ec);

    auto staleRes = runCli({"check-icons", pathToUtf8(pkgDir), "--werror", "--json"});
    EXPECT_EQ(staleRes.code, kExitError);
    nlohmann::json js = nlohmann::json::parse(staleRes.out);
    EXPECT_TRUE(js["pak_stale"].get<bool>());
    EXPECT_FALSE(js["pak_stale_reasons"].empty());
}

TEST(GameDataToolIconsTest, PruneIconsDryRunVsApplyAndFromNeverTouchesPak) {
    TempDirGuard tmp("prune_icons");
    const std::filesystem::path pkgDir = tmp.path() / "pkg";
    writeMinimalTestPackage(pkgDir);

    const auto png16 = makeTestPng(16, 16, 10, 20, 30);
    const std::filesystem::path customDir = tmp.path() / "custom_icons";
    writeFileBytes(customDir / "resources" / "iron-ore.png", png16);
    writeFileBytes(customDir / "resources" / "obsolete-a.png", png16);
    writeFileBytes(customDir / "machines" / "obsolete-b.png", png16);

    // Also pack icons.pak in pkgDir AND place an icons.pak inside customDir to verify neither is touched
    ASSERT_EQ(runCli({"pack-icons", pathToUtf8(pkgDir), "--from", pathToUtf8(customDir)}).code, kExitOk);
    ASSERT_TRUE(std::filesystem::exists(pkgDir / "icons.pak"));
    writeFileBytes(customDir / "icons.pak", readFileBytes(pkgDir / "icons.pak"));

    // Dry run: lists unreferenced PNGs, deletes nothing
    auto dry = runCli({"prune-icons", pathToUtf8(pkgDir), "--from", pathToUtf8(customDir), "--json"});
    ASSERT_EQ(dry.code, kExitOk) << dry.err;
    nlohmann::json jd = nlohmann::json::parse(dry.out);
    EXPECT_FALSE(jd["apply"].get<bool>());
    EXPECT_EQ(jd["unreferenced"].size(), 2u);
    EXPECT_EQ(jd["deleted"].size(), 0u);
    EXPECT_TRUE(std::filesystem::exists(customDir / "resources" / "obsolete-a.png"));
    EXPECT_TRUE(std::filesystem::exists(customDir / "machines" / "obsolete-b.png"));

    // Apply: deletes only unreferenced PNGs; keeps referenced PNG and both icons.pak files
    auto applied = runCli({
        "prune-icons", pathToUtf8(pkgDir),
        "--from", pathToUtf8(customDir),
        "--apply",
        "--json"
    });
    ASSERT_EQ(applied.code, kExitOk) << applied.err;
    nlohmann::json ja = nlohmann::json::parse(applied.out);
    EXPECT_TRUE(ja["apply"].get<bool>());
    EXPECT_EQ(ja["deleted"].size(), 2u);
    EXPECT_FALSE(std::filesystem::exists(customDir / "resources" / "obsolete-a.png"));
    EXPECT_FALSE(std::filesystem::exists(customDir / "machines" / "obsolete-b.png"));
    EXPECT_TRUE(std::filesystem::exists(customDir / "resources" / "iron-ore.png"));
    EXPECT_TRUE(std::filesystem::exists(pkgDir / "icons.pak"));
    EXPECT_TRUE(std::filesystem::exists(customDir / "icons.pak"));
}

TEST(GameDataToolIconsTest, EdgeCasesCorruptPakValidateListAndUsageErrors) {
    TempDirGuard tmp("icon_edges");
    const std::filesystem::path pkgDir = tmp.path() / "pkg";
    writeMinimalTestPackage(pkgDir);

    // 1. Corrupt icons.pak in package_root -> validate exits 1 with V-ICON-1
    ASSERT_TRUE(atomicWriteFile(pkgDir / "icons.pak", "NOT_A_VALID_PAK_FILE"));
    auto valCorrupt = runCli({"validate", pathToUtf8(pkgDir), "--json"});
    EXPECT_EQ(valCorrupt.code, kExitError);
    nlohmann::json jvc = nlohmann::json::parse(valCorrupt.out);
    EXPECT_EQ(jvc["counts"]["errors"], 1);
    EXPECT_EQ(jvc["groups"][0]["rule"], "V-ICON-1");

    // 2. pack-icons --list on missing or corrupt pak
    EXPECT_EQ(runCli({"pack-icons", "--list", pathToUtf8(tmp.path() / "missing.pak")}).code, kExitFatal);
    EXPECT_EQ(runCli({"pack-icons", "--list", pathToUtf8(pkgDir / "icons.pak")}).code, kExitError);

    // 3. Stale pak via missing entry in --from dir
    const std::filesystem::path fromDir = tmp.path() / "from_icons";
    const auto png16 = makeTestPng(16, 16, 20, 30, 40);
    writeFileBytes(fromDir / "resources" / "iron-ore.png", png16);
    ASSERT_EQ(runCli({"pack-icons", pathToUtf8(pkgDir), "--from", pathToUtf8(fromDir)}).code, kExitOk);
    // Add a second icon to fromDir without repacking
    writeFileBytes(fromDir / "resources" / "copper-plate.png", png16);
    auto staleMissing = runCli({"check-icons", pathToUtf8(pkgDir), "--from", pathToUtf8(fromDir), "--werror", "--json"});
    EXPECT_EQ(staleMissing.code, kExitError);
    nlohmann::json jsm = nlohmann::json::parse(staleMissing.out);
    EXPECT_TRUE(jsm["pak_stale"].get<bool>());

    // 4. Usage errors for new subcommands/flags
    EXPECT_EQ(runCli({"check-icons"}).code, kExitUsage);
    EXPECT_EQ(runCli({"prune-icons"}).code, kExitUsage);
    EXPECT_EQ(runCli({"pack-icons"}).code, kExitUsage);
    EXPECT_EQ(runCli({"pack-icons", pathToUtf8(pkgDir), "--list", "foo.pak"}).code, kExitUsage);
    EXPECT_EQ(runCli({"pack-icons", "--list", "foo.pak", "--verify"}).code, kExitUsage);
    EXPECT_EQ(runCli({"check-icons", pathToUtf8(pkgDir), "--bogus"}).code, kExitUsage);
    EXPECT_EQ(runCli({"check-icons", "--help"}).code, kExitOk);
    EXPECT_EQ(runCli({"prune-icons", "--help"}).code, kExitOk);
    EXPECT_EQ(runCli({"pack-icons", "--help"}).code, kExitOk);
}

} // namespace
} // namespace gamedata_tool
