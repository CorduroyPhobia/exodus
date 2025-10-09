#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include "imgui/imgui.h"
#include <imgui_internal.h>

#include "exodus.h"
#include "include/other_tools.h"

int prev_fovX = config.fovX;
int prev_fovY = config.fovY;
float prev_minSpeedMultiplier = config.minSpeedMultiplier;
float prev_maxSpeedMultiplier = config.maxSpeedMultiplier;
float prev_predictionInterval = config.predictionInterval;
float prev_snapRadius = config.snapRadius;
float prev_nearRadius = config.nearRadius;
float prev_speedCurveExponent = config.speedCurveExponent;
float prev_snapBoostFactor = config.snapBoostFactor;

bool  prev_wind_mouse_enabled = config.wind_mouse_enabled;
float prev_wind_G = config.wind_G;
float prev_wind_W = config.wind_W;
float prev_wind_M = config.wind_M;
float prev_wind_D = config.wind_D;
float prev_wind_speed_multiplier = config.wind_speed_multiplier;
float prev_wind_min_velocity = config.wind_min_velocity;
float prev_wind_target_radius = config.wind_target_radius;

bool prev_auto_shoot = config.auto_shoot;
bool prev_auto_shoot_hold_until_off_target = config.auto_shoot_hold_until_off_target;
bool prev_auto_shoot_with_auto_hip_aim = config.auto_shoot_with_auto_hip_aim;
float prev_bScope_multiplier = config.bScope_multiplier;
float prev_auto_shoot_fire_delay_ms = config.auto_shoot_fire_delay_ms;
float prev_auto_shoot_press_duration_ms = config.auto_shoot_press_duration_ms;
float prev_auto_shoot_full_auto_grace_ms = config.auto_shoot_full_auto_grace_ms;

static void draw_target_correction_demo()
{
    if (ImGui::CollapsingHeader("Visual demo"))
    {
        ImVec2 canvas_sz(220, 220);
        ImGui::InvisibleButton("##tc_canvas", canvas_sz);

        ImVec2 p0 = ImGui::GetItemRectMin();
        ImVec2 p1 = ImGui::GetItemRectMax();
        ImVec2 center{ (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f };

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, p1, IM_COL32(25, 25, 25, 255));

        const float scale = 4.0f;
        float near_px = config.nearRadius * scale;
        float snap_px = config.snapRadius * scale;
        near_px = ImClamp(near_px, 10.0f, canvas_sz.x * 0.45f);
        snap_px = ImClamp(snap_px, 6.0f, near_px - 4.0f);

        dl->AddCircle(center, near_px, IM_COL32(80, 120, 255, 180), 64, 2.0f);
        dl->AddCircle(center, snap_px, IM_COL32(255, 100, 100, 180), 64, 2.0f);

        static float  dist_px = near_px;
        static float  vel_px = 0.0f;
        static double last_t = ImGui::GetTime();
        double now = ImGui::GetTime();
        double dt = now - last_t;
        last_t = now;

        double dist_units = dist_px / scale;
        double speed_mult;
        if (dist_units < config.snapRadius)
            speed_mult = config.minSpeedMultiplier * config.snapBoostFactor;
        else if (dist_units < config.nearRadius)
        {
            double t = dist_units / config.nearRadius;
            double crv = 1.0 - pow(1.0 - t, config.speedCurveExponent);
            speed_mult = config.minSpeedMultiplier +
                (config.maxSpeedMultiplier - config.minSpeedMultiplier) * crv;
        }
        else
        {
            double norm = ImClamp(dist_units / config.nearRadius, 0.0, 1.0);
            speed_mult = config.minSpeedMultiplier +
                (config.maxSpeedMultiplier - config.minSpeedMultiplier) * norm;
        }

        double base_px_s = 60.0;
        vel_px = static_cast<float>(base_px_s * speed_mult);
        dist_px -= vel_px * static_cast<float>(dt);
        if (dist_px <= 0.0f) dist_px = near_px;

        ImVec2 dot{ center.x - dist_px, center.y };
        dl->AddCircleFilled(dot, 4.0f, IM_COL32(255, 255, 80, 255));

        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextColored(ImVec4(0.31f, 0.48f, 1.0f, 1.0f), "Near radius");
        ImGui::SameLine(130);
        ImGui::TextColored(ImVec4(1.0f, 0.39f, 0.39f, 1.0f), "Snap radius");
    }
}

void draw_mouse()
{
    ImGui::SeparatorText("FOV");
    ImGui::SliderInt("FOV X", &config.fovX, 10, 120);
    ImGui::SliderInt("FOV Y", &config.fovY, 10, 120);

    ImGui::SeparatorText("Speed Multiplier");
    ImGui::SliderFloat("Min Speed Multiplier", &config.minSpeedMultiplier, 0.1f, 5.0f, "%.1f");
    ImGui::SliderFloat("Max Speed Multiplier", &config.maxSpeedMultiplier, 0.1f, 5.0f, "%.1f");

    ImGui::SeparatorText("Prediction");
    ImGui::SliderFloat("Prediction Interval", &config.predictionInterval, 0.00f, 0.5f, "%.2f");
    if (config.predictionInterval == 0.00f)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(255, 0, 0, 255), "-> Disabled");
    }
    else
    {
        
        if (ImGui::SliderInt("Future Positions", &config.prediction_futurePositions, 1, 40))
        {
            config.saveConfig();
        }

        ImGui::SameLine();
        if (ImGui::Checkbox("Draw##draw_future_positions_button", &config.draw_futurePositions))
        {
            config.saveConfig();
        }
    }

    ImGui::SeparatorText("Target corrention");
    ImGui::SliderFloat("Snap Radius", &config.snapRadius, 0.1f, 5.0f, "%.1f");
    ImGui::SliderFloat("Near Radius", &config.nearRadius, 1.0f, 40.0f, "%.1f");
    ImGui::SliderFloat("Speed Curve Exponent", &config.speedCurveExponent, 0.1f, 10.0f, "%.1f");
    ImGui::SliderFloat("Snap Boost Factor", &config.snapBoostFactor, 0.01f, 4.00f, "%.2f");
    draw_target_correction_demo();

    ImGui::SeparatorText("Mouse Sensitivity");

    float sens_f = static_cast<float>(config.sens);
    float yaw_f = static_cast<float>(config.yaw);
    float pitch_f = static_cast<float>(config.fovScaled ? config.pitch : config.yaw);
    float baseFOV_f = static_cast<float>(config.baseFOV);
    bool fov_scaled = config.fovScaled;

    bool sens_changed = false;
    sens_changed |= ImGui::SliderFloat("Sensitivity", &sens_f, 0.001f, 10.0f, "%.4f");
    sens_changed |= ImGui::SliderFloat("Yaw", &yaw_f, 0.001f, 0.1f, "%.4f");

    if (!fov_scaled)
        ImGui::BeginDisabled(true);
    sens_changed |= ImGui::SliderFloat("Pitch", &pitch_f, 0.001f, 0.1f, "%.4f");
    if (!fov_scaled)
        ImGui::EndDisabled();

    sens_changed |= ImGui::Checkbox("FOV Scaled", &fov_scaled);

    if (!fov_scaled)
    {
        float display_base = baseFOV_f <= 0.0f ? 90.0f : baseFOV_f;
        ImGui::BeginDisabled(true);
        ImGui::SliderFloat("Base FOV", &display_base, 10.0f, 180.0f, "%.1f");
        ImGui::EndDisabled();
    }
    else
    {
        sens_changed |= ImGui::SliderFloat("Base FOV", &baseFOV_f, 10.0f, 180.0f, "%.1f");
    }

    if (sens_changed)
    {
        config.sens = static_cast<double>(sens_f);
        config.yaw = static_cast<double>(yaw_f);
        config.fovScaled = fov_scaled;
        if (config.fovScaled)
            config.pitch = static_cast<double>(pitch_f);
        else
            config.pitch = config.yaw;
        if (config.fovScaled)
            config.baseFOV = static_cast<double>(baseFOV_f);

        config.saveConfig();
    }

    ImGui::SeparatorText("Easy No Recoil");
    ImGui::Checkbox("Easy No Recoil", &config.easynorecoil);
    if (config.easynorecoil)
    {
        ImGui::SliderFloat("No Recoil Strength", &config.easynorecoilstrength, 0.1f, 500.0f, "%.1f");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Left/Right Arrow keys: Adjust recoil strength by 10");
        
        if (config.easynorecoilstrength >= 100.0f)
        {
            ImGui::TextColored(ImVec4(255, 255, 0, 255), "WARNING: High recoil strength may be detected.");
        }
    }

    ImGui::SeparatorText("Auto Shoot");

    ImGui::Checkbox("Auto Shoot", &config.auto_shoot);
    ImGui::Checkbox("Auto Shoot During Auto Hip-Aim", &config.auto_shoot_with_auto_hip_aim);

    const bool autoShootConfigured = config.auto_shoot || config.auto_shoot_with_auto_hip_aim;
    if (autoShootConfigured)
    {
        ImGui::Checkbox("Hold Trigger Until Target Lost", &config.auto_shoot_hold_until_off_target);
        ImGui::SliderFloat("bScope Multiplier", &config.bScope_multiplier, 0.5f, 2.0f, "%.1f");
        ImGui::SliderFloat("Shot Delay (ms)", &config.auto_shoot_fire_delay_ms, 0.0f, 500.0f, "%.0f");

        if (config.auto_shoot_hold_until_off_target)
        {
            ImGui::BeginDisabled();
        }

        ImGui::SliderFloat("Shot Hold (ms)", &config.auto_shoot_press_duration_ms, 0.0f, 200.0f, "%.0f");
        ImGui::SliderFloat("Full Auto Grace (ms)", &config.auto_shoot_full_auto_grace_ms, 0.0f, 400.0f, "%.0f");

        if (config.auto_shoot_hold_until_off_target)
        {
            ImGui::EndDisabled();
            ImGui::TextDisabled("Trigger stays held until the aim leaves the target.");
        }
        else
        {
            ImGui::TextDisabled("Set Hold to 0 to keep the trigger down while a target stays in scope.");
        }
    }

    ImGui::SeparatorText("Wind mouse");

    if (ImGui::Checkbox("Enable WindMouse", &config.wind_mouse_enabled))
    {
        config.saveConfig();
    }

    if (config.wind_mouse_enabled)
    {
        if (ImGui::SliderFloat("Gravity force", &config.wind_G, 4.00f, 40.00f, "%.2f"))
        {
            config.saveConfig();
        }

        if (ImGui::SliderFloat("Wind fluctuation", &config.wind_W, 1.00f, 40.00f, "%.2f"))
        {
            config.saveConfig();
        }

        if (ImGui::SliderFloat("Max step (velocity clip)", &config.wind_M, 1.00f, 40.00f, "%.2f"))
        {
            config.saveConfig();
        }

        if (ImGui::SliderFloat("Distance where behaviour changes", &config.wind_D, 1.00f, 40.00f, "%.2f"))
        {
            config.saveConfig();
        }

        if (ImGui::SliderFloat("Speed multiplier", &config.wind_speed_multiplier, 0.10f, 5.00f, "%.2f"))
        {
            if (config.wind_speed_multiplier < 0.10f)
                config.wind_speed_multiplier = 0.10f;
            config.saveConfig();
        }

        if (ImGui::SliderFloat("Minimum speed", &config.wind_min_velocity, 0.00f, 20.00f, "%.2f"))
        {
            if (config.wind_min_velocity < 0.0f)
                config.wind_min_velocity = 0.0f;
            config.saveConfig();
        }

        if (ImGui::SliderFloat("Stick radius", &config.wind_target_radius, 0.10f, 5.00f, "%.2f"))
        {
            if (config.wind_target_radius < 0.10f)
                config.wind_target_radius = 0.10f;
            config.saveConfig();
        }

        if (ImGui::Button("Reset Wind Mouse to default settings"))
        {
            config.wind_G = 18.0f;
            config.wind_W = 15.0f;
            config.wind_M = 10.0f;
            config.wind_D = 8.0f;
            config.wind_speed_multiplier = 1.0f;
            config.wind_min_velocity = 0.0f;
            config.wind_target_radius = 1.0f;
            config.saveConfig();
        }
    }

    ImGui::SeparatorText("Input method");
    ImGui::TextDisabled("WIN32 is the only supported mouse input method.");

    ImGui::Separator();
    ImGui::TextColored(ImVec4(255, 255, 255, 100), "Do not test shooting and aiming with the overlay is open.");

    if (prev_fovX != config.fovX ||
        prev_fovY != config.fovY ||
        prev_minSpeedMultiplier != config.minSpeedMultiplier ||
        prev_maxSpeedMultiplier != config.maxSpeedMultiplier ||
        prev_predictionInterval != config.predictionInterval ||
        prev_snapRadius != config.snapRadius ||
        prev_nearRadius != config.nearRadius ||
        prev_speedCurveExponent != config.speedCurveExponent ||
        prev_snapBoostFactor != config.snapBoostFactor)
    {
        prev_fovX = config.fovX;
        prev_fovY = config.fovY;
        prev_minSpeedMultiplier = config.minSpeedMultiplier;
        prev_maxSpeedMultiplier = config.maxSpeedMultiplier;
        prev_predictionInterval = config.predictionInterval;
        prev_snapRadius = config.snapRadius;
        prev_nearRadius = config.nearRadius;
        prev_speedCurveExponent = config.speedCurveExponent;
        prev_snapBoostFactor = config.snapBoostFactor;

        globalMouseThread->updateConfig(
            config.detection_resolution,
            config.fovX,
            config.fovY,
            config.minSpeedMultiplier,
            config.maxSpeedMultiplier,
            config.predictionInterval,
            config.auto_shoot,
            config.auto_shoot_hold_until_off_target,
            config.bScope_multiplier,
            config.auto_shoot_fire_delay_ms,
            config.auto_shoot_press_duration_ms,
            config.auto_shoot_full_auto_grace_ms);

        config.saveConfig();
    }

    if (prev_wind_mouse_enabled != config.wind_mouse_enabled ||
        prev_wind_G != config.wind_G ||
        prev_wind_W != config.wind_W ||
        prev_wind_M != config.wind_M ||
        prev_wind_D != config.wind_D ||
        prev_wind_speed_multiplier != config.wind_speed_multiplier ||
        prev_wind_min_velocity != config.wind_min_velocity ||
        prev_wind_target_radius != config.wind_target_radius)
    {
        prev_wind_mouse_enabled = config.wind_mouse_enabled;
        prev_wind_G = config.wind_G;
        prev_wind_W = config.wind_W;
        prev_wind_M = config.wind_M;
        prev_wind_D = config.wind_D;
        prev_wind_speed_multiplier = config.wind_speed_multiplier;
        prev_wind_min_velocity = config.wind_min_velocity;
        prev_wind_target_radius = config.wind_target_radius;

        globalMouseThread->updateConfig(
            config.detection_resolution,
            config.fovX,
            config.fovY,
            config.minSpeedMultiplier,
            config.maxSpeedMultiplier,
            config.predictionInterval,
            config.auto_shoot,
            config.auto_shoot_hold_until_off_target,
            config.bScope_multiplier,
            config.auto_shoot_fire_delay_ms,
            config.auto_shoot_press_duration_ms,
            config.auto_shoot_full_auto_grace_ms);

        config.saveConfig();
    }

    if (prev_auto_shoot != config.auto_shoot ||
        prev_auto_shoot_hold_until_off_target != config.auto_shoot_hold_until_off_target ||
        prev_auto_shoot_with_auto_hip_aim != config.auto_shoot_with_auto_hip_aim ||
        prev_bScope_multiplier != config.bScope_multiplier ||
        prev_auto_shoot_fire_delay_ms != config.auto_shoot_fire_delay_ms ||
        prev_auto_shoot_press_duration_ms != config.auto_shoot_press_duration_ms ||
        prev_auto_shoot_full_auto_grace_ms != config.auto_shoot_full_auto_grace_ms)
    {
        prev_auto_shoot = config.auto_shoot;
        prev_auto_shoot_hold_until_off_target = config.auto_shoot_hold_until_off_target;
        prev_auto_shoot_with_auto_hip_aim = config.auto_shoot_with_auto_hip_aim;
        prev_bScope_multiplier = config.bScope_multiplier;
        prev_auto_shoot_fire_delay_ms = config.auto_shoot_fire_delay_ms;
        prev_auto_shoot_press_duration_ms = config.auto_shoot_press_duration_ms;
        prev_auto_shoot_full_auto_grace_ms = config.auto_shoot_full_auto_grace_ms;

        globalMouseThread->updateConfig(
            config.detection_resolution,
            config.fovX,
            config.fovY,
            config.minSpeedMultiplier,
            config.maxSpeedMultiplier,
            config.predictionInterval,
            config.auto_shoot,
            config.auto_shoot_hold_until_off_target,
            config.bScope_multiplier,
            config.auto_shoot_fire_delay_ms,
            config.auto_shoot_press_duration_ms,
            config.auto_shoot_full_auto_grace_ms);

        config.saveConfig();
    }
}