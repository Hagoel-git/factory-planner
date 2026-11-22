#ifndef ICONSOURCERESOLVER_H
#define ICONSOURCERESOLVER_H

#include "core/data/GameDataScanner.h"
#include "core/data/IconPak.h"
#include "services/SettingsManager.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

enum class IconKind {
    Resource,
    Machine,
    Option
};

struct IconRef {
    std::string package_id; // canonical package root (or logical ID in tests)
    IconKind kind = IconKind::Resource;
    std::string id;         // resource id, machine id, or "<option_set>/<option_key>"
    std::string name;       // display name for fallback slugify matching (empty for options)
};

struct IconPackageDescriptor {
    std::string package_id;
    std::string game_id;
    std::filesystem::path package_root;
    std::filesystem::path icon_root;
};

struct ResolvedIconBytes {
    std::vector<uint8_t> bytes;
    bool from_fallback = false;
    std::string source_package_id;
};

// Returns "resources/<id>", "machines/<id>", or "options/<set>/<key>".
std::string makeIconKey(IconKind kind, std::string_view id);

// Returns a canonical package key suitable for sharing icon cache entries across tabs.
std::string canonicalPackageId(
    const std::filesystem::path& packageRoot,
    const std::filesystem::path& iconRoot = {},
    std::string_view gameId = {},
    std::string_view dataVersion = {});

// Builds an IconPackageDescriptor from a scanned GameDataPackage.
IconPackageDescriptor descriptorFromScannedPackage(const GameDataPackage& pkg);

// Per-worker file stream cache for reading blobs from icons.pak without reopening files.
// Owned on each worker thread's stack (not thread_local) so handles close deterministically on shutdown.
struct WorkerPakStreams {
    uint64_t epoch = 0;
    std::unordered_map<std::string, std::ifstream> streams;

    std::ifstream* getOrOpen(const std::filesystem::path& pakPath, uint64_t currentEpoch);
    void clear();
};

// GL-free, thread-safe icon source resolver implementing:
// - T8 resolution order: <package_root>/icons.pak -> <icon_root>/icons.pak -> loose <icon_root>/<key>.png
// - Single-check "no icons" mode (zero per-key filesystem lookups when no sources exist)
// - Cross-pack icon fallback (§4.3) with lazy candidate pak opening and (kind, name_slug) maps
class IconSourceResolver {
public:
    IconSourceResolver() = default;

    // Registers (or updates) an active package descriptor. Returns its package_id.
    std::string registerPackage(const IconPackageDescriptor& desc);

    // Updates fallback settings (enabled flag and game-ID priority list).
    // Returns true if the settings changed.
    bool setFallbackSettings(const IconFallbackSettings& fallback);

    // Replaces the list of scanned packages (preserving scan order for fallback candidates).
    void setScannedPackages(const std::vector<IconPackageDescriptor>& scanned);
    void setScannedPackages(const std::vector<GameDataPackage>& scanned);

    // Clears all registered packages, scanned packages, and cached pak readers.
    void clear();

    // Resolves icon PNG bytes for `ref` on a worker thread (no GL/stb calls).
    std::optional<ResolvedIconBytes> resolve(const IconRef& ref, WorkerPakStreams& streams) const;

    // Diagnostic/test inspection helpers.
    bool isPackageIconless(std::string_view packageId) const;
    uint64_t looseStatCount() const { return m_looseStatCount.load(std::memory_order_relaxed); }
    uint64_t pakOpenCount() const { return m_pakOpenCount.load(std::memory_order_relaxed); }
    uint64_t epoch() const { return m_epoch.load(std::memory_order_acquire); }

private:
    struct PakWithNameMaps {
        std::shared_ptr<const gamedata::IconPakReader> reader;
        bool name_maps_built = false;
        std::unordered_map<std::string, const gamedata::IconPakEntry*> resource_by_name_slug;
        std::unordered_map<std::string, const gamedata::IconPakEntry*> machine_by_name_slug;
    };

    struct PackageSourceState {
        IconPackageDescriptor desc;
        bool initialized = false;
        bool iconless = false;
        bool has_loose_dir = false;
        std::filesystem::path loose_dir;
        std::shared_ptr<PakWithNameMaps> root_pak;
        std::shared_ptr<PakWithNameMaps> icon_pak;
    };

    struct PackageSnapshot {
        bool iconless = true;
        bool has_loose_dir = false;
        std::filesystem::path loose_dir;
        std::shared_ptr<const gamedata::IconPakReader> root_pak;
        std::shared_ptr<const gamedata::IconPakReader> icon_pak;
    };

    PackageSnapshot getOrInitPackageSnapshot(const std::string& packageId, uint64_t& outEpoch) const;
    std::optional<std::vector<uint8_t>> readExactFromSnapshot(
        const PackageSnapshot& snap,
        std::string_view key,
        WorkerPakStreams& streams,
        uint64_t streamEpoch) const;
    std::optional<std::vector<uint8_t>> readByNameSlugFromPackage(
        const std::string& packageId,
        IconKind kind,
        std::string_view nameSlug,
        WorkerPakStreams& streams,
        uint64_t streamEpoch) const;
    std::vector<std::string> buildFallbackCandidatesLocked(const std::string& currentPackageId) const;

    mutable std::mutex m_mutex;
    std::atomic<uint64_t> m_epoch{1};
    IconFallbackSettings m_fallback;
    std::unordered_map<std::string, IconPackageDescriptor> m_registeredPackages;
    std::vector<IconPackageDescriptor> m_scannedPackages;
    std::unordered_map<std::string, std::shared_ptr<PackageSourceState>> m_packageStates;

    mutable std::atomic<uint64_t> m_looseStatCount{0};
    mutable std::atomic<uint64_t> m_pakOpenCount{0};
};

#endif // ICONSOURCERESOLVER_H
