#include "AITuner.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

#include "AimbotTarget.h"

namespace {
constexpr float kMinFloatEpsilon = 1e-3f;
constexpr std::size_t kMaxHistoryEntries = 256;
constexpr double kRewardTolerance = 1e-4;

template <typename T>
T clampValue(T value, T minValue, T maxValue) {
    return std::max(minValue, std::min(maxValue, value));
}

bool settingsApproximatelyEqual(const MouseSettings& a, const MouseSettings& b) {
    auto almostEqual = [](float lhs, float rhs) {
        return std::fabs(lhs - rhs) <= 1e-3f;
    };

    return a.dpi == b.dpi &&
           almostEqual(a.sensitivity, b.sensitivity) &&
           almostEqual(a.minSpeedMultiplier, b.minSpeedMultiplier) &&
           almostEqual(a.maxSpeedMultiplier, b.maxSpeedMultiplier) &&
           almostEqual(a.predictionInterval, b.predictionInterval) &&
           almostEqual(a.snapRadius, b.snapRadius) &&
           almostEqual(a.nearRadius, b.nearRadius) &&
           almostEqual(a.speedCurveExponent, b.speedCurveExponent) &&
           almostEqual(a.snapBoostFactor, b.snapBoostFactor) &&
           a.wind_mouse_enabled == b.wind_mouse_enabled &&
           almostEqual(a.wind_G, b.wind_G) &&
           almostEqual(a.wind_W, b.wind_W) &&
           almostEqual(a.wind_M, b.wind_M) &&
           almostEqual(a.wind_D, b.wind_D) &&
           a.easynorecoil == b.easynorecoil &&
           almostEqual(a.easynorecoilstrength, b.easynorecoilstrength);
}
}

AITuner::AITuner() {
    std::random_device rd;
    rng.seed(rd());

    state.minBounds = MouseSettings{400, 0.05f, 0.05f, 0.35f, 0.004f, 4.0f, 2.0f, 0.8f, 0.5f,
                                    false, 0.5f, 0.5f, 0.5f, 0.5f, false, 0.0f};
    state.maxBounds = MouseSettings{6400, 3.5f, 3.0f, 6.0f, 0.08f, 40.0f, 25.0f, 4.0f, 5.0f,
                                    true, 10.0f, 10.0f, 10.0f, 10.0f, true, 1.0f};

    state.minBounds = sanitizeSettings(state.minBounds, state.minBounds, state.maxBounds);
    state.maxBounds = sanitizeSettings(state.maxBounds, state.minBounds, state.maxBounds);

    state.bestReward = std::numeric_limits<double>::lowest();
    state.totalIterations = config.maxIterations;
    applyModeLocked(state.currentMode);
}

AITuner::~AITuner() = default;

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
    state.totalIterations = config.maxIterations;
    if (state.iteration > state.totalIterations) {
        state.iteration = state.totalIterations;
    }
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
        state.calibrationIndex = 0;
        state.calibrationSchedule.clear();
    } else if (state.trainingActive) {
        state.calibrating = true;
        state.bestReward = std::numeric_limits<double>::lowest();
        state.bestSettings = state.currentSettings;
        rebuildCalibrationScheduleLocked();
        if (!state.calibrationSchedule.empty()) {
            state.calibrationIndex = 0;
            beginEvaluationLocked(state.calibrationSchedule.front());
        }
    }
}

void AITuner::setSettingsBounds(const MouseSettings& min, const MouseSettings& max) {
    std::lock_guard<std::mutex> lock(mutex);

    MouseSettings minBounds = min;
    MouseSettings maxBounds = max;
    if (minBounds.dpi > maxBounds.dpi) std::swap(minBounds.dpi, maxBounds.dpi);
    if (minBounds.sensitivity > maxBounds.sensitivity) std::swap(minBounds.sensitivity, maxBounds.sensitivity);
    if (minBounds.minSpeedMultiplier > maxBounds.minSpeedMultiplier) std::swap(minBounds.minSpeedMultiplier, maxBounds.minSpeedMultiplier);
    if (minBounds.maxSpeedMultiplier > maxBounds.maxSpeedMultiplier) std::swap(minBounds.maxSpeedMultiplier, maxBounds.maxSpeedMultiplier);
    if (minBounds.predictionInterval > maxBounds.predictionInterval) std::swap(minBounds.predictionInterval, maxBounds.predictionInterval);
    if (minBounds.snapRadius > maxBounds.snapRadius) std::swap(minBounds.snapRadius, maxBounds.snapRadius);
    if (minBounds.nearRadius > maxBounds.nearRadius) std::swap(minBounds.nearRadius, maxBounds.nearRadius);
    if (minBounds.speedCurveExponent > maxBounds.speedCurveExponent) std::swap(minBounds.speedCurveExponent, maxBounds.speedCurveExponent);
    if (minBounds.snapBoostFactor > maxBounds.snapBoostFactor) std::swap(minBounds.snapBoostFactor, maxBounds.snapBoostFactor);
    if (minBounds.wind_G > maxBounds.wind_G) std::swap(minBounds.wind_G, maxBounds.wind_G);
    if (minBounds.wind_W > maxBounds.wind_W) std::swap(minBounds.wind_W, maxBounds.wind_W);
    if (minBounds.wind_M > maxBounds.wind_M) std::swap(minBounds.wind_M, maxBounds.wind_M);
    if (minBounds.wind_D > maxBounds.wind_D) std::swap(minBounds.wind_D, maxBounds.wind_D);
    if (minBounds.easynorecoilstrength > maxBounds.easynorecoilstrength) std::swap(minBounds.easynorecoilstrength, maxBounds.easynorecoilstrength);

    minBounds.wind_mouse_enabled = minBounds.wind_mouse_enabled && maxBounds.wind_mouse_enabled;
    maxBounds.wind_mouse_enabled = maxBounds.wind_mouse_enabled || minBounds.wind_mouse_enabled;
    minBounds.easynorecoil = minBounds.easynorecoil && maxBounds.easynorecoil;
    maxBounds.easynorecoil = maxBounds.easynorecoil || minBounds.easynorecoil;

    if (minBounds.maxSpeedMultiplier < minBounds.minSpeedMultiplier) {
        minBounds.maxSpeedMultiplier = minBounds.minSpeedMultiplier;
    }
    if (maxBounds.maxSpeedMultiplier < maxBounds.minSpeedMultiplier) {
        maxBounds.maxSpeedMultiplier = maxBounds.minSpeedMultiplier;
    }

    if (maxBounds.nearRadius < minBounds.nearRadius) {
        maxBounds.nearRadius = minBounds.nearRadius;
    }
    if (maxBounds.snapRadius < minBounds.snapRadius) {
        maxBounds.snapRadius = minBounds.snapRadius;
    }

    state.minBounds = sanitizeSettings(minBounds, minBounds, maxBounds);
    state.maxBounds = sanitizeSettings(maxBounds, state.minBounds, maxBounds);
    state.currentSettings = sanitizeSettings(state.currentSettings, state.minBounds, state.maxBounds);
    state.bestSettings = sanitizeSettings(state.bestSettings, state.minBounds, state.maxBounds);

    if (state.trainingActive && state.calibrating) {
        rebuildCalibrationScheduleLocked();
        if (!state.calibrationSchedule.empty()) {
            state.calibrationIndex = std::min<std::size_t>(state.calibrationIndex, state.calibrationSchedule.size() - 1);
            beginEvaluationLocked(state.calibrationSchedule[state.calibrationIndex]);
        }
    }
}

void AITuner::startTraining() {
    std::lock_guard<std::mutex> lock(mutex);
    if (state.trainingActive) {
        return;
    }

    state.trainingActive = true;
    state.paused = false;
    resetStatisticsLocked();

    if (config.autoCalibrate) {
        state.calibrating = true;
        rebuildCalibrationScheduleLocked();
        if (!state.calibrationSchedule.empty()) {
            state.calibrationIndex = 0;
            beginEvaluationLocked(state.calibrationSchedule.front());
            return;
        }
    }

    state.calibrating = false;
    beginEvaluationLocked(state.currentSettings);
}

void AITuner::stopTraining() {
    std::lock_guard<std::mutex> lock(mutex);
    state.trainingActive = false;
    state.paused = false;
    state.calibrating = false;
    state.calibrationSchedule.clear();
    state.calibrationIndex = 0;
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
}

void AITuner::resetTraining() {
    std::lock_guard<std::mutex> lock(mutex);

    resetStatisticsLocked();
    if (!state.trainingActive) {
        state.calibrating = false;
        state.calibrationSchedule.clear();
        state.calibrationIndex = 0;
        state.currentSettings = sanitizeSettings(state.currentSettings, state.minBounds, state.maxBounds);
        state.bestSettings = state.currentSettings;
        return;
    }

    if (config.autoCalibrate) {
        state.calibrating = true;
        rebuildCalibrationScheduleLocked();
        if (!state.calibrationSchedule.empty()) {
            state.calibrationIndex = 0;
            beginEvaluationLocked(state.calibrationSchedule.front());
            return;
        }
    }

    state.calibrating = false;
    beginEvaluationLocked(state.currentSettings);
}

void AITuner::provideFeedback(const AimbotTarget& target, double mouseX, double mouseY) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!state.trainingActive || state.paused) {
        return;
    }

    const double reward = calculateReward(target, mouseX, mouseY, config.targetRadius);
    state.iteration = std::min(state.iteration + 1, config.maxIterations);
    state.totalIterations = config.maxIterations;
    state.lastReward = reward;
    state.totalReward += reward;
    state.totalCount++;
    if (reward > 0.0) {
        state.successCount++;
    }

    state.rewardHistory.push_back(reward);
    trimHistoryLocked();

    state.evaluationRewardSum += reward;
    state.evaluationSamplesCollected++;

    if (state.evaluationSamplesCollected < std::max(1, state.evaluationWindow)) {
        return;
    }

    const double averageReward = state.evaluationRewardSum /
                                 static_cast<double>(state.evaluationSamplesCollected);
    finalizeEvaluationLocked(averageReward);
}

void AITuner::startCalibration() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!state.trainingActive) {
        return;
    }

    state.calibrating = true;
    state.bestReward = std::numeric_limits<double>::lowest();
    state.bestSettings = state.currentSettings;
    rebuildCalibrationScheduleLocked();
    if (!state.calibrationSchedule.empty()) {
        state.calibrationIndex = 0;
        beginEvaluationLocked(state.calibrationSchedule.front());
    }
}

void AITuner::stopCalibration() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!state.trainingActive) {
        state.calibrating = false;
        state.calibrationSchedule.clear();
        state.calibrationIndex = 0;
        return;
    }

    state.calibrating = false;
    state.calibrationSchedule.clear();
    state.calibrationIndex = 0;
    beginEvaluationLocked(state.bestSettings);
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
    return static_cast<double>(state.successCount) /
           static_cast<double>(state.totalCount);
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
    stats.totalIterations = state.totalIterations;
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
    return sanitizeSettings(defaults, state.minBounds, state.maxBounds);
}

void AITuner::applyModeSettings(AimMode mode) {
    std::lock_guard<std::mutex> lock(mutex);
    applyModeLocked(mode);
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
    result.wind_G = clampValue(result.wind_G, minBounds.wind_G, maxBounds.wind_G);
    result.wind_W = clampValue(result.wind_W, minBounds.wind_W, maxBounds.wind_W);
    result.wind_M = clampValue(result.wind_M, minBounds.wind_M, maxBounds.wind_M);
    result.wind_D = clampValue(result.wind_D, minBounds.wind_D, maxBounds.wind_D);
    result.easynorecoilstrength = clampValue(result.easynorecoilstrength,
                                             minBounds.easynorecoilstrength,
                                             maxBounds.easynorecoilstrength);
    return result;
}

MouseSettings AITuner::sanitizeSettings(const MouseSettings& settings,
                                        const MouseSettings& minBounds,
                                        const MouseSettings& maxBounds) const {
    MouseSettings result = clampSettings(settings, minBounds, maxBounds);

    float lowerSpeed = std::min(result.minSpeedMultiplier, result.maxSpeedMultiplier);
    float upperSpeed = std::max(result.minSpeedMultiplier, result.maxSpeedMultiplier);
    lowerSpeed = clampValue(lowerSpeed, minBounds.minSpeedMultiplier, maxBounds.minSpeedMultiplier);
    upperSpeed = clampValue(upperSpeed, std::max(lowerSpeed, minBounds.maxSpeedMultiplier), maxBounds.maxSpeedMultiplier);
    if (upperSpeed < lowerSpeed) {
        upperSpeed = lowerSpeed;
    }
    result.minSpeedMultiplier = lowerSpeed;
    result.maxSpeedMultiplier = upperSpeed;

    result.snapRadius = clampValue(result.snapRadius, minBounds.snapRadius, maxBounds.snapRadius);
    result.nearRadius = clampValue(result.nearRadius, minBounds.nearRadius, maxBounds.nearRadius);

    if (!maxBounds.wind_mouse_enabled) {
        result.wind_mouse_enabled = false;
    }
    if (!result.wind_mouse_enabled) {
        result.wind_G = clampValue(minBounds.wind_G, minBounds.wind_G, maxBounds.wind_G);
        result.wind_W = clampValue(minBounds.wind_W, minBounds.wind_W, maxBounds.wind_W);
        result.wind_M = clampValue(minBounds.wind_M, minBounds.wind_M, maxBounds.wind_M);
        result.wind_D = clampValue(minBounds.wind_D, minBounds.wind_D, maxBounds.wind_D);
    }

    if (!maxBounds.easynorecoil) {
        result.easynorecoil = false;
    }
    if (!result.easynorecoil) {
        result.easynorecoilstrength = minBounds.easynorecoilstrength;
    }

    return clampSettings(result, minBounds, maxBounds);
}

MouseSettings AITuner::randomSettings(const MouseSettings& minBounds,
                                      const MouseSettings& maxBounds) {
    std::uniform_real_distribution<float> zeroOne(0.0f, 1.0f);
    MouseSettings result = minBounds;
    result.dpi = clampValue(
        static_cast<int>(std::round(minBounds.dpi +
                                    (maxBounds.dpi - minBounds.dpi) * zeroOne(rng))),
        minBounds.dpi,
        maxBounds.dpi);
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
    result.easynorecoilstrength = minBounds.easynorecoilstrength +
                                  (maxBounds.easynorecoilstrength - minBounds.easynorecoilstrength) * zeroOne(rng);
    return sanitizeSettings(result, minBounds, maxBounds);
}

MouseSettings AITuner::mutateSettings(const MouseSettings& base,
                                      const MouseSettings& minBounds,
                                      const MouseSettings& maxBounds,
                                      float intensity) {
    std::normal_distribution<float> noise(0.0f, std::max(intensity, kMinFloatEpsilon));
    MouseSettings candidate = base;
    candidate.dpi = clampValue(static_cast<int>(std::round(candidate.dpi + noise(rng) * 400.0f)),
                               minBounds.dpi, maxBounds.dpi);
    candidate.sensitivity += noise(rng) * 0.1f;
    candidate.minSpeedMultiplier += noise(rng) * 0.15f;
    candidate.maxSpeedMultiplier += noise(rng) * 0.2f;
    candidate.predictionInterval += noise(rng) * 0.005f;
    candidate.snapRadius += noise(rng) * 1.5f;
    candidate.nearRadius += noise(rng) * 2.0f;
    candidate.speedCurveExponent += noise(rng) * 0.2f;
    candidate.snapBoostFactor += noise(rng) * 0.3f;
    candidate.wind_G += noise(rng) * 0.5f;
    candidate.wind_W += noise(rng) * 0.5f;
    candidate.wind_M += noise(rng) * 0.5f;
    candidate.wind_D += noise(rng) * 0.5f;
    candidate.easynorecoilstrength += noise(rng) * 0.2f;

    if (maxBounds.wind_mouse_enabled) {
        std::bernoulli_distribution toggle(std::min(0.3f, std::fabs(noise(rng))));
        if (toggle(rng)) {
            candidate.wind_mouse_enabled = !candidate.wind_mouse_enabled;
        }
    }
    if (maxBounds.easynorecoil) {
        std::bernoulli_distribution toggle(std::min(0.3f, std::fabs(noise(rng))));
        if (toggle(rng)) {
            candidate.easynorecoil = !candidate.easynorecoil;
        }
    }

    return sanitizeSettings(candidate, minBounds, maxBounds);
}

double AITuner::calculateReward(const AimbotTarget& target,
                               double mouseX,
                               double mouseY,
                               float radius) const {
    double pivotX = target.pivotX;
    double pivotY = target.pivotY;
    if (pivotX == 0.0 && pivotY == 0.0) {
        pivotX = static_cast<double>(target.x) + static_cast<double>(target.w) * 0.5;
        pivotY = static_cast<double>(target.y) + static_cast<double>(target.h) * 0.5;
    }

    const double dx = pivotX - mouseX;
    const double dy = pivotY - mouseY;
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
            return MouseSettings{800, 1.2f, 0.25f, 2.0f, 0.016f, 10.0f, 5.0f, 1.2f, 1.0f,
                                 false, 1.5f, 1.5f, 1.5f, 1.5f, false, 0.0f};
        case AimMode::AIM_BOT:
            return MouseSettings{1200, 1.6f, 0.4f, 3.0f, 0.012f, 14.0f, 8.0f, 1.5f, 1.4f,
                                 true, 3.0f, 3.0f, 3.0f, 3.0f, true, 0.25f};
        case AimMode::RAGE_BAITER:
        default:
            return MouseSettings{1600, 2.0f, 0.8f, 4.0f, 0.010f, 18.0f, 10.0f, 2.0f, 2.0f,
                                 true, 5.0f, 5.0f, 5.0f, 5.0f, true, 0.4f};
    }
}

void AITuner::applyModeLocked(AimMode mode) {
    state.currentMode = mode;
    MouseSettings defaults = defaultsForMode(mode);
    state.currentSettings = sanitizeSettings(defaults, state.minBounds, state.maxBounds);
    state.bestSettings = state.currentSettings;
    state.bestReward = std::numeric_limits<double>::lowest();
    state.calibrating = false;
    state.calibrationSchedule.clear();
    state.calibrationIndex = 0;
    resetStatisticsLocked();
    beginEvaluationLocked(state.currentSettings);
}

void AITuner::resetStatisticsLocked() {
    state.iteration = 0;
    state.totalIterations = config.maxIterations;
    state.evaluationSamplesCollected = 0;
    state.evaluationRewardSum = 0.0;
    state.lastReward = 0.0;
    state.totalReward = 0.0;
    state.bestReward = std::numeric_limits<double>::lowest();
    state.successCount = 0;
    state.totalCount = 0;
    state.rewardHistory.clear();
    state.settingsHistory.clear();
    state.calibrationIndex = 0;
    state.calibrationSchedule.clear();
    state.currentSettings = sanitizeSettings(state.currentSettings, state.minBounds, state.maxBounds);
    state.bestSettings = sanitizeSettings(state.bestSettings, state.minBounds, state.maxBounds);
}

void AITuner::beginEvaluationLocked(const MouseSettings& candidate, bool clearAccum) {
    state.currentSettings = sanitizeSettings(candidate, state.minBounds, state.maxBounds);
    state.settingsHistory.push_back(state.currentSettings);
    trimHistoryLocked();
    if (clearAccum) {
        state.evaluationRewardSum = 0.0;
        state.evaluationSamplesCollected = 0;
    }
}

void AITuner::finalizeEvaluationLocked(double averageReward) {
    state.evaluationRewardSum = 0.0;
    state.evaluationSamplesCollected = 0;

    if (state.calibrating) {
        if (averageReward > state.bestReward + kRewardTolerance ||
            state.bestReward == std::numeric_limits<double>::lowest()) {
            state.bestReward = averageReward;
            state.bestSettings = state.currentSettings;
        }

        if (state.calibrationIndex + 1 >= state.calibrationSchedule.size()) {
            state.calibrating = false;
            beginEvaluationLocked(state.bestSettings);
            return;
        }

        state.calibrationIndex++;
        beginEvaluationLocked(state.calibrationSchedule[state.calibrationIndex]);
        return;
    }

    if (state.bestReward == std::numeric_limits<double>::lowest() ||
        averageReward > state.bestReward + kRewardTolerance) {
        state.bestReward = averageReward;
        state.bestSettings = state.currentSettings;
    } else if (averageReward + kRewardTolerance < state.bestReward) {
        beginEvaluationLocked(state.bestSettings);
        return;
    }

    MouseSettings nextCandidate = generateExplorationCandidateLocked();
    beginEvaluationLocked(nextCandidate);
}

void AITuner::rebuildCalibrationScheduleLocked() {
    state.calibrationSchedule.clear();

    if (state.calibrationBudget <= 0) {
        state.calibrationBudget = 1;
    }

    MouseSettings defaults = sanitizeSettings(defaultsForMode(state.currentMode),
                                              state.minBounds, state.maxBounds);
    MouseSettings minBounds = sanitizeSettings(state.minBounds, state.minBounds, state.maxBounds);
    MouseSettings maxBounds = sanitizeSettings(state.maxBounds, state.minBounds, state.maxBounds);

    auto pushCandidate = [&](const MouseSettings& candidate) {
        MouseSettings sanitized = sanitizeSettings(candidate, state.minBounds, state.maxBounds);
        if (state.calibrationSchedule.empty() ||
            !settingsApproximatelyEqual(state.calibrationSchedule.back(), sanitized)) {
            state.calibrationSchedule.push_back(sanitized);
        }
    };

    pushCandidate(defaults);

    if (state.calibrationBudget == 1) {
        return;
    }

    const int steps = std::max(2, state.calibrationBudget);
    for (int i = 0; i < steps; ++i) {
        const float t = (steps == 1) ? 0.0f : static_cast<float>(i) / static_cast<float>(steps - 1);
        MouseSettings candidate = defaults;
        candidate.dpi = static_cast<int>(std::round(minBounds.dpi + (maxBounds.dpi - minBounds.dpi) * t));
        candidate.sensitivity = minBounds.sensitivity + (maxBounds.sensitivity - minBounds.sensitivity) * t;
        candidate.minSpeedMultiplier = minBounds.minSpeedMultiplier +
                                       (maxBounds.minSpeedMultiplier - minBounds.minSpeedMultiplier) * t;
        candidate.maxSpeedMultiplier = minBounds.maxSpeedMultiplier +
                                       (maxBounds.maxSpeedMultiplier - minBounds.maxSpeedMultiplier) * t;
        candidate.predictionInterval = minBounds.predictionInterval +
                                       (maxBounds.predictionInterval - minBounds.predictionInterval) * t;
        candidate.snapRadius = minBounds.snapRadius +
                               (maxBounds.snapRadius - minBounds.snapRadius) * t;
        candidate.nearRadius = minBounds.nearRadius +
                               (maxBounds.nearRadius - minBounds.nearRadius) * t;
        candidate.speedCurveExponent = minBounds.speedCurveExponent +
                                       (maxBounds.speedCurveExponent - minBounds.speedCurveExponent) * t;
        candidate.snapBoostFactor = minBounds.snapBoostFactor +
                                    (maxBounds.snapBoostFactor - minBounds.snapBoostFactor) * t;
        candidate.wind_mouse_enabled = (t >= 0.5f) && maxBounds.wind_mouse_enabled;
        candidate.wind_G = minBounds.wind_G + (maxBounds.wind_G - minBounds.wind_G) * t;
        candidate.wind_W = minBounds.wind_W + (maxBounds.wind_W - minBounds.wind_W) * t;
        candidate.wind_M = minBounds.wind_M + (maxBounds.wind_M - minBounds.wind_M) * t;
        candidate.wind_D = minBounds.wind_D + (maxBounds.wind_D - minBounds.wind_D) * t;
        candidate.easynorecoil = (t >= 0.5f) && maxBounds.easynorecoil;
        candidate.easynorecoilstrength = minBounds.easynorecoilstrength +
                                         (maxBounds.easynorecoilstrength - minBounds.easynorecoilstrength) * t;
        pushCandidate(candidate);
    }
}

MouseSettings AITuner::generateExplorationCandidateLocked() {
    if (state.bestReward == std::numeric_limits<double>::lowest()) {
        return state.currentSettings;
    }

    const float intensity = config.explorationRate * (1.0f + static_cast<float>(config.learningRate));
    MouseSettings mutated = mutateSettings(state.bestSettings, state.minBounds, state.maxBounds, intensity);
    return sanitizeSettings(mutated, state.minBounds, state.maxBounds);
}

void AITuner::trimHistoryLocked() {
    if (state.settingsHistory.size() > kMaxHistoryEntries) {
        state.settingsHistory.erase(state.settingsHistory.begin(),
                                    state.settingsHistory.begin() +
                                    (state.settingsHistory.size() - kMaxHistoryEntries));
    }
    if (state.rewardHistory.size() > kMaxHistoryEntries) {
        state.rewardHistory.erase(state.rewardHistory.begin(),
                                   state.rewardHistory.begin() +
                                   (state.rewardHistory.size() - kMaxHistoryEntries));
    }
}
