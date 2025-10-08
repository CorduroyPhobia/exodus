#ifndef AI_TUNER_H
#define AI_TUNER_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

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

    MouseSettings() = default;

    MouseSettings(int dpiValue, float sensitivityValue, float minSpeed, float maxSpeed,
                  float prediction, float snapR, float nearR, float speedCurve,
                  float snapBoost, bool windEnabled, float windG, float windW,
                  float windM, float windD, bool easyRecoil, float recoilStrength)
        : dpi(dpiValue),
          sensitivity(sensitivityValue),
          minSpeedMultiplier(minSpeed),
          maxSpeedMultiplier(maxSpeed),
          predictionInterval(prediction),
          snapRadius(snapR),
          nearRadius(nearR),
          speedCurveExponent(speedCurve),
          snapBoostFactor(snapBoost),
          wind_mouse_enabled(windEnabled),
          wind_G(windG),
          wind_W(windW),
          wind_M(windM),
          wind_D(windD),
          easynorecoil(easyRecoil),
          easynorecoilstrength(recoilStrength) {}
};

enum class AimMode {
    AIM_ASSIST,
    AIM_BOT,
    RAGE_BAITER
};

class AITuner {
private:
    struct FeedbackEvent {
        AimbotTarget target;
        double mouseX = 0.0;
        double mouseY = 0.0;
    };

    struct RewardSample {
        double reward = 0.0;
        bool targetHit = false;
    };

    struct SharedState {
        AimMode currentMode = AimMode::AIM_ASSIST;
        float learningRate = 0.01f;
        float explorationRate = 0.1f;
        int maxIterations = 1000;
        float targetRadius = 10.0f;
        bool autoCalibrate = true;

        MouseSettings minSettings;
        MouseSettings maxSettings;
        MouseSettings currentSettings;
        MouseSettings bestSettings;

        double bestReward = -std::numeric_limits<double>::infinity();
        double lastReward = 0.0;
        int iteration = 0;
        bool calibrating = false;
        int calibrationStep = 0;
        int calibrationMaxSteps = 150;
        bool trainingActive = false;
        bool paused = false;
        bool threadRunning = false;
        std::chrono::steady_clock::time_point lastUpdate = std::chrono::steady_clock::now();
        double totalReward = 0.0;
        int successfulTargets = 0;
        int totalTargets = 0;
        std::vector<MouseSettings> settingsHistory;
        std::vector<double> rewardHistory;
    };

    mutable std::mutex stateMutex;
    std::condition_variable workAvailable;
    SharedState shared;
    std::deque<FeedbackEvent> feedbackQueue;

    std::thread trainingThread;
    std::atomic<bool> stopRequested;

    std::mt19937 rng;

    // Helper methods
    void trainingLoop();
    void applyModeSettingsInternal(AimMode mode, SharedState& state);
    MouseSettings clampSettings(const MouseSettings& settings,
                                const MouseSettings& minBounds,
                                const MouseSettings& maxBounds) const;
    MouseSettings generateRandomSettings(const MouseSettings& minBounds,
                                         const MouseSettings& maxBounds);
    MouseSettings mutateSettings(const MouseSettings& base,
                                 const MouseSettings& minBounds,
                                 const MouseSettings& maxBounds,
                                 float intensity);
    RewardSample calculateReward(const AimbotTarget& target, double mouseX, double mouseY, double radius) const;
    MouseSettings interpolateSettings(const MouseSettings& a, const MouseSettings& b, float t) const;
    MouseSettings performCalibrationStep(const SharedState& snapshot,
                                         const RewardSample& sample,
                                         bool& finished);
    MouseSettings performTrainingStep(const SharedState& snapshot,
                                      const RewardSample& sample);

public:
    AITuner();
    ~AITuner();

    // Configuration
    void setAimMode(AimMode mode);
    void setLearningRate(float rate);
    void setExplorationRate(float rate);
    void setMaxIterations(int iterations);
    void setTargetRadius(float radius);
    void setAutoCalibrate(bool enabled);
    void setSettingsBounds(const MouseSettings& min, const MouseSettings& max);

    // Training control
    void startTraining();
    void stopTraining();
    void pauseTraining();
    void resumeTraining();
    void resetTraining();

    // Feedback
    void provideFeedback(const AimbotTarget& target, double mouseX, double mouseY);
    void startCalibration();
    void stopCalibration();

    // Getters
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

    // Mode-specific settings
    MouseSettings getModeSettings(AimMode mode) const;
    void applyModeSettings(AimMode mode);
};

#endif // AI_TUNER_H
