#include <gtest/gtest.h>

#include "core/data/GameData.h"
#include "core/data/GameDataLoader.h"
#include "core/data/GameDataScanner.h"
#include "core/data/NodeEvaluator.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace gamedata {
namespace {

// ============================================================================
// Part 1: Worked Examples from Spec §10.1 – §10.5
// ============================================================================

TEST(GameDataV4ExamplesTest, Example10_1_SatisfactoryConstructorAndGenerator) {
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "satisfactory", "name": "Satisfactory", "data_version": "1.1.0.4" },
      "units": { "recipe_time": "s", "default_rate": "min" },
      "attributes": {
        "machine": {
          "power": { "type": "number", "default": 0 },
          "somersloop_slots": { "type": "number", "default": 0 }
        }
      },
      "resources": [
        { "id": "iron-ingot", "name": "Iron Ingot" },
        { "id": "iron-plate", "name": "Iron Plate" },
        { "id": "coal", "name": "Coal" }
      ],
      "parameters": {
        "clock": {
          "label": "Clock speed", "type": "number", "default": 100,
          "min": "1", "max": "250", "step": 0.0001, "unit": "%",
          "ui": { "widget": "drag", "format": "%.4g%%", "placement": "body" }
        },
        "somersloops": {
          "label": "Somersloops", "type": "int", "default": 0,
          "min": "0", "max": "machine.somersloop_slots",
          "visible_if": "machine.somersloop_slots > 0",
          "ui": { "widget": "stepper", "placement": "body" }
        }
      },
      "metrics": {
        "power":  { "label": "Power", "unit": "MW", "display": ["body", "tooltip", "totals"],
                    "aggregate": "sum", "split_sign": true, "format": "%.2f" },
        "shards": { "label": "Power shards", "unit": "", "display": ["tooltip", "totals"],
                    "aggregate": "sum", "format": "%.0f" },
        "sloops": { "label": "Somersloops", "unit": "", "display": ["tooltip", "totals"],
                    "aggregate": "sum", "format": "%.0f" }
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
        },
        "sf_generator": {
          "params": ["clock"],
          "let": { "c": "param.clock / 100" },
          "craft_time": "recipe.time / (machine.speed * c)",
          "metrics": { "power": { "per_machine": "-machine.power * c" } }
        }
      },
      "machines": [
        { "id": "constructor", "name": "Constructor", "profile": "sf_production",
          "categories": ["constructor"], "speed": 1, "attrs": { "power": 4, "somersloop_slots": 1 } },
        { "id": "coal-generator", "name": "Coal Generator", "profile": "sf_generator",
          "categories": ["generator"], "speed": 1, "attrs": { "power": 75 } }
      ],
      "recipes": [
        { "id": "iron-plate", "name": "Iron Plate", "category": "constructor", "time": 6,
          "in": [["iron-ingot", 3]], "out": [["iron-plate", 2]] },
        { "id": "burn-coal", "name": "Burn Coal", "category": "generator", "time": 4,
          "in": [["coal", 1]] }
      ]
    })JSON";

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);
    EXPECT_TRUE(report.ok());
    EXPECT_TRUE(report.warnings.empty());

    // NodeEval for clock = 150, somersloops = 1
    std::unordered_map<std::string, ParamValue> params = {
        {"clock", 150.0},
        {"somersloops", 1}
    };
    NodeEval eval = evaluateNode(pkg, "constructor", "iron-plate", params, "Constructor #1");
    ASSERT_EQ(eval.status, EvalStatus::OK);
    EXPECT_TRUE(eval.messages.empty());

    EXPECT_DOUBLE_EQ(eval.resolved_lets.at("c").asNumber(), 1.5);
    EXPECT_DOUBLE_EQ(eval.resolved_lets.at("amp").asNumber(), 2.0);
    EXPECT_DOUBLE_EQ(eval.resolved_lets.at("shards_per_machine").asNumber(), 1.0);
    EXPECT_DOUBLE_EQ(eval.craft_time_s, 4.0);

    const EvaluatedPort* in0 = eval.findPort("in:0");
    ASSERT_NE(in0, nullptr);
    EXPECT_TRUE(in0->active);
    EXPECT_EQ(in0->resource, "iron-ingot");
    EXPECT_DOUBLE_EQ(in0->amount_per_craft, 3.0);

    const EvaluatedPort* out0 = eval.findPort("out:0");
    ASSERT_NE(out0, nullptr);
    EXPECT_TRUE(out0->active);
    EXPECT_EQ(out0->resource, "iron-plate");
    EXPECT_DOUBLE_EQ(out0->amount_per_craft, 4.0);

    const EvaluatedMetricCoeff* powerCoeff = eval.findMetric("power");
    ASSERT_NE(powerCoeff, nullptr);
    EXPECT_TRUE(powerCoeff->enabled);
    EXPECT_EQ(powerCoeff->form, MetricFormKind::PerMachine);
    const double expectedPowerCoeff = 4.0 * 4.0 * std::pow(1.5, 1.321928);
    EXPECT_NEAR(powerCoeff->coeff, expectedPowerCoeff, 1e-6);
    EXPECT_NEAR(powerCoeff->coeff, 27.35, 1e-2);

    // Post-solve with target 90 plates/min = 1.5/s canonical -> crafts = 0.375/s, M = 1.5 machines
    const double solvedCrafts = 1.5 / 4.0; // 0.375
    const double solvedMachines = solvedCrafts * eval.craft_time_s; // 1.5
    NodeMetricsResult nodeMetrics = evaluateNodeMetrics(
        pkg, "constructor", "iron-plate", eval, solvedMachines, solvedCrafts);
    EXPECT_EQ(nodeMetrics.node_status, EvalStatus::OK);

    const EvaluatedNodeMetric* mPower = nodeMetrics.findMetric("power");
    const EvaluatedNodeMetric* mShards = nodeMetrics.findMetric("shards");
    const EvaluatedNodeMetric* mSloops = nodeMetrics.findMetric("sloops");
    ASSERT_NE(mPower, nullptr);
    ASSERT_NE(mShards, nullptr);
    ASSERT_NE(mSloops, nullptr);
    EXPECT_NEAR(mPower->value, 41.02, 1e-2);
    EXPECT_DOUBLE_EQ(mShards->value, 2.0);
    EXPECT_DOUBLE_EQ(mSloops->value, 2.0);

    // Generator node (negative power) + split_sign totals
    NodeEval genEval = evaluateNode(pkg, "coal-generator", "burn-coal", {{"clock", 100.0}});
    ASSERT_EQ(genEval.status, EvalStatus::OK);
    EXPECT_DOUBLE_EQ(genEval.findMetric("power")->coeff, -75.0);
    NodeMetricsResult genMetrics = evaluateNodeMetrics(
        pkg, "coal-generator", "burn-coal", genEval, 1.0, 0.25);

    std::vector<NodeMetricsResult> allNodes = {nodeMetrics, genMetrics};
    std::vector<MetricTotal> totals = aggregateMetricTotals(pkg, allNodes);
    ASSERT_EQ(totals.size(), 3u);
    EXPECT_EQ(totals[0].metric_id, "power");
    EXPECT_TRUE(totals[0].split_sign);
    EXPECT_NEAR(totals[0].positive_sum, 41.02, 1e-2);
    EXPECT_DOUBLE_EQ(totals[0].negative_sum, -75.0);
    EXPECT_NEAR(totals[0].net_sum, 41.02 - 75.0, 1e-2);
}

TEST(GameDataV4ExamplesTest, Example10_2_GtnhMaceratorByproductsAndRequires) {
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "gtnh", "name": "GTNH", "data_version": "2.7.0" },
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
            { "key": "hv",  "label": "HV",  "attrs": { "t": 3, "v": 512 } },
            { "key": "ev",  "label": "EV",  "attrs": { "t": 4, "v": 2048 } },
            { "key": "iv",  "label": "IV",  "attrs": { "t": 5, "v": 8192 } },
            { "key": "luv", "label": "LuV", "attrs": { "t": 6, "v": 32768 } },
            { "key": "zpm", "label": "ZPM", "attrs": { "t": 7, "v": 131072 } },
            { "key": "uv",  "label": "UV",  "attrs": { "t": 8, "v": 524288 } }
          ]
        }
      },
      "parameters": {
        "voltage_tier": {
          "label": "Voltage tier", "type": "enum", "option_set": "gt_tiers", "default": "lv",
          "allowed_if": "option.t >= machine.min_tier && option.t <= machine.max_tier",
          "ui": { "widget": "combo", "placement": "body" }
        }
      },
      "metrics": {
        "eut": { "label": "Power", "unit": "EU/t", "display": ["body", "totals"], "aggregate": "sum" }
      },
      "profiles": {
        "gt_single": {
          "params": ["voltage_tier"],
          "let": {
            "t": "param.voltage_tier.t",
            "v": "param.voltage_tier.v",
            "oc_v": "max(0, floor(log(v / max(8, recipe.eut), 4) + 1e-9))",
            "oc": "min(oc_v, floor(log(recipe.time_raw, 2) + 1e-9))"
          },
          "requires": [
            { "expr": "v >= recipe.eut", "message": "Machine tier too low for this recipe" }
          ],
          "craft_time": "max(1, floor(recipe.time_raw / 2 ^ oc)) / game.ticks_per_second",
          "output_enabled": "t >= port.min_tier",
          "output_amount": "port.amount * min(1, port.chance + port.chance_boost * t)",
          "metrics": {
            "eut": { "per_machine": "recipe.eut * 4 ^ oc" }
          }
        }
      },
      "machines": [
        { "id": "macerator", "name": "Macerator", "profile": "gt_single",
          "categories": ["gt-macerator"], "attrs": { "min_tier": 1, "max_tier": 8 } }
      ],
      "recipes": [
        { "id": "macerate-crushed-copper", "category": "gt-macerator", "time": 400,
          "attrs": { "eut": 2 },
          "in":  [["crushed_copper", 1]],
          "out": [["impure_dust_copper", 1],
                  { "res": "dust_gold", "amount": 1, "chance": 0.1, "attrs": { "min_tier": 3 } }] },
        { "id": "macerate-hv-ore", "category": "gt-macerator", "time": 200,
          "attrs": { "eut": 120 },
          "in":  [["crushed_copper", 1]],
          "out": [["impure_dust_copper", 1]] }
      ]
    })JSON";

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);
    EXPECT_TRUE(report.ok());

    // 1. NodeEval at MV (t = 2, v = 128)
    NodeEval mvEval = evaluateNode(
        pkg, "macerator", "macerate-crushed-copper", {{"voltage_tier", "mv"}});
    ASSERT_EQ(mvEval.status, EvalStatus::OK);
    EXPECT_DOUBLE_EQ(mvEval.craft_time_s, 5.0);
    EXPECT_TRUE(mvEval.findPort("in:0")->active);
    EXPECT_DOUBLE_EQ(mvEval.findPort("in:0")->amount_per_craft, 1.0);
    EXPECT_TRUE(mvEval.findPort("out:0")->active);
    EXPECT_DOUBLE_EQ(mvEval.findPort("out:0")->amount_per_craft, 1.0);
    // out:1 dust_gold has min_tier 3 > 2 -> inactive!
    EXPECT_FALSE(mvEval.findPort("out:1")->active);
    EXPECT_DOUBLE_EQ(mvEval.findPort("out:1")->amount_per_craft, 0.0);
    EXPECT_DOUBLE_EQ(mvEval.findMetric("eut")->coeff, 32.0);

    // 2. NodeEval at HV (t = 3, v = 512)
    NodeEval hvEval = evaluateNode(
        pkg, "macerator", "macerate-crushed-copper", {{"voltage_tier", "hv"}});
    ASSERT_EQ(hvEval.status, EvalStatus::OK);
    EXPECT_DOUBLE_EQ(hvEval.craft_time_s, 2.5);
    EXPECT_TRUE(hvEval.findPort("out:1")->active);
    EXPECT_DOUBLE_EQ(hvEval.findPort("out:1")->amount_per_craft, 0.1);
    EXPECT_DOUBLE_EQ(hvEval.findMetric("eut")->coeff, 128.0);

    // Solved at HV, target 2 impure dust/s -> crafts = 2/s, M = 5.0 machines, energy = 640 EU/t
    NodeMetricsResult hvMetrics = evaluateNodeMetrics(
        pkg, "macerator", "macerate-crushed-copper", hvEval, 5.0, 2.0);
    EXPECT_DOUBLE_EQ(hvMetrics.findMetric("eut")->value, 640.0);

    // 3. Stored ULV fails allowed_if (min_tier = 1) -> falls back to LV with WARNING
    NodeEval ulvFallback = evaluateNode(
        pkg, "macerator", "macerate-crushed-copper", {{"voltage_tier", "ulv"}});
    EXPECT_EQ(ulvFallback.status, EvalStatus::WARNING);
    EXPECT_EQ(ulvFallback.resolved_params.at("voltage_tier").asString(), "lv");

    // 4. Recipe with eut = 120 at LV (v = 32) -> BLOCKED by requires
    NodeEval blockedEval = evaluateNode(
        pkg, "macerator", "macerate-hv-ore", {{"voltage_tier", "lv"}});
    EXPECT_EQ(blockedEval.status, EvalStatus::BLOCKED);
    ASSERT_FALSE(blockedEval.messages.empty());
    EXPECT_EQ(blockedEval.messages.front().text, "Machine tier too low for this recipe");
    for (const auto& p : blockedEval.ports) {
        EXPECT_FALSE(p.active);
        EXPECT_DOUBLE_EQ(p.amount_per_craft, 0.0);
    }
}

TEST(GameDataV4ExamplesTest, Example10_3_GtnhCropManagerExtraPortsAndParameters) {
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "gtnh", "name": "GregTech: New Horizons", "data_version": "2.9.0",
                "attrs": { "crop_tick_s": 12.8 } },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "attributes": {
        "machine": { "min_tier": { "type": "number", "default": 0 },
                     "max_tier": { "type": "number", "default": 14 },
                     "water_per_s": { "type": "number", "default": 0 },
                     "fertilizer_per_s": { "type": "number", "default": 0 } },
        "recipe":  { "crop_tier": { "type": "number", "default": 1 },
                     "growth_points": { "type": "number", "default": 0 } }
      },
      "resources": [
        { "id": "sugar_cane", "name": "Sugar Cane" },
        { "id": "sticky_resin", "name": "Sticky Resin" },
        { "id": "water", "name": "Water" },
        { "id": "fertilizer", "name": "Fertilizer" }
      ],
      "option_sets": {
        "gt_tiers": {
          "attrs": { "t": { "type": "number", "default": 0 }, "v": { "type": "number", "default": 0 } },
          "options": [
            { "key": "ulv", "label": "ULV", "attrs": { "t": 0, "v": 8 } },
            { "key": "lv",  "label": "LV",  "attrs": { "t": 1, "v": 32 } },
            { "key": "mv",  "label": "MV",  "attrs": { "t": 2, "v": 128 } },
            { "key": "hv",  "label": "HV",  "attrs": { "t": 3, "v": 512 } },
            { "key": "ev",  "label": "EV",  "attrs": { "t": 4, "v": 2048 } },
            { "key": "iv",  "label": "IV",  "attrs": { "t": 5, "v": 8192 } }
          ]
        }
      },
      "tables": {
        "crop_tier_cap": [0, 4, 7, 10, 13, 16],
        "crop_manager_eut": [0, 32, 128, 512, 2048, 8192]
      },
      "parameters": {
        "voltage_tier": {
          "label": "Voltage tier", "type": "enum", "option_set": "gt_tiers", "default": "lv",
          "allowed_if": "option.t >= machine.min_tier && option.t <= machine.max_tier"
        },
        "growth":     { "label": "Growth",     "type": "int", "default": 1, "min": "1", "max": "31", "ui": { "widget": "slider", "group": "Crop stats" } },
        "gain":       { "label": "Gain",       "type": "int", "default": 1, "min": "1", "max": "31", "ui": { "widget": "slider", "group": "Crop stats" } },
        "resistance": { "label": "Resistance", "type": "int", "default": 1, "min": "1", "max": "31", "ui": { "widget": "slider", "group": "Crop stats" } },
        "hydrated":   { "label": "Hydrated",   "type": "bool", "default": false, "ui": { "widget": "checkbox", "placement": "body" } },
        "fertilized": { "label": "Fertilized", "type": "bool", "default": false, "ui": { "widget": "checkbox", "placement": "body" } }
      },
      "metrics": {
        "eut": { "label": "Power", "unit": "EU/t", "display": ["body", "totals"], "aggregate": "sum" }
      },
      "profiles": {
        "gt_crop_manager": {
          "params": ["voltage_tier", "growth", "gain", "resistance", "hydrated", "fertilized"],
          "let": {
            "t": "param.voltage_tier.t",
            "points_per_tick": "3 + param.growth + if(param.hydrated, 2, 0) + if(param.fertilized, 2, 0)",
            "yield": "1.03 ^ param.gain"
          },
          "requires": [ { "expr": "recipe.crop_tier <= lookup(\"crop_tier_cap\", t)",
                          "message": "Crop tier too high for this Crop Manager tier" } ],
          "craft_time": "recipe.growth_points / points_per_tick * game.crop_tick_s",
          "output_amount": "port.amount * port.chance * yield",
          "extra_ports": [
            { "id": "water", "dir": "in", "resource": "water",
              "amount": "machine.water_per_s * craft_time", "enabled": "param.hydrated" },
            { "id": "fertilizer", "dir": "in", "resource": "fertilizer",
              "amount": "machine.fertilizer_per_s * craft_time", "enabled": "param.fertilized" }
          ],
          "metrics": { "eut": { "per_machine": "lookup(\"crop_manager_eut\", t)" } }
        }
      },
      "machines": [
        { "id": "crop_manager", "name": "Crop Manager", "profile": "gt_crop_manager",
          "categories": ["crops"],
          "attrs": { "min_tier": 1, "max_tier": 5, "water_per_s": 0.5, "fertilizer_per_s": 0.01 } }
      ],
      "recipes": [
        { "id": "crop-stickreed", "name": "Stickreed", "category": "crops", "time": 1,
          "attrs": { "crop_tier": 4, "growth_points": 3200 },
          "out": [["sugar_cane", 4], ["sticky_resin", 1, 0.5]] }
      ]
    })JSON";

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);
    EXPECT_TRUE(report.ok());

    // LV manager (t = 1), growth = 31, gain = 31, hydrated = true, fertilized = true
    std::unordered_map<std::string, ParamValue> params = {
        {"voltage_tier", "lv"},
        {"growth", 31},
        {"gain", 31},
        {"resistance", 1},
        {"hydrated", true},
        {"fertilized", true}
    };
    NodeEval eval = evaluateNode(pkg, "crop_manager", "crop-stickreed", params);
    ASSERT_EQ(eval.status, EvalStatus::OK);

    const double expectedCraftTime = 3200.0 / 38.0 * 12.8;
    const double expectedYield = std::pow(1.03, 31.0);
    EXPECT_NEAR(eval.craft_time_s, expectedCraftTime, 1e-6);
    EXPECT_NEAR(eval.craft_time_s, 1077.89, 1e-2);
    EXPECT_NEAR(eval.resolved_lets.at("yield").asNumber(), expectedYield, 1e-6);

    const EvaluatedPort* out0 = eval.findPort("out:0");
    const EvaluatedPort* out1 = eval.findPort("out:1");
    const EvaluatedPort* xinWater = eval.findPort("xin:water");
    const EvaluatedPort* xinFert = eval.findPort("xin:fertilizer");
    ASSERT_NE(out0, nullptr);
    ASSERT_NE(out1, nullptr);
    ASSERT_NE(xinWater, nullptr);
    ASSERT_NE(xinFert, nullptr);

    EXPECT_TRUE(out0->active);
    EXPECT_NEAR(out0->amount_per_craft, 4.0 * expectedYield, 1e-5);
    EXPECT_NEAR(out0->amount_per_craft, 10.0003, 1e-4);

    EXPECT_TRUE(out1->active);
    EXPECT_NEAR(out1->amount_per_craft, 0.5 * expectedYield, 1e-5);
    EXPECT_NEAR(out1->amount_per_craft, 1.25004, 1e-4);

    EXPECT_TRUE(xinWater->active);
    EXPECT_EQ(xinWater->resource, "water");
    EXPECT_NEAR(xinWater->amount_per_craft, 0.5 * expectedCraftTime, 1e-4);
    EXPECT_NEAR(xinWater->amount_per_craft, 538.95, 1e-2);

    EXPECT_TRUE(xinFert->active);
    EXPECT_EQ(xinFert->resource, "fertilizer");
    EXPECT_NEAR(xinFert->amount_per_craft, 0.01 * expectedCraftTime, 1e-5);
    EXPECT_NEAR(xinFert->amount_per_craft, 10.779, 1e-3);

    EXPECT_DOUBLE_EQ(eval.findMetric("eut")->coeff, 32.0);

    // Unchecking hydrated sets xin:water inactive and lowers points_per_tick to 36 -> craft_time = 1137.78 s
    params["hydrated"] = false;
    NodeEval unhydratedEval = evaluateNode(pkg, "crop_manager", "crop-stickreed", params);
    ASSERT_EQ(unhydratedEval.status, EvalStatus::OK);
    EXPECT_FALSE(unhydratedEval.findPort("xin:water")->active);
    EXPECT_DOUBLE_EQ(unhydratedEval.findPort("xin:water")->amount_per_craft, 0.0);
    EXPECT_TRUE(unhydratedEval.findPort("xin:fertilizer")->active);
    EXPECT_NEAR(unhydratedEval.craft_time_s, 3200.0 / 36.0 * 12.8, 1e-5);
    EXPECT_NEAR(unhydratedEval.craft_time_s, 1137.78, 1e-2);
}

TEST(GameDataV4ExamplesTest, Example10_4_DspProliferatorExtraProductsAndSpeedup) {
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "dsp", "name": "Dyson Sphere Program", "data_version": "0.10" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "attributes": {
        "machine":  { "power": { "type": "number", "default": 0 } },
        "recipe":   { "allows_extra": { "type": "bool", "default": false } },
        "resource": { "extra": { "type": "number", "default": 0 },
                      "speedup": { "type": "number", "default": 0 },
                      "power_mult": { "type": "number", "default": 1 },
                      "charges": { "type": "number", "default": 1 } }
      },
      "resources": [
        { "id": "iron-ingot", "name": "Iron Ingot" },
        { "id": "copper-ingot", "name": "Copper Ingot" },
        { "id": "circuit-board", "name": "Circuit Board" },
        { "id": "proliferator-mk1", "name": "Proliferator Mk.I",   "tags": ["proliferator"],
          "attrs": { "extra": 0.125, "speedup": 0.25, "power_mult": 1.3, "charges": 12 } },
        { "id": "proliferator-mk2", "name": "Proliferator Mk.II",  "tags": ["proliferator"],
          "attrs": { "extra": 0.2,   "speedup": 0.5,  "power_mult": 1.7, "charges": 24 } },
        { "id": "proliferator-mk3", "name": "Proliferator Mk.III", "tags": ["proliferator"],
          "attrs": { "extra": 0.25,  "speedup": 1.0,  "power_mult": 2.5, "charges": 60 } }
      ],
      "option_sets": {
        "prolif_modes": { "attrs": {}, "options": [ { "key": "extra", "label": "Extra products" },
                                                    { "key": "speed", "label": "Production speedup" } ] }
      },
      "parameters": {
        "prolif":      { "label": "Proliferator", "type": "resource_choice", "tag": "proliferator",
                         "allow_none": true, "default": null, "ui": { "widget": "combo", "placement": "body" } },
        "prolif_mode": { "label": "Mode", "type": "enum", "option_set": "prolif_modes", "default": "speed",
                         "allowed_if": "option != \"extra\" || recipe.allows_extra",
                         "visible_if": "is_set(param.prolif)", "ui": { "widget": "combo", "placement": "body" } }
      },
      "metrics": {
        "power": { "label": "Power", "unit": "MW", "display": ["body", "totals"], "aggregate": "sum", "format": "%.2f" }
      },
      "profiles": {
        "dsp_crafter": {
          "params": ["prolif", "prolif_mode"],
          "let": {
            "on": "is_set(param.prolif)",
            "extra": "if(on && param.prolif_mode == \"extra\", param.prolif.extra, 0)",
            "spd": "if(on && param.prolif_mode == \"speed\", param.prolif.speedup, 0)"
          },
          "craft_time": "recipe.time / (machine.speed * (1 + spd))",
          "output_amount": "port.amount * port.chance * (1 + extra)",
          "extra_ports": [
            { "id": "spray", "dir": "in", "resource_param": "prolif",
              "amount": "recipe.input_items_total / param.prolif.charges", "enabled": "on" }
          ],
          "metrics": { "power": { "per_machine": "machine.power * if(on, param.prolif.power_mult, 1)" } }
        }
      },
      "machines": [
        { "id": "assembler-mk3", "name": "Assembling Machine Mk.III", "profile": "dsp_crafter",
          "categories": ["assembler"], "speed": 1.5, "attrs": { "power": 0.78 } }
      ],
      "recipes": [
        { "id": "circuit-board", "name": "Circuit Board", "category": "assembler", "time": 1,
          "attrs": { "allows_extra": true },
          "in": [["iron-ingot", 2], ["copper-ingot", 1]], "out": [["circuit-board", 2]] }
      ]
    })JSON";

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);
    EXPECT_TRUE(report.ok());

    // 1. Mk.III, mode = extra
    NodeEval extraEval = evaluateNode(
        pkg, "assembler-mk3", "circuit-board",
        {{"prolif", "proliferator-mk3"}, {"prolif_mode", "extra"}});
    ASSERT_EQ(extraEval.status, EvalStatus::OK);
    EXPECT_NEAR(extraEval.craft_time_s, 1.0 / 1.5, 1e-6);
    EXPECT_DOUBLE_EQ(extraEval.findPort("out:0")->amount_per_craft, 2.5);
    const EvaluatedPort* sprayExtra = extraEval.findPort("xin:spray");
    ASSERT_NE(sprayExtra, nullptr);
    EXPECT_TRUE(sprayExtra->active);
    EXPECT_EQ(sprayExtra->resource, "proliferator-mk3");
    EXPECT_DOUBLE_EQ(sprayExtra->amount_per_craft, 0.05);
    EXPECT_DOUBLE_EQ(extraEval.findMetric("power")->coeff, 1.95);

    // 2. Mk.III, mode = speed
    NodeEval speedEval = evaluateNode(
        pkg, "assembler-mk3", "circuit-board",
        {{"prolif", "proliferator-mk3"}, {"prolif_mode", "speed"}});
    ASSERT_EQ(speedEval.status, EvalStatus::OK);
    EXPECT_NEAR(speedEval.craft_time_s, 1.0 / 3.0, 1e-6);
    EXPECT_DOUBLE_EQ(speedEval.findPort("out:0")->amount_per_craft, 2.0);
    const EvaluatedPort* spraySpeed = speedEval.findPort("xin:spray");
    ASSERT_NE(spraySpeed, nullptr);
    EXPECT_TRUE(spraySpeed->active);
    EXPECT_EQ(spraySpeed->resource, "proliferator-mk3");
    EXPECT_DOUBLE_EQ(spraySpeed->amount_per_craft, 0.05);
    EXPECT_DOUBLE_EQ(speedEval.findMetric("power")->coeff, 1.95);

    // 3. prolif = null (unset): prolif_mode is hidden, xin:spray is inactive, no runtime error reading charges
    NodeEval noneEval = evaluateNode(
        pkg, "assembler-mk3", "circuit-board",
        {{"prolif", nullptr}, {"prolif_mode", "extra"}});
    ASSERT_EQ(noneEval.status, EvalStatus::OK);
    EXPECT_FALSE(noneEval.param_visible.at("prolif_mode"));
    EXPECT_FALSE(noneEval.findPort("xin:spray")->active);
    EXPECT_DOUBLE_EQ(noneEval.findPort("xin:spray")->amount_per_craft, 0.0);
    EXPECT_DOUBLE_EQ(noneEval.findPort("out:0")->amount_per_craft, 2.0);
    EXPECT_DOUBLE_EQ(noneEval.findMetric("power")->coeff, 0.78);
}

TEST(GameDataV4ExamplesTest, Example10_5_FactorioAssemblerWithModulesAndBeacons) {
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "factorio", "name": "Factorio", "data_version": "2.0",
                "attrs": { "beacon_efficiency": 1.5 } },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "attributes": {
        "machine": { "power": { "type": "number", "default": 0 },
                     "module_slots": { "type": "number", "default": 0 },
                     "base_prod": { "type": "number", "default": 0 } },
        "recipe":  { "allows_productivity": { "type": "bool", "default": false } }
      },
      "resources": [
        { "id": "iron-plate", "name": "Iron plate" },
        { "id": "copper-cable", "name": "Copper cable" },
        { "id": "electronic-circuit", "name": "Electronic circuit" }
      ],
      "option_sets": {
        "modules": {
          "attrs": { "speed": { "type": "number", "default": 0 },
                     "prod": { "type": "number", "default": 0 },
                     "consumption": { "type": "number", "default": 0 } },
          "options": [
            { "key": "speed-module-3", "label": "Speed module 3",
              "attrs": { "speed": 0.5, "consumption": 0.7 } },
            { "key": "productivity-module-3", "label": "Productivity module 3",
              "attrs": { "speed": -0.15, "prod": 0.1, "consumption": 0.8 } },
            { "key": "efficiency-module-3", "label": "Efficiency module 3",
              "attrs": { "consumption": -0.5 } }
          ]
        }
      },
      "parameters": {
        "modules": { "label": "Modules", "type": "slots", "option_set": "modules",
                     "count": "machine.module_slots",
                     "allowed_if": "option.prod <= 0 || recipe.allows_productivity",
                     "visible_if": "machine.module_slots > 0",
                     "ui": { "widget": "slot_row", "placement": "body" } },
        "beacon_count": { "label": "Beacons", "type": "int", "default": 0, "min": "0", "max": "16",
                          "visible_if": "machine.module_slots > 0",
                          "ui": { "widget": "stepper", "group": "Beacons" } },
        "beacon_modules": { "label": "Beacon modules", "type": "slots", "option_set": "modules",
                            "count": "2", "allowed_if": "option.prod <= 0",
                            "visible_if": "param.beacon_count > 0",
                            "ui": { "widget": "slot_row", "group": "Beacons" } }
      },
      "metrics": {
        "power": { "label": "Power", "unit": "MW", "display": ["body", "totals"], "aggregate": "sum", "format": "%.2f" }
      },
      "profiles": {
        "fa_crafter": {
          "params": ["modules", "beacon_count", "beacon_modules"],
          "let": {
            "n": "param.beacon_count",
            "bmul": "if(n > 0, game.beacon_efficiency * sqrt(n), 0)",
            "s": "sum(param.modules.speed) + bmul * sum(param.beacon_modules.speed)",
            "p": "machine.base_prod + sum(param.modules.prod)",
            "e": "sum(param.modules.consumption) + bmul * sum(param.beacon_modules.consumption)",
            "speed": "max(0.2, 1 + s)",
            "prod": "1 + clamp(p, 0, 3)",
            "energy": "max(0.2, 1 + e)"
          },
          "craft_time": "recipe.time / (machine.speed * speed)",
          "output_amount": "(port.catalyst + (port.amount - port.catalyst) * prod) * port.chance",
          "metrics": { "power": { "per_machine": "machine.power * energy" } }
        }
      },
      "machines": [
        { "id": "assembling-machine-3", "name": "Assembling machine 3", "profile": "fa_crafter",
          "categories": ["crafting"], "speed": 1.25,
          "attrs": { "power": 0.375, "module_slots": 4 } }
      ],
      "recipes": [
        { "id": "electronic-circuit", "name": "Electronic circuit", "category": "crafting", "time": 0.5,
          "attrs": { "allows_productivity": true },
          "in": [["iron-plate", 1], ["copper-cable", 3]], "out": [["electronic-circuit", 1]] }
      ]
    })JSON";

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);
    EXPECT_TRUE(report.ok());

    std::unordered_map<std::string, ParamValue> params = {
        {"modules", ParamValue::slots({
            "productivity-module-3",
            "productivity-module-3",
            "productivity-module-3",
            "productivity-module-3"
        })},
        {"beacon_count", 8},
        {"beacon_modules", ParamValue::slots({
            "speed-module-3",
            "speed-module-3"
        })}
    };

    NodeEval eval = evaluateNode(pkg, "assembling-machine-3", "electronic-circuit", params);
    ASSERT_EQ(eval.status, EvalStatus::OK);

    const double expectedBmul = 1.5 * std::sqrt(8.0);
    const double expectedSpeed = 1.0 + (-0.6 + expectedBmul * 1.0);
    const double expectedEnergy = 1.0 + (3.2 + expectedBmul * 1.4);
    const double expectedCraftTime = 0.5 / (1.25 * expectedSpeed);
    const double expectedPowerCoeff = 0.375 * expectedEnergy;

    EXPECT_NEAR(eval.resolved_lets.at("bmul").asNumber(), 4.242641, 1e-6);
    EXPECT_NEAR(eval.resolved_lets.at("speed").asNumber(), 4.642641, 1e-6);
    EXPECT_DOUBLE_EQ(eval.resolved_lets.at("prod").asNumber(), 1.4);
    EXPECT_NEAR(eval.resolved_lets.at("energy").asNumber(), 10.139697, 1e-6);
    EXPECT_NEAR(eval.craft_time_s, expectedCraftTime, 1e-8);
    EXPECT_NEAR(eval.craft_time_s, 0.086158, 1e-6);

    EXPECT_DOUBLE_EQ(eval.findPort("in:0")->amount_per_craft, 1.0);
    EXPECT_DOUBLE_EQ(eval.findPort("in:1")->amount_per_craft, 3.0);
    EXPECT_DOUBLE_EQ(eval.findPort("out:0")->amount_per_craft, 1.4);

    ASSERT_NE(eval.findMetric("power"), nullptr);
    EXPECT_NEAR(eval.findMetric("power")->coeff, expectedPowerCoeff, 1e-6);
    EXPECT_NEAR(eval.findMetric("power")->coeff, 3.8024, 1e-4);

    // Solved with target 30 circuits/s
    const double solvedCrafts = 30.0 / 1.4; // 21.428571...
    const double solvedMachines = solvedCrafts * eval.craft_time_s; // 1.846240...
    EXPECT_NEAR(solvedMachines, 1.8462, 1e-4);

    NodeMetricsResult metrics = evaluateNodeMetrics(
        pkg, "assembling-machine-3", "electronic-circuit", eval, solvedMachines, solvedCrafts);
    ASSERT_NE(metrics.findMetric("power"), nullptr);
    EXPECT_NEAR(metrics.findMetric("power")->value, 7.020, 1e-3);
}

// ============================================================================
// Part 2: Pre-Solve & Post-Solve Pipeline Edge Cases (§3.1–§3.2, §4.5, §6, §7.6)
// ============================================================================

TEST(GameDataV4EvaluationTest, PortBoundResourceChoiceAndFallbackToStaticRes) {
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "oni", "name": "Oxygen Not Included" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "attributes": {
        "port": { "required_tag": { "type": "string", "default": "" } }
      },
      "resources": [
        { "id": "sand", "name": "Sand", "tags": ["filtration"] },
        { "id": "regolith", "name": "Regolith", "tags": ["filtration"] },
        { "id": "water", "name": "Water" }
      ],
      "parameters": {
        "filter_medium": {
          "label": "Filter Medium", "type": "resource_choice",
          "resources": ["sand", "regolith", "water"],
          "allow_none": true, "default": "sand",
          "visible_if": "port.required_tag != ''",
          "allowed_if": "has_tag(option, port.required_tag)"
        }
      },
      "profiles": {
        "oni_sieve": {
          "params": ["filter_medium"]
        }
      },
      "machines": [
        { "id": "water_sieve", "name": "Water Sieve", "profile": "oni_sieve", "categories": ["sieve"] }
      ],
      "recipes": [
        {
          "id": "sieve_water", "category": "sieve", "time": 1,
          "in": [
            { "res": "sand", "resource_param": "filter_medium", "amount": 1,
              "attrs": { "required_tag": "filtration" } }
          ],
          "out": [["water", 5]]
        },
        {
          "id": "boil_water", "category": "sieve", "time": 1,
          "in": [["water", 5]],
          "out": [["water", 5]]
        }
      ]
    })JSON";

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);

    // 1. Choosing "regolith" (has tag "filtration")
    NodeEval regEval = evaluateNode(
        pkg, "water_sieve", "sieve_water", {{"filter_medium", "regolith"}});
    ASSERT_EQ(regEval.status, EvalStatus::OK);
    EXPECT_TRUE(regEval.param_visible.at("filter_medium"));
    EXPECT_EQ(regEval.findPort("in:0")->resource, "regolith");

    // 2. Choosing "water" (lacks tag "filtration") -> falls back to default "sand" with WARNING
    NodeEval badChoiceEval = evaluateNode(
        pkg, "water_sieve", "sieve_water", {{"filter_medium", "water"}});
    EXPECT_EQ(badChoiceEval.status, EvalStatus::WARNING);
    EXPECT_EQ(badChoiceEval.findPort("in:0")->resource, "sand");

    // 3. Choosing null (allow_none: true) on a port that also declares static "res": "sand" -> falls back to "sand"
    NodeEval nullChoiceEval = evaluateNode(
        pkg, "water_sieve", "sieve_water", {{"filter_medium", nullptr}});
    ASSERT_EQ(nullChoiceEval.status, EvalStatus::OK);
    EXPECT_EQ(nullChoiceEval.findPort("in:0")->resource, "sand");
    EXPECT_TRUE(nullChoiceEval.findPort("in:0")->active);

    // 4. Recipe "boil_water" does not use resource_param "filter_medium" -> parameter auto-hidden!
    NodeEval unusedParamEval = evaluateNode(
        pkg, "water_sieve", "boil_water", {{"filter_medium", "water"}});
    ASSERT_EQ(unusedParamEval.status, EvalStatus::OK);
    EXPECT_FALSE(unusedParamEval.param_visible.at("filter_medium"));
}

TEST(GameDataV4EvaluationTest, GlobalStatusIsolationAndPostSolveMetricErrorIsolation) {
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "test_game", "name": "Test Game" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [
        { "id": "ore", "name": "Ore" },
        { "id": "plate", "name": "Plate" }
      ],
      "globals": {
        "prod_level": { "type": "int", "default": 0, "min": "0", "max": "10" },
        "unused_bad_global": { "type": "int", "default": 10, "min": "10", "max": "0 + global.prod_level" }
      },
      "metrics": {
        "power": { "label": "Power", "aggregate": "sum" },
        "eff_per_machine": { "label": "Eff", "aggregate": "min" },
        "peak_heat": { "label": "Peak Heat", "aggregate": "max" },
        "debug_only": { "label": "Debug", "aggregate": "none" }
      },
      "profiles": {
        "smelter_prof": {
          "let": { "mult": "1 + 0.1 * global.prod_level" },
          "output_amount": "port.amount * mult",
          "metrics": {
            "power": { "per_machine": "10" },
            "eff_per_machine": { "value": "100 / node.machines" },
            "peak_heat": { "per_craft": "50" },
            "debug_only": { "value": "node.crafts * 2" }
          }
        }
      },
      "machines": [
        { "id": "smelter", "profile": "smelter_prof", "categories": ["smelting"] }
      ],
      "recipes": [
        { "id": "smelt_ore", "category": "smelting", "time": 2,
          "in": [["ore", 1]], "out": [["plate", 1]] }
      ]
    })JSON";

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);

    // Resolve globals where `prod_level` = 50 (clamped to 10 -> WARNING) and `unused_bad_global` has min 10 > max 5 -> ERROR
    // Wait: With prod_level = 50 clamped to 10, max is 10. With prod_level = 2, unused_bad_global has min 10 > max 2 -> ERROR!
    ResolvedGlobals globals = resolveProjectGlobals(pkg, {{"prod_level", 2}});
    EXPECT_EQ(globals.status_by_global.at("prod_level"), EvalStatus::OK);
    EXPECT_EQ(globals.status_by_global.at("unused_bad_global"), EvalStatus::ERROR);

    // Because `smelter_prof` only references `global.prod_level` and NEVER references `global.unused_bad_global`,
    // `smelter` evaluates cleanly as OK!
    NodeEval eval = evaluateNode(pkg, "smelter", "smelt_ore", {}, globals);
    EXPECT_EQ(eval.status, EvalStatus::OK);
    EXPECT_DOUBLE_EQ(eval.findPort("out:0")->amount_per_craft, 1.2);

    // Post-solve when solvedMachines == 0: `eff_per_machine` ("100 / node.machines") divides by zero!
    // Only `eff_per_machine` becomes ERROR; `power` and `peak_heat` stay OK!
    NodeMetricsResult mZero = evaluateNodeMetrics(pkg, "smelter", "smelt_ore", eval, 0.0, 0.0, globals);
    ASSERT_NE(mZero.findMetric("eff_per_machine"), nullptr);
    EXPECT_EQ(mZero.findMetric("eff_per_machine")->status, EvalStatus::ERROR);
    EXPECT_EQ(mZero.findMetric("power")->status, EvalStatus::OK);
    EXPECT_DOUBLE_EQ(mZero.findMetric("power")->value, 0.0);

    // Second node with solvedMachines = 2.0, solvedCraftsPerSec = 1.0
    NodeMetricsResult mActive = evaluateNodeMetrics(pkg, "smelter", "smelt_ore", eval, 2.0, 1.0, globals);
    EXPECT_EQ(mActive.findMetric("eff_per_machine")->status, EvalStatus::OK);
    EXPECT_DOUBLE_EQ(mActive.findMetric("eff_per_machine")->value, 50.0);
    EXPECT_DOUBLE_EQ(mActive.findMetric("power")->value, 20.0);
    EXPECT_DOUBLE_EQ(mActive.findMetric("peak_heat")->value, 50.0);

    // Aggregate across both nodes:
    // - `debug_only` (aggregate: "none") is omitted
    // - `eff_per_machine` (min) excludes the errored metric on mZero and takes 50.0 from mActive
    std::vector<NodeMetricsResult> results = {mZero, mActive};
    std::vector<MetricTotal> totals = aggregateMetricTotals(pkg, results);
    ASSERT_EQ(totals.size(), 3u);
    EXPECT_EQ(totals[0].metric_id, "power");
    EXPECT_DOUBLE_EQ(totals[0].value, 20.0);
    EXPECT_EQ(totals[1].metric_id, "eff_per_machine");
    EXPECT_TRUE(totals[1].has_value);
    EXPECT_DOUBLE_EQ(totals[1].value, 50.0);
    EXPECT_EQ(totals[2].metric_id, "peak_heat");
    EXPECT_TRUE(totals[2].has_value);
    EXPECT_DOUBLE_EQ(totals[2].value, 50.0);
}

TEST(GameDataV4EvaluationTest, PostSolveMetricRespectsAllBlockedOrErroredResolvedGlobals) {
    // Finding #2: when every project global is BLOCKED/ERROR, globals.eval_values is empty
    // while globals.status_by_global is populated. evaluateNodeMetrics must NOT discard
    // caller globals and fall back to resolveProjectGlobals(pkg, {}).
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "globals_all_blocked_test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "option_sets": {
        "tiers": {
          "options": [
            { "key": "basic", "label": "Basic" },
            { "key": "pro", "label": "Pro" }
          ]
        }
      },
      "globals": {
        "tier": { "type": "enum", "option_set": "tiers", "default": "pro" }
      },
      "metrics": {
        "power": { "label": "Power", "aggregate": "sum" }
      },
      "resources": [
        { "id": "ore", "name": "Ore" },
        { "id": "plate", "name": "Plate" }
      ],
      "profiles": {
        "smelter_prof": {
          "metrics": {
            "power": { "value": "node.machines * if(global.tier == 'pro', 2, 1)" }
          }
        }
      },
      "machines": [
        { "id": "smelter", "profile": "smelter_prof", "categories": ["smelting"] }
      ],
      "recipes": [
        { "id": "smelt_ore", "category": "smelting", "time": 1, "in": [["ore", 1]], "out": [["plate", 1]] }
      ]
    })JSON";

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);

    // Simulate resolved project globals where the sole global is BLOCKED/ERROR:
    // eval_values is empty, while status_by_global/visible/messages_by_global are populated.
    ResolvedGlobals badGlobals;
    badGlobals.visible["tier"] = true;
    badGlobals.status_by_global["tier"] = EvalStatus::BLOCKED;
    badGlobals.messages_by_global["tier"] = {
        EvalMessage{EvalStatus::BLOCKED, "No allowed tier option", "globals/tier"}
    };
    ASSERT_TRUE(badGlobals.eval_values.empty());

    // Pre-solve doesn't touch global.tier, so NodeEval itself is OK.
    NodeEval eval = evaluateNode(pkg, "smelter", "smelt_ore", {}, badGlobals);
    EXPECT_EQ(eval.status, EvalStatus::OK);

    // Post-solve `value` metric references global.tier and must observe the failed global
    // (not fall back to default "pro" with value 10.0).
    NodeMetricsResult badMetrics = evaluateNodeMetrics(pkg, "smelter", "smelt_ore", eval, 5.0, 5.0, badGlobals);
    const EvaluatedNodeMetric* badPower = badMetrics.findMetric("power");
    ASSERT_NE(badPower, nullptr);
    EXPECT_EQ(badPower->status, EvalStatus::ERROR);
    ASSERT_FALSE(badMetrics.messages.empty());
    EXPECT_EQ(badMetrics.messages.front().text, "No allowed tier option");

    // Convenience: omitting globals (default-constructed ResolvedGlobals{}) still resolves default "pro" (10.0).
    NodeMetricsResult defaultMetrics = evaluateNodeMetrics(pkg, "smelter", "smelt_ore", eval, 5.0, 5.0);
    const EvaluatedNodeMetric* defPower = defaultMetrics.findMetric("power");
    ASSERT_NE(defPower, nullptr);
    EXPECT_EQ(defPower->status, EvalStatus::OK);
    EXPECT_DOUBLE_EQ(defPower->value, 10.0);
}

TEST(GameDataV4EvaluationTest, PreservesSmallAndHighPrecisionNumericLiteralsInExtraPortsAndMetrics) {
    // Finding #4: JSON numeric literals < 5e-7 or with > 6 fractional digits must not truncate via std::to_string.
    const double tiny = 4e-7;
    const double highPrec = 1.2345678901234567e-10;
    const double frac = 1.123456789;
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "numeric_precision_test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "metrics": {
        "m_pm": { "label": "PM", "aggregate": "sum" },
        "m_pc": { "label": "PC", "aggregate": "sum" },
        "m_val": { "label": "Val", "aggregate": "sum" },
        "m_ovr": { "label": "Ovr", "aggregate": "sum" }
      },
      "resources": [
        { "id": "ore", "name": "Ore" },
        { "id": "water", "name": "Water" }
      ],
      "profiles": {
        "base_p": {
          "extra_ports": [
            { "id": "cat_tiny", "dir": "in", "resource": "water", "amount": 4e-7 },
            { "id": "cat_frac", "dir": "in", "resource": "water", "amount": 1.123456789 }
          ],
          "metrics": {
            "m_pm": { "per_machine": 4e-7 },
            "m_pc": { "per_craft": 4e-7 },
            "m_val": { "value": 1.2345678901234567e-10 },
            "m_ovr": { "per_craft": 1 }
          }
        },
        "child_p": {
          "extends": "base_p"
        }
      },
      "machines": [
        { "id": "mach", "profile": "child_p", "categories": ["cat"] }
      ],
      "recipes": [
        {
          "id": "rec", "category": "cat", "time": 1,
          "in": [["ore", 1]], "out": [["ore", 1]],
          "overrides": {
            "metrics": {
              "m_ovr": { "value": 4e-7 }
            }
          }
        }
      ]
    })JSON";

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);

    NodeEval eval = evaluateNode(pkg, "mach", "rec");
    EXPECT_EQ(eval.status, EvalStatus::OK);

    const EvaluatedPort* epTiny = eval.findPort("xin:cat_tiny");
    ASSERT_NE(epTiny, nullptr);
    EXPECT_TRUE(epTiny->active);
    EXPECT_DOUBLE_EQ(epTiny->amount_per_craft, tiny);

    const EvaluatedPort* epFrac = eval.findPort("xin:cat_frac");
    ASSERT_NE(epFrac, nullptr);
    EXPECT_TRUE(epFrac->active);
    EXPECT_DOUBLE_EQ(epFrac->amount_per_craft, frac);

    NodeMetricsResult mres = evaluateNodeMetrics(pkg, "mach", "rec", eval, 1.0, 1.0);
    ASSERT_NE(mres.findMetric("m_pm"), nullptr);
    ASSERT_NE(mres.findMetric("m_pc"), nullptr);
    ASSERT_NE(mres.findMetric("m_val"), nullptr);
    ASSERT_NE(mres.findMetric("m_ovr"), nullptr);
    EXPECT_DOUBLE_EQ(mres.findMetric("m_pm")->value, tiny);
    EXPECT_DOUBLE_EQ(mres.findMetric("m_pc")->value, tiny);
    EXPECT_DOUBLE_EQ(mres.findMetric("m_val")->value, highPrec);
    EXPECT_DOUBLE_EQ(mres.findMetric("m_ovr")->value, tiny);
}

TEST(GameDataV4EvaluationTest, EvaluatedParamUiMetadataAndBlockedVisibility) {
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "ui_meta_test", "name": "UI Meta Test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "attributes": {
        "machine": {
          "somersloop_slots": { "type": "number", "default": 0 },
          "module_slots": { "type": "number", "default": 2 }
        },
        "recipe": {
          "allow_prod": { "type": "bool", "default": true }
        },
        "port": {
          "required_tag": { "type": "string", "default": "" }
        }
      },
      "resources": [
        { "id": "iron_ore", "name": "Iron Ore" },
        { "id": "iron_plate", "name": "Iron Plate" },
        { "id": "sand", "name": "Sand", "tags": ["filtration"] },
        { "id": "regolith", "name": "Regolith", "tags": ["filtration"] },
        { "id": "water", "name": "Water" }
      ],
      "option_sets": {
        "modules": {
          "attrs": {
            "is_prod": { "type": "bool", "default": false },
            "tier": { "type": "number", "default": 1 }
          },
          "options": [
            { "key": "speed1", "label": "Speed 1", "attrs": { "is_prod": false, "tier": 1 } },
            { "key": "prod1", "label": "Prod 1", "attrs": { "is_prod": true, "tier": 1 } },
            { "key": "speed2", "label": "Speed 2", "attrs": { "is_prod": false, "tier": 2 } }
          ]
        },
        "prolif_levels": {
          "options": [
            { "key": "none", "label": "None" },
            { "key": "mk1", "label": "Mk1" }
          ]
        },
        "prolif_modes": {
          "attrs": {
            "is_prod": { "type": "bool", "default": false }
          },
          "options": [
            { "key": "extra", "label": "Extra", "attrs": { "is_prod": true } },
            { "key": "speed", "label": "Speed", "attrs": { "is_prod": false } }
          ]
        }
      },
      "parameters": {
        "overclock": { "label": "Overclock", "type": "number", "min": 1, "max": 250, "default": 100 },
        "somersloops": { "label": "Somersloops", "type": "int", "min": 0, "max": "machine.somersloop_slots", "default": 0 },
        "prolif_level": {
          "label": "Proliferator", "type": "enum",
          "option_set": "prolif_levels",
          "default": "none"
        },
        "prolif_mode": {
          "label": "Prolif Mode", "type": "enum",
          "option_set": "prolif_modes",
          "default": "speed",
          "visible_if": "param.prolif_level != 'none'",
          "allowed_if": "recipe.allow_prod || !option.is_prod"
        },
        "modules": {
          "label": "Modules", "type": "slots",
          "option_set": "modules",
          "count": "machine.module_slots",
          "allow_empty": false,
          "allowed_if": "recipe.allow_prod || !option.is_prod"
        },
        "filter_medium": {
          "label": "Filter Medium", "type": "resource_choice",
          "resources": ["sand", "regolith", "water"],
          "allow_none": true, "default": "sand",
          "allowed_if": "has_tag(option, port.required_tag)"
        }
      },
      "profiles": {
        "main_prof": {
          "params": ["overclock", "somersloops", "prolif_level", "prolif_mode", "filter_medium", "modules"]
        }
      },
      "machines": [
        { "id": "m_basic", "name": "Basic", "profile": "main_prof", "categories": ["craft"], "attrs": { "somersloop_slots": 0, "module_slots": 1 } },
        { "id": "m_adv", "name": "Advanced", "profile": "main_prof", "categories": ["craft"], "attrs": { "somersloop_slots": 2, "module_slots": 3 } }
      ],
      "recipes": [
        {
          "id": "r_no_filter", "category": "craft", "time": 1,
          "attrs": { "allow_prod": false },
          "in": [["iron_ore", 1]], "out": [["iron_plate", 1]]
        },
        {
          "id": "r_with_filter", "category": "craft", "time": 1,
          "attrs": { "allow_prod": true },
          "in": [{ "res": "sand", "resource_param": "filter_medium", "amount": 1, "attrs": { "required_tag": "filtration" } }],
          "out": [["water", 1]]
        }
      ]
    })JSON";

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);

    // 1. Blocked node (modules allow_empty=false with empty slots) still populates param_ui & param_visible!
    NodeEval blockedEval = evaluateNode(pkg, "m_basic", "r_no_filter", {});
    EXPECT_EQ(blockedEval.status, EvalStatus::BLOCKED);
    ASSERT_TRUE(blockedEval.param_ui.contains("modules"));
    ASSERT_TRUE(blockedEval.param_visible.contains("modules"));
    EXPECT_TRUE(blockedEval.param_visible.at("modules"));
    EXPECT_EQ(blockedEval.param_ui.at("modules").slot_count, 1u);
    EXPECT_EQ(blockedEval.param_ui.at("modules").allowed_options,
              (std::vector<std::string>{"speed1", "speed2"})); // prod1 filtered by allow_prod=false

    // Dynamic bounds on static/dynamic number & int params
    ASSERT_TRUE(blockedEval.param_ui.contains("overclock"));
    ASSERT_TRUE(blockedEval.param_ui.contains("somersloops"));
    EXPECT_TRUE(blockedEval.param_ui.at("overclock").has_min);
    EXPECT_TRUE(blockedEval.param_ui.at("overclock").has_max);
    EXPECT_DOUBLE_EQ(blockedEval.param_ui.at("overclock").min_val, 1.0);
    EXPECT_DOUBLE_EQ(blockedEval.param_ui.at("overclock").max_val, 250.0);
    EXPECT_DOUBLE_EQ(blockedEval.param_ui.at("somersloops").max_val, 0.0);

    // visible_if hides prolif_mode when prolif_level == 'none'
    ASSERT_TRUE(blockedEval.param_visible.contains("prolif_mode"));
    ASSERT_TRUE(blockedEval.param_ui.contains("prolif_mode"));
    EXPECT_FALSE(blockedEval.param_visible.at("prolif_mode"));
    EXPECT_FALSE(blockedEval.param_ui.at("prolif_mode").visible);

    // Port-bound resource_choice auto-hides on recipe not referencing it
    ASSERT_TRUE(blockedEval.param_visible.contains("filter_medium"));
    ASSERT_TRUE(blockedEval.param_ui.contains("filter_medium"));
    EXPECT_FALSE(blockedEval.param_visible.at("filter_medium"));
    EXPECT_FALSE(blockedEval.param_ui.at("filter_medium").visible);

    // 2. Partial empty slots on allow_empty=false still BLOCKS
    NodeEval partialEmptyEval = evaluateNode(
        pkg, "m_adv", "r_with_filter",
        {
            {"prolif_level", std::string("mk1")},
            {"modules", ParamValue::slots({"prod1", "speed2", std::nullopt})}
        });
    EXPECT_EQ(partialEmptyEval.status, EvalStatus::BLOCKED);

    // 3. Active node with m_adv + r_with_filter + full non-empty modules + prolif_level=mk1
    NodeEval okEval = evaluateNode(
        pkg, "m_adv", "r_with_filter",
        {
            {"prolif_level", std::string("mk1")},
            {"modules", ParamValue::slots({"prod1", "speed2", "speed1"})}
        });
    EXPECT_EQ(okEval.status, EvalStatus::OK);
    ASSERT_TRUE(okEval.param_ui.contains("somersloops"));
    ASSERT_TRUE(okEval.param_ui.contains("prolif_mode"));
    ASSERT_TRUE(okEval.param_ui.contains("modules"));
    ASSERT_TRUE(okEval.param_ui.contains("filter_medium"));
    EXPECT_DOUBLE_EQ(okEval.param_ui.at("somersloops").max_val, 2.0);
    EXPECT_TRUE(okEval.param_ui.at("prolif_mode").visible);
    EXPECT_EQ(okEval.param_ui.at("prolif_mode").allowed_options,
              (std::vector<std::string>{"extra", "speed"}));
    EXPECT_EQ(okEval.param_ui.at("modules").slot_count, 3u);
    EXPECT_EQ(okEval.param_ui.at("modules").allowed_options,
              (std::vector<std::string>{"speed1", "prod1", "speed2"}));
    EXPECT_TRUE(okEval.param_ui.at("filter_medium").visible);
    EXPECT_EQ(okEval.param_ui.at("filter_medium").allowed_resources,
              (std::vector<std::string>{"sand", "regolith"}));
}

// ============================================================================
// Part 3: Validation Rules (F-1..F-5, V-*) & Cascade Entity Dropping (§9.1–§9.2)
// ============================================================================

TEST(GameDataV4ValidationTest, FatalRules_F1_To_F5) {
    GamePackage pkg;
    LoadReport report;

    // F-1: Missing manifest file on disk
    EXPECT_FALSE(loadGamePackage("/nonexistent/path/to/game.json", pkg, report));
    EXPECT_TRUE(report.fatal);
    EXPECT_TRUE(report.hasRule("F-1"));

    // F-2: Invalid JSON in manifest
    EXPECT_FALSE(loadGamePackageFromMemory("{ broken json ", {}, pkg, report));
    EXPECT_TRUE(report.fatal);
    EXPECT_TRUE(report.hasRule("F-2"));

    // F-3: Unsupported schema_version
    EXPECT_FALSE(loadGamePackageFromMemory(
        R"({"schema_version": 3, "game": {"id": "g"}, "units": {"recipe_time": "s", "default_rate": "s"}})",
        {}, pkg, report));
    EXPECT_TRUE(report.fatal);
    EXPECT_TRUE(report.hasRule("F-3"));

    // F-4: Invalid game.id
    EXPECT_FALSE(loadGamePackageFromMemory(
        R"({"schema_version": 4, "game": {"id": "Bad ID!"}, "units": {"recipe_time": "s", "default_rate": "s"}})",
        {}, pkg, report));
    EXPECT_TRUE(report.fatal);
    EXPECT_TRUE(report.hasRule("F-4"));

    // F-5: recipe_time = 'tick' without ticks_per_second, or bad default_rate
    EXPECT_FALSE(loadGamePackageFromMemory(
        R"({"schema_version": 4, "game": {"id": "g"}, "units": {"recipe_time": "tick", "default_rate": "s"}})",
        {}, pkg, report));
    EXPECT_TRUE(report.fatal);
    EXPECT_TRUE(report.hasRule("F-5"));

    EXPECT_FALSE(loadGamePackageFromMemory(
        R"({"schema_version": 4, "game": {"id": "g"}, "units": {"recipe_time": "s", "default_rate": "minute"}})",
        {}, pkg, report));
    EXPECT_TRUE(report.fatal);
    EXPECT_TRUE(report.hasRule("F-5"));
}

TEST(GameDataV4ValidationTest, IncludeAndShardRules_V_INC_1_To_3_And_DuplicateKeys_V_ID_2) {
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "include": [
        "missing_shard.json",
        "broken_shard.json",
        "../outside.json",
        "manifest_key_shard.json",
        "valid_shard.json"
      ],
      "resources": [
        { "id": "iron", "name": "Iron" }
      ],
      "machines": [
        { "id": "m1", "categories": ["cat"] }
      ]
    })JSON";

    std::unordered_map<std::string, std::string> shards = {
        {"broken_shard.json", "{ not valid json"},
        {"manifest_key_shard.json", R"({"include": ["other.json"]})"},
        // valid_shard has an in-object duplicate key ("iron" vs "iron" in tables) and a valid recipe
        {"valid_shard.json", R"({
          "tables": { "t1": { "a": 1, "a": 2 } },
          "recipes": [
            { "id": "r1", "category": "cat", "time": 1, "in": [["iron", 1]], "out": [["iron", 1]] }
          ]
        })"}
    };

    GamePackage pkg;
    LoadReport report;
    // Without ignoreErrors, returns false because errors exist
    EXPECT_FALSE(loadGamePackageFromMemory(manifest, shards, pkg, report, false));
    EXPECT_FALSE(report.fatal);
    EXPECT_TRUE(report.hasRule("V-INC-1"));
    EXPECT_TRUE(report.hasRule("V-INC-2"));
    EXPECT_TRUE(report.hasRule("V-INC-3"));
    EXPECT_TRUE(report.hasRule("V-ID-2"));

    // With ignoreErrors ("Load anyway"), loads surviving entities and preserves first key "a": 1!
    ASSERT_TRUE(loadGamePackageFromMemory(manifest, shards, pkg, report, true));
    EXPECT_TRUE(report.degraded);
    ASSERT_NE(pkg.findTable("t1"), nullptr);
    EXPECT_DOUBLE_EQ(pkg.findTable("t1")->object_values.at("a"), 1.0);
    EXPECT_NE(pkg.findRecipe("r1"), nullptr);
}

TEST(GameDataV4ValidationTest, StructuralIdRefAttrTableNumRules) {
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "test", "attrs": { "id": 1 } },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "attributes": {
        "machine": {
          "speed": { "type": "number", "default": 1 },
          "power": { "type": "number", "default": 10 }
        }
      },
      "tables": {
        "empty_tbl": [],
        "bad_tbl": [1, "two"]
      },
      "resources": [
        { "id": "iron", "name": "Iron" },
        { "id": "iron", "name": "Duplicate Iron" },
        { "id": "Bad Slug", "name": "Bad" },
        { "id": "unk_kind_res", "kind": "plasma" }
      ],
      "machines": [
        { "id": "m_valid", "categories": ["cat"], "speed": 1, "attrs": { "power": 20 } },
        { "id": "m_bad_speed", "categories": ["cat"], "speed": 0 },
        { "id": "m_bad_prof", "profile": "nonexistent_prof", "categories": ["cat"] },
        { "id": "m_undecl_attr", "categories": ["cat"], "attrs": { "powr": 5 } },
        { "id": "m_wrong_attr_type", "categories": ["cat"], "attrs": { "power": "high" } }
      ],
      "recipes": [
        { "id": "r_unknown_field", "category": "cat", "time": 1, "ammount": 5, "in": [["iron", 1]] },
        { "id": "r_bad_time", "category": "cat", "time": -1, "in": [["iron", 1]] },
        { "id": "r_bad_amount", "category": "cat", "time": 1, "in": [["iron", 0]] },
        { "id": "r_bad_chance", "category": "cat", "time": 1, "in": [["iron", 1, 1.5]] },
        { "id": "r_empty_ports", "category": "cat", "time": 1, "in": [], "out": [] },
        { "id": "r_both_cat_mach", "category": "cat", "machines": ["m_valid"], "time": 1, "in": [["iron", 1]] },
        { "id": "r_no_mach_for_cat", "category": "missing_cat", "time": 1, "in": [["iron", 1]] },
        { "id": "r_unknown_res", "category": "cat", "time": 1, "in": [["irn", 1]] },
        { "id": "r_valid", "category": "cat", "time": 1, "in": [["iron", 1]], "out": [["iron", 1]] }
      ]
    })JSON";

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackageFromMemory(manifest, {}, pkg, report, true));
    EXPECT_TRUE(report.degraded);

    EXPECT_TRUE(report.hasRule("V-STRUCT-1"));
    EXPECT_TRUE(report.hasRule("V-ID-1"));
    EXPECT_TRUE(report.hasRule("V-ID-2"));
    EXPECT_TRUE(report.hasRule("V-REF-1"));
    EXPECT_TRUE(report.hasRule("V-REF-2"));
    EXPECT_TRUE(report.hasRule("V-REF-3"));
    EXPECT_TRUE(report.hasRule("V-REF-4"));
    EXPECT_TRUE(report.hasRule("V-REF-5"));
    EXPECT_TRUE(report.hasRule("V-ATTR-1"));
    EXPECT_TRUE(report.hasRule("V-ATTR-2"));
    EXPECT_TRUE(report.hasRule("V-TBL-1"));
    EXPECT_TRUE(report.hasRule("V-NUM-1"));
    EXPECT_TRUE(report.hasRule("V-NUM-2"));
    EXPECT_TRUE(report.hasRule("V-NUM-3"));
    EXPECT_TRUE(report.hasRule("V-NUM-4"));

    // Surviving valid entities:
    EXPECT_NE(pkg.findResource("iron"), nullptr);
    EXPECT_EQ(pkg.findResource("iron")->name, "Iron"); // First definition preserved!
    EXPECT_NE(pkg.findMachine("m_valid"), nullptr);
    EXPECT_NE(pkg.findRecipe("r_valid"), nullptr);
}

TEST(GameDataV4ValidationTest, ParameterProfileOverrideAndMetricRules) {
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [
        { "id": "iron", "name": "Iron", "tags": ["metal"] }
      ],
      "option_sets": {
        "empty_set": { "options": [] },
        "dup_set": { "options": [{ "key": "a" }, { "key": "a" }] },
        "valid_set": { "options": [{ "key": "a" }, { "key": "b" }] }
      },
      "parameters": {
        "p_missing_def": { "type": "number" },
        "p_out_of_range": { "type": "int", "default": 5, "min": 0, "max": 4 },
        "p_bad_opt": { "type": "enum", "option_set": "valid_set", "default": "c" },
        "p_bad_field": { "type": "number", "default": 1, "count": 2 },
        "p_warn_widget": { "type": "bool", "default": true, "ui": { "widget": "slider" } },
        "p_port_bound": { "type": "resource_choice", "tag": "metal", "default": "iron",
                          "visible_if": "port.amount > 0" }
      },
      "globals": {
        "g_cycle_1": { "type": "number", "default": 1, "min": "global.g_cycle_2" },
        "g_cycle_2": { "type": "number", "default": 1, "min": "global.g_cycle_1" }
      },
      "metrics": {
        "bad_metric": { "aggregate": "max", "split_sign": true },
        "good_metric": { "aggregate": "sum" }
      },
      "profiles": {
        "prof_unknown_ext": { "extends": "no_such_parent" },
        "prof_cycle_a": { "extends": "prof_cycle_b" },
        "prof_cycle_b": { "extends": "prof_cycle_a" },
        "prof_bad_metric_form": {
          "metrics": { "good_metric": { "per_machine": "1", "per_craft": "2" } }
        },
        "prof_let_cycle": {
          "let": { "x": "y + 1", "y": "x + 1" }
        },
        "prof_bad_ctx": {
          "craft_time": "node.machines"
        },
        "prof_bad_type": {
          "port_enabled": "1 + 2"
        },
        "prof_undecl_param": {
          "let": { "x": "param.p_warn_widget" }
        },
        "prof_port_bound_in_let": {
          "params": ["p_port_bound"],
          "let": { "x": "if(param.p_port_bound == 'iron', 1, 0)" }
        },
        "prof_survives_dropped_metric": {
          "params": ["p_warn_widget"],
          "let": { "mult": "if(param.p_warn_widget, 2, 1)" },
          "metrics": {
            "bad_metric": { "per_machine": "10" },
            "good_metric": { "per_machine": "5 * mult" }
          }
        }
      },
      "machines": [
        { "id": "m1", "profile": "prof_survives_dropped_metric", "categories": ["cat"] }
      ],
      "recipes": [
        {
          "id": "r_bad_ovr_key", "category": "cat", "time": 1,
          "in": [["iron", 1]],
          "overrides": { "params": ["p_warn_widget"] }
        },
        {
          "id": "r_bad_ovr_let_type", "category": "cat", "time": 1,
          "in": [["iron", 1]],
          "overrides": { "let": { "mult": "true" } }
        },
        {
          "id": "r_dup_resource_param", "category": "cat", "time": 1,
          "in": [
            { "resource_param": "p_port_bound", "amount": 1 },
            { "resource_param": "p_port_bound", "amount": 1 }
          ]
        },
        {
          "id": "r_valid", "category": "cat", "time": 1,
          "in": [["iron", 1]], "out": [["iron", 1], ["iron", 1]]
        }
      ]
    })JSON";

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackageFromMemory(manifest, {}, pkg, report, true));
    EXPECT_TRUE(report.degraded);

    EXPECT_TRUE(report.hasRule("V-PAR-1"));
    EXPECT_TRUE(report.hasRule("V-PAR-2"));
    EXPECT_TRUE(report.hasRule("V-PAR-3"));
    EXPECT_TRUE(report.hasRule("V-PAR-4"));
    EXPECT_TRUE(report.hasRule("V-PAR-5"));
    EXPECT_TRUE(report.hasRule("V-EXPR-3"));
    EXPECT_TRUE(report.hasRule("V-EXPR-4"));
    EXPECT_TRUE(report.hasRule("V-EXPR-5"));
    EXPECT_TRUE(report.hasRule("V-EXPR-6"));
    EXPECT_TRUE(report.hasRule("V-PROF-1"));
    EXPECT_TRUE(report.hasRule("V-PROF-2"));
    EXPECT_TRUE(report.hasRule("V-PROF-3"));
    EXPECT_TRUE(report.hasRule("V-PROF-4"));
    EXPECT_TRUE(report.hasRule("V-OVR-1"));
    EXPECT_TRUE(report.hasRule("V-OVR-2"));
    EXPECT_TRUE(report.hasRule("V-MET-1"));
    EXPECT_TRUE(report.hasRule("V-W-3"));

    // Verify V-PAR-5 fell back widget to Checkbox
    ASSERT_NE(pkg.findParameter("p_warn_widget"), nullptr);
    EXPECT_EQ(pkg.findParameter("p_warn_widget")->ui.widget, UiWidget::Checkbox);

    // Verify V-MET-1 dropped `bad_metric` from `prof_survives_dropped_metric` WITHOUT dropping the profile!
    const Profile* survProf = pkg.findProfile("prof_survives_dropped_metric");
    ASSERT_NE(survProf, nullptr);
    EXPECT_EQ(survProf->metrics.count("bad_metric"), 0u);
    EXPECT_EQ(survProf->metrics.count("good_metric"), 1u);
    EXPECT_NE(pkg.findMachine("m1"), nullptr);
    EXPECT_NE(pkg.findRecipe("r_valid"), nullptr);
}

TEST(GameDataV4ValidationTest, SecondaryErrorSuppressionAndCascadeDropOrder) {
    // One broken resource ("broken_res" with invalid kind) is referenced by:
    // - parameter p1 -> referenced by parameter p2 -> referenced by profile prof1 -> child profile prof2 -> machine m1 -> recipe r1
    // Verify that secondary V-REF-1 / V-EXPR-5 / V-REF-4 / V-REF-3 errors are suppressed so only 1 primary error is reported,
    // while all dependent entities are cleanly recorded in LoadReport.dropped!
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "cascade_test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [
        { "id": "broken_res", "kind": "unknown_kind" },
        { "id": "good_res", "name": "Good" }
      ],
      "parameters": {
        "p1": { "type": "resource_choice", "resources": ["broken_res"], "default": "broken_res" },
        "p2": { "type": "number", "default": 1, "visible_if": "param.p1 == 'broken_res'" }
      },
      "profiles": {
        "prof1": { "params": ["p1", "p2"] },
        "prof2": { "extends": "prof1" }
      },
      "machines": [
        { "id": "m_dropped", "profile": "prof2", "categories": ["dropped_cat"] },
        { "id": "m_good", "categories": ["good_cat"] }
      ],
      "recipes": [
        { "id": "r_dropped_by_cat", "category": "dropped_cat", "time": 1, "in": [["good_res", 1]] },
        { "id": "r_dropped_by_res", "category": "good_cat", "time": 1, "in": [["broken_res", 1]] },
        { "id": "r_good", "category": "good_cat", "time": 1, "in": [["good_res", 1]], "out": [["good_res", 1]] }
      ]
    })JSON";

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackageFromMemory(manifest, {}, pkg, report, true));
    EXPECT_TRUE(report.degraded);

    // Only the primary V-REF-5 error on `broken_res` should be in `report.errors`!
    ASSERT_EQ(report.errors.size(), 1u);
    EXPECT_EQ(report.errors.front().rule_id, "V-REF-5");

    // Verify full transitive cascade in `report.dropped`
    EXPECT_EQ(report.dropped["resources"], std::vector<std::string>({"broken_res"}));
    EXPECT_EQ(report.dropped["parameters"], std::vector<std::string>({"p1", "p2"}));
    EXPECT_EQ(report.dropped["profiles"], std::vector<std::string>({"prof1", "prof2"}));
    EXPECT_EQ(report.dropped["machines"], std::vector<std::string>({"m_dropped"}));
    EXPECT_EQ(report.dropped["recipes"], std::vector<std::string>({"r_dropped_by_cat", "r_dropped_by_res"}));

    // Surviving entities work normally
    EXPECT_NE(pkg.findResource("good_res"), nullptr);
    EXPECT_NE(pkg.findMachine("m_good"), nullptr);
    EXPECT_NE(pkg.findRecipe("r_good"), nullptr);
}

TEST(GameDataV4ValidationTest, UnusedEntityWarnings_V_W_2_And_MissingIcons_V_W_1) {
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "warn_test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "attributes": {
        "resource": { "burnt_result": { "type": "string", "default": "" } }
      },
      "resources": [
        { "id": "coal", "attrs": { "burnt_result": "ash" } },
        { "id": "ash" },
        { "id": "unused_res" }
      ],
      "option_sets": {
        "unused_oset": { "options": [{ "key": "a" }] }
      },
      "tables": {
        "unused_tbl": [1, 2, 3]
      },
      "parameters": {
        "fuel": { "type": "resource_choice", "resources": ["coal"], "default": "coal" },
        "unused_param": { "type": "number", "default": 1 }
      },
      "metrics": {
        "unused_metric": { "aggregate": "sum" }
      },
      "profiles": {
        "burner": {
          "params": ["fuel"],
          "extra_ports": [
            { "id": "fuel_in", "dir": "in", "resource_param": "fuel", "amount": "1" },
            { "id": "ash_out", "dir": "out", "resource_expr": "param.fuel.burnt_result", "amount": "1" }
          ]
        }
      },
      "machines": [
        { "id": "boiler", "profile": "burner", "categories": ["boil"] },
        { "id": "unused_machine", "categories": ["other"] }
      ],
      "recipes": [
        { "id": "make_steam", "category": "boil", "time": 1, "out": [["coal", 1]] }
      ]
    })JSON";

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackageFromMemory(
        manifest, {}, pkg, report, false, "/nonexistent/icon_test_root"));
    EXPECT_TRUE(report.ok());

    // Note: "ash" is referenced via coal's `burnt_result` attribute read by `resource_expr`,
    // so only `unused_res` should get a resource V-W-2 warning (along with unused_param, unused_oset, unused_tbl, unused_metric, unused_machine).
    EXPECT_EQ(report.warnings.size(), 6u);
    for (const auto& w : report.warnings) {
        EXPECT_EQ(w.rule_id, "V-W-2");
        EXPECT_EQ(w.message.find("'ash'"), std::string::npos);
    }

    // Check V-W-1 missing icons
    checkMissingIcons(pkg, report);
    EXPECT_TRUE(report.hasRule("V-W-1"));
}

TEST(GameDataV4ValidationTest, FilesystemPackageLoadAndGlobDeduplication) {
    namespace fs = std::filesystem;
    fs::path tempRoot = fs::temp_directory_path() / "fp_test_gamedata_v4_pkg";
    std::error_code ec;
    fs::remove_all(tempRoot, ec);
    fs::create_directories(tempRoot / "recipes" / "sub", ec);

    auto writeFile = [](const fs::path& p, const std::string& content) {
        std::ofstream ofs(p, std::ios::binary);
        ofs << content;
    };

    writeFile(tempRoot / "game.json", R"JSON({
      "schema_version": 4,
      "game": { "id": "disk_test", "name": "Disk Test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "include": [
        "resources.json",
        "recipes/tier1.json",
        "recipes/"
      ],
      "machines": [
        { "id": "assembler", "categories": ["crafting"] }
      ]
    })JSON");

    writeFile(tempRoot / "resources.json", R"JSON({
      "resources": [
        { "id": "iron_ore" },
        { "id": "iron_plate" },
        { "id": "iron_rod" }
      ]
    })JSON");

    // Both "recipes/*.json" and "recipes/**/*.json" match "recipes/tier1.json";
    // loader must deduplicate it so r1 is only loaded once!
    writeFile(tempRoot / "recipes" / "tier1.json", R"JSON({
      "recipes": [
        { "id": "r1", "category": "crafting", "time": 1, "in": [["iron_ore", 1]], "out": [["iron_plate", 1]] }
      ]
    })JSON");

    // Only "recipes/**/*.json" matches "recipes/sub/tier2.json"
    writeFile(tempRoot / "recipes" / "sub" / "tier2.json", R"JSON({
      "recipes": [
        { "id": "r2", "category": "crafting", "time": 2, "in": [["iron_plate", 1]], "out": [["iron_rod", 1]] }
      ]
    })JSON");

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackage(tempRoot, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(pkg.recipes.size(), 2u);
    EXPECT_NE(pkg.findRecipe("r1"), nullptr);
    EXPECT_NE(pkg.findRecipe("r2"), nullptr);

    fs::remove_all(tempRoot, ec);
}

namespace fs = std::filesystem;

static fs::path locateRepoGameDataDir() {
    std::error_code ec;
    fs::path cur = fs::current_path(ec);
    for (int i = 0; i < 6 && !cur.empty(); ++i) {
        fs::path cand = cur / "data" / "game_data";
        if (fs::exists(cand / "satisfactory" / "game.json", ec) &&
            fs::exists(cand / "mindustry" / "game.json", ec)) {
            return cand;
        }
        cand = cur / "game_data";
        if (fs::exists(cand / "satisfactory" / "game.json", ec) &&
            fs::exists(cand / "mindustry" / "game.json", ec)) {
            return cand;
        }
        if (!cur.has_parent_path() || cur == cur.parent_path()) break;
        cur = cur.parent_path();
    }
    return {};
}

TEST(GameDataV4ValidationTest, ProductionPackagesScanWithoutLegacyGdFiles) {
    const fs::path gameDataDir = locateRepoGameDataDir();
    ASSERT_FALSE(gameDataDir.empty()) << "Could not locate data/game_data directory";

    std::error_code ec;
    EXPECT_FALSE(fs::exists(gameDataDir / "satisfactory" / "game_datas", ec));
    EXPECT_FALSE(fs::exists(gameDataDir / "mindustry" / "game_datas", ec));

    const auto scanned = scanForGameData(gameDataDir);
    bool foundSatisfactory = false;
    bool foundMindustry = false;
    for (const auto& pkg : scanned) {
        if (pkg.uuid == "satisfactory") {
            foundSatisfactory = true;
            EXPECT_EQ(pkg.gameName, "Satisfactory");
            EXPECT_EQ(pkg.dataVersion, "1.1");
        } else if (pkg.uuid == "mindustry") {
            foundMindustry = true;
            EXPECT_EQ(pkg.gameName, "Mindustry");
            EXPECT_EQ(pkg.dataVersion, "build-154-3");
        }
    }
    EXPECT_TRUE(foundSatisfactory);
    EXPECT_TRUE(foundMindustry);
}

TEST(GameDataV4ValidationTest, SatisfactoryProductionPackageValidatesAndEvaluates) {
    const fs::path gameDataDir = locateRepoGameDataDir();
    ASSERT_FALSE(gameDataDir.empty());

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackage(gameDataDir / "satisfactory", pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);
    EXPECT_TRUE(report.ok());
    EXPECT_TRUE(report.errors.empty());
    EXPECT_TRUE(report.warnings.empty())
        << (report.warnings.empty() ? "" : report.warnings.front().formatted);

    checkMissingIcons(pkg, report);
    EXPECT_TRUE(report.warnings.empty())
        << (report.warnings.empty() ? "" : report.warnings.front().formatted);

    EXPECT_EQ(pkg.game.id, "satisfactory");
    EXPECT_EQ(pkg.units.default_rate, RateUnit::PerMinute);
    EXPECT_EQ(pkg.resources.size(), 152u);
    EXPECT_EQ(pkg.machines.size(), 22u);
    EXPECT_EQ(pkg.recipes.size(), 308u);

    const Resource* water = pkg.findResource("water");
    ASSERT_NE(water, nullptr);
    EXPECT_EQ(water->kind, "fluid");
    const Resource* plate = pkg.findResource("iron-plate");
    ASSERT_NE(plate, nullptr);
    EXPECT_EQ(plate->kind, "item");

    // Production machine with clock speed & somersloops (spec section 10.1 formulas)
    {
        const std::unordered_map<std::string, ParamValue> params = {
            {"clock", ParamValue::number(150.0)},
            {"somersloops", ParamValue::number(1.0)},
        };
        NodeEval ne = evaluateNode(pkg, "constructor", "iron-plate", params, "Constructor");
        EXPECT_EQ(ne.status, EvalStatus::OK);
        EXPECT_NEAR(ne.craft_time_s, 4.0, 1e-9);
        const EvaluatedPort* in0 = ne.findPort("in:0");
        const EvaluatedPort* out0 = ne.findPort("out:0");
        ASSERT_NE(in0, nullptr);
        ASSERT_NE(out0, nullptr);
        EXPECT_NEAR(in0->amount_per_craft, 3.0, 1e-9);
        EXPECT_NEAR(out0->amount_per_craft, 4.0, 1e-9);

        NodeMetricsResult mr = evaluateNodeMetrics(pkg, "constructor", "iron-plate", ne, 1.5, 1.5 / 4.0);
        const EvaluatedNodeMetric* power = mr.findMetric("power");
        const EvaluatedNodeMetric* shards = mr.findMetric("shards");
        const EvaluatedNodeMetric* sloops = mr.findMetric("sloops");
        ASSERT_NE(power, nullptr);
        ASSERT_NE(shards, nullptr);
        ASSERT_NE(sloops, nullptr);
        EXPECT_TRUE(power->enabled);
        EXPECT_NEAR(power->value, 1.5 * 16.0 * std::pow(1.5, 1.321928), 1e-6);
        EXPECT_TRUE(shards->enabled);
        EXPECT_DOUBLE_EQ(shards->value, 2.0);
        EXPECT_TRUE(sloops->enabled);
        EXPECT_DOUBLE_EQ(sloops->value, 2.0);
    }

    // Extractor (miner-mk2: somersloop_slots == 0 hides somersloops & disables sloops metric)
    {
        const std::unordered_map<std::string, ParamValue> params = {
            {"clock", ParamValue::number(200.0)},
        };
        NodeEval ne = evaluateNode(pkg, "miner-mk2", "iron-ore", params, "Miner");
        EXPECT_EQ(ne.status, EvalStatus::OK);
        EXPECT_FALSE(ne.param_visible.at("somersloops"));
        EXPECT_NEAR(ne.craft_time_s, 0.25, 1e-9);

        NodeMetricsResult mr = evaluateNodeMetrics(pkg, "miner-mk2", "iron-ore", ne, 1.0, 4.0);
        const EvaluatedNodeMetric* power = mr.findMetric("power");
        const EvaluatedNodeMetric* shards = mr.findMetric("shards");
        const EvaluatedNodeMetric* sloops = mr.findMetric("sloops");
        ASSERT_NE(power, nullptr);
        ASSERT_NE(shards, nullptr);
        ASSERT_NE(sloops, nullptr);
        EXPECT_NEAR(power->value, 15.0 * std::pow(2.0, 1.321928), 1e-6);
        EXPECT_DOUBLE_EQ(shards->value, 2.0);
        EXPECT_FALSE(sloops->enabled);
    }

    // Generator (linear clock power & fuel scaling, negative power output)
    {
        const std::unordered_map<std::string, ParamValue> params = {
            {"clock", ParamValue::number(200.0)},
        };
        NodeEval ne = evaluateNode(pkg, "coal-powered-generator", "burn-coal", params, "CoalGen");
        EXPECT_EQ(ne.status, EvalStatus::OK);
        EXPECT_NEAR(ne.craft_time_s, 2.0, 1e-9);

        NodeMetricsResult mr = evaluateNodeMetrics(pkg, "coal-powered-generator", "burn-coal", ne, 1.0, 0.5);
        const EvaluatedNodeMetric* power = mr.findMetric("power");
        const EvaluatedNodeMetric* shards = mr.findMetric("shards");
        ASSERT_NE(power, nullptr);
        ASSERT_NE(shards, nullptr);
        EXPECT_NEAR(power->value, -150.0, 1e-6);
        EXPECT_DOUBLE_EQ(shards->value, 2.0);
    }

    // Variable-power Particle Accelerator recipes (Diamonds 500 MW avg vs Nuclear Pasta 1000 MW avg)
    {
        NodeEval neDiam = evaluateNode(pkg, "particle-accelerator", "diamonds");
        EXPECT_EQ(neDiam.status, EvalStatus::OK);
        NodeMetricsResult mrDiam = evaluateNodeMetrics(pkg, "particle-accelerator", "diamonds", neDiam, 1.0, 0.5);
        ASSERT_NE(mrDiam.findMetric("power"), nullptr);
        EXPECT_NEAR(mrDiam.findMetric("power")->value, 500.0, 1e-6);

        NodeEval nePasta = evaluateNode(pkg, "particle-accelerator", "nuclear-pasta");
        EXPECT_EQ(nePasta.status, EvalStatus::OK);
        NodeMetricsResult mrPasta = evaluateNodeMetrics(pkg, "particle-accelerator", "nuclear-pasta", nePasta, 1.0, 1.0 / 120.0);
        ASSERT_NE(mrPasta.findMetric("power"), nullptr);
        EXPECT_NEAR(mrPasta.findMetric("power")->value, 1000.0, 1e-6);
    }

    // Space Elevator uses default profile (no power / clock / somersloops)
    {
        NodeEval neElev = evaluateNode(pkg, "space-elevator", "phase-1-distribution-platform");
        EXPECT_EQ(neElev.status, EvalStatus::OK);
        EXPECT_TRUE(neElev.metric_coeffs.empty());
    }
}

TEST(GameDataV4ValidationTest, MindustryProductionPackageValidatesAndEvaluates) {
    const fs::path gameDataDir = locateRepoGameDataDir();
    ASSERT_FALSE(gameDataDir.empty());

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackage(gameDataDir / "mindustry", pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);
    EXPECT_TRUE(report.ok());
    EXPECT_TRUE(report.errors.empty());
    EXPECT_TRUE(report.warnings.empty())
        << (report.warnings.empty() ? "" : report.warnings.front().formatted);

    checkMissingIcons(pkg, report);
    EXPECT_TRUE(report.warnings.empty())
        << (report.warnings.empty() ? "" : report.warnings.front().formatted);

    EXPECT_EQ(pkg.game.id, "mindustry");
    EXPECT_EQ(pkg.units.default_rate, RateUnit::PerSecond);
    EXPECT_EQ(pkg.resources.size(), 84u);
    EXPECT_EQ(pkg.machines.size(), 75u);
    EXPECT_EQ(pkg.recipes.size(), 119u);

    const Resource* cryo = pkg.findResource("cryofluid");
    ASSERT_NE(cryo, nullptr);
    EXPECT_EQ(cryo->kind, "fluid");
    const Resource* silicon = pkg.findResource("silicon");
    ASSERT_NE(silicon, nullptr);
    EXPECT_EQ(silicon->kind, "item");

    const Recipe* smelterRec = pkg.findRecipe("silicon-smelter");
    ASSERT_NE(smelterRec, nullptr);
    ASSERT_EQ(smelterRec->compatible_machine_ids.size(), 1u);
    EXPECT_EQ(smelterRec->compatible_machine_ids[0], "silicon-smelter");

    NodeEval neDefault = evaluateNode(pkg, "silicon-smelter", "silicon-smelter");
    EXPECT_EQ(neDefault.status, EvalStatus::OK);
    EXPECT_NEAR(neDefault.craft_time_s, 0.6666, 1e-9);

    const std::unordered_map<std::string, ParamValue> boostedParams = {
        {"overdrive", ParamValue::number(250.0)},
        {"efficiency", ParamValue::number(200.0)},
    };
    NodeEval neBoosted = evaluateNode(pkg, "silicon-smelter", "silicon-smelter", boostedParams);
    EXPECT_EQ(neBoosted.status, EvalStatus::OK);
    EXPECT_NEAR(neBoosted.craft_time_s, 0.6666 / (2.5 * 2.0), 1e-9);
}

TEST(GameDataV4ValidationTest, ChildProfileRecompilesInheritedFormulasOnLetOverride) {
    // Finding #1: child profiles inheriting requires, extra_ports, and metrics
    // must recompile inherited formulas against the child's merged `let` env.
    auto makeManifest = [](const std::string& childLetJson) {
        return R"JSON({
          "schema_version": 4,
          "game": { "id": "child_recomp_test" },
          "units": { "recipe_time": "s", "default_rate": "s" },
          "metrics": {
            "power": { "label": "Power", "unit": "W", "aggregate": "sum" },
            "emissions": { "label": "Emissions", "unit": "u/s", "aggregate": "sum" },
            "heat": { "label": "Heat", "unit": "J", "aggregate": "sum" }
          },
          "resources": [
            { "id": "iron", "name": "Iron" },
            { "id": "steam", "name": "Steam" }
          ],
          "profiles": {
            "base_req": {
              "let": { "x": "2" },
              "requires": [{ "expr": "x > 0", "message": "x must be positive" }]
            },
            "child_req": {
              "extends": "base_req",
              "let": { "x": )JSON" + childLetJson + R"JSON( }
            },
            "base_ep_amt": {
              "let": { "x": "2" },
              "extra_ports": [{ "id": "ep_amt", "dir": "out", "resource": "steam", "amount": "x * 2" }]
            },
            "child_ep_amt": {
              "extends": "base_ep_amt",
              "let": { "x": )JSON" + childLetJson + R"JSON( }
            },
            "base_ep_res": {
              "let": { "x": "2" },
              "extra_ports": [{ "id": "ep_res", "dir": "out", "resource_expr": "if(x > 1, 'steam', '')", "amount": "1" }]
            },
            "child_ep_res": {
              "extends": "base_ep_res",
              "let": { "x": )JSON" + childLetJson + R"JSON( }
            },
            "base_ep_en": {
              "let": { "x": "2" },
              "extra_ports": [{ "id": "ep_en", "dir": "out", "resource": "steam", "amount": "1", "enabled": "x > 1" }]
            },
            "child_ep_en": {
              "extends": "base_ep_en",
              "let": { "x": )JSON" + childLetJson + R"JSON( }
            },
            "base_met_pm": {
              "let": { "x": "2" },
              "metrics": { "power": { "per_machine": "x * 10" } }
            },
            "child_met_pm": {
              "extends": "base_met_pm",
              "let": { "x": )JSON" + childLetJson + R"JSON( }
            },
            "base_met_en": {
              "let": { "x": "2" },
              "metrics": { "heat": { "per_machine": "10", "enabled": "x > 0" } }
            },
            "child_met_en": {
              "extends": "base_met_en",
              "let": { "x": )JSON" + childLetJson + R"JSON( }
            },
            "base_met_val": {
              "let": { "x": "2" },
              "metrics": { "emissions": { "value": "x * node.machines" } }
            },
            "child_met_val": {
              "extends": "base_met_val",
              "let": { "x": )JSON" + childLetJson + R"JSON( }
            }
          },
          "machines": [
            { "id": "m_base", "profile": "base_req", "categories": ["cat"] },
            { "id": "m_ep_amt", "profile": "child_ep_amt", "categories": ["cat"] },
            { "id": "m_met_pm", "profile": "child_met_pm", "categories": ["cat"] },
            { "id": "m_met_val", "profile": "child_met_val", "categories": ["cat"] }
          ],
          "recipes": [
            { "id": "r1", "category": "cat", "time": 1, "in": [["iron", 1]], "out": [["iron", 1]] }
          ]
        })JSON";
    };

    // Valid compatible override compiles cleanly and preserves inherited entries.
    {
        GamePackage pkg;
        LoadReport report;
        ASSERT_TRUE(loadGamePackageFromMemory(makeManifest("\"5\""), {}, pkg, report, false));
        EXPECT_TRUE(report.errors.empty());

        NodeEval epEval = evaluateNode(pkg, "m_ep_amt", "r1");
        EXPECT_EQ(epEval.status, EvalStatus::OK);
        const EvaluatedPort* epPort = epEval.findPort("xout:ep_amt");
        ASSERT_NE(epPort, nullptr);
        EXPECT_TRUE(epPort->active);
        EXPECT_DOUBLE_EQ(epPort->amount_per_craft, 10.0);

        NodeEval pmEval = evaluateNode(pkg, "m_met_pm", "r1");
        NodeMetricsResult pmRes = evaluateNodeMetrics(pkg, "m_met_pm", "r1", pmEval, 2.0, 2.0);
        const EvaluatedNodeMetric* pmMet = pmRes.findMetric("power");
        ASSERT_NE(pmMet, nullptr);
        EXPECT_TRUE(pmMet->enabled);
        EXPECT_DOUBLE_EQ(pmMet->value, 100.0);

        NodeEval valEval = evaluateNode(pkg, "m_met_val", "r1");
        NodeMetricsResult valRes = evaluateNodeMetrics(pkg, "m_met_val", "r1", valEval, 3.0, 3.0);
        const EvaluatedNodeMetric* valMet = valRes.findMetric("emissions");
        ASSERT_NE(valMet, nullptr);
        EXPECT_TRUE(valMet->enabled);
        EXPECT_DOUBLE_EQ(valMet->value, 15.0);
    }

    // Incompatible let type override must trigger V-EXPR-4 on every child profile.
    {
        GamePackage pkg;
        LoadReport report;
        EXPECT_FALSE(loadGamePackageFromMemory(makeManifest("\"true\""), {}, pkg, report, false));
        EXPECT_TRUE(report.hasRule("V-EXPR-4"));
        EXPECT_EQ(report.errors.size(), 7u);
        for (const auto& err : report.errors) {
            EXPECT_EQ(err.rule_id, "V-EXPR-4") << err.formatted;
        }

        GamePackage degradedPkg;
        LoadReport degradedReport;
        ASSERT_TRUE(loadGamePackageFromMemory(makeManifest("\"true\""), {}, degradedPkg, degradedReport, true));
        EXPECT_TRUE(degradedReport.hasRule("V-EXPR-4"));
        EXPECT_NE(degradedPkg.findProfile("base_req"), nullptr);
        EXPECT_EQ(degradedPkg.findProfile("child_req"), nullptr);
        EXPECT_EQ(degradedPkg.findProfile("child_ep_amt"), nullptr);
        EXPECT_EQ(degradedPkg.findProfile("child_ep_res"), nullptr);
        EXPECT_EQ(degradedPkg.findProfile("child_ep_en"), nullptr);
        EXPECT_EQ(degradedPkg.findProfile("child_met_pm"), nullptr);
        EXPECT_EQ(degradedPkg.findProfile("child_met_en"), nullptr);
        EXPECT_EQ(degradedPkg.findProfile("child_met_val"), nullptr);
    }
}

TEST(GameDataV4ValidationTest, RejectsPortBaseResourceWithoutStaticRes_V_EXPR_3) {
    // Finding #3: port.base_resource is only legal when applicable recipe ports declare static 'res'.
    auto expectVExpr3Failure = [](const std::string& label, const std::string& manifest) {
        SCOPED_TRACE(label);
        GamePackage pkg;
        LoadReport report;
        EXPECT_FALSE(loadGamePackageFromMemory(manifest, {}, pkg, report, false));
        EXPECT_TRUE(report.hasRule("V-EXPR-3"));
    };

    // 1. Recipe-level overrides (input_amount, output_amount, port_enabled, input_enabled, output_enabled)
    expectVExpr3Failure("overrides.input_amount", R"JSON({
      "schema_version": 4,
      "game": { "id": "t3a" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [{ "id": "iron", "name": "Iron" }],
      "machines": [{ "id": "m", "categories": ["cat"] }],
      "recipes": [{
        "id": "r", "category": "cat", "time": 1,
        "in": [{ "resource_expr": "'iron'", "amount": 1 }],
        "out": [{ "res": "iron", "amount": 1 }],
        "overrides": { "input_amount": "if(port.base_resource.id == 'iron', 1, 2)" }
      }]
    })JSON");

    expectVExpr3Failure("overrides.output_amount", R"JSON({
      "schema_version": 4,
      "game": { "id": "t3b" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [{ "id": "iron", "name": "Iron" }],
      "machines": [{ "id": "m", "categories": ["cat"] }],
      "recipes": [{
        "id": "r", "category": "cat", "time": 1,
        "in": [{ "res": "iron", "amount": 1 }],
        "out": [{ "resource_expr": "'iron'", "amount": 1 }],
        "overrides": { "output_amount": "if(port.base_resource.id == 'iron', 1, 2)" }
      }]
    })JSON");

    expectVExpr3Failure("overrides.port_enabled", R"JSON({
      "schema_version": 4,
      "game": { "id": "t3c" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [{ "id": "iron", "name": "Iron", "tags": ["ore"] }],
      "machines": [{ "id": "m", "categories": ["cat"] }],
      "recipes": [{
        "id": "r", "category": "cat", "time": 1,
        "in": [{ "resource_expr": "'iron'", "amount": 1 }],
        "out": [{ "res": "iron", "amount": 1 }],
        "overrides": { "port_enabled": "has_tag(port.base_resource, 'ore')" }
      }]
    })JSON");

    expectVExpr3Failure("overrides.input_enabled", R"JSON({
      "schema_version": 4,
      "game": { "id": "t3d" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [{ "id": "iron", "name": "Iron", "tags": ["ore"] }],
      "machines": [{ "id": "m", "categories": ["cat"] }],
      "recipes": [{
        "id": "r", "category": "cat", "time": 1,
        "in": [{ "resource_expr": "'iron'", "amount": 1 }],
        "overrides": { "input_enabled": "has_tag(port.base_resource, 'ore')" }
      }]
    })JSON");

    expectVExpr3Failure("overrides.output_enabled", R"JSON({
      "schema_version": 4,
      "game": { "id": "t3e" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [{ "id": "iron", "name": "Iron", "tags": ["ore"] }],
      "machines": [{ "id": "m", "categories": ["cat"] }],
      "recipes": [{
        "id": "r", "category": "cat", "time": 1,
        "out": [{ "resource_expr": "'iron'", "amount": 1 }],
        "overrides": { "output_enabled": "has_tag(port.base_resource, 'ore')" }
      }]
    })JSON");

    // 2. Profile-level port formulas compiled with a recipe whose port omits static 'res'
    expectVExpr3Failure("profile.input_amount", R"JSON({
      "schema_version": 4,
      "game": { "id": "t3f" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [{ "id": "iron", "name": "Iron" }],
      "profiles": {
        "p": { "input_amount": "if(port.base_resource.id == 'iron', port.amount, 2)" }
      },
      "machines": [{ "id": "m", "profile": "p", "categories": ["cat"] }],
      "recipes": [{
        "id": "r", "category": "cat", "time": 1,
        "in": [{ "resource_expr": "'iron'", "amount": 1 }]
      }]
    })JSON");

    expectVExpr3Failure("profile.port_enabled", R"JSON({
      "schema_version": 4,
      "game": { "id": "t3g" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [{ "id": "iron", "name": "Iron", "tags": ["ore"] }],
      "profiles": {
        "p": { "port_enabled": "has_tag(port.base_resource, 'ore')" }
      },
      "machines": [{ "id": "m", "profile": "p", "categories": ["cat"] }],
      "recipes": [{
        "id": "r", "category": "cat", "time": 1,
        "out": [{ "resource_expr": "'iron'", "amount": 1 }]
      }]
    })JSON");

    // 3. Port-bound resource_choice visible_if / allowed_if using port.base_resource without static 'res'
    expectVExpr3Failure("resource_param.visible_if", R"JSON({
      "schema_version": 4,
      "game": { "id": "t3h" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [{ "id": "iron", "name": "Iron" }],
      "parameters": {
        "rp": { "type": "resource_choice", "resources": ["iron"], "default": "iron",
                "visible_if": "port.base_resource.id == 'iron'" }
      },
      "profiles": { "p": { "params": ["rp"] } },
      "machines": [{ "id": "m", "profile": "p", "categories": ["cat"] }],
      "recipes": [{
        "id": "r", "category": "cat", "time": 1,
        "in": [{ "resource_param": "rp", "amount": 1 }]
      }]
    })JSON");

    expectVExpr3Failure("resource_param.allowed_if", R"JSON({
      "schema_version": 4,
      "game": { "id": "t3i" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [{ "id": "iron", "name": "Iron", "tags": ["ore"] }],
      "parameters": {
        "rp": { "type": "resource_choice", "resources": ["iron"], "default": "iron",
                "allowed_if": "has_tag(port.base_resource, 'ore')" }
      },
      "profiles": { "p": { "params": ["rp"] } },
      "machines": [{ "id": "m", "profile": "p", "categories": ["cat"] }],
      "recipes": [{
        "id": "r", "category": "cat", "time": 1,
        "out": [{ "resource_param": "rp", "amount": 1 }]
      }]
    })JSON");

    // 4. Positive controls: profile/overrides using port.base_resource SUCCEED when applicable ports
    // declare static 'res' (or when missing-'res' ports override the formula cleanly).
    {
        const std::string validManifest = R"JSON({
          "schema_version": 4,
          "game": { "id": "t3_ok" },
          "units": { "recipe_time": "s", "default_rate": "s" },
          "resources": [{ "id": "iron", "name": "Iron", "tags": ["ore"] }],
          "profiles": {
            "p": { "input_amount": "if(port.base_resource.id == 'iron', port.amount * 2, port.amount)" }
          },
          "machines": [{ "id": "m", "profile": "p", "categories": ["cat"] }],
          "recipes": [
            {
              "id": "r_static", "category": "cat", "time": 1,
              "in": [{ "res": "iron", "amount": 1 }],
              "out": [{ "res": "iron", "amount": 1 }]
            },
            {
              "id": "r_gen_empty_in", "category": "cat", "time": 1,
              "out": [{ "res": "iron", "amount": 1 }]
            },
            {
              "id": "r_port_override", "category": "cat", "time": 1,
              "in": [{ "resource_expr": "'iron'", "amount": 1, "amount_expr": "3" }],
              "out": [{ "res": "iron", "amount": 1 }]
            }
          ]
        })JSON";
        GamePackage pkg;
        LoadReport report;
        ASSERT_TRUE(loadGamePackageFromMemory(validManifest, {}, pkg, report, false))
            << (report.errors.empty() ? "" : report.errors.front().formatted);
        EXPECT_TRUE(report.errors.empty());
    }
}

TEST(GameDataV4ValidationTest, RejectsPortBoundResourceParamUsedOutsideRecipePort_V_PROF_4) {
    // Finding #5: a resource_choice parameter referenced by any recipe port's resource_param
    // is port-bound and must be rejected with V-PROF-4 if also used outside recipe resource_param.
    auto expectVProf4 = [](const std::string& label, const std::string& manifest) {
        SCOPED_TRACE(label);
        GamePackage pkg;
        LoadReport report;
        EXPECT_FALSE(loadGamePackageFromMemory(manifest, {}, pkg, report, false));
        EXPECT_TRUE(report.hasRule("V-PROF-4"));
    };

    // 1. Audit #5 scenario: used in extra_ports.resource_param on "boiler" + recipe port on "alt_recipe"
    const std::string extraPortConflict = R"JSON({
      "schema_version": 4,
      "game": { "id": "t5_extra" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [
        { "id": "coal", "name": "Coal" },
        { "id": "wood", "name": "Wood" },
        { "id": "water", "name": "Water" },
        { "id": "steam", "name": "Steam" }
      ],
      "parameters": {
        "fuel": { "type": "resource_choice", "resources": ["coal", "wood"], "default": "coal" }
      },
      "profiles": {
        "boiler": {
          "params": ["fuel"],
          "extra_ports": [{ "id": "fuel_in", "dir": "in", "resource_param": "fuel", "amount": "1" }]
        },
        "furnace": { "params": ["fuel"] }
      },
      "machines": [
        { "id": "m_boiler", "profile": "boiler", "categories": ["boil"] },
        { "id": "m_furnace", "profile": "furnace", "categories": ["smelt"] }
      ],
      "recipes": [
        { "id": "boil_water", "category": "boil", "time": 1,
          "in": [["water", 1]], "out": [["steam", 1]] },
        { "id": "alt_recipe", "category": "smelt", "time": 1,
          "in": [{ "resource_param": "fuel", "amount": 1 }], "out": [["coal", 1]] }
      ]
    })JSON";
    expectVProf4("extra_ports.resource_param", extraPortConflict);

    // In degraded mode, boil_water survives with fuel NOT marked port-bound so stored "wood" works!
    {
        GamePackage pkg;
        LoadReport report;
        ASSERT_TRUE(loadGamePackageFromMemory(extraPortConflict, {}, pkg, report, true));
        EXPECT_TRUE(report.hasRule("V-PROF-4"));
        EXPECT_NE(pkg.findRecipe("boil_water"), nullptr);
        EXPECT_EQ(pkg.findRecipe("alt_recipe"), nullptr);
        NodeEval ne = evaluateNode(pkg, "m_boiler", "boil_water", {{"fuel", ParamValue::string("wood")}});
        EXPECT_EQ(ne.status, EvalStatus::OK);
        EXPECT_TRUE(ne.param_visible.at("fuel"));
        const EvaluatedPort* ep = ne.findPort("xin:fuel_in");
        ASSERT_NE(ep, nullptr);
        EXPECT_EQ(ep->resource, "wood");
    }

    // 2. Used in profile let / metrics
    expectVProf4("profile.let_and_metrics", R"JSON({
      "schema_version": 4,
      "game": { "id": "t5_let" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "attributes": { "resource": { "energy": { "type": "number", "default": 1 } } },
      "metrics": { "power": { "label": "Power", "aggregate": "sum" } },
      "resources": [
        { "id": "coal", "name": "Coal", "attrs": { "energy": 4 } },
        { "id": "iron", "name": "Iron" }
      ],
      "parameters": {
        "fuel": { "type": "resource_choice", "resources": ["coal"], "default": "coal" }
      },
      "profiles": {
        "p_let": {
          "params": ["fuel"],
          "let": { "fv": "param.fuel.energy" },
          "metrics": { "power": { "per_machine": "fv * 10" } }
        }
      },
      "machines": [{ "id": "m", "profile": "p_let", "categories": ["cat"] }],
      "recipes": [
        { "id": "r_port", "category": "cat", "time": 1,
          "in": [{ "resource_param": "fuel", "amount": 1 }], "out": [["iron", 1]] }
      ]
    })JSON");

    // 3. Same recipe uses resource_param AND reads param in overrides
    expectVProf4("same_recipe_override", R"JSON({
      "schema_version": 4,
      "game": { "id": "t5_same_rec" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [{ "id": "coal", "name": "Coal" }],
      "parameters": {
        "fuel": { "type": "resource_choice", "resources": ["coal"], "default": "coal" }
      },
      "profiles": { "p": { "params": ["fuel"] } },
      "machines": [{ "id": "m", "profile": "p", "categories": ["cat"] }],
      "recipes": [{
        "id": "r", "category": "cat", "time": 1,
        "in": [{ "resource_param": "fuel", "amount": 1 }],
        "overrides": { "craft_time": "if(is_set(param.fuel), 1, 2)" }
      }]
    })JSON");

    // 4. Cross-recipe order-independence (override before port, and port before override)
    expectVProf4("override_before_port", R"JSON({
      "schema_version": 4,
      "game": { "id": "t5_order1" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [{ "id": "coal", "name": "Coal" }],
      "parameters": {
        "fuel": { "type": "resource_choice", "resources": ["coal"], "default": "coal" }
      },
      "profiles": { "p": { "params": ["fuel"] } },
      "machines": [{ "id": "m", "profile": "p", "categories": ["cat"] }],
      "recipes": [
        {
          "id": "r_ovr", "category": "cat", "time": 1,
          "in": [["coal", 1]],
          "overrides": { "craft_time": "if(is_set(param.fuel), 1, 2)" }
        },
        {
          "id": "r_port", "category": "cat", "time": 1,
          "in": [{ "resource_param": "fuel", "amount": 1 }]
        }
      ]
    })JSON");

    // 5. Static expression of another parameter referencing param.fuel
    expectVProf4("other_param_static", R"JSON({
      "schema_version": 4,
      "game": { "id": "t5_param" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [{ "id": "coal", "name": "Coal" }],
      "parameters": {
        "fuel": { "type": "resource_choice", "resources": ["coal"], "default": "coal" },
        "extra": { "type": "bool", "default": false, "visible_if": "is_set(param.fuel)" }
      },
      "profiles": { "p": { "params": ["fuel", "extra"] } },
      "machines": [{ "id": "m", "profile": "p", "categories": ["cat"] }],
      "recipes": [
        { "id": "r_port", "category": "cat", "time": 1,
          "in": [{ "resource_param": "fuel", "amount": 1 }] }
      ]
    })JSON");
}

TEST(GameDataV4EvaluationTest, PopulatesDownstreamParamUiWhenEarlierParamIsBlockedOrErrored) {
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "downstream_ui_test", "name": "Downstream UI Test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "attributes": {
        "machine": {
          "base_min": { "type": "number", "default": 2 },
          "base_max": { "type": "number", "default": 8 },
          "slot_cap": { "type": "number", "default": 3 },
          "frac_min": { "type": "number", "default": 0.4 },
          "frac_max": { "type": "number", "default": 0.6 }
        }
      },
      "resources": [
        { "id": "iron", "name": "Iron" },
        { "id": "copper", "name": "Copper" }
      ],
      "option_sets": {
        "opts": {
          "options": [
            { "key": "a", "label": "Opt A" },
            { "key": "b", "label": "Opt B" }
          ]
        }
      },
      "parameters": {
        "p_block": {
          "type": "resource_choice",
          "resources": ["iron", "copper"],
          "allow_none": false,
          "allowed_if": "false"
        },
        "p_hidden_lit": {
          "type": "bool",
          "default": false,
          "visible_if": "false"
        },
        "p_hidden_expr": {
          "type": "bool",
          "default": true,
          "visible_if": "recipe.id == 'other_recipe'"
        },
        "p_enum": {
          "type": "enum",
          "option_set": "opts",
          "allowed_if": "option != 'b'",
          "default": "a"
        },
        "p_res": {
          "type": "resource_choice",
          "resources": ["iron", "copper"],
          "allowed_if": "option == 'copper'",
          "default": "copper"
        },
        "p_bounds": {
          "type": "int",
          "min": "machine.base_min",
          "max": "machine.base_max",
          "default": 5
        },
        "p_slots": {
          "type": "slots",
          "option_set": "opts",
          "count": "machine.slot_cap",
          "allowed_if": "option != 'b'",
          "allow_empty": true
        },
        "p_dep_vis": {
          "type": "number",
          "default": 1,
          "visible_if": "is_set(param.p_block)"
        },
        "p_dep_bounds": {
          "type": "number",
          "min": "param.p_bounds + 1",
          "max": 100,
          "default": 10
        },
        "p_dep_blocked_min": {
          "type": "number",
          "min": "if(is_set(param.p_block), 2, 1)",
          "max": 10,
          "default": 5
        },
        "p_err_int_inv": {
          "type": "int",
          "min": "machine.frac_min",
          "max": "machine.frac_max",
          "default": 0
        }
      },
      "profiles": {
        "p_blocked": {
          "params": [
            "p_block",
            "p_hidden_lit",
            "p_hidden_expr",
            "p_enum",
            "p_res",
            "p_bounds",
            "p_slots",
            "p_dep_vis",
            "p_dep_bounds",
            "p_dep_blocked_min"
          ]
        },
        "p_errored": {
          "params": [
            "p_err_int_inv",
            "p_hidden_lit",
            "p_enum"
          ]
        }
      },
      "machines": [
        { "id": "m_block", "profile": "p_blocked", "categories": ["cat"] },
        { "id": "m_err", "profile": "p_errored", "categories": ["cat"] }
      ],
      "recipes": [
        {
          "id": "r1",
          "category": "cat",
          "time": 1,
          "in": [{ "res": "iron", "amount": 1 }],
          "out": [{ "res": "copper", "amount": 1 }]
        }
      ]
    })JSON";

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);

    // 1. Upstream BLOCKED parameter must NOT leave downstream param_visible / param_ui empty
    //    and must NOT escalate BLOCKED into ERROR for downstream params referencing p_block.
    NodeEval blockedEval = evaluateNode(pkg, "m_block", "r1", {});
    EXPECT_EQ(blockedEval.status, EvalStatus::BLOCKED);

    ASSERT_TRUE(blockedEval.param_visible.contains("p_hidden_lit"));
    ASSERT_TRUE(blockedEval.param_ui.contains("p_hidden_lit"));
    EXPECT_FALSE(blockedEval.param_visible.at("p_hidden_lit"));
    EXPECT_FALSE(blockedEval.param_ui.at("p_hidden_lit").visible);

    ASSERT_TRUE(blockedEval.param_visible.contains("p_hidden_expr"));
    ASSERT_TRUE(blockedEval.param_ui.contains("p_hidden_expr"));
    EXPECT_FALSE(blockedEval.param_visible.at("p_hidden_expr"));
    EXPECT_FALSE(blockedEval.param_ui.at("p_hidden_expr").visible);

    ASSERT_TRUE(blockedEval.param_ui.contains("p_enum"));
    EXPECT_TRUE(blockedEval.param_ui.at("p_enum").visible);
    EXPECT_EQ(blockedEval.param_ui.at("p_enum").allowed_options, (std::vector<std::string>{"a"}));

    ASSERT_TRUE(blockedEval.param_ui.contains("p_res"));
    EXPECT_TRUE(blockedEval.param_ui.at("p_res").visible);
    EXPECT_EQ(blockedEval.param_ui.at("p_res").allowed_resources, (std::vector<std::string>{"copper"}));

    ASSERT_TRUE(blockedEval.param_ui.contains("p_bounds"));
    EXPECT_TRUE(blockedEval.param_ui.at("p_bounds").has_min);
    EXPECT_TRUE(blockedEval.param_ui.at("p_bounds").has_max);
    EXPECT_DOUBLE_EQ(blockedEval.param_ui.at("p_bounds").min_val, 2.0);
    EXPECT_DOUBLE_EQ(blockedEval.param_ui.at("p_bounds").max_val, 8.0);

    ASSERT_TRUE(blockedEval.param_ui.contains("p_slots"));
    EXPECT_EQ(blockedEval.param_ui.at("p_slots").slot_count, 3u);
    EXPECT_EQ(blockedEval.param_ui.at("p_slots").allowed_options, (std::vector<std::string>{"a"}));

    ASSERT_TRUE(blockedEval.param_visible.contains("p_dep_vis"));
    ASSERT_TRUE(blockedEval.param_ui.contains("p_dep_vis"));
    EXPECT_FALSE(blockedEval.param_visible.at("p_dep_vis"));
    EXPECT_FALSE(blockedEval.param_ui.at("p_dep_vis").visible);

    ASSERT_TRUE(blockedEval.param_ui.contains("p_dep_bounds"));
    EXPECT_TRUE(blockedEval.param_ui.at("p_dep_bounds").has_min);
    EXPECT_DOUBLE_EQ(blockedEval.param_ui.at("p_dep_bounds").min_val, 6.0);
    EXPECT_TRUE(blockedEval.param_ui.at("p_dep_bounds").has_max);
    EXPECT_DOUBLE_EQ(blockedEval.param_ui.at("p_dep_bounds").max_val, 100.0);

    ASSERT_TRUE(blockedEval.param_ui.contains("p_dep_blocked_min"));
    EXPECT_TRUE(blockedEval.param_ui.at("p_dep_blocked_min").visible);
    EXPECT_TRUE(blockedEval.param_ui.at("p_dep_blocked_min").has_max);
    EXPECT_DOUBLE_EQ(blockedEval.param_ui.at("p_dep_blocked_min").max_val, 10.0);

    // 2. Upstream ERROR parameter (fractional int bounds 0.4..0.6 -> 1..0) must also populate
    //    downstream param_visible & param_ui, and must not expose inverted [1, 0] bounds.
    NodeEval errEval = evaluateNode(pkg, "m_err", "r1", {});
    EXPECT_EQ(errEval.status, EvalStatus::ERROR);
    ASSERT_TRUE(errEval.param_ui.contains("p_err_int_inv"));
    EXPECT_FALSE(errEval.param_ui.at("p_err_int_inv").has_min);
    EXPECT_FALSE(errEval.param_ui.at("p_err_int_inv").has_max);

    ASSERT_TRUE(errEval.param_visible.contains("p_hidden_lit"));
    ASSERT_TRUE(errEval.param_ui.contains("p_hidden_lit"));
    EXPECT_FALSE(errEval.param_visible.at("p_hidden_lit"));
    EXPECT_FALSE(errEval.param_ui.at("p_hidden_lit").visible);

    ASSERT_TRUE(errEval.param_ui.contains("p_enum"));
    EXPECT_EQ(errEval.param_ui.at("p_enum").allowed_options, (std::vector<std::string>{"a"}));
}

TEST(GameDataV4EvaluationTest, ResolveProjectGlobalsPopulatesEvaluatedParamUi) {
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "globals_ui_meta_test", "name": "Globals UI Meta Test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [
        { "id": "coal", "name": "Coal", "tags": ["combustible"] },
        { "id": "fuel", "name": "Fuel", "tags": ["combustible"] },
        { "id": "water", "name": "Water" }
      ],
      "option_sets": {
        "tiers": {
          "attrs": { "t": { "type": "number", "default": 1 } },
          "options": [
            { "key": "t1", "label": "Tier 1", "attrs": { "t": 1 } },
            { "key": "t2", "label": "Tier 2", "attrs": { "t": 2 } },
            { "key": "t3", "label": "Tier 3", "attrs": { "t": 3 } }
          ]
        }
      },
      "globals": {
        "max_tier": { "label": "Max Tier", "type": "int", "min": 1, "max": 3, "default": 2 },
        "tier_enum": {
          "label": "Tier", "type": "enum", "option_set": "tiers", "default": "t1",
          "allowed_if": "option.t <= global.max_tier"
        },
        "fuel_res": {
          "label": "Fuel", "type": "resource_choice", "resources": ["coal", "fuel", "water"], "default": "coal",
          "allowed_if": "has_tag(option, 'combustible')"
        },
        "mod_slots": {
          "label": "Modules", "type": "slots", "option_set": "tiers",
          "count": "global.max_tier", "allowed_if": "option.t <= global.max_tier"
        },
        "hidden_g": {
          "label": "Hidden", "type": "number", "default": 1,
          "visible_if": "global.max_tier > 2"
        },
        "bounded_g": {
          "label": "Bounded", "type": "number", "default": 1.5,
          "min": "0.5", "max": "global.max_tier * 2"
        }
      }
    })JSON";

    GamePackage pkg;
    LoadReport report;
    ASSERT_TRUE(loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);

    ResolvedGlobals defGlobals = resolveProjectGlobals(pkg, {});
    ASSERT_TRUE(defGlobals.param_ui.contains("hidden_g"));
    EXPECT_FALSE(defGlobals.visible.at("hidden_g"));
    EXPECT_FALSE(defGlobals.param_ui.at("hidden_g").visible);

    ASSERT_TRUE(defGlobals.param_ui.contains("bounded_g"));
    EXPECT_TRUE(defGlobals.param_ui.at("bounded_g").visible);
    EXPECT_TRUE(defGlobals.param_ui.at("bounded_g").has_min);
    EXPECT_TRUE(defGlobals.param_ui.at("bounded_g").has_max);
    EXPECT_DOUBLE_EQ(defGlobals.param_ui.at("bounded_g").min_val, 0.5);
    EXPECT_DOUBLE_EQ(defGlobals.param_ui.at("bounded_g").max_val, 4.0);

    ASSERT_TRUE(defGlobals.param_ui.contains("tier_enum"));
    EXPECT_EQ(defGlobals.param_ui.at("tier_enum").allowed_options,
              (std::vector<std::string>{"t1", "t2"}));

    ASSERT_TRUE(defGlobals.param_ui.contains("fuel_res"));
    EXPECT_EQ(defGlobals.param_ui.at("fuel_res").allowed_resources,
              (std::vector<std::string>{"coal", "fuel"}));

    ASSERT_TRUE(defGlobals.param_ui.contains("mod_slots"));
    EXPECT_EQ(defGlobals.param_ui.at("mod_slots").slot_count, 2u);
    EXPECT_EQ(defGlobals.param_ui.at("mod_slots").allowed_options,
              (std::vector<std::string>{"t1", "t2"}));

    ResolvedGlobals tier3Globals = resolveProjectGlobals(pkg, {{"max_tier", int64_t(3)}});
    EXPECT_TRUE(tier3Globals.param_ui.at("hidden_g").visible);
    EXPECT_DOUBLE_EQ(tier3Globals.param_ui.at("bounded_g").max_val, 6.0);
    EXPECT_EQ(tier3Globals.param_ui.at("tier_enum").allowed_options,
              (std::vector<std::string>{"t1", "t2", "t3"}));
    EXPECT_EQ(tier3Globals.param_ui.at("mod_slots").slot_count, 3u);
    EXPECT_EQ(tier3Globals.param_ui.at("mod_slots").allowed_options,
              (std::vector<std::string>{"t1", "t2", "t3"}));
}

} // namespace
} // namespace gamedata
