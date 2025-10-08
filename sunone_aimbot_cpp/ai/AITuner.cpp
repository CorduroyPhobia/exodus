#include "AITuner.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "AimbotTarget.h"

namespace {
constexpr float kMinFloatEpsilon = 1e-3f;

auto clampValue(float value, float minValue, float maxValue) {
    return std::max(minValue, std::min(maxValue, value));
}

auto clampValue(int value, int minValue, int maxValue) {
    return std::max(minValue, std::min(maxValue, value));
}
}

AITuner::AITuner() {
    std::random_device rd;
    rng.seed(rd());

    state.minBounds = MouseSettings{400, 0.05f, 0.05f, 0.35f, 0.004f, 4.0f, 2.0f, 0.8f, 0.5f, false, 0.5f, 0.5f, 0.5f, 0.5f, false, 0.0f};
    state.maxBounds = MouseSettings{6400, 3.5f, 3.0f, 6.0f, 0.08f, 40.0f, 25.0f, 4.0f, 5.0f, true, 10.0f, 10.0f, 10.0f, 10.0f, true, 1.0f};

    state.bestReward = std::numeric_limits<double>::lowest();
    applyModeLocked(state.currentMode);

    workerThread = std::thread(&AITuner::trainingLoop, this);
}

AITuner::~AITuner() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopWorker = true;
        state.trainingActive = false;
        state.paused = false;
        condition.notify_all();
    }

    if (workerThread.joinable()) {
        workerThread.join();
    }
}

void AITuner::setAimMode(AimMode mode) {
    std::lock_guard<std::mutex> lock(mutex);
    applyModeLocked(mode);
}

void AITuner::setLearningRate(float rate) {
    std::lock_guard<std::mutex> lock(mutex);
    config.learningRate = std::max(rate, kMinFloatEpsilon);
}

void AITuner::setExplorationRate(float rate) {
    std::lock_guard<std::mutex> lock(mutex);
    config.explorationRate = std::max(rate, kMinFloatEpsilon);
}

void AITuner::setMaxIterations(int iterations) {
    std::lock_guard<std::mutex> lock(mutex);
    config.maxIterations = std::max(iterations, 1);
}

void AITuner::setTargetRadius(float radius) {
    std::lock_guard<std::mutex> lock(mutex);
    config.targetRadius = std::max(radius, kMinFloatEpsilon);
}

void AITuner::setAutoCalibrate(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    config.autoCalibrate = enabled;
    if (!enabled) {
        state.calibrating = false;
        state.calibrationStep = 0;
    }
}

void AITuner::setSettingsBounds(const MouseSettings& min, const MouseSettings& max) {
    std::lock_guard<std::mutex> lock(mutex);
    state.minBounds = min;
    state.maxBounds = max;
    state.currentSettings = clampSettings(state.currentSettings, state.minBounds, state.maxBounds);
    state.bestSettings = clampSettings(state.bestSettings, state.minBounds, state.maxBounds);
}

void AITuner::startTraining() {
    std::lock_guard<std::mutex> lock(mutex);
    if (state.trainingActive) {
        return;
    }

    state.trainingActive = true;
    state.paused = false;
    state.iteration = 0;
    state.totalReward = 0.0;
    state.lastReward = 0.0;
    state.totalCount = 0;
    state.successCount = 0;
    state.bestReward = std::numeric_limits<double>::lowest();
    state.bestSettings = state.currentSettings;
    feedbackQueue.clear();

    if (config.autoCalibrate) {
        state.calibrating = true;
        state.calibrationStep = 0;
        state.currentSettings = randomSettings(state.minBounds, state.maxBounds);
        state.bestSettings = state.currentSettings;
    } else {
        state.calibrating = false;
        state.calibrationStep = 0;
    }

    state.settingsHistory.clear();
    state.rewardHistory.clear();
    state.settingsHistory.push_back(state.currentSettings);

    condition.notify_all();
}

void AITuner::stopTraining() {
    std::lock_guard<std::mutex> lock(mutex);
    state.trainingActive = false;
    state.paused = false;
    state.calibrating = false;
    state.calibrationStep = 0;
    feedbackQueue.clear();
    condition.notify_all();
}

void AITuner::pauseTraining() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!state.trainingActive) {
        return;
    }
    state.paused = true;
}

void AITuner::resumeTraining() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!state.trainingActive) {
        return;
    }
    state.paused = false;
    condition.notify_all();
}

void AITuner::resetTraining() {
    std::lock_guard<std::mutex> lock(mutex);
    state.iteration = 0;
    state.lastReward = 0.0;
    state.totalReward = 0.0;
    state.bestReward = std::numeric_limits<double>::lowest();
    state.successCount = 0;
    state.totalCount = 0;
    state.calibrationStep = 0;
    state.calibrating = state.trainingActive && config.autoCalibrate;
    feedbackQueue.clear();
    state.rewardHistory.clear();
    state.settingsHistory.clear();
    if (state.calibrating) {
        state.currentSettings = randomSettings(state.minBounds, state.maxBounds);
    }
    state.bestSettings = state.currentSettings;
    state.settingsHistory.push_back(state.currentSettings);
    condition.notify_all();
}

void AITuner::provideFeedback(const AimbotTarget& target, double mouseX, double mouseY) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!state.trainingActive || state.paused) {
        return;
    }
    feedbackQueue.push_back({target, mouseX, mouseY});
    condition.notify_all();
}

void AITuner::startCalibration() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!state.trainingActive) {
        return;
    }
    state.calibrating = true;
    state.calibrationStep = 0;
    state.currentSettings = randomSettings(state.minBounds, state.maxBounds);
    state.bestSettings = state.currentSettings;
    state.bestReward = std::numeric_limits<double>::lowest();
    state.settingsHistory.push_back(state.currentSettings);
    if (state.settingsHistory.size() > 256) {
        state.settingsHistory.erase(state.settingsHistory.begin());
    }
    condition.notify_all();
}

void AITuner::stopCalibration() {
    std::lock_guard<std::mutex> lock(mutex);
    state.calibrating = false;
    state.calibrationStep = 0;
}

MouseSettings AITuner::getCurrentSettings() const {
    std::lock_guard<std::mutex> lock(mutex);
    return state.currentSettings;
}

AimMode AITuner::getCurrentMode() const {
    std::lock_guard<std::mutex> lock(mutex);
    return state.currentMode;
}

double AITuner::getCurrentReward() const {
    std::lock_guard<std::mutex> lock(mutex);
    return state.lastReward;
}

int AITuner::getCurrentIteration() const {
    std::lock_guard<std::mutex> lock(mutex);
    return state.iteration;
}

bool AITuner::isTraining() const {
    std::lock_guard<std::mutex> lock(mutex);
    return state.trainingActive;
}

bool AITuner::isCalibrating() const {
    std::lock_guard<std::mutex> lock(mutex);
    return state.calibrating;
}

double AITuner::getSuccessRate() const {
    std::lock_guard<std::mutex> lock(mutex);
    if (state.totalCount == 0) {
        return 0.0;
    }
    return static_cast<double>(state.successCount) / static_cast<double>(state.totalCount);
}

MouseSettings AITuner::getBestSettings() const {
    std::lock_guard<std::mutex> lock(mutex);
    return state.bestSettings;
}

AITuner::TrainingStats AITuner::getTrainingStats() const {
    std::lock_guard<std::mutex> lock(mutex);
    TrainingStats stats;
    stats.bestReward = state.bestReward;
    stats.currentIteration = state.iteration;
    stats.totalIterations = state.totalCount;
    stats.isTraining = state.trainingActive;
    stats.isCalibrating = state.calibrating;
    if (state.totalCount > 0) {
        stats.averageReward = state.totalReward / static_cast<double>(state.totalCount);
        stats.successRate = static_cast<double>(state.successCount) /
                            static_cast<double>(state.totalCount);
    }
    return stats;
}

MouseSettings AITuner::getModeSettings(AimMode mode) const {
    std::lock_guard<std::mutex> lock(mutex);
    MouseSettings defaults = defaultsForMode(mode);
    return clampSettings(defaults, state.minBounds, state.maxBounds);
}

void AITuner::applyModeSettings(AimMode mode) {
    std::lock_guard<std::mutex> lock(mutex);
    applyModeLocked(mode);
}

void AITuner::trainingLoop() {
    std::unique_lock<std::mutex> lock(mutex);
    while (!stopWorker) {
        condition.wait(lock, [&]() {
            return stopWorker ||
                   (state.trainingActive && !state.paused &&
                    !feedbackQueue.empty());
        });

        if (stopWorker) {
            break;
        }

        if (!state.trainingActive || state.paused) {
            continue;
        }

        if (feedbackQueue.empty()) {
            continue;
        }

        FeedbackEvent event = feedbackQueue.front();
        feedbackQueue.pop_front();

        double reward = calculateReward(event, config.targetRadius);
        bool hitTarget = reward > 0.0;

        state.iteration = std::min(state.iteration + 1, config.maxIterations);
        state.lastReward = reward;
        state.totalReward += reward;
        state.totalCount++;
        if (hitTarget) {
            state.successCount++;
        }

        state.rewardHistory.push_back(reward);
        if (state.rewardHistory.size() > 256) {
            state.rewardHistory.erase(state.rewardHistory.begin());
        }

        if (state.calibrating) {
            if (reward > state.bestReward) {
                state.bestReward = reward;
                state.bestSettings = state.currentSettings;
            }

            if (state.calibrationStep >= state.calibrationBudget) {
                state.calibrating = false;
                state.calibrationStep = 0;
                state.currentSettings = state.bestSettings;
            } else {
                state.calibrationStep++;
                state.currentSettings = randomSettings(state.minBounds, state.maxBounds);
                state.settingsHistory.push_back(state.currentSettings);
            }
        } else {
            if (reward > state.bestReward) {
                state.bestReward = reward;
                state.bestSettings = state.currentSettings;
            }

            MouseSettings baseline = (reward >= 0.0) ? state.currentSettings : state.bestSettings;
            float intensity = (reward >= 0.0) ? config.explorationRate * 0.5f : config.explorationRate;
            const float adjustment = 1.0f + static_cast<float>(std::abs(reward)) * config.learningRate;
            if (reward >= 0.0) {
                intensity = std::max(kMinFloatEpsilon, intensity / adjustment);
            } else {
                intensity = std::max(kMinFloatEpsilon, intensity * adjustment);
            }
            MouseSettings candidate = mutateSettings(baseline, state.minBounds, state.maxBounds, intensity);
            state.currentSettings = candidate;
            state.settingsHistory.push_back(state.currentSettings);
        }

        if (state.settingsHistory.size() > 256) {
            state.settingsHistory.erase(state.settingsHistory.begin());
        }
    }
}

MouseSettings AITuner::clampSettings(const MouseSettings& settings,
                                     const MouseSettings& minBounds,
                                     const MouseSettings& maxBounds) const {
    MouseSettings result = settings;
    result.dpi = clampValue(result.dpi, minBounds.dpi, maxBounds.dpi);
    result.sensitivity = clampValue(result.sensitivity, minBounds.sensitivity, maxBounds.sensitivity);
    result.minSpeedMultiplier = clampValue(result.minSpeedMultiplier, minBounds.minSpeedMultiplier, maxBounds.minSpeedMultiplier);
    result.maxSpeedMultiplier = clampValue(result.maxSpeedMultiplier, minBounds.maxSpeedMultiplier, maxBounds.maxSpeedMultiplier);
    result.predictionInterval = clampValue(result.predictionInterval, minBounds.predictionInterval, maxBounds.predictionInterval);
    result.snapRadius = clampValue(result.snapRadius, minBounds.snapRadius, maxBounds.snapRadius);
    result.nearRadius = clampValue(result.nearRadius, minBounds.nearRadius, maxBounds.nearRadius);
    result.speedCurveExponent = clampValue(result.speedCurveExponent, minBounds.speedCurveExponent, maxBounds.speedCurveExponent);
    result.snapBoostFactor = clampValue(result.snapBoostFactor, minBounds.snapBoostFactor, maxBounds.snapBoostFactor);
    result.wind_mouse_enabled = settings.wind_mouse_enabled && maxBounds.wind_mouse_enabled;
    result.wind_G = clampValue(result.wind_G, minBounds.wind_G, maxBounds.wind_G);
    result.wind_W = clampValue(result.wind_W, minBounds.wind_W, maxBounds.wind_W);
    result.wind_M = clampValue(result.wind_M, minBounds.wind_M, maxBounds.wind_M);
    result.wind_D = clampValue(result.wind_D, minBounds.wind_D, maxBounds.wind_D);
    result.easynorecoil = settings.easynorecoil && maxBounds.easynorecoil;
    result.easynorecoilstrength = clampValue(result.easynorecoilstrength, minBounds.easynorecoilstrength, maxBounds.easynorecoilstrength);
    return result;
}

MouseSettings AITuner::randomSettings(const MouseSettings& minBounds,
                                      const MouseSettings& maxBounds) {
    std::uniform_real_distribution<float> zeroOne(0.0f, 1.0f);
    MouseSettings result;
    result.dpi = clampValue(static_cast<int>(minBounds.dpi + (maxBounds.dpi - minBounds.dpi) * zeroOne(rng)), minBounds.dpi, maxBounds.dpi);
    result.sensitivity = minBounds.sensitivity + (maxBounds.sensitivity - minBounds.sensitivity) * zeroOne(rng);
    result.minSpeedMultiplier = minBounds.minSpeedMultiplier + (maxBounds.minSpeedMultiplier - minBounds.minSpeedMultiplier) * zeroOne(rng);
    result.maxSpeedMultiplier = minBounds.maxSpeedMultiplier + (maxBounds.maxSpeedMultiplier - minBounds.maxSpeedMultiplier) * zeroOne(rng);
    result.predictionInterval = minBounds.predictionInterval + (maxBounds.predictionInterval - minBounds.predictionInterval) * zeroOne(rng);
    result.snapRadius = minBounds.snapRadius + (maxBounds.snapRadius - minBounds.snapRadius) * zeroOne(rng);
    result.nearRadius = minBounds.nearRadius + (maxBounds.nearRadius - minBounds.nearRadius) * zeroOne(rng);
    result.speedCurveExponent = minBounds.speedCurveExponent + (maxBounds.speedCurveExponent - minBounds.speedCurveExponent) * zeroOne(rng);
    result.snapBoostFactor = minBounds.snapBoostFactor + (maxBounds.snapBoostFactor - minBounds.snapBoostFactor) * zeroOne(rng);
    result.wind_mouse_enabled = zeroOne(rng) > 0.5f && maxBounds.wind_mouse_enabled;
    result.wind_G = minBounds.wind_G + (maxBounds.wind_G - minBounds.wind_G) * zeroOne(rng);
    result.wind_W = minBounds.wind_W + (maxBounds.wind_W - minBounds.wind_W) * zeroOne(rng);
    result.wind_M = minBounds.wind_M + (maxBounds.wind_M - minBounds.wind_M) * zeroOne(rng);
    result.wind_D = minBounds.wind_D + (maxBounds.wind_D - minBounds.wind_D) * zeroOne(rng);
    result.easynorecoil = zeroOne(rng) > 0.5f && maxBounds.easynorecoil;
    result.easynorecoilstrength = minBounds.easynorecoilstrength + (maxBounds.easynorecoilstrength - minBounds.easynorecoilstrength) * zeroOne(rng);
    return clampSettings(result, minBounds, maxBounds);
}

MouseSettings AITuner::mutateSettings(const MouseSettings& base,
                                      const MouseSettings& minBounds,
                                      const MouseSettings& maxBounds,
                                      float intensity) {
    std::normal_distribution<float> noise(0.0f, intensity);
    MouseSettings candidate = base;
    candidate.dpi = clampValue(static_cast<int>(std::round(candidate.dpi + candidate.dpi * noise(rng) * 0.1f)), minBounds.dpi, maxBounds.dpi);
    candidate.sensitivity = clampValue(candidate.sensitivity + noise(rng), minBounds.sensitivity, maxBounds.sensitivity);
    candidate.minSpeedMultiplier = clampValue(candidate.minSpeedMultiplier + noise(rng), minBounds.minSpeedMultiplier, maxBounds.minSpeedMultiplier);
    candidate.maxSpeedMultiplier = clampValue(candidate.maxSpeedMultiplier + noise(rng), minBounds.maxSpeedMultiplier, maxBounds.maxSpeedMultiplier);
    candidate.predictionInterval = clampValue(candidate.predictionInterval + noise(rng) * 0.01f, minBounds.predictionInterval, maxBounds.predictionInterval);
    candidate.snapRadius = clampValue(candidate.snapRadius + noise(rng) * 2.0f, minBounds.snapRadius, maxBounds.snapRadius);
    candidate.nearRadius = clampValue(candidate.nearRadius + noise(rng) * 2.0f, minBounds.nearRadius, maxBounds.nearRadius);
    candidate.speedCurveExponent = clampValue(candidate.speedCurveExponent + noise(rng) * 0.2f, minBounds.speedCurveExponent, maxBounds.speedCurveExponent);
    candidate.snapBoostFactor = clampValue(candidate.snapBoostFactor + noise(rng) * 0.5f, minBounds.snapBoostFactor, maxBounds.snapBoostFactor);
    candidate.wind_G = clampValue(candidate.wind_G + noise(rng), minBounds.wind_G, maxBounds.wind_G);
    candidate.wind_W = clampValue(candidate.wind_W + noise(rng), minBounds.wind_W, maxBounds.wind_W);
    candidate.wind_M = clampValue(candidate.wind_M + noise(rng), minBounds.wind_M, maxBounds.wind_M);
    candidate.wind_D = clampValue(candidate.wind_D + noise(rng), minBounds.wind_D, maxBounds.wind_D);
    candidate.easynorecoilstrength = clampValue(candidate.easynorecoilstrength + noise(rng) * 0.25f, minBounds.easynorecoilstrength, maxBounds.easynorecoilstrength);
    return clampSettings(candidate, minBounds, maxBounds);
}

double AITuner::calculateReward(const FeedbackEvent& event, float radius) const {
    double pivotX = event.target.pivotX;
    double pivotY = event.target.pivotY;
    if (pivotX == 0.0 && pivotY == 0.0) {
        pivotX = static_cast<double>(event.target.x) + static_cast<double>(event.target.w) * 0.5;
        pivotY = static_cast<double>(event.target.y) + static_cast<double>(event.target.h) * 0.5;
    }

    const double dx = pivotX - event.mouseX;
    const double dy = pivotY - event.mouseY;
    const double distance = std::sqrt(dx * dx + dy * dy);
    const double clampedRadius = std::max(static_cast<double>(radius), 1.0);

    if (distance >= clampedRadius) {
        return -distance / clampedRadius;
    }

    return 1.0 - (distance / clampedRadius);
}

MouseSettings AITuner::defaultsForMode(AimMode mode) const {
    switch (mode) {
        case AimMode::AIM_ASSIST:
            return MouseSettings{800, 1.2f, 0.25f, 2.0f, 0.016f, 10.0f, 5.0f, 1.2f, 1.0f, false, 1.5f, 1.5f, 1.5f, 1.5f, false, 0.0f};
        case AimMode::AIM_BOT:
            return MouseSettings{1200, 1.6f, 0.4f, 3.0f, 0.012f, 14.0f, 8.0f, 1.5f, 1.4f, true, 3.0f, 3.0f, 3.0f, 3.0f, true, 0.25f};
        case AimMode::RAGE_BAITER:
        default:
            return MouseSettings{1600, 2.0f, 0.8f, 4.0f, 0.010f, 18.0f, 10.0f, 2.0f, 2.0f, true, 5.0f, 5.0f, 5.0f, 5.0f, true, 0.4f};
    }
}

void AITuner::applyModeLocked(AimMode mode) {
    state.currentMode = mode;
    MouseSettings defaults = defaultsForMode(mode);
    state.currentSettings = clampSettings(defaults, state.minBounds, state.maxBounds);
    state.bestSettings = state.currentSettings;
    state.bestReward = std::numeric_limits<double>::lowest();
    state.settingsHistory.clear();
    state.rewardHistory.clear();
    state.settingsHistory.push_back(state.currentSettings);
}
