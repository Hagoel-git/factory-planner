#include <gtest/gtest.h>

#include "common/StringUtils.h"
#include "core/expr/ExprCompiler.h"
#include "core/expr/ExprDiagnostic.h"
#include "core/expr/ExprEnv.h"
#include "core/expr/ExprEvaluator.h"
#include "core/expr/ExprValue.h"

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using namespace expr;

CompileEnv makeStandardCompileEnv() {
    CompileEnv env;
    env.context = ExprContext::PreSolveLet;

    env.game_attrs["crop_tick_s"] = AttrType::Number;
    env.game_attrs["beacon_efficiency"] = AttrType::Number;
    env.game_attrs["edition"] = AttrType::String;

    env.machine_attrs["power"] = AttrType::Number;
    env.machine_attrs["somersloop_slots"] = AttrType::Number;
    env.machine_attrs["min_tier"] = AttrType::Number;
    env.machine_attrs["max_tier"] = AttrType::Number;
    env.machine_attrs["module_slots"] = AttrType::Number;
    env.machine_attrs["base_prod"] = AttrType::Number;
    env.machine_attrs["water_per_s"] = AttrType::Number;
    env.machine_attrs["fertilizer_per_s"] = AttrType::Number;

    env.recipe_attrs["eut"] = AttrType::Number;
    env.recipe_attrs["allows_productivity"] = AttrType::Bool;
    env.recipe_attrs["allows_extra"] = AttrType::Bool;
    env.recipe_attrs["crop_tier"] = AttrType::Number;
    env.recipe_attrs["growth_points"] = AttrType::Number;

    env.port_attrs["min_tier"] = AttrType::Number;
    env.port_attrs["chance_boost"] = AttrType::Number;
    env.port_attrs["slot_tag"] = AttrType::String;

    env.resource_attrs["fuel_value"] = AttrType::Number;
    env.resource_attrs["burnt_result"] = AttrType::String;
    env.resource_attrs["extra"] = AttrType::Number;
    env.resource_attrs["speedup"] = AttrType::Number;
    env.resource_attrs["power_mult"] = AttrType::Number;
    env.resource_attrs["charges"] = AttrType::Number;

    OptionSetCompileInfo gt_tiers;
    gt_tiers.option_keys = {"ulv", "lv", "mv", "hv", "ev", "iv", "luv", "zpm", "uv"};
    gt_tiers.attrs["t"] = AttrType::Number;
    gt_tiers.attrs["v"] = AttrType::Number;
    env.option_sets["gt_tiers"] = gt_tiers;

    OptionSetCompileInfo prolif_modes;
    prolif_modes.option_keys = {"extra", "speed"};
    env.option_sets["prolif_modes"] = prolif_modes;

    OptionSetCompileInfo modules;
    modules.option_keys = {"speed-module-3", "productivity-module-3", "efficiency-module-3"};
    modules.attrs["speed"] = AttrType::Number;
    modules.attrs["prod"] = AttrType::Number;
    modules.attrs["consumption"] = AttrType::Number;
    modules.attrs["tag_name"] = AttrType::String;
    env.option_sets["modules"] = modules;

    env.tables["gt_voltage"] = TableKind::Array;
    env.tables["crop_tier_cap"] = TableKind::Array;
    env.tables["crop_manager_eut"] = TableKind::Array;
    env.tables["coil_heat"] = TableKind::Object;

    env.resources = {
        "iron-ore", "iron-ingot", "iron-plate", "copper-ingot", "copper-cable",
        "electronic-circuit", "circuit-board", "water", "fertilizer", "coal", "ash",
        "crushed_copper", "impure_dust_copper", "dust_gold", "sugar_cane", "sticky_resin",
        "proliferator-mk1", "proliferator-mk2", "proliferator-mk3"
    };

    env.parameters["clock"] = {ExprType::number(), false};
    env.parameters["somersloops"] = {ExprType::number(), false};
    env.parameters["voltage_tier"] = {ExprType::option("gt_tiers"), false};
    env.parameters["prolif"] = {ExprType::resource(), true};
    env.parameters["prolif_mode"] = {ExprType::option("prolif_modes"), false};
    env.parameters["modules"] = {ExprType::slots("modules"), false};
    env.parameters["beacon_count"] = {ExprType::number(), false};
    env.parameters["beacon_modules"] = {ExprType::slots("modules"), false};
    env.parameters["hydrated"] = {ExprType::boolean(), false};
    env.parameters["fertilized"] = {ExprType::boolean(), false};
    env.parameters["growth"] = {ExprType::number(), false};
    env.parameters["gain"] = {ExprType::number(), false};

    env.globals["mining_prod_level"] = {ExprType::number(), false};
    env.globals["global_fuel"] = {ExprType::resource(), true};
    env.globals["global_tier"] = {ExprType::option("gt_tiers"), true};
    env.globals["global_modules"] = {ExprType::slots("modules"), false};

    env.profile_id = "test_profile";
    env.profile_params = {
        "clock", "somersloops", "voltage_tier", "prolif", "prolif_mode",
        "modules", "beacon_count", "beacon_modules", "hydrated", "fertilized",
        "growth", "gain"
    };

    return env;
}

EvalEnv makeStandardEvalEnv() {
    EvalEnv env;
    env.game_ticks_per_second = 20.0;
    env.game_attrs["crop_tick_s"] = EvalValue::number(12.8);
    env.game_attrs["beacon_efficiency"] = EvalValue::number(1.5);
    env.game_attrs["edition"] = EvalValue::string("space-age");

    env.machine_attr_defaults["power"] = EvalValue::number(0.0);
    env.machine_attr_defaults["somersloop_slots"] = EvalValue::number(0.0);
    env.machine_attr_defaults["min_tier"] = EvalValue::number(0.0);
    env.machine_attr_defaults["max_tier"] = EvalValue::number(14.0);
    env.machine_attr_defaults["module_slots"] = EvalValue::number(0.0);
    env.machine_attr_defaults["base_prod"] = EvalValue::number(0.0);
    env.machine_attr_defaults["water_per_s"] = EvalValue::number(0.0);
    env.machine_attr_defaults["fertilizer_per_s"] = EvalValue::number(0.0);

    env.recipe_attr_defaults["eut"] = EvalValue::number(0.0);
    env.recipe_attr_defaults["allows_productivity"] = EvalValue::boolean(false);
    env.recipe_attr_defaults["allows_extra"] = EvalValue::boolean(false);
    env.recipe_attr_defaults["crop_tier"] = EvalValue::number(1.0);
    env.recipe_attr_defaults["growth_points"] = EvalValue::number(0.0);

    env.port_attr_defaults["min_tier"] = EvalValue::number(0.0);
    env.port_attr_defaults["chance_boost"] = EvalValue::number(0.0);
    env.port_attr_defaults["slot_tag"] = EvalValue::string("");

    env.resource_attr_defaults["fuel_value"] = EvalValue::number(0.0);
    env.resource_attr_defaults["burnt_result"] = EvalValue::string("");
    env.resource_attr_defaults["extra"] = EvalValue::number(0.0);
    env.resource_attr_defaults["speedup"] = EvalValue::number(0.0);
    env.resource_attr_defaults["power_mult"] = EvalValue::number(1.0);
    env.resource_attr_defaults["charges"] = EvalValue::number(1.0);

    // Option sets
    OptionSetEvalData gt_tiers;
    gt_tiers.attr_defaults["t"] = EvalValue::number(0.0);
    gt_tiers.attr_defaults["v"] = EvalValue::number(0.0);
    const std::vector<std::pair<std::string, double>> tiers = {
        {"ulv", 8}, {"lv", 32}, {"mv", 128}, {"hv", 512}, {"ev", 2048},
        {"iv", 8192}, {"luv", 32768}, {"zpm", 131072}, {"uv", 524288}
    };
    for (size_t i = 0; i < tiers.size(); ++i) {
        gt_tiers.option_attrs[tiers[i].first]["t"] = EvalValue::number(static_cast<double>(i));
        gt_tiers.option_attrs[tiers[i].first]["v"] = EvalValue::number(tiers[i].second);
    }
    env.option_sets["gt_tiers"] = gt_tiers;

    OptionSetEvalData modules;
    modules.attr_defaults["speed"] = EvalValue::number(0.0);
    modules.attr_defaults["prod"] = EvalValue::number(0.0);
    modules.attr_defaults["consumption"] = EvalValue::number(0.0);
    modules.option_attrs["speed-module-3"]["speed"] = EvalValue::number(0.5);
    modules.option_attrs["speed-module-3"]["consumption"] = EvalValue::number(0.7);
    modules.option_attrs["productivity-module-3"]["speed"] = EvalValue::number(-0.15);
    modules.option_attrs["productivity-module-3"]["prod"] = EvalValue::number(0.1);
    modules.option_attrs["productivity-module-3"]["consumption"] = EvalValue::number(0.8);
    modules.option_attrs["efficiency-module-3"]["consumption"] = EvalValue::number(-0.5);
    env.option_sets["modules"] = modules;

    env.option_sets["prolif_modes"] = OptionSetEvalData{};

    // Resources
    env.resources["iron-ore"].tags = {"ore"};
    env.resources["coal"].tags = {"fuel", "chemical"};
    env.resources["coal"].attrs["fuel_value"] = EvalValue::number(4.0);
    env.resources["coal"].attrs["burnt_result"] = EvalValue::string("ash");

    env.resources["proliferator-mk1"].tags = {"proliferator"};
    env.resources["proliferator-mk1"].attrs["extra"] = EvalValue::number(0.125);
    env.resources["proliferator-mk1"].attrs["speedup"] = EvalValue::number(0.25);
    env.resources["proliferator-mk1"].attrs["power_mult"] = EvalValue::number(1.3);
    env.resources["proliferator-mk1"].attrs["charges"] = EvalValue::number(12.0);

    env.resources["proliferator-mk2"].tags = {"proliferator"};
    env.resources["proliferator-mk2"].attrs["extra"] = EvalValue::number(0.2);
    env.resources["proliferator-mk2"].attrs["speedup"] = EvalValue::number(0.5);
    env.resources["proliferator-mk2"].attrs["power_mult"] = EvalValue::number(1.7);
    env.resources["proliferator-mk2"].attrs["charges"] = EvalValue::number(24.0);

    env.resources["proliferator-mk3"].tags = {"proliferator"};
    env.resources["proliferator-mk3"].attrs["extra"] = EvalValue::number(0.25);
    env.resources["proliferator-mk3"].attrs["speedup"] = EvalValue::number(1.0);
    env.resources["proliferator-mk3"].attrs["power_mult"] = EvalValue::number(2.5);
    env.resources["proliferator-mk3"].attrs["charges"] = EvalValue::number(60.0);

    // Tables
    env.array_tables["gt_voltage"] = {8, 32, 128, 512, 2048, 8192, 32768, 131072, 524288};
    env.array_tables["crop_tier_cap"] = {0, 4, 7, 10, 13, 16};
    env.array_tables["crop_manager_eut"] = {0, 32, 128, 512, 2048, 8192};
    env.object_tables["coil_heat"] = {
        {"cupronickel", 1801.0}, {"kanthal", 2701.0}, {"nichrome", 3601.0}
    };

    // Default bindings
    env.machine.id = "constructor";
    env.machine.speed = 1.0;
    env.machine.attrs["power"] = EvalValue::number(4.0);
    env.machine.attrs["somersloop_slots"] = EvalValue::number(1.0);

    env.recipe.id = "iron-plate";
    env.recipe.time = 6.0;
    env.recipe.time_raw = 6.0;
    env.recipe.input_items_total = 3.0;
    env.recipe.output_items_total = 2.0;

    env.params["clock"] = EvalValue::number(100.0);
    env.params["somersloops"] = EvalValue::number(0.0);
    env.params["voltage_tier"] = EvalValue::option("gt_tiers", "lv");
    env.params["prolif"] = EvalValue::resource(std::nullopt);
    env.params["prolif_mode"] = EvalValue::option("prolif_modes", "speed");
    env.params["modules"] = EvalValue::slots("modules", {});
    env.params["beacon_count"] = EvalValue::number(0.0);
    env.params["beacon_modules"] = EvalValue::slots("modules", {});
    env.params["hydrated"] = EvalValue::boolean(false);
    env.params["fertilized"] = EvalValue::boolean(false);
    env.params["growth"] = EvalValue::number(1.0);
    env.params["gain"] = EvalValue::number(1.0);

    env.globals["mining_prod_level"] = EvalValue::number(0.0);
    env.globals["global_fuel"] = EvalValue::resource("coal");
    env.globals["global_tier"] = EvalValue::option("gt_tiers", std::nullopt);
    env.globals["global_modules"] = EvalValue::slots("modules", {"speed-module-3", std::nullopt});

    return env;
}

double evalNum(std::string_view expr_str, const CompileEnv& cenv, const EvalEnv& eenv) {
    CompileResult cr = compile(expr_str, cenv, ExpectedType::Number);
    EXPECT_TRUE(cr.ok) << "Compile failed for '" << expr_str << "': " << cr.error.message;
    if (!cr.ok) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    EvalResult er = evaluate(cr.expr, eenv);
    EXPECT_TRUE(er.ok) << "Eval failed for '" << expr_str << "': " << er.error;
    if (!er.ok) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return er.value.asNumber();
}

bool evalBool(std::string_view expr_str, const CompileEnv& cenv, const EvalEnv& eenv) {
    CompileResult cr = compile(expr_str, cenv, ExpectedType::Bool);
    EXPECT_TRUE(cr.ok) << "Compile failed for '" << expr_str << "': " << cr.error.message;
    if (!cr.ok) {
        return false;
    }
    EvalResult er = evaluate(cr.expr, eenv);
    EXPECT_TRUE(er.ok) << "Eval failed for '" << expr_str << "': " << er.error;
    if (!er.ok) {
        return false;
    }
    return er.value.asBool();
}

} // namespace

// ============================================================================
// 1. StringUtils::slugify() & ID validation tests
// ============================================================================

TEST(SlugifyTest, ReplacesSlashColonAndDotWithHyphen) {
    EXPECT_EQ(slugify("a/b"), "a-b");
    EXPECT_EQ(slugify("gregtech:gt.metaitem.01/32000"), "gregtech-gt-metaitem-01-32000");
    EXPECT_EQ(slugify("  //Leading/And//Trailing/Slashes//  "), "leading-and-trailing-slashes");
    EXPECT_EQ(slugify("Fluid:Water / Steam.High+"), "fluid-water-steam-highplus");
    EXPECT_EQ(slugify("Iron_Ore (Impure)"), "iron_ore-impure");
    EXPECT_EQ(slugify("   "), "");
}

TEST(SlugifyTest, IdentifierAndSlugValidationHelpers) {
    EXPECT_TRUE(isValidSlugId("iron-ore_01"));
    EXPECT_FALSE(isValidSlugId("Iron-Ore"));
    EXPECT_FALSE(isValidSlugId("iron/ore"));
    EXPECT_FALSE(isValidSlugId(""));

    EXPECT_TRUE(isValidIdentifierId("clock_speed_1"));
    EXPECT_TRUE(isValidIdentifierId("_private"));
    EXPECT_FALSE(isValidIdentifierId("1clock"));
    EXPECT_FALSE(isValidIdentifierId("clock-speed"));
    EXPECT_FALSE(isValidIdentifierId(""));

    EXPECT_TRUE(isReservedIdentifier("machine"));
    EXPECT_TRUE(isReservedIdentifier("craft_time"));
    EXPECT_TRUE(isReservedIdentifier("lookup"));
    EXPECT_TRUE(isReservedIdentifier("has_tag"));
    EXPECT_FALSE(isReservedIdentifier("clock"));
}

// ============================================================================
// 2. Lexer & Parser tests (§5.1–§5.2)
// ============================================================================

TEST(ExprLexerAndParserTest, OperatorPrecedenceAndAssociativity) {
    CompileEnv cenv = makeStandardCompileEnv();
    EvalEnv eenv = makeStandardEvalEnv();

    // ^ is right-associative and binds tighter than unary minus (§5.2)
    EXPECT_DOUBLE_EQ(evalNum("-2^2", cenv, eenv), -4.0);
    EXPECT_DOUBLE_EQ(evalNum("(-2)^2", cenv, eenv), 4.0);
    EXPECT_DOUBLE_EQ(evalNum("2^3^2", cenv, eenv), 512.0);
    EXPECT_DOUBLE_EQ(evalNum("(2^3)^2", cenv, eenv), 64.0);
    EXPECT_DOUBLE_EQ(evalNum("2^-1", cenv, eenv), 0.5);
    EXPECT_DOUBLE_EQ(evalNum("4^-0.5", cenv, eenv), 0.5);

    // Arithmetic precedence
    EXPECT_DOUBLE_EQ(evalNum("1 + 2 * 3 - 4 / 2", cenv, eenv), 5.0);
    EXPECT_DOUBLE_EQ(evalNum("7.5 % 2.0", cenv, eenv), 1.5);
    EXPECT_DOUBLE_EQ(evalNum("-7.5 % 2.0", cenv, eenv), -1.5);
    EXPECT_DOUBLE_EQ(evalNum("1e-9 * 1e9 + 2.5E3", cenv, eenv), 2501.0);

    // ! binds looser than comparisons (§5.2: !a == b means !(a == b))
    EXPECT_TRUE(evalBool("!1 == 2", cenv, eenv));
    EXPECT_FALSE(evalBool("!2 == 2", cenv, eenv));
    EXPECT_TRUE(evalBool("!!true", cenv, eenv));
    EXPECT_TRUE(evalBool("false || true && false == false", cenv, eenv));
}

TEST(ExprLexerAndParserTest, StringQuotesAndEscapes) {
    CompileEnv cenv = makeStandardCompileEnv();
    EvalEnv eenv = makeStandardEvalEnv();

    EXPECT_TRUE(evalBool("game.edition == \"space-age\"", cenv, eenv));
    EXPECT_TRUE(evalBool("game.edition == 'space-age'", cenv, eenv));
    EXPECT_TRUE(evalBool("'a\\'b\\\\c\\\"d' == \"a'b\\\\c\\\"d\"", cenv, eenv));

    // Invalid escapes and unterminated strings are rejected with V-EXPR-1
    EXPECT_FALSE(parse("\"hello\\nworld\"").ok);
    EXPECT_FALSE(parse("'unterminated").ok);
    EXPECT_FALSE(parse("\"trailing\\").ok);
}

TEST(ExprLexerAndParserTest, RejectsInvalidNumbersAndChainedComparisons) {
    // Leading dot, trailing dot, hex, missing exponent digits, suffix
    EXPECT_FALSE(parse(".5").ok);
    EXPECT_FALSE(parse("12.").ok);
    EXPECT_FALSE(parse("0x10").ok);
    EXPECT_FALSE(parse("1.5e").ok);
    EXPECT_FALSE(parse("1.5e+").ok);
    EXPECT_FALSE(parse("1.5f").ok);
    EXPECT_FALSE(parse("1.2.3").ok);

    // Non-chaining comparisons (§5.2)
    ParseResult chained_lt = parse("1 < 2 < 3");
    EXPECT_FALSE(chained_lt.ok);
    EXPECT_EQ(chained_lt.error.rule_id, "V-EXPR-1");

    ParseResult chained_eq = parse("1 == 1 == true");
    EXPECT_FALSE(chained_eq.ok);
    EXPECT_EQ(chained_eq.error.rule_id, "V-EXPR-1");

    // No ternary ?:
    EXPECT_FALSE(parse("true ? 1 : 0").ok);
}

// ============================================================================
// 3. Type & Context Checking tests (§5.3–§5.5)
// ============================================================================

TEST(ExprTypeAndContextTest, EnforcesAllEightContexts) {
    CompileEnv env = makeStandardCompileEnv();
    env.let_types["c"] = ExprType::number();

    // 1. GlobalStatic: allows game.*, global.*, option (in allowed_if); rejects machine, recipe, param, port, let, craft_time, node
    env.context = ExprContext::GlobalStatic;
    EXPECT_TRUE(compile("game.ticks_per_second + global.mining_prod_level", env, ExpectedType::Number).ok);
    EXPECT_FALSE(compile("machine.speed", env, ExpectedType::Number).ok);
    EXPECT_EQ(compile("machine.speed", env, ExpectedType::Number).error.rule_id, "V-EXPR-3");
    EXPECT_FALSE(compile("recipe.time", env, ExpectedType::Number).ok);
    EXPECT_FALSE(compile("param.clock", env, ExpectedType::Number).ok);
    EXPECT_FALSE(compile("c", env, ExpectedType::Number).ok);
    EXPECT_FALSE(compile("craft_time", env, ExpectedType::Number).ok);
    EXPECT_FALSE(compile("node.machines", env, ExpectedType::Number).ok);

    // 2. ParamStatic: standalone vs profile-bound parameter order (V-EXPR-5)
    env.context = ExprContext::ParamStatic;
    env.check_profile_params = true;
    env.max_param_index = 1; // "somersloops" is at index 1; "clock" is at index 0
    EXPECT_TRUE(compile("param.clock / 100", env, ExpectedType::Number).ok);
    CompileResult self_ref = compile("param.somersloops", env, ExpectedType::Number);
    EXPECT_FALSE(self_ref.ok);
    EXPECT_EQ(self_ref.error.rule_id, "V-EXPR-5");
    CompileResult later_ref = compile("param.beacon_count", env, ExpectedType::Number);
    EXPECT_FALSE(later_ref.ok);
    EXPECT_EQ(later_ref.error.rule_id, "V-EXPR-5");

    // Standalone parameter check (Stage 4 before profile binding)
    env.check_profile_params = false;
    env.current_param_id = "somersloops";
    EXPECT_TRUE(compile("param.beacon_count", env, ExpectedType::Number).ok);
    EXPECT_FALSE(compile("param.somersloops", env, ExpectedType::Number).ok);
    EXPECT_EQ(compile("param.unknown_p", env, ExpectedType::Number).error.rule_id, "V-EXPR-5");
    env.check_profile_params = true;
    env.current_param_id.clear();

    // Port-bound resource_choice visible_if / allowed_if in ParamStatic
    env.allow_port_in_static = true;
    env.allow_option = true;
    env.option_type = ExprType::resource();
    CompileResult port_bound = compile("has_tag(option, port.slot_tag)", env, ExpectedType::Bool);
    EXPECT_TRUE(port_bound.ok);
    EXPECT_TRUE(port_bound.expr.references_port);
    EXPECT_FALSE(port_bound.expr.references_port_resource);
    // port.resource is forbidden in static context even when allow_port_in_static is true
    CompileResult port_res_in_static = compile("port.resource == 'coal'", env, ExpectedType::Bool);
    EXPECT_FALSE(port_res_in_static.ok);
    EXPECT_EQ(port_res_in_static.error.rule_id, "V-EXPR-3");
    env.allow_port_in_static = false;
    env.allow_option = false;

    // 3. PreSolveLet & PreSolveCraftTime: craft_time, port.*, node.* forbidden
    env.context = ExprContext::PreSolveCraftTime;
    EXPECT_TRUE(compile("recipe.time / (machine.speed * c)", env, ExpectedType::Number).ok);
    EXPECT_FALSE(compile("craft_time * 2", env, ExpectedType::Number).ok);
    EXPECT_EQ(compile("craft_time * 2", env, ExpectedType::Number).error.rule_id, "V-EXPR-3");
    EXPECT_FALSE(compile("node.machines", env, ExpectedType::Number).ok);
    EXPECT_EQ(compile("node.machines", env, ExpectedType::Number).error.rule_id, "V-EXPR-3");

    // 4. PreSolveAfterCraftTime: craft_time allowed, port.* and node.* forbidden
    env.context = ExprContext::PreSolveAfterCraftTime;
    EXPECT_TRUE(compile("machine.water_per_s * craft_time", env, ExpectedType::Number).ok);
    EXPECT_FALSE(compile("port.amount", env, ExpectedType::Number).ok);
    EXPECT_FALSE(compile("node.crafts", env, ExpectedType::Number).ok);

    // 5. RecipePortResourceExpr: port.* allowed except port.resource; ExpectedType::ResourceOrString
    env.context = ExprContext::RecipePortResourceExpr;
    env.port_has_base_resource = true;
    EXPECT_TRUE(compile("if(is_set(param.prolif), param.prolif, port.base_resource)", env, ExpectedType::ResourceOrString).ok);
    EXPECT_TRUE(compile("if(port.min_tier > 2, 'coal', '')", env, ExpectedType::ResourceOrString).ok);
    EXPECT_FALSE(compile("port.resource", env, ExpectedType::ResourceOrString).ok);
    EXPECT_EQ(compile("port.resource", env, ExpectedType::ResourceOrString).error.rule_id, "V-EXPR-3");
    // Unknown non-empty resource literal in result position is rejected with V-REF-1
    CompileResult bad_res_lit = compile("if(port.min_tier > 2, 'unknown-item', '')", env, ExpectedType::ResourceOrString);
    EXPECT_FALSE(bad_res_lit.ok);
    EXPECT_EQ(bad_res_lit.error.rule_id, "V-REF-1");
    // Non-result string literal in condition is NOT checked as a resource ID (R2-08)
    EXPECT_TRUE(compile("if(game.edition == 'not_a_resource_id', 'coal', '')", env, ExpectedType::ResourceOrString).ok);

    // 6. RecipePortFormula: port.resource allowed and tracked
    env.context = ExprContext::RecipePortFormula;
    CompileResult port_en_no_res = compile("port.min_tier <= 3", env, ExpectedType::Bool);
    EXPECT_TRUE(port_en_no_res.ok);
    EXPECT_TRUE(port_en_no_res.expr.references_port);
    EXPECT_FALSE(port_en_no_res.expr.references_port_resource);

    CompileResult port_en_with_res = compile("port.resource.fuel_value > 0", env, ExpectedType::Bool);
    EXPECT_TRUE(port_en_with_res.ok);
    EXPECT_TRUE(port_en_with_res.expr.references_port);
    EXPECT_TRUE(port_en_with_res.expr.references_port_resource);

    // 7. PostSolveMetricValue: node.machines and node.crafts allowed; port.* forbidden
    env.context = ExprContext::PostSolveMetricValue;
    EXPECT_TRUE(compile("ceil(node.machines) * node.crafts * craft_time", env, ExpectedType::Number).ok);
    EXPECT_FALSE(compile("port.amount", env, ExpectedType::Number).ok);
}

TEST(ExprTypeAndContextTest, PostfixMemberAccessOnLetAndIfExpressions) {
    CompileEnv cenv = makeStandardCompileEnv();
    EvalEnv eenv = makeStandardEvalEnv();
    cenv.context = ExprContext::RecipePortFormula;
    eenv.port.resource = "coal";
    eenv.port.base_resource = "iron-ore";

    // Let variable holding option<gt_tiers> and resource
    cenv.let_types["tier_opt"] = ExprType::option("gt_tiers");
    cenv.let_types["chosen_res"] = ExprType::resource();
    eenv.lets["tier_opt"] = EvalValue::option("gt_tiers", "hv");
    eenv.lets["chosen_res"] = EvalValue::resource("coal");

    EXPECT_DOUBLE_EQ(evalNum("tier_opt.v", cenv, eenv), 512.0);
    EXPECT_TRUE(evalBool("tier_opt.key == 'hv'", cenv, eenv));
    EXPECT_DOUBLE_EQ(evalNum("chosen_res.fuel_value", cenv, eenv), 4.0);
    EXPECT_TRUE(evalBool("chosen_res.id == 'coal'", cenv, eenv));
    EXPECT_TRUE(evalBool("if(true, port.resource, port.base_resource).id == 'coal'", cenv, eenv));
    EXPECT_DOUBLE_EQ(evalNum("if(false, port.resource, port.base_resource).fuel_value", cenv, eenv), 0.0);

    // Non-number attribute on slots<S> is rejected with V-EXPR-4
    CompileResult non_num_slot_attr = compile("sum(param.modules.tag_name)", cenv, ExpectedType::Number);
    EXPECT_FALSE(non_num_slot_attr.ok);
    EXPECT_EQ(non_num_slot_attr.error.rule_id, "V-EXPR-4");
}

TEST(ExprTypeAndContextTest, RejectsTypeMismatchesAndInvalidOptionOrResourceKeys) {
    CompileEnv env = makeStandardCompileEnv();

    // No implicit bool <-> number conversions
    EXPECT_FALSE(compile("1 + true", env, ExpectedType::Number).ok);
    EXPECT_FALSE(compile("if(1, 2, 3)", env, ExpectedType::Number).ok);
    EXPECT_FALSE(compile("if(true, 1, 'str')", env, ExpectedType::Any).ok);

    // Comparing options from different option sets is a type error (V-EXPR-4)
    CompileResult diff_sets = compile("param.voltage_tier == param.prolif_mode", env, ExpectedType::Bool);
    EXPECT_FALSE(diff_sets.ok);
    EXPECT_EQ(diff_sets.error.rule_id, "V-EXPR-4");

    // Invalid option key string literal in comparison (V-EXPR-2)
    CompileResult bad_opt_lit = compile("param.prolif_mode == 'extras'", env, ExpectedType::Bool);
    EXPECT_FALSE(bad_opt_lit.ok);
    EXPECT_EQ(bad_opt_lit.error.rule_id, "V-EXPR-2");
    EXPECT_EQ(bad_opt_lit.error.offending_kind, OffendingSymbolKind::OptionKey);
    EXPECT_EQ(bad_opt_lit.error.offending_symbol, "extras");
    EXPECT_EQ(bad_opt_lit.error.offending_parent, "prolif_modes");

    // Invalid resource id string literal in comparison (V-EXPR-2)
    CompileResult bad_res_cmp = compile("param.prolif == 'crushed_coper'", env, ExpectedType::Bool);
    EXPECT_FALSE(bad_res_cmp.ok);
    EXPECT_EQ(bad_res_cmp.error.rule_id, "V-EXPR-2");
    EXPECT_EQ(bad_res_cmp.error.offending_kind, OffendingSymbolKind::Resource);
}

// ============================================================================
// 4. Diagnostics, Caret Formatting & Levenshtein Suggestions (§5.7)
// ============================================================================

TEST(ExprDiagnosticsAndSuggestionsTest, CaretFormattingAndDidYouMean) {
    CompileEnv env = makeStandardCompileEnv();

    // Missing closing parenthesis example from §5.7
    CompileResult syntax_err = compile(
        "min(1, floor(log(recipe.time_raw, 2) + 1e-9)", env, ExpectedType::Number);
    EXPECT_FALSE(syntax_err.ok);
    EXPECT_EQ(syntax_err.error.rule_id, "V-EXPR-1");
    std::string formatted = formatCompileError(
        syntax_err.error, "recipes/gt.json", "/profiles/gt_single/let/oc");
    EXPECT_NE(formatted.find("recipes/gt.json: /profiles/gt_single/let/oc: 1:45:"), std::string::npos);
    EXPECT_NE(formatted.find("^"), std::string::npos);

    // Unknown param with Levenshtein suggestion from §5.7
    CompileResult typo_param = compile("recipe.time / (machine.speed * param.clok)", env, ExpectedType::Number);
    EXPECT_FALSE(typo_param.ok);
    EXPECT_EQ(typo_param.error.rule_id, "V-EXPR-5");
    EXPECT_NE(typo_param.error.message.find("did you mean 'param.clock'?"), std::string::npos);

    // .attrs.x detection on machine, recipe, port, game, option, and resource (§5.7, V-EXPR-2)
    env.context = ExprContext::RecipePortFormula;
    CompileResult mach_attrs = compile("machine.attrs.power", env, ExpectedType::Number);
    EXPECT_FALSE(mach_attrs.ok);
    EXPECT_EQ(mach_attrs.error.rule_id, "V-EXPR-2");
    EXPECT_NE(mach_attrs.error.message.find("did you mean 'machine.power'?"), std::string::npos);

    CompileResult rec_attrs = compile("recipe.attrs.eut", env, ExpectedType::Number);
    EXPECT_FALSE(rec_attrs.ok);
    EXPECT_NE(rec_attrs.error.message.find("did you mean 'recipe.eut'?"), std::string::npos);

    CompileResult port_attrs = compile("port.attrs.min_tier", env, ExpectedType::Number);
    EXPECT_FALSE(port_attrs.ok);
    EXPECT_NE(port_attrs.error.message.find("did you mean 'port.min_tier'?"), std::string::npos);

    CompileResult opt_attrs = compile("param.voltage_tier.attrs.t", env, ExpectedType::Number);
    EXPECT_FALSE(opt_attrs.ok);
    EXPECT_NE(opt_attrs.error.message.find("did you mean '.t'?"), std::string::npos);

    CompileResult res_attrs = compile("port.resource.attrs.fuel_value", env, ExpectedType::Number);
    EXPECT_FALSE(res_attrs.ok);
    EXPECT_NE(res_attrs.error.message.find("did you mean '.fuel_value'?"), std::string::npos);
}

TEST(ExprDiagnosticsAndSuggestionsTest, SuggestionBudgetCapsAtTwentyFive) {
    CompileEnv env = makeStandardCompileEnv();
    SuggestionBudget budget;
    env.suggestion_budget = &budget;

    for (size_t i = 0; i < 25; ++i) {
        CompileResult r = compile("param.clok", env, ExpectedType::Number);
        EXPECT_FALSE(r.ok);
        EXPECT_NE(r.error.message.find("did you mean 'param.clock'?"), std::string::npos);
    }
    EXPECT_EQ(budget.used, 25u);

    // 26th error exceeds budget and omits fuzzy Levenshtein search
    CompileResult r26 = compile("param.clok", env, ExpectedType::Number);
    EXPECT_FALSE(r26.ok);
    EXPECT_EQ(r26.error.message.find("did you mean"), std::string::npos);
}

// ============================================================================
// 5. Built-in Functions tests (§5.6)
// ============================================================================

TEST(ExprBuiltinFunctionsTest, AllScalarAndListBuiltins) {
    CompileEnv cenv = makeStandardCompileEnv();
    EvalEnv eenv = makeStandardEvalEnv();

    // Scalar min / max (1 arg and multi-arg)
    EXPECT_DOUBLE_EQ(evalNum("min(42)", cenv, eenv), 42.0);
    EXPECT_DOUBLE_EQ(evalNum("max(42)", cenv, eenv), 42.0);
    EXPECT_DOUBLE_EQ(evalNum("min(5, -2, 8, 1)", cenv, eenv), -2.0);
    EXPECT_DOUBLE_EQ(evalNum("max(5, -2, 8, 1)", cenv, eenv), 8.0);

    // List min / max / sum / count / is_set on empty slots
    eenv.params["modules"] = EvalValue::slots("modules", {std::nullopt, std::nullopt});
    EXPECT_DOUBLE_EQ(evalNum("sum(param.modules.speed)", cenv, eenv), 0.0);
    EXPECT_DOUBLE_EQ(evalNum("min(param.modules.speed)", cenv, eenv), 0.0);
    EXPECT_DOUBLE_EQ(evalNum("max(param.modules.speed)", cenv, eenv), 0.0);
    EXPECT_DOUBLE_EQ(evalNum("min(param.modules.speed, 99)", cenv, eenv), 99.0);
    EXPECT_DOUBLE_EQ(evalNum("max(param.modules.speed, -5)", cenv, eenv), -5.0);
    EXPECT_DOUBLE_EQ(evalNum("count(param.modules)", cenv, eenv), 0.0);
    EXPECT_DOUBLE_EQ(evalNum("count(param.modules, 'speed-module-3')", cenv, eenv), 0.0);
    EXPECT_FALSE(evalBool("is_set(param.modules)", cenv, eenv));

    // Populated slots
    eenv.params["modules"] = EvalValue::slots(
        "modules", {"speed-module-3", "productivity-module-3", std::nullopt, "speed-module-3"});
    EXPECT_NEAR(evalNum("sum(param.modules.speed)", cenv, eenv), 0.85, 1e-12);
    EXPECT_DOUBLE_EQ(evalNum("min(param.modules.speed)", cenv, eenv), -0.15);
    EXPECT_DOUBLE_EQ(evalNum("max(param.modules.speed)", cenv, eenv), 0.5);
    EXPECT_DOUBLE_EQ(evalNum("count(param.modules)", cenv, eenv), 3.0);
    EXPECT_DOUBLE_EQ(evalNum("count(param.modules, 'speed-module-3')", cenv, eenv), 2.0);
    EXPECT_DOUBLE_EQ(evalNum("count(param.modules, 'efficiency-module-3')", cenv, eenv), 0.0);
    EXPECT_TRUE(evalBool("is_set(param.modules)", cenv, eenv));
    EXPECT_TRUE(evalBool("is_set(global.global_modules)", cenv, eenv));

    // clamp, floor, ceil, round (half away from zero), abs, sqrt, pow, exp, ln, log
    EXPECT_DOUBLE_EQ(evalNum("clamp(15, 0, 10)", cenv, eenv), 10.0);
    EXPECT_DOUBLE_EQ(evalNum("clamp(-5, 0, 10)", cenv, eenv), 0.0);
    EXPECT_DOUBLE_EQ(evalNum("clamp(4, 4, 4)", cenv, eenv), 4.0);
    EXPECT_DOUBLE_EQ(evalNum("floor(2.9)", cenv, eenv), 2.0);
    EXPECT_DOUBLE_EQ(evalNum("floor(-2.1)", cenv, eenv), -3.0);
    EXPECT_DOUBLE_EQ(evalNum("ceil(2.1)", cenv, eenv), 3.0);
    EXPECT_DOUBLE_EQ(evalNum("ceil(-2.9)", cenv, eenv), -2.0);
    EXPECT_DOUBLE_EQ(evalNum("round(2.5)", cenv, eenv), 3.0);
    EXPECT_DOUBLE_EQ(evalNum("round(-2.5)", cenv, eenv), -3.0);
    EXPECT_DOUBLE_EQ(evalNum("round(2.4)", cenv, eenv), 2.0);
    EXPECT_DOUBLE_EQ(evalNum("abs(-12.5)", cenv, eenv), 12.5);
    EXPECT_DOUBLE_EQ(evalNum("sqrt(16)", cenv, eenv), 4.0);
    EXPECT_DOUBLE_EQ(evalNum("pow(2, 10)", cenv, eenv), 1024.0);
    EXPECT_NEAR(evalNum("ln(exp(3.5))", cenv, eenv), 3.5, 1e-12);
    EXPECT_NEAR(evalNum("log(512 / 8, 4)", cenv, eenv), 3.0, 1e-12);

    // lookup (array and object, with near-integer key rounding via std::llround)
    EXPECT_DOUBLE_EQ(evalNum("lookup('gt_voltage', 0)", cenv, eenv), 8.0);
    EXPECT_DOUBLE_EQ(evalNum("lookup('gt_voltage', 1.9999999995)", cenv, eenv), 128.0);
    EXPECT_DOUBLE_EQ(evalNum("lookup('gt_voltage', 99, -1)", cenv, eenv), -1.0);
    EXPECT_DOUBLE_EQ(evalNum("lookup('coil_heat', 'kanthal')", cenv, eenv), 2701.0);
    EXPECT_DOUBLE_EQ(evalNum("lookup('coil_heat', 'missing_coil', 900)", cenv, eenv), 900.0);

    // has_tag
    EXPECT_TRUE(evalBool("has_tag(global.global_fuel, 'chemical')", cenv, eenv));
    EXPECT_FALSE(evalBool("has_tag(global.global_fuel, 'ore')", cenv, eenv));
}

// ============================================================================
// 6. Short-Circuiting & Unset (`allow_none`) Semantics (§5.2, §5.3)
// ============================================================================

TEST(ExprShortCircuitAndUnsetTest, ShortCircuitsAndHandlesUnsetParameters) {
    CompileEnv cenv = makeStandardCompileEnv();
    EvalEnv eenv = makeStandardEvalEnv();

    // param.prolif and global.global_tier are unset (std::nullopt)
    EXPECT_FALSE(evalBool("is_set(param.prolif)", cenv, eenv));
    EXPECT_FALSE(evalBool("is_set(global.global_tier)", cenv, eenv));
    EXPECT_TRUE(evalBool("is_set(global.global_fuel)", cenv, eenv));
    EXPECT_TRUE(evalBool("is_set(param.voltage_tier)", cenv, eenv));

    // Equality with unset option/resource evaluates to false (==) and true (!=), never an error
    EXPECT_FALSE(evalBool("param.prolif == 'proliferator-mk1'", cenv, eenv));
    EXPECT_TRUE(evalBool("param.prolif != 'proliferator-mk1'", cenv, eenv));
    EXPECT_FALSE(evalBool("global.global_tier == 'lv'", cenv, eenv));
    EXPECT_TRUE(evalBool("global.global_tier != 'lv'", cenv, eenv));

    // Short-circuiting prevents runtime errors from reading .attr, .id, .key, has_tag, or / 0
    EXPECT_FALSE(evalBool("is_set(param.prolif) && param.prolif.charges > 0", cenv, eenv));
    EXPECT_FALSE(evalBool("is_set(param.prolif) && has_tag(param.prolif, 'proliferator')", cenv, eenv));
    EXPECT_TRUE(evalBool("!is_set(param.prolif) || param.prolif.charges > 0", cenv, eenv));
    EXPECT_DOUBLE_EQ(evalNum("if(is_set(param.prolif), param.prolif.extra, 0)", cenv, eenv), 0.0);
    EXPECT_DOUBLE_EQ(evalNum("if(false, 1 / 0, 42)", cenv, eenv), 42.0);
    EXPECT_TRUE(evalBool("true || (1 / 0 > 0)", cenv, eenv));
    EXPECT_FALSE(evalBool("false && (1 / 0 > 0)", cenv, eenv));
}

// ============================================================================
// 7. Topological Sort & Cycle Detection tests (V-EXPR-6, V-OVR-1, V-OVR-2)
// ============================================================================

TEST(ExprDependencySortAndCyclesTest, TopologicalSortAndCycleSeparation) {
    // Diamond DAG with tie-breaking by name:
    // d depends on b and c; b and c depend on a; e has no dependencies
    std::unordered_map<std::string, std::vector<std::string>> dag = {
        {"d", {"b", "c"}},
        {"b", {"a"}},
        {"c", {"a"}},
        {"a", {}},
        {"e", {}}
    };
    DependencyOrderResult dag_res = resolveDependencyOrder(dag);
    EXPECT_TRUE(dag_res.ok);
    std::vector<std::string> expected_order = {"a", "b", "c", "d", "e"};
    EXPECT_EQ(dag_res.order, expected_order);

    // Cycle oc <-> t, plus downstream node 'z' depending on 'oc', and independent node 'a'
    std::unordered_map<std::string, std::vector<std::string>> cyclic = {
        {"oc", {"t"}},
        {"t", {"oc"}},
        {"z", {"oc"}},
        {"a", {}}
    };
    DependencyOrderResult cyc_res = resolveDependencyOrder(cyclic);
    EXPECT_FALSE(cyc_res.ok);
    EXPECT_EQ(cyc_res.order, std::vector<std::string>({"a"}));
    EXPECT_EQ(cyc_res.cycled_nodes, (std::vector<std::string>{"oc", "t"}));
    EXPECT_EQ(cyc_res.downstream_of_cycle, std::vector<std::string>({"z"}));
    EXPECT_EQ(cyc_res.cycle_message, "cycle oc -> t -> oc");
}

TEST(ExprDependencySortAndCyclesTest, CompileLetMapAndRecipeOverrides) {
    CompileEnv env = makeStandardCompileEnv();
    std::unordered_map<std::string, std::string> base_lets = {
        {"c", "param.clock / 100"},
        {"amp", "1 + param.somersloops"},
        {"eff", "c * amp"}
    };

    LetCompileResult base_res = compileLetMap(base_lets, env);
    ASSERT_TRUE(base_res.ok);
    EXPECT_EQ(base_res.evaluation_order, (std::vector<std::string>{"amp", "c", "eff"}));

    // Valid override preserving type
    LetCompileResult valid_ovr = compileLetMap(
        base_lets, env, {{"amp", "2"}}, &base_res.let_types);
    EXPECT_TRUE(valid_ovr.ok);

    // V-OVR-1: overriding a let name that does not exist in the profile
    LetCompileResult unknown_ovr = compileLetMap(
        base_lets, env, {{"nonexistent", "1"}}, &base_res.let_types);
    ASSERT_FALSE(unknown_ovr.ok);
    EXPECT_EQ(unknown_ovr.errors.front().rule_id, "V-OVR-1");

    // V-OVR-2: override changing let variable's type (number -> bool)
    LetCompileResult type_change_ovr = compileLetMap(
        base_lets, env, {{"amp", "true"}}, &base_res.let_types);
    ASSERT_FALSE(type_change_ovr.ok);
    EXPECT_EQ(type_change_ovr.errors.front().rule_id, "V-OVR-2");

    // V-OVR-2: override introducing a dependency cycle (c -> eff -> c)
    LetCompileResult cycle_ovr = compileLetMap(
        base_lets, env, {{"c", "eff + 1"}}, &base_res.let_types);
    ASSERT_FALSE(cycle_ovr.ok);
    EXPECT_EQ(cycle_ovr.errors.front().rule_id, "V-OVR-2");
    EXPECT_EQ(cycle_ovr.errors.front().message, "cycle c -> eff -> c");
}

// ============================================================================
// 8. Runtime Errors, -0.0 Normalization & Spec §10 Worked Examples
// ============================================================================

TEST(ExprRuntimeAndDeterminismTest, NormalizesNegativeZeroAndCatchesRuntimeErrors) {
    CompileEnv cenv = makeStandardCompileEnv();
    EvalEnv eenv = makeStandardEvalEnv();

    // -0.0 is normalized to +0.0 (signbit == false)
    for (const char* expr_str : {"-0", "0 * -5", "-1 * 0", "round(-0.1)", "ceil(-0.5)"}) {
        double v = evalNum(expr_str, cenv, eenv);
        EXPECT_DOUBLE_EQ(v, 0.0);
        EXPECT_FALSE(std::signbit(v)) << "Negative zero not normalized for: " << expr_str;
    }

    // Runtime error cases (§5.7)
    const std::vector<std::string> error_exprs = {
        "1 / 0",
        "5 % 0",
        "sqrt(-0.001)",
        "ln(0)",
        "ln(-2)",
        "log(8, 1)",
        "log(8, -2)",
        "log(0, 2)",
        "pow(0, -1)",
        "0 ^ -1",
        "(-2) ^ 0.5",
        "clamp(5, 10, 2)",
        "exp(10000)",
        "lookup('gt_voltage', 9.5)",
        "lookup('gt_voltage', 9.5, 0)",
        "lookup('gt_voltage', 99)",
        "lookup('coil_heat', 'unknown_coil')",
        "param.prolif.charges",
        "param.prolif.id",
        "global.global_tier.key",
        "lookup('coil_heat', param.prolif, 0)"
    };

    for (const std::string& src : error_exprs) {
        CompileResult cr = compile(src, cenv, ExpectedType::Any);
        ASSERT_TRUE(cr.ok) << "Expected compile success for runtime-error test: " << src;
        EvalResult er = evaluate(cr.expr, eenv);
        EXPECT_FALSE(er.ok) << "Expected runtime error for: " << src;
    }

    // has_tag on unset resource fails at runtime
    CompileResult ht_cr = compile("has_tag(param.prolif, 'proliferator')", cenv, ExpectedType::Bool);
    ASSERT_TRUE(ht_cr.ok);
    EXPECT_FALSE(evaluate(ht_cr.expr, eenv).ok);

    // Runtime error formatting (§5.7)
    std::string formatted = formatRuntimeError(
        "Macerator #3", "division by zero", "profiles/gt_single/let/oc", "macerate-crushed-copper");
    EXPECT_EQ(
        formatted,
        "Node \"Macerator #3\": division by zero in profiles/gt_single/let/oc (recipe 'macerate-crushed-copper')");
}

TEST(ExprWorkedExamplesTest, EvaluatesAllFiveSpecExamplesExactly) {
    CompileEnv cenv = makeStandardCompileEnv();
    EvalEnv eenv = makeStandardEvalEnv();

    auto runLets = [&](const std::unordered_map<std::string, std::string>& lets) {
        LetCompileResult lr = compileLetMap(lets, cenv);
        EXPECT_TRUE(lr.ok);
        cenv.let_types = lr.let_types;
        eenv.lets.clear();
        for (const std::string& name : lr.evaluation_order) {
            EvalResult er = evaluate(lr.compiled_lets.at(name), eenv);
            EXPECT_TRUE(er.ok) << er.error;
            eenv.lets[name] = er.value;
        }
    };

    // --- Example 10.1: Satisfactory Constructor (clock = 150, somersloops = 1) ---
    eenv.params["clock"] = EvalValue::number(150.0);
    eenv.params["somersloops"] = EvalValue::number(1.0);
    runLets({
        {"c", "param.clock / 100"},
        {"amp", "if(machine.somersloop_slots > 0, 1 + param.somersloops / machine.somersloop_slots, 1)"},
        {"shards_per_machine", "clamp(ceil((param.clock - 100) / 50), 0, 3)"}
    });
    cenv.context = ExprContext::PreSolveCraftTime;
    eenv.craft_time = evalNum("recipe.time / (machine.speed * c)", cenv, eenv);
    EXPECT_DOUBLE_EQ(eenv.craft_time, 4.0);

    cenv.context = ExprContext::RecipePortFormula;
    eenv.port.amount = 2.0;
    eenv.port.chance = 1.0;
    EXPECT_DOUBLE_EQ(evalNum("port.amount * port.chance * amp", cenv, eenv), 4.0);

    cenv.context = ExprContext::PreSolveAfterCraftTime;
    EXPECT_NEAR(evalNum("machine.power * amp ^ 2 * c ^ 1.321928", cenv, eenv), 27.34638, 1e-4);

    cenv.context = ExprContext::PostSolveMetricValue;
    eenv.node.machines = 1.5;
    EXPECT_DOUBLE_EQ(evalNum("ceil(node.machines) * shards_per_machine", cenv, eenv), 2.0);
    EXPECT_DOUBLE_EQ(evalNum("ceil(node.machines) * param.somersloops", cenv, eenv), 2.0);

    // --- Example 10.2: GTNH Macerator at MV (t=2) and HV (t=3) ---
    eenv.recipe.time_raw = 400.0;
    eenv.recipe.attrs["eut"] = EvalValue::number(2.0);
    eenv.port.amount = 1.0;
    eenv.port.chance = 0.1;
    eenv.port.attrs["min_tier"] = EvalValue::number(3.0);
    eenv.port.attrs["chance_boost"] = EvalValue::number(0.0);

    eenv.params["voltage_tier"] = EvalValue::option("gt_tiers", "mv");
    runLets({
        {"t", "param.voltage_tier.t"},
        {"v", "param.voltage_tier.v"},
        {"oc_v", "max(0, floor(log(v / max(8, recipe.eut), 4) + 1e-9))"},
        {"oc", "min(oc_v, max(0, floor(log(recipe.time_raw, 2) + 1e-9)))"}
    });
    cenv.context = ExprContext::PreSolveCraftTime;
    EXPECT_DOUBLE_EQ(
        evalNum("max(1, floor(recipe.time_raw / 2 ^ oc)) / game.ticks_per_second", cenv, eenv), 5.0);
    cenv.context = ExprContext::RecipePortFormula;
    EXPECT_FALSE(evalBool("port.min_tier <= t", cenv, eenv));

    eenv.params["voltage_tier"] = EvalValue::option("gt_tiers", "hv");
    runLets({
        {"t", "param.voltage_tier.t"},
        {"v", "param.voltage_tier.v"},
        {"oc_v", "max(0, floor(log(v / max(8, recipe.eut), 4) + 1e-9))"},
        {"oc", "min(oc_v, max(0, floor(log(recipe.time_raw, 2) + 1e-9)))"}
    });
    cenv.context = ExprContext::PreSolveCraftTime;
    EXPECT_DOUBLE_EQ(
        evalNum("max(1, floor(recipe.time_raw / 2 ^ oc)) / game.ticks_per_second", cenv, eenv), 2.5);
    cenv.context = ExprContext::RecipePortFormula;
    EXPECT_TRUE(evalBool("port.min_tier <= t", cenv, eenv));
    EXPECT_DOUBLE_EQ(
        evalNum("port.amount * min(1, port.chance + port.chance_boost * oc)", cenv, eenv), 0.1);
    cenv.context = ExprContext::PreSolveAfterCraftTime;
    EXPECT_DOUBLE_EQ(evalNum("recipe.eut * 4 ^ oc", cenv, eenv), 128.0);

    // --- Example 10.3: GTNH Crop Manager (LV, growth=31, gain=31, hydrated, fertilized) ---
    eenv.params["voltage_tier"] = EvalValue::option("gt_tiers", "lv");
    eenv.params["growth"] = EvalValue::number(31.0);
    eenv.params["gain"] = EvalValue::number(31.0);
    eenv.params["hydrated"] = EvalValue::boolean(true);
    eenv.params["fertilized"] = EvalValue::boolean(true);
    eenv.machine.attrs["water_per_s"] = EvalValue::number(0.5);
    eenv.machine.attrs["fertilizer_per_s"] = EvalValue::number(0.01);
    eenv.recipe.attrs["crop_tier"] = EvalValue::number(4.0);
    eenv.recipe.attrs["growth_points"] = EvalValue::number(3200.0);

    runLets({
        {"t", "param.voltage_tier.t"},
        {"points_per_tick", "3 + param.growth + if(param.hydrated, 2, 0) + if(param.fertilized, 2, 0)"},
        {"yield", "1.03 ^ param.gain"}
    });
    cenv.context = ExprContext::PreSolveLet;
    EXPECT_TRUE(evalBool("recipe.crop_tier <= lookup(\"crop_tier_cap\", t)", cenv, eenv));
    cenv.context = ExprContext::PreSolveCraftTime;
    eenv.craft_time = evalNum("recipe.growth_points / points_per_tick * game.crop_tick_s", cenv, eenv);
    EXPECT_NEAR(eenv.craft_time, 1077.8947, 1e-3);
    cenv.context = ExprContext::PreSolveAfterCraftTime;
    EXPECT_NEAR(evalNum("machine.water_per_s * craft_time", cenv, eenv), 538.947, 1e-2);
    EXPECT_DOUBLE_EQ(evalNum("lookup(\"crop_manager_eut\", t)", cenv, eenv), 32.0);

    // --- Example 10.4: DSP Proliferator (Mk.III, mode = extra) ---
    eenv.params["prolif"] = EvalValue::resource("proliferator-mk3");
    eenv.params["prolif_mode"] = EvalValue::option("prolif_modes", "extra");
    eenv.machine.speed = 1.5;
    eenv.machine.attrs["power"] = EvalValue::number(0.78);
    eenv.recipe.time = 1.0;
    eenv.recipe.input_items_total = 3.0;
    runLets({
        {"on", "is_set(param.prolif)"},
        {"extra", "if(on && param.prolif_mode == \"extra\", param.prolif.extra, 0)"},
        {"spd", "if(on && param.prolif_mode == \"speed\", param.prolif.speedup, 0)"}
    });
    cenv.context = ExprContext::PreSolveCraftTime;
    EXPECT_NEAR(evalNum("recipe.time / (machine.speed * (1 + spd))", cenv, eenv), 2.0 / 3.0, 1e-6);
    cenv.context = ExprContext::PreSolveAfterCraftTime;
    EXPECT_DOUBLE_EQ(evalNum("recipe.input_items_total / param.prolif.charges", cenv, eenv), 0.05);
    EXPECT_NEAR(evalNum("machine.power * if(on, param.prolif.power_mult, 1)", cenv, eenv), 1.95, 1e-6);

    // --- Example 10.5: Factorio 2.0 Assembling machine 3 (4x prod-3, 8 beacons with 2x speed-3) ---
    eenv.machine.speed = 1.25;
    eenv.machine.attrs["power"] = EvalValue::number(0.375);
    eenv.machine.attrs["base_prod"] = EvalValue::number(0.0);
    eenv.recipe.time = 0.5;
    eenv.params["modules"] = EvalValue::slots("modules", {
        "productivity-module-3", "productivity-module-3",
        "productivity-module-3", "productivity-module-3"
    });
    eenv.params["beacon_count"] = EvalValue::number(8.0);
    eenv.params["beacon_modules"] = EvalValue::slots("modules", {
        "speed-module-3", "speed-module-3"
    });
    runLets({
        {"n", "param.beacon_count"},
        {"bmul", "if(n > 0, game.beacon_efficiency * sqrt(n), 0)"},
        {"s", "sum(param.modules.speed) + bmul * sum(param.beacon_modules.speed)"},
        {"p", "machine.base_prod + sum(param.modules.prod)"},
        {"e", "sum(param.modules.consumption) + bmul * sum(param.beacon_modules.consumption)"},
        {"speed", "max(0.2, 1 + s)"},
        {"prod", "1 + clamp(p, 0, 3)"},
        {"energy", "max(0.2, 1 + e)"}
    });
    cenv.context = ExprContext::PreSolveCraftTime;
    EXPECT_NEAR(evalNum("recipe.time / (machine.speed * speed)", cenv, eenv), 0.0861578, 1e-6);
    cenv.context = ExprContext::RecipePortFormula;
    eenv.port.amount = 1.0;
    eenv.port.catalyst = 0.0;
    eenv.port.chance = 1.0;
    EXPECT_NEAR(
        evalNum("(port.catalyst + (port.amount - port.catalyst) * prod) * port.chance", cenv, eenv),
        1.4, 1e-9);
    cenv.context = ExprContext::PreSolveAfterCraftTime;
    EXPECT_NEAR(evalNum("machine.power * energy", cenv, eenv), 3.802386, 1e-5);
}
