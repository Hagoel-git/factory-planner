#include <gtest/gtest.h>

#include "SyntheticPackageGenerator.h"
#include "common/FilesystemUtils.h"
#include "core/data/GameDataLoader.h"
#include "core/data/ReportGrouping.h"
#include "tools/gamedata_tool/Commands.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
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
                 ("fp_gamedata_tool_test_" + prefix + "_" +
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

uint32_t readU32BE(const std::string& bytes, size_t offset) {
    return (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset])) << 24) |
           (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 8) |
           static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 3]));
}

std::string readFileBytes(const std::filesystem::path& p) {
    std::ifstream ifs(p, std::ios::binary);
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

// ============================================================================
// 1. CLI Skeleton, Version, Help & Exit Codes
// ============================================================================

TEST(GameDataToolCliTest, VersionHelpAndUsageErrors) {
    auto ver = runCli({"--version"});
    EXPECT_EQ(ver.code, kExitOk);
    EXPECT_NE(ver.out.find("gamedata_tool"), std::string::npos);
    EXPECT_NE(ver.out.find("schema_version 4"), std::string::npos);
    EXPECT_TRUE(ver.err.empty());

    auto help = runCli({"--help"});
    EXPECT_EQ(help.code, kExitOk);
    EXPECT_NE(help.out.find("validate"), std::string::npos);

    auto empty = runCli({});
    EXPECT_EQ(empty.code, kExitUsage);
    EXPECT_FALSE(empty.err.empty());

    auto unknownCmd = runCli({"bogus"});
    EXPECT_EQ(unknownCmd.code, kExitUsage);

    auto unknownFlag = runCli({"validate", "foo", "--not-a-flag"});
    EXPECT_EQ(unknownFlag.code, kExitUsage);

    auto missingPkg = runCli({"validate"});
    EXPECT_EQ(missingPkg.code, kExitUsage);
}

// ============================================================================
// 2. `init` command (§9)
// ============================================================================

TEST(GameDataToolInitTest, ScaffoldValidatesCleanlyWithNoWarningsAndWerror) {
    TempDirGuard tmp("init_clean");
    const std::filesystem::path pkgDir = tmp.path() / "my_factory_game";

    auto initRes = runCli({"init", pathToUtf8(pkgDir)});
    ASSERT_EQ(initRes.code, kExitOk) << initRes.err;
    EXPECT_TRUE(std::filesystem::exists(pkgDir / "game.json"));
    EXPECT_TRUE(std::filesystem::exists(pkgDir / ".gitignore"));
    EXPECT_FALSE(std::filesystem::exists(pkgDir / "icons"));

    const std::string gitignore = readFileBytes(pkgDir / ".gitignore");
    EXPECT_NE(gitignore.find("icons/"), std::string::npos);
    EXPECT_NE(gitignore.find("icons.pak"), std::string::npos);
    EXPECT_NE(gitignore.find(".tmp_icons/"), std::string::npos);

    // Validate default scaffold: 0 errors, 0 warnings, 1 "no icons" info note, passes --werror!
    auto valRes = runCli({"validate", pathToUtf8(pkgDir), "--werror", "--json"});
    ASSERT_EQ(valRes.code, kExitOk) << valRes.out << valRes.err;

    nlohmann::json j = nlohmann::json::parse(valRes.out);
    EXPECT_EQ(j["report_version"], 1);
    EXPECT_TRUE(j["ok"].get<bool>());
    EXPECT_FALSE(j["fatal"].get<bool>());
    EXPECT_EQ(j["counts"]["errors"], 0);
    EXPECT_EQ(j["counts"]["warnings"], 0);
    EXPECT_EQ(j["counts"]["notes"], 1);
    ASSERT_EQ(j["notes"].size(), 1u);
    EXPECT_NE(j["notes"][0].get<std::string>().find("package has no icons"), std::string::npos);
}

TEST(GameDataToolInitTest, IconsFlagWritesValidSquarePngs) {
    TempDirGuard tmp("init_icons");
    const std::filesystem::path pkgDir = tmp.path() / "with_icons";

    auto initRes = runCli({
        "init", pathToUtf8(pkgDir),
        "--id", "demo-game",
        "--name", "Demo Game",
        "--time", "tick",
        "--tps", "20",
        "--rate", "tick",
        "--icons"
    });
    ASSERT_EQ(initRes.code, kExitOk) << initRes.err;

    const std::vector<std::filesystem::path> expectedIcons = {
        pkgDir / "icons" / "resources" / "iron-ore.png",
        pkgDir / "icons" / "resources" / "iron-ingot.png",
        pkgDir / "icons" / "machines" / "smelter.png"
    };
    for (const auto& iconPath : expectedIcons) {
        ASSERT_TRUE(std::filesystem::is_regular_file(iconPath)) << iconPath;
        const std::string bytes = readFileBytes(iconPath);
        ASSERT_GE(bytes.size(), 33u);
        // Verify PNG magic signature
        EXPECT_EQ(static_cast<unsigned char>(bytes[0]), 0x89);
        EXPECT_EQ(bytes.substr(1, 3), "PNG");
        // Verify IHDR chunk and square <= 64x64 dimensions
        EXPECT_EQ(bytes.substr(12, 4), "IHDR");
        const uint32_t w = readU32BE(bytes, 16);
        const uint32_t h = readU32BE(bytes, 20);
        EXPECT_GT(w, 0u);
        EXPECT_EQ(w, h);
        EXPECT_LE(w, 64u);
    }

    auto valRes = runCli({"validate", pathToUtf8(pkgDir), "--werror", "--json"});
    ASSERT_EQ(valRes.code, kExitOk) << valRes.out;
    nlohmann::json j = nlohmann::json::parse(valRes.out);
    EXPECT_EQ(j["game_id"], "demo-game");
    EXPECT_EQ(j["counts"]["errors"], 0);
    EXPECT_EQ(j["counts"]["warnings"], 0);
    EXPECT_EQ(j["counts"]["notes"], 0); // Has icons/ dir, so not in "no icons" mode
}

TEST(GameDataToolInitTest, RefusesNonEmptyDirectoryWithoutForce) {
    TempDirGuard tmp("init_nonempty");
    const std::filesystem::path pkgDir = tmp.path() / "occupied";
    ASSERT_TRUE(atomicWriteFile(pkgDir / "existing.txt", "keep me"));

    auto refused = runCli({"init", pathToUtf8(pkgDir)});
    EXPECT_NE(refused.code, kExitOk);
    EXPECT_FALSE(std::filesystem::exists(pkgDir / "game.json"));

    auto forced = runCli({"init", pathToUtf8(pkgDir), "--force"});
    EXPECT_EQ(forced.code, kExitOk) << forced.err;
    EXPECT_TRUE(std::filesystem::exists(pkgDir / "game.json"));
}

// ============================================================================
// 3. `validate` & `ReportGrouping` (§9)
// ============================================================================

TEST(GameDataToolValidateTest, BrokenProfileYieldsOneGroupAndOneCascade) {
    TempDirGuard tmp("val_cascade");
    const std::filesystem::path pkgDir = tmp.path();

    // 1 broken profile -> 3 machines -> 15 recipes dropped in cascade
    std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "gtnh", "name": "GTNH" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [
        { "id": "ore", "name": "Ore" },
        { "id": "dust", "name": "Dust" }
      ],
      "parameters": {
        "tier": { "type": "int", "default": 1, "min": "1", "max": "8" }
      },
      "profiles": {
        "gt_single": {
          "params": ["tier"],
          "let": { "oc": "param.teir * 2" }
        }
      },
      "machines": [
        { "id": "mac-1", "name": "M1", "profile": "gt_single", "categories": ["cat-a"] },
        { "id": "mac-2", "name": "M2", "profile": "gt_single", "categories": ["cat-b"] },
        { "id": "mac-3", "name": "M3", "profile": "gt_single", "categories": ["cat-c"] }
      ],
      "recipes": [
)JSON";
    for (int i = 0; i < 15; ++i) {
        const char* cat = (i % 3 == 0) ? "cat-a" : ((i % 3 == 1) ? "cat-b" : "cat-c");
        manifest += "        {\"id\":\"rec-" + std::to_string(i) + "\",\"category\":\"" + cat +
                    "\",\"time\":1,\"in\":[[\"ore\",1]],\"out\":[[\"dust\",1]]}";
        if (i + 1 < 15) manifest += ",";
        manifest += "\n";
    }
    manifest += "      ]\n    }\n";
    ASSERT_TRUE(atomicWriteFile(pkgDir / "game.json", manifest));

    auto res = runCli({"validate", pathToUtf8(pkgDir), "--no-icons", "--json"});
    ASSERT_EQ(res.code, kExitError) << res.out << res.err;

    nlohmann::json j = nlohmann::json::parse(res.out);
    EXPECT_EQ(j["report_version"], 1);
    EXPECT_EQ(j["game_id"], "gtnh");
    EXPECT_FALSE(j["ok"].get<bool>());
    EXPECT_FALSE(j["fatal"].get<bool>());

    // 1 error group (NOT 19 errors!) + 1 cascade
    ASSERT_EQ(j["groups"].size(), 1u);
    EXPECT_EQ(j["groups"][0]["count"], 1);
    EXPECT_EQ(j["groups"][0]["rule"], "V-EXPR-5");
    EXPECT_EQ(j["groups"][0]["subject"], "param.teir");

    ASSERT_EQ(j["cascades"].size(), 1u);
    EXPECT_EQ(j["cascades"][0]["root"], "profile:gt_single");
    EXPECT_EQ(j["cascades"][0]["rule"], "V-EXPR-5");
    EXPECT_EQ(j["cascades"][0]["dropped"]["machine"], 3);
    EXPECT_EQ(j["cascades"][0]["dropped"]["recipe"], 15);
    EXPECT_FALSE(j["cascades"][0]["sample_ids"].empty());

    // Human output includes cascade arrow line
    auto human = runCli({"validate", pathToUtf8(pkgDir), "--no-icons"});
    EXPECT_EQ(human.code, kExitError);
    EXPECT_NE(human.out.find("profile gt_single"), std::string::npos);
    EXPECT_NE(human.out.find("3 machines"), std::string::npos);
    EXPECT_NE(human.out.find("15 recipes"), std::string::npos);
}

TEST(GameDataToolValidateTest, ThousandCopiesOfOneBadResourceIdGroupIntoOneSubjectGroup) {
    TempDirGuard tmp("val_subject_1000");
    const std::filesystem::path pkgDir = tmp.path();

    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "subj-test", "name": "Subject Test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [
        { "id": "crushed_copper", "name": "Crushed Copper" },
        { "id": "dust_copper", "name": "Copper Dust" }
      ],
      "machines": [
        { "id": "macerator", "name": "Macerator", "categories": ["mac"] }
      ],
      "include": ["recipes/"]
    })JSON";
    ASSERT_TRUE(atomicWriteFile(pkgDir / "game.json", manifest));

    std::string shard = "{\"recipes\":[\n";
    for (int i = 0; i < 1000; ++i) {
        shard += "{\"id\":\"r-" + std::to_string(i) +
                 "\",\"category\":\"mac\",\"time\":1,\"in\":[[\"crushed_coper\",1]],\"out\":[[\"dust_copper\",1]]}";
        if (i + 1 < 1000) shard += ",";
        shard += "\n";
    }
    shard += "]}\n";
    ASSERT_TRUE(atomicWriteFile(pkgDir / "recipes" / "gt_macerator.json", shard));

    auto res = runCli({"validate", pathToUtf8(pkgDir), "--no-icons", "--json"});
    ASSERT_EQ(res.code, kExitError);

    nlohmann::json j = nlohmann::json::parse(res.out);
    EXPECT_EQ(j["counts"]["errors"], 1000);
    ASSERT_EQ(j["groups"].size(), 1u);
    EXPECT_EQ(j["groups"][0]["severity"], "error");
    EXPECT_EQ(j["groups"][0]["rule"], "V-REF-1");
    EXPECT_EQ(j["groups"][0]["subject"], "crushed_coper");
    EXPECT_TRUE(j["groups"][0]["pattern"].is_null());
    EXPECT_EQ(j["groups"][0]["count"], 1000);
    EXPECT_EQ(j["groups"][0]["suggestion"], "crushed_copper");
    EXPECT_EQ(j["groups"][0]["examples"].size(), 3u);
}

TEST(GameDataToolValidateTest, ThousandDistinctBadIdsGroupIntoOnePatternGroup) {
    TempDirGuard tmp("val_pattern_1000");
    const std::filesystem::path pkgDir = tmp.path();

    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "pat-test", "name": "Pattern Test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [
        { "id": "ore", "name": "Ore" },
        { "id": "plate", "name": "Plate" }
      ],
      "machines": [
        { "id": "smelter", "name": "Smelter", "categories": ["smelt"] }
      ],
      "include": ["recipes/"]
    })JSON";
    ASSERT_TRUE(atomicWriteFile(pkgDir / "game.json", manifest));

    // Spread 1000 distinct bad recipe IDs across 4 shards
    for (int s = 0; s < 4; ++s) {
        std::string shard = "{\"recipes\":[\n";
        for (int i = 0; i < 250; ++i) {
            const int idx = s * 250 + i;
            shard += "{\"id\":\"Bad Recipe " + std::to_string(idx) +
                     "\",\"category\":\"smelt\",\"time\":1,\"in\":[[\"ore\",1]],\"out\":[[\"plate\",1]]}";
            if (i + 1 < 250) shard += ",";
            shard += "\n";
        }
        shard += "]}\n";
        ASSERT_TRUE(atomicWriteFile(pkgDir / "recipes" / ("shard_" + std::to_string(s) + ".json"), shard));
    }

    auto res = runCli({"validate", pathToUtf8(pkgDir), "--no-icons", "--json"});
    ASSERT_EQ(res.code, kExitError);

    nlohmann::json j = nlohmann::json::parse(res.out);
    EXPECT_EQ(j["counts"]["errors"], 1000);
    ASSERT_EQ(j["groups"].size(), 1u);
    EXPECT_EQ(j["groups"][0]["rule"], "V-ID-1");
    EXPECT_TRUE(j["groups"][0]["subject"].is_null());
    ASSERT_TRUE(j["groups"][0]["pattern"].is_string());
    const std::string pattern = j["groups"][0]["pattern"].get<std::string>();
    EXPECT_NE(pattern.find("recipes/"), std::string::npos);
    EXPECT_NE(pattern.find("*.json"), std::string::npos);
    EXPECT_NE(pattern.find("/recipes/*/id"), std::string::npos);
    EXPECT_EQ(j["groups"][0]["count"], 1000);
    EXPECT_EQ(j["groups"][0]["examples"].size(), 3u);
}

TEST(GameDataToolValidateTest, JsonStableKeysAllFlagExitCodesAndWerror) {
    TempDirGuard tmp("val_codes");

    // 1. Fatal error (missing manifest) -> exit 2
    auto fatalMissing = runCli({"validate", pathToUtf8(tmp.path() / "does_not_exist"), "--json"});
    EXPECT_EQ(fatalMissing.code, kExitFatal);
    nlohmann::json jf = nlohmann::json::parse(fatalMissing.out);
    EXPECT_EQ(jf["report_version"], 1);
    EXPECT_FALSE(jf["ok"].get<bool>());
    EXPECT_TRUE(jf["fatal"].get<bool>());
    EXPECT_TRUE(jf.contains("package"));
    EXPECT_TRUE(jf.contains("game_id"));
    EXPECT_TRUE(jf.contains("degraded"));
    EXPECT_TRUE(jf.contains("counts"));
    EXPECT_TRUE(jf.contains("groups"));
    EXPECT_TRUE(jf.contains("cascades"));
    EXPECT_TRUE(jf.contains("notes"));
    EXPECT_TRUE(jf.contains("timing_ms"));
    EXPECT_TRUE(jf["timing_ms"].contains("load"));

    // 2. Package with 1 warning (unused resource) and no icons:
    //    - without --werror -> exit 0
    //    - with --werror    -> exit 1
    const std::filesystem::path warnDir = tmp.path() / "warn_pkg";
    const std::string warnManifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "warn-game", "name": "Warn Game" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [
        { "id": "ore", "name": "Ore" },
        { "id": "plate", "name": "Plate" },
        { "id": "unused-item", "name": "Unused" }
      ],
      "machines": [{ "id": "smelter", "categories": ["smelt"] }],
      "recipes": [{ "id": "r1", "category": "smelt", "time": 1, "in": [["ore", 1]], "out": [["plate", 1]] }]
    })JSON";
    ASSERT_TRUE(atomicWriteFile(warnDir / "game.json", warnManifest));

    auto okWithWarn = runCli({"validate", pathToUtf8(warnDir), "--json", "--all"});
    EXPECT_EQ(okWithWarn.code, kExitOk);
    nlohmann::json jw = nlohmann::json::parse(okWithWarn.out);
    EXPECT_TRUE(jw["ok"].get<bool>());
    EXPECT_EQ(jw["counts"]["warnings"], 1);
    EXPECT_EQ(jw["counts"]["notes"], 1);
    ASSERT_TRUE(jw.contains("diagnostics"));
    EXPECT_EQ(jw["diagnostics"].size(), 1u);

    auto failWerror = runCli({"validate", pathToUtf8(warnDir), "--werror", "--json"});
    EXPECT_EQ(failWerror.code, kExitError);
    nlohmann::json jwe = nlohmann::json::parse(failWerror.out);
    EXPECT_FALSE(jwe["ok"].get<bool>());

    // 3. No warnings + "no icons" mode -> note only, exit 0 even with --werror
    const std::filesystem::path cleanDir = tmp.path() / "clean_no_icons";
    ASSERT_EQ(runCli({"init", pathToUtf8(cleanDir)}).code, kExitOk);
    auto noIconsWerror = runCli({"validate", pathToUtf8(cleanDir), "--werror", "--json"});
    EXPECT_EQ(noIconsWerror.code, kExitOk);
    nlohmann::json jn = nlohmann::json::parse(noIconsWerror.out);
    EXPECT_TRUE(jn["ok"].get<bool>());
    EXPECT_EQ(jn["counts"]["warnings"], 0);
    EXPECT_EQ(jn["counts"]["notes"], 1);

    // 4. --no-icons suppresses the "no icons" note
    auto skipIcons = runCli({"validate", pathToUtf8(cleanDir), "--no-icons", "--json"});
    EXPECT_EQ(skipIcons.code, kExitOk);
    nlohmann::json js = nlohmann::json::parse(skipIcons.out);
    EXPECT_EQ(js["counts"]["notes"], 0);

    // 5. --ignore-errors marks degraded=true when a package has non-fatal errors
    const std::filesystem::path badDir = tmp.path() / "bad_degraded";
    const std::string badManifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "deg-game", "name": "Degraded Game" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [{ "id": "ore", "name": "Ore" }, { "id": "plate", "name": "Plate" }],
      "machines": [{ "id": "smelter", "categories": ["smelt"] }],
      "recipes": [
        { "id": "good", "category": "smelt", "time": 1, "in": [["ore", 1]], "out": [["plate", 1]] },
        { "id": "bad", "category": "smelt", "time": 1, "in": [["missing_res", 1]], "out": [["plate", 1]] }
      ]
    })JSON";
    ASSERT_TRUE(atomicWriteFile(badDir / "game.json", badManifest));
    auto degRes = runCli({"validate", pathToUtf8(badDir), "--no-icons", "--ignore-errors", "--json"});
    EXPECT_EQ(degRes.code, kExitError);
    nlohmann::json jd = nlohmann::json::parse(degRes.out);
    EXPECT_TRUE(jd["degraded"].get<bool>());
    EXPECT_FALSE(jd["fatal"].get<bool>());
    EXPECT_EQ(jd["counts"]["errors"], 1);
    EXPECT_EQ(jd["counts"]["dropped"], 1);
}

// ============================================================================
// 4. `eval` command (§9: Spec §10.1, §10.2, --sweep, --cases, bad --param)
// ============================================================================

TEST(GameDataToolEvalTest, ReproducesSpec10_1SatisfactoryAndCases) {
    TempDirGuard tmp("eval_10_1");
    const std::filesystem::path pkgDir = tmp.path() / "pkg";

    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "satisfactory", "name": "Satisfactory", "data_version": "1.1" },
      "units": { "recipe_time": "s", "default_rate": "min" },
      "attributes": {
        "machine": {
          "power": { "type": "number", "default": 0 },
          "somersloop_slots": { "type": "number", "default": 0 }
        }
      },
      "resources": [
        { "id": "iron-ingot", "name": "Iron Ingot" },
        { "id": "iron-plate", "name": "Iron Plate" }
      ],
      "parameters": {
        "clock": { "label": "Clock speed", "type": "number", "default": 100, "min": "1", "max": "250" },
        "somersloops": { "label": "Somersloops", "type": "int", "default": 0, "min": "0",
                         "max": "machine.somersloop_slots", "visible_if": "machine.somersloop_slots > 0" }
      },
      "metrics": {
        "power":  { "label": "Power", "unit": "MW", "aggregate": "sum", "split_sign": true },
        "shards": { "label": "Power shards", "aggregate": "sum" },
        "sloops": { "label": "Somersloops", "aggregate": "sum" }
      },
      "profiles": {
        "sf_production": {
          "params": ["clock", "somersloops"],
          "let": {
            "c": "param.clock / 100",
            "amp": "if(machine.somersloop_slots > 0, 1 + param.somersloops / machine.somersloop_slots, 1)",
            "shards_per_machine": "clamp(ceil((param.clock - 100) / 50), 0, 3)"
          },
          "craft_time": "recipe.time / (machine.speed * c)",
          "output_amount": "port.amount * port.chance * amp",
          "metrics": {
            "power":  { "per_machine": "machine.power * amp ^ 2 * c ^ 1.321928" },
            "shards": { "value": "ceil(node.machines) * shards_per_machine" },
            "sloops": { "value": "ceil(node.machines) * param.somersloops" }
          }
        }
      },
      "machines": [
        { "id": "constructor", "name": "Constructor", "profile": "sf_production",
          "categories": ["constructor"], "speed": 1, "attrs": { "power": 4, "somersloop_slots": 1 } }
      ],
      "recipes": [
        { "id": "iron-plate", "name": "Iron Plate", "category": "constructor", "time": 6,
          "in": [["iron-ingot", 3]], "out": [["iron-plate", 2]] }
      ]
    })JSON";
    ASSERT_TRUE(atomicWriteFile(pkgDir / "game.json", manifest));

    // Spec §10.1: clock=150, somersloops=1, machines=1.5 -> 4 s, out 4, 27.35 MW/machine, shards 2
    auto res = runCli({
        "eval", pathToUtf8(pkgDir),
        "--recipe", "iron-plate",
        "--machine", "constructor",
        "--param", "clock=150",
        "--param", "somersloops=1",
        "--machines", "1.5",
        "--json"
    });
    ASSERT_EQ(res.code, kExitOk) << res.err;

    nlohmann::json j = nlohmann::json::parse(res.out);
    EXPECT_EQ(j["status"], "OK");
    EXPECT_DOUBLE_EQ(j["craft_time_s"].get<double>(), 4.0);
    ASSERT_EQ(j["ports"].size(), 2u);
    EXPECT_EQ(j["ports"][0]["slot_key"], "in:0");
    EXPECT_DOUBLE_EQ(j["ports"][0]["amount_per_craft"].get<double>(), 3.0);
    EXPECT_EQ(j["ports"][1]["slot_key"], "out:0");
    EXPECT_DOUBLE_EQ(j["ports"][1]["amount_per_craft"].get<double>(), 4.0);
    EXPECT_NEAR(j["metric_coeffs"]["power"]["coeff"].get<double>(), 27.35, 0.01);
    EXPECT_DOUBLE_EQ(j["metrics"]["shards"]["value"].get<double>(), 2.0);
    EXPECT_DOUBLE_EQ(j["metrics"]["sloops"]["value"].get<double>(), 2.0);

    // Clamped param detection
    auto clamped = runCli({
        "eval", pathToUtf8(pkgDir),
        "--recipe", "iron-plate",
        "--param", "clock=999",
        "--json"
    });
    ASSERT_EQ(clamped.code, kExitOk) << clamped.err;
    nlohmann::json jc = nlohmann::json::parse(clamped.out);
    EXPECT_EQ(jc["status"], "WARNING");
    EXPECT_DOUBLE_EQ(jc["params"]["clock"]["value"].get<double>(), 250.0);
    EXPECT_EQ(jc["params"]["clock"]["adjusted"], "clamped");

    // --cases PASS
    const std::filesystem::path passCases = tmp.path() / "cases_pass.json";
    ASSERT_TRUE(atomicWriteFile(passCases, R"JSON([
      {
        "name": "constructor 150% + 1 sloop",
        "recipe": "iron-plate",
        "machine": "constructor",
        "params": { "clock": 150, "somersloops": 1 },
        "globals": {},
        "machines": 1.5,
        "expect": {
          "status": "OK",
          "craft_time_s": 4,
          "ports": { "in:0": 3, "out:0": 4 },
          "metric_coeffs": { "power": 27.35 },
          "metrics": { "shards": 2, "sloops": 2 }
        },
        "tol": 1e-3
      }
    ])JSON"));
    auto casesPassRes = runCli({"eval", pathToUtf8(pkgDir), "--cases", pathToUtf8(passCases)});
    EXPECT_EQ(casesPassRes.code, kExitOk) << casesPassRes.out << casesPassRes.err;
    EXPECT_NE(casesPassRes.out.find("PASS"), std::string::npos);

    // --cases FAIL (wrong expectation + missing recipe + missing param — must fail cleanly, never crash)
    const std::filesystem::path failCases = tmp.path() / "cases_fail.json";
    ASSERT_TRUE(atomicWriteFile(failCases, R"JSON([
      {
        "name": "wrong power",
        "recipe": "iron-plate",
        "params": { "clock": 150, "somersloops": 1 },
        "expect": { "metric_coeffs": { "power": 99.0 } }
      },
      {
        "name": "missing recipe",
        "recipe": "nonexistent-recipe",
        "expect": { "status": "OK" }
      },
      {
        "name": "missing param",
        "recipe": "iron-plate",
        "params": { "does_not_exist": 1 },
        "expect": { "status": "OK" }
      }
    ])JSON"));
    auto casesFailRes = runCli({"eval", pathToUtf8(pkgDir), "--cases", pathToUtf8(failCases), "--json"});
    EXPECT_EQ(casesFailRes.code, kExitError);
    nlohmann::json jcf = nlohmann::json::parse(casesFailRes.out);
    EXPECT_FALSE(jcf["ok"].get<bool>());
    EXPECT_EQ(jcf["failed"], 3);

    // Bad --param gives usage error (exit 64)
    EXPECT_EQ(runCli({"eval", pathToUtf8(pkgDir), "--recipe", "iron-plate", "--param", "no_equals"}).code,
              kExitUsage);
    EXPECT_EQ(runCli({"eval", pathToUtf8(pkgDir), "--recipe", "iron-plate", "--param", "clock=not_json"}).code,
              kExitUsage);
    EXPECT_EQ(runCli({"eval", pathToUtf8(pkgDir), "--recipe", "iron-plate", "--param", "unknown_p=1"}).code,
              kExitUsage);

    // Int --sweep over somersloops [0, 1] and --rate s + --all-machines
    auto intSweep = runCli({
        "eval", pathToUtf8(pkgDir),
        "--recipe", "iron-plate",
        "--sweep", "somersloops",
        "--rate", "s",
        "--json"
    });
    ASSERT_EQ(intSweep.code, kExitOk) << intSweep.err;
    nlohmann::json jis = nlohmann::json::parse(intSweep.out);
    ASSERT_EQ(jis["rows"].size(), 2u);
    EXPECT_EQ(jis["rows"][0]["sweep_value"], 0);
    EXPECT_EQ(jis["rows"][1]["sweep_value"], 1);
    EXPECT_EQ(jis["rows"][0]["rate_unit"], "s");
    EXPECT_DOUBLE_EQ(jis["rows"][0]["ports"][1]["amount_per_craft"].get<double>(), 2.0);
    EXPECT_DOUBLE_EQ(jis["rows"][1]["ports"][1]["amount_per_craft"].get<double>(), 4.0);

    auto allMach = runCli({
        "eval", pathToUtf8(pkgDir),
        "--recipe", "iron-plate",
        "--all-machines",
        "--json"
    });
    ASSERT_EQ(allMach.code, kExitOk) << allMach.err;
    nlohmann::json jam = nlohmann::json::parse(allMach.out);
    ASSERT_EQ(jam["results"].size(), 1u);
    EXPECT_EQ(jam["results"][0]["machine"], "constructor");

    // --cases directory mode (loads sorted *.json files)
    const std::filesystem::path casesDir = tmp.path() / "cases_dir";
    ASSERT_TRUE(atomicWriteFile(casesDir / "01_base.json", R"JSON([
      { "name": "base", "recipe": "iron-plate", "expect": { "status": "OK", "craft_time_s": 6 } }
    ])JSON"));
    ASSERT_TRUE(atomicWriteFile(casesDir / "02_sloop.json", R"JSON([
      { "name": "sloop", "recipe": "iron-plate", "params": { "somersloops": 1 }, "expect": { "ports": { "out:0": 4 } } }
    ])JSON"));
    auto dirCasesRes = runCli({"eval", pathToUtf8(pkgDir), "--cases", pathToUtf8(casesDir), "--json"});
    ASSERT_EQ(dirCasesRes.code, kExitOk) << dirCasesRes.out << dirCasesRes.err;
    nlohmann::json jdc = nlohmann::json::parse(dirCasesRes.out);
    EXPECT_EQ(jdc["total"], 2);
    EXPECT_EQ(jdc["passed"], 2);
}

TEST(GameDataToolEvalTest, ReproducesSpec10_2GtnhMvVsHvGatingAndSweep) {
    TempDirGuard tmp("eval_10_2");
    const std::filesystem::path pkgDir = tmp.path() / "pkg";

    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "gtnh", "name": "GTNH" },
      "units": { "recipe_time": "tick", "ticks_per_second": 20, "default_rate": "s" },
      "attributes": {
        "machine": { "min_tier": { "type": "number", "default": 0 },
                     "max_tier": { "type": "number", "default": 14 } },
        "recipe":  { "eut": { "type": "number", "default": 0 } },
        "port":    { "min_tier": { "type": "number", "default": 0 },
                     "chance_boost": { "type": "number", "default": 0 } }
      },
      "resources": [
        { "id": "crushed_copper", "name": "Crushed Copper" },
        { "id": "impure_dust_copper", "name": "Impure Copper Dust" },
        { "id": "dust_gold", "name": "Gold Dust" }
      ],
      "option_sets": {
        "gt_tiers": {
          "attrs": { "t": { "type": "number", "default": 0 }, "v": { "type": "number", "default": 0 } },
          "options": [
            { "key": "ulv", "label": "ULV", "attrs": { "t": 0, "v": 8 } },
            { "key": "lv",  "label": "LV",  "attrs": { "t": 1, "v": 32 } },
            { "key": "mv",  "label": "MV",  "attrs": { "t": 2, "v": 128 } },
            { "key": "hv",  "label": "HV",  "attrs": { "t": 3, "v": 512 } }
          ]
        }
      },
      "parameters": {
        "voltage_tier": {
          "label": "Tier", "type": "enum", "option_set": "gt_tiers", "default": "lv",
          "allowed_if": "option.t >= machine.min_tier && option.t <= machine.max_tier"
        }
      },
      "metrics": {
        "eut": { "label": "Energy", "unit": "EU/t", "aggregate": "sum" }
      },
      "profiles": {
        "gt_single": {
          "params": ["voltage_tier"],
          "let": {
            "t": "param.voltage_tier.t",
            "v": "param.voltage_tier.v",
            "oc_v": "max(0, floor(log(v / max(8, recipe.eut), 4) + 1e-9))",
            "oc": "min(oc_v, max(0, floor(log(recipe.time_raw, 2) + 1e-9)))"
          },
          "requires": [{ "expr": "v >= recipe.eut", "message": "Machine tier too low" }],
          "craft_time": "max(1, floor(recipe.time_raw / 2 ^ oc)) / game.ticks_per_second",
          "port_enabled": "port.min_tier <= t",
          "output_amount": "port.amount * min(1, port.chance + port.chance_boost * oc)",
          "metrics": { "eut": { "per_machine": "recipe.eut * 4 ^ oc" } }
        }
      },
      "machines": [
        { "id": "macerator", "name": "Macerator", "profile": "gt_single",
          "categories": ["gt-macerator"], "attrs": { "min_tier": 1, "max_tier": 3 } }
      ],
      "recipes": [
        {
          "id": "macerate-crushed-copper", "category": "gt-macerator", "time": 400,
          "attrs": { "eut": 2 },
          "in": [["crushed_copper", 1]],
          "out": [["impure_dust_copper", 1],
                  { "res": "dust_gold", "amount": 1, "chance": 0.1, "attrs": { "min_tier": 3 } }]
        }
      ]
    })JSON";
    ASSERT_TRUE(atomicWriteFile(pkgDir / "game.json", manifest));

    // MV: craft_time=5.0, out:1 inactive, eut=32
    auto mv = runCli({
        "eval", pathToUtf8(pkgDir),
        "--recipe", "macerate-crushed-copper",
        "--param", "voltage_tier=\"mv\"",
        "--json"
    });
    ASSERT_EQ(mv.code, kExitOk) << mv.err;
    nlohmann::json jmv = nlohmann::json::parse(mv.out);
    EXPECT_DOUBLE_EQ(jmv["craft_time_s"].get<double>(), 5.0);
    EXPECT_FALSE(jmv["ports"][2]["active"].get<bool>());
    EXPECT_DOUBLE_EQ(jmv["metric_coeffs"]["eut"]["coeff"].get<double>(), 32.0);

    // HV: craft_time=2.5, out:1 active with amount 0.1, eut=128
    auto hv = runCli({
        "eval", pathToUtf8(pkgDir),
        "--recipe", "macerate-crushed-copper",
        "--param", "voltage_tier=\"hv\"",
        "--machines", "5",
        "--json"
    });
    ASSERT_EQ(hv.code, kExitOk) << hv.err;
    nlohmann::json jhv = nlohmann::json::parse(hv.out);
    EXPECT_DOUBLE_EQ(jhv["craft_time_s"].get<double>(), 2.5);
    EXPECT_TRUE(jhv["ports"][2]["active"].get<bool>());
    EXPECT_DOUBLE_EQ(jhv["ports"][2]["amount_per_craft"].get<double>(), 0.1);
    EXPECT_DOUBLE_EQ(jhv["metric_coeffs"]["eut"]["coeff"].get<double>(), 128.0);
    EXPECT_DOUBLE_EQ(jhv["metrics"]["eut"]["value"].get<double>(), 640.0);

    // --sweep voltage_tier -> allowed options only (lv, mv, hv; ulv excluded by min_tier=1)
    auto sweep = runCli({
        "eval", pathToUtf8(pkgDir),
        "--recipe", "macerate-crushed-copper",
        "--sweep", "voltage_tier",
        "--json"
    });
    ASSERT_EQ(sweep.code, kExitOk) << sweep.err;
    nlohmann::json jsw = nlohmann::json::parse(sweep.out);
    ASSERT_EQ(jsw["rows"].size(), 3u);
    EXPECT_EQ(jsw["rows"][0]["sweep_value"], "lv");
    EXPECT_EQ(jsw["rows"][1]["sweep_value"], "mv");
    EXPECT_EQ(jsw["rows"][2]["sweep_value"], "hv");
    EXPECT_FALSE(jsw["rows"][1]["ports"][2]["active"].get<bool>());
    EXPECT_TRUE(jsw["rows"][2]["ports"][2]["active"].get<bool>());

    // Reject --sweep on an int range with > 32 values (exit 64)
    const std::filesystem::path wideDir = tmp.path() / "wide_sweep";
    ASSERT_TRUE(atomicWriteFile(wideDir / "game.json", R"JSON({
      "schema_version": 4,
      "game": { "id": "wide", "name": "Wide" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [{ "id": "a" }, { "id": "b" }],
      "parameters": { "level": { "type": "int", "default": 0, "min": "0", "max": "100" } },
      "profiles": { "p": { "params": ["level"] } },
      "machines": [{ "id": "m", "profile": "p", "categories": ["c"] }],
      "recipes": [{ "id": "r", "category": "c", "time": 1, "in": [["a", 1]], "out": [["b", 1]] }]
    })JSON"));
    EXPECT_EQ(runCli({"eval", pathToUtf8(wideDir), "--recipe", "r", "--sweep", "level"}).code,
              kExitUsage);
}

// ============================================================================
// 5. `stats` & Synthetic 50k Package Generator + Load Benchmark
// ============================================================================

TEST(GameDataToolStatsTest, StatsCountsDistributionsAndRepeat) {
    TempDirGuard tmp("stats_small");
    test_helpers::SyntheticPackageConfig cfg;
    cfg.resource_count = 40;
    cfg.machine_count = 10;
    cfg.category_count = 5;
    cfg.recipe_count = 100;
    cfg.shard_count = 4;

    auto gen = test_helpers::writeSyntheticPackage(tmp.path(), cfg);
    ASSERT_TRUE(gen.ok) << gen.error;

    auto val = runCli({"validate", pathToUtf8(tmp.path()), "--werror", "--json"});
    ASSERT_EQ(val.code, kExitOk) << val.out << val.err;

    auto st = runCli({"stats", pathToUtf8(tmp.path()), "--repeat", "3", "--timing", "--json"});
    ASSERT_EQ(st.code, kExitOk) << st.err;

    nlohmann::json j = nlohmann::json::parse(st.out);
    EXPECT_EQ(j["counts"]["resources"], 40);
    EXPECT_EQ(j["counts"]["machines"], 10);
    EXPECT_EQ(j["counts"]["recipes"], 100);
    EXPECT_EQ(j["counts"]["files"], 7); // game.json + resources.json + machines.json + 4 shards
    EXPECT_EQ(j["json_bytes"].get<uint64_t>(), gen.total_json_bytes);
    EXPECT_EQ(j["timing_ms"]["repeat"], 3);
    EXPECT_GE(j["timing_ms"]["total"].get<double>(), 0.0);
    EXPECT_EQ(j["recipes_by_category"].size(), 5u);
    EXPECT_EQ(j["icons"]["mode"], "none");
}

TEST(GameDataToolStatsTest, Synthetic50kPackageLoadAndReportTiming) {
    TempDirGuard tmp("stats_50k");
    test_helpers::SyntheticPackageConfig cfg; // Default: 3,500 resources, 200 machines, 50,000 recipes, 20 shards

    auto gen = test_helpers::writeSyntheticPackage(tmp.path(), cfg);
    ASSERT_TRUE(gen.ok) << gen.error;
    EXPECT_EQ(gen.files_written, 23u); // game.json + resources.json + machines.json + 20 shards

#ifdef NDEBUG
    const std::string repeatStr = "5";
#else
    const std::string repeatStr = "1";
#endif
    auto st = runCli({"stats", pathToUtf8(tmp.path()), "--timing", "--repeat", repeatStr, "--json"});
    ASSERT_EQ(st.code, kExitOk) << st.err;

    nlohmann::json j = nlohmann::json::parse(st.out);
    EXPECT_EQ(j["counts"]["resources"], 3500);
    EXPECT_EQ(j["counts"]["machines"], 200);
    EXPECT_EQ(j["counts"]["recipes"], 50000);
    EXPECT_EQ(j["counts"]["files"], 23);

    const double totalMs = j["timing_ms"]["total"].get<double>();
    const double parseMs = j["timing_ms"]["parse"].get<double>();
    const double compileMs = j["timing_ms"]["compile"].get<double>();
    RecordProperty("synthetic_50k_repeat", repeatStr);
    RecordProperty("synthetic_50k_total_ms", std::to_string(totalMs));
    RecordProperty("synthetic_50k_parse_ms", std::to_string(parseMs));
    RecordProperty("synthetic_50k_compile_ms", std::to_string(compileMs));
    std::cout << "[Benchmark] Synthetic 50k package load (median of " << repeatStr
              << "): total=" << totalMs << " ms (parse=" << parseMs
              << " ms, compile=" << compileMs << " ms, json=" << gen.total_json_bytes << " bytes)\n";
}

// ============================================================================
// 6. `guide` command (embedded authoring docs, §2.7)
// ============================================================================

std::string normalizeLineEndings(std::string s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') {
            continue;
        }
        out.push_back(s[i]);
    }
    return out;
}

std::filesystem::path locateRepoRootForDocs() {
    std::vector<std::filesystem::path> roots;
    if (const char* ghWorkspace = std::getenv("GITHUB_WORKSPACE")) {
        roots.emplace_back(ghWorkspace);
    }
    if (auto execDir = getExecutableDirectory(); execDir.has_value()) {
        roots.push_back(*execDir);
    }
    roots.push_back(std::filesystem::current_path());

    for (const auto& root : roots) {
        std::filesystem::path cur = root;
        for (int i = 0; i < 6; ++i) {
            std::error_code ec;
            if (std::filesystem::is_regular_file(cur / "docs" / "gamedata-v4.md", ec) &&
                std::filesystem::is_regular_file(
                    cur / "docs" / "authoring" / "pack-author-prompt.md", ec)) {
                return cur;
            }
            if (!cur.has_parent_path() || cur == cur.parent_path()) break;
            cur = cur.parent_path();
        }
    }
    return {};
}

TEST(GameDataToolGuideTest, PrintsEmbeddedPromptAndReferenceMatchingDocsOnDisk) {
    const std::filesystem::path repoRoot = locateRepoRootForDocs();
    ASSERT_FALSE(repoRoot.empty()) << "Failed to locate repository root containing docs/";

    const std::string diskPrompt =
        normalizeLineEndings(readFileBytes(repoRoot / "docs" / "authoring" / "pack-author-prompt.md"));
    const std::string diskReference =
        normalizeLineEndings(readFileBytes(repoRoot / "docs" / "gamedata-v4.md"));
    ASSERT_FALSE(diskPrompt.empty());
    ASSERT_FALSE(diskReference.empty());

    auto promptRes = runCli({"guide", "prompt"});
    ASSERT_EQ(promptRes.code, kExitOk) << promptRes.err;
    EXPECT_TRUE(promptRes.err.empty());
    EXPECT_EQ(promptRes.out, diskPrompt);
    EXPECT_NE(promptRes.out.find("NEVER rely on memory"), std::string::npos);
    EXPECT_NE(promptRes.out.find("authoritative references"), std::string::npos);
    EXPECT_NE(promptRes.out.find("CITE EXACT SOURCE + VERSION"), std::string::npos);

    auto refRes = runCli({"guide", "reference"});
    ASSERT_EQ(refRes.code, kExitOk) << refRes.err;
    EXPECT_TRUE(refRes.err.empty());
    EXPECT_EQ(refRes.out, diskReference);
    EXPECT_NE(refRes.out.find("## 2. Schema Cheat Sheet"), std::string::npos);
    EXPECT_NE(refRes.out.find("## 3. Expression Language"), std::string::npos);
    EXPECT_NE(refRes.out.find("## 4. Validation Rules"), std::string::npos);
    EXPECT_NE(refRes.out.find("## 6. Mechanic"), std::string::npos);
    EXPECT_NE(refRes.out.find("## 9. `gamedata_tool` Command Reference"), std::string::npos);
}

TEST(GameDataToolGuideTest, TopicListJsonHelpAndUsageErrors) {
    // Bare `guide` lists available topics
    auto bare = runCli({"guide"});
    ASSERT_EQ(bare.code, kExitOk) << bare.err;
    EXPECT_NE(bare.out.find("prompt"), std::string::npos);
    EXPECT_NE(bare.out.find("reference"), std::string::npos);

    // `--json` forms
    auto bareJson = runCli({"guide", "--json"});
    ASSERT_EQ(bareJson.code, kExitOk) << bareJson.err;
    nlohmann::json jBare = nlohmann::json::parse(bareJson.out);
    ASSERT_EQ(jBare["topics"].size(), 2u);
    EXPECT_EQ(jBare["topics"][0]["name"], "prompt");
    EXPECT_EQ(jBare["topics"][1]["name"], "reference");

    auto promptJson = runCli({"guide", "prompt", "--json"});
    ASSERT_EQ(promptJson.code, kExitOk) << promptJson.err;
    nlohmann::json jPrompt = nlohmann::json::parse(promptJson.out);
    EXPECT_EQ(jPrompt["topic"], "prompt");
    EXPECT_EQ(jPrompt["content"].get<std::string>(), runCli({"guide", "prompt"}).out);

    auto refJson = runCli({"guide", "reference", "--json"});
    ASSERT_EQ(refJson.code, kExitOk) << refJson.err;
    nlohmann::json jRef = nlohmann::json::parse(refJson.out);
    EXPECT_EQ(jRef["topic"], "reference");
    EXPECT_EQ(jRef["content"].get<std::string>(), runCli({"guide", "reference"}).out);

    // Help mentions guide
    auto topHelp = runCli({"--help"});
    ASSERT_EQ(topHelp.code, kExitOk);
    EXPECT_NE(topHelp.out.find("guide"), std::string::npos);

    auto guideHelp = runCli({"guide", "--help"});
    ASSERT_EQ(guideHelp.code, kExitOk);
    EXPECT_NE(guideHelp.out.find("prompt"), std::string::npos);
    EXPECT_NE(guideHelp.out.find("reference"), std::string::npos);

    // Usage errors
    EXPECT_EQ(runCli({"guide", "unknown"}).code, kExitUsage);
    EXPECT_EQ(runCli({"guide", "prompt", "extra"}).code, kExitUsage);
    EXPECT_EQ(runCli({"guide", "--unknown-flag"}).code, kExitUsage);
}

TEST(GameDataToolEvalTest, RateInvariantResourceKindIgnoresRateUnitInJsonAndHuman) {
    TempDirGuard tmp("eval_rate_invariant");
    const std::filesystem::path pkgDir = tmp.path() / "pkg";

    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "factorio-heat-test", "name": "Factorio Heat Test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resource_kinds": {
        "fluid":    { "label": "Fluid",    "display_unit": "L",  "display_scale": 1 },
        "heat":     { "label": "Heat",     "display_unit": "MW", "display_scale": 1,     "rate_invariant": true },
        "power_kw": { "label": "Power kW", "display_unit": "MW", "display_scale": 0.001, "rate_invariant": true }
      },
      "resources": [
        { "id": "steam", "name": "Steam", "kind": "fluid" },
        { "id": "heat",  "name": "Heat",  "kind": "heat" },
        { "id": "power", "name": "Power", "kind": "power_kw" }
      ],
      "machines": [
        { "id": "reactor", "name": "Reactor", "categories": ["react"] }
      ],
      "recipes": [
        {
          "id": "generate-heat",
          "name": "Generate Heat",
          "category": "react",
          "time": 1,
          "out": [["heat", 40], ["power", 500], ["steam", 10]]
        }
      ]
    })JSON";
    ASSERT_TRUE(atomicWriteFile(pkgDir / "game.json", manifest));

    // JSON output with --rate min: rate_invariant kinds stay unscaled by 60 (still apply display_scale).
    auto jsonRes = runCli({
        "eval", pathToUtf8(pkgDir),
        "--recipe", "generate-heat",
        "--rate", "min",
        "--json"
    });
    ASSERT_EQ(jsonRes.code, kExitOk) << jsonRes.err;
    nlohmann::json j = nlohmann::json::parse(jsonRes.out);
    EXPECT_EQ(j["rate_unit"], "min");
    ASSERT_EQ(j["ports"].size(), 3u);
    EXPECT_DOUBLE_EQ(j["ports"][0]["rate_per_machine"].get<double>(), 40.0);  // heat: 40 MW
    EXPECT_DOUBLE_EQ(j["ports"][1]["rate_per_machine"].get<double>(), 0.5);   // power_kw: 500 * 0.001 MW
    EXPECT_DOUBLE_EQ(j["ports"][2]["rate_per_machine"].get<double>(), 600.0); // steam: 10 * 60 L/min

    // Human output with --rate min: omits /min suffix on rate_invariant resources, keeps L/min on fluid.
    auto humanRes = runCli({
        "eval", pathToUtf8(pkgDir),
        "--recipe", "generate-heat",
        "--rate", "min"
    });
    ASSERT_EQ(humanRes.code, kExitOk) << humanRes.err;
    EXPECT_NE(humanRes.out.find("(40 MW per machine)"), std::string::npos) << humanRes.out;
    EXPECT_NE(humanRes.out.find("(0.5 MW per machine)"), std::string::npos) << humanRes.out;
    EXPECT_NE(humanRes.out.find("(600 L/min per machine)"), std::string::npos) << humanRes.out;
    EXPECT_EQ(humanRes.out.find("MW/min"), std::string::npos) << humanRes.out;
}

} // namespace
} // namespace gamedata_tool
