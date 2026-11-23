#include <gtest/gtest.h>

#include "SyntheticPackageGenerator.h"
#include "common/FilesystemUtils.h"
#include "common/StringUtils.h"
#include "core/data/GameDataLoader.h"
#include "core/data/GameDataScanner.h"
#include "core/data/IconPak.h"
#include "core/data/RecipeSearchIndex.h"
#include "services/IconLoader.h"
#include "services/IconSourceResolver.h"
#include "services/SettingsManager.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

class TempDirGuard {
public:
    explicit TempDirGuard(const std::string& prefix) {
        static std::atomic<uint64_t> counter{0};
        static const uint64_t salt = []() {
            std::random_device rd;
            uint64_t r = (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
            uint64_t t = static_cast<uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count());
            return r ^ t;
        }();
        m_path = std::filesystem::temp_directory_path() /
                 ("fp_lazy_icons_test_" + prefix + "_" +
                  std::to_string(salt) + "_" + std::to_string(++counter));
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

void appendU32BE(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    buf.push_back(static_cast<uint8_t>(v & 0xFFu));
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
    appendU32BE(png, computeCrc32(png.data() + crcStart, png.size() - crcStart));
}

// Builds a valid 1x1 RGBA PNG carrying a unique color signature (r, g, b, 255).
std::vector<uint8_t> makeTaggedPng1x1(uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    std::vector<uint8_t> ihdr;
    appendU32BE(ihdr, 1); // width
    appendU32BE(ihdr, 1); // height
    ihdr.push_back(8);    // bit depth
    ihdr.push_back(6);    // RGBA
    ihdr.push_back(0);    // compression
    ihdr.push_back(0);    // filter
    ihdr.push_back(0);    // interlace
    appendPngChunk(png, "IHDR", ihdr);

    // Raw scanline: filter byte 0 + 4 RGBA bytes = 5 bytes
    const uint8_t raw[5] = {0, r, g, b, 255};
    std::vector<uint8_t> zlib;
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    zlib.push_back(0x01); // BFINAL=1, BTYPE=00 (stored)
    zlib.push_back(0x05);
    zlib.push_back(0x00);
    zlib.push_back(0xFA);
    zlib.push_back(0xFF);
    zlib.insert(zlib.end(), raw, raw + 5);
    appendU32BE(zlib, computeAdler32(raw, 5));
    appendPngChunk(png, "IDAT", zlib);

    appendPngChunk(png, "IEND", {});
    return png;
}

bool writeBinaryFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    if (!bytes.empty()) {
        ofs.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return ofs.good();
}

DecodedIcon makeDecodedPixel(uint8_t tag, bool fromFallback = false) {
    DecodedIcon d;
    d.found = true;
    d.from_fallback = fromFallback;
    d.width = 1;
    d.height = 1;
    d.rgba = {tag, 10, 20, 255};
    return d;
}

std::filesystem::path findRepoDataDir(const std::string& subPath) {
    const std::vector<std::filesystem::path> candidates = {
        std::filesystem::current_path() / "data" / "game_data" / subPath,
        std::filesystem::current_path().parent_path() / "data" / "game_data" / subPath,
        std::filesystem::current_path().parent_path().parent_path() / "data" / "game_data" / subPath,
    };
    for (const auto& c : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(c, ec)) {
            return c;
        }
    }
    if (auto exeDir = getExecutableDirectory()) {
        std::filesystem::path cur = *exeDir;
        for (int i = 0; i < 5; ++i) {
            std::filesystem::path p = cur / "data" / "game_data" / subPath;
            std::error_code ec;
            if (std::filesystem::exists(p, ec)) {
                return p;
            }
            if (!cur.has_parent_path() || cur == cur.parent_path()) break;
            cur = cur.parent_path();
        }
    }
    return {};
}

// Legacy linear filter matching pre-4c FactoryNodeEditor recipe picker logic verbatim.
std::vector<uint32_t> legacyLinearRecipeFilter(
    const gamedata::GamePackage& pkg,
    std::string_view rawQuery,
    gamedata::RecipeSearchContextKind ctxKind,
    std::string_view ctxResKey) {
    std::string filter(rawQuery);
    std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    std::vector<uint32_t> out;
    for (size_t idx = 0; idx < pkg.recipes.size(); ++idx) {
        const auto& recipe = pkg.recipes[idx];
        if (ctxKind != gamedata::RecipeSearchContextKind::All) {
            bool compatible = false;
            const auto& targetPorts =
                (ctxKind == gamedata::RecipeSearchContextKind::ProducingResource)
                    ? recipe.out
                    : recipe.in;
            for (const auto& p : targetPorts) {
                if (p.res.value_or("") == ctxResKey) {
                    compatible = true;
                    break;
                }
            }
            if (!compatible) continue;
        }

        bool match = false;
        if (filter.empty()) {
            match = true;
        } else {
            std::string nameLower = recipe.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (nameLower.find(filter) != std::string::npos) match = true;

            if (!match) {
                for (const auto& p : recipe.in) {
                    std::string rKey = p.res.value_or("");
                    if (const auto* res = pkg.findResource(rKey)) {
                        std::string resName = res->name;
                        std::transform(resName.begin(), resName.end(), resName.begin(), [](unsigned char c) {
                            return static_cast<char>(std::tolower(c));
                        });
                        if (resName.find(filter) != std::string::npos) {
                            match = true;
                            break;
                        }
                    }
                }
            }
            if (!match) {
                for (const auto& p : recipe.out) {
                    std::string rKey = p.res.value_or("");
                    if (const auto* res = pkg.findResource(rKey)) {
                        std::string resName = res->name;
                        std::transform(resName.begin(), resName.end(), resName.begin(), [](unsigned char c) {
                            return static_cast<char>(std::tolower(c));
                        });
                        if (resName.find(filter) != std::string::npos) {
                            match = true;
                            break;
                        }
                    }
                }
            }
        }

        if (match) {
            out.push_back(static_cast<uint32_t>(idx));
        }
    }
    return out;
}

// ============================================================================
// 1. IconLoader (GL-free scheduling core — §4.1, §9)
// ============================================================================

TEST(IconLoaderTest, DedupAndLifoPopOrder) {
    constexpr TextureHandle kLoading = 1;
    constexpr TextureHandle kMissing = 2;

    std::vector<std::string> decodedOrder;
    TextureHandle nextHandle = 100;

    IconLoaderConfig cfg;
    cfg.loading_placeholder = kLoading;
    cfg.missing_placeholder = kMissing;
    cfg.auto_start_workers = false;
    cfg.decode_fn = [&](const IconRef& ref, uint64_t, size_t) {
        decodedOrder.push_back(ref.id);
        return makeDecodedPixel(static_cast<uint8_t>(decodedOrder.size()));
    };
    cfg.upload_fn = [&](const uint8_t*, int, int) { return nextHandle++; };

    IconLoader loader(std::move(cfg));
    loader.beginFrame(1);

    // Enqueue A, B, C, then re-touch A (deduplicates A and promotes A to top of LIFO stack).
    EXPECT_EQ(loader.get({"pkg", IconKind::Resource, "a", "A"}), kLoading);
    EXPECT_EQ(loader.get({"pkg", IconKind::Resource, "b", "B"}), kLoading);
    EXPECT_EQ(loader.get({"pkg", IconKind::Resource, "c", "C"}), kLoading);
    EXPECT_EQ(loader.get({"pkg", IconKind::Resource, "a", "A"}), kLoading);

    EXPECT_EQ(loader.stats().pending_requests, 3u);
    EXPECT_TRUE(loader.busy());

    // LIFO order after promoting A: A, C, B.
    while (loader.stepWorkerOnce()) {}
    ASSERT_EQ(decodedOrder.size(), 3u);
    EXPECT_EQ(decodedOrder[0], "a");
    EXPECT_EQ(decodedOrder[1], "c");
    EXPECT_EQ(decodedOrder[2], "b");

    // Next frame uploads all 3; subsequent get() returns resident handles without re-decoding.
    loader.beginFrame(2);
    EXPECT_FALSE(loader.busy());
    const TextureHandle ha = loader.get({"pkg", IconKind::Resource, "a", "A"});
    const TextureHandle hb = loader.get({"pkg", IconKind::Resource, "b", "B"});
    const TextureHandle hc = loader.get({"pkg", IconKind::Resource, "c", "C"});
    EXPECT_GE(ha, 100u);
    EXPECT_GE(hb, 100u);
    EXPECT_GE(hc, 100u);
    EXPECT_NE(ha, hb);
    EXPECT_NE(hb, hc);
    EXPECT_EQ(decodedOrder.size(), 3u);
}

TEST(IconLoaderTest, StaleRequestDropAndCleanReRequestOnRescroll) {
    constexpr TextureHandle kLoading = 1;
    constexpr TextureHandle kMissing = 2;

    std::vector<std::string> decoded;
    TextureHandle nextHandle = 100;

    IconLoaderConfig cfg;
    cfg.loading_placeholder = kLoading;
    cfg.missing_placeholder = kMissing;
    cfg.stale_frame_window = 2;
    cfg.auto_start_workers = false;
    cfg.decode_fn = [&](const IconRef& ref, uint64_t, size_t) {
        decoded.push_back(ref.id);
        return makeDecodedPixel(42);
    };
    cfg.upload_fn = [&](const uint8_t*, int, int) { return nextHandle++; };

    IconLoader loader(std::move(cfg));

    // Frame 10: request old_item and kept_item.
    loader.beginFrame(10);
    EXPECT_EQ(loader.get({"pkg", IconKind::Resource, "old_item", "Old"}), kLoading);
    EXPECT_EQ(loader.get({"pkg", IconKind::Resource, "kept_item", "Kept"}), kLoading);

    // Frame 12: touch kept_item only; old_item is still within N-2 (12 - 10 == 2).
    loader.beginFrame(12);
    EXPECT_EQ(loader.get({"pkg", IconKind::Resource, "kept_item", "Kept"}), kLoading);

    // Frame 13: old_item was last touched at frame 10 -> 13 - 10 = 3 > 2 -> stale drop!
    // kept_item was touched at frame 12 -> 13 - 12 = 1 <= 2 -> still decoded!
    loader.beginFrame(13);
    while (loader.stepWorkerOnce()) {}

    ASSERT_EQ(decoded.size(), 1u);
    EXPECT_EQ(decoded[0], "kept_item");
    EXPECT_EQ(loader.stats().dropped_stale_frame, 1u);

    loader.beginFrame(14);
    EXPECT_GE(loader.get({"pkg", IconKind::Resource, "kept_item", "Kept"}), 100u);

    // Scroll back to old_item: since it was dropped cleanly (not cached missing), it re-requests!
    EXPECT_EQ(loader.get({"pkg", IconKind::Resource, "old_item", "Old"}), kLoading);
    EXPECT_TRUE(loader.stepWorkerOnce());
    ASSERT_EQ(decoded.size(), 2u);
    EXPECT_EQ(decoded[1], "old_item");
    loader.beginFrame(15);
    EXPECT_GE(loader.get({"pkg", IconKind::Resource, "old_item", "Old"}), 100u);
}

TEST(IconLoaderTest, UploadBudgetRespectsCountAndTimeCapAndKeepsBusyTrue) {
    constexpr TextureHandle kLoading = 1;
    constexpr TextureHandle kMissing = 2;

    // Part A: count cap (4 per frame, 10 ready items -> 4 + 4 + 2 across 3 frames).
    {
        size_t uploadCalls = 0;
        TextureHandle nextHandle = 100;
        IconLoaderConfig cfg;
        cfg.loading_placeholder = kLoading;
        cfg.missing_placeholder = kMissing;
        cfg.max_uploads_per_frame = 4;
        cfg.max_upload_time_ms = 1000.0;
        cfg.auto_start_workers = false;
        cfg.decode_fn = [](const IconRef&, uint64_t, size_t) { return makeDecodedPixel(7); };
        cfg.upload_fn = [&](const uint8_t*, int, int) {
            ++uploadCalls;
            return nextHandle++;
        };

        IconLoader loader(std::move(cfg));
        loader.beginFrame(1);
        for (int i = 0; i < 10; ++i) {
            EXPECT_EQ(loader.get({"pkg", IconKind::Resource, "r" + std::to_string(i), ""}), kLoading);
        }
        while (loader.stepWorkerOnce()) {}
        EXPECT_EQ(loader.stats().ready_to_upload, 10u);

        loader.beginFrame(2);
        EXPECT_EQ(uploadCalls, 4u);
        EXPECT_TRUE(loader.busy());

        loader.beginFrame(3);
        EXPECT_EQ(uploadCalls, 8u);
        EXPECT_TRUE(loader.busy());

        loader.beginFrame(4);
        EXPECT_EQ(uploadCalls, 10u);
        EXPECT_FALSE(loader.busy());
    }

    // Part B: time cap (1.5 ms cap with mock clock advancing 0.6 ms per upload -> at most 3 uploads/frame).
    {
        auto mockNow = std::chrono::steady_clock::time_point{};
        size_t uploadCalls = 0;
        TextureHandle nextHandle = 500;

        IconLoaderConfig cfg;
        cfg.loading_placeholder = kLoading;
        cfg.missing_placeholder = kMissing;
        cfg.max_uploads_per_frame = 64;
        cfg.max_upload_time_ms = 1.5;
        cfg.auto_start_workers = false;
        cfg.now_fn = [&]() { return mockNow; };
        cfg.decode_fn = [](const IconRef&, uint64_t, size_t) { return makeDecodedPixel(9); };
        cfg.upload_fn = [&](const uint8_t*, int, int) {
            ++uploadCalls;
            mockNow += std::chrono::microseconds(600); // 0.6 ms per upload
            return nextHandle++;
        };

        IconLoader loader(std::move(cfg));
        loader.beginFrame(1);
        for (int i = 0; i < 6; ++i) {
            loader.get({"pkg", IconKind::Resource, "t" + std::to_string(i), ""});
        }
        while (loader.stepWorkerOnce()) {}

        // t=0.0 -> upload #1 (t=0.6) -> upload #2 (t=1.2) -> upload #3 (t=1.8 >= 1.5) -> stop.
        loader.beginFrame(2);
        EXPECT_EQ(uploadCalls, 3u);
        EXPECT_TRUE(loader.busy());

        loader.beginFrame(3);
        EXPECT_EQ(uploadCalls, 6u);
        EXPECT_FALSE(loader.busy());
    }
}

TEST(IconLoaderTest, LruEvictionNeverEvictsCurrentOrPreviousFrameTextures) {
    constexpr TextureHandle kLoading = 1;
    constexpr TextureHandle kMissing = 2;

    TextureHandle nextHandle = 200;
    std::vector<TextureHandle> freed;

    IconLoaderConfig cfg;
    cfg.loading_placeholder = kLoading;
    cfg.missing_placeholder = kMissing;
    cfg.max_resident_textures = 2;
    cfg.auto_start_workers = false;
    cfg.decode_fn = [](const IconRef& ref, uint64_t, size_t) {
        if (ref.id == "missing_one") {
            return DecodedIcon{}; // found = false
        }
        return makeDecodedPixel(11);
    };
    cfg.upload_fn = [&](const uint8_t*, int, int) { return nextHandle++; };
    cfg.free_fn = [&](TextureHandle h) { freed.push_back(h); };

    IconLoader loader(std::move(cfg));

    // Frame 1: request a, b, c, and missing_one.
    loader.beginFrame(1);
    loader.get({"pkg", IconKind::Resource, "a", ""});
    loader.get({"pkg", IconKind::Resource, "b", ""});
    loader.get({"pkg", IconKind::Resource, "c", ""});
    loader.get({"pkg", IconKind::Resource, "missing_one", ""});
    while (loader.stepWorkerOnce()) {}

    // Frame 2: uploads a, b, c (all stamped frame 2) + caches missing_one.
    // Even though 3 > max_resident_textures (2), NONE can be evicted because all are frame 2 (current frame).
    loader.beginFrame(2);
    EXPECT_EQ(loader.stats().resident_textures, 3u);
    EXPECT_EQ(loader.stats().missing_entries, 1u);
    EXPECT_TRUE(freed.empty());
    EXPECT_EQ(loader.get({"pkg", IconKind::Resource, "missing_one", ""}), kMissing);

    // Frame 3: all 3 textures were last used on frame 2 == currentFrame - 1 (previous frame) -> STILL protected!
    loader.beginFrame(3);
    EXPECT_EQ(loader.stats().resident_textures, 3u);
    EXPECT_TRUE(freed.empty());

    // Touch "b" and "c" on frame 3 so "a" becomes the oldest (last used frame 2).
    const TextureHandle hb = loader.get({"pkg", IconKind::Resource, "b", ""});
    const TextureHandle hc = loader.get({"pkg", IconKind::Resource, "c", ""});
    EXPECT_GE(hb, 200u);
    EXPECT_GE(hc, 200u);

    // Frame 4: "a" was last used on frame 2 (2 + 1 < 4) -> safe to evict; "b" & "c" remain resident!
    loader.beginFrame(4);
    EXPECT_EQ(loader.stats().resident_textures, 2u);
    ASSERT_EQ(freed.size(), 1u);
    EXPECT_EQ(loader.stats().evicted_count, 1u);

    // Evicted key "a" requests again cleanly and reloads.
    EXPECT_EQ(loader.get({"pkg", IconKind::Resource, "a", ""}), kLoading);
    EXPECT_TRUE(loader.stepWorkerOnce());
    loader.beginFrame(5);
    EXPECT_GE(loader.get({"pkg", IconKind::Resource, "a", ""}), 200u);
    // "b" and "c" were frame 3 (3 + 1 < 5) so one of them was evicted when "a" pushed resident > 2.
    EXPECT_EQ(loader.stats().resident_textures, 2u);
}

TEST(IconLoaderTest, GenerationDiscardOnInvalidateAndDeferredFree) {
    constexpr TextureHandle kLoading = 1;
    constexpr TextureHandle kMissing = 2;

    TextureHandle nextHandle = 300;
    std::vector<TextureHandle> freed;

    IconLoaderConfig cfg;
    cfg.loading_placeholder = kLoading;
    cfg.missing_placeholder = kMissing;
    cfg.auto_start_workers = false;
    cfg.decode_fn = [](const IconRef&, uint64_t, size_t) { return makeDecodedPixel(55); };
    cfg.upload_fn = [&](const uint8_t*, int, int) { return nextHandle++; };
    cfg.free_fn = [&](TextureHandle h) { freed.push_back(h); };

    IconLoader loader(std::move(cfg));

    // Frame 1: request pkg1:a (uploaded frame 2) and pkg2:x (uploaded frame 2).
    loader.beginFrame(1);
    loader.get({"pkg1", IconKind::Resource, "a", ""});
    loader.get({"pkg2", IconKind::Resource, "x", ""});
    while (loader.stepWorkerOnce()) {}

    loader.beginFrame(2);
    const TextureHandle ha = loader.get({"pkg1", IconKind::Resource, "a", ""});
    const TextureHandle hx = loader.get({"pkg2", IconKind::Resource, "x", ""});
    EXPECT_GE(ha, 300u);
    EXPECT_GE(hx, 300u);

    // Also enqueue pkg1:b and decode it into ready queue BEFORE invalidating pkg1.
    loader.get({"pkg1", IconKind::Resource, "b", ""});
    EXPECT_TRUE(loader.stepWorkerOnce());

    // Invalidate pkg1 on frame 2:
    // - pkg1:a was used on frame 2, so its GL free is deferred until frame 4 (safe for in-flight draw list).
    // - pkg1:b ready result is purged / discarded.
    // - pkg2:x remains untouched!
    loader.invalidate("pkg1");
    EXPECT_EQ(loader.get({"pkg2", IconKind::Resource, "x", ""}), hx);
    EXPECT_TRUE(freed.empty()); // deferred because ha was used on frame 2
    EXPECT_EQ(loader.stats().deferred_frees, 1u);

    // Frame 3: still protected (2 >= 3 - 1).
    loader.beginFrame(3);
    EXPECT_TRUE(freed.empty());

    // Frame 4: deferred free executes.
    loader.beginFrame(4);
    ASSERT_EQ(freed.size(), 1u);
    EXPECT_EQ(freed[0], ha);
    EXPECT_EQ(loader.stats().deferred_frees, 0u);

    // Verify stale in-flight decode result discarded when generation bumps mid-flight.
    loader.get({"pkg1", IconKind::Resource, "c", ""});
    EXPECT_TRUE(loader.stepWorkerOnce()); // pushes generation G into ready queue
    // Bump global generation via invalidateAll before beginFrame.
    loader.invalidateAll();
    loader.beginFrame(6); // hx was frame 2 (2 + 1 < 6) so hx freed immediately on invalidateAll!
    EXPECT_EQ(loader.stats().resident_textures, 0u);
    EXPECT_EQ(freed.size(), 2u);
}

TEST(IconLoaderTest, InvalidateFallbackAndMissingRetainsDirectTextures) {
    constexpr TextureHandle kLoading = 1;
    constexpr TextureHandle kMissing = 2;

    TextureHandle nextHandle = 400;
    std::vector<TextureHandle> freed;

    IconLoaderConfig cfg;
    cfg.loading_placeholder = kLoading;
    cfg.missing_placeholder = kMissing;
    cfg.auto_start_workers = false;
    cfg.decode_fn = [](const IconRef& ref, uint64_t, size_t) {
        if (ref.id == "direct") return makeDecodedPixel(1, /*fromFallback=*/false);
        if (ref.id == "fallback") return makeDecodedPixel(2, /*fromFallback=*/true);
        return DecodedIcon{}; // missing
    };
    cfg.upload_fn = [&](const uint8_t*, int, int) { return nextHandle++; };
    cfg.free_fn = [&](TextureHandle h) { freed.push_back(h); };

    IconLoader loader(std::move(cfg));
    loader.beginFrame(1);
    loader.get({"pkg", IconKind::Resource, "direct", ""});
    loader.get({"pkg", IconKind::Resource, "fallback", ""});
    loader.get({"pkg", IconKind::Resource, "none", ""});
    while (loader.stepWorkerOnce()) {}

    loader.beginFrame(2);
    const TextureHandle hDirect = loader.get({"pkg", IconKind::Resource, "direct", ""});
    const TextureHandle hFallback = loader.get({"pkg", IconKind::Resource, "fallback", ""});
    EXPECT_GE(hDirect, 400u);
    EXPECT_GE(hFallback, 400u);
    EXPECT_EQ(loader.get({"pkg", IconKind::Resource, "none", ""}), kMissing);

    loader.beginFrame(5); // advance past protection window so frees happen immediately
    loader.invalidateFallbackAndMissing();

    // Direct icon survives; fallback and missing are cleared.
    EXPECT_EQ(loader.get({"pkg", IconKind::Resource, "direct", ""}), hDirect);
    EXPECT_EQ(loader.get({"pkg", IconKind::Resource, "fallback", ""}), kLoading);
    EXPECT_EQ(loader.get({"pkg", IconKind::Resource, "none", ""}), kLoading);
    ASSERT_EQ(freed.size(), 1u);
    EXPECT_EQ(freed[0], hFallback);
}

TEST(IconLoaderTest, GetReportsIsReadyFlagForOptionIcons) {
    constexpr TextureHandle kLoading = 1;
    constexpr TextureHandle kMissing = 2;

    TextureHandle nextHandle = 500;
    IconLoaderConfig cfg;
    cfg.loading_placeholder = kLoading;
    cfg.missing_placeholder = kMissing;
    cfg.auto_start_workers = false;
    cfg.decode_fn = [](const IconRef& ref, uint64_t, size_t) {
        if (ref.kind == IconKind::Option && ref.id == "node_purity/pure") {
            return makeDecodedPixel(77);
        }
        return DecodedIcon{};
    };
    cfg.upload_fn = [&](const uint8_t*, int, int) { return nextHandle++; };
    cfg.free_fn = [](TextureHandle) {};

    IconLoader loader(std::move(cfg));
    loader.beginFrame(1);

    // Empty ID returns missing placeholder with isReady == false.
    bool isReady = true;
    EXPECT_EQ(loader.get({"pkg", IconKind::Option, "", ""}, &isReady), kMissing);
    EXPECT_FALSE(isReady);

    // Present option: frame 1 miss enqueues and returns kLoading with isReady == false.
    isReady = true;
    EXPECT_EQ(loader.get({"pkg", IconKind::Option, "node_purity/pure", ""}, &isReady), kLoading);
    EXPECT_FALSE(isReady);

    // Repeating while queued/in-flight still returns kLoading with isReady == false.
    isReady = true;
    EXPECT_EQ(loader.get({"pkg", IconKind::Option, "node_purity/pure", ""}, &isReady), kLoading);
    EXPECT_FALSE(isReady);

    // Missing option: also enqueues on frame 1 with isReady == false.
    isReady = true;
    EXPECT_EQ(loader.get({"pkg", IconKind::Option, "node_purity/missing", ""}, &isReady), kLoading);
    EXPECT_FALSE(isReady);

    while (loader.stepWorkerOnce()) {}

    // Frame 2: uploads present option; caches absent option as Missing.
    loader.beginFrame(2);
    isReady = false;
    const TextureHandle pureHandle =
        loader.get({"pkg", IconKind::Option, "node_purity/pure", ""}, &isReady);
    EXPECT_GE(pureHandle, 500u);
    EXPECT_TRUE(isReady);

    isReady = true;
    EXPECT_EQ(loader.get({"pkg", IconKind::Option, "node_purity/missing", ""}, &isReady), kMissing);
    EXPECT_FALSE(isReady);
}

TEST(IconLoaderTest, MultiThreadedWorkerPoolConvergesAndShutsDownCleanly) {
    constexpr TextureHandle kLoading = 1;
    constexpr TextureHandle kMissing = 2;

    std::atomic<TextureHandle> nextHandle{1000};
    std::atomic<size_t> freedCount{0};

    IconLoaderConfig cfg;
    cfg.loading_placeholder = kLoading;
    cfg.missing_placeholder = kMissing;
    cfg.worker_count = 2;
    cfg.auto_start_workers = true;
    cfg.decode_fn = [](const IconRef& ref, uint64_t, size_t) {
        if (ref.id == "missing") return DecodedIcon{};
        return makeDecodedPixel(99);
    };
    cfg.upload_fn = [&](const uint8_t*, int, int) { return nextHandle.fetch_add(1); };
    cfg.free_fn = [&](TextureHandle) { freedCount.fetch_add(1); };

    IconLoader loader(std::move(cfg));
    uint64_t frame = 1;
    loader.beginFrame(frame);

    const int count = 40;
    for (int i = 0; i < count; ++i) {
        EXPECT_EQ(loader.get({"pkg", IconKind::Resource, "item_" + std::to_string(i), ""}), kLoading);
    }
    EXPECT_EQ(loader.get({"pkg", IconKind::Resource, "missing", ""}), kLoading);
    EXPECT_TRUE(loader.busy());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (loader.busy() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        loader.beginFrame(++frame);
    }
    ASSERT_FALSE(loader.busy());

    for (int i = 0; i < count; ++i) {
        EXPECT_GE(loader.get({"pkg", IconKind::Resource, "item_" + std::to_string(i), ""}), 1000u);
    }
    EXPECT_EQ(loader.get({"pkg", IconKind::Resource, "missing", ""}), kMissing);

    loader.shutdown();
    EXPECT_EQ(freedCount.load(), static_cast<size_t>(count));
}

// ============================================================================
// 2. IconSourceResolver — T8, "No Icons" Mode & Cross-Pack Fallback (§4.2, §4.3, §9)
// ============================================================================

TEST(IconSourceResolverTest, T8PrecedenceRootPakOverIconPakOverLoose) {
    TempDirGuard tmp("t8_order");
    const auto pkgRoot = tmp.path() / "pkg";
    const auto iconRoot = pkgRoot / "icons";

    const auto pngRootPak = makeTaggedPng1x1(10, 0, 0);
    const auto pngIconPak = makeTaggedPng1x1(20, 0, 0);
    const auto pngLoose = makeTaggedPng1x1(30, 0, 0);

    // root icons.pak has "iron"
    {
        std::string err;
        ASSERT_TRUE(gamedata::IconPakWriter::write(
            pkgRoot / "icons.pak",
            {gamedata::IconPakInput{"resources/iron", "iron", {}, pngRootPak, 1, 1}},
            &err)) << err;
    }
    // icon_root/icons.pak has "iron" (shadowed by root pak) and "copper"
    {
        std::string err;
        ASSERT_TRUE(gamedata::IconPakWriter::write(
            iconRoot / "icons.pak",
            {
                gamedata::IconPakInput{"resources/iron", "iron", {}, pngIconPak, 1, 1},
                gamedata::IconPakInput{"resources/copper", "copper", {}, pngIconPak, 1, 1},
            },
            &err)) << err;
    }
    // loose PNGs have "iron", "copper" (both shadowed), and "coal"
    ASSERT_TRUE(writeBinaryFile(iconRoot / "resources" / "iron.png", pngLoose));
    ASSERT_TRUE(writeBinaryFile(iconRoot / "resources" / "copper.png", pngLoose));
    ASSERT_TRUE(writeBinaryFile(iconRoot / "resources" / "coal.png", pngLoose));

    IconSourceResolver resolver;
    const std::string pkgId = resolver.registerPackage({
        "", "testgame", pkgRoot, iconRoot
    });

    WorkerPakStreams streams;
    auto rIron = resolver.resolve({pkgId, IconKind::Resource, "iron", "Iron"}, streams);
    ASSERT_TRUE(rIron.has_value());
    EXPECT_EQ(rIron->bytes, pngRootPak);
    EXPECT_FALSE(rIron->from_fallback);

    auto rCopper = resolver.resolve({pkgId, IconKind::Resource, "copper", "Copper"}, streams);
    ASSERT_TRUE(rCopper.has_value());
    EXPECT_EQ(rCopper->bytes, pngIconPak);
    EXPECT_FALSE(rCopper->from_fallback);

    auto rCoal = resolver.resolve({pkgId, IconKind::Resource, "coal", "Coal"}, streams);
    ASSERT_TRUE(rCoal.has_value());
    EXPECT_EQ(rCoal->bytes, pngLoose);
    EXPECT_FALSE(rCoal->from_fallback);

    auto rMissing = resolver.resolve({pkgId, IconKind::Resource, "uranium", "Uranium"}, streams);
    EXPECT_FALSE(rMissing.has_value());
}

TEST(IconSourceResolverTest, CorruptPakAndCorruptBlobHandledGracefully) {
    TempDirGuard tmp("corrupt_pak");
    const auto pkgRoot = tmp.path() / "pkg";
    const auto iconRoot = pkgRoot / "icons";

    // Write garbage bytes as <package_root>/icons.pak, and a valid loose PNG as fallback.
    ASSERT_TRUE(writeBinaryFile(pkgRoot / "icons.pak", {'B', 'A', 'D', '!', 1, 2, 3, 4}));
    const auto pngGood = makeTaggedPng1x1(77, 88, 99);
    ASSERT_TRUE(writeBinaryFile(iconRoot / "resources" / "good.png", pngGood));

    // Also build a pak with a corrupt blob in another package.
    const auto pakPkgRoot = tmp.path() / "pak_pkg";
    {
        std::string err;
        ASSERT_TRUE(gamedata::IconPakWriter::write(
            pakPkgRoot / "icons.pak",
            {
                gamedata::IconPakInput{"resources/badblob", "badblob", {}, {'N', 'O', 'T', 'P', 'N', 'G'}, 1, 1},
                gamedata::IconPakInput{"resources/okblob", "okblob", {}, pngGood, 1, 1},
            },
            &err)) << err;
    }

    IconSourceResolver resolver;
    const std::string pkgId1 = resolver.registerPackage({"", "g1", pkgRoot, iconRoot});
    const std::string pkgId2 = resolver.registerPackage({"", "g2", pakPkgRoot, pakPkgRoot / "icons"});

    WorkerPakStreams streams;
    auto r1 = resolver.resolve({pkgId1, IconKind::Resource, "good", "Good"}, streams);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->bytes, pngGood);

    // Corrupt blob returns missing while sibling valid blob succeeds.
    EXPECT_FALSE(resolver.resolve({pkgId2, IconKind::Resource, "badblob", "Bad"}, streams).has_value());
    auto rOk = resolver.resolve({pkgId2, IconKind::Resource, "okblob", "Ok"}, streams);
    ASSERT_TRUE(rOk.has_value());
    EXPECT_EQ(rOk->bytes, pngGood);
}

TEST(IconSourceResolverTest, NoIconsModeDoesZeroPerKeyFilesystemLookups) {
    TempDirGuard tmp("no_icons");
    const auto pkgRoot = tmp.path() / "iconless_pkg";
    std::filesystem::create_directories(pkgRoot);

    IconSourceResolver resolver;
    const std::string pkgId = resolver.registerPackage({
        "", "gtnh", pkgRoot, pkgRoot / "icons"
    });

    WorkerPakStreams streams;
    for (int i = 0; i < 100; ++i) {
        auto res = resolver.resolve({pkgId, IconKind::Resource, "res_" + std::to_string(i), "Res"}, streams);
        EXPECT_FALSE(res.has_value());
    }

    EXPECT_TRUE(resolver.isPackageIconless(pkgId));
    EXPECT_EQ(resolver.looseStatCount(), 0u);
    EXPECT_EQ(resolver.pakOpenCount(), 0u);
}

TEST(IconSourceResolverTest, CrossPackFallbackRulesAndPrecedence) {
    TempDirGuard tmp("fallback_rules");

    // Current package: game "gtnh", v2.7 (icon-less).
    const auto curRoot = tmp.path() / "gtnh" / "2.7";
    std::filesystem::create_directories(curRoot);

    // Same-game sibling 1 (scan order #1): game "gtnh", v2.6.
    const auto gtnhSiblingRoot = tmp.path() / "gtnh" / "2.6";
    const auto pngSameGame = makeTaggedPng1x1(11, 11, 11);
    {
        std::string err;
        ASSERT_TRUE(gamedata::IconPakWriter::write(
            gtnhSiblingRoot / "icons.pak",
            {
                gamedata::IconPakInput{"resources/same-game-item", "same-game-item", {}, pngSameGame, 1, 1},
            },
            &err)) << err;
    }

    // Priority #1 game: "factorio" (pak).
    const auto facRoot = tmp.path() / "factorio" / "2.0";
    const auto pngFacExact = makeTaggedPng1x1(22, 1, 1);
    const auto pngFacByNameA = makeTaggedPng1x1(22, 2, 1);
    const auto pngFacByNameB = makeTaggedPng1x1(22, 2, 2);
    const auto pngFacMach = makeTaggedPng1x1(22, 3, 1);
    const auto pngFacOpt = makeTaggedPng1x1(22, 4, 1);
    {
        std::string err;
        ASSERT_TRUE(gamedata::IconPakWriter::write(
            facRoot / "icons.pak",
            {
                // Same-game item also exists here to prove same-game wins over fallback.order!
                gamedata::IconPakInput{"resources/same-game-item", "same-game-item", {}, makeTaggedPng1x1(99, 0, 0), 1, 1},
                // Exact key vs name_slug test: exact key "resources/iron-plate" AND different key with same name_slug.
                gamedata::IconPakInput{"resources/iron-plate", "different-name", {}, pngFacExact, 1, 1},
                // Tie-breaking by key byte order within one pack: "a-copper" < "z-copper", both slug "copper-plate".
                gamedata::IconPakInput{"resources/a-copper", "copper-plate", {}, pngFacByNameA, 1, 1},
                gamedata::IconPakInput{"resources/z-copper", "copper-plate", {}, pngFacByNameB, 1, 1},
                // Machine with slug "smelter" (must NOT match a Resource named "Smelter").
                gamedata::IconPakInput{"machines/electric-furnace", "smelter", {}, pngFacMach, 1, 1},
                // Option icon (exact key only).
                gamedata::IconPakInput{"options/tiers/hv", "", {}, pngFacOpt, 1, 1},
            },
            &err)) << err;
    }

    // Priority #2 game: "satisfactory" (pak).
    const auto satRoot = tmp.path() / "satisfactory" / "1.1";
    const auto pngSatOnly = makeTaggedPng1x1(33, 1, 1);
    const auto pngSatCopper = makeTaggedPng1x1(33, 2, 1);
    {
        std::string err;
        ASSERT_TRUE(gamedata::IconPakWriter::write(
            satRoot / "icons.pak",
            {
                gamedata::IconPakInput{"resources/iron-plate", "iron-plate", {}, makeTaggedPng1x1(33, 9, 9), 1, 1},
                gamedata::IconPakInput{"resources/copper-plate", "copper-plate", {}, pngSatCopper, 1, 1},
                gamedata::IconPakInput{"resources/concrete", "concrete", {}, pngSatOnly, 1, 1},
                gamedata::IconPakInput{"resources/smelter-item", "smelter", {}, makeTaggedPng1x1(33, 7, 7), 1, 1},
            },
            &err)) << err;
    }

    // Priority #3 game: "loose_dev" (loose PNGs only -> matches by exact key ONLY, not name_slug).
    const auto looseRoot = tmp.path() / "loose_dev";
    const auto pngLooseKey = makeTaggedPng1x1(44, 1, 1);
    ASSERT_TRUE(writeBinaryFile(looseRoot / "icons" / "resources" / "dev-key.png", pngLooseKey));

    IconSourceResolver resolver;
    const std::string curId = resolver.registerPackage({"", "gtnh", curRoot, curRoot / "icons"});
    const std::vector<IconPackageDescriptor> scanned = {
        {curId, "gtnh", curRoot, curRoot / "icons"},
        {"", "gtnh", gtnhSiblingRoot, gtnhSiblingRoot / "icons"},
        {"", "factorio", facRoot, facRoot / "icons"},
        {"", "satisfactory", satRoot, satRoot / "icons"},
        {"", "loose_dev", looseRoot, looseRoot / "icons"},
    };
    resolver.setScannedPackages(scanned);

    WorkerPakStreams streams;

    // 1. Disabled by default: nothing resolves via fallback, and candidate paks are NOT opened!
    EXPECT_FALSE(resolver.resolve({curId, IconKind::Resource, "iron-plate", "Iron Plate"}, streams).has_value());
    EXPECT_EQ(resolver.pakOpenCount(), 0u);

    // 2. Enable fallback with order ["factorio", "satisfactory", "loose_dev"].
    IconFallbackSettings fb;
    fb.enabled = true;
    fb.order = {"factorio", "satisfactory", "loose_dev"};
    EXPECT_TRUE(resolver.setFallbackSettings(fb));

    // Same-game sibling pack wins over factorio (and only opens same-game pak lazily!).
    auto rSameGame = resolver.resolve({curId, IconKind::Resource, "same-game-item", "Same Game"}, streams);
    ASSERT_TRUE(rSameGame.has_value());
    EXPECT_TRUE(rSameGame->from_fallback);
    EXPECT_EQ(rSameGame->bytes, pngSameGame);
    EXPECT_EQ(resolver.pakOpenCount(), 1u); // factorio & satisfactory paks still unopened!

    // Priority order: factorio wins over satisfactory for exact key "iron-plate".
    auto rExact = resolver.resolve({curId, IconKind::Resource, "iron-plate", "Iron Plate"}, streams);
    ASSERT_TRUE(rExact.has_value());
    EXPECT_TRUE(rExact->from_fallback);
    EXPECT_EQ(rExact->bytes, pngFacExact);

    // Pack-first + name_slug tie-breaking by key byte order within factorio ("a-copper" < "z-copper").
    auto rNameTie = resolver.resolve({curId, IconKind::Resource, "gt_copper_plate", "Copper Plate"}, streams);
    ASSERT_TRUE(rNameTie.has_value());
    EXPECT_TRUE(rNameTie->from_fallback);
    EXPECT_EQ(rNameTie->bytes, pngFacByNameA);

    // Same kind only: Resource named "Smelter" does NOT match factorio's Machine "electric-furnace" (slug "smelter");
    // it skips factorio and matches satisfactory's Resource "smelter-item" (slug "smelter").
    auto rResSmelter = resolver.resolve({curId, IconKind::Resource, "my_smelter_res", "Smelter"}, streams);
    ASSERT_TRUE(rResSmelter.has_value());
    EXPECT_NE(rResSmelter->bytes, pngFacMach);
    EXPECT_EQ(rResSmelter->bytes, makeTaggedPng1x1(33, 7, 7));

    // Machine named "Smelter" DOES match factorio's Machine "electric-furnace".
    auto rMachSmelter = resolver.resolve({curId, IconKind::Machine, "gt_smelter", "Smelter"}, streams);
    ASSERT_TRUE(rMachSmelter.has_value());
    EXPECT_EQ(rMachSmelter->bytes, pngFacMach);

    // Options fall back ONLY by exact key (never by display name).
    auto rOptExact = resolver.resolve({curId, IconKind::Option, "tiers/hv", "HV"}, streams);
    ASSERT_TRUE(rOptExact.has_value());
    EXPECT_EQ(rOptExact->bytes, pngFacOpt);
    auto rOptNoNameMatch = resolver.resolve({curId, IconKind::Option, "other/hv", "Copper Plate"}, streams);
    EXPECT_FALSE(rOptNoNameMatch.has_value());

    // Loose-only candidate pack matches by exact key only.
    auto rLooseExact = resolver.resolve({curId, IconKind::Resource, "dev-key", "Anything"}, streams);
    ASSERT_TRUE(rLooseExact.has_value());
    EXPECT_EQ(rLooseExact->bytes, pngLooseKey);
    auto rLooseNoName = resolver.resolve({curId, IconKind::Resource, "other-key", "Dev Key"}, streams);
    EXPECT_FALSE(rLooseNoName.has_value());

    // Missing across all packs returns nullopt (-> unknown.png).
    EXPECT_FALSE(resolver.resolve({curId, IconKind::Resource, "totally-absent", "Totally Absent"}, streams).has_value());

    // Reorder priority: ["satisfactory", "factorio"] -> satisfactory now wins for "copper-plate" (exact key) and "iron-plate".
    fb.order = {"satisfactory", "factorio"};
    EXPECT_TRUE(resolver.setFallbackSettings(fb));
    auto rReordered = resolver.resolve({curId, IconKind::Resource, "gt_copper_plate", "Copper Plate"}, streams);
    ASSERT_TRUE(rReordered.has_value());
    EXPECT_EQ(rReordered->bytes, pngSatCopper);

    // Current package is NEVER used as its own fallback (even if same-key missing & same name_slug exists in self).
    const std::string facId = canonicalPackageId(facRoot, facRoot / "icons", "factorio");
    fb.order = {"factorio"};
    resolver.setFallbackSettings(fb);
    auto rSelfSkip = resolver.resolve({facId, IconKind::Resource, "nonexistent-key", "Copper Plate"}, streams);
    EXPECT_FALSE(rSelfSkip.has_value());
}

TEST(IconSourceResolverTest, SettingsChangeInvalidatesCachedFallbackAndMissingInLoader) {
    TempDirGuard tmp("fallback_invalidation");
    const auto curRoot = tmp.path() / "cur";
    const auto otherRoot = tmp.path() / "other";
    std::filesystem::create_directories(curRoot);

    const auto pngOther = makeTaggedPng1x1(66, 77, 88);
    {
        std::string err;
        ASSERT_TRUE(gamedata::IconPakWriter::write(
            otherRoot / "icons.pak",
            {gamedata::IconPakInput{"resources/plate", "plate", {}, pngOther, 1, 1}},
            &err)) << err;
    }

    IconSourceResolver resolver;
    const std::string curId = resolver.registerPackage({"", "curgame", curRoot, curRoot / "icons"});
    resolver.setScannedPackages(std::vector<IconPackageDescriptor>{
        {curId, "curgame", curRoot, curRoot / "icons"},
        {"", "othergame", otherRoot, otherRoot / "icons"},
    });

    WorkerPakStreams streams;
    TextureHandle nextHandle = 700;
    constexpr TextureHandle kLoading = 1;
    constexpr TextureHandle kMissing = 2;

    IconLoaderConfig cfg;
    cfg.loading_placeholder = kLoading;
    cfg.missing_placeholder = kMissing;
    cfg.auto_start_workers = false;
    cfg.decode_fn = [&](const IconRef& ref, uint64_t, size_t) {
        auto bytes = resolver.resolve(ref, streams);
        if (!bytes.has_value()) return DecodedIcon{};
        return makeDecodedPixel(bytes->bytes.size() & 0xFFu, bytes->from_fallback);
    };
    cfg.upload_fn = [&](const uint8_t*, int, int) { return nextHandle++; };

    IconLoader loader(std::move(cfg));
    loader.beginFrame(1);

    // Fallback OFF -> cached as Missing.
    EXPECT_EQ(loader.get({curId, IconKind::Resource, "plate", "Plate"}), kLoading);
    EXPECT_TRUE(loader.stepWorkerOnce());
    loader.beginFrame(2);
    EXPECT_EQ(loader.get({curId, IconKind::Resource, "plate", "Plate"}), kMissing);

    // Enable fallback + invalidateFallbackAndMissing -> re-resolves and loads from othergame!
    resolver.setFallbackSettings(IconFallbackSettings{true, {"othergame"}});
    loader.invalidateFallbackAndMissing();

    EXPECT_EQ(loader.get({curId, IconKind::Resource, "plate", "Plate"}), kLoading);
    EXPECT_TRUE(loader.stepWorkerOnce());
    loader.beginFrame(3);
    EXPECT_GE(loader.get({curId, IconKind::Resource, "plate", "Plate"}), 700u);
}

TEST(IconFallbackSettingsTest, MergeRemovesDeletedPacksAndPreservesInstalledOrder) {
    GameDataPackage pSat;
    pSat.uuid = "satisfactory";
    pSat.gameName = "Satisfactory";
    GameDataPackage pMind;
    pMind.uuid = "mindustry";
    pMind.gameName = "Mindustry";
    GameDataPackage pDsp;
    pDsp.uuid = "dsp";
    pDsp.gameName = "Dyson Sphere Program";

    // "gtnh" and "factorio" were deleted from disk; "satisfactory" kept priority; "mindustry" & "dsp" appended in scan order.
    const std::vector<std::string> savedOrder = {"gtnh", "satisfactory", "factorio", "satisfactory"};
    const std::vector<GameDataPackage> scanned = {pMind, pSat, pDsp, pSat};

    const auto merged = mergeIconFallbackOrder(savedOrder, scanned);
    ASSERT_EQ(merged.size(), 3u);
    EXPECT_EQ(merged[0], "satisfactory");
    EXPECT_EQ(merged[1], "mindustry");
    EXPECT_EQ(merged[2], "dsp");
}

TEST(IconFallbackSettingsTest, SettingsManagerJsonRoundTrip) {
    const AppSettings orig = SettingsManager::instance().getSettings();
    const std::filesystem::path settingsPath =
        getExecutableDirectory().value_or(std::filesystem::current_path()) / "app_settings.json";
    std::optional<std::string> origFileContent;
    if (std::filesystem::exists(settingsPath)) {
        std::ifstream ifs(settingsPath, std::ios::binary);
        origFileContent = std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }

    struct Cleanup {
        AppSettings origSettings;
        std::filesystem::path path;
        std::optional<std::string> origContent;
        ~Cleanup() {
            SettingsManager::instance().setSettings(origSettings);
            std::error_code ec;
            if (origContent.has_value()) {
                atomicWriteFile(path, *origContent);
            } else {
                std::filesystem::remove(path, ec);
            }
        }
    } guard{orig, settingsPath, origFileContent};

    AppSettings s = orig;
    s.iconFallback.enabled = true;
    s.iconFallback.order = {"gtnh", "factorio", "satisfactory"};
    SettingsManager::instance().setSettings(s);
    SettingsManager::instance().save();

    // Verify JSON structure matches §4.3 ("icon_fallback": { "enabled": ..., "order": [...] }).
    {
        std::ifstream ifs(settingsPath);
        ASSERT_TRUE(ifs.is_open());
        nlohmann::json j = nlohmann::json::parse(ifs);
        ASSERT_TRUE(j.contains("icon_fallback"));
        EXPECT_TRUE(j["icon_fallback"]["enabled"].get<bool>());
        EXPECT_EQ(j["icon_fallback"]["order"].get<std::vector<std::string>>(),
                  (std::vector<std::string>{"gtnh", "factorio", "satisfactory"}));
    }

    // Reset in-memory settings and reload from disk.
    AppSettings reset = orig;
    reset.iconFallback = {};
    SettingsManager::instance().setSettings(reset);
    SettingsManager::instance().load();

    const auto& loaded = SettingsManager::instance().getSettings().iconFallback;
    EXPECT_TRUE(loaded.enabled);
    EXPECT_EQ(loaded.order, (std::vector<std::string>{"gtnh", "factorio", "satisfactory"}));
}

// ============================================================================
// 3. RecipeSearchIndex & Synthetic 50k Picker Benchmark (§4.4, §4.5, §9)
// ============================================================================

TEST(RecipeSearchIndexTest, MatchesLegacyLinearFilterOnRealFixtures) {
    const std::vector<std::string> fixtures = {"satisfactory", "mindustry"};
    for (const auto& name : fixtures) {
        const std::filesystem::path dir = findRepoDataDir(name);
        ASSERT_FALSE(dir.empty()) << "Could not locate fixture data/game_data/" << name;

        gamedata::GamePackage pkg;
        gamedata::LoadReport report;
        ASSERT_TRUE(gamedata::loadGamePackage(dir, pkg, report)) << "Failed loading " << dir;
        ASSERT_FALSE(pkg.recipes.empty());

        const gamedata::RecipeSearchIndex index = gamedata::RecipeSearchIndex::build(pkg);
        EXPECT_EQ(index.recipeCount(), pkg.recipes.size());

        const std::vector<std::string> queries = {
            "", "iron", "IRON", "plate", "water", "smelt", "copper", "zz_no_match_zz"
        };

        // Pick a couple of real resources for producing/consuming context parity checks.
        std::vector<std::string> sampleRes;
        for (size_t i = 0; i < std::min<size_t>(pkg.resources.size(), 5); ++i) {
            sampleRes.push_back(pkg.resources[i].id);
        }

        gamedata::RecipeSearchState state;
        for (const auto& q : queries) {
            state.reset();
            state.update(index, q, gamedata::RecipeSearchContextKind::All);
            EXPECT_EQ(state.results(),
                      legacyLinearRecipeFilter(pkg, q, gamedata::RecipeSearchContextKind::All, ""))
                << "Fixture=" << name << " query=" << q;

            for (const auto& resId : sampleRes) {
                state.reset();
                state.update(index, q, gamedata::RecipeSearchContextKind::ProducingResource, resId);
                EXPECT_EQ(state.results(),
                          legacyLinearRecipeFilter(pkg, q, gamedata::RecipeSearchContextKind::ProducingResource, resId))
                    << "Fixture=" << name << " Producing=" << resId << " query=" << q;

                state.reset();
                state.update(index, q, gamedata::RecipeSearchContextKind::ConsumingResource, resId);
                EXPECT_EQ(state.results(),
                          legacyLinearRecipeFilter(pkg, q, gamedata::RecipeSearchContextKind::ConsumingResource, resId))
                    << "Fixture=" << name << " Consuming=" << resId << " query=" << q;
            }
        }
    }
}

TEST(RecipeSearchIndexTest, SeparatorPreventsCrossFieldFalseMatches) {
    gamedata::GamePackage pkg;
    gamedata::Resource r1;
    r1.id = "ore";
    r1.name = "Alpha";
    gamedata::Resource r2;
    r2.id = "ingot";
    r2.name = "Beta";
    pkg.resources = {r1, r2};
    pkg.resource_index["ore"] = 0;
    pkg.resource_index["ingot"] = 1;

    gamedata::Recipe rec;
    rec.id = "r0";
    rec.name = "Craft";
    gamedata::RecipePort inP;
    inP.dir = "in";
    inP.res = "ore";
    gamedata::RecipePort outP;
    outP.dir = "out";
    outP.res = "ingot";
    rec.in = {inP};
    rec.out = {outP};
    pkg.recipes = {rec};

    const auto index = gamedata::RecipeSearchIndex::build(pkg);
    EXPECT_TRUE(index.matchesLower(0, "craft"));
    EXPECT_TRUE(index.matchesLower(0, "alpha"));
    EXPECT_TRUE(index.matchesLower(0, "beta"));
    // Cross-boundary substrings must NOT match across separator NULs.
    EXPECT_FALSE(index.matchesLower(0, "ftal"));
    EXPECT_FALSE(index.matchesLower(0, "phabe"));
}

TEST(RecipeSearchIndexTest, IncrementalNarrowingAndContextLookup) {
    gamedata::GamePackage pkg;
    gamedata::Resource rIron;
    rIron.id = "iron";
    rIron.name = "Iron Ore";
    gamedata::Resource rPlate;
    rPlate.id = "plate";
    rPlate.name = "Iron Plate";
    gamedata::Resource rCopper;
    rCopper.id = "copper";
    rCopper.name = "Copper Ore";
    pkg.resources = {rIron, rPlate, rCopper};
    pkg.resource_index["iron"] = 0;
    pkg.resource_index["plate"] = 1;
    pkg.resource_index["copper"] = 2;

    auto makeRec = [](std::string id, std::string name, std::vector<std::string> ins, std::vector<std::string> outs) {
        gamedata::Recipe r;
        r.id = std::move(id);
        r.name = std::move(name);
        for (const auto& inId : ins) {
            gamedata::RecipePort p;
            p.dir = "in";
            p.res = inId;
            r.in.push_back(std::move(p));
        }
        for (const auto& outId : outs) {
            gamedata::RecipePort p;
            p.dir = "out";
            p.res = outId;
            r.out.push_back(std::move(p));
        }
        return r;
    };

    pkg.recipes.push_back(makeRec("r0", "Smelt Iron", {"iron", "iron"}, {"plate"})); // duplicate input port
    pkg.recipes.push_back(makeRec("r1", "Smelt Copper", {"copper"}, {"copper"}));
    pkg.recipes.push_back(makeRec("r2", "Press Iron Plate", {"iron"}, {"plate", "plate"})); // duplicate output port

    const auto index = gamedata::RecipeSearchIndex::build(pkg);

    // Context lookups are deduplicated and preserve package order.
    EXPECT_EQ(index.consuming("iron"), (std::vector<uint32_t>{0, 2}));
    EXPECT_EQ(index.producing("plate"), (std::vector<uint32_t>{0, 2}));
    EXPECT_EQ(index.consuming("copper"), (std::vector<uint32_t>{1}));
    EXPECT_TRUE(index.producing("nonexistent").empty());

    // Incremental narrowing check.
    gamedata::RecipeSearchState state;
    EXPECT_TRUE(state.update(index, "i"));
    EXPECT_FALSE(state.lastSearchWasIncremental());
    EXPECT_EQ(state.lastCandidateCount(), 3u);
    EXPECT_EQ(state.results(), (std::vector<uint32_t>{0, 2}));

    // Unchanged query returns false (no recompute).
    EXPECT_FALSE(state.update(index, "I"));

    // Extending query ("i" -> "iron p") narrows incrementally from 2 previous results.
    EXPECT_TRUE(state.update(index, "iron p"));
    EXPECT_TRUE(state.lastSearchWasIncremental());
    EXPECT_EQ(state.lastCandidateCount(), 2u);
    EXPECT_EQ(state.results(), (std::vector<uint32_t>{0, 2}));

    // Extending further ("iron p" -> "press iron p") narrows again.
    EXPECT_TRUE(state.update(index, "press iron p"));
    EXPECT_TRUE(state.lastSearchWasIncremental());
    EXPECT_EQ(state.lastCandidateCount(), 2u);
    EXPECT_EQ(state.results(), (std::vector<uint32_t>{2}));

    // Backspace ("press iron p" -> "smelt") resets to full scan.
    EXPECT_TRUE(state.update(index, "smelt"));
    EXPECT_FALSE(state.lastSearchWasIncremental());
    EXPECT_EQ(state.lastCandidateCount(), 3u);
    EXPECT_EQ(state.results(), (std::vector<uint32_t>{0, 1}));

    // Context switch resets to direct context candidate list.
    EXPECT_TRUE(state.update(index, "smelt", gamedata::RecipeSearchContextKind::ProducingResource, "plate"));
    EXPECT_FALSE(state.lastSearchWasIncremental());
    EXPECT_EQ(state.lastCandidateCount(), 2u);
    EXPECT_EQ(state.results(), (std::vector<uint32_t>{0}));
}

TEST(RecipeSearchIndexTest, Synthetic50kPickerBenchmark) {
    TempDirGuard tmp("synthetic_50k_picker");
    test_helpers::SyntheticPackageConfig cfg;
    cfg.recipe_count = 50000;
    cfg.resource_count = 3500;
    cfg.machine_count = 200;

    const auto gen = test_helpers::writeSyntheticPackage(tmp.path(), cfg);
    ASSERT_TRUE(gen.ok) << gen.error;

    gamedata::GamePackage pkg;
    gamedata::LoadReport report;
    ASSERT_TRUE(gamedata::loadGamePackage(tmp.path(), pkg, report));
    ASSERT_EQ(pkg.recipes.size(), 50000u);

    auto msSince = [](std::chrono::steady_clock::time_point t0) {
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
    };

    const auto tBuild = std::chrono::steady_clock::now();
    const gamedata::RecipeSearchIndex index = gamedata::RecipeSearchIndex::build(pkg);
    const double buildMs = msSince(tBuild);

    gamedata::RecipeSearchState state;

    // 1. Cold full-index filter ("iron")
    const auto tCold = std::chrono::steady_clock::now();
    state.update(index, "iron");
    const double coldFilterMs = msSince(tCold);
    const size_t ironMatches = state.results().size();
    EXPECT_GT(ironMatches, 0u);
    EXPECT_FALSE(state.lastSearchWasIncremental());

    // 2. Incremental keystroke narrowing ("iron" -> "iron plate" -> "iron plate 1")
    const auto tInc1 = std::chrono::steady_clock::now();
    state.update(index, "iron plate");
    const double inc1Ms = msSince(tInc1);
    const size_t ironPlateMatches = state.results().size();
    EXPECT_TRUE(state.lastSearchWasIncremental());
    EXPECT_GT(ironPlateMatches, 0u);

    const auto tInc2 = std::chrono::steady_clock::now();
    state.update(index, "iron plate 1");
    const double inc2Ms = msSince(tInc2);
    const size_t ironPlate1Matches = state.results().size();
    EXPECT_TRUE(state.lastSearchWasIncremental());

    // 3. Port-drag context filter (direct producing lookup + substring filter)
    const auto tCtx = std::chrono::steady_clock::now();
    state.update(index, "process", gamedata::RecipeSearchContextKind::ProducingResource, "res-0001");
    const double ctxFilterMs = msSince(tCtx);
    const size_t ctxMatches = state.results().size();
    EXPECT_GT(ctxMatches, 0u);

    // Report timing metrics (not brittle-asserted per §9).
    RecordProperty("recipes", static_cast<int>(pkg.recipes.size()));
    RecordProperty("index_build_ms", std::to_string(buildMs));
    RecordProperty("cold_filter_iron_ms", std::to_string(coldFilterMs));
    RecordProperty("incremental_filter_iron_plate_ms", std::to_string(inc1Ms));
    RecordProperty("incremental_filter_iron_plate_1_ms", std::to_string(inc2Ms));
    RecordProperty("context_filter_producing_ms", std::to_string(ctxFilterMs));

    std::cout << std::fixed << std::setprecision(3)
              << "[Synthetic50kPickerBenchmark] recipes=" << pkg.recipes.size()
              << " build=" << buildMs << " ms"
              << " | cold('iron')=" << coldFilterMs << " ms (" << ironMatches << " matches)"
              << " | inc('iron plate')=" << inc1Ms << " ms (" << ironPlateMatches << " matches)"
              << " | inc('iron plate 1')=" << inc2Ms << " ms (" << ironPlate1Matches << " matches)"
              << " | context(producing 'res-0001', 'process')=" << ctxFilterMs
              << " ms (" << ctxMatches << " matches)\n";
}

TEST(RecipeSearchIndexTest, VisibilityFilterUpdatesOnRevisionAndRespectsContext) {
    gamedata::GamePackage pkg;
    auto addRes = [&](const std::string& id, const std::string& name) {
        gamedata::Resource r;
        r.id = id;
        r.name = name;
        pkg.resource_index[id] = pkg.resources.size();
        pkg.resources.push_back(std::move(r));
    };
    addRes("ore", "Iron Ore");
    addRes("plate", "Iron Plate");

    auto makeRec = [](const std::string& id, const std::string& name,
                      const std::vector<std::string>& ins,
                      const std::vector<std::string>& outs) {
        gamedata::Recipe r;
        r.id = id;
        r.name = name;
        for (const auto& s : ins) {
            gamedata::RecipePort p;
            p.dir = "in";
            p.res = s;
            r.in.push_back(std::move(p));
        }
        for (const auto& s : outs) {
            gamedata::RecipePort p;
            p.dir = "out";
            p.res = s;
            r.out.push_back(std::move(p));
        }
        return r;
    };
    pkg.recipes.push_back(makeRec("r0", "Smelt Iron", {"ore"}, {"plate"}));
    pkg.recipes.push_back(makeRec("r1", "Debug Source Iron", {}, {"plate"}));

    const auto index = gamedata::RecipeSearchIndex::build(pkg);

    gamedata::EntityVisibility vis;
    vis.all_visible = false;
    vis.revision = 1;
    vis.recipe_visible = {1, 0};

    gamedata::RecipeSearchState state;
    // Empty query with visibility hides r1
    EXPECT_TRUE(state.update(index, "", gamedata::RecipeSearchContextKind::All, {}, &vis));
    EXPECT_EQ(state.results(), (std::vector<uint32_t>{0}));
    // Same revision + same query -> no recompute
    EXPECT_FALSE(state.update(index, "", gamedata::RecipeSearchContextKind::All, {}, &vis));

    // Port-drag context also excludes hidden recipe r1
    EXPECT_TRUE(state.update(index, "", gamedata::RecipeSearchContextKind::ProducingResource, "plate", &vis));
    EXPECT_EQ(state.results(), (std::vector<uint32_t>{0}));

    // Toggling visibility (new revision) invalidates cache and reveals r1
    vis.revision = 2;
    vis.recipe_visible = {1, 1};
    EXPECT_TRUE(state.update(index, "", gamedata::RecipeSearchContextKind::ProducingResource, "plate", &vis));
    EXPECT_EQ(state.results(), (std::vector<uint32_t>{0, 1}));

    // Incremental narrowing stays consistent within the same visibility revision
    EXPECT_TRUE(state.update(index, "iron", gamedata::RecipeSearchContextKind::All, {}, &vis));
    EXPECT_EQ(state.results(), (std::vector<uint32_t>{0, 1}));
    EXPECT_TRUE(state.update(index, "debug", gamedata::RecipeSearchContextKind::All, {}, &vis));
    EXPECT_EQ(state.results(), (std::vector<uint32_t>{1}));

    // Hiding r1 again drops it even for an unchanged query string
    vis.revision = 3;
    vis.recipe_visible = {1, 0};
    EXPECT_TRUE(state.update(index, "debug", gamedata::RecipeSearchContextKind::All, {}, &vis));
    EXPECT_TRUE(state.results().empty());
}

} // namespace
