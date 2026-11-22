#include "services/IconSourceResolver.h"

#include "common/FilesystemUtils.h"
#include "common/StringUtils.h"

#include <absl/log/log.h>

#include <system_error>
#include <unordered_set>

namespace {

bool pathsReferToSameLocation(const std::filesystem::path& a, const std::filesystem::path& b) {
    if (a.empty() || b.empty()) return false;
    std::error_code ec;
    if (std::filesystem::equivalent(a, b, ec)) {
        return true;
    }
    return a.lexically_normal() == b.lexically_normal();
}

std::optional<std::vector<uint8_t>> readFileBytes(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        return std::nullopt;
    }
    const std::streamoff endPos = ifs.tellg();
    if (endPos <= 0) {
        return std::nullopt;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(endPos));
    ifs.seekg(0, std::ios::beg);
    if (!ifs) {
        return std::nullopt;
    }
    ifs.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (ifs.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return std::nullopt;
    }
    return bytes;
}

void buildNameSlugMaps(
    const gamedata::IconPakReader& reader,
    std::unordered_map<std::string, const gamedata::IconPakEntry*>& resourceMap,
    std::unordered_map<std::string, const gamedata::IconPakEntry*>& machineMap) {
    resourceMap.clear();
    machineMap.clear();

    constexpr std::string_view kResPrefix = "resources/";
    constexpr std::string_view kMachPrefix = "machines/";

    auto insertSmallestKey = [](
        std::unordered_map<std::string, const gamedata::IconPakEntry*>& map,
        const gamedata::IconPakEntry& entry) {
        if (entry.name_slug.empty()) {
            return;
        }
        auto it = map.find(entry.name_slug);
        if (it == map.end() || entry.key < it->second->key) {
            map[entry.name_slug] = &entry;
        }
    };

    for (const auto& entry : reader.entries()) {
        if (entry.name_slug.empty()) {
            continue;
        }
        if (entry.key.size() > kResPrefix.size() &&
            entry.key.compare(0, kResPrefix.size(), kResPrefix) == 0) {
            insertSmallestKey(resourceMap, entry);
        } else if (entry.key.size() > kMachPrefix.size() &&
                   entry.key.compare(0, kMachPrefix.size(), kMachPrefix) == 0) {
            insertSmallestKey(machineMap, entry);
        }
    }
}

std::optional<std::vector<uint8_t>> readPakEntryWithStream(
    const gamedata::IconPakReader& reader,
    const gamedata::IconPakEntry& entry,
    WorkerPakStreams& streams,
    uint64_t streamEpoch) {
    std::ifstream* stream = streams.getOrOpen(reader.path(), streamEpoch);
    if (stream == nullptr) {
        return std::nullopt;
    }
    return reader.readBlob(entry, *stream, /*validatePng=*/true);
}

} // namespace

std::string makeIconKey(IconKind kind, std::string_view id) {
    std::string out;
    switch (kind) {
        case IconKind::Resource:
            out.reserve(10 + id.size());
            out.append("resources/");
            out.append(id);
            break;
        case IconKind::Machine:
            out.reserve(9 + id.size());
            out.append("machines/");
            out.append(id);
            break;
        case IconKind::Option:
            out.reserve(8 + id.size());
            out.append("options/");
            out.append(id);
            break;
    }
    return out;
}

std::string canonicalPackageId(
    const std::filesystem::path& packageRoot,
    const std::filesystem::path& iconRoot,
    std::string_view gameId,
    std::string_view dataVersion) {
    std::error_code ec;
    if (!packageRoot.empty()) {
        auto canon = std::filesystem::weakly_canonical(packageRoot, ec);
        if (!ec && !canon.empty()) {
            return pathToUtf8(canon.lexically_normal());
        }
        return pathToUtf8(packageRoot.lexically_normal());
    }
    if (!iconRoot.empty()) {
        auto canon = std::filesystem::weakly_canonical(iconRoot, ec);
        if (!ec && !canon.empty()) {
            return "iconroot:" + pathToUtf8(canon.lexically_normal());
        }
        return "iconroot:" + pathToUtf8(iconRoot.lexically_normal());
    }
    if (!gameId.empty()) {
        std::string key = "mem:";
        key.append(gameId);
        if (!dataVersion.empty()) {
            key.push_back(':');
            key.append(dataVersion);
        }
        return key;
    }
    return {};
}

IconPackageDescriptor descriptorFromScannedPackage(const GameDataPackage& pkg) {
    IconPackageDescriptor desc;
    desc.game_id = pkg.uuid;
    desc.package_root = pkg.dataFilePath.parent_path();
    desc.icon_root = pkg.iconRootPath;
    desc.package_id = canonicalPackageId(desc.package_root, desc.icon_root, desc.game_id, pkg.dataVersion);
    return desc;
}

std::ifstream* WorkerPakStreams::getOrOpen(const std::filesystem::path& pakPath, uint64_t currentEpoch) {
    if (epoch != currentEpoch) {
        streams.clear();
        epoch = currentEpoch;
    }
    const std::string key = pathToUtf8(pakPath);
    auto it = streams.find(key);
    if (it != streams.end()) {
        if (it->second.is_open()) {
            return &it->second;
        }
        streams.erase(it);
    }
    std::ifstream ifs(pakPath, std::ios::binary);
    if (!ifs.is_open()) {
        return nullptr;
    }
    auto [inserted, _] = streams.emplace(key, std::move(ifs));
    return &inserted->second;
}

void WorkerPakStreams::clear() {
    streams.clear();
    epoch = 0;
}

std::string IconSourceResolver::registerPackage(const IconPackageDescriptor& rawDesc) {
    IconPackageDescriptor desc = rawDesc;
    if (desc.package_id.empty()) {
        desc.package_id = canonicalPackageId(desc.package_root, desc.icon_root, desc.game_id);
    }
    if (desc.package_id.empty()) {
        return {};
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_registeredPackages.find(desc.package_id);
    const bool changed = (it == m_registeredPackages.end()) ||
                         (it->second.game_id != desc.game_id) ||
                         (it->second.package_root != desc.package_root) ||
                         (it->second.icon_root != desc.icon_root);
    if (!changed) {
        return desc.package_id;
    }

    m_registeredPackages[desc.package_id] = desc;
    auto state = std::make_shared<PackageSourceState>();
    state->desc = desc;
    m_packageStates[desc.package_id] = std::move(state);
    m_epoch.fetch_add(1, std::memory_order_release);
    return desc.package_id;
}

bool IconSourceResolver::setFallbackSettings(const IconFallbackSettings& fallback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_fallback == fallback) {
        return false;
    }
    m_fallback = fallback;
    m_epoch.fetch_add(1, std::memory_order_release);
    return true;
}

void IconSourceResolver::setScannedPackages(const std::vector<IconPackageDescriptor>& scanned) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_scannedPackages.clear();
    m_scannedPackages.reserve(scanned.size());

    std::unordered_set<std::string> seenIds;
    seenIds.reserve(scanned.size());
    for (const auto& raw : scanned) {
        IconPackageDescriptor desc = raw;
        if (desc.package_id.empty()) {
            desc.package_id = canonicalPackageId(desc.package_root, desc.icon_root, desc.game_id);
        }
        if (desc.package_id.empty() || !seenIds.insert(desc.package_id).second) {
            continue;
        }
        m_scannedPackages.push_back(std::move(desc));
    }

    // Rebuild package states so rescans pick up newly created or removed paks/icon folders.
    m_packageStates.clear();
    for (const auto& desc : m_scannedPackages) {
        auto state = std::make_shared<PackageSourceState>();
        state->desc = desc;
        m_packageStates[desc.package_id] = std::move(state);
    }
    for (const auto& [pkgId, desc] : m_registeredPackages) {
        auto state = std::make_shared<PackageSourceState>();
        state->desc = desc;
        m_packageStates[pkgId] = std::move(state);
    }

    m_epoch.fetch_add(1, std::memory_order_release);
}

void IconSourceResolver::setScannedPackages(const std::vector<GameDataPackage>& scanned) {
    std::vector<IconPackageDescriptor> descriptors;
    descriptors.reserve(scanned.size());
    for (const auto& pkg : scanned) {
        descriptors.push_back(descriptorFromScannedPackage(pkg));
    }
    setScannedPackages(descriptors);
}

void IconSourceResolver::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fallback = {};
    m_registeredPackages.clear();
    m_scannedPackages.clear();
    m_packageStates.clear();
    m_epoch.fetch_add(1, std::memory_order_release);
}

IconSourceResolver::PackageSnapshot IconSourceResolver::getOrInitPackageSnapshot(
    const std::string& packageId,
    uint64_t& outEpoch) const {
    PackageSnapshot snap;
    IconPackageDescriptor descToInit;
    std::shared_ptr<PackageSourceState> state;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        outEpoch = m_epoch.load(std::memory_order_acquire);
        auto it = m_packageStates.find(packageId);
        if (it == m_packageStates.end() || !it->second) {
            snap.iconless = true;
            return snap;
        }
        state = it->second;
        if (state->initialized) {
            snap.iconless = state->iconless;
            snap.has_loose_dir = state->has_loose_dir;
            snap.loose_dir = state->loose_dir;
            snap.root_pak = state->root_pak ? state->root_pak->reader : nullptr;
            snap.icon_pak = state->icon_pak ? state->icon_pak->reader : nullptr;
            return snap;
        }
        descToInit = state->desc;
    }

    // Perform single-check source discovery & pak index reading outside the mutex.
    std::error_code ec;
    std::filesystem::path rootPakPath;
    bool hasRootPakFile = false;
    if (!descToInit.package_root.empty()) {
        rootPakPath = descToInit.package_root / "icons.pak";
        hasRootPakFile = std::filesystem::is_regular_file(rootPakPath, ec);
    }

    std::filesystem::path iconPakPath;
    bool hasIconPakFile = false;
    bool hasLooseDir = false;
    std::filesystem::path looseDir;
    if (!descToInit.icon_root.empty()) {
        looseDir = descToInit.icon_root;
        ec.clear();
        hasLooseDir = std::filesystem::is_directory(looseDir, ec);

        std::filesystem::path candidateIconPak = descToInit.icon_root / "icons.pak";
        if (!pathsReferToSameLocation(candidateIconPak, rootPakPath)) {
            iconPakPath = std::move(candidateIconPak);
            ec.clear();
            hasIconPakFile = std::filesystem::is_regular_file(iconPakPath, ec);
        }
    }

    std::shared_ptr<PakWithNameMaps> openedRootPak;
    if (hasRootPakFile) {
        auto reader = std::make_shared<gamedata::IconPakReader>();
        std::string err;
        m_pakOpenCount.fetch_add(1, std::memory_order_relaxed);
        if (gamedata::IconPakReader::open(rootPakPath, *reader, &err)) {
            openedRootPak = std::make_shared<PakWithNameMaps>();
            openedRootPak->reader = std::move(reader);
        } else {
            LOG(WARNING) << "Failed to open icons.pak at " << rootPakPath << ": " << err;
        }
    }

    std::shared_ptr<PakWithNameMaps> openedIconPak;
    if (hasIconPakFile) {
        auto reader = std::make_shared<gamedata::IconPakReader>();
        std::string err;
        m_pakOpenCount.fetch_add(1, std::memory_order_relaxed);
        if (gamedata::IconPakReader::open(iconPakPath, *reader, &err)) {
            openedIconPak = std::make_shared<PakWithNameMaps>();
            openedIconPak->reader = std::move(reader);
        } else {
            LOG(WARNING) << "Failed to open icons.pak at " << iconPakPath << ": " << err;
        }
    }

    const bool iconless = !openedRootPak && !openedIconPak && !hasLooseDir;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!state->initialized) {
        state->root_pak = std::move(openedRootPak);
        state->icon_pak = std::move(openedIconPak);
        state->has_loose_dir = hasLooseDir;
        state->loose_dir = std::move(looseDir);
        state->iconless = iconless;
        state->initialized = true;
        if (iconless) {
            VLOG(1) << "IconSourceResolver: package '" << packageId << "' entered no-icons mode.";
        }
    }

    snap.iconless = state->iconless;
    snap.has_loose_dir = state->has_loose_dir;
    snap.loose_dir = state->loose_dir;
    snap.root_pak = state->root_pak ? state->root_pak->reader : nullptr;
    snap.icon_pak = state->icon_pak ? state->icon_pak->reader : nullptr;
    return snap;
}

std::optional<std::vector<uint8_t>> IconSourceResolver::readExactFromSnapshot(
    const PackageSnapshot& snap,
    std::string_view key,
    WorkerPakStreams& streams,
    uint64_t streamEpoch) const {
    if (snap.iconless) {
        return std::nullopt;
    }

    // T8 order:
    // 1. <package_root>/icons.pak
    if (snap.root_pak) {
        if (const gamedata::IconPakEntry* entry = snap.root_pak->find(key)) {
            if (auto bytes = readPakEntryWithStream(*snap.root_pak, *entry, streams, streamEpoch)) {
                return bytes;
            }
        }
    }

    // 2. <icon_root>/icons.pak
    if (snap.icon_pak) {
        if (const gamedata::IconPakEntry* entry = snap.icon_pak->find(key)) {
            if (auto bytes = readPakEntryWithStream(*snap.icon_pak, *entry, streams, streamEpoch)) {
                return bytes;
            }
        }
    }

    // 3. Loose <icon_root>/<key>.png
    if (snap.has_loose_dir && !snap.loose_dir.empty()) {
        std::string relPng(key);
        relPng.append(".png");
        const std::filesystem::path fullPath = snap.loose_dir / pathFromUtf8(relPng);
        m_looseStatCount.fetch_add(1, std::memory_order_relaxed);
        std::error_code ec;
        if (std::filesystem::is_regular_file(fullPath, ec)) {
            if (auto bytes = readFileBytes(fullPath)) {
                return bytes;
            }
        }
    }

    return std::nullopt;
}

std::optional<std::vector<uint8_t>> IconSourceResolver::readByNameSlugFromPackage(
    const std::string& packageId,
    IconKind kind,
    std::string_view nameSlug,
    WorkerPakStreams& streams,
    uint64_t streamEpoch) const {
    if (kind == IconKind::Option || nameSlug.empty()) {
        return std::nullopt;
    }

    // Lookup matching entry in root_pak then icon_pak, lazily building name_slug maps once per pak.
    struct PakMatch {
        std::shared_ptr<const gamedata::IconPakReader> reader;
        const gamedata::IconPakEntry* entry = nullptr;
    };
    std::vector<PakMatch> matches;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_packageStates.find(packageId);
        if (it == m_packageStates.end() || !it->second || !it->second->initialized || it->second->iconless) {
            return std::nullopt;
        }
        auto checkPak = [&](const std::shared_ptr<PakWithNameMaps>& pak) {
            if (!pak || !pak->reader) {
                return;
            }
            if (!pak->name_maps_built) {
                buildNameSlugMaps(*pak->reader, pak->resource_by_name_slug, pak->machine_by_name_slug);
                pak->name_maps_built = true;
            }
            const auto& map = (kind == IconKind::Resource)
                ? pak->resource_by_name_slug
                : pak->machine_by_name_slug;
            auto mit = map.find(std::string(nameSlug));
            if (mit != map.end() && mit->second != nullptr) {
                matches.push_back(PakMatch{pak->reader, mit->second});
            }
        };
        checkPak(it->second->root_pak);
        checkPak(it->second->icon_pak);
    }

    for (const auto& m : matches) {
        if (auto bytes = readPakEntryWithStream(*m.reader, *m.entry, streams, streamEpoch)) {
            return bytes;
        }
    }
    return std::nullopt;
}

std::vector<std::string> IconSourceResolver::buildFallbackCandidatesLocked(
    const std::string& currentPackageId) const {
    std::vector<std::string> candidates;
    if (!m_fallback.enabled) {
        return candidates;
    }

    std::string currentGameId;
    if (auto it = m_packageStates.find(currentPackageId); it != m_packageStates.end() && it->second) {
        currentGameId = it->second->desc.game_id;
    }

    std::unordered_set<std::string> visited;
    visited.insert(currentPackageId);

    // 1. Other packages of the same game first (in scan order).
    if (!currentGameId.empty()) {
        for (const auto& pkg : m_scannedPackages) {
            if (pkg.game_id == currentGameId && visited.insert(pkg.package_id).second) {
                candidates.push_back(pkg.package_id);
            }
        }
    }

    // 2. Games in user-configured priority order (each game's packages in scan order).
    for (const auto& gid : m_fallback.order) {
        if (gid.empty()) continue;
        for (const auto& pkg : m_scannedPackages) {
            if (pkg.game_id == gid && visited.insert(pkg.package_id).second) {
                candidates.push_back(pkg.package_id);
            }
        }
    }

    return candidates;
}

std::optional<ResolvedIconBytes> IconSourceResolver::resolve(
    const IconRef& ref,
    WorkerPakStreams& streams) const {
    if (ref.id.empty()) {
        return std::nullopt;
    }

    const std::string iconKey = makeIconKey(ref.kind, ref.id);

    // 1. Primary package lookup (T8 order).
    uint64_t snapEpoch = 0;
    if (!ref.package_id.empty()) {
        PackageSnapshot primarySnap = getOrInitPackageSnapshot(ref.package_id, snapEpoch);
        if (auto bytes = readExactFromSnapshot(primarySnap, iconKey, streams, snapEpoch)) {
            ResolvedIconBytes out;
            out.bytes = std::move(*bytes);
            out.from_fallback = false;
            out.source_package_id = ref.package_id;
            return out;
        }
    }

    // 2. Cross-pack fallback (§4.3), pack-first: for each candidate pack, try exact key then name_slug.
    std::vector<std::string> candidates;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_fallback.enabled) {
            return std::nullopt;
        }
        candidates = buildFallbackCandidatesLocked(ref.package_id);
    }

    if (candidates.empty()) {
        return std::nullopt;
    }

    const std::string nameSlug =
        (ref.kind != IconKind::Option && !ref.name.empty()) ? slugify(ref.name) : std::string{};

    for (const auto& candPkgId : candidates) {
        uint64_t candEpoch = 0;
        PackageSnapshot candSnap = getOrInitPackageSnapshot(candPkgId, candEpoch);
        if (candSnap.iconless) {
            continue;
        }

        // 2a. Exact key match in candidate pack (T8 order).
        if (auto bytes = readExactFromSnapshot(candSnap, iconKey, streams, candEpoch)) {
            ResolvedIconBytes out;
            out.bytes = std::move(*bytes);
            out.from_fallback = true;
            out.source_package_id = candPkgId;
            return out;
        }

        // 2b. Display-name slug match (same kind only, pak index only; skipped for options).
        if (ref.kind != IconKind::Option && !nameSlug.empty()) {
            if (auto bytes = readByNameSlugFromPackage(candPkgId, ref.kind, nameSlug, streams, candEpoch)) {
                ResolvedIconBytes out;
                out.bytes = std::move(*bytes);
                out.from_fallback = true;
                out.source_package_id = candPkgId;
                return out;
            }
        }
    }

    return std::nullopt;
}

bool IconSourceResolver::isPackageIconless(std::string_view packageId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_packageStates.find(std::string(packageId));
    return (it != m_packageStates.end() && it->second && it->second->initialized && it->second->iconless);
}
