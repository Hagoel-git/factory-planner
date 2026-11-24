#include <gtest/gtest.h>

#include "common/NumberFormat.h"
#include "core/data/GameDataLoader.h"
#include "core/graph/FactoryGraph.h"
#include "services/UndoRedoSystem.h"

#include <limits>

TEST(NumberFormatTest, DisplayPrecisionSerializationHelpers) {
    using numfmt::DisplayPrecision;

    EXPECT_EQ(numfmt::displayPrecisionToString(DisplayPrecision::Auto), "auto");
    EXPECT_EQ(numfmt::displayPrecisionToString(DisplayPrecision::Fixed0), "0");
    EXPECT_EQ(numfmt::displayPrecisionToString(DisplayPrecision::Fixed1), "1");
    EXPECT_EQ(numfmt::displayPrecisionToString(DisplayPrecision::Fixed2), "2");
    EXPECT_EQ(numfmt::displayPrecisionToString(DisplayPrecision::Fixed3), "3");
    EXPECT_EQ(numfmt::displayPrecisionToString(DisplayPrecision::Fixed4), "4");

    EXPECT_EQ(numfmt::displayPrecisionFromString("auto"), DisplayPrecision::Auto);
    EXPECT_EQ(numfmt::displayPrecisionFromString("0"), DisplayPrecision::Fixed0);
    EXPECT_EQ(numfmt::displayPrecisionFromString("2"), DisplayPrecision::Fixed2);
    EXPECT_EQ(numfmt::displayPrecisionFromString("4"), DisplayPrecision::Fixed4);
    EXPECT_EQ(numfmt::displayPrecisionFromString("5"), std::nullopt);
    EXPECT_EQ(numfmt::displayPrecisionFromString("invalid"), std::nullopt);

    EXPECT_EQ(numfmt::displayPrecisionFromInt(-1), DisplayPrecision::Auto);
    EXPECT_EQ(numfmt::displayPrecisionFromInt(0), DisplayPrecision::Fixed0);
    EXPECT_EQ(numfmt::displayPrecisionFromInt(3), DisplayPrecision::Fixed3);
    EXPECT_EQ(numfmt::displayPrecisionFromInt(4), DisplayPrecision::Fixed4);
    EXPECT_EQ(numfmt::displayPrecisionFromInt(-2), std::nullopt);
    EXPECT_EQ(numfmt::displayPrecisionFromInt(5), std::nullopt);
}

TEST(NumberFormatTest, AutoPrecisionTrimsZerosAndExpandsSmallActiveValues) {
    using numfmt::DisplayPrecision;

    // Exact zero & LP solver noise (< 1e-9)
    EXPECT_EQ(numfmt::formatNumber(0.0, DisplayPrecision::Auto), "0");
    EXPECT_EQ(numfmt::formatNumber(-0.0, DisplayPrecision::Auto), "0");
    EXPECT_EQ(numfmt::formatNumber(1e-12, DisplayPrecision::Auto), "0");
    EXPECT_EQ(numfmt::formatNumber(-5e-10, DisplayPrecision::Auto), "0");

    // Moderate & large values (>= 0.01): up to 2 decimals with trailing zeros stripped
    EXPECT_EQ(numfmt::formatNumber(0.01, DisplayPrecision::Auto), "0.01");
    EXPECT_EQ(numfmt::formatNumber(0.25, DisplayPrecision::Auto), "0.25");
    EXPECT_EQ(numfmt::formatNumber(1.0, DisplayPrecision::Auto), "1");
    EXPECT_EQ(numfmt::formatNumber(1.5, DisplayPrecision::Auto), "1.5");
    EXPECT_EQ(numfmt::formatNumber(1.567, DisplayPrecision::Auto), "1.57");
    EXPECT_EQ(numfmt::formatNumber(40.0, DisplayPrecision::Auto), "40");
    EXPECT_EQ(numfmt::formatNumber(1234.5, DisplayPrecision::Auto), "1234.5");
    EXPECT_EQ(numfmt::formatNumber(-40.0, DisplayPrecision::Auto), "-40");

    // Small active values in [1e-4, 0.01): 2 significant digits in fixed decimal (never "0.00")
    EXPECT_EQ(numfmt::formatNumber(0.0041666, DisplayPrecision::Auto), "0.0042");
    EXPECT_EQ(numfmt::formatNumber(0.001, DisplayPrecision::Auto), "0.001");
    EXPECT_EQ(numfmt::formatNumber(0.00999, DisplayPrecision::Auto), "0.01");
    EXPECT_EQ(numfmt::formatNumber(0.00015, DisplayPrecision::Auto), "0.00015");
    EXPECT_EQ(numfmt::formatNumber(0.0001, DisplayPrecision::Auto), "0.0001");
    EXPECT_EQ(numfmt::formatNumber(-0.0042, DisplayPrecision::Auto), "-0.0042");

    // Tiny active values in [1e-9, 1e-4): compact normalized scientific notation
    EXPECT_EQ(numfmt::formatNumber(4.23e-5, DisplayPrecision::Auto), "4.2e-5");
    EXPECT_EQ(numfmt::formatNumber(1e-9, DisplayPrecision::Auto), "1e-9");
    EXPECT_EQ(numfmt::formatNumber(9.96e-5, DisplayPrecision::Auto), "0.0001");

    // Non-finite & extreme magnitudes
    EXPECT_EQ(numfmt::formatNumber(std::numeric_limits<double>::quiet_NaN(), DisplayPrecision::Auto), "NaN");
    EXPECT_EQ(numfmt::formatNumber(std::numeric_limits<double>::infinity(), DisplayPrecision::Auto), "Inf");
    EXPECT_EQ(numfmt::formatNumber(-std::numeric_limits<double>::infinity(), DisplayPrecision::Auto), "-Inf");
    EXPECT_EQ(numfmt::formatNumber(1.5e18, DisplayPrecision::Auto), "1.5e+18");
}

TEST(NumberFormatTest, FixedPrecisionModesRespectDecimalsAndNormalizeNegZero) {
    using numfmt::DisplayPrecision;

    EXPECT_EQ(numfmt::formatNumber(1.5, DisplayPrecision::Fixed0), "2");
    EXPECT_EQ(numfmt::formatNumber(1.5, DisplayPrecision::Fixed1), "1.5");
    EXPECT_EQ(numfmt::formatNumber(1.5, DisplayPrecision::Fixed2), "1.50");
    EXPECT_EQ(numfmt::formatNumber(1.5, DisplayPrecision::Fixed3), "1.500");
    EXPECT_EQ(numfmt::formatNumber(1.5, DisplayPrecision::Fixed4), "1.5000");

    EXPECT_EQ(numfmt::formatNumber(0.004166, DisplayPrecision::Fixed2), "0.00");
    EXPECT_EQ(numfmt::formatNumber(0.004166, DisplayPrecision::Fixed3), "0.004");
    EXPECT_EQ(numfmt::formatNumber(0.004166, DisplayPrecision::Fixed4), "0.0042");

    // Negative values rounding to zero must never emit "-0" or "-0.00"
    EXPECT_EQ(numfmt::formatNumber(-0.0001, DisplayPrecision::Fixed0), "0");
    EXPECT_EQ(numfmt::formatNumber(-0.0001, DisplayPrecision::Fixed2), "0.00");
}

TEST(NumberFormatTest, SiUnitParsingAndAutoScalingWithRoundingCarryOver) {
    using numfmt::DisplayPrecision;
    using numfmt::SiFamily;
    using numfmt::SiUnitSpec;

    // Exact case-sensitive recognition for W/kW/MW/GW/TW and J/kJ/MJ/GJ/TJ
    EXPECT_EQ(numfmt::parseSiUnit("W"), (SiUnitSpec{SiFamily::Power, 0}));
    EXPECT_EQ(numfmt::parseSiUnit("kW"), (SiUnitSpec{SiFamily::Power, 1}));
    EXPECT_EQ(numfmt::parseSiUnit("MW"), (SiUnitSpec{SiFamily::Power, 2}));
    EXPECT_EQ(numfmt::parseSiUnit("GW"), (SiUnitSpec{SiFamily::Power, 3}));
    EXPECT_EQ(numfmt::parseSiUnit("TW"), (SiUnitSpec{SiFamily::Power, 4}));
    EXPECT_EQ(numfmt::parseSiUnit("J"), (SiUnitSpec{SiFamily::Energy, 0}));
    EXPECT_EQ(numfmt::parseSiUnit("MJ"), (SiUnitSpec{SiFamily::Energy, 2}));
    EXPECT_EQ(numfmt::parseSiUnit("TJ"), (SiUnitSpec{SiFamily::Energy, 4}));

    // Non-matching & wrong-case strings
    EXPECT_EQ(numfmt::parseSiUnit("mW"), std::nullopt);
    EXPECT_EQ(numfmt::parseSiUnit("mw"), std::nullopt);
    EXPECT_EQ(numfmt::parseSiUnit("kw"), std::nullopt);
    EXPECT_EQ(numfmt::parseSiUnit("EU/t"), std::nullopt);
    EXPECT_EQ(numfmt::parseSiUnit("kL"), std::nullopt);
    EXPECT_EQ(numfmt::parseSiUnit(""), std::nullopt);

    const SiUnitSpec mw{SiFamily::Power, 2};
    const SiUnitSpec mj{SiFamily::Energy, 2};

    // Zero stays anchored in the declared base unit
    EXPECT_EQ(numfmt::formatSiScaled(0.0, mw, DisplayPrecision::Auto).combined(), "0 MW");
    EXPECT_EQ(numfmt::formatSiScaled(0.0, mw, DisplayPrecision::Fixed2).combined(), "0.00 MW");
    EXPECT_EQ(numfmt::formatSiScaled(1e-12, mw, DisplayPrecision::Auto).combined(), "0 MW");

    // Downscaling to W and kW, upscaling to GW and TW
    EXPECT_EQ(numfmt::formatSiScaled(0.0005, mw, DisplayPrecision::Auto).combined(), "500 W");
    EXPECT_EQ(numfmt::formatSiScaled(0.0005, mw, DisplayPrecision::Fixed2).combined(), "500.00 W");
    EXPECT_EQ(numfmt::formatSiScaled(0.15, mw, DisplayPrecision::Auto).combined(), "150 kW");
    EXPECT_EQ(numfmt::formatSiScaled(-0.09, mw, DisplayPrecision::Auto).combined(), "-90 kW");
    EXPECT_EQ(numfmt::formatSiScaled(40.0, mw, DisplayPrecision::Auto).combined(), "40 MW");
    EXPECT_EQ(numfmt::formatSiScaled(4500.0, mw, DisplayPrecision::Auto).combined(), "4.5 GW");
    EXPECT_EQ(numfmt::formatSiScaled(-4500.0, mw, DisplayPrecision::Fixed2).combined(), "-4.50 GW");
    EXPECT_EQ(numfmt::formatSiScaled(2500000.0, mw, DisplayPrecision::Auto).combined(), "2.5 TW");
    EXPECT_EQ(numfmt::formatSiScaled(0.025, mj, DisplayPrecision::Auto).combined(), "25 kJ");
    EXPECT_EQ(numfmt::formatSiScaled(1500.0, mj, DisplayPrecision::Auto).combined(), "1.5 GJ");

    // Rounding carry-over guard: 999.999 kW at 2 decimals -> 1 MW (never 1000 kW)
    EXPECT_EQ(numfmt::formatSiScaled(0.999999, mw, DisplayPrecision::Auto).combined(), "1 MW");
    EXPECT_EQ(numfmt::formatSiScaled(0.999999, mw, DisplayPrecision::Fixed2).combined(), "1.00 MW");
    EXPECT_EQ(numfmt::formatSiScaled(0.999999, mw, DisplayPrecision::Fixed3).combined(), "999.999 kW");
    EXPECT_EQ(numfmt::formatSiScaled(0.9996, mw, DisplayPrecision::Fixed0).combined(), "1 MW");
    EXPECT_EQ(numfmt::formatSiScaled(-0.999999, mw, DisplayPrecision::Auto).combined(), "-1 MW");
}

TEST(NumberFormatTest, SiSuffixInputParsing) {
    using numfmt::SiFamily;
    using numfmt::SiUnitSpec;

    const SiUnitSpec mw{SiFamily::Power, 2};
    const SiUnitSpec mj{SiFamily::Energy, 2};

    auto expectNearOpt = [](std::optional<double> actual, double expected) {
        ASSERT_TRUE(actual.has_value());
        EXPECT_NEAR(*actual, expected, 1e-9);
    };

    // Bare number uses declared unit (MW)
    expectNearOpt(numfmt::parseSiInput("40", mw), 40.0);
    expectNearOpt(numfmt::parseSiInput("  12.5  ", mw), 12.5);

    // Power suffixes (full and shorthand, case-insensitive)
    expectNearOpt(numfmt::parseSiInput("250 W", mw), 0.00025);
    expectNearOpt(numfmt::parseSiInput("250w", mw), 0.00025);
    expectNearOpt(numfmt::parseSiInput("500k", mw), 0.5);
    expectNearOpt(numfmt::parseSiInput("500 kW", mw), 0.5);
    expectNearOpt(numfmt::parseSiInput("500kw", mw), 0.5);
    expectNearOpt(numfmt::parseSiInput("40 MW", mw), 40.0);
    expectNearOpt(numfmt::parseSiInput("40m", mw), 40.0);
    expectNearOpt(numfmt::parseSiInput("1.2 GW", mw), 1200.0);
    expectNearOpt(numfmt::parseSiInput("1.2g", mw), 1200.0);
    expectNearOpt(numfmt::parseSiInput("1 TW", mw), 1e6);
    expectNearOpt(numfmt::parseSiInput("1.5e2 kW", mw), 0.15);

    // Energy suffixes
    expectNearOpt(numfmt::parseSiInput("500 kJ", mj), 0.5);
    expectNearOpt(numfmt::parseSiInput("2.5 GJ", mj), 2500.0);

    // Reject empty, non-numeric, wrong family, or unrecognized suffixes
    EXPECT_EQ(numfmt::parseSiInput("", mw), std::nullopt);
    EXPECT_EQ(numfmt::parseSiInput("   ", mw), std::nullopt);
    EXPECT_EQ(numfmt::parseSiInput("abc", mw), std::nullopt);
    EXPECT_EQ(numfmt::parseSiInput("500 kJ", mw), std::nullopt);
    EXPECT_EQ(numfmt::parseSiInput("500 kW", mj), std::nullopt);
    EXPECT_EQ(numfmt::parseSiInput("500 foo", mw), std::nullopt);
}

TEST(NumberFormatTest, MetricFormatSpecClassification) {
    EXPECT_TRUE(numfmt::isIntegerFormatSpec("%.0f"));
    EXPECT_TRUE(numfmt::isIntegerFormatSpec("%.0F"));
    EXPECT_TRUE(numfmt::isIntegerFormatSpec("%d"));
    EXPECT_TRUE(numfmt::isIntegerFormatSpec("%i"));
    EXPECT_FALSE(numfmt::isIntegerFormatSpec("%.2f"));
    EXPECT_FALSE(numfmt::isIntegerFormatSpec("%.3g"));
    EXPECT_FALSE(numfmt::isIntegerFormatSpec("%.0f%%"));

    EXPECT_TRUE(numfmt::isPlainNumericFormatSpec(""));
    EXPECT_TRUE(numfmt::isPlainNumericFormatSpec("%.2f"));
    EXPECT_TRUE(numfmt::isPlainNumericFormatSpec("%.3g"));
    EXPECT_TRUE(numfmt::isPlainNumericFormatSpec("%+.1f"));
    EXPECT_FALSE(numfmt::isPlainNumericFormatSpec("~%.2f"));
    EXPECT_FALSE(numfmt::isPlainNumericFormatSpec("%.1f%%"));
}

TEST(NumberFormatTest, FactoryGraphDisplayPrecisionRoundTripAndUndoRedo) {
    GameData gd;
    gd.game.id = "precision_test";
    gd.game.name = "Precision Test";
    gamedata::ensureBuiltInDefaultProfile(gd);

    FactoryGraph graph(gd);
    EXPECT_EQ(graph.getDisplayPrecision(), numfmt::DisplayPrecision::Auto);

    // Undoable SetDisplayPrecisionCommand
    UndoRedoManager undoMgr;
    undoMgr.executeCommand(
        std::make_unique<SetDisplayPrecisionCommand>(
            numfmt::DisplayPrecision::Auto, numfmt::DisplayPrecision::Fixed3),
        graph);
    EXPECT_EQ(graph.getDisplayPrecision(), numfmt::DisplayPrecision::Fixed3);

    // Serialize & deserialize preserves "display_precision": "3"
    nlohmann::json saved = graph.serialize();
    EXPECT_EQ(saved["display_precision"], "3");

    FactoryGraph loaded(gd);
    ASSERT_TRUE(loaded.deserialize(saved, gd));
    EXPECT_EQ(loaded.getDisplayPrecision(), numfmt::DisplayPrecision::Fixed3);

    // Backward compatibility: older .fpp missing "display_precision" defaults to Auto
    nlohmann::json legacy = saved;
    legacy.erase("display_precision");
    ASSERT_TRUE(loaded.deserialize(legacy, gd));
    EXPECT_EQ(loaded.getDisplayPrecision(), numfmt::DisplayPrecision::Auto);

    // Also accepts numeric integer form (e.g. 2) and falls back cleanly on unknown string
    nlohmann::json intForm = saved;
    intForm["display_precision"] = 2;
    ASSERT_TRUE(loaded.deserialize(intForm, gd));
    EXPECT_EQ(loaded.getDisplayPrecision(), numfmt::DisplayPrecision::Fixed2);

    nlohmann::json badForm = saved;
    badForm["display_precision"] = "unknown";
    ASSERT_TRUE(loaded.deserialize(badForm, gd));
    EXPECT_EQ(loaded.getDisplayPrecision(), numfmt::DisplayPrecision::Auto);

    // Undo / Redo on original graph
    undoMgr.undo(graph);
    EXPECT_EQ(graph.getDisplayPrecision(), numfmt::DisplayPrecision::Auto);
    undoMgr.redo(graph);
    EXPECT_EQ(graph.getDisplayPrecision(), numfmt::DisplayPrecision::Fixed3);
}
