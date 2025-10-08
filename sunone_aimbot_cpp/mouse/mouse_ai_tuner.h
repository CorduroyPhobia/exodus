#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <random>
#include <string>

#include "config.h"
#include "mouse.h"

class MouseAITuner
{
public:
    enum class Mode : int
    {
        Manual = 0,
        AimAssist = 1,
        AimBot = 2,
        RageBaiter = 3
    };

    struct Status
    {
        bool active = false;
        bool exploring = false;
        float progress = 0.0f;
        double baselineScore = 0.0;
        double candidateScore = 0.0;
        double lastReward = 0.0;
        std::string description;
    };

    void initialize(Config* cfg, MouseThread* thread);
    void onSample(const AimbotTarget* target);
    void notifyProfileChanged();
    void notifyResolutionChanged(int resolution);
    void setMode(Mode mode);
    Mode mode() const;
    void resetMode(Mode mode);
    void resetCurrentMode();
    Status status() const;

    static Mode modeFromString(const std::string& name);
    static std::string modeToString(Mode mode);

private:
    struct ModeState
    {
        Config::MouseAIPreset best{};
        Config::MouseAIPreset exploring{};
        Config::MouseAIPreset active{};
        double baselineScore = 0.0;
        double bestWindowScore = 0.0;
        double exploringWindowScore = 0.0;
        int bestSamples = 0;
        int exploringSamples = 0;
        bool exploringActive = false;
        int staleIterations = 0;
    };

    Config* configRef_ = nullptr;
    MouseThread* threadRef_ = nullptr;
    std::array<ModeState, 4> states_{};
    Mode currentMode_ = Mode::Manual;
    bool initialized_ = false;

    mutable std::mutex tunerMutex_;
    std::mt19937 rng_{};

    Config::MouseAIPreset manualSnapshot_{};
    bool manualSnapshotValid_ = false;

    std::atomic<int> detectionResolution_{ 320 };
    double lastReward_ = 0.0;

    static constexpr int sampleWindow_ = 90;
    static constexpr double improvementThreshold_ = 0.05;
    static constexpr int staleResetLimit_ = 6;

    static size_t modeIndex(Mode mode);
    static const char* modeKey(Mode mode);

    void ensureStateInitialized(Mode mode);
    Config::MouseAIPreset midpointPreset(Mode mode) const;
    Config::MouseAIPreset randomizePreset(const Config::MouseAIPreset& base, Mode mode);
    Config::MouseAIPreset clampPreset(const Config::MouseAIPreset& preset, Mode mode) const;
    void applyPreset(Mode mode, const Config::MouseAIPreset& preset);
    void persistAll(Mode mode, const Config::MouseAIPreset* presetIfAny);
    double computeReward(const AimbotTarget* target, const ModeState& state) const;
};

extern MouseAITuner mouseAITuner;
