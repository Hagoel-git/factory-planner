#include <gtest/gtest.h>
#include "common/FramePacer.h"
#include <chrono>
#include <limits>

using namespace std::chrono_literals;

class FramePacerTest : public ::testing::Test {
protected:
    common::FramePacer pacer{60.0};
};

TEST_F(FramePacerTest, InitialStateAndConfiguration) {
    EXPECT_DOUBLE_EQ(pacer.getTargetFps(), 60.0);
    EXPECT_EQ(pacer.getFrameDuration(), std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / 60.0)));

    pacer.setTargetFps(144.0);
    EXPECT_DOUBLE_EQ(pacer.getTargetFps(), 144.0);

    // Invalid fps should be ignored
    pacer.setTargetFps(0.0);
    EXPECT_DOUBLE_EQ(pacer.getTargetFps(), 144.0);
    pacer.setTargetFps(-30.0);
    EXPECT_DOUBLE_EQ(pacer.getTargetFps(), 144.0);
    pacer.setTargetFps(std::numeric_limits<double>::infinity());
    EXPECT_DOUBLE_EQ(pacer.getTargetFps(), 144.0);
    pacer.setTargetFps(-std::numeric_limits<double>::infinity());
    EXPECT_DOUBLE_EQ(pacer.getTargetFps(), 144.0);
    pacer.setTargetFps(std::numeric_limits<double>::quiet_NaN());
    EXPECT_DOUBLE_EQ(pacer.getTargetFps(), 144.0);
    pacer.setTargetFps(1e-15);
    EXPECT_DOUBLE_EQ(pacer.getTargetFps(), 144.0);
    pacer.setTargetFps(1e15);
    EXPECT_DOUBLE_EQ(pacer.getTargetFps(), 144.0);

    // Constructor with invalid fps defaults safely to 60.0
    common::FramePacer invalidPacer0(0.0);
    EXPECT_DOUBLE_EQ(invalidPacer0.getTargetFps(), 60.0);
    common::FramePacer invalidPacerInf(std::numeric_limits<double>::infinity());
    EXPECT_DOUBLE_EQ(invalidPacerInf.getTargetFps(), 60.0);
    common::FramePacer invalidPacerNaN(std::numeric_limits<double>::quiet_NaN());
    EXPECT_DOUBLE_EQ(invalidPacerNaN.getTargetFps(), 60.0);
    common::FramePacer invalidPacerTiny(1e-15);
    EXPECT_DOUBLE_EQ(invalidPacerTiny.getTargetFps(), 60.0);
}

TEST_F(FramePacerTest, PacesNormalFramesAt60Fps) {
    auto t0 = std::chrono::steady_clock::time_point(0s);
    pacer.reset(t0);

    // Frame 1: work takes 5ms (fast frame)
    auto now1 = t0 + 5ms;
    std::chrono::steady_clock::time_point sleptUntil1{};
    bool didSleep1 = pacer.pace(now1, [&](std::chrono::steady_clock::time_point target) {
        sleptUntil1 = target;
    });

    EXPECT_TRUE(didSleep1);
    EXPECT_EQ(sleptUntil1, t0 + pacer.getFrameDuration());
    auto sleepDuration1 = sleptUntil1 - now1;
    EXPECT_EQ(sleepDuration1, pacer.getFrameDuration() - 5ms);

    // Frame 2: starts at end of Frame 1 target, work takes 8ms
    auto frame1End = sleptUntil1;
    auto now2 = frame1End + 8ms;
    std::chrono::steady_clock::time_point sleptUntil2{};
    bool didSleep2 = pacer.pace(now2, [&](std::chrono::steady_clock::time_point target) {
        sleptUntil2 = target;
    });

    EXPECT_TRUE(didSleep2);
    EXPECT_EQ(sleptUntil2, t0 + 2 * pacer.getFrameDuration());
    auto sleepDuration2 = sleptUntil2 - now2;
    EXPECT_EQ(sleepDuration2, pacer.getFrameDuration() - 8ms);

    // Total elapsed time between frame 1 completion and frame 2 completion is exactly 1 frame duration (16.66ms = 60 FPS)
    auto frameInterval = sleptUntil2 - frame1End;
    EXPECT_EQ(frameInterval, pacer.getFrameDuration());
}

TEST_F(FramePacerTest, DoesNotCapAt40Fps) {
    // When work takes ~8.33ms, a 60 FPS pacer must sleep ~8.33ms so total frame time is 16.67ms (60 FPS).
    // The buggy proposed fix slept for the full 16.67ms AFTER the 8.33ms work, resulting in 25ms (40 FPS).
    auto t0 = std::chrono::steady_clock::time_point(0s);
    pacer.reset(t0);

    auto workDuration = 8333us;
    auto now = t0 + workDuration;
    std::chrono::steady_clock::time_point sleptUntil{};
    pacer.pace(now, [&](std::chrono::steady_clock::time_point target) {
        sleptUntil = target;
    });

    auto actualSleep = sleptUntil - now;
    // Sleep duration must be ~8.33ms (remainder of frame), NOT 16.67ms (full targetTime)
    EXPECT_LT(actualSleep, pacer.getFrameDuration());
    EXPECT_EQ(actualSleep, pacer.getFrameDuration() - workDuration);

    // Total frame time is strictly 1 frame duration (60 FPS), NOT 25ms (40 FPS)
    EXPECT_EQ(sleptUntil - t0, pacer.getFrameDuration());
}

TEST_F(FramePacerTest, AbsorbsSmallSleepJitter) {
    auto t0 = std::chrono::steady_clock::time_point(0s);
    pacer.reset(t0);

    // Frame 1 runs and paces normally
    auto now1 = t0 + 6ms;
    std::chrono::steady_clock::time_point frame1Target{};
    EXPECT_TRUE(pacer.pace(now1, [&](std::chrono::steady_clock::time_point target) {
        frame1Target = target;
    }));
    EXPECT_EQ(frame1Target, t0 + pacer.getFrameDuration());

    // Frame 1 wakes up 1ms late due to OS scheduling jitter
    auto jitter = 1ms;
    auto frame1Wake = frame1Target + jitter;

    // Frame 2 work takes 8ms from wake time
    auto now2 = frame1Wake + 8ms;
    std::chrono::steady_clock::time_point sleptUntil2{};
    bool didSleep = pacer.pace(now2, [&](std::chrono::steady_clock::time_point target) {
        sleptUntil2 = target;
    });

    EXPECT_TRUE(didSleep);
    // Frame 2 target must still align with 2 * frameDuration
    EXPECT_EQ(sleptUntil2, t0 + 2 * pacer.getFrameDuration());
    // Sleep duration should be reduced by the jitter (1ms) to compensate
    EXPECT_EQ(sleptUntil2 - now2, pacer.getFrameDuration() - 8ms - jitter);
}

TEST_F(FramePacerTest, HandlesLagSpikeWithoutAccumulatorDrift) {
    auto t0 = std::chrono::steady_clock::time_point(0s);
    pacer.reset(t0);

    // Lag spike: frame takes 100ms (far exceeding 16.6ms)
    auto nowSpike = t0 + 100ms;
    bool sleepCalled = false;
    bool didSleep = pacer.pace(nowSpike, [&](std::chrono::steady_clock::time_point) {
        sleepCalled = true;
    });

    EXPECT_FALSE(didSleep);
    EXPECT_FALSE(sleepCalled);
    // Target must be clamped to now, NOT left 83.3ms in the past
    EXPECT_EQ(pacer.getNextFrame(), nowSpike);

    // Next frame: work takes normal 8ms
    auto nowNext = nowSpike + 8ms;
    std::chrono::steady_clock::time_point sleptUntil{};
    bool didSleepNext = pacer.pace(nowNext, [&](std::chrono::steady_clock::time_point target) {
        sleptUntil = target;
    });

    EXPECT_TRUE(didSleepNext);
    // Immediately recovers to 60 FPS pacing relative to nowSpike
    EXPECT_EQ(sleptUntil, nowSpike + pacer.getFrameDuration());
    EXPECT_EQ(sleptUntil - nowNext, pacer.getFrameDuration() - 8ms);
}

TEST_F(FramePacerTest, ConsecutiveSlowFramesDoNotAccumulateDebt) {
    auto t0 = std::chrono::steady_clock::time_point(0s);
    pacer.reset(t0);

    auto currentTime = t0;
    // 10 consecutive slow frames taking 30ms each
    for (int i = 0; i < 10; ++i) {
        currentTime += 30ms;
        bool sleepCalled = false;
        bool didSleep = pacer.pace(currentTime, [&](std::chrono::steady_clock::time_point) {
            sleepCalled = true;
        });
        EXPECT_FALSE(didSleep);
        EXPECT_FALSE(sleepCalled);
        EXPECT_EQ(pacer.getNextFrame(), currentTime);
    }

    // After 10 slow frames, next frame takes 5ms
    currentTime += 5ms;
    std::chrono::steady_clock::time_point sleptUntil{};
    bool didSleep = pacer.pace(currentTime, [&](std::chrono::steady_clock::time_point target) {
        sleptUntil = target;
    });

    EXPECT_TRUE(didSleep);
    // Resumes pacing immediately on the very first normal frame (no debt)
    EXPECT_EQ(sleptUntil - (currentTime - 5ms), pacer.getFrameDuration());
}

TEST_F(FramePacerTest, ResetClearsAccumulatedState) {
    auto t0 = std::chrono::steady_clock::time_point(0s);
    pacer.reset(t0);

    auto tIdleEnd = t0 + 10s;
    pacer.reset(tIdleEnd);
    EXPECT_EQ(pacer.getNextFrame(), tIdleEnd);

    // First frame after idle: work takes 6ms
    auto now = tIdleEnd + 6ms;
    std::chrono::steady_clock::time_point sleptUntil{};
    bool didSleep = pacer.pace(now, [&](std::chrono::steady_clock::time_point target) {
        sleptUntil = target;
    });

    EXPECT_TRUE(didSleep);
    EXPECT_EQ(sleptUntil, tIdleEnd + pacer.getFrameDuration());
    EXPECT_EQ(sleptUntil - now, pacer.getFrameDuration() - 6ms);
}

TEST_F(FramePacerTest, ExactDeadlineBoundary) {
    auto t0 = std::chrono::steady_clock::time_point(0s);
    pacer.reset(t0);

    // Work takes exactly one frame duration
    auto exactDeadline = t0 + pacer.getFrameDuration();
    bool sleepCalled = false;
    bool didSleep = pacer.pace(exactDeadline, [&](std::chrono::steady_clock::time_point) {
        sleepCalled = true;
    });

    // Should skip sleep and clamp nextFrame to now
    EXPECT_FALSE(didSleep);
    EXPECT_FALSE(sleepCalled);
    EXPECT_EQ(pacer.getNextFrame(), exactDeadline);

    // Subsequent normal frame resumes pacing immediately
    auto nowNext = exactDeadline + 4ms;
    std::chrono::steady_clock::time_point sleptUntil{};
    bool didSleepNext = pacer.pace(nowNext, [&](std::chrono::steady_clock::time_point target) {
        sleptUntil = target;
    });

    EXPECT_TRUE(didSleepNext);
    EXPECT_EQ(sleptUntil, exactDeadline + pacer.getFrameDuration());
    EXPECT_EQ(sleptUntil - nowNext, pacer.getFrameDuration() - 4ms);
}

TEST_F(FramePacerTest, AbsorbsNegativeSleepJitter) {
    auto t0 = std::chrono::steady_clock::time_point(0s);
    pacer.reset(t0);

    // Frame 1 runs and targets t0 + frameDuration
    auto now1 = t0 + 6ms;
    std::chrono::steady_clock::time_point frame1Target{};
    EXPECT_TRUE(pacer.pace(now1, [&](std::chrono::steady_clock::time_point target) {
        frame1Target = target;
    }));

    // Frame 1 wakes up 1ms EARLY (spurious wakeup handled or high-res timer discrepancy)
    auto jitter = -1ms;
    auto frame1Wake = frame1Target + jitter;

    // Frame 2 work takes 8ms from early wake time
    auto now2 = frame1Wake + 8ms;
    std::chrono::steady_clock::time_point sleptUntil2{};
    bool didSleep = pacer.pace(now2, [&](std::chrono::steady_clock::time_point target) {
        sleptUntil2 = target;
    });

    EXPECT_TRUE(didSleep);
    EXPECT_EQ(sleptUntil2, t0 + 2 * pacer.getFrameDuration());
    // Sleep duration should expand by 1ms to absorb the negative jitter
    EXPECT_EQ(sleptUntil2 - now2, pacer.getFrameDuration() - 8ms - jitter);
}

TEST_F(FramePacerTest, PacesAtHighRefreshRate144Fps) {
    pacer.setTargetFps(144.0);
    auto t0 = std::chrono::steady_clock::time_point(0s);
    pacer.reset(t0);

    auto workDuration = 2ms;
    auto now = t0 + workDuration;
    std::chrono::steady_clock::time_point sleptUntil{};
    bool didSleep = pacer.pace(now, [&](std::chrono::steady_clock::time_point target) {
        sleptUntil = target;
    });

    EXPECT_TRUE(didSleep);
    EXPECT_EQ(sleptUntil, t0 + pacer.getFrameDuration());
    EXPECT_EQ(sleptUntil - now, pacer.getFrameDuration() - workDuration);
    EXPECT_LT(pacer.getFrameDuration(), 7ms); // 1/144 = 6.944ms
}

TEST_F(FramePacerTest, DynamicFpsSwitching) {
    auto t0 = std::chrono::steady_clock::time_point(0s);
    pacer.reset(t0);

    // Frame at 60 FPS
    auto now60 = t0 + 5ms;
    std::chrono::steady_clock::time_point target60{};
    pacer.pace(now60, [&](std::chrono::steady_clock::time_point target) {
        target60 = target;
    });
    EXPECT_EQ(target60, t0 + pacer.getFrameDuration());

    // User switches to 120 FPS
    auto switchTime = target60;
    pacer.setTargetFps(120.0);
    pacer.reset(switchTime);

    // Frame at 120 FPS: work takes 3ms
    auto now120 = switchTime + 3ms;
    std::chrono::steady_clock::time_point target120{};
    bool didSleep = pacer.pace(now120, [&](std::chrono::steady_clock::time_point target) {
        target120 = target;
    });

    EXPECT_TRUE(didSleep);
    EXPECT_EQ(target120, switchTime + pacer.getFrameDuration());
    EXPECT_EQ(target120 - now120, pacer.getFrameDuration() - 3ms);
}

TEST_F(FramePacerTest, IdleWakeupStateTransition) {
    auto t0 = std::chrono::steady_clock::time_point(0s);
    pacer.reset(t0);

    // Simulate idle period (100ms timeout)
    auto idleWaitDuration = 100ms;
    auto tWake = t0 + idleWaitDuration;

    // In main.cpp: wasIdle = true, user interacts -> isIdle = false -> reset() is triggered!
    pacer.reset(tWake);

    // Active frame render work takes 4ms
    auto now = tWake + 4ms;
    std::chrono::steady_clock::time_point sleptUntil{};
    bool didSleep = pacer.pace(now, [&](std::chrono::steady_clock::time_point target) {
        sleptUntil = target;
    });

    // Pacer must properly sleep for the remainder of the 60 FPS frame (12.66ms)
    EXPECT_TRUE(didSleep);
    EXPECT_EQ(sleptUntil, tWake + pacer.getFrameDuration());
    EXPECT_EQ(sleptUntil - now, pacer.getFrameDuration() - 4ms);
}

TEST_F(FramePacerTest, LivePaceExecutionWithSteadyClock) {
    // Test the live parameterless pace() that uses real std::this_thread::sleep_until
    common::FramePacer livePacer(200.0); // 5ms per frame
    livePacer.reset();

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 3; ++i) {
        // Small simulated work (1ms)
        std::this_thread::sleep_for(1ms);
        livePacer.pace();
    }
    auto elapsed = std::chrono::steady_clock::now() - start;

    // 3 frames at 200 FPS = ~15ms total. Allow tolerance for OS scheduling.
    EXPECT_GE(elapsed, 12ms);
    EXPECT_LE(elapsed, 60ms);
}

