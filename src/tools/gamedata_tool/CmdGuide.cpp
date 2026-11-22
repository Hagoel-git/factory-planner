#include "tools/gamedata_tool/Commands.h"
#include "tools/gamedata_tool/GuideEmbed.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace gamedata_tool {

namespace {

using json = nlohmann::ordered_json;

} // namespace

int runGuide(const Args& args, std::ostream& out, std::ostream& err) {
    if (args.guide_topic.empty()) {
        if (args.json_output) {
            json doc;
            doc["topics"] = json::array({
                {
                    {"name", "prompt"},
                    {"description", "Interactive AI pack-authoring prompt (docs/authoring/pack-author-prompt.md)"}
                },
                {
                    {"name", "reference"},
                    {"description", "GameData v4 author reference (docs/gamedata-v4.md)"}
                }
            });
            out << doc.dump(2) << "\n";
            return kExitOk;
        }

        out << "Available embedded authoring guides:\n"
            << "  prompt     Interactive AI pack-authoring prompt (docs/authoring/pack-author-prompt.md)\n"
            << "  reference  GameData v4 author reference (docs/gamedata-v4.md)\n"
            << "\n"
            << "Usage:\n"
            << "  gamedata_tool guide prompt\n"
            << "  gamedata_tool guide reference\n";
        return kExitOk;
    }

    std::string_view content;
    if (args.guide_topic == "prompt") {
        content = embeddedGuidePrompt();
    } else if (args.guide_topic == "reference") {
        content = embeddedGuideReference();
    } else {
        err << "error: unknown guide topic '" << args.guide_topic
            << "' (expected 'prompt' or 'reference')\n";
        return kExitUsage;
    }

    if (args.json_output) {
        json doc;
        doc["topic"] = args.guide_topic;
        doc["content"] = std::string(content);
        out << doc.dump(2) << "\n";
        return kExitOk;
    }

    out << content;
    return kExitOk;
}

} // namespace gamedata_tool
