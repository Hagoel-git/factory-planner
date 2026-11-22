#ifndef GAMEDATA_TOOL_GUIDE_EMBED_H
#define GAMEDATA_TOOL_GUIDE_EMBED_H

#include <string_view>

namespace gamedata_tool {

// Returns the embedded contents of docs/authoring/pack-author-prompt.md (LF line endings).
std::string_view embeddedGuidePrompt();

// Returns the embedded contents of docs/gamedata-v4.md (LF line endings).
std::string_view embeddedGuideReference();

} // namespace gamedata_tool

#endif // GAMEDATA_TOOL_GUIDE_EMBED_H
