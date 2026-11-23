#ifndef ICONLOADER_H
#define ICONLOADER_H

#include "services/IconSourceResolver.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

using TextureHandle = uint64_t;

struct DecodedIcon {
    bool found = false;
    bool from_fallback = false;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

struct IconLoaderConfig {
    TextureHandle loading_placeholder = 0;
    TextureHandle missing_placeholder = 0;

    size_t max_uploads_per_frame = 64;
    double max_upload_time_ms = 1.5;
    size_t max_resident_textures = 4096;
    uint64_t stale_frame_window = 2; // drop requests untouched since frame N-2
    size_t worker_count = 0;         // 0 => clamp(hardware_concurrency - 1, 1, 2)
    bool auto_start_workers = true;  // false => deterministic manual stepWorkerOnce() mode

    std::function<DecodedIcon(const IconRef&, uint64_t generation, size_t workerIndex)> decode_fn;
    std::function<TextureHandle(const uint8_t* rgba, int width, int height)> upload_fn;
    std::function<void(TextureHandle)> free_fn;
    std::function<std::chrono::steady_clock::time_point()> now_fn;
};

struct IconLoaderStats {
    size_t resident_textures = 0;
    size_t max_resident_textures = 0;
    size_t missing_entries = 0;
    size_t pending_requests = 0;
    size_t in_flight_decodes = 0;
    size_t ready_to_upload = 0;
    size_t deferred_frees = 0;
    uint64_t dropped_stale_frame = 0;
    uint64_t dropped_stale_generation = 0;
    uint64_t evicted_count = 0;
    uint64_t uploaded_count = 0;
    bool busy = false;
};

// GL-free async icon loading & LRU cache scheduling core (§4.1).
// All UI methods (beginFrame, get, setPlaceholders, invalidate*, shutdown) run on the UI thread;
// decode_fn runs on 1-2 background worker threads.
class IconLoader {
public:
    explicit IconLoader(IconLoaderConfig config);
    ~IconLoader();

    IconLoader(const IconLoader&) = delete;
    IconLoader& operator=(const IconLoader&) = delete;

    void setPlaceholders(TextureHandle loadingPlaceholder, TextureHandle missingPlaceholder);

    // Advances the frame clock, flushes deferred frees, uploads ready icons up to budget,
    // and evicts LRU entries above cap (never evicting frame N or N-1 textures).
    void beginFrame(uint64_t frameIndex);

    // UI-thread lookup. Hit path takes zero locks; misses enqueue/promote in LIFO stack
    // and return the loading placeholder. When outIsReady is provided, it is set to true
    // only if a Ready texture handle is returned (false for loading/missing placeholders).
    TextureHandle get(const IconRef& ref, bool* outIsReady = nullptr);

    // Returns true if any requests are pending, decoding on a worker, or queued for upload.
    bool busy() const;

    // Invalidates cached textures and pending requests for one package (bumps its generation).
    void invalidate(std::string_view packageId);

    // Invalidates all packages (bumps global generation).
    void invalidateAll();

    // Invalidates fallback-sourced and missing entries while retaining direct non-fallback textures
    // (bumps global generation so in-flight decodes from old settings are discarded).
    void invalidateFallbackAndMissing();

    // Stops and joins worker threads and frees all resident & deferred texture handles.
    void shutdown();

    // Deterministic single-step worker execution (for unit tests with auto_start_workers == false).
    // Returns true if one queued request was popped (either decoded or dropped as stale).
    bool stepWorkerOnce(size_t workerIndex = 0);

    IconLoaderStats stats() const;
    size_t workerCount() const { return m_effectiveWorkerCount; }

private:
    struct CacheKey {
        std::string package_id;
        std::string icon_key;

        bool operator==(const CacheKey& other) const {
            return package_id == other.package_id && icon_key == other.icon_key;
        }
    };

    struct CacheKeyHash {
        size_t operator()(const CacheKey& k) const noexcept {
            const size_t h1 = std::hash<std::string>{}(k.package_id);
            const size_t h2 = std::hash<std::string>{}(k.icon_key);
            return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
        }
    };

    enum class EntryState {
        Ready,
        Missing
    };

    struct CacheEntry {
        EntryState state = EntryState::Missing;
        TextureHandle handle = 0;
        bool from_fallback = false;
        uint64_t last_used_frame = 0;
        std::list<CacheKey>::iterator lru_it;
    };

    struct PendingRequest {
        CacheKey key;
        IconRef ref;
        uint64_t generation = 0;
        uint64_t last_requested_frame = 0;
    };

    struct ReadyItem {
        CacheKey key;
        uint64_t generation = 0;
        DecodedIcon decoded;
    };

    struct DeferredFree {
        TextureHandle handle = 0;
        uint64_t last_used_frame = 0;
    };

    static size_t resolveWorkerCount(size_t configured);
    uint64_t currentGenerationLocked(const std::string& packageId) const;
    bool isFrameProtected(uint64_t lastUsedFrame) const;
    void scheduleFreeOrDefer(TextureHandle handle, uint64_t lastUsedFrame);
    void flushDeferredFrees();
    void evictLruIfNeeded();
    void workerLoop(size_t workerIndex);
    bool processPoppedRequest(PendingRequest req, size_t workerIndex);
    void purgeStaleReadyItems(std::string_view onlyPackageId, bool purgeAll);

    IconLoaderConfig m_config;
    size_t m_effectiveWorkerCount = 1;

    // UI-thread-only cache and LRU state (no lock needed on UI hit path).
    uint64_t m_currentFrame = 0;
    std::atomic<uint64_t> m_currentFrameAtomic{0};
    std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> m_cache;
    std::list<CacheKey> m_lruList; // Front = MRU, Back = LRU
    size_t m_residentCount = 0;
    size_t m_missingCount = 0;
    std::vector<DeferredFree> m_deferredFrees;

    // Request stack & generation state (protected by m_requestMutex).
    // Lock discipline: m_requestMutex and m_readyMutex are NEVER held simultaneously.
    mutable std::mutex m_requestMutex;
    std::condition_variable m_requestCv;
    bool m_stop = false;
    bool m_shutdownDone = false;
    uint64_t m_nextGen = 1;
    uint64_t m_globalGen = 1;
    std::unordered_map<std::string, uint64_t> m_packageGens;
    std::list<PendingRequest> m_requestStack; // Back = top of LIFO stack
    std::unordered_map<CacheKey, std::list<PendingRequest>::iterator, CacheKeyHash> m_requestIndex;
    std::unordered_map<CacheKey, uint64_t, CacheKeyHash> m_inFlightOrReady;
    size_t m_inFlightCount = 0;

    // Ready-to-upload queue (protected by m_readyMutex).
    mutable std::mutex m_readyMutex;
    std::deque<ReadyItem> m_readyQueue;
    std::atomic<size_t> m_readyCountAtomic{0};

    // Workers.
    std::vector<std::thread> m_workers;

    // Diagnostics.
    std::atomic<uint64_t> m_droppedStaleFrame{0};
    std::atomic<uint64_t> m_droppedStaleGen{0};
    uint64_t m_evictedCount = 0;
    uint64_t m_uploadedCount = 0;
};

#endif // ICONLOADER_H
