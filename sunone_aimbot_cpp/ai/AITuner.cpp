#include "AITuner.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>

AITuner::AITuner() 
    : currentMode(AimMode::AIM_ASSIST)
    , learningRate(0.01f)
    , explorationRate(0.1f)
    , maxIterations(1000)
    , targetRadius(10.0f)
    , autoCalibrate(true)
    , rng(std::random_device{}())
    , floatDist(0.0f, 1.0f)
    , intDist(400, 16000)
    , shouldStop(false)
    , totalReward(0.0)
    , successfulTargets(0)
    , totalTargets(0)
{
    try {
        // Set default bounds
        minSettings = MouseSettings(400, 0.1f, 0.01f, 0.01f, 0.001f, 0.5f, 5.0f, 1.0f, 1.0f, false, 10.0f, 5.0f, 5.0f, 3.0f, false, 0.0f);
        maxSettings = MouseSettings(16000, 5.0f, 2.0f, 2.0f, 0.1f, 5.0f, 100.0f, 10.0f, 3.0f, true, 50.0f, 30.0f, 20.0f, 15.0f, true, 2.0f);
        
        // Initialize with mode-specific settings
        applyModeSettings(currentMode);
        
        std::cout << "[AITuner] Constructor completed successfully" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[AITuner] Constructor error: " << e.what() << std::endl;
        throw;
    }
}

AITuner::~AITuner() {
    stopTraining();
}

void AITuner::setAimMode(AimMode mode) {
    std::lock_guard<std::mutex> lock(stateMutex);
    currentMode = mode;
    applyModeSettings(mode);
}

void AITuner::setLearningRate(float rate) {
    std::lock_guard<std::mutex> lock(stateMutex);
    learningRate = std::clamp(rate, 0.001f, 0.1f);
}

void AITuner::setExplorationRate(float rate) {
    std::lock_guard<std::mutex> lock(stateMutex);
    explorationRate = std::clamp(rate, 0.01f, 0.5f);
}

void AITuner::setMaxIterations(int iterations) {
    std::lock_guard<std::mutex> lock(stateMutex);
    maxIterations = std::max(100, iterations);
}

void AITuner::setTargetRadius(float radius) {
    std::lock_guard<std::mutex> lock(stateMutex);
    targetRadius = std::clamp(radius, 1.0f, 50.0f);
}

void AITuner::setAutoCalibrate(bool enabled) {
    bool shouldStartCalibration = false;
    bool shouldStopCalibration = false;

    {
        std::lock_guard<std::mutex> lock(stateMutex);
        autoCalibrate = enabled;

        if (autoCalibrate && !state.isCalibrating) {
            shouldStartCalibration = true;
        }

        if (!autoCalibrate && state.isCalibrating) {
            shouldStopCalibration = true;
        }
    }

    if (shouldStartCalibration) {
        startCalibration();
    }

    if (shouldStopCalibration) {
        stopCalibration();
    }
}

void AITuner::setSettingsBounds(const MouseSettings& min, const MouseSettings& max) {
    std::lock_guard<std::mutex> lock(stateMutex);
    minSettings = min;
    maxSettings = max;
}

void AITuner::startTraining() {
    try {
        if (trainingThread.joinable()) {
            std::cout << "[AITuner] Training already active" << std::endl;
            return; // Already training
        }

        shouldStop = false;
        trainingThread = std::thread(&AITuner::trainingLoop, this);
        std::cout << "[AITuner] Training started successfully" << std::endl;

        bool needsCalibration = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            needsCalibration = autoCalibrate && !state.isCalibrating;
        }

        if (needsCalibration) {
            startCalibration();
        }
    } catch (const std::exception& e) {
        std::cerr << "[AITuner] Error starting training: " << e.what() << std::endl;
    }
}

void AITuner::stopTraining() {
    shouldStop = true;
    cv.notify_all();
    
    if (trainingThread.joinable()) {
        trainingThread.join();
    }
}

void AITuner::pauseTraining() {
    shouldStop = true;
}

void AITuner::resumeTraining() {
    if (!trainingThread.joinable()) {
        startTraining();
    } else {
        shouldStop = false;
        cv.notify_all();
    }
}

void AITuner::resetTraining() {
    stopTraining();
    
    std::lock_guard<std::mutex> lock(stateMutex);
    state.iteration = 0;
    state.currentReward = 0.0;
    state.isCalibrating = false;
    settingsHistory.clear();
    rewardHistory.clear();
    totalReward = 0.0;
    successfulTargets = 0;
    totalTargets = 0;
    
    applyModeSettings(currentMode);
}

void AITuner::provideFeedback(const AimbotTarget& target, double mouseX, double mouseY) {
    try {
        double radiusSnapshot = 0.0;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            radiusSnapshot = targetRadius;
        }

        RewardSample rewardSample = calculateReward(target, mouseX, mouseY, radiusSnapshot);

        std::lock_guard<std::mutex> lock(queueMutex);

        // Prevent queue from growing too large
        if (feedbackQueue.size() > 100) {
            feedbackQueue.pop(); // Remove oldest feedback
        }

        feedbackQueue.push({target, rewardSample.reward, rewardSample.targetHit});
        cv.notify_all();
    } catch (const std::exception& e) {
        std::cerr << "[AITuner] Error providing feedback: " << e.what() << std::endl;
    }
}

void AITuner::startCalibration() {
    std::lock_guard<std::mutex> lock(stateMutex);
    state.isCalibrating = true;
    state.iteration = 0;
    
    // Start with very low sensitivity for calibration
    state.currentSettings.sensitivity = minSettings.sensitivity;
    state.currentSettings.dpi = minSettings.dpi;
}

void AITuner::stopCalibration() {
    std::lock_guard<std::mutex> lock(stateMutex);
    state.isCalibrating = false;
}

MouseSettings AITuner::getCurrentSettings() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(stateMutex));
    return state.currentSettings;
}

AimMode AITuner::getCurrentMode() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(stateMutex));
    return currentMode;
}

double AITuner::getCurrentReward() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(stateMutex));
    return state.currentReward;
}

int AITuner::getCurrentIteration() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(stateMutex));
    return state.iteration;
}

bool AITuner::isTraining() const {
    return trainingThread.joinable() && !shouldStop;
}

bool AITuner::isCalibrating() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(stateMutex));
    return state.isCalibrating;
}

double AITuner::getSuccessRate() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(stateMutex));
    return totalTargets > 0 ? (double)successfulTargets / totalTargets : 0.0;
}

MouseSettings AITuner::getBestSettings() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(stateMutex));
    
    if (rewardHistory.empty()) {
        return state.currentSettings;
    }
    
    auto bestIt = std::max_element(rewardHistory.begin(), rewardHistory.end());
    size_t bestIndex = std::distance(rewardHistory.begin(), bestIt);
    
    if (bestIndex < settingsHistory.size()) {
        return settingsHistory[bestIndex];
    }
    
    return state.currentSettings;
}

AITuner::TrainingStats AITuner::getTrainingStats() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(stateMutex));
    
    TrainingStats stats;
    stats.currentIteration = state.iteration;
    stats.isTraining = isTraining();
    stats.isCalibrating = state.isCalibrating;
    stats.totalIterations = maxIterations;
    
    if (!rewardHistory.empty()) {
        stats.averageReward = std::accumulate(rewardHistory.begin(), rewardHistory.end(), 0.0) / rewardHistory.size();
        stats.bestReward = *std::max_element(rewardHistory.begin(), rewardHistory.end());
    } else {
        stats.averageReward = 0.0;
        stats.bestReward = 0.0;
    }
    
    stats.successRate = getSuccessRate();
    
    return stats;
}

MouseSettings AITuner::getModeSettings(AimMode mode) const {
    MouseSettings settings;
    
    switch (mode) {
        case AimMode::AIM_ASSIST:
            // Subtle assistance - moderate settings
            settings = MouseSettings(
                800, 1.0f, 0.1f, 0.3f, 0.01f, 2.0f, 20.0f, 2.0f, 1.1f,
                true, 15.0f, 8.0f, 8.0f, 6.0f, false, 0.1f
            );
            break;
            
        case AimMode::AIM_BOT:
            // Good lock-on - more aggressive settings
            settings = MouseSettings(
                1200, 1.5f, 0.2f, 0.8f, 0.02f, 1.5f, 15.0f, 2.5f, 1.2f,
                true, 25.0f, 12.0f, 12.0f, 8.0f, true, 0.3f
            );
            break;
            
        case AimMode::RAGE_BAITER:
            // Maximum performance - most aggressive settings
            settings = MouseSettings(
                1600, 2.0f, 0.5f, 1.5f, 0.03f, 1.0f, 10.0f, 3.0f, 1.5f,
                true, 40.0f, 20.0f, 15.0f, 10.0f, true, 0.5f
            );
            break;
    }
    
    return settings;
}

void AITuner::applyModeSettings(AimMode mode) {
    std::lock_guard<std::mutex> lock(stateMutex);
    state.currentSettings = getModeSettings(mode);
    settingsHistory.push_back(state.currentSettings);
    rewardHistory.push_back(0.0);
}

MouseSettings AITuner::generateRandomSettings(const MouseSettings& minBounds, const MouseSettings& maxBounds) {
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
    settings.wind_mouse_enabled = floatDist(rng) < 0.5f;
    settings.wind_G = std::uniform_real_distribution<float>(minBounds.wind_G, maxBounds.wind_G)(rng);
    settings.wind_W = std::uniform_real_distribution<float>(minBounds.wind_W, maxBounds.wind_W)(rng);
    settings.wind_M = std::uniform_real_distribution<float>(minBounds.wind_M, maxBounds.wind_M)(rng);
    settings.wind_D = std::uniform_real_distribution<float>(minBounds.wind_D, maxBounds.wind_D)(rng);
    settings.easynorecoil = floatDist(rng) < 0.5f;
    settings.easynorecoilstrength = std::uniform_real_distribution<float>(minBounds.easynorecoilstrength, maxBounds.easynorecoilstrength)(rng);

    return settings;
}

MouseSettings AITuner::mutateSettings(const MouseSettings& base,
                                     const MouseSettings& minBounds,
                                     const MouseSettings& maxBounds,
                                     float explorationRateValue) {
    MouseSettings mutated = base;

    // Mutate each parameter with small random changes
    if (floatDist(rng) < explorationRateValue) {
        mutated.dpi = std::clamp(mutated.dpi + (intDist(rng) - 8000) / 100, minBounds.dpi, maxBounds.dpi);
    }
    if (floatDist(rng) < explorationRateValue) {
        mutated.sensitivity += (floatDist(rng) - 0.5f) * 0.2f;
        mutated.sensitivity = std::clamp(mutated.sensitivity, minBounds.sensitivity, maxBounds.sensitivity);
    }
    if (floatDist(rng) < explorationRateValue) {
        mutated.minSpeedMultiplier += (floatDist(rng) - 0.5f) * 0.1f;
        mutated.minSpeedMultiplier = std::clamp(mutated.minSpeedMultiplier, minBounds.minSpeedMultiplier, maxBounds.minSpeedMultiplier);
    }
    if (floatDist(rng) < explorationRateValue) {
        mutated.maxSpeedMultiplier += (floatDist(rng) - 0.5f) * 0.1f;
        mutated.maxSpeedMultiplier = std::clamp(mutated.maxSpeedMultiplier, minBounds.maxSpeedMultiplier, maxBounds.maxSpeedMultiplier);
    }

    // Continue with other parameters...
    // (Similar mutation logic for remaining parameters)

    return mutated;
}

AITuner::RewardSample AITuner::calculateReward(const AimbotTarget& target,
                                              double mouseX,
                                              double mouseY,
                                              double radius) const {
    // Calculate distance from mouse to target center
    double targetCenterX = target.x + target.w / 2.0;
    double targetCenterY = target.y + target.h / 2.0;

    double distance = std::sqrt(std::pow(mouseX - targetCenterX, 2) + std::pow(mouseY - targetCenterY, 2));

    // Reward based on proximity to target
    double reward = 0.0;
    bool targetHit = distance <= radius;

    if (targetHit) {
        // High reward for being on target
        reward = 1.0 - (distance / radius) * 0.5;
    } else if (distance <= radius * 2) {
        // Medium reward for being close
        reward = 0.5 - (distance - radius) / (radius * 2) * 0.3;
    } else {
        // Low reward for being far
        reward = 0.1 - std::min(distance / (radius * 10), 0.1);
    }

    return {std::max(reward, -1.0), targetHit};
}

MouseSettings AITuner::interpolateSettings(const MouseSettings& a, const MouseSettings& b, float t) {
    MouseSettings result;
    
    result.dpi = (int)(a.dpi + (b.dpi - a.dpi) * t);
    result.sensitivity = a.sensitivity + (b.sensitivity - a.sensitivity) * t;
    result.minSpeedMultiplier = a.minSpeedMultiplier + (b.minSpeedMultiplier - a.minSpeedMultiplier) * t;
    result.maxSpeedMultiplier = a.maxSpeedMultiplier + (b.maxSpeedMultiplier - a.maxSpeedMultiplier) * t;
    result.predictionInterval = a.predictionInterval + (b.predictionInterval - a.predictionInterval) * t;
    result.snapRadius = a.snapRadius + (b.snapRadius - a.snapRadius) * t;
    result.nearRadius = a.nearRadius + (b.nearRadius - a.nearRadius) * t;
    result.speedCurveExponent = a.speedCurveExponent + (b.speedCurveExponent - a.speedCurveExponent) * t;
    result.snapBoostFactor = a.snapBoostFactor + (b.snapBoostFactor - a.snapBoostFactor) * t;
    result.wind_mouse_enabled = t > 0.5f ? b.wind_mouse_enabled : a.wind_mouse_enabled;
    result.wind_G = a.wind_G + (b.wind_G - a.wind_G) * t;
    result.wind_W = a.wind_W + (b.wind_W - a.wind_W) * t;
    result.wind_M = a.wind_M + (b.wind_M - a.wind_M) * t;
    result.wind_D = a.wind_D + (b.wind_D - a.wind_D) * t;
    result.easynorecoil = t > 0.5f ? b.easynorecoil : a.easynorecoil;
    result.easynorecoilstrength = a.easynorecoilstrength + (b.easynorecoilstrength - a.easynorecoilstrength) * t;
    
    return result;
}

void AITuner::updateSettings(const MouseSettings& newSettings) {
    std::lock_guard<std::mutex> lock(stateMutex);
    state.currentSettings = newSettings;
    state.lastUpdate = std::chrono::steady_clock::now();
}

void AITuner::trainingLoop() {
    try {
        while (true) {
            if (shouldStop) {
                break;
            }

            int iterationSnapshot = 0;
            int maxIterationsSnapshot = 0;
            {
                std::lock_guard<std::mutex> stateLock(stateMutex);
                iterationSnapshot = state.iteration;
                maxIterationsSnapshot = maxIterations;
            }

            if (iterationSnapshot >= maxIterationsSnapshot) {
                break;
            }

            std::unique_lock<std::mutex> lock(queueMutex);

            // Wait for feedback or timeout
            cv.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !feedbackQueue.empty() || shouldStop;
            });

            if (shouldStop) {
                break;
            }

            if (feedbackQueue.empty()) {
                continue;
            }

            FeedbackSample feedback = feedbackQueue.front();
            feedbackQueue.pop();
            lock.unlock();

            MouseSettings currentSettingsSnapshot;
            bool isCalibratingSnapshot = false;
            MouseSettings minSettingsSnapshot;
            MouseSettings maxSettingsSnapshot;
            float explorationRateSnapshot = 0.0f;
            double currentRewardSnapshot = feedback.reward;
            double successRateSnapshot = 0.0;

            {
                std::lock_guard<std::mutex> stateLock(stateMutex);
                state.currentReward = feedback.reward;
                rewardHistory.push_back(feedback.reward);
                currentSettingsSnapshot = state.currentSettings;
                isCalibratingSnapshot = state.isCalibrating;
                minSettingsSnapshot = minSettings;
                maxSettingsSnapshot = maxSettings;
                explorationRateSnapshot = explorationRate;

                totalTargets++;
                if (feedback.targetHit) {
                    successfulTargets++;
                }
                totalReward += feedback.reward;
                successRateSnapshot = totalTargets > 0 ? static_cast<double>(successfulTargets) / totalTargets : 0.0;
                currentRewardSnapshot = state.currentReward;
            }

            bool stopCalibrationAfterUpdate = false;
            MouseSettings updatedSettings = currentSettingsSnapshot;
            bool appliedNewSettings = false;

            // Update settings based on reward
            if (isCalibratingSnapshot) {
                // During calibration, gradually increase sensitivity
                if (feedback.reward < 0.3 && updatedSettings.sensitivity < maxSettingsSnapshot.sensitivity) {
                    updatedSettings.sensitivity *= 1.1f;
                    updatedSettings.sensitivity = std::min(updatedSettings.sensitivity, maxSettingsSnapshot.sensitivity);
                    appliedNewSettings = true;
                }
                if (feedback.reward < 0.3 && updatedSettings.dpi < maxSettingsSnapshot.dpi) {
                    updatedSettings.dpi = std::min(updatedSettings.dpi + 100, maxSettingsSnapshot.dpi);
                    appliedNewSettings = true;
                }

                if (feedback.reward >= 0.8 ||
                    feedback.targetHit ||
                    updatedSettings.sensitivity >= maxSettingsSnapshot.sensitivity ||
                    updatedSettings.dpi >= maxSettingsSnapshot.dpi) {
                    stopCalibrationAfterUpdate = true;
                }
            } else {
                // Normal training - explore and exploit
                if (floatDist(rng) < explorationRateSnapshot) {
                    // Explore: try random settings
                    updatedSettings = generateRandomSettings(minSettingsSnapshot, maxSettingsSnapshot);
                } else {
                    // Exploit: improve current settings
                    updatedSettings = mutateSettings(currentSettingsSnapshot,
                                                     minSettingsSnapshot,
                                                     maxSettingsSnapshot,
                                                     explorationRateSnapshot);
                }
                appliedNewSettings = true;
            }

            if (appliedNewSettings) {
                updateSettings(updatedSettings);
                currentSettingsSnapshot = updatedSettings;
            }

            int updatedIterationSnapshot = 0;
            {
                std::lock_guard<std::mutex> stateLock(stateMutex);
                settingsHistory.push_back(currentSettingsSnapshot);
                state.iteration++;
                updatedIterationSnapshot = state.iteration;
            }

            if (stopCalibrationAfterUpdate) {
                stopCalibration();
            }

            if (updatedIterationSnapshot % 100 == 0) {
                std::cout << "[AITuner] Iteration " << updatedIterationSnapshot
                          << ", Reward: " << currentRewardSnapshot
                          << ", Success Rate: " << successRateSnapshot << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[AITuner] Training loop error: " << e.what() << std::endl;
    }
}
