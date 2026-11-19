#include "tools/gamedata_tool/Args.h"
#include "tools/gamedata_tool/Commands.h"

#include <absl/log/globals.h>
#include <absl/log/initialize.h>

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    absl::InitializeLog();
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kWarning);

    std::vector<std::string> args;
    if (argc > 1) {
        args.reserve(static_cast<size_t>(argc - 1));
        for (int i = 1; i < argc; ++i) {
            args.emplace_back(argv[i]);
        }
    }

    return gamedata_tool::runCommand(args, std::cout, std::cerr);
}
