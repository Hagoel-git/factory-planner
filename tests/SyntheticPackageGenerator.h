#ifndef TESTS_SYNTHETIC_PACKAGE_GENERATOR_H
#define TESTS_SYNTHETIC_PACKAGE_GENERATOR_H

#include "common/FilesystemUtils.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace test_helpers {

struct SyntheticPackageConfig {
    std::string game_id = "synthetic-50k";
    std::string game_name = "Synthetic 50k Benchmark";
    size_t resource_count = 3500;
    size_t machine_count = 200;
    size_t category_count = 50;
    size_t recipe_count = 50000;
    size_t shard_count = 20;
};

struct SyntheticPackageResult {
    bool ok = false;
    std::string error;
    std::filesystem::path root_dir;
    size_t files_written = 0;
    uint64_t total_json_bytes = 0;
};

inline std::string paddedIndex(size_t idx, int width) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%0*zu", width, idx);
    return std::string(buf);
}

// Writes a deterministic, zero-warning GTNH-scale GameData v4 package to `rootDir`.
inline SyntheticPackageResult writeSyntheticPackage(
    const std::filesystem::path& rootDir,
    const SyntheticPackageConfig& cfg = {}) {
    SyntheticPackageResult res;
    res.root_dir = rootDir;

    const size_t numCategories = std::max<size_t>(1, std::min(cfg.category_count, cfg.recipe_count));
    const size_t numMachines = std::max<size_t>(numCategories, cfg.machine_count);
    const size_t numResources = std::max<size_t>(2, std::min(cfg.resource_count, cfg.recipe_count * 2));
    const size_t numShards = std::max<size_t>(1, std::min(cfg.shard_count, std::max<size_t>(1, cfg.recipe_count)));

    auto writeFile = [&](const std::filesystem::path& rel, const std::string& content) -> bool {
        std::string err;
        if (!atomicWriteFile(rootDir / rel, content, &err)) {
            res.error = err;
            return false;
        }
        ++res.files_written;
        res.total_json_bytes += content.size();
        return true;
    };

    // 1. Manifest: game.json
    std::string manifest;
    manifest.reserve(4096);
    manifest += "{\n";
    manifest += "  \"schema_version\": 4,\n";
    manifest += "  \"game\": { \"id\": \"" + cfg.game_id + "\", \"name\": \"" + cfg.game_name + "\", \"data_version\": \"1.0.0\" },\n";
    manifest += "  \"units\": { \"recipe_time\": \"tick\", \"ticks_per_second\": 20, \"default_rate\": \"s\" },\n";
    manifest += "  \"resource_kinds\": {\n";
    manifest += "    \"item\": { \"label\": \"Item\", \"display_unit\": \"\", \"display_scale\": 1 },\n";
    manifest += "    \"fluid\": { \"label\": \"Fluid\", \"display_unit\": \"L\", \"display_scale\": 1 }\n";
    manifest += "  },\n";
    manifest += "  \"attributes\": {\n";
    manifest += "    \"machine\": {\n";
    manifest += "      \"min_tier\": { \"type\": \"number\", \"default\": 1 },\n";
    manifest += "      \"max_tier\": { \"type\": \"number\", \"default\": 8 }\n";
    manifest += "    },\n";
    manifest += "    \"recipe\": {\n";
    manifest += "      \"eut\": { \"type\": \"number\", \"default\": 30 }\n";
    manifest += "    },\n";
    manifest += "    \"port\": {\n";
    manifest += "      \"min_tier\": { \"type\": \"number\", \"default\": 0 },\n";
    manifest += "      \"chance_boost\": { \"type\": \"number\", \"default\": 0 }\n";
    manifest += "    }\n";
    manifest += "  },\n";
    manifest += "  \"option_sets\": {\n";
    manifest += "    \"gt_tiers\": {\n";
    manifest += "      \"attrs\": { \"t\": { \"type\": \"number\", \"default\": 0 }, \"v\": { \"type\": \"number\", \"default\": 0 } },\n";
    manifest += "      \"options\": [\n";
    manifest += "        { \"key\": \"ulv\", \"label\": \"ULV\", \"attrs\": { \"t\": 0, \"v\": 8 } },\n";
    manifest += "        { \"key\": \"lv\",  \"label\": \"LV\",  \"attrs\": { \"t\": 1, \"v\": 32 } },\n";
    manifest += "        { \"key\": \"mv\",  \"label\": \"MV\",  \"attrs\": { \"t\": 2, \"v\": 128 } },\n";
    manifest += "        { \"key\": \"hv\",  \"label\": \"HV\",  \"attrs\": { \"t\": 3, \"v\": 512 } },\n";
    manifest += "        { \"key\": \"ev\",  \"label\": \"EV\",  \"attrs\": { \"t\": 4, \"v\": 2048 } },\n";
    manifest += "        { \"key\": \"iv\",  \"label\": \"IV\",  \"attrs\": { \"t\": 5, \"v\": 8192 } },\n";
    manifest += "        { \"key\": \"luv\", \"label\": \"LuV\", \"attrs\": { \"t\": 6, \"v\": 32768 } },\n";
    manifest += "        { \"key\": \"zpm\", \"label\": \"ZPM\", \"attrs\": { \"t\": 7, \"v\": 131072 } },\n";
    manifest += "        { \"key\": \"uv\",  \"label\": \"UV\",  \"attrs\": { \"t\": 8, \"v\": 524288 } }\n";
    manifest += "      ]\n";
    manifest += "    }\n";
    manifest += "  },\n";
    manifest += "  \"parameters\": {\n";
    manifest += "    \"voltage_tier\": {\n";
    manifest += "      \"label\": \"Tier\", \"type\": \"enum\", \"option_set\": \"gt_tiers\", \"default\": \"lv\",\n";
    manifest += "      \"allowed_if\": \"option.t >= machine.min_tier && option.t <= machine.max_tier\"\n";
    manifest += "    }\n";
    manifest += "  },\n";
    manifest += "  \"metrics\": {\n";
    manifest += "    \"eut\": { \"label\": \"Energy\", \"unit\": \"EU/t\", \"display\": [\"body\", \"totals\"], \"aggregate\": \"sum\" }\n";
    manifest += "  },\n";
    manifest += "  \"profiles\": {\n";
    manifest += "    \"gt_single\": {\n";
    manifest += "      \"params\": [\"voltage_tier\"],\n";
    manifest += "      \"let\": {\n";
    manifest += "        \"t\": \"param.voltage_tier.t\",\n";
    manifest += "        \"v\": \"param.voltage_tier.v\",\n";
    manifest += "        \"oc_v\": \"max(0, floor(log(v / max(8, recipe.eut), 4) + 1e-9))\",\n";
    manifest += "        \"oc\": \"min(oc_v, max(0, floor(log(recipe.time_raw, 2) + 1e-9)))\"\n";
    manifest += "      },\n";
    manifest += "      \"requires\": [{ \"expr\": \"v >= recipe.eut\", \"message\": \"Machine tier too low\" }],\n";
    manifest += "      \"craft_time\": \"max(1, floor(recipe.time_raw / 2 ^ oc)) / game.ticks_per_second\",\n";
    manifest += "      \"port_enabled\": \"port.min_tier <= t\",\n";
    manifest += "      \"output_amount\": \"port.amount * min(1, port.chance + port.chance_boost * oc)\",\n";
    manifest += "      \"metrics\": { \"eut\": { \"per_machine\": \"recipe.eut * 4 ^ oc\" } }\n";
    manifest += "    },\n";
    manifest += "    \"gt_basic\": {\n";
    manifest += "      \"extends\": \"default\"\n";
    manifest += "    }\n";
    manifest += "  },\n";
    manifest += "  \"include\": [\"resources.json\", \"machines.json\", \"recipes/\"]\n";
    manifest += "}\n";

    if (!writeFile("game.json", manifest)) {
        return res;
    }

    // 2. resources.json
    static constexpr std::array<const char*, 10> kMaterials = {
        "Iron", "Copper", "Steel", "Titanium", "Aluminium",
        "Tungsten", "Chrome", "Nickel", "Platinum", "Cobalt"
    };
    static constexpr std::array<const char*, 8> kForms = {
        "Dust", "Ingot", "Plate", "Gear", "Wire", "Rod", "Circuit", "Fluid"
    };

    std::string resJson;
    resJson.reserve(numResources * 72 + 64);
    resJson += "{\"resources\":[\n";
    for (size_t i = 0; i < numResources; ++i) {
        const bool isFluid = (i % 8 == 7);
        const char* mat = kMaterials[i % kMaterials.size()];
        const char* form = kForms[(i / kMaterials.size()) % kForms.size()];
        resJson += "{\"id\":\"res-";
        resJson += paddedIndex(i, 4);
        resJson += "\",\"name\":\"";
        resJson += mat;
        resJson += ' ';
        resJson += form;
        resJson += ' ';
        resJson += std::to_string(i);
        resJson += "\",\"kind\":\"";
        resJson += isFluid ? "fluid" : "item";
        resJson += "\"}";
        if (i + 1 < numResources) resJson += ',';
        resJson += '\n';
    }
    resJson += "]}\n";
    if (!writeFile("resources.json", resJson)) {
        return res;
    }

    // 3. machines.json
    std::string machJson;
    machJson.reserve(numMachines * 120 + 64);
    machJson += "{\"machines\":[\n";
    for (size_t i = 0; i < numMachines; ++i) {
        const size_t catIdx = i % numCategories;
        const bool useBasic = (i % 5 == 0);
        machJson += "{\"id\":\"mach-";
        machJson += paddedIndex(i, 3);
        machJson += "\",\"name\":\"Machine ";
        machJson += std::to_string(i);
        machJson += "\",\"profile\":\"";
        machJson += useBasic ? "gt_basic" : "gt_single";
        machJson += "\",\"categories\":[\"cat-";
        machJson += paddedIndex(catIdx, 2);
        machJson += "\"],\"speed\":1";
        if (!useBasic) {
            machJson += ",\"attrs\":{\"min_tier\":1,\"max_tier\":8}";
        }
        machJson += "}";
        if (i + 1 < numMachines) machJson += ',';
        machJson += '\n';
    }
    machJson += "]}\n";
    if (!writeFile("machines.json", machJson)) {
        return res;
    }

    // 4. recipes/shard_XX.json
    const size_t basePerShard = cfg.recipe_count / numShards;
    const size_t remainder = cfg.recipe_count % numShards;
    size_t recipeCursor = 0;

    for (size_t s = 0; s < numShards; ++s) {
        const size_t count = basePerShard + (s < remainder ? 1 : 0);
        std::string shardJson;
        shardJson.reserve(count * 220 + 64);
        shardJson += "{\"recipes\":[\n";

        for (size_t localIdx = 0; localIdx < count; ++localIdx) {
            const size_t i = recipeCursor++;
            const size_t catIdx = i % numCategories;
            const size_t in0 = i % numResources;
            size_t in1 = (i * 7 + 13) % numResources;
            if (in1 == in0) in1 = (in0 + 1) % numResources;
            const size_t out0 = (i * 3 + 1) % numResources;
            size_t out1 = (i * 5 + 2) % numResources;
            if (out1 == out0) out1 = (out0 + 1) % numResources;

            const int eut = (i % 4 == 0) ? 30 : ((i % 4 == 1) ? 120 : ((i % 4 == 2) ? 480 : 16));
            const int timeTicks = 20 + static_cast<int>((i % 20) * 10);

            shardJson += "{\"id\":\"rec-";
            shardJson += paddedIndex(i, 5);
            shardJson += "\",\"name\":\"Process ";
            shardJson += kMaterials[i % kMaterials.size()];
            shardJson += ' ';
            shardJson += std::to_string(i);
            shardJson += "\",\"category\":\"cat-";
            shardJson += paddedIndex(catIdx, 2);
            shardJson += "\",\"time\":";
            shardJson += std::to_string(timeTicks);
            shardJson += ",\"attrs\":{\"eut\":";
            shardJson += std::to_string(eut);
            shardJson += "},\"in\":[[\"res-";
            shardJson += paddedIndex(in0, 4);
            shardJson += "\",2]";
            if (i % 2 == 0) {
                shardJson += ",[\"res-";
                shardJson += paddedIndex(in1, 4);
                shardJson += "\",1]";
            }
            shardJson += "],\"out\":[[\"res-";
            shardJson += paddedIndex(out0, 4);
            shardJson += "\",1]";
            if (i % 3 == 0) {
                shardJson += ",{\"res\":\"res-";
                shardJson += paddedIndex(out1, 4);
                shardJson += "\",\"amount\":1,\"chance\":0.1,\"attrs\":{\"min_tier\":3}}";
            }
            shardJson += "]}";
            if (localIdx + 1 < count) shardJson += ',';
            shardJson += '\n';
        }
        shardJson += "]}\n";

        std::filesystem::path shardRel =
            std::filesystem::path("recipes") / ("shard_" + paddedIndex(s, 2) + ".json");
        if (!writeFile(shardRel, shardJson)) {
            return res;
        }
    }

    res.ok = true;
    return res;
}

} // namespace test_helpers

#endif // TESTS_SYNTHETIC_PACKAGE_GENERATOR_H
