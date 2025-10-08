#include "AITuner.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr std::size_t kMaxHistory = 256;
constexpr std::size_t kMaxFeedbackQueue = 512;
}

AITuner::AITuner()
    : stopRequested(false),
      rng(std::random_device{}()) {
    std::lock_guard<std::mutex> lock(stateMutex);

    shared.minSettings = MouseSettings(400, 0.1f, 0.01f, 0.01f, 0.001f, 0.5f, 5.0f,
                                       1.0f, 1.0f, false, 10.0f, 5.0f, 5.0f, 3.0f,
                                       false, 0.0f);
    shared.maxSettings = MouseSettings(16000, 5.0f, 2.0f, 2.0f, 0.1f, 5.0f, 100.0f,
                                       10.0f, 3.0f, true, 50.0f, 30.0f, 20.0f, 15.0f,
                                       true, 2.0f);

    applyModeSettingsInternal(shared.currentMode, shared);
}

AITuner::~AITuner() {
    stopTraining();
}

void AITuner::setAimMode(AimMode mode) {
    std::lock_guard<std::mutex> lock(stateMutex);
    shared.currentMode = mode;
    applyModeSettingsInternal(mode, shared);
}

void AITuner::setLearningRate(float rate) {
    std::lock_guard<std::mutex> lock(stateMutex);
    shared.learningRate = std::clamp(rate, 0.001f, 0.5f);
}

void AITuner::setExplorationRate(float rate) {
    std::lock_guard<std::mutex> lock(stateMutex);
    shared.explorationRate = std::clamp(rate, 0.01f, 1.0f);
}

void AITuner::setMaxIterations(int iterations) {
    std::lock_guard<std::mutex> lock(stateMutex);
    shared.maxIterations = std::max(1, iterations);
}

void AITuner::setTargetRadius(float radius) {
    std::lock_guard<std::mutex> lock(stateMutex);
    shared.targetRadius = std::clamp(radius, 1.0f, 50.0f);
}

void AITuner::setAutoCalibrate(bool enabled) {
    bool shouldStartCalibration = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        shared.autoCalibrate = enabled;
        shouldStartCalibration = enabled && shared.trainingActive && !shared.calibrating;
        if (!enabled && shared.calibrating) {
            shared.calibrating = false;
            shared.calibrationStep = 0;
        }
    }

    if (shouldStartCalibration) {
        startCalibration();
    }
}

void AITuner::setSettingsBounds(const MouseSettings& min, const MouseSettings& max) {
    std::lock_guard<std::mutex> lock(stateMutex);
    shared.minSettings = min;
    shared.maxSettings = max;
    shared.currentSettings = clampSettings(shared.currentSettings, shared.minSettings, shared.maxSettings);
    shared.bestSettings = clampSettings(shared.bestSettings, shared.minSettings, shared.maxSettings);
}

void AITuner::startTraining() {
    bool joinPrevious = false;
    bool launchThread = false;
    bool startCalibrationNow = false;

    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (!shared.threadRunning && trainingThread.joinable()) {
            joinPrevious = true;
        }
    }

    if (joinPrevious) {
        trainingThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (!shared.threadRunning) {
            shared.trainingActive = true;
            shared.paused = false;
            shared.threadRunning = true;
            stopRequested.store(false);
            launchThread = true;
        } else {
            shared.trainingActive = true;
            shared.paused = false;
        }
        startCalibrationNow = shared.autoCalibrate && !shared.calibrating;
    }

    if (launchThread) {
        try {
            trainingThread = std::thread(&AITuner::trainingLoop, this);
        } catch (...) {
            std::lock_guard<std::mutex> lock(stateMutex);
            shared.threadRunning = false;
            shared.trainingActive = false;
            throw;
        }
    }

    workAvailable.notify_all();

    if (startCalibrationNow) {
        startCalibration();
    }
}

void AITuner::stopTraining() {
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        shared.trainingActive = false;
        shared.paused = false;
        shared.calibrating = false;
        shared.calibrationStep = 0;
    }

    stopRequested.store(true);
    workAvailable.notify_all();

    if (trainingThread.joinable()) {
        trainingThread.join();
    }

    stopRequested.store(false);

    std::lock_guard<std::mutex> lock(stateMutex);
    shared.threadRunning = false;
    feedbackQueue.clear();
}

void AITuner::pauseTraining() {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (shared.threadRunning) {
        shared.paused = true;
    }
}

void AITuner::resumeTraining() {
    bool needStart = false;
    bool startCalibrationNow = false;

    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (!shared.threadRunning) {
            needStart = true;
        } else {
            shared.paused = false;
            shared.trainingActive = true;
            startCalibrationNow = shared.autoCalibrate && !shared.calibrating;
        }
    }

    if (needStart) {
        startTraining();
        return;
    }

    workAvailable.notify_all();

    if (startCalibrationNow) {
        startCalibration();
    }
}

void AITuner::resetTraining() {
    stopTraining();

    std::lock_guard<std::mutex> lock(stateMutex);
    shared.iteration = 0;
    shared.totalReward = 0.0;
    shared.totalTargets = 0;
    shared.successfulTargets = 0;
    shared.lastReward = 0.0;
    shared.rewardHistory.clear();
    shared.settingsHistory.clear();
    shared.calibrating = false;
    shared.calibrationStep = 0;
    applyModeSettingsInternal(shared.currentMode, shared);
}

void AITuner::provideFeedback(const AimbotTarget& target, double mouseX, double mouseY) {
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (feedbackQueue.size() >= kMaxFeedbackQueue) {
            feedbackQueue.pop_front();
        }
        feedbackQueue.push_back({target, mouseX, mouseY});
    }

    workAvailable.notify_one();
}

void AITuner::startCalibration() {
    std::lock_guard<std::mutex> lock(stateMutex);
    shared.calibrating = true;
    shared.calibrationStep = 0;

    MouseSettings baseline = shared.minSettings;
    MouseSettings modeDefaults = clampSettings(getModeSettings(shared.currentMode), shared.minSettings, shared.maxSettings);

    baseline.wind_mouse_enabled = modeDefaults.wind_mouse_enabled;
    baseline.easynorecoil = modeDefaults.easynorecoil;

    shared.currentSettings = clampSettings(baseline, shared.minSettings, shared.maxSettings);
    shared.settingsHistory.push_back(shared.currentSettings);
    if (shared.settingsHistory.size() > kMaxHistory) {
        shared.settingsHistory.erase(shared.settingsHistory.begin());
    }
    shared.rewardHistory.push_back(0.0);
    if (shared.rewardHistory.size() > kMaxHistory) {
        shared.rewardHistory.erase(shared.rewardHistory.begin());
    }
}

void AITuner::stopCalibration() {
    std::lock_guard<std::mutex> lock(stateMutex);
    shared.calibrating = false;
    shared.calibrationStep = 0;
}

MouseSettings AITuner::getCurrentSettings() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return shared.currentSettings;
}

AimMode AITuner::getCurrentMode() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return shared.currentMode;
}

double AITuner::getCurrentReward() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return shared.lastReward;
}

int AITuner::getCurrentIteration() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return shared.iteration;
}

bool AITuner::isTraining() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return shared.trainingActive && !shared.paused;
}

bool AITuner::isCalibrating() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return shared.calibrating;
}

double AITuner::getSuccessRate() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return shared.totalTargets > 0 ? static_cast<double>(shared.successfulTargets) / shared.totalTargets : 0.0;
}

MouseSettings AITuner::getBestSettings() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (std::isfinite(shared.bestReward)) {
        return shared.bestSettings;
    }
    return shared.currentSettings;
}

AITuner::TrainingStats AITuner::getTrainingStats() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    TrainingStats stats;
    stats.currentIteration = shared.iteration;
    stats.totalIterations = shared.maxIterations;
    stats.isTraining = shared.trainingActive && !shared.paused;
    stats.isCalibrating = shared.calibrating;
    stats.successRate = shared.totalTargets > 0
        ? static_cast<double>(shared.successfulTargets) / shared.totalTargets
        : 0.0;
    if (!shared.rewardHistory.empty()) {
        double sum = 0.0;
        for (double value : shared.rewardHistory) {
            sum += value;
        }
        stats.averageReward = sum / static_cast<double>(shared.rewardHistory.size());
    }
    if (std::isfinite(shared.bestReward) && shared.bestReward > -std::numeric_limits<double>::infinity()) {
        stats.bestReward = shared.bestReward;
    }
    return stats;
}

MouseSettings AITuner::getModeSettings(AimMode mode) const {
    switch (mode) {
        case AimMode::AIM_ASSIST:
            return MouseSettings(800, 1.0f, 0.1f, 0.3f, 0.01f, 2.0f, 20.0f, 2.0f, 1.1f,
                                 true, 15.0f, 8.0f, 8.0f, 6.0f, false, 0.1f);
        case AimMode::AIM_BOT:
            return MouseSettings(1200, 1.5f, 0.2f, 0.8f, 0.02f, 1.5f, 15.0f, 2.5f, 1.2f,
                                 true, 25.0f, 12.0f, 12.0f, 8.0f, true, 0.3f);
        case AimMode::RAGE_BAITER:
        default:
            return MouseSettings(1600, 2.0f, 0.5f, 1.5f, 0.03f, 1.0f, 10.0f, 3.0f, 1.5f,
                                 true, 40.0f, 20.0f, 15.0f, 10.0f, true, 0.5f);
    }
}

void AITuner::applyModeSettings(AimMode mode) {
    std::lock_guard<std::mutex> lock(stateMutex);
    shared.currentMode = mode;
    applyModeSettingsInternal(mode, shared);
}

void AITuner::trainingLoop() {
    while (true) {
        FeedbackEvent event;
        SharedState snapshot;

        {
            std::unique_lock<std::mutex> lock(stateMutex);
            workAvailable.wait(lock, [this] {
                return stopRequested.load() ||
                       (!shared.paused && shared.trainingActive && !feedbackQueue.empty());
            });

            if (stopRequested.load()) {
                break;
            }

            if (feedbackQueue.empty() || !shared.trainingActive || shared.paused) {
                continue;
            }

            event = feedbackQueue.front();
            feedbackQueue.pop_front();
            snapshot = shared;
        }

        RewardSample sample = calculateReward(event.target, event.mouseX, event.mouseY, snapshot.targetRadius);

        bool calibrationFinished = false;
        MouseSettings nextSettings;
        if (snapshot.calibrating) {
            nextSettings = performCalibrationStep(snapshot, sample, calibrationFinished);
        } else {
            nextSettings = performTrainingStep(snapshot, sample);
        }
        nextSettings = clampSettings(nextSettings, snapshot.minSettings, snapshot.maxSettings);

        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (!shared.trainingActive) {
                if (calibrationFinished && shared.calibrating) {
                    shared.calibrating = false;
                    shared.calibrationStep = 0;
                }
                continue;
            }

            if (shared.calibrating && !snapshot.calibrating) {
                // A new calibration began while we processed this sample; keep the
                // baseline provided by the caller and wait for fresh feedback.
                continue;
            }

            shared.currentSettings = nextSettings;
            shared.lastReward = sample.reward;
            shared.iteration++;
            shared.lastUpdate = std::chrono::steady_clock::now();
            shared.totalReward += sample.reward;
            shared.totalTargets++;
            if (sample.targetHit) {
                shared.successfulTargets++;
            }

            if (shared.rewardHistory.size() >= kMaxHistory) {
                shared.rewardHistory.erase(shared.rewardHistory.begin());
            }
            shared.rewardHistory.push_back(sample.reward);

            if (shared.settingsHistory.size() >= kMaxHistory) {
                shared.settingsHistory.erase(shared.settingsHistory.begin());
            }
            shared.settingsHistory.push_back(nextSettings);

            if (!std::isfinite(shared.bestReward) || sample.reward > shared.bestReward) {
                shared.bestReward = sample.reward;
                shared.bestSettings = nextSettings;
            }

            if (snapshot.calibrating && shared.calibrating) {
                shared.calibrationStep = snapshot.calibrationStep + 1;
                if (calibrationFinished || shared.calibrationStep >= shared.calibrationMaxSteps) {
                    shared.calibrating = false;
                    shared.calibrationStep = 0;
                }
            }

            if (shared.iteration >= shared.maxIterations) {
                shared.trainingActive = false;
            }
        }
    }

    std::lock_guard<std::mutex> lock(stateMutex);
    shared.threadRunning = false;
    shared.trainingActive = false;
    shared.paused = false;
    shared.calibrating = false;
    shared.calibrationStep = 0;
}

void AITuner::applyModeSettingsInternal(AimMode mode, SharedState& state) {
    MouseSettings defaults = clampSettings(getModeSettings(mode), state.minSettings, state.maxSettings);
    state.currentSettings = defaults;
    state.bestSettings = defaults;
    state.bestReward = -std::numeric_limits<double>::infinity();
    state.lastReward = 0.0;
    state.iteration = 0;
    state.calibrating = false;
    state.calibrationStep = 0;
    state.totalReward = 0.0;
    state.totalTargets = 0;
    state.successfulTargets = 0;
    state.settingsHistory.clear();
    state.rewardHistory.clear();
    state.settingsHistory.push_back(defaults);
    state.rewardHistory.push_back(0.0);
    state.lastUpdate = std::chrono::steady_clock::now();
}

MouseSettings AITuner::clampSettings(const MouseSettings& settings,
                                     const MouseSettings& minBounds,
                                     const MouseSettings& maxBounds) const {
    MouseSettings clamped = settings;
    clamped.dpi = std::clamp(clamped.dpi, minBounds.dpi, maxBounds.dpi);
    clamped.sensitivity = std::clamp(clamped.sensitivity, minBounds.sensitivity, maxBounds.sensitivity);
    clamped.minSpeedMultiplier = std::clamp(clamped.minSpeedMultiplier, minBounds.minSpeedMultiplier, maxBounds.minSpeedMultiplier);
    clamped.maxSpeedMultiplier = std::clamp(clamped.maxSpeedMultiplier, minBounds.maxSpeedMultiplier, maxBounds.maxSpeedMultiplier);
    clamped.predictionInterval = std::clamp(clamped.predictionInterval, minBounds.predictionInterval, maxBounds.predictionInterval);
    clamped.snapRadius = std::clamp(clamped.snapRadius, minBounds.snapRadius, maxBounds.snapRadius);
    clamped.nearRadius = std::clamp(clamped.nearRadius, minBounds.nearRadius, maxBounds.nearRadius);
    clamped.speedCurveExponent = std::clamp(clamped.speedCurveExponent, minBounds.speedCurveExponent, maxBounds.speedCurveExponent);
    clamped.snapBoostFactor = std::clamp(clamped.snapBoostFactor, minBounds.snapBoostFactor, maxBounds.snapBoostFactor);
    clamped.wind_G = std::clamp(clamped.wind_G, minBounds.wind_G, maxBounds.wind_G);
    clamped.wind_W = std::clamp(clamped.wind_W, minBounds.wind_W, maxBounds.wind_W);
    clamped.wind_M = std::clamp(clamped.wind_M, minBounds.wind_M, maxBounds.wind_M);
    clamped.wind_D = std::clamp(clamped.wind_D, minBounds.wind_D, maxBounds.wind_D);
    clamped.easynorecoilstrength = std::clamp(clamped.easynorecoilstrength,
                                              minBounds.easynorecoilstrength,
                                              maxBounds.easynorecoilstrength);
    return clamped;
}

MouseSettings AITuner::generateRandomSettings(const MouseSettings& minBounds,
                                              const MouseSettings& maxBounds) {
    std::uniform_real_distribution<float> random01(0.0f, 1.0f);
    MouseSettings settings;
    settings.dpi = std::uniform_int_distribution<int>(minBounds.dpi, maxBounds.dpi)(rng);
    settings.sensitivity = std::uniform_real_distribution<float>(minBounds.sensitivity, maxBounds.sensitivity)(rng);
    settings.minSpeedMultiplier = std::uniform_real_distribution<float>(minBounds.minSpeedMultiplier, maxBounds.minSpeedMultiplier)(rng);
    settings.maxSpeedMultiplier = std::uniform_real_distribution<float>(minBounds.maxSpeedMultiplier, maxBounds.maxSpeedMultiplier)(rng);
    settings.predictionInterval = std::uniform_real_distribution<float>(minBounds.predictionInterval, maxBounds.predictionInterval)(rng);
    settings.snapRadius = std::uniform_real_distribution<float>(minBounds.snapRadius, maxBounds.snapRadius)(rng);
    settings.nearRadius = std::uniform_real_distribution<float>(minBounds.nearRadius, maxBounds.nearRadius)(rng);
    settings.speedCurveExponent = std::uniform_real_distribution<float>(minBounds.speedCurveExponent, maxBounds.speedCurveExponent)(rng);
    settings.snapBoostFactor = std::uniform_real_distribution<float>(minBounds.snapBoostFactor, maxBounds.snapBoostFactor)(rng);
    settings.wind_mouse_enabled = random01(rng) < 0.5f;
    settings.wind_G = std::uniform_real_distribution<float>(minBounds.wind_G, maxBounds.wind_G)(rng);
    settings.wind_W = std::uniform_real_distribution<float>(minBounds.wind_W, maxBounds.wind_W)(rng);
    settings.wind_M = std::uniform_real_distribution<float>(minBounds.wind_M, maxBounds.wind_M)(rng);
    settings.wind_D = std::uniform_real_distribution<float>(minBounds.wind_D, maxBounds.wind_D)(rng);
    settings.easynorecoil = random01(rng) < 0.5f;
    settings.easynorecoilstrength = std::uniform_real_distribution<float>(minBounds.easynorecoilstrength, maxBounds.easynorecoilstrength)(rng);
    return settings;
}

MouseSettings AITuner::mutateSettings(const MouseSettings& base,
                                      const MouseSettings& minBounds,
                                      const MouseSettings& maxBounds,
                                      float intensity) {
    std::uniform_real_distribution<float> randomNegPos(-1.0f, 1.0f);
    std::uniform_real_distribution<float> random01(0.0f, 1.0f);
    MouseSettings mutated = base;
    float scale = std::clamp(intensity, 0.01f, 1.0f);

    auto mutateFloat = [&](float value, float minValue, float maxValue, float span) {
        float delta = randomNegPos(rng) * span * scale;
        return std::clamp(value + delta, minValue, maxValue);
    };

    mutated.dpi = std::clamp(base.dpi + static_cast<int>(randomNegPos(rng) * 400.0f * scale),
                             minBounds.dpi, maxBounds.dpi);
    mutated.sensitivity = mutateFloat(base.sensitivity, minBounds.sensitivity, maxBounds.sensitivity,
                                      std::max(0.05f, base.sensitivity * 0.2f + 0.05f));
    mutated.minSpeedMultiplier = mutateFloat(base.minSpeedMultiplier, minBounds.minSpeedMultiplier, maxBounds.minSpeedMultiplier,
                                             std::max(0.02f, base.minSpeedMultiplier * 0.1f + 0.01f));
    mutated.maxSpeedMultiplier = mutateFloat(base.maxSpeedMultiplier, minBounds.maxSpeedMultiplier, maxBounds.maxSpeedMultiplier,
                                             std::max(0.02f, base.maxSpeedMultiplier * 0.1f + 0.01f));
    mutated.predictionInterval = mutateFloat(base.predictionInterval, minBounds.predictionInterval, maxBounds.predictionInterval,
                                             std::max(0.001f, base.predictionInterval * 0.2f + 0.001f));
    mutated.snapRadius = mutateFloat(base.snapRadius, minBounds.snapRadius, maxBounds.snapRadius,
                                     std::max(0.1f, base.snapRadius * 0.1f + 0.05f));
    mutated.nearRadius = mutateFloat(base.nearRadius, minBounds.nearRadius, maxBounds.nearRadius,
                                     std::max(0.5f, base.nearRadius * 0.1f + 0.2f));
    mutated.speedCurveExponent = mutateFloat(base.speedCurveExponent, minBounds.speedCurveExponent, maxBounds.speedCurveExponent,
                                             std::max(0.05f, base.speedCurveExponent * 0.2f + 0.05f));
    mutated.snapBoostFactor = mutateFloat(base.snapBoostFactor, minBounds.snapBoostFactor, maxBounds.snapBoostFactor,
                                          std::max(0.05f, base.snapBoostFactor * 0.2f + 0.05f));
    if (random01(rng) < 0.1f * scale) {
        mutated.wind_mouse_enabled = !base.wind_mouse_enabled;
    }
    mutated.wind_G = mutateFloat(base.wind_G, minBounds.wind_G, maxBounds.wind_G,
                                 std::max(0.5f, base.wind_G * 0.1f + 0.2f));
    mutated.wind_W = mutateFloat(base.wind_W, minBounds.wind_W, maxBounds.wind_W,
                                 std::max(0.5f, base.wind_W * 0.1f + 0.2f));
    mutated.wind_M = mutateFloat(base.wind_M, minBounds.wind_M, maxBounds.wind_M,
                                 std::max(0.5f, base.wind_M * 0.1f + 0.2f));
    mutated.wind_D = mutateFloat(base.wind_D, minBounds.wind_D, maxBounds.wind_D,
                                 std::max(0.5f, base.wind_D * 0.1f + 0.2f));
    if (random01(rng) < 0.05f * scale) {
        mutated.easynorecoil = !base.easynorecoil;
    }
    mutated.easynorecoilstrength = mutateFloat(base.easynorecoilstrength,
                                               minBounds.easynorecoilstrength,
                                               maxBounds.easynorecoilstrength,
                                               std::max(0.05f, base.easynorecoilstrength * 0.2f + 0.05f));
    return mutated;
}

AITuner::RewardSample AITuner::calculateReward(const AimbotTarget& target,
                                               double mouseX,
                                               double mouseY,
                                               double radius) const {
    double targetCenterX = target.x + target.w / 2.0;
    double targetCenterY = target.y + target.h / 2.0;

    double dx = mouseX - targetCenterX;
    double dy = mouseY - targetCenterY;
    double distance = std::sqrt(dx * dx + dy * dy);

    bool targetHit = distance <= radius;
    double reward = 0.0;

    if (targetHit) {
        reward = 1.0 - (distance / std::max(radius, 0.001));
    } else if (distance <= radius * 2.0) {
        reward = 0.5 - ((distance - radius) / std::max(radius * 2.0, 0.001)) * 0.3;
    } else {
        reward = -std::min(distance / std::max(radius * 10.0, 0.001), 1.0);
    }

    return {std::clamp(reward, -1.0, 1.0), targetHit};
}

MouseSettings AITuner::interpolateSettings(const MouseSettings& a,
                                           const MouseSettings& b,
                                           float t) const {
    float clampedT = std::clamp(t, 0.0f, 1.0f);
    MouseSettings result;
    result.dpi = static_cast<int>(a.dpi + (b.dpi - a.dpi) * clampedT);
    result.sensitivity = a.sensitivity + (b.sensitivity - a.sensitivity) * clampedT;
    result.minSpeedMultiplier = a.minSpeedMultiplier + (b.minSpeedMultiplier - a.minSpeedMultiplier) * clampedT;
    result.maxSpeedMultiplier = a.maxSpeedMultiplier + (b.maxSpeedMultiplier - a.maxSpeedMultiplier) * clampedT;
    result.predictionInterval = a.predictionInterval + (b.predictionInterval - a.predictionInterval) * clampedT;
    result.snapRadius = a.snapRadius + (b.snapRadius - a.snapRadius) * clampedT;
    result.nearRadius = a.nearRadius + (b.nearRadius - a.nearRadius) * clampedT;
    result.speedCurveExponent = a.speedCurveExponent + (b.speedCurveExponent - a.speedCurveExponent) * clampedT;
    result.snapBoostFactor = a.snapBoostFactor + (b.snapBoostFactor - a.snapBoostFactor) * clampedT;
    result.wind_mouse_enabled = clampedT > 0.5f ? b.wind_mouse_enabled : a.wind_mouse_enabled;
    result.wind_G = a.wind_G + (b.wind_G - a.wind_G) * clampedT;
    result.wind_W = a.wind_W + (b.wind_W - a.wind_W) * clampedT;
    result.wind_M = a.wind_M + (b.wind_M - a.wind_M) * clampedT;
    result.wind_D = a.wind_D + (b.wind_D - a.wind_D) * clampedT;
    result.easynorecoil = clampedT > 0.5f ? b.easynorecoil : a.easynorecoil;
    result.easynorecoilstrength = a.easynorecoilstrength + (b.easynorecoilstrength - a.easynorecoilstrength) * clampedT;
    return result;
}

MouseSettings AITuner::performCalibrationStep(const SharedState& snapshot,
                                              const RewardSample& sample,
                                              bool& finished) {
    MouseSettings target = clampSettings(getModeSettings(snapshot.currentMode),
                                         snapshot.minSettings,
                                         snapshot.maxSettings);
    float progress = static_cast<float>(snapshot.calibrationStep + 1) /
                     static_cast<float>(std::max(1, snapshot.calibrationMaxSteps));
    float rewardFactor = static_cast<float>(std::clamp(sample.reward, 0.0, 1.0));
    float blend = std::clamp(progress * 0.75f + rewardFactor * 0.5f, 0.0f, 1.0f);

    if (sample.targetHit || sample.reward >= 0.85) {
        finished = true;
        blend = 1.0f;
    } else if (snapshot.calibrationStep + 1 >= snapshot.calibrationMaxSteps) {
        finished = true;
    } else {
        finished = false;
    }

    return interpolateSettings(snapshot.minSettings, target, blend);
}

MouseSettings AITuner::performTrainingStep(const SharedState& snapshot,
                                           const RewardSample& sample) {
    std::uniform_real_distribution<float> random01(0.0f, 1.0f);
    float exploreRoll = random01(rng);
    if (exploreRoll < snapshot.explorationRate) {
        return generateRandomSettings(snapshot.minSettings, snapshot.maxSettings);
    }

    MouseSettings candidate = mutateSettings(snapshot.currentSettings,
                                             snapshot.minSettings,
                                             snapshot.maxSettings,
                                             snapshot.learningRate);

    if (std::isfinite(snapshot.bestReward) && snapshot.bestReward > -std::numeric_limits<double>::infinity()) {
        float improvement = static_cast<float>(std::clamp(sample.reward, 0.0, 1.0));
        float blend = std::clamp(snapshot.learningRate * 0.5f + improvement * 0.25f, 0.0f, 1.0f);
        candidate = interpolateSettings(candidate, snapshot.bestSettings, blend);
    }

    return candidate;
}
