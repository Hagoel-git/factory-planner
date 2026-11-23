#include "services/IconCache.h"

#include "common/TextureUtils.h"

#include <absl/log/log.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <filesystem>
#include <thread>

IconCache& IconCache::instance() {
    static IconCache inst;
    return inst;
}

IconCache::~IconCache() {
    shutdown();
}

void IconCache::ensurePlaceholders() {
    if (m_placeholdersCreated) {
        return;
    }
    if (glfwGetCurrentContext() == nullptr) {
        return;
    }

    // Loading placeholder: 1x1 neutral translucent box (no flash of unknown.png).
    const unsigned char neutralPixel[4] = {128, 128, 128, 40};
    TextureUtils::uploadRgbaTexture(neutralPixel, 1, 1, &m_loadingPlaceholderTex);

    // Missing placeholder: assets/icons/unknown.png (magenta 1x1 fallback).
    const std::filesystem::path unknownPath =
        SettingsManager::instance().getSettings().executablePath / "assets" / "icons" / "unknown.png";
    if (!TextureUtils::loadTextureFromFile(unknownPath.string().c_str(), &m_missingPlaceholderTex, nullptr, nullptr)) {
        LOG(WARNING) << "Failed to load missing icon placeholder from: " << unknownPath;
        const unsigned char magentaPixel[4] = {255, 0, 255, 255};
        TextureUtils::uploadRgbaTexture(magentaPixel, 1, 1, &m_missingPlaceholderTex);
    }

    m_placeholdersCreated = true;
    if (m_loader) {
        m_loader->setPlaceholders(
            static_cast<TextureHandle>(m_loadingPlaceholderTex),
            static_cast<TextureHandle>(m_missingPlaceholderTex));
    }
}

void IconCache::ensureInitialized() {
    if (m_loader) {
        ensurePlaceholders();
        return;
    }

    const unsigned int hw = std::thread::hardware_concurrency();
    const size_t workerCount = (hw <= 2)
        ? 1u
        : static_cast<size_t>(std::clamp<unsigned int>(hw - 1, 1u, 2u));
    m_workerStreams.clear();
    m_workerStreams.resize(workerCount);

    IconLoaderConfig cfg;
    cfg.worker_count = workerCount;
    cfg.auto_start_workers = true;
    cfg.max_uploads_per_frame = 64;
    cfg.max_upload_time_ms = 1.5;
    cfg.max_resident_textures = 4096;
    cfg.stale_frame_window = 2;

    cfg.decode_fn = [this](const IconRef& ref, uint64_t /*generation*/, size_t workerIndex) -> DecodedIcon {
        DecodedIcon out;
        WorkerPakStreams& streams = m_workerStreams[workerIndex % m_workerStreams.size()];
        auto resolved = m_resolver.resolve(ref, streams);
        if (!resolved.has_value() || resolved->bytes.empty()) {
            return out;
        }
        int w = 0;
        int h = 0;
        std::vector<unsigned char> rgba;
        if (!TextureUtils::decodeRgbaFromMemory(
                resolved->bytes.data(),
                resolved->bytes.size(),
                w,
                h,
                rgba)) {
            return out;
        }
        out.found = true;
        out.from_fallback = resolved->from_fallback;
        out.width = w;
        out.height = h;
        out.rgba.assign(rgba.begin(), rgba.end());
        return out;
    };

    cfg.upload_fn = [](const uint8_t* rgba, int width, int height) -> TextureHandle {
        if (glfwGetCurrentContext() == nullptr) {
            return 0;
        }
        unsigned int texId = 0;
        if (!TextureUtils::uploadRgbaTexture(rgba, width, height, &texId)) {
            return 0;
        }
        return static_cast<TextureHandle>(texId);
    };

    cfg.free_fn = [](TextureHandle handle) {
        if (handle == 0 || glfwGetCurrentContext() == nullptr) {
            return;
        }
        GLuint texId = static_cast<GLuint>(handle);
        glDeleteTextures(1, &texId);
    };

    m_loader = std::make_unique<IconLoader>(std::move(cfg));
    ensurePlaceholders();
}

void IconCache::beginFrame(uint64_t frameIndex) {
    ensureInitialized();
    m_loader->beginFrame(frameIndex);
}

ImTextureID IconCache::get(const IconRef& ref) {
    ensureInitialized();
    return static_cast<ImTextureID>(m_loader->get(ref));
}

std::string IconCache::registerPackage(const GameData& pkg) {
    IconPackageDescriptor desc;
    desc.game_id = pkg.game.id;
    desc.package_root = pkg.package_root;
    desc.icon_root = pkg.icon_root;
    desc.package_id = canonicalPackageId(
        pkg.package_root,
        pkg.icon_root,
        pkg.game.id,
        pkg.game.data_version);
    return m_resolver.registerPackage(desc);
}

ImTextureID IconCache::getResourceIcon(std::string_view packageId, const Resource& res) {
    IconRef ref;
    ref.package_id = std::string(packageId);
    ref.kind = IconKind::Resource;
    ref.id = res.id;
    ref.name = res.name;
    return get(ref);
}

ImTextureID IconCache::getMachineIcon(std::string_view packageId, const Machine& mach) {
    IconRef ref;
    ref.package_id = std::string(packageId);
    ref.kind = IconKind::Machine;
    ref.id = mach.id;
    ref.name = mach.name;
    return get(ref);
}

ImTextureID IconCache::getOptionIcon(
    std::string_view packageId,
    std::string_view optionSetId,
    std::string_view optionKey) {
    IconRef ref;
    ref.package_id = std::string(packageId);
    ref.kind = IconKind::Option;
    ref.id.reserve(optionSetId.size() + 1 + optionKey.size());
    ref.id.append(optionSetId);
    ref.id.push_back('/');
    ref.id.append(optionKey);
    return get(ref);
}

ImTextureID IconCache::getOptionIconIfReady(
    std::string_view packageId,
    std::string_view optionSetId,
    std::string_view optionKey) {
    if (optionSetId.empty() || optionKey.empty()) {
        return 0;
    }
    ensureInitialized();
    IconRef ref;
    ref.package_id = std::string(packageId);
    ref.kind = IconKind::Option;
    ref.id.reserve(optionSetId.size() + 1 + optionKey.size());
    ref.id.append(optionSetId);
    ref.id.push_back('/');
    ref.id.append(optionKey);
    bool isReady = false;
    const TextureHandle handle = m_loader->get(ref, &isReady);
    return isReady ? static_cast<ImTextureID>(handle) : ImTextureID{0};
}

bool IconCache::busy() const {
    return m_loader ? m_loader->busy() : false;
}

void IconCache::invalidate(std::string_view packageId) {
    if (m_loader) {
        m_loader->invalidate(packageId);
    }
}

void IconCache::invalidateAll() {
    if (m_loader) {
        m_loader->invalidateAll();
    }
}

void IconCache::updateSettings(
    const IconFallbackSettings& fallback,
    const std::vector<GameDataPackage>& scannedPackages) {
    m_resolver.setFallbackSettings(fallback);
    m_resolver.setScannedPackages(scannedPackages);
    if (m_loader) {
        m_loader->invalidateFallbackAndMissing();
    }
}

void IconCache::refreshScannedPackages(const std::vector<GameDataPackage>& scannedPackages) {
    m_resolver.setScannedPackages(scannedPackages);
    if (m_loader) {
        m_loader->invalidateFallbackAndMissing();
    }
}

void IconCache::shutdown() {
    if (m_loader) {
        m_loader->shutdown();
        m_loader.reset();
    }
    m_workerStreams.clear();

    if (m_placeholdersCreated) {
        if (glfwGetCurrentContext() != nullptr) {
            if (m_loadingPlaceholderTex != 0) {
                GLuint id = static_cast<GLuint>(m_loadingPlaceholderTex);
                glDeleteTextures(1, &id);
            }
            if (m_missingPlaceholderTex != 0) {
                GLuint id = static_cast<GLuint>(m_missingPlaceholderTex);
                glDeleteTextures(1, &id);
            }
        }
        m_loadingPlaceholderTex = 0;
        m_missingPlaceholderTex = 0;
        m_placeholdersCreated = false;
    }

    m_resolver.clear();
    VLOG(1) << "IconCache shut down.";
}

IconLoaderStats IconCache::getStats() const {
    return m_loader ? m_loader->stats() : IconLoaderStats{};
}
