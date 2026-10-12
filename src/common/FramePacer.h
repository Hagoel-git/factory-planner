#pragma once

#include <chrono>
#include <thread>
#include <algorithm>
#include <cmath>

namespace common {

/**
 * @brief Frame rate controller and pacer to maintain target frame rates without
 * accumulator drift, latency backlogs, or sleep over-allocation.
 */
class FramePacer {
public:
    static constexpr double DEFAULT_FPS = 60.0;
    static constexpr double MIN_FPS = 1.0;
    static constexpr double MAX_FPS = 1000.0;

    static bool isValidFps(double fps) {
        return std::isfinite(fps) && fps >= MIN_FPS && fps <= MAX_FPS;
    }

    explicit FramePacer(double targetFps = DEFAULT_FPS)
        : m_targetFps(isValidFps(targetFps) ? targetFps : DEFAULT_FPS)
        , m_frameDuration(calculateDuration(m_targetFps))
        , m_nextFrame(std::chrono::steady_clock::now())
    {
    }

    void setTargetFps(double targetFps) {
        if (isValidFps(targetFps)) {
            m_targetFps = targetFps;
            m_frameDuration = calculateDuration(m_targetFps);
            reset();
        }
    }

    double getTargetFps() const {
        return m_targetFps;
    }

    std::chrono::steady_clock::duration getFrameDuration() const {
        return m_frameDuration;
    }

    std::chrono::steady_clock::time_point getNextFrame() const {
        return m_nextFrame;
    }

    void reset(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
        m_nextFrame = now;
    }

    /**
     * @brief Paces the current frame towards m_nextFrame using the provided clock timestamp and sleeper callback.
     * 
     * Advances m_nextFrame by m_frameDuration. If now < m_nextFrame, sleeps until m_nextFrame.
     * If the frame deadline has passed (now >= m_nextFrame), clamps m_nextFrame = now to prevent
     * negative latency debt accumulation (which causes 100% CPU spinning) without prematurely
     * resetting the target before deadline evaluation (which causes a 40 FPS cap).
     * 
     * @return true if a sleep occurred, false if the frame was late and sleep was skipped.
     */
    template <typename SleepUntilFunc>
    bool pace(std::chrono::steady_clock::time_point now, SleepUntilFunc&& sleepUntil) {
        m_nextFrame += m_frameDuration;
        if (now < m_nextFrame) {
            sleepUntil(m_nextFrame);
            return true;
        } else {
            m_nextFrame = now;
            return false;
        }
    }

    /**
     * @brief Paces the frame using std::chrono::steady_clock::now() and std::this_thread::sleep_until.
     * 
     * @return true if a sleep occurred, false if sleep was skipped.
     */
    bool pace() {
        return pace(std::chrono::steady_clock::now(), [](std::chrono::steady_clock::time_point target) {
            std::this_thread::sleep_until(target);
        });
    }

private:
    static std::chrono::steady_clock::duration calculateDuration(double fps) {
        return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(1.0 / fps));
    }

    double m_targetFps;
    std::chrono::steady_clock::duration m_frameDuration;
    std::chrono::steady_clock::time_point m_nextFrame;
};

} // namespace common
