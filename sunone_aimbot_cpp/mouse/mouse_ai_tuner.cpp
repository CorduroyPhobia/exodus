#include "mouse/mouse_ai_tuner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <random>

#include "sunone_aimbot_cpp.h"

MouseAITuner mouseAITuner;

namespace
{
    struct ParameterRange
    {
        double min;
        double max;
    };

    struct IntRange
    {
        int min;
        int max;
    };

    struct ModeLimits
    {
        IntRange fovX;
        IntRange fovY;
        ParameterRange minSpeed;
        ParameterRange maxSpeed;
        ParameterRange predictionInterval;
        IntRange futurePositions;
        ParameterRange snapRadius;
        ParameterRange nearRadius;
        ParameterRange speedCurveExponent;
        ParameterRange snapBoostFactor;
        ParameterRange bScopeMultiplier;
        ParameterRange fireDelay;
        ParameterRange pressDuration;
        ParameterRange fullAutoGrace;
        double autoShootProbability;
        bool allowAutoShoot;
        bool preferDrawFuture;
        bool allowZeroHold;
    };

    ModeLimits limitsForMode(MouseAITuner::Mode mode)
    {
        using Mode = MouseAITuner::Mode;
        switch (mode)
        {
        case Mode::AimAssist:
            return ModeLimits{
                { 80, 105 },
                { 60, 80 },
                { 0.05, 0.25 },
                { 0.35, 0.80 },
                { 0.000, 0.020 },
                { 5, 18 },
                { 0.8, 1.8 },
                { 15.0, 28.0 },
                { 2.0, 3.5 },
                { 1.00, 1.30 },
                { 0.85, 1.30 },
                { 80.0, 190.0 },
                { 40.0, 140.0 },
                { 100.0, 220.0 },
                0.35,
                true,
                false,
                false
            };
        case Mode::AimBot:
            return ModeLimits{
                { 90, 115 },
                { 70, 90 },
                { 0.10, 0.40 },
                { 0.80, 1.45 },
                { 0.005, 0.030 },
                { 10, 25 },
                { 1.0, 2.6 },
                { 20.0, 35.0 },
                { 2.5, 4.8 },
                { 1.10, 1.60 },
                { 0.95, 1.55 },
                { 40.0, 120.0 },
                { 0.0, 90.0 },
                { 80.0, 180.0 },
                0.75,
                true,
                true,
                true
            };
        case Mode::RageBaiter:
            return ModeLimits{
                { 100, 120 },
                { 80, 100 },
                { 0.20, 0.65 },
                { 1.20, 2.50 },
                { 0.010, 0.045 },
                { 15, 30 },
                { 1.5, 3.5 },
                { 25.0, 40.0 },
                { 3.0, 6.0 },
                { 1.20, 2.30 },
                { 1.05, 1.80 },
                { 0.0, 70.0 },
                { 0.0, 60.0 },
                { 50.0, 140.0 },
                0.92,
                true,
                true,
                true
            };
        case Mode::Manual:
        default:
            return ModeLimits{
                { 10, 120 },
                { 10, 120 },
                { 0.01, 5.0 },
                { 0.10, 5.0 },
                { 0.0, 0.50 },
                { 1, 40 },
                { 0.1, 5.0 },
                { 1.0, 40.0 },
                { 0.1, 10.0 },
                { 0.01, 4.0 },
                { 0.5, 2.0 },
                { 0.0, 500.0 },
                { 0.0, 200.0 },
                { 0.0, 400.0 },
                0.0,
                true,
                false,
                true
            };
        }
    }
}

size_t MouseAITuner::modeIndex(Mode mode)
{
    return static_cast<size_t>(mode);
}

const char* MouseAITuner::modeKey(Mode mode)
{
    switch (mode)
    {
    case Mode::AimAssist: return "aim_assist";
    case Mode::AimBot: return "aim_bot";
    case Mode::RageBaiter: return "rage_baiter";
    case Mode::Manual:
    default: return "manual";
    }
}

MouseAITuner::Mode MouseAITuner::modeFromString(const std::string& name)
{
    std::string lowered = name;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lowered == "aim_assist") return Mode::AimAssist;
    if (lowered == "aimbot" || lowered == "aim_bot") return Mode::AimBot;
    if (lowered == "rage" || lowered == "rage_baiter") return Mode::RageBaiter;
    return Mode::Manual;
}

std::string MouseAITuner::modeToString(Mode mode)
{
    switch (mode)
    {
    case Mode::AimAssist: return "aim_assist";
    case Mode::AimBot: return "aim_bot";
    case Mode::RageBaiter: return "rage_baiter";
    case Mode::Manual:
    default: return "manual";
    }
}

void MouseAITuner::initialize(Config* cfg, MouseThread* thread)
{
    std::lock_guard<std::mutex> lock(tunerMutex_);
    configRef_ = cfg;
    threadRef_ = thread;
    detectionResolution_.store(cfg ? cfg->detection_resolution : 320);
    rng_.seed(static_cast<unsigned int>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    if (cfg)
    {
        manualSnapshot_ = cfg->captureMousePreset();
        manualSnapshotValid_ = true;
    }
    for (Mode mode : { Mode::AimAssist, Mode::AimBot, Mode::RageBaiter })
    {
        ensureStateInitialized(mode);
    }
    currentMode_ = cfg ? modeFromString(cfg->mouse_ai_mode) : Mode::Manual;
    if (currentMode_ != Mode::Manual)
    {
        ensureStateInitialized(currentMode_);
        applyPreset(currentMode_, states_[modeIndex(currentMode_)].best);
    }
    initialized_ = true;
}

void MouseAITuner::setMode(Mode mode)
{
    std::lock_guard<std::mutex> lock(tunerMutex_);
    if (!initialized_ || !configRef_)
        return;

    if (mode == currentMode_)
        return;

    if (currentMode_ == Mode::Manual)
    {
        manualSnapshot_ = configRef_->captureMousePreset();
        manualSnapshotValid_ = true;
    }

    currentMode_ = mode;

    if (mode == Mode::Manual)
    {
        if (manualSnapshotValid_)
        {
            applyPreset(Mode::Manual, manualSnapshot_);
        }
        persistAll(Mode::Manual, nullptr);
        return;
    }

    ensureStateInitialized(mode);
    ModeState& state = states_[modeIndex(mode)];
    state.bestSamples = 0;
    state.bestWindowScore = 0.0;
    state.exploringSamples = 0;
    state.exploringWindowScore = 0.0;
    state.exploringActive = false;
    state.staleIterations = 0;
    applyPreset(mode, state.best);
    persistAll(mode, nullptr);
}

MouseAITuner::Mode MouseAITuner::mode() const
{
    std::lock_guard<std::mutex> lock(tunerMutex_);
    return currentMode_;
}

void MouseAITuner::notifyProfileChanged()
{
    std::lock_guard<std::mutex> lock(tunerMutex_);
    if (!initialized_ || !configRef_)
        return;

    manualSnapshot_ = configRef_->captureMousePreset();
    manualSnapshotValid_ = true;

    for (size_t i = 1; i < states_.size(); ++i)
    {
        states_[i].bestSamples = 0;
        states_[i].exploringSamples = 0;
        states_[i].bestWindowScore = 0.0;
        states_[i].exploringWindowScore = 0.0;
        states_[i].baselineScore = 0.0;
        states_[i].exploringActive = false;
        states_[i].staleIterations = 0;
    }
}

void MouseAITuner::notifyResolutionChanged(int resolution)
{
    detectionResolution_.store(resolution);
}

void MouseAITuner::resetMode(Mode mode)
{
    std::lock_guard<std::mutex> lock(tunerMutex_);
    if (!initialized_ || !configRef_)
        return;

    if (mode == Mode::Manual)
    {
        manualSnapshot_ = configRef_->captureMousePreset();
        manualSnapshotValid_ = true;
        if (currentMode_ == Mode::Manual)
        {
            applyPreset(Mode::Manual, manualSnapshot_);
            persistAll(Mode::Manual, nullptr);
        }
        return;
    }

    ModeState& state = states_[modeIndex(mode)];
    state.best = clampPreset(midpointPreset(mode), mode);
    state.active = state.best;
    state.baselineScore = 0.0;
    state.bestWindowScore = 0.0;
    state.exploringWindowScore = 0.0;
    state.bestSamples = 0;
    state.exploringSamples = 0;
    state.exploringActive = false;
    state.staleIterations = 0;

    if (currentMode_ == mode)
    {
        applyPreset(mode, state.best);
    }
    persistAll(mode, &state.best);
}

void MouseAITuner::resetCurrentMode()
{
    resetMode(mode());
}

void MouseAITuner::onSample(const AimbotTarget* target)
{
    if (!initialized_ || currentMode_ == Mode::Manual || !aiming.load())
        return;

    std::lock_guard<std::mutex> lock(tunerMutex_);
    if (!configRef_)
        return;

    ModeState& state = states_[modeIndex(currentMode_)];
    ensureStateInitialized(currentMode_);

    double reward = computeReward(target, state);
    lastReward_ = reward;

    if (!state.exploringActive)
    {
        state.bestWindowScore += reward;
        ++state.bestSamples;
        if (state.bestSamples >= sampleWindow_)
        {
            state.baselineScore = state.bestWindowScore / static_cast<double>(state.bestSamples);
            state.bestWindowScore = 0.0;
            state.bestSamples = 0;
            state.exploring = randomizePreset(state.best, currentMode_);
            state.exploringActive = true;
            state.exploringSamples = 0;
            state.exploringWindowScore = 0.0;
            applyPreset(currentMode_, state.exploring);
        }
        return;
    }

    state.exploringWindowScore += reward;
    ++state.exploringSamples;

    if (state.exploringSamples >= sampleWindow_)
    {
        double candidateScore = state.exploringWindowScore / static_cast<double>(state.exploringSamples);
        if (candidateScore > state.baselineScore + improvementThreshold_)
        {
            state.best = state.exploring;
            state.active = state.best;
            state.baselineScore = candidateScore;
            state.staleIterations = 0;
            applyPreset(currentMode_, state.best);
            persistAll(currentMode_, &state.best);
        }
        else
        {
            ++state.staleIterations;
            applyPreset(currentMode_, state.best);
            if (state.staleIterations >= staleResetLimit_)
            {
                state.best = clampPreset(midpointPreset(currentMode_), currentMode_);
                state.baselineScore = 0.0;
                state.staleIterations = 0;
                persistAll(currentMode_, &state.best);
            }
        }

        state.exploringActive = false;
        state.exploringSamples = 0;
        state.exploringWindowScore = 0.0;
    }
}

MouseAITuner::Status MouseAITuner::status() const
{
    std::lock_guard<std::mutex> lock(tunerMutex_);
    Status stat;
    stat.lastReward = lastReward_;

    if (!initialized_ || currentMode_ == Mode::Manual)
    {
        stat.active = false;
        stat.description = "Manual control";
        stat.progress = 0.0f;
        return stat;
    }

    const ModeState& state = states_[modeIndex(currentMode_)];
    stat.active = true;
    stat.exploring = state.exploringActive;
    int currentSamples = state.exploringActive ? state.exploringSamples : state.bestSamples;
    stat.progress = static_cast<float>(std::min(currentSamples, sampleWindow_)) / static_cast<float>(sampleWindow_);
    stat.baselineScore = state.baselineScore;
    stat.candidateScore = state.exploringSamples > 0
        ? state.exploringWindowScore / static_cast<double>(state.exploringSamples)
        : state.baselineScore;
    stat.description = state.exploringActive ? "Exploring new configuration" : "Measuring baseline";
    return stat;
}

void MouseAITuner::ensureStateInitialized(Mode mode)
{
    if (!configRef_ || mode == Mode::Manual)
        return;

    ModeState& state = states_[modeIndex(mode)];
    if (state.best.fovX != 0)
    {
        state.best = clampPreset(state.best, mode);
        if (state.active.fovX == 0)
            state.active = state.best;
        return;
    }

    Config::MouseAIPreset preset = midpointPreset(mode);
    {
        std::lock_guard<std::recursive_mutex> cfgLock(configMutex);
        auto it = configRef_->mouse_ai_presets.find(modeKey(mode));
        if (it != configRef_->mouse_ai_presets.end())
        {
            preset = it->second;
        }
    }
    state.best = clampPreset(preset, mode);
    state.active = state.best;
}

Config::MouseAIPreset MouseAITuner::midpointPreset(Mode mode) const
{
    ModeLimits limits = limitsForMode(mode);
    Config::MouseAIPreset preset{};

    switch (mode)
    {
    case Mode::AimAssist:
        preset.fovX = limits.fovX.min;
        preset.fovY = limits.fovY.min;
        preset.minSpeedMultiplier = static_cast<float>(limits.minSpeed.min);
        preset.maxSpeedMultiplier = static_cast<float>(limits.maxSpeed.min);
        preset.predictionInterval = static_cast<float>(limits.predictionInterval.min);
        preset.prediction_futurePositions = limits.futurePositions.min;
        preset.draw_futurePositions = false;
        preset.snapRadius = static_cast<float>(limits.snapRadius.min);
        preset.nearRadius = static_cast<float>(limits.nearRadius.min);
        preset.speedCurveExponent = static_cast<float>(limits.speedCurveExponent.min);
        preset.snapBoostFactor = static_cast<float>(limits.snapBoostFactor.min);
        preset.auto_shoot = false;
        preset.bScope_multiplier = static_cast<float>(limits.bScopeMultiplier.min);
        preset.auto_shoot_fire_delay_ms = static_cast<float>(limits.fireDelay.min);
        preset.auto_shoot_press_duration_ms = static_cast<float>(limits.pressDuration.min);
        preset.auto_shoot_full_auto_grace_ms = static_cast<float>(limits.fullAutoGrace.min);
        break;
    case Mode::AimBot:
        preset.fovX = limits.fovX.min;
        preset.fovY = limits.fovY.min;
        preset.minSpeedMultiplier = static_cast<float>(limits.minSpeed.min);
        preset.maxSpeedMultiplier = static_cast<float>(limits.maxSpeed.min);
        preset.predictionInterval = static_cast<float>(limits.predictionInterval.min);
        preset.prediction_futurePositions = limits.futurePositions.min;
        preset.draw_futurePositions = true;
        preset.snapRadius = static_cast<float>(limits.snapRadius.min);
        preset.nearRadius = static_cast<float>(limits.nearRadius.min);
        preset.speedCurveExponent = static_cast<float>(limits.speedCurveExponent.min);
        preset.snapBoostFactor = static_cast<float>(limits.snapBoostFactor.min);
        preset.auto_shoot = limits.allowAutoShoot;
        preset.bScope_multiplier = static_cast<float>(limits.bScopeMultiplier.min);
        preset.auto_shoot_fire_delay_ms = static_cast<float>(limits.fireDelay.min);
        preset.auto_shoot_press_duration_ms = static_cast<float>(limits.pressDuration.min);
        preset.auto_shoot_full_auto_grace_ms = static_cast<float>(limits.fullAutoGrace.min);
        break;
    case Mode::RageBaiter:
        preset.fovX = limits.fovX.min;
        preset.fovY = limits.fovY.min;
        preset.minSpeedMultiplier = static_cast<float>(limits.minSpeed.min);
        preset.maxSpeedMultiplier = static_cast<float>(limits.maxSpeed.min);
        preset.predictionInterval = static_cast<float>(limits.predictionInterval.min);
        preset.prediction_futurePositions = limits.futurePositions.min;
        preset.draw_futurePositions = true;
        preset.snapRadius = static_cast<float>(limits.snapRadius.min);
        preset.nearRadius = static_cast<float>(limits.nearRadius.min);
        preset.speedCurveExponent = static_cast<float>(limits.speedCurveExponent.min);
        preset.snapBoostFactor = static_cast<float>(limits.snapBoostFactor.min);
        preset.auto_shoot = limits.allowAutoShoot;
        preset.bScope_multiplier = static_cast<float>(limits.bScopeMultiplier.min);
        preset.auto_shoot_fire_delay_ms = static_cast<float>(limits.fireDelay.min);
        preset.auto_shoot_press_duration_ms = static_cast<float>(limits.pressDuration.min);
        preset.auto_shoot_full_auto_grace_ms = static_cast<float>(limits.fullAutoGrace.min);
        break;
    case Mode::Manual:
    default:
        preset.fovX = (limits.fovX.min + limits.fovX.max) / 2;
        preset.fovY = (limits.fovY.min + limits.fovY.max) / 2;
        preset.minSpeedMultiplier = static_cast<float>((limits.minSpeed.min + limits.minSpeed.max) * 0.5);
        preset.maxSpeedMultiplier = static_cast<float>((limits.maxSpeed.min + limits.maxSpeed.max) * 0.5);
        preset.predictionInterval = static_cast<float>((limits.predictionInterval.min + limits.predictionInterval.max) * 0.5);
        preset.prediction_futurePositions = (limits.futurePositions.min + limits.futurePositions.max) / 2;
        preset.draw_futurePositions = limits.preferDrawFuture;
        preset.snapRadius = static_cast<float>((limits.snapRadius.min + limits.snapRadius.max) * 0.5);
        preset.nearRadius = static_cast<float>((limits.nearRadius.min + limits.nearRadius.max) * 0.5);
        preset.speedCurveExponent = static_cast<float>((limits.speedCurveExponent.min + limits.speedCurveExponent.max) * 0.5);
        preset.snapBoostFactor = static_cast<float>((limits.snapBoostFactor.min + limits.snapBoostFactor.max) * 0.5);
        preset.auto_shoot = limits.allowAutoShoot && limits.autoShootProbability > 0.5;
        preset.bScope_multiplier = static_cast<float>((limits.bScopeMultiplier.min + limits.bScopeMultiplier.max) * 0.5);
        preset.auto_shoot_fire_delay_ms = static_cast<float>((limits.fireDelay.min + limits.fireDelay.max) * 0.5);
        preset.auto_shoot_press_duration_ms = static_cast<float>((limits.pressDuration.min + limits.pressDuration.max) * 0.5);
        preset.auto_shoot_full_auto_grace_ms = static_cast<float>((limits.fullAutoGrace.min + limits.fullAutoGrace.max) * 0.5);
        break;
    }

    if (!limits.allowZeroHold && preset.auto_shoot_press_duration_ms < 10.0f)
        preset.auto_shoot_press_duration_ms = 10.0f;

    return clampPreset(preset, mode);
}

Config::MouseAIPreset MouseAITuner::randomizePreset(const Config::MouseAIPreset& base, Mode mode)
{
    ModeLimits limits = limitsForMode(mode);
    Config::MouseAIPreset preset = base;

    std::normal_distribution<double> gaussian01(0.0, 1.0);

    auto sampleInt = [&](int value, const IntRange& range)
    {
        double sigma = std::max(1.0, (range.max - range.min) * 0.15);
        int candidate = static_cast<int>(std::round(value + gaussian01(rng_) * sigma));
        if (candidate < range.min || candidate > range.max)
        {
            std::uniform_int_distribution<int> uni(range.min, range.max);
            candidate = uni(rng_);
        }
        return candidate;
    };

    auto sampleFloat = [&](float value, const ParameterRange& range)
    {
        double span = range.max - range.min;
        double sigma = std::max(0.01, span * 0.15);
        double candidate = value + gaussian01(rng_) * sigma;
        if (candidate < range.min || candidate > range.max)
        {
            std::uniform_real_distribution<double> uni(range.min, range.max);
            candidate = uni(rng_);
        }
        return static_cast<float>(candidate);
    };

    preset.fovX = sampleInt(base.fovX, limits.fovX);
    preset.fovY = sampleInt(base.fovY, limits.fovY);
    preset.minSpeedMultiplier = sampleFloat(base.minSpeedMultiplier, limits.minSpeed);
    preset.maxSpeedMultiplier = sampleFloat(base.maxSpeedMultiplier, limits.maxSpeed);
    preset.predictionInterval = sampleFloat(base.predictionInterval, limits.predictionInterval);
    preset.prediction_futurePositions = sampleInt(base.prediction_futurePositions, limits.futurePositions);
    preset.draw_futurePositions = limits.preferDrawFuture ? true : preset.draw_futurePositions;
    preset.snapRadius = sampleFloat(base.snapRadius, limits.snapRadius);
    preset.nearRadius = sampleFloat(base.nearRadius, limits.nearRadius);
    preset.speedCurveExponent = sampleFloat(base.speedCurveExponent, limits.speedCurveExponent);
    preset.snapBoostFactor = sampleFloat(base.snapBoostFactor, limits.snapBoostFactor);
    std::bernoulli_distribution toggle(limits.autoShootProbability);
    if (limits.allowAutoShoot)
    {
        preset.auto_shoot = toggle(rng_);
    }
    else
    {
        preset.auto_shoot = false;
    }
    preset.bScope_multiplier = sampleFloat(base.bScope_multiplier, limits.bScopeMultiplier);
    preset.auto_shoot_fire_delay_ms = sampleFloat(base.auto_shoot_fire_delay_ms, limits.fireDelay);
    preset.auto_shoot_press_duration_ms = sampleFloat(base.auto_shoot_press_duration_ms, limits.pressDuration);
    preset.auto_shoot_full_auto_grace_ms = sampleFloat(base.auto_shoot_full_auto_grace_ms, limits.fullAutoGrace);

    if (!limits.preferDrawFuture)
    {
        // Randomize drawing in subtle mode to avoid clutter.
        std::bernoulli_distribution drawToggle(0.25);
        preset.draw_futurePositions = drawToggle(rng_);
    }

    return clampPreset(preset, mode);
}

Config::MouseAIPreset MouseAITuner::clampPreset(const Config::MouseAIPreset& preset, Mode mode) const
{
    Config::MouseAIPreset result = preset;
    ModeLimits limits = limitsForMode(mode);

    auto clampInt = [](int value, const IntRange& range)
    {
        return std::clamp(value, range.min, range.max);
    };
    auto clampFloat = [](float value, const ParameterRange& range)
    {
        return static_cast<float>(std::clamp<double>(value, range.min, range.max));
    };

    if (mode != Mode::Manual)
    {
        result.fovX = clampInt(result.fovX, limits.fovX);
        result.fovY = clampInt(result.fovY, limits.fovY);
        result.minSpeedMultiplier = clampFloat(result.minSpeedMultiplier, limits.minSpeed);
        result.maxSpeedMultiplier = clampFloat(result.maxSpeedMultiplier, limits.maxSpeed);
        result.predictionInterval = clampFloat(result.predictionInterval, limits.predictionInterval);
        result.prediction_futurePositions = clampInt(result.prediction_futurePositions, limits.futurePositions);
        result.snapRadius = clampFloat(result.snapRadius, limits.snapRadius);
        result.nearRadius = clampFloat(result.nearRadius, limits.nearRadius);
        result.speedCurveExponent = clampFloat(result.speedCurveExponent, limits.speedCurveExponent);
        result.snapBoostFactor = clampFloat(result.snapBoostFactor, limits.snapBoostFactor);
        result.bScope_multiplier = clampFloat(result.bScope_multiplier, limits.bScopeMultiplier);
        result.auto_shoot_fire_delay_ms = clampFloat(result.auto_shoot_fire_delay_ms, limits.fireDelay);
        result.auto_shoot_press_duration_ms = clampFloat(result.auto_shoot_press_duration_ms, limits.pressDuration);
        result.auto_shoot_full_auto_grace_ms = clampFloat(result.auto_shoot_full_auto_grace_ms, limits.fullAutoGrace);
        if (!limits.allowAutoShoot)
            result.auto_shoot = false;
        if (!limits.allowZeroHold && result.auto_shoot_press_duration_ms < 5.0f)
            result.auto_shoot_press_duration_ms = 5.0f;
        result.draw_futurePositions = limits.preferDrawFuture ? true : result.draw_futurePositions;
    }
    else
    {
        result.fovX = clampInt(result.fovX, limits.fovX);
        result.fovY = clampInt(result.fovY, limits.fovY);
        result.minSpeedMultiplier = std::max(0.01f, result.minSpeedMultiplier);
        result.maxSpeedMultiplier = std::max(result.minSpeedMultiplier + 0.01f, result.maxSpeedMultiplier);
        result.prediction_futurePositions = std::max(1, result.prediction_futurePositions);
        result.snapRadius = std::max(0.1f, result.snapRadius);
        result.nearRadius = std::max(result.snapRadius + 0.1f, result.nearRadius);
        result.speedCurveExponent = std::max(0.1f, result.speedCurveExponent);
        result.snapBoostFactor = std::max(0.01f, result.snapBoostFactor);
        result.auto_shoot_fire_delay_ms = std::max(0.0f, result.auto_shoot_fire_delay_ms);
        result.auto_shoot_press_duration_ms = std::max(0.0f, result.auto_shoot_press_duration_ms);
        result.auto_shoot_full_auto_grace_ms = std::max(0.0f, result.auto_shoot_full_auto_grace_ms);
    }

    if (result.maxSpeedMultiplier < result.minSpeedMultiplier + 0.05f)
        result.maxSpeedMultiplier = result.minSpeedMultiplier + 0.05f;

    if (result.nearRadius <= result.snapRadius)
        result.nearRadius = result.snapRadius + 0.5f;

    return result;
}

void MouseAITuner::applyPreset(Mode mode, const Config::MouseAIPreset& preset)
{
    if (!configRef_ || !threadRef_)
        return;

    Config::MouseAIPreset sanitized = clampPreset(preset, mode);
    ModeState& state = states_[modeIndex(mode)];
    state.active = sanitized;

    int resolution = detectionResolution_.load();
    int fovX = sanitized.fovX;
    int fovY = sanitized.fovY;
    double minSpeed = sanitized.minSpeedMultiplier;
    double maxSpeed = sanitized.maxSpeedMultiplier;
    double predictionInterval = sanitized.predictionInterval;
    bool autoShoot = sanitized.auto_shoot;
    float bScope = sanitized.bScope_multiplier;
    double fireDelay = sanitized.auto_shoot_fire_delay_ms;
    double pressDuration = sanitized.auto_shoot_press_duration_ms;
    double grace = sanitized.auto_shoot_full_auto_grace_ms;

    {
        std::lock_guard<std::recursive_mutex> cfgLock(configMutex);
        configRef_->fovX = sanitized.fovX;
        configRef_->fovY = sanitized.fovY;
        configRef_->minSpeedMultiplier = sanitized.minSpeedMultiplier;
        configRef_->maxSpeedMultiplier = sanitized.maxSpeedMultiplier;
        configRef_->predictionInterval = sanitized.predictionInterval;
        configRef_->prediction_futurePositions = sanitized.prediction_futurePositions;
        configRef_->draw_futurePositions = sanitized.draw_futurePositions;
        configRef_->snapRadius = sanitized.snapRadius;
        configRef_->nearRadius = sanitized.nearRadius;
        configRef_->speedCurveExponent = sanitized.speedCurveExponent;
        configRef_->snapBoostFactor = sanitized.snapBoostFactor;
        configRef_->auto_shoot = sanitized.auto_shoot;
        configRef_->bScope_multiplier = sanitized.bScope_multiplier;
        configRef_->auto_shoot_fire_delay_ms = sanitized.auto_shoot_fire_delay_ms;
        configRef_->auto_shoot_press_duration_ms = sanitized.auto_shoot_press_duration_ms;
        configRef_->auto_shoot_full_auto_grace_ms = sanitized.auto_shoot_full_auto_grace_ms;
    }

    threadRef_->updateConfig(
        resolution,
        fovX,
        fovY,
        minSpeed,
        maxSpeed,
        predictionInterval,
        autoShoot,
        bScope,
        fireDelay,
        pressDuration,
        grace);
}

void MouseAITuner::persistAll(Mode mode, const Config::MouseAIPreset* presetIfAny)
{
    if (!configRef_)
        return;

    std::lock_guard<std::recursive_mutex> cfgLock(configMutex);
    configRef_->mouse_ai_mode = modeToString(currentMode_);
    if (presetIfAny && mode != Mode::Manual)
    {
        configRef_->mouse_ai_presets[modeKey(mode)] = *presetIfAny;
    }
    configRef_->saveConfig();
}

double MouseAITuner::computeReward(const AimbotTarget* target, const ModeState& state) const
{
    if (!target)
        return -0.1;

    int res = detectionResolution_.load();
    double center = res / 2.0;
    double dx = target->pivotX - center;
    double dy = target->pivotY - center;
    double distance = std::hypot(dx, dy);

    double nearRadius = std::max(1.0, static_cast<double>(state.active.nearRadius));
    double snapRadius = std::max(0.5, static_cast<double>(state.active.snapRadius));

    bool inScope = false;
    if (threadRef_)
    {
        double reduction = state.active.auto_shoot ? state.active.bScope_multiplier : 1.0;
        inScope = threadRef_->check_target_in_scope(target->x, target->y, target->w, target->h, reduction);
    }

    double reward = 0.0;
    if (inScope)
        reward += 1.5;

    double closeness = 1.0 - std::min(distance, nearRadius) / nearRadius;
    reward += std::max(0.0, closeness);

    if (distance <= snapRadius)
        reward += 0.5;

    if (!inScope && distance > nearRadius)
    {
        reward -= std::min(1.0, (distance - nearRadius) / nearRadius);
    }

    return reward;
}
