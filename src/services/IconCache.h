#ifndef ICONCACHE_H
#define ICONCACHE_H

#include "core/data/GameData.h"
#include "core/data/GameDataScanner.h"
#include "services/IconLoader.h"
#include "services/IconSourceResolver.h"
#include "services/SettingsManager.h"

#include <imgui.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// GL-backed async icon cache singleton wrapping IconLoader + IconSourceResolver (§4.1).
// Headless-safe: when no OpenGL context is current (e.g., unit tests), GL uploads/frees
// and placeholder creation are safely skipped without crashing.
class IconCache {
public:
    static IconCache& instance();

    void beginFrame(uint64_t frameIndex);
    ImTextureID get(const IconRef& ref);

    // Registers a loaded GamePackage and returns its canonical package_id for fast per-frame lookups.
    std::string registerPackage(const GameData& pkg);

    ImTextureID getResourceIcon(std::string_view packageId, const Resource& res);
    ImTextureID getMachineIcon(std::string_view packageId, const Machine& mach);
    ImTextureID getOptionIcon(
        std::string_view packageId,
        std::string_view optionSetId,
        std::string_view optionKey);

    bool busy() const;

    void invalidate(std::string_view packageId);
    void invalidateAll();

    void updateSettings(
        const IconFallbackSettings& fallback,
        const std::vector<GameDataPackage>& scannedPackages);
    void refreshScannedPackages(const std::vector<GameDataPackage>& scannedPackages);

    void shutdown();

    IconLoaderStats getStats() const;

private:
    IconCache() = default;
    ~IconCache();

    IconCache(const IconCache&) = delete;
    IconCache& operator=(const IconCache&) = delete;

    void ensureInitialized();
    void ensurePlaceholders();

    IconSourceResolver m_resolver;
    std::vector<WorkerPakStreams> m_workerStreams;
    std::unique_ptr<IconLoader> m_loader;

    unsigned int m_loadingPlaceholderTex = 0;
    unsigned int m_missingPlaceholderTex = 0;
    bool m_placeholdersCreated = false;
};

#endif // ICONCACHE_H
