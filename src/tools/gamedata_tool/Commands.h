#ifndef GAMEDATA_TOOL_COMMANDS_H
#define GAMEDATA_TOOL_COMMANDS_H

#include "tools/gamedata_tool/Args.h"

#include <ostream>
#include <string>
#include <vector>

namespace gamedata_tool {

// Exit codes per docs/design/gamedata-tool.md §1.3
constexpr int kExitOk = 0;    // OK (warnings allowed unless --werror)
constexpr int kExitError = 1; // Validation / eval errors (or warnings with --werror)
constexpr int kExitFatal = 2; // Fatal package error (F-*) or I/O error
constexpr int kExitUsage = 64; // CLI usage error

int runInit(const Args& args, std::ostream& out, std::ostream& err);
int runValidate(const Args& args, std::ostream& out, std::ostream& err);
int runEval(const Args& args, std::ostream& out, std::ostream& err);
int runStats(const Args& args, std::ostream& out, std::ostream& err);
int runCheckIcons(const Args& args, std::ostream& out, std::ostream& err);
int runPruneIcons(const Args& args, std::ostream& out, std::ostream& err);
int runPackIcons(const Args& args, std::ostream& out, std::ostream& err);

// Top-level entry point suitable for both main() and in-process unit tests.
// `argv` should NOT include argv[0] (program name); it starts with the subcommand or flag.
int runCommand(const std::vector<std::string>& argv, std::ostream& out, std::ostream& err);

} // namespace gamedata_tool

#endif // GAMEDATA_TOOL_COMMANDS_H
