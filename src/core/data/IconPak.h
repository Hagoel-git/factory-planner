#ifndef ICONPAK_H
#define ICONPAK_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gamedata {

inline constexpr char kIconPakMagic[8] = {'F', 'P', 'I', 'C', 'O', 'N', 'S', '\0'};
inline constexpr uint32_t kIconPakVersion = 1;
inline constexpr size_t kIconPakHeaderSize = 32;

struct IconPakEntry {
    std::string key;       // "resources/<id>" | "machines/<id>" | "options/<set>/<key>"
    std::string name_slug; // slugify(display name) for resources/machines; empty for options
    uint64_t offset = 0;
    uint32_t size = 0;
    uint16_t width = 0;
    uint16_t height = 0;
};

class IconPakReader {
public:
    static bool open(
        const std::filesystem::path& path,
        IconPakReader& outReader,
        std::string* outError = nullptr);

    const std::filesystem::path& path() const { return m_path; }
    uint32_t version() const { return m_version; }
    size_t size() const { return m_entries.size(); }
    bool empty() const { return m_entries.empty(); }

    const std::vector<IconPakEntry>& entries() const { return m_entries; }

    const IconPakEntry* find(std::string_view key) const;
    bool contains(std::string_view key) const;

    // Opens a binary file stream to the pak file for per-thread on-demand blob reads.
    std::ifstream openStream() const;

    // Reads a single entry's raw PNG bytes. Returns std::nullopt if the entry is
    // unreadable, truncated, or (when validatePng is true) has corrupt PNG structure.
    std::optional<std::vector<uint8_t>> readBlob(
        const IconPakEntry& entry,
        bool validatePng = true) const;

    std::optional<std::vector<uint8_t>> readBlob(
        std::string_view key,
        bool validatePng = true) const;

    std::optional<std::vector<uint8_t>> readBlob(
        const IconPakEntry& entry,
        std::ifstream& stream,
        bool validatePng = true) const;

private:
    std::filesystem::path m_path;
    uint32_t m_version = 0;
    uint64_t m_indexOffset = 0;
    uint64_t m_indexSize = 0;
    std::vector<IconPakEntry> m_entries;
    std::unordered_map<std::string, size_t> m_keyIndex;
};

struct IconPakInput {
    std::string key;
    std::string name_slug;
    // Provide either source_path (streamed from disk) or inline_bytes (in-memory).
    std::filesystem::path source_path;
    std::vector<uint8_t> inline_bytes;
    uint16_t width = 0;
    uint16_t height = 0;
};

class IconPakWriter {
public:
    // Writes a deterministic, atomic v1 icons.pak file sorted by key bytes.
    static bool write(
        const std::filesystem::path& outputPath,
        std::vector<IconPakInput> entries,
        std::string* outError = nullptr);
};

} // namespace gamedata

#endif // ICONPAK_H
