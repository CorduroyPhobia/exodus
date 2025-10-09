#ifndef AI_TUNER_H
#define AI_TUNER_H

#include <array>
#include <limits>
#include <mutex>
#include <random>
#include <string>
#include <vector>
#include <cstddef>

#include "AimbotTarget.h"

struct MouseSettings {
    int dpi = 0;
    float sensitivity = 0.0f;
    float minSpeedMultiplier = 0.0f;
    float maxSpeedMultiplier = 0.0f;
    float predictionInterval = 0.0f;
    float snapRadius = 0.0f;
    float nearRadius = 0.0f;
    float speedCurveExponent = 0.0f;
    float snapBoostFactor = 0.0f;
    bool wind_mouse_enabled = false;
    float wind_G = 0.0f;
    float wind_W = 0.0f;
    float wind_M = 0.0f;
    float wind_D = 0.0f;
    bool easynorecoil = false;
    float easynorecoilstrength = 0.0f;
};

enum class AimMode {
    AIM_ASSIST,
    AIM_BOT,
    RAGE_BAITER
};

class AITuner {
public:
    AITuner();
    ~AITuner();

    void setAimMode(AimMode mode);
    void setLearningRate(float rate);
    void setExplorationRate(float rate);
    void setMaxIterations(int iterations);
    void setTargetRadius(float radius);
    void setAutoCalibrate(bool enabled);
    void setSettingsBounds(const MouseSettings& min, const MouseSettings& max);
    void setEvaluationWindow(int windowSize);
    void setCalibrationBudget(int budget);
    void setHistoryLimit(std::size_t limit);

    void startTraining();
    void stopTraining();
    void pauseTraining();
    void resumeTraining();
    void resetTraining();

    void provideFeedback(const AimbotTarget& target, double mouseX, double mouseY);
    void startCalibration();
    void stopCalibration();

    MouseSettings getCurrentSettings() const;
    AimMode getCurrentMode() const;
    double getCurrentReward() const;
    int getCurrentIteration() const;
    bool isTraining() const;
    bool isCalibrating() const;
    double getSuccessRate() const;
    MouseSettings getBestSettings() const;

    struct TrainingStats {
        double averageReward = 0.0;
        double bestReward = 0.0;
        double successRate = 0.0;
        int totalIterations = 0;
        int currentIteration = 0;
        bool isTraining = false;
        bool isCalibrating = false;
    };

    TrainingStats getTrainingStats() const;

    MouseSettings getModeSettings(AimMode mode) const;
    void applyModeSettings(AimMode mode);

private:
    struct Config {
        float learningRate = 0.01f;
        float explorationRate = 0.15f;
        int maxIterations = 1000;
        float targetRadius = 12.0f;
        bool autoCalibrate = true;
    };

    struct PersistedModeState {
        MouseSettings settings{};
        double bestReward = std::numeric_limits<double>::lowest();
        bool hasData = false;
    };

    struct RuntimeState {
        AimMode currentMode = AimMode::AIM_ASSIST;
        bool trainingActive = false;
        bool paused = false;
        bool calibrating = false;
        int iteration = 0;
        int totalIterations = 0;
        int evaluationSamplesCollected = 0;
        int evaluationWindow = 12;
        int calibrationBudget = 12;
        std::size_t calibrationIndex = 0;
        std::size_t historyLimit = 256;
        double evaluationRewardSum = 0.0;
        double lastReward = 0.0;
        double totalReward = 0.0;
        double bestReward = 0.0;
        int successCount = 0;
        int totalCount = 0;
        MouseSettings minBounds{};
        MouseSettings maxBounds{};
        MouseSettings currentSettings{};
        MouseSettings bestSettings{};
        MouseSettings appliedSettings{};
        bool hasAppliedSettings = false;
        std::vector<MouseSettings> calibrationSchedule;
        std::vector<MouseSettings> settingsHistory;
        std::vector<double> rewardHistory;
        std::array<PersistedModeState, 3> persistedStates{};
    };

    mutable std::mutex mutex;

    Config config;
    RuntimeState state;
    std::mt19937 rng;

    MouseSettings clampSettings(const MouseSettings& settings,
                                const MouseSettings& minBounds,
                                const MouseSettings& maxBounds) const;
    MouseSettings sanitizeSettings(const MouseSettings& settings,
                                   const MouseSettings& minBounds,
                                   const MouseSettings& maxBounds) const;
    MouseSettings randomSettings(const MouseSettings& minBounds,
                                 const MouseSettings& maxBounds);
    MouseSettings mutateSettings(const MouseSettings& base,
                                 const MouseSettings& minBounds,
                                 const MouseSettings& maxBounds,
                                 float intensity);
    MouseSettings smoothSettings(const MouseSettings& from,
                                 const MouseSettings& to,
                                 float smoothing) const;
    float currentSmoothingFactor() const;
    double calculateReward(const AimbotTarget& target,
                           double mouseX,
                           double mouseY,
                           float radius) const;
    MouseSettings defaultsForMode(AimMode mode) const;
    void applyModeLocked(AimMode mode);
    void resetStatisticsLocked();
    void beginEvaluationLocked(const MouseSettings& candidate, bool clearAccum = true);
    void finalizeEvaluationLocked(double averageReward);
    void rebuildCalibrationScheduleLocked();
    MouseSettings generateExplorationCandidateLocked();
    void trimHistoryLocked();
    void stopTrainingLocked();
    void loadPersistedStateLocked();
    void savePersistedStateLocked();
    void applyPersistedBestLocked();
    void updatePersistedBestLocked();
};

#endif // AI_TUNER_H
