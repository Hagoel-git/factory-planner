#include "tools/gamedata_tool/Args.h"
#include "tools/gamedata_tool/Commands.h"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <string_view>

namespace gamedata_tool {

namespace {

ParseResult usageError(std::string msg) {
    ParseResult r;
    r.ok = false;
    r.exit_code = kExitUsage;
    r.error = std::move(msg);
    return r;
}

bool parseSizeT(const std::string& s, size_t& out) {
    if (s.empty() || s[0] == '-') return false;
    char* end = nullptr;
    errno = 0;
    unsigned long long val = std::strtoull(s.c_str(), &end, 10);
    if (errno != 0 || end == nullptr || *end != '\0') return false;
    out = static_cast<size_t>(val);
    return true;
}

bool parsePositiveInt(const std::string& s, int& out) {
    if (s.empty() || s[0] == '-') return false;
    char* end = nullptr;
    errno = 0;
    long val = std::strtol(s.c_str(), &end, 10);
    if (errno != 0 || end == nullptr || *end != '\0') return false;
    if (val <= 0 || val > std::numeric_limits<int>::max()) return false;
    out = static_cast<int>(val);
    return true;
}

bool parseFiniteDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    double val = std::strtod(s.c_str(), &end);
    if (errno != 0 || end == nullptr || *end != '\0' || !std::isfinite(val)) return false;
    out = val;
    return true;
}

// Splits "--flag=value" into (flag, optional value).
std::pair<std::string_view, std::optional<std::string_view>> splitFlag(std::string_view tok) {
    if (tok.size() >= 2 && tok[0] == '-' && tok[1] == '-') {
        size_t eq = tok.find('=');
        if (eq != std::string_view::npos) {
            return {tok.substr(0, eq), tok.substr(eq + 1)};
        }
    }
    return {tok, std::nullopt};
}

bool consumeValue(
    const std::vector<std::string>& argv,
    size_t& idx,
    const std::optional<std::string_view>& inlineVal,
    std::string& outVal) {
    if (inlineVal.has_value()) {
        outVal = std::string(*inlineVal);
        return true;
    }
    if (idx + 1 >= argv.size()) {
        return false;
    }
    outVal = argv[++idx];
    return true;
}

} // namespace

void printTopLevelHelp(std::ostream& out) {
    out << "Usage: gamedata_tool <command> [options]\n"
        << "\n"
        << "Commands:\n"
        << "  init <dir>         Create a minimal valid GameData v4 package scaffold\n"
        << "  validate <pkg>     Load, compile, validate, and group diagnostics\n"
        << "  eval <pkg>         Evaluate a recipe/machine or run regression --cases\n"
        << "  stats <pkg>        Print entity counts, distributions, and load timing\n"
        << "  check-icons <pkg>  Check missing, unreferenced, invalid, or stale icons\n"
        << "  prune-icons <pkg>  List or delete unreferenced loose PNG icons\n"
        << "  pack-icons <pkg>   Build or inspect an icons.pak container\n"
        << "  guide [topic]      Print embedded authoring docs (prompt | reference)\n"
        << "\n"
        << "Options:\n"
        << "  -h, --help         Show this help (or command help: gamedata_tool <cmd> --help)\n"
        << "  -V, --version      Print tool version and schema version\n";
}

void printCommandHelp(const std::string& command, std::ostream& out) {
    if (command == "init") {
        out << "Usage: gamedata_tool init <dir> [options]\n"
            << "  --id <slug>              Game id (default: slugified directory name)\n"
            << "  --name <name>            Display name (default: game id)\n"
            << "  --data-version <ver>     Data version (default: 0.1.0)\n"
            << "  --time s|tick            Recipe time unit (default: s)\n"
            << "  --tps <N>                Ticks per second (required/default 20 for tick)\n"
            << "  --rate tick|s|min|h      Default display rate unit (default: min)\n"
            << "  --icons                  Write placeholder square PNG icons\n"
            << "  --force                  Allow writing into a non-empty directory\n"
            << "  --json                   Emit JSON output on stdout\n";
    } else if (command == "validate") {
        out << "Usage: gamedata_tool validate <pkg> [options]\n"
            << "  --json                   Emit JSON report (report_version: 1) on stdout\n"
            << "  --werror                 Treat warnings as errors (notes never fail)\n"
            << "  --no-icons               Skip icon checks\n"
            << "  --ignore-errors          Report degraded load (\"Load anyway\")\n"
            << "  --max-groups <N>         Maximum groups to output (default: 50)\n"
            << "  --examples <N>           Maximum examples per group (default: 3)\n"
            << "  --all                    Include flat ungrouped diagnostics list\n"
            << "  --timing                 Show load timing in human output\n";
    } else if (command == "eval") {
        out << "Usage: gamedata_tool eval <pkg> [options]\n"
            << "  --recipe <id>            Recipe id (required unless --cases is given)\n"
            << "  --machine <id>           Machine id (default: first compatible machine)\n"
            << "  --all-machines           Evaluate every compatible machine\n"
            << "  --param <k=v>            Node parameter override (JSON literal, repeatable)\n"
            << "  --global <k=v>           Project global override (JSON literal, repeatable)\n"
            << "  --sweep <param>          Sweep enum options or int range (max 32 values)\n"
            << "  --machines <N>           Evaluate post-solve metrics at N machines\n"
            << "  --rate tick|s|min|h      Display rate unit (default: package default_rate)\n"
            << "  --cases <file|dir>       Run JSON regression cases file or directory\n"
            << "  --json                   Emit JSON output on stdout\n";
    } else if (command == "stats") {
        out << "Usage: gamedata_tool stats <pkg> [options]\n"
            << "  --repeat <N>             Load N times and report median timing (default: 1)\n"
            << "  --timing                 Highlight timing summary\n"
            << "  --ignore-errors          Allow degraded package loads\n"
            << "  --json                   Emit JSON output on stdout\n";
    } else if (command == "check-icons") {
        out << "Usage: gamedata_tool check-icons <pkg> [options]\n"
            << "  --from <dir>             Loose icon directory (default: <icon_root>)\n"
            << "  --werror                 Fail on missing, unreferenced, case mismatch, or stale pak\n"
            << "  --json                   Emit JSON output on stdout\n";
    } else if (command == "prune-icons") {
        out << "Usage: gamedata_tool prune-icons <pkg> [options]\n"
            << "  --from <dir>             Loose icon directory (default: <icon_root>)\n"
            << "  --apply                  Actually delete unreferenced PNGs (dry run by default)\n"
            << "  --json                   Emit JSON output on stdout\n";
    } else if (command == "pack-icons") {
        out << "Usage: gamedata_tool pack-icons <pkg> [options]\n"
            << "   or: gamedata_tool pack-icons --list <file> [--json]\n"
            << "  --from <dir>             Loose icon directory (default: <icon_root>)\n"
            << "  -o, --output <file>      Output pak path (default: <package_root>/icons.pak)\n"
            << "  --verify                 Read packed file back and compare byte-for-byte\n"
            << "  --list <file>            Print the index of an existing icons.pak\n"
            << "  --json                   Emit JSON output on stdout\n";
    } else if (command == "guide") {
        out << "Usage: gamedata_tool guide [prompt|reference] [options]\n"
            << "  prompt                   Print the embedded AI pack-authoring prompt\n"
            << "  reference                Print the embedded GameData v4 author reference\n"
            << "  --json                   Emit JSON output on stdout\n";
    } else {
        printTopLevelHelp(out);
    }
}

ParseResult parseArgs(const std::vector<std::string>& argv) {
    ParseResult res;
    if (argv.empty()) {
        return usageError("missing command (run 'gamedata_tool --help' for usage)");
    }

    const std::string& first = argv[0];
    if (first == "--help" || first == "-h") {
        res.args.show_help = true;
        return res;
    }
    if (first == "--version" || first == "-V") {
        if (argv.size() > 1) {
            return usageError("unexpected argument after --version: '" + argv[1] + "'");
        }
        res.args.show_version = true;
        return res;
    }

    if (first != "init" && first != "validate" && first != "eval" && first != "stats" &&
        first != "check-icons" && first != "prune-icons" && first != "pack-icons" &&
        first != "guide") {
        return usageError(
            "unknown command '" + first +
            "' (expected init, validate, eval, stats, check-icons, prune-icons, pack-icons, or guide)");
    }

    Args& a = res.args;
    a.command = first;

    bool afterDoubleDash = false;
    std::vector<std::string> positionals;

    for (size_t i = 1; i < argv.size(); ++i) {
        const std::string& tok = argv[i];
        if (!afterDoubleDash && tok == "--") {
            afterDoubleDash = true;
            continue;
        }
        if (afterDoubleDash || tok.empty() || tok[0] != '-') {
            positionals.push_back(tok);
            continue;
        }

        auto [flag, inlineVal] = splitFlag(tok);
        auto requireNoVal = [&]() -> bool {
            return !inlineVal.has_value();
        };

        if (flag == "--help" || flag == "-h") {
            if (!requireNoVal()) return usageError("flag '--help' does not take a value");
            a.show_help = true;
            return res;
        }

        if (flag == "--json") {
            if (!requireNoVal()) return usageError("flag '--json' does not take a value");
            a.json_output = true;
            continue;
        }

        if (a.command == "init") {
            if (flag == "--id") {
                if (!consumeValue(argv, i, inlineVal, a.init_id)) {
                    return usageError("missing value for '--id'");
                }
            } else if (flag == "--name") {
                if (!consumeValue(argv, i, inlineVal, a.init_name)) {
                    return usageError("missing value for '--name'");
                }
            } else if (flag == "--data-version") {
                if (!consumeValue(argv, i, inlineVal, a.init_data_version)) {
                    return usageError("missing value for '--data-version'");
                }
            } else if (flag == "--time") {
                if (!consumeValue(argv, i, inlineVal, a.init_time)) {
                    return usageError("missing value for '--time'");
                }
                if (a.init_time != "s" && a.init_time != "tick") {
                    return usageError("invalid --time '" + a.init_time + "' (expected 's' or 'tick')");
                }
            } else if (flag == "--tps") {
                std::string raw;
                double tps = 0.0;
                if (!consumeValue(argv, i, inlineVal, raw) || !parseFiniteDouble(raw, tps) || tps <= 0.0) {
                    return usageError("invalid --tps value (must be a finite number > 0)");
                }
                a.init_tps = tps;
            } else if (flag == "--rate") {
                if (!consumeValue(argv, i, inlineVal, a.init_rate)) {
                    return usageError("missing value for '--rate'");
                }
                if (a.init_rate != "tick" && a.init_rate != "s" &&
                    a.init_rate != "min" && a.init_rate != "h") {
                    return usageError("invalid --rate '" + a.init_rate + "' (expected tick, s, min, or h)");
                }
            } else if (flag == "--icons") {
                if (!requireNoVal()) return usageError("flag '--icons' does not take a value");
                a.init_icons = true;
            } else if (flag == "--force") {
                if (!requireNoVal()) return usageError("flag '--force' does not take a value");
                a.init_force = true;
            } else {
                return usageError("unknown flag '" + std::string(flag) + "' for 'init'");
            }
        } else if (a.command == "validate") {
            if (flag == "--werror") {
                if (!requireNoVal()) return usageError("flag '--werror' does not take a value");
                a.werror = true;
            } else if (flag == "--no-icons") {
                if (!requireNoVal()) return usageError("flag '--no-icons' does not take a value");
                a.no_icons = true;
            } else if (flag == "--ignore-errors") {
                if (!requireNoVal()) return usageError("flag '--ignore-errors' does not take a value");
                a.ignore_errors = true;
            } else if (flag == "--all") {
                if (!requireNoVal()) return usageError("flag '--all' does not take a value");
                a.show_all = true;
            } else if (flag == "--timing") {
                if (!requireNoVal()) return usageError("flag '--timing' does not take a value");
                a.timing = true;
            } else if (flag == "--max-groups") {
                std::string raw;
                size_t val = 0;
                if (!consumeValue(argv, i, inlineVal, raw) || !parseSizeT(raw, val) || val == 0) {
                    return usageError("invalid --max-groups value (expected positive integer)");
                }
                a.max_groups = val;
            } else if (flag == "--examples") {
                std::string raw;
                size_t val = 0;
                if (!consumeValue(argv, i, inlineVal, raw) || !parseSizeT(raw, val)) {
                    return usageError("invalid --examples value (expected non-negative integer)");
                }
                a.examples = val;
            } else {
                return usageError("unknown flag '" + std::string(flag) + "' for 'validate'");
            }
        } else if (a.command == "eval") {
            if (flag == "--recipe") {
                if (!consumeValue(argv, i, inlineVal, a.recipe) || a.recipe.empty()) {
                    return usageError("missing value for '--recipe'");
                }
            } else if (flag == "--machine") {
                if (!consumeValue(argv, i, inlineVal, a.machine) || a.machine.empty()) {
                    return usageError("missing value for '--machine'");
                }
            } else if (flag == "--all-machines") {
                if (!requireNoVal()) return usageError("flag '--all-machines' does not take a value");
                a.all_machines = true;
            } else if (flag == "--param") {
                std::string raw;
                if (!consumeValue(argv, i, inlineVal, raw)) {
                    return usageError("missing value for '--param'");
                }
                a.params.push_back(std::move(raw));
            } else if (flag == "--global") {
                std::string raw;
                if (!consumeValue(argv, i, inlineVal, raw)) {
                    return usageError("missing value for '--global'");
                }
                a.globals.push_back(std::move(raw));
            } else if (flag == "--sweep") {
                if (!consumeValue(argv, i, inlineVal, a.sweep_param) || a.sweep_param.empty()) {
                    return usageError("missing value for '--sweep'");
                }
            } else if (flag == "--machines") {
                std::string raw;
                double val = 0.0;
                if (!consumeValue(argv, i, inlineVal, raw) || !parseFiniteDouble(raw, val) || val < 0.0) {
                    return usageError("invalid --machines value (expected finite number >= 0)");
                }
                a.machines = val;
            } else if (flag == "--rate") {
                if (!consumeValue(argv, i, inlineVal, a.rate)) {
                    return usageError("missing value for '--rate'");
                }
                if (a.rate != "tick" && a.rate != "s" && a.rate != "min" && a.rate != "h") {
                    return usageError("invalid --rate '" + a.rate + "' (expected tick, s, min, or h)");
                }
            } else if (flag == "--cases") {
                if (!consumeValue(argv, i, inlineVal, a.cases_path) || a.cases_path.empty()) {
                    return usageError("missing value for '--cases'");
                }
            } else {
                return usageError("unknown flag '" + std::string(flag) + "' for 'eval'");
            }
        } else if (a.command == "stats") {
            if (flag == "--repeat") {
                std::string raw;
                int val = 1;
                if (!consumeValue(argv, i, inlineVal, raw) || !parsePositiveInt(raw, val)) {
                    return usageError("invalid --repeat value (expected positive integer)");
                }
                a.repeat = val;
            } else if (flag == "--timing") {
                if (!requireNoVal()) return usageError("flag '--timing' does not take a value");
                a.timing = true;
            } else if (flag == "--ignore-errors") {
                if (!requireNoVal()) return usageError("flag '--ignore-errors' does not take a value");
                a.ignore_errors = true;
            } else {
                return usageError("unknown flag '" + std::string(flag) + "' for 'stats'");
            }
        } else if (a.command == "check-icons") {
            if (flag == "--from") {
                if (!consumeValue(argv, i, inlineVal, a.from_dir) || a.from_dir.empty()) {
                    return usageError("missing value for '--from'");
                }
            } else if (flag == "--werror") {
                if (!requireNoVal()) return usageError("flag '--werror' does not take a value");
                a.werror = true;
            } else {
                return usageError("unknown flag '" + std::string(flag) + "' for 'check-icons'");
            }
        } else if (a.command == "prune-icons") {
            if (flag == "--from") {
                if (!consumeValue(argv, i, inlineVal, a.from_dir) || a.from_dir.empty()) {
                    return usageError("missing value for '--from'");
                }
            } else if (flag == "--apply") {
                if (!requireNoVal()) return usageError("flag '--apply' does not take a value");
                a.apply = true;
            } else {
                return usageError("unknown flag '" + std::string(flag) + "' for 'prune-icons'");
            }
        } else if (a.command == "pack-icons") {
            if (flag == "--from") {
                if (!consumeValue(argv, i, inlineVal, a.from_dir) || a.from_dir.empty()) {
                    return usageError("missing value for '--from'");
                }
            } else if (flag == "-o" || flag == "--output") {
                if (!consumeValue(argv, i, inlineVal, a.out_pak) || a.out_pak.empty()) {
                    return usageError("missing value for '" + std::string(flag) + "'");
                }
            } else if (flag == "--verify") {
                if (!requireNoVal()) return usageError("flag '--verify' does not take a value");
                a.verify = true;
            } else if (flag == "--list") {
                if (!consumeValue(argv, i, inlineVal, a.list_pak) || a.list_pak.empty()) {
                    return usageError("missing value for '--list'");
                }
            } else {
                return usageError("unknown flag '" + std::string(flag) + "' for 'pack-icons'");
            }
        } else if (a.command == "guide") {
            return usageError("unknown flag '" + std::string(flag) + "' for 'guide'");
        }
    }

    if (a.command == "guide") {
        if (positionals.size() > 1) {
            return usageError("unexpected positional argument '" + positionals[1] + "'");
        }
        if (positionals.size() == 1) {
            const std::string& topic = positionals.front();
            if (topic != "prompt" && topic != "reference") {
                return usageError(
                    "unknown guide topic '" + topic + "' (expected 'prompt' or 'reference')");
            }
            a.guide_topic = topic;
        }
    } else if (a.command == "pack-icons" && !a.list_pak.empty()) {
        if (!positionals.empty()) {
            return usageError("--list cannot be combined with a package argument");
        }
        if (!a.from_dir.empty() || !a.out_pak.empty() || a.verify) {
            return usageError("--list cannot be combined with --from, -o, or --verify");
        }
    } else {
        if (positionals.empty()) {
            return usageError("missing required path argument for '" + a.command + "'");
        }
        if (positionals.size() > 1) {
            return usageError("unexpected positional argument '" + positionals[1] + "'");
        }
        a.target = positionals.front();
    }

    if (a.command == "eval") {
        if (!a.cases_path.empty()) {
            if (!a.recipe.empty() || !a.machine.empty() || a.all_machines || !a.sweep_param.empty()) {
                return usageError("--cases cannot be combined with --recipe, --machine, --all-machines, or --sweep");
            }
        } else {
            if (a.recipe.empty()) {
                return usageError("'eval' requires --recipe <id> (or --cases <file|dir>)");
            }
            if (!a.machine.empty() && a.all_machines) {
                return usageError("--machine and --all-machines are mutually exclusive");
            }
        }
    }

    return res;
}

int runCommand(const std::vector<std::string>& argv, std::ostream& out, std::ostream& err) {
    ParseResult parsed = parseArgs(argv);
    if (!parsed.ok) {
        err << "error: " << parsed.error << "\n";
        return parsed.exit_code;
    }

    const Args& args = parsed.args;
    if (args.show_version) {
#ifndef GAMEDATA_TOOL_VERSION
#define GAMEDATA_TOOL_VERSION "0.1"
#endif
        out << "gamedata_tool " << GAMEDATA_TOOL_VERSION << " (schema_version 4)\n";
        return kExitOk;
    }

    if (args.show_help) {
        if (args.command.empty()) {
            printTopLevelHelp(out);
        } else {
            printCommandHelp(args.command, out);
        }
        return kExitOk;
    }

    if (args.command == "init") {
        return runInit(args, out, err);
    }
    if (args.command == "validate") {
        return runValidate(args, out, err);
    }
    if (args.command == "eval") {
        return runEval(args, out, err);
    }
    if (args.command == "stats") {
        return runStats(args, out, err);
    }
    if (args.command == "check-icons") {
        return runCheckIcons(args, out, err);
    }
    if (args.command == "prune-icons") {
        return runPruneIcons(args, out, err);
    }
    if (args.command == "pack-icons") {
        return runPackIcons(args, out, err);
    }
    if (args.command == "guide") {
        return runGuide(args, out, err);
    }

    err << "error: unknown command '" << args.command << "'\n";
    return kExitUsage;
}

} // namespace gamedata_tool
