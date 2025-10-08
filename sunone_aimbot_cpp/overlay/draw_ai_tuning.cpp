#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include "imgui/imgui.h"
#include "sunone_aimbot_cpp.h"
#include "overlay.h"
#include "../ai/AITuner.h"

extern AITuner* globalAITuner;
extern std::mutex globalAITunerMutex;
static bool aiTunerInitialized = false;

void draw_ai_tuning()
{

    ImGui::SeparatorText("AI Tuning System");
    
    // Enable/Disable AI Tuning
    bool aiEnabled = config.ai_tuning_enabled;
    if (ImGui::Checkbox("Enable AI Tuning", &aiEnabled)) {
        config.ai_tuning_enabled = aiEnabled;
        config.saveConfig();
        
        std::lock_guard<std::mutex> lock(globalAITunerMutex);
        
        if (aiEnabled) {
            // Create AI tuner if it doesn't exist
            if (!globalAITuner && !aiTunerInitialized) {
                try {
                    aiTunerInitialized = true;
                    globalAITuner = new AITuner();
                    globalAITuner->setLearningRate(config.ai_learning_rate);
                    globalAITuner->setExplorationRate(config.ai_exploration_rate);
                    globalAITuner->setMaxIterations(config.ai_training_iterations);
                    globalAITuner->setTargetRadius(config.ai_target_radius);
                    globalAITuner->setAutoCalibrate(config.ai_auto_calibrate);
                    
                    // Set aim mode
                    AimMode mode = AimMode::AIM_ASSIST;
                    if (config.ai_aim_mode == "aim_bot") mode = AimMode::AIM_BOT;
                    else if (config.ai_aim_mode == "rage_baiter") mode = AimMode::RAGE_BAITER;
                    globalAITuner->setAimMode(mode);
                    
                    // Set settings bounds
                    MouseSettings minSettings(config.ai_dpi_min, config.ai_sensitivity_min, 0.01f, 0.01f, 0.001f, 0.5f, 5.0f, 1.0f, 1.0f, false, 10.0f, 5.0f, 5.0f, 3.0f, false, 0.0f);
                    MouseSettings maxSettings(config.ai_dpi_max, config.ai_sensitivity_max, 2.0f, 2.0f, 0.1f, 5.0f, 100.0f, 10.0f, 3.0f, true, 50.0f, 30.0f, 20.0f, 15.0f, true, 2.0f);
                    globalAITuner->setSettingsBounds(minSettings, maxSettings);
                    
                    std::cout << "[AI Tuner] Created successfully" << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "[AI Tuner] Failed to create: " << e.what() << std::endl;
                    globalAITuner = nullptr;
                    aiTunerInitialized = false;
                    config.ai_tuning_enabled = false;
                    config.saveConfig();
                }
            }
            
            if (globalAITuner) {
                globalAITuner->startTraining();
            }
        } else {
            if (globalAITuner) {
                globalAITuner->stopTraining();
                delete globalAITuner;
                globalAITuner = nullptr;
                aiTunerInitialized = false;
                std::cout << "[AI Tuner] Destroyed" << std::endl;
            }
        }
    }
    
    if (!aiEnabled) {
        ImGui::TextDisabled("AI Tuning is disabled. Enable it to use automatic mouse setting optimization.");
        return;
    }
    
    ImGui::SeparatorText("Aim Mode");
    
    // Aim Mode Selection
    static const char* aim_modes[] = { "Aim Assist", "Aim Bot", "Rage Baiter" };
    static int selected_mode = 0;
    
    // Map config string to index
    if (config.ai_aim_mode == "aim_assist") selected_mode = 0;
    else if (config.ai_aim_mode == "aim_bot") selected_mode = 1;
    else if (config.ai_aim_mode == "rage_baiter") selected_mode = 2;
    
    if (ImGui::Combo("Aim Mode", &selected_mode, aim_modes, 3)) {
        switch (selected_mode) {
            case 0: config.ai_aim_mode = "aim_assist"; break;
            case 1: config.ai_aim_mode = "aim_bot"; break;
            case 2: config.ai_aim_mode = "rage_baiter"; break;
        }
        config.saveConfig();
        
        // Apply mode to AI tuner
        if (globalAITuner) {
            AimMode mode = static_cast<AimMode>(selected_mode);
            globalAITuner->setAimMode(mode);
        }
    }
    
    // Mode descriptions
    ImGui::TextDisabled("Aim Assist: Subtle assistance with moderate settings");
    ImGui::TextDisabled("Aim Bot: Good lock-on with more aggressive settings");
    ImGui::TextDisabled("Rage Baiter: Maximum performance with most aggressive settings");
    
    ImGui::SeparatorText("Training Parameters");
    
    // Learning Rate
    float learningRate = config.ai_learning_rate;
    if (ImGui::SliderFloat("Learning Rate", &learningRate, 0.001f, 0.1f, "%.3f")) {
        config.ai_learning_rate = learningRate;
        config.saveConfig();
        if (globalAITuner) {
            globalAITuner->setLearningRate(learningRate);
        }
    }
    
    // Exploration Rate
    float explorationRate = config.ai_exploration_rate;
    if (ImGui::SliderFloat("Exploration Rate", &explorationRate, 0.01f, 0.5f, "%.2f")) {
        config.ai_exploration_rate = explorationRate;
        config.saveConfig();
        if (globalAITuner) {
            globalAITuner->setExplorationRate(explorationRate);
        }
    }
    
    // Target Radius
    float targetRadius = config.ai_target_radius;
    if (ImGui::SliderFloat("Target Radius", &targetRadius, 1.0f, 50.0f, "%.1f")) {
        config.ai_target_radius = targetRadius;
        config.saveConfig();
        if (globalAITuner) {
            globalAITuner->setTargetRadius(targetRadius);
        }
    }
    
    // Auto Calibrate
    bool autoCalibrate = config.ai_auto_calibrate;
    if (ImGui::Checkbox("Auto Calibrate", &autoCalibrate)) {
        config.ai_auto_calibrate = autoCalibrate;
        config.saveConfig();
        if (globalAITuner) {
            globalAITuner->setAutoCalibrate(autoCalibrate);
        }
    }
    
    ImGui::SeparatorText("Training Control");
    
    // Training status
    bool isTraining = globalAITuner ? globalAITuner->isTraining() : false;
    bool isCalibrating = globalAITuner ? globalAITuner->isCalibrating() : false;
    
    if (isTraining) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Training: ACTIVE");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Training: INACTIVE");
    }
    
    if (isCalibrating) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Calibration: ACTIVE");
    }
    
    // Training controls
    if (globalAITuner) {
        if (!isTraining) {
            if (ImGui::Button("Start Training")) {
                globalAITuner->startTraining();
            }
        } else {
            if (ImGui::Button("Stop Training")) {
                globalAITuner->stopTraining();
            }
            ImGui::SameLine();
            if (ImGui::Button("Pause Training")) {
                globalAITuner->pauseTraining();
            }
        }
        
        if (!isCalibrating) {
            ImGui::SameLine();
            if (ImGui::Button("Start Calibration")) {
                globalAITuner->startCalibration();
            }
        } else {
            ImGui::SameLine();
            if (ImGui::Button("Stop Calibration")) {
                globalAITuner->stopCalibration();
            }
        }
        
        if (ImGui::Button("Reset Training")) {
            globalAITuner->resetTraining();
        }
    }
    
    ImGui::SeparatorText("Training Statistics");
    
    // Get training stats
    AITuner::TrainingStats stats;
    if (globalAITuner) {
        stats = globalAITuner->getTrainingStats();
    } else {
        stats = AITuner::TrainingStats{0.0, 0.0, 0.0, 0, 0, false, false};
    }
    
    ImGui::Text("Current Iteration: %d / %d", stats.currentIteration, stats.totalIterations);
    ImGui::Text("Average Reward: %.3f", stats.averageReward);
    ImGui::Text("Best Reward: %.3f", stats.bestReward);
    ImGui::Text("Success Rate: %.1f%%", stats.successRate * 100.0f);
    
    // Progress bar
    float progress = stats.totalIterations > 0 ? (float)stats.currentIteration / stats.totalIterations : 0.0f;
    ImGui::ProgressBar(progress, ImVec2(-1, 0), "");
    
    ImGui::SeparatorText("Current Settings");
    
    // Display current AI-tuned settings
    MouseSettings currentSettings;
    if (globalAITuner) {
        currentSettings = globalAITuner->getCurrentSettings();
    } else {
        currentSettings = MouseSettings();
    }
    
    ImGui::Text("DPI: %d", currentSettings.dpi);
    ImGui::Text("Sensitivity: %.2f", currentSettings.sensitivity);
    ImGui::Text("Min Speed Multiplier: %.2f", currentSettings.minSpeedMultiplier);
    ImGui::Text("Max Speed Multiplier: %.2f", currentSettings.maxSpeedMultiplier);
    ImGui::Text("Prediction Interval: %.3f", currentSettings.predictionInterval);
    ImGui::Text("Snap Radius: %.2f", currentSettings.snapRadius);
    ImGui::Text("Near Radius: %.2f", currentSettings.nearRadius);
    ImGui::Text("Speed Curve Exponent: %.2f", currentSettings.speedCurveExponent);
    ImGui::Text("Snap Boost Factor: %.2f", currentSettings.snapBoostFactor);
    ImGui::Text("Wind Mouse: %s", currentSettings.wind_mouse_enabled ? "Enabled" : "Disabled");
    
    if (currentSettings.wind_mouse_enabled) {
        ImGui::Text("  Wind G: %.2f", currentSettings.wind_G);
        ImGui::Text("  Wind W: %.2f", currentSettings.wind_W);
        ImGui::Text("  Wind M: %.2f", currentSettings.wind_M);
        ImGui::Text("  Wind D: %.2f", currentSettings.wind_D);
    }
    
    ImGui::Text("Easy No Recoil: %s", currentSettings.easynorecoil ? "Enabled" : "Disabled");
    if (currentSettings.easynorecoil) {
        ImGui::Text("  Recoil Strength: %.2f", currentSettings.easynorecoilstrength);
    }
    
    ImGui::SeparatorText("Settings Bounds");
    
    // Settings bounds configuration
    ImGui::Text("Sensitivity Range: %.2f - %.2f", config.ai_sensitivity_min, config.ai_sensitivity_max);
    ImGui::Text("DPI Range: %d - %d", config.ai_dpi_min, config.ai_dpi_max);
    
    if (ImGui::Button("Apply Best Settings") && globalAITuner) {
        auto bestSettings = globalAITuner->getBestSettings();
        
        // Apply best settings to config
        config.dpi = bestSettings.dpi;
        config.sensitivity = bestSettings.sensitivity;
        config.minSpeedMultiplier = bestSettings.minSpeedMultiplier;
        config.maxSpeedMultiplier = bestSettings.maxSpeedMultiplier;
        config.predictionInterval = bestSettings.predictionInterval;
        config.snapRadius = bestSettings.snapRadius;
        config.nearRadius = bestSettings.nearRadius;
        config.speedCurveExponent = bestSettings.speedCurveExponent;
        config.snapBoostFactor = bestSettings.snapBoostFactor;
        config.wind_mouse_enabled = bestSettings.wind_mouse_enabled;
        config.wind_G = bestSettings.wind_G;
        config.wind_W = bestSettings.wind_W;
        config.wind_M = bestSettings.wind_M;
        config.wind_D = bestSettings.wind_D;
        config.easynorecoil = bestSettings.easynorecoil;
        config.easynorecoilstrength = bestSettings.easynorecoilstrength;
        
        config.saveConfig();
        
        // Update mouse thread with new settings
        if (globalMouseThread) {
            globalMouseThread->updateConfig(
                config.detection_resolution,
                config.fovX,
                config.fovY,
                config.minSpeedMultiplier,
                config.maxSpeedMultiplier,
                config.predictionInterval,
                config.auto_shoot,
                config.bScope_multiplier,
                config.auto_shoot_fire_delay_ms,
                config.auto_shoot_press_duration_ms,
                config.auto_shoot_full_auto_grace_ms
            );
        }
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Reset to Defaults") && globalAITuner) {
        // Reset to default mode settings
        globalAITuner->applyModeSettings(static_cast<AimMode>(selected_mode));
    }
    
    ImGui::SeparatorText("Information");
    ImGui::TextWrapped("The AI tuning system automatically adjusts mouse settings based on your performance. "
                      "It rewards the system when the mouse is on target and penalizes it when it's not. "
                      "The system will gradually improve settings over time through reinforcement learning.");
    
    ImGui::TextWrapped("Calibration mode starts with very low sensitivity and gradually increases it until "
                      "optimal performance is achieved. This is useful when switching games or weapons.");
}
