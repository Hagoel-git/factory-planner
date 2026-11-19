#include "core/data/IconPak.h"

#include "common/FilesystemUtils.h"
#include "core/data/PngInfo.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <system_error>

namespace gamedata {

namespace {

uint16_t readU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t readU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t readU64LE(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<uint64_t>(p[i]) << (8 * i);
    }
    return v;
}

void appendU16LE(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}

void appendU32LE(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}

void appendU64LE(std::vector<uint8_t>& buf, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        buf.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
    }
}

void writeHeaderBytes(
    std::array<uint8_t, kIconPakHeaderSize>& hdr,
    uint32_t version,
    uint32_t count,
    uint64_t indexOffset,
    uint64_t indexSize) {
    std::memcpy(hdr.data(), kIconPakMagic, 8);
    auto putU32 = [&](size_t off, uint32_t v) {
        hdr[off + 0] = static_cast<uint8_t>(v & 0xFFu);
        hdr[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
        hdr[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
        hdr[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    };
    auto putU64 = [&](size_t off, uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            hdr[off + static_cast<size_t>(i)] = static_cast<uint8_t>((v >> (8 * i)) & 0xFFu);
        }
    };
    putU32(8, version);
    putU32(12, count);
    putU64(16, indexOffset);
    putU64(24, indexSize);
}

} // namespace

bool IconPakReader::open(
    const std::filesystem::path& path,
    IconPakReader& outReader,
    std::string* outError) {
    if (outError) outError->clear();
    outReader = IconPakReader{};

    auto fail = [&](std::string msg) -> bool {
        if (outError) *outError = std::move(msg);
        outReader = IconPakReader{};
        return false;
    };

    std::error_code ec;
    const uint64_t fileSize = std::filesystem::file_size(path, ec);
    if (ec) {
        return fail("cannot stat '" + pathToUtf8(path) + "': " + ec.message());
    }
    if (fileSize < kIconPakHeaderSize) {
        return fail("truncated icons.pak header (" + std::to_string(fileSize) + " bytes)");
    }

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        return fail("cannot open '" + pathToUtf8(path) + "' for reading");
    }

    std::array<uint8_t, kIconPakHeaderSize> hdr{};
    ifs.read(reinterpret_cast<char*>(hdr.data()), static_cast<std::streamsize>(hdr.size()));
    if (ifs.gcount() != static_cast<std::streamsize>(hdr.size())) {
        return fail("truncated icons.pak header");
    }

    if (std::memcmp(hdr.data(), kIconPakMagic, 8) != 0) {
        return fail("invalid icons.pak magic");
    }

    const uint32_t version = readU32LE(hdr.data() + 8);
    if (version != kIconPakVersion) {
        return fail("unsupported icons.pak version " + std::to_string(version));
    }

    const uint32_t count = readU32LE(hdr.data() + 12);
    const uint64_t indexOffset = readU64LE(hdr.data() + 16);
    const uint64_t indexSize = readU64LE(hdr.data() + 24);

    if (indexOffset < kIconPakHeaderSize || indexOffset > fileSize) {
        return fail("icons.pak index offset out of bounds");
    }
    if (indexSize > fileSize - indexOffset) {
        return fail("icons.pak index extends past EOF");
    }

    constexpr uint64_t kMinEntryBytes = 20; // u16 + u16 + u64 + u32 + u16 + u16
    if (static_cast<uint64_t>(count) > indexSize / kMinEntryBytes) {
        return fail("icons.pak index truncated for declared entry count");
    }

    if (indexSize > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return fail("icons.pak index size exceeds addressable memory");
    }

    std::vector<uint8_t> indexBuf(static_cast<size_t>(indexSize));
    if (indexSize > 0) {
        ifs.seekg(static_cast<std::streamoff>(indexOffset), std::ios::beg);
        if (!ifs) {
            return fail("failed seeking to icons.pak index");
        }
        ifs.read(reinterpret_cast<char*>(indexBuf.data()), static_cast<std::streamsize>(indexSize));
        if (ifs.gcount() != static_cast<std::streamsize>(indexSize)) {
            return fail("truncated icons.pak index");
        }
    }

    IconPakReader reader;
    reader.m_path = path;
    reader.m_version = version;
    reader.m_indexOffset = indexOffset;
    reader.m_indexSize = indexSize;
    reader.m_entries.reserve(count);
    reader.m_keyIndex.reserve(count);

    size_t cursor = 0;
    const size_t totalIndexBytes = indexBuf.size();
    for (uint32_t i = 0; i < count; ++i) {
        if (totalIndexBytes - cursor < 4) {
            return fail("truncated icons.pak index entry header");
        }
        const uint16_t keyLen = readU16LE(indexBuf.data() + cursor);
        cursor += 2;
        if (keyLen == 0) {
            return fail("icons.pak contains empty key at entry " + std::to_string(i));
        }
        if (totalIndexBytes - cursor < static_cast<size_t>(keyLen) + 2) {
            return fail("truncated icons.pak index entry key");
        }
        std::string key(reinterpret_cast<const char*>(indexBuf.data() + cursor), keyLen);
        cursor += keyLen;

        const uint16_t nameLen = readU16LE(indexBuf.data() + cursor);
        cursor += 2;
        if (totalIndexBytes - cursor < static_cast<size_t>(nameLen) + 16) {
            return fail("truncated icons.pak index entry payload");
        }
        std::string nameSlug(reinterpret_cast<const char*>(indexBuf.data() + cursor), nameLen);
        cursor += nameLen;

        const uint64_t offset = readU64LE(indexBuf.data() + cursor);
        cursor += 8;
        const uint32_t size = readU32LE(indexBuf.data() + cursor);
        cursor += 4;
        const uint16_t w = readU16LE(indexBuf.data() + cursor);
        cursor += 2;
        const uint16_t h = readU16LE(indexBuf.data() + cursor);
        cursor += 2;

        if (offset < kIconPakHeaderSize || offset > indexOffset ||
            static_cast<uint64_t>(size) > indexOffset - offset) {
            return fail("icons.pak entry '" + key + "' blob extends beyond data section");
        }

        if (!reader.m_keyIndex.emplace(key, reader.m_entries.size()).second) {
            return fail("duplicate key '" + key + "' in icons.pak");
        }

        IconPakEntry entry;
        entry.key = std::move(key);
        entry.name_slug = std::move(nameSlug);
        entry.offset = offset;
        entry.size = size;
        entry.width = w;
        entry.height = h;
        reader.m_entries.push_back(std::move(entry));
    }

    if (cursor != totalIndexBytes) {
        return fail("icons.pak index size mismatch (trailing bytes)");
    }

    outReader = std::move(reader);
    return true;
}

const IconPakEntry* IconPakReader::find(std::string_view key) const {
    auto it = m_keyIndex.find(std::string(key));
    if (it == m_keyIndex.end()) {
        return nullptr;
    }
    return &m_entries[it->second];
}

bool IconPakReader::contains(std::string_view key) const {
    return find(key) != nullptr;
}

std::ifstream IconPakReader::openStream() const {
    return std::ifstream(m_path, std::ios::binary);
}

std::optional<std::vector<uint8_t>> IconPakReader::readBlob(
    const IconPakEntry& entry,
    bool validatePng) const {
    std::ifstream ifs = openStream();
    if (!ifs) {
        return std::nullopt;
    }
    return readBlob(entry, ifs, validatePng);
}

std::optional<std::vector<uint8_t>> IconPakReader::readBlob(
    std::string_view key,
    bool validatePng) const {
    const IconPakEntry* entry = find(key);
    if (entry == nullptr) {
        return std::nullopt;
    }
    return readBlob(*entry, validatePng);
}

std::optional<std::vector<uint8_t>> IconPakReader::readBlob(
    const IconPakEntry& entry,
    std::ifstream& stream,
    bool validatePng) const {
    if (entry.size == 0 || entry.offset < kIconPakHeaderSize ||
        entry.offset > m_indexOffset ||
        static_cast<uint64_t>(entry.size) > m_indexOffset - entry.offset) {
        return std::nullopt;
    }

    if (!stream.is_open()) {
        return std::nullopt;
    }
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
    if (!stream) {
        return std::nullopt;
    }

    std::vector<uint8_t> bytes(entry.size);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(entry.size));
    if (stream.gcount() != static_cast<std::streamsize>(entry.size)) {
        return std::nullopt;
    }

    if (validatePng) {
        PngInfo info;
        if (!validatePngStructure(bytes.data(), bytes.size(), &info)) {
            return std::nullopt;
        }
        if (info.width != entry.width || info.height != entry.height) {
            return std::nullopt;
        }
    }

    return bytes;
}

bool IconPakWriter::write(
    const std::filesystem::path& outputPath,
    std::vector<IconPakInput> entries,
    std::string* outError) {
    if (outError) outError->clear();

    if (outputPath.empty()) {
        if (outError) *outError = "output path is empty";
        return false;
    }
    if (entries.size() > std::numeric_limits<uint32_t>::max()) {
        if (outError) *outError = "too many entries for icons.pak";
        return false;
    }

    for (const auto& e : entries) {
        if (e.key.empty()) {
            if (outError) *outError = "icons.pak entry key cannot be empty";
            return false;
        }
        if (e.key.size() > std::numeric_limits<uint16_t>::max()) {
            if (outError) *outError = "icons.pak entry key exceeds 65535 bytes: " + e.key;
            return false;
        }
        if (e.name_slug.size() > std::numeric_limits<uint16_t>::max()) {
            if (outError) *outError = "icons.pak name_slug exceeds 65535 bytes for key: " + e.key;
            return false;
        }
        if (e.inline_bytes.empty() && e.source_path.empty()) {
            if (outError) *outError = "icons.pak entry '" + e.key + "' has no data source";
            return false;
        }
    }

    // Deterministic order: sorted by key bytes
    std::sort(entries.begin(), entries.end(), [](const IconPakInput& a, const IconPakInput& b) {
        return a.key < b.key;
    });

    for (size_t i = 1; i < entries.size(); ++i) {
        if (entries[i].key == entries[i - 1].key) {
            if (outError) *outError = "duplicate icons.pak key '" + entries[i].key + "'";
            return false;
        }
    }

    std::error_code ec;
    const std::filesystem::path parent = outputPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            if (outError) {
                *outError = "cannot create directory '" + pathToUtf8(parent) + "': " + ec.message();
            }
            return false;
        }
    }

    std::filesystem::path tmp = outputPath;
    tmp += ".tmp";

    auto cleanupTmp = [&]() {
        std::error_code rmEc;
        std::filesystem::remove(tmp, rmEc);
    };

    {
        std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            if (outError) {
                *outError = "cannot open temporary file '" + pathToUtf8(tmp) + "' for writing";
            }
            return false;
        }

        // Write placeholder 32-byte header
        std::array<uint8_t, kIconPakHeaderSize> hdr{};
        ofs.write(reinterpret_cast<const char*>(hdr.data()), static_cast<std::streamsize>(hdr.size()));
        if (!ofs.good()) {
            ofs.close();
            cleanupTmp();
            if (outError) *outError = "failed writing icons.pak header";
            return false;
        }

        uint64_t currentOffset = kIconPakHeaderSize;
        std::vector<IconPakEntry> writtenIndex;
        writtenIndex.reserve(entries.size());
        std::vector<uint8_t> fileBuf;

        for (const auto& item : entries) {
            const uint8_t* blobPtr = nullptr;
            size_t blobSize = 0;

            if (!item.inline_bytes.empty()) {
                blobPtr = item.inline_bytes.data();
                blobSize = item.inline_bytes.size();
            } else {
                const uint64_t fsize = std::filesystem::file_size(item.source_path, ec);
                if (ec) {
                    ofs.close();
                    cleanupTmp();
                    if (outError) {
                        *outError = "cannot stat '" + pathToUtf8(item.source_path) + "': " + ec.message();
                    }
                    return false;
                }
                if (fsize > std::numeric_limits<uint32_t>::max()) {
                    ofs.close();
                    cleanupTmp();
                    if (outError) {
                        *outError = "icon file exceeds 4 GiB limit: " + pathToUtf8(item.source_path);
                    }
                    return false;
                }
                fileBuf.resize(static_cast<size_t>(fsize));
                std::ifstream src(item.source_path, std::ios::binary);
                if (!src) {
                    ofs.close();
                    cleanupTmp();
                    if (outError) {
                        *outError = "cannot open '" + pathToUtf8(item.source_path) + "' for reading";
                    }
                    return false;
                }
                if (fsize > 0) {
                    src.read(reinterpret_cast<char*>(fileBuf.data()), static_cast<std::streamsize>(fsize));
                    if (src.gcount() != static_cast<std::streamsize>(fsize)) {
                        ofs.close();
                        cleanupTmp();
                        if (outError) {
                            *outError = "failed reading '" + pathToUtf8(item.source_path) + "'";
                        }
                        return false;
                    }
                }
                blobPtr = fileBuf.data();
                blobSize = fileBuf.size();
            }

            if (blobSize > std::numeric_limits<uint32_t>::max()) {
                ofs.close();
                cleanupTmp();
                if (outError) *outError = "icon blob exceeds 4 GiB limit for key '" + item.key + "'";
                return false;
            }

            if (blobSize > 0) {
                ofs.write(reinterpret_cast<const char*>(blobPtr), static_cast<std::streamsize>(blobSize));
                if (!ofs.good()) {
                    ofs.close();
                    cleanupTmp();
                    if (outError) *outError = "failed writing icon blob for key '" + item.key + "'";
                    return false;
                }
            }

            IconPakEntry idxEntry;
            idxEntry.key = item.key;
            idxEntry.name_slug = item.name_slug;
            idxEntry.offset = currentOffset;
            idxEntry.size = static_cast<uint32_t>(blobSize);
            idxEntry.width = item.width;
            idxEntry.height = item.height;
            writtenIndex.push_back(std::move(idxEntry));

            currentOffset += blobSize;
        }

        const uint64_t indexOffset = currentOffset;
        std::vector<uint8_t> indexBuf;
        for (const auto& idxEntry : writtenIndex) {
            appendU16LE(indexBuf, static_cast<uint16_t>(idxEntry.key.size()));
            indexBuf.insert(indexBuf.end(), idxEntry.key.begin(), idxEntry.key.end());
            appendU16LE(indexBuf, static_cast<uint16_t>(idxEntry.name_slug.size()));
            indexBuf.insert(indexBuf.end(), idxEntry.name_slug.begin(), idxEntry.name_slug.end());
            appendU64LE(indexBuf, idxEntry.offset);
            appendU32LE(indexBuf, idxEntry.size);
            appendU16LE(indexBuf, idxEntry.width);
            appendU16LE(indexBuf, idxEntry.height);
        }

        if (!indexBuf.empty()) {
            ofs.write(reinterpret_cast<const char*>(indexBuf.data()), static_cast<std::streamsize>(indexBuf.size()));
            if (!ofs.good()) {
                ofs.close();
                cleanupTmp();
                if (outError) *outError = "failed writing icons.pak index";
                return false;
            }
        }

        const uint64_t indexSize = indexBuf.size();
        writeHeaderBytes(
            hdr,
            kIconPakVersion,
            static_cast<uint32_t>(writtenIndex.size()),
            indexOffset,
            indexSize);

        ofs.seekp(0, std::ios::beg);
        ofs.write(reinterpret_cast<const char*>(hdr.data()), static_cast<std::streamsize>(hdr.size()));
        ofs.flush();
        if (!ofs.good()) {
            ofs.close();
            cleanupTmp();
            if (outError) *outError = "failed finalizing icons.pak header";
            return false;
        }
        ofs.close();
        if (!ofs.good()) {
            cleanupTmp();
            if (outError) *outError = "failed closing temporary icons.pak file";
            return false;
        }
    }

    std::filesystem::rename(tmp, outputPath, ec);
    if (ec) {
        cleanupTmp();
        if (outError) {
            *outError = "failed renaming '" + pathToUtf8(tmp) + "' to '" + pathToUtf8(outputPath) + "': " + ec.message();
        }
        return false;
    }

    return true;
}

} // namespace gamedata
