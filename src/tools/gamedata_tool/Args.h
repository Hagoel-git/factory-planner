#ifndef GAMEDATA_TOOL_ARGS_H
#define GAMEDATA_TOOL_ARGS_H

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace gamedata_tool {

struct Args {
    std::string command; // "init", "validate", "eval", "stats", "check-icons", "prune-icons", "pack-icons", "guide"
    bool show_help = false;
    bool show_version = false;

    // Primary positional argument (<dir> for init, <pkg> for validate/eval/stats)
    std::string target;

    // Common flags
    bool json_output = false;

    // init flags
    std::string init_id;
    std::string init_name;
    std::string init_data_version = "0.1.0";
    std::string init_time = "s";
    std::optional<double> init_tps;
    std::string init_rate = "min";
    bool init_icons = false;
    bool init_force = false;

    // validate flags
    bool werror = false;
    bool no_icons = false;
    bool ignore_errors = false;
    size_t max_groups = 50;
    size_t examples = 3;
    bool show_all = false;
    bool timing = false;

    // eval flags
    std::string recipe;
    std::string machine;
    bool all_machines = false;
    std::vector<std::string> params;
    std::vector<std::string> globals;
    std::string sweep_param;
    std::optional<double> machines;
    std::string rate; // empty -> package default_rate
    std::string cases_path;

    // stats flags
    int repeat = 1;

    // icon command flags (check-icons, prune-icons, pack-icons)
    std::string from_dir;
    bool apply = false;
    std::string out_pak;
    bool verify = false;
    std::string list_pak;

    // guide topic ("prompt", "reference", or empty for topic list)
    std::string guide_topic;
};

struct ParseResult {
    bool ok = true;
    int exit_code = 0;
    std::string error;
    Args args;
};

// Parses CLI arguments (excluding program name argv[0]).
ParseResult parseArgs(const std::vector<std::string>& argv);

void printTopLevelHelp(std::ostream& out);
void printCommandHelp(const std::string& command, std::ostream& out);

} // namespace gamedata_tool

#endif // GAMEDATA_TOOL_ARGS_H
