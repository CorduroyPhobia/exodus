#ifndef AI_TUNER_H
#define AI_TUNER_H

#include <vector>
#include <random>
#include <chrono>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>
#include <condition_variable>
#include "AimbotTarget.h"

struct MouseSettings {
    int dpi;
    float sensitivity;
    float minSpeedMultiplier;
    float maxSpeedMultiplier;
    float predictionInterval;
    float snapRadius;
    float nearRadius;
    float speedCurveExponent;
    float snapBoostFactor;
    bool wind_mouse_enabled;
    float wind_G;
    float wind_W;
    float wind_M;
    float wind_D;
    bool easynorecoil;
    float easynorecoilstrength;
    
    MouseSettings() = default;
    
    MouseSettings(int dpi, float sensitivity, float minSpeed, float maxSpeed,
                  float prediction, float snapR, float nearR, float speedCurve,
                  float snapBoost, bool windEnabled, float windG, float windW,
                  float windM, float windD, bool easyRecoil, float recoilStrength)
        : dpi(dpi), sensitivity(sensitivity), minSpeedMultiplier(minSpeed),
          maxSpeedMultiplier(maxSpeed), predictionInterval(prediction),
          snapRadius(snapR), nearRadius(nearR), speedCurveExponent(speedCurve),
          snapBoostFactor(snapBoost), wind_mouse_enabled(windEnabled),
          wind_G(windG), wind_W(windW), wind_M(windM), wind_D(windD),
          easynorecoil(easyRecoil), easynorecoilstrength(recoilStrength) {}
};

struct TrainingState {
    MouseSettings currentSettings;
    double currentReward;
    int iteration;
    bool isCalibrating;
    std::chrono::steady_clock::time_point lastUpdate;
    
    TrainingState() : currentReward(0.0), iteration(0), isCalibrating(false) {
        lastUpdate = std::chrono::steady_clock::now();
    }
};

enum class AimMode {
    AIM_ASSIST,    // Subtle assistance
    AIM_BOT,       // Good lock-on
    RAGE_BAITER    // Maximum performance
};

class AITuner {
private:
    // Configuration
    AimMode currentMode;
    float learningRate;
    float explorationRate;
    int maxIterations;
    float targetRadius;
    bool autoCalibrate;
    MouseSettings minSettings;
    MouseSettings maxSettings;
    
    // Training state
    TrainingState state;
    std::vector<MouseSettings> settingsHistory;
    std::vector<double> rewardHistory;
    
    // Random number generation
    std::mt19937 rng;
    std::uniform_real_distribution<float> floatDist;
    std::uniform_int_distribution<int> intDist;
    
    // Threading
    std::thread trainingThread;
    std::atomic<bool> shouldStop;
    std::mutex stateMutex;
    std::condition_variable cv;

    struct FeedbackSample {
        AimbotTarget target;
        double reward;
        bool targetHit;
    };

    std::queue<FeedbackSample> feedbackQueue;
    std::mutex queueMutex;
    
    // Performance tracking
    std::chrono::steady_clock::time_point lastTargetTime;
    double totalReward;
    int successfulTargets;
    int totalTargets;

    struct RewardSample {
        double reward;
        bool targetHit;
    };

    // Helper methods
    MouseSettings generateRandomSettings(const MouseSettings& minBounds, const MouseSettings& maxBounds);
    MouseSettings mutateSettings(const MouseSettings& base,
                                 const MouseSettings& minBounds,
                                 const MouseSettings& maxBounds,
                                 float explorationRateValue);
    RewardSample calculateReward(const AimbotTarget& target, double mouseX, double mouseY, double radius) const;
    MouseSettings interpolateSettings(const MouseSettings& a, const MouseSettings& b, float t);
    void updateSettings(const MouseSettings& newSettings);
    void applyModeSettingsLocked(AimMode mode);
    void trainingLoop();
    
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
    
    // Statistics
    struct TrainingStats {
        double averageReward;
        double bestReward;
        double successRate;
        int totalIterations;
        int currentIteration;
        bool isTraining;
        bool isCalibrating;
    };
    
    TrainingStats getTrainingStats() const;
    
    // Mode-specific settings
    MouseSettings getModeSettings(AimMode mode) const;
    void applyModeSettings(AimMode mode);
};

#endif // AI_TUNER_H
