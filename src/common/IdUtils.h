#ifndef IDUTILS_H
#define IDUTILS_H

#include <cstdint>
#include "imgui_node_editor.h"

namespace IdUtils
{
    namespace ed = ax::NodeEditor;

    static constexpr uintptr_t NODE_ID_OFFSET = 0x10000000u;
    static constexpr uintptr_t PIN_ID_OFFSET  = 0x20000000u;
    static constexpr uintptr_t LINK_ID_OFFSET = 0x30000000u;

    static inline ed::NodeId toNodeId(uint64_t id) { return ed::NodeId(reinterpret_cast<void*>((uintptr_t)id + NODE_ID_OFFSET)); }
    static inline ed::PinId  toPinId(uint64_t id)  { return ed::PinId(reinterpret_cast<void*>((uintptr_t)id + PIN_ID_OFFSET)); }
    static inline ed::LinkId toLinkId(uint64_t id) { return ed::LinkId(reinterpret_cast<void*>((uintptr_t)id + LINK_ID_OFFSET)); }

    static inline uint64_t fromNodeId(ed::NodeId id) { return (uint64_t)(reinterpret_cast<uintptr_t>(id.AsPointer()) - NODE_ID_OFFSET); }
    static inline uint64_t fromPinId(ed::PinId id)   { return (uint64_t)(reinterpret_cast<uintptr_t>(id.AsPointer()) - PIN_ID_OFFSET); }
    static inline uint64_t fromLinkId(ed::LinkId id) { return (uint64_t)(reinterpret_cast<uintptr_t>(id.AsPointer()) - LINK_ID_OFFSET); }
}
#endif //IDUTILS_H
