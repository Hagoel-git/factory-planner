#include "services/IconLoader.h"

#include <algorithm>
#include <utility>

size_t IconLoader::resolveWorkerCount(size_t configured) {
    if (configured > 0) {
        return configured;
    }
    const unsigned int hw = std::thread::hardware_concurrency();
    if (hw <= 2) {
        return 1;
    }
    return static_cast<size_t>(std::clamp<unsigned int>(hw - 1, 1u, 2u));
}

IconLoader::IconLoader(IconLoaderConfig config)
    : m_config(std::move(config)),
      m_effectiveWorkerCount(resolveWorkerCount(m_config.worker_count)) {
    if (!m_config.now_fn) {
        m_config.now_fn = []() { return std::chrono::steady_clock::now(); };
    }
    if (m_config.auto_start_workers) {
        m_workers.reserve(m_effectiveWorkerCount);
        for (size_t i = 0; i < m_effectiveWorkerCount; ++i) {
            m_workers.emplace_back([this, i]() { workerLoop(i); });
        }
    }
}

IconLoader::~IconLoader() {
    shutdown();
}

void IconLoader::setPlaceholders(TextureHandle loadingPlaceholder, TextureHandle missingPlaceholder) {
    m_config.loading_placeholder = loadingPlaceholder;
    m_config.missing_placeholder = missingPlaceholder;
}

uint64_t IconLoader::currentGenerationLocked(const std::string& packageId) const {
    auto it = m_packageGens.find(packageId);
    return (it != m_packageGens.end()) ? it->second : m_globalGen;
}

bool IconLoader::isFrameProtected(uint64_t lastUsedFrame) const {
    if (m_currentFrame <= 1) {
        return true;
    }
    return lastUsedFrame >= m_currentFrame - 1;
}

void IconLoader::scheduleFreeOrDefer(TextureHandle handle, uint64_t lastUsedFrame) {
    if (handle == 0 || !m_config.free_fn) {
        return;
    }
    if (isFrameProtected(lastUsedFrame)) {
        m_deferredFrees.push_back(DeferredFree{handle, lastUsedFrame});
    } else {
        m_config.free_fn(handle);
    }
}

void IconLoader::flushDeferredFrees() {
    if (m_deferredFrees.empty()) {
        return;
    }
    size_t writePos = 0;
    for (size_t i = 0; i < m_deferredFrees.size(); ++i) {
        const DeferredFree& df = m_deferredFrees[i];
        if (!isFrameProtected(df.last_used_frame)) {
            if (df.handle != 0 && m_config.free_fn) {
                m_config.free_fn(df.handle);
            }
        } else {
            m_deferredFrees[writePos++] = df;
        }
    }
    m_deferredFrees.resize(writePos);
}

void IconLoader::evictLruIfNeeded() {
    while (m_residentCount > m_config.max_resident_textures && !m_lruList.empty()) {
        const CacheKey& oldestKey = m_lruList.back();
        auto it = m_cache.find(oldestKey);
        if (it == m_cache.end() || it->second.state != EntryState::Ready) {
            m_lruList.pop_back();
            continue;
        }
        // Never evict textures used in the current or previous frame (§4.1).
        if (isFrameProtected(it->second.last_used_frame)) {
            break;
        }
        const TextureHandle doomedHandle = it->second.handle;
        m_lruList.pop_back();
        m_cache.erase(it);
        --m_residentCount;
        ++m_evictedCount;
        if (doomedHandle != 0 && m_config.free_fn) {
            m_config.free_fn(doomedHandle);
        }
    }
}

void IconLoader::beginFrame(uint64_t frameIndex) {
    m_currentFrame = frameIndex;
    m_currentFrameAtomic.store(frameIndex, std::memory_order_release);

    flushDeferredFrees();

    const auto startTime = m_config.now_fn();
    size_t uploadsThisFrame = 0;
    std::vector<std::pair<CacheKey, uint64_t>> completedKeys;

    for (;;) {
        if (uploadsThisFrame >= m_config.max_uploads_per_frame) {
            break;
        }
        const auto now = m_config.now_fn();
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(now - startTime).count();
        if (elapsedMs >= m_config.max_upload_time_ms) {
            break;
        }

        ReadyItem item;
        {
            std::lock_guard<std::mutex> lock(m_readyMutex);
            if (m_readyQueue.empty()) {
                break;
            }
            item = std::move(m_readyQueue.front());
            m_readyQueue.pop_front();
            m_readyCountAtomic.store(m_readyQueue.size(), std::memory_order_release);
        }

        completedKeys.emplace_back(item.key, item.generation);

        uint64_t expectedGen = 0;
        {
            std::lock_guard<std::mutex> lock(m_requestMutex);
            expectedGen = currentGenerationLocked(item.key.package_id);
        }
        if (item.generation != expectedGen) {
            m_droppedStaleGen.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        if (item.decoded.found &&
            item.decoded.width > 0 &&
            item.decoded.height > 0 &&
            !item.decoded.rgba.empty() &&
            m_config.upload_fn) {
            const TextureHandle handle = m_config.upload_fn(
                item.decoded.rgba.data(),
                item.decoded.width,
                item.decoded.height);
            ++uploadsThisFrame;

            if (handle != 0) {
                ++m_uploadedCount;
                auto existing = m_cache.find(item.key);
                if (existing != m_cache.end()) {
                    if (existing->second.state == EntryState::Ready) {
                        scheduleFreeOrDefer(existing->second.handle, existing->second.last_used_frame);
                        m_lruList.erase(existing->second.lru_it);
                        --m_residentCount;
                    } else {
                        --m_missingCount;
                    }
                    m_cache.erase(existing);
                }

                m_lruList.push_front(item.key);
                CacheEntry entry;
                entry.state = EntryState::Ready;
                entry.handle = handle;
                entry.from_fallback = item.decoded.from_fallback;
                entry.last_used_frame = m_currentFrame;
                entry.lru_it = m_lruList.begin();
                m_cache.emplace(item.key, std::move(entry));
                ++m_residentCount;
                continue;
            }
        }

        // Missing or decode/upload failure -> cache as Missing.
        auto existing = m_cache.find(item.key);
        if (existing != m_cache.end()) {
            if (existing->second.state == EntryState::Ready) {
                scheduleFreeOrDefer(existing->second.handle, existing->second.last_used_frame);
                m_lruList.erase(existing->second.lru_it);
                --m_residentCount;
                existing->second.state = EntryState::Missing;
                existing->second.handle = 0;
                existing->second.from_fallback = false;
                existing->second.last_used_frame = m_currentFrame;
                ++m_missingCount;
            }
        } else {
            CacheEntry entry;
            entry.state = EntryState::Missing;
            entry.handle = 0;
            entry.from_fallback = false;
            entry.last_used_frame = m_currentFrame;
            m_cache.emplace(item.key, std::move(entry));
            ++m_missingCount;
        }
    }

    if (!completedKeys.empty()) {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        for (const auto& [key, gen] : completedKeys) {
            auto it = m_inFlightOrReady.find(key);
            if (it != m_inFlightOrReady.end() && it->second == gen) {
                m_inFlightOrReady.erase(it);
            }
        }
    }

    evictLruIfNeeded();
}

TextureHandle IconLoader::get(const IconRef& ref, bool* outIsReady) {
    if (outIsReady) {
        *outIsReady = false;
    }
    if (ref.id.empty()) {
        return m_config.missing_placeholder;
    }

    CacheKey key{ref.package_id, makeIconKey(ref.kind, ref.id)};

    // Lock-free UI-thread cache hit path.
    if (auto it = m_cache.find(key); it != m_cache.end()) {
        if (it->second.state == EntryState::Ready) {
            it->second.last_used_frame = m_currentFrame;
            m_lruList.splice(m_lruList.begin(), m_lruList, it->second.lru_it);
            if (outIsReady) {
                *outIsReady = true;
            }
            return it->second.handle;
        }
        return m_config.missing_placeholder;
    }

    bool shouldNotify = false;
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        if (m_stop) {
            return m_config.loading_placeholder;
        }
        const uint64_t gen = currentGenerationLocked(key.package_id);

        if (auto actIt = m_inFlightOrReady.find(key);
            actIt != m_inFlightOrReady.end() && actIt->second == gen) {
            return m_config.loading_placeholder;
        }

        if (auto reqIt = m_requestIndex.find(key); reqIt != m_requestIndex.end()) {
            auto listIt = reqIt->second;
            listIt->last_requested_frame = m_currentFrame;
            listIt->generation = gen;
            if (!ref.name.empty() && listIt->ref.name.empty()) {
                listIt->ref.name = ref.name;
            }
            // Promote to top of LIFO stack (back of list).
            m_requestStack.splice(m_requestStack.end(), m_requestStack, listIt);
        } else {
            PendingRequest req;
            req.key = key;
            req.ref = ref;
            req.generation = gen;
            req.last_requested_frame = m_currentFrame;
            m_requestStack.push_back(std::move(req));
            auto insertedIt = std::prev(m_requestStack.end());
            m_requestIndex.emplace(std::move(key), insertedIt);
            shouldNotify = true;
        }
    }

    if (shouldNotify) {
        m_requestCv.notify_one();
    }
    return m_config.loading_placeholder;
}

bool IconLoader::busy() const {
    if (m_readyCountAtomic.load(std::memory_order_acquire) > 0) {
        return true;
    }
    std::lock_guard<std::mutex> lock(m_requestMutex);
    return !m_requestStack.empty() || m_inFlightCount > 0;
}

void IconLoader::purgeStaleReadyItems(std::string_view onlyPackageId, bool purgeAll) {
    std::lock_guard<std::mutex> lock(m_readyMutex);
    if (purgeAll) {
        m_readyQueue.clear();
    } else if (!onlyPackageId.empty()) {
        std::deque<ReadyItem> kept;
        for (auto& item : m_readyQueue) {
            if (item.key.package_id != onlyPackageId) {
                kept.push_back(std::move(item));
            }
        }
        m_readyQueue = std::move(kept);
    }
    m_readyCountAtomic.store(m_readyQueue.size(), std::memory_order_release);
}

void IconLoader::invalidate(std::string_view packageId) {
    const std::string pkgId(packageId);

    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_packageGens[pkgId] = ++m_nextGen;

        for (auto it = m_requestStack.begin(); it != m_requestStack.end();) {
            if (it->key.package_id == pkgId) {
                m_requestIndex.erase(it->key);
                it = m_requestStack.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = m_inFlightOrReady.begin(); it != m_inFlightOrReady.end();) {
            if (it->first.package_id == pkgId) {
                it = m_inFlightOrReady.erase(it);
            } else {
                ++it;
            }
        }
    }

    purgeStaleReadyItems(pkgId, /*purgeAll=*/false);

    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (it->first.package_id == pkgId) {
            if (it->second.state == EntryState::Ready) {
                scheduleFreeOrDefer(it->second.handle, it->second.last_used_frame);
                m_lruList.erase(it->second.lru_it);
                --m_residentCount;
            } else {
                --m_missingCount;
            }
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}

void IconLoader::invalidateAll() {
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_globalGen = ++m_nextGen;
        m_packageGens.clear();
        m_requestStack.clear();
        m_requestIndex.clear();
        m_inFlightOrReady.clear();
    }

    purgeStaleReadyItems({}, /*purgeAll=*/true);

    for (auto& [_, entry] : m_cache) {
        if (entry.state == EntryState::Ready) {
            scheduleFreeOrDefer(entry.handle, entry.last_used_frame);
        }
    }
    m_cache.clear();
    m_lruList.clear();
    m_residentCount = 0;
    m_missingCount = 0;
}

void IconLoader::invalidateFallbackAndMissing() {
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_globalGen = ++m_nextGen;
        m_packageGens.clear();
        m_requestStack.clear();
        m_requestIndex.clear();
        m_inFlightOrReady.clear();
    }

    purgeStaleReadyItems({}, /*purgeAll=*/true);

    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (it->second.state == EntryState::Missing) {
            --m_missingCount;
            it = m_cache.erase(it);
        } else if (it->second.from_fallback) {
            scheduleFreeOrDefer(it->second.handle, it->second.last_used_frame);
            m_lruList.erase(it->second.lru_it);
            --m_residentCount;
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}

void IconLoader::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        if (m_shutdownDone) {
            return;
        }
        m_stop = true;
        m_shutdownDone = true;
        m_requestStack.clear();
        m_requestIndex.clear();
        m_inFlightOrReady.clear();
    }
    m_requestCv.notify_all();

    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();

    purgeStaleReadyItems({}, /*purgeAll=*/true);

    for (auto& [_, entry] : m_cache) {
        if (entry.state == EntryState::Ready && entry.handle != 0 && m_config.free_fn) {
            m_config.free_fn(entry.handle);
        }
    }
    m_cache.clear();
    m_lruList.clear();
    m_residentCount = 0;
    m_missingCount = 0;

    for (const auto& df : m_deferredFrees) {
        if (df.handle != 0 && m_config.free_fn) {
            m_config.free_fn(df.handle);
        }
    }
    m_deferredFrees.clear();
}

bool IconLoader::processPoppedRequest(PendingRequest req, size_t workerIndex) {
    DecodedIcon decoded;
    if (m_config.decode_fn) {
        decoded = m_config.decode_fn(req.ref, req.generation, workerIndex);
    }

    {
        std::lock_guard<std::mutex> lock(m_readyMutex);
        ReadyItem item;
        item.key = std::move(req.key);
        item.generation = req.generation;
        item.decoded = std::move(decoded);
        m_readyQueue.push_back(std::move(item));
        m_readyCountAtomic.store(m_readyQueue.size(), std::memory_order_release);
    }

    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        if (m_inFlightCount > 0) {
            --m_inFlightCount;
        }
    }
    return true;
}

void IconLoader::workerLoop(size_t workerIndex) {
    for (;;) {
        PendingRequest req;
        bool hasReq = false;
        {
            std::unique_lock<std::mutex> lock(m_requestMutex);
            m_requestCv.wait(lock, [this]() { return m_stop || !m_requestStack.empty(); });
            if (m_stop) {
                return;
            }
            if (m_requestStack.empty()) {
                continue;
            }

            // Pop from top of LIFO stack (back of list).
            req = std::move(m_requestStack.back());
            m_requestStack.pop_back();
            m_requestIndex.erase(req.key);

            if (req.generation != currentGenerationLocked(req.key.package_id)) {
                m_droppedStaleGen.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            const uint64_t curFrame = m_currentFrameAtomic.load(std::memory_order_acquire);
            if (curFrame > req.last_requested_frame &&
                (curFrame - req.last_requested_frame) > m_config.stale_frame_window) {
                m_droppedStaleFrame.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            m_inFlightOrReady[req.key] = req.generation;
            ++m_inFlightCount;
            hasReq = true;
        }

        if (hasReq) {
            processPoppedRequest(std::move(req), workerIndex);
        }
    }
}

bool IconLoader::stepWorkerOnce(size_t workerIndex) {
    PendingRequest req;
    bool shouldDecode = false;
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        if (m_stop || m_requestStack.empty()) {
            return false;
        }

        req = std::move(m_requestStack.back());
        m_requestStack.pop_back();
        m_requestIndex.erase(req.key);

        if (req.generation != currentGenerationLocked(req.key.package_id)) {
            m_droppedStaleGen.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        const uint64_t curFrame = m_currentFrameAtomic.load(std::memory_order_acquire);
        if (curFrame > req.last_requested_frame &&
            (curFrame - req.last_requested_frame) > m_config.stale_frame_window) {
            m_droppedStaleFrame.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        m_inFlightOrReady[req.key] = req.generation;
        ++m_inFlightCount;
        shouldDecode = true;
    }

    if (shouldDecode) {
        processPoppedRequest(std::move(req), workerIndex);
    }
    return true;
}

IconLoaderStats IconLoader::stats() const {
    IconLoaderStats s;
    s.resident_textures = m_residentCount;
    s.max_resident_textures = m_config.max_resident_textures;
    s.missing_entries = m_missingCount;
    s.deferred_frees = m_deferredFrees.size();
    s.dropped_stale_frame = m_droppedStaleFrame.load(std::memory_order_relaxed);
    s.dropped_stale_generation = m_droppedStaleGen.load(std::memory_order_relaxed);
    s.evicted_count = m_evictedCount;
    s.uploaded_count = m_uploadedCount;
    s.ready_to_upload = m_readyCountAtomic.load(std::memory_order_acquire);

    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        s.pending_requests = m_requestStack.size();
        s.in_flight_decodes = m_inFlightCount;
    }
    s.busy = (s.pending_requests > 0 || s.in_flight_decodes > 0 || s.ready_to_upload > 0);
    return s;
}
