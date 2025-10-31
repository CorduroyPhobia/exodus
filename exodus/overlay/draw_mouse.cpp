#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include "imgui/imgui.h"
#include <imgui_internal.h>

#include "exodus.h"
#include "include/other_tools.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <random>
#include <vector>

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
float prev_wind_randomness = config.wind_randomness;
float prev_wind_inertia = config.wind_inertia;
float prev_wind_step_randomness = config.wind_step_randomness;

bool prev_auto_shoot = config.auto_shoot;
bool prev_auto_shoot_hold_until_off_target = config.auto_shoot_hold_until_off_target;
bool prev_auto_shoot_with_auto_hip_aim = config.auto_shoot_with_auto_hip_aim;
float prev_bScope_multiplier = config.bScope_multiplier;
float prev_auto_shoot_fire_delay_ms = config.auto_shoot_fire_delay_ms;
float prev_auto_shoot_press_duration_ms = config.auto_shoot_press_duration_ms;
float prev_auto_shoot_full_auto_grace_ms = config.auto_shoot_full_auto_grace_ms;

static bool g_show_aim_advanced = false;
static bool g_show_wind_advanced = false;

static void show_help_tooltip(const char* text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

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

struct WindPreviewSnapshot
{
    float G;
    float W;
    float M;
    float D;
    float speedMultiplier;
    float minVelocity;
    float targetRadius;
    float randomness;
    float inertia;
    float stepRandomness;
    int   enabled;
};

static std::vector<ImVec2> simulate_wind_preview(const WindPreviewSnapshot& snap, ImU32 seed, int dx, int dy)
{
    std::vector<ImVec2> points;
    points.emplace_back(0.0f, 0.0f);

    if (!snap.enabled)
    {
        points.emplace_back(static_cast<float>(dx), static_cast<float>(dy));
        return points;
    }

    constexpr double SQRT3 = 1.7320508075688772;
    constexpr double SQRT5 = 2.23606797749979;

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist01(0.0, 1.0);
    std::uniform_real_distribution<double> distNegPos(-1.0, 1.0);

    double sx = 0.0, sy = 0.0;
    double targetX = static_cast<double>(dx);
    double targetY = static_cast<double>(dy);
    double vx = 0.0, vy = 0.0;
    double wX = 0.0, wY = 0.0;
    double baseMaxStep = std::max(0.5, static_cast<double>(snap.M));
    double dynamicMaxStep = baseMaxStep;
    int cx = 0, cy = 0;

    double tolerance = std::max(0.1, static_cast<double>(snap.targetRadius));
    double speedScale = std::max(0.1, static_cast<double>(snap.speedMultiplier));
    double minSpeed = std::max(0.0, static_cast<double>(snap.minVelocity));
    double minSpeedClamp = snap.M > 0.0f
        ? std::min(minSpeed, static_cast<double>(snap.M))
        : minSpeed;
    double randomnessScale = std::max(0.0, static_cast<double>(snap.randomness));
    double inertiaScale = std::clamp(static_cast<double>(snap.inertia), 0.0, 2.0);
    double inertiaFactor = 0.2 + inertiaScale * 0.6;
    double clipRandomness = std::clamp(static_cast<double>(snap.stepRandomness), 0.0, 1.0);

    const int maxIterations = 2048;
    int iterations = 0;

    while (true)
    {
        double remaining = std::hypot(targetX - sx, targetY - sy);
        if (remaining < tolerance || iterations++ >= maxIterations)
            break;

        double dist = remaining;
        double wMag = std::min(static_cast<double>(snap.W), dist);

        if (dist >= snap.D)
        {
            wX = wX / SQRT3 + distNegPos(rng) * wMag / SQRT5 * randomnessScale;
            wY = wY / SQRT3 + distNegPos(rng) * wMag / SQRT5 * randomnessScale;
        }
        else
        {
            wX /= SQRT3;
            wY /= SQRT3;
            dynamicMaxStep = dynamicMaxStep < 3.0 ? dist01(rng) * 3.0 + 3.0 : dynamicMaxStep / SQRT5;
        }

        double divisor = dist > 1e-6 ? dist : 1.0;
        vx = vx * inertiaFactor + wX + snap.G * (targetX - sx) / divisor;
        vy = vy * inertiaFactor + wY + snap.G * (targetY - sy) / divisor;

        vx *= speedScale;
        vy *= speedScale;

        double vMag = std::hypot(vx, vy);
        if (vMag > dynamicMaxStep && dynamicMaxStep > 0.0)
        {
            double minFactor = std::clamp(1.0 - clipRandomness, 0.0, 1.0);
            double factor = minFactor;
            if (clipRandomness > 0.0)
            {
                factor = minFactor + clipRandomness * dist01(rng);
                factor = std::clamp(factor, 0.0, 1.0);
            }
            double vClip = dynamicMaxStep * factor;
            if (vMag > 0.0)
            {
                vx = (vx / vMag) * vClip;
                vy = (vy / vMag) * vClip;
                vMag = std::hypot(vx, vy);
            }
        }

        if (minSpeedClamp > 0.0 && vMag < minSpeedClamp)
        {
            if (vMag == 0.0)
            {
                double angle = std::atan2(targetY - sy, targetX - sx);
                vx = std::cos(angle) * minSpeedClamp;
                vy = std::sin(angle) * minSpeedClamp;
            }
            else
            {
                double scale = minSpeedClamp / vMag;
                vx *= scale;
                vy *= scale;
            }
        }

        sx += vx;
        sy += vy;
        int rx = static_cast<int>(std::round(sx));
        int ry = static_cast<int>(std::round(sy));
        if (rx != cx || ry != cy)
        {
            cx = rx;
            cy = ry;
            points.emplace_back(static_cast<float>(cx), static_cast<float>(cy));
        }
    }

    int final_x = static_cast<int>(std::round(targetX)) - cx;
    int final_y = static_cast<int>(std::round(targetY)) - cy;
    if (final_x || final_y)
    {
        cx += final_x;
        cy += final_y;
        points.emplace_back(static_cast<float>(cx), static_cast<float>(cy));
    }

    if (points.size() == 1)
        points.emplace_back(static_cast<float>(dx), static_cast<float>(dy));

    return points;
}

static void draw_wind_mouse_demo()
{
    if (!ImGui::CollapsingHeader("Wind mouse demo"))
        return;

    ImVec2 canvas_sz(260.0f, 200.0f);
    ImGui::InvisibleButton("##wind_canvas", canvas_sz);
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(22, 22, 22, 255));
    dl->AddRect(p0, p1, IM_COL32(60, 60, 60, 255));

    ImVec2 start{ p0.x + 28.0f, p1.y - 28.0f };
    ImVec2 target{ p1.x - 28.0f, p0.y + 48.0f };
    const int dx = 140;
    const int dy = -90;

    float scaleX = (target.x - start.x) / static_cast<float>(dx);
    float scaleY = (target.y - start.y) / static_cast<float>(dy);

    WindPreviewSnapshot snap{
        config.wind_G,
        config.wind_W,
        config.wind_M,
        config.wind_D,
        config.wind_speed_multiplier,
        config.wind_min_velocity,
        config.wind_target_radius,
        config.wind_randomness,
        config.wind_inertia,
        config.wind_step_randomness,
        config.wind_mouse_enabled ? 1 : 0
    };

    ImU32 snapshotHash = ImHashData(&snap, sizeof(snap));
    static ImU32 cachedHash = 0;
    static std::vector<ImVec2> cachedPath;
    if (cachedHash != snapshotHash)
    {
        cachedPath = simulate_wind_preview(snap, snapshotHash, dx, dy);
        cachedHash = snapshotHash;
    }

    std::vector<ImVec2> mapped;
    mapped.reserve(cachedPath.size());
    for (const ImVec2& pt : cachedPath)
    {
        mapped.emplace_back(
            start.x + pt.x * scaleX,
            start.y + pt.y * scaleY);
    }

    if (mapped.size() >= 2)
    {
        ImU32 pathColor = config.wind_mouse_enabled ? IM_COL32(120, 200, 255, 220) : IM_COL32(170, 170, 170, 220);
        for (size_t i = 1; i < mapped.size(); ++i)
        {
            dl->AddLine(mapped[i - 1], mapped[i], pathColor, 2.0f);
        }

        static double lastTime = ImGui::GetTime();
        static double travelled = 0.0;
        static ImU32 animHash = 0;
        double now = ImGui::GetTime();
        double dt = now - lastTime;
        lastTime = now;
        if (animHash != snapshotHash)
        {
            travelled = 0.0;
            animHash = snapshotHash;
        }

        float totalLen = 0.0f;
        std::vector<float> cumulative(mapped.size(), 0.0f);
        for (size_t i = 1; i < mapped.size(); ++i)
        {
            const ImVec2 delta(mapped[i].x - mapped[i - 1].x, mapped[i].y - mapped[i - 1].y);
            float segLen = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            totalLen += segLen;
            cumulative[i] = totalLen;
        }

        if (totalLen > 0.0f)
        {
            const float previewSpeed = 80.0f;
            travelled += dt * previewSpeed;
            while (travelled > totalLen)
                travelled -= totalLen;

            ImVec2 marker = mapped.back();
            for (size_t i = 1; i < mapped.size(); ++i)
            {
                if (travelled <= cumulative[i])
                {
                    float segmentLen = cumulative[i] - cumulative[i - 1];
                    float t = segmentLen > 0.0f ? static_cast<float>((travelled - cumulative[i - 1]) / segmentLen) : 0.0f;
                    marker = ImLerp(mapped[i - 1], mapped[i], t);
                    break;
                }
            }
            dl->AddCircleFilled(marker, 4.5f, IM_COL32(255, 255, 120, 255));
        }
    }

    float avgScale = (std::abs(scaleX) + std::abs(scaleY)) * 0.5f;
    float behaviourRadius = config.wind_D * avgScale;
    float stickRadius = config.wind_target_radius * avgScale;
    behaviourRadius = ImClamp(behaviourRadius, 8.0f, canvas_sz.x * 0.48f);
    stickRadius = ImClamp(stickRadius, 4.0f, behaviourRadius - 4.0f);

    dl->AddCircle(target, behaviourRadius, IM_COL32(84, 142, 255, 180), 64, 1.5f);
    dl->AddCircle(target, stickRadius, IM_COL32(255, 180, 100, 200), 64, 1.5f);

    dl->AddCircleFilled(start, 5.0f, IM_COL32(110, 220, 110, 255));
    dl->AddCircleFilled(target, 5.0f, IM_COL32(220, 110, 110, 255));

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextColored(ImVec4(0.44f, 0.67f, 1.0f, 1.0f), "Behavior change radius");
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.35f, 1.0f), "Stick radius");
    ImGui::TextColored(ImVec4(0.44f, 0.86f, 0.44f, 1.0f), "Start");
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::TextColored(ImVec4(1.0f, 0.44f, 0.44f, 1.0f), "Target");
    ImGui::TextDisabled("Animated point follows the simulated WindMouse path.");
}

void draw_mouse()
{
    ImGui::SeparatorText("FOV");
    ImGui::SliderInt("FOV X", &config.fovX, 10, 120);
    ImGui::SliderInt("FOV Y", &config.fovY, 10, 120);

    ImGui::SeparatorText("Aim behaviour");

    float response = config.aim_response_control;
    if (ImGui::SliderFloat("Responsiveness", &response, 0.0f, 1.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
    {
        config.applyAimProfile(response, config.aim_smooth_control, config.aim_stickiness_control);
        config.saveConfig();
    }
    ImGui::SameLine();
    const char* responseDesc = response < 0.33f ? "Calm" : (response < 0.66f ? "Balanced" : "Aggressive");
    ImGui::TextDisabled("%s", responseDesc);
    ImGui::SameLine();
    show_help_tooltip("Sets how quickly the cursor commits to the target. Higher values accelerate harder and increase predictive lead.");

    float smooth = config.aim_smooth_control;
    if (ImGui::SliderFloat("Smoothness##aim_profile", &smooth, 0.0f, 1.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
    {
        config.applyAimProfile(config.aim_response_control, smooth, config.aim_stickiness_control);
        config.saveConfig();
    }
    ImGui::SameLine();
    const char* smoothDesc = smooth < 0.33f ? "Snappy" : (smooth < 0.66f ? "Blended" : "Gliding");
    ImGui::TextDisabled("%s", smoothDesc);
    ImGui::SameLine();
    show_help_tooltip("Controls how much the motion eases versus snapping in a straight line. Higher values blend the curve and reduce visible micro-jitter.");

    float stick = config.aim_stickiness_control;
    if (ImGui::SliderFloat("Stickiness", &stick, 0.0f, 1.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
    {
        config.applyAimProfile(config.aim_response_control, config.aim_smooth_control, stick);
        config.saveConfig();
    }
    ImGui::SameLine();
    const char* stickDesc = stick < 0.33f ? "Loose" : (stick < 0.66f ? "Focused" : "Locked");
    ImGui::TextDisabled("%s", stickDesc);
    ImGui::SameLine();
    show_help_tooltip("Sets how firmly the aim settles on the last few pixels. Higher values widen the stick zone and raise the motion noise floor, keeping the cursor planted once centred.");

    double timeConstant = 0.012 + (0.15 * (1.0 - static_cast<double>(config.aim_response_control)));
    double halfLifeMs = std::log(2.0) * timeConstant * 1000.0;
    double leadMs = (config.predictionInterval + 0.05 * config.tracking_prediction_boost) * 1000.0;
    ImGui::TextDisabled("Half-life %.0f ms   Lead %.0f ms   Stick radius %.2f px", halfLifeMs, leadMs, config.snapRadius);
    ImGui::TextDisabled("Speed window %.2f× – %.2f× counts   Noise floor %.2f px", config.minSpeedMultiplier, config.maxSpeedMultiplier, config.tracking_noise_floor);

    ImGui::Checkbox("Show advanced aim tuning", &g_show_aim_advanced);
    ImGui::SameLine();
    show_help_tooltip("Reveal the legacy aim sliders for manual tweaking. Changes automatically sync back to the profile controls above.");

    if (g_show_aim_advanced)
    {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.58f, 0.78f, 1.0f, 1.0f), "Advanced aim parameters");

        bool aimAdvancedChanged = false;
        aimAdvancedChanged |= ImGui::SliderFloat("Min Speed Multiplier", &config.minSpeedMultiplier, 0.1f, 5.0f, "%.1f");
        aimAdvancedChanged |= ImGui::SliderFloat("Max Speed Multiplier", &config.maxSpeedMultiplier, 0.1f, 5.0f, "%.1f");

        ImGui::SeparatorText("Prediction");
        if (ImGui::SliderFloat("Prediction Interval", &config.predictionInterval, 0.00f, 0.5f, "%.2f"))
        {
            aimAdvancedChanged = true;
        }
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

        ImGui::SeparatorText("Target correction");
        if (ImGui::SliderFloat("Snap Radius", &config.snapRadius, 0.1f, 5.0f, "%.1f"))
        {
            aimAdvancedChanged = true;
        }
        if (ImGui::SliderFloat("Near Radius", &config.nearRadius, 1.0f, 40.0f, "%.1f"))
        {
            aimAdvancedChanged = true;
        }
        if (ImGui::SliderFloat("Speed Curve Exponent", &config.speedCurveExponent, 0.1f, 10.0f, "%.1f"))
        {
            aimAdvancedChanged = true;
        }
        if (ImGui::SliderFloat("Snap Boost Factor", &config.snapBoostFactor, 0.01f, 4.00f, "%.2f"))
        {
            aimAdvancedChanged = true;
        }
        draw_target_correction_demo();

        if (aimAdvancedChanged)
        {
            config.deriveAimProfileFromRaw();
            config.saveConfig();
        }
    }

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

    bool simpleWindChanged = false;

    float followControl = ImSaturate((config.wind_G - 6.0f) / 26.0f);
    float smoothControl = ImSaturate((config.wind_D - 2.5f) / 18.0f);
    float jitterControl = ImSaturate(config.wind_randomness / 2.0f);

    if (ImGui::SliderFloat("Follow speed", &followControl, 0.0f, 1.0f, "%.0f%%"))
    {
        config.wind_G = 6.0f + followControl * 26.0f;
        config.wind_speed_multiplier = 0.45f + followControl * 1.10f;
        config.wind_min_velocity = followControl * 6.0f;
        simpleWindChanged = true;
    }
    ImGui::SameLine();
    const char* followDesc = followControl < 0.33f ? "Relaxed" : (followControl < 0.66f ? "Balanced" : "Snappy");
    ImGui::TextDisabled("%s", followDesc);
    ImGui::SameLine();
    show_help_tooltip("Controls how quickly the cursor commits to the target. Higher values accelerate harder and raise the minimum speed, making the aim more aggressive.");

    if (ImGui::SliderFloat("Smoothness", &smoothControl, 0.0f, 1.0f, "%.0f%%"))
    {
        config.wind_D = 2.5f + smoothControl * 18.0f;
        config.wind_target_radius = 0.3f + (1.0f - smoothControl) * 1.2f;
        config.wind_M = 4.0f + (1.0f - smoothControl) * 12.0f;
        simpleWindChanged = true;
    }
    ImGui::SameLine();
    const char* windSmoothDesc = smoothControl < 0.33f ? "Snappy" : (smoothControl < 0.66f ? "Blended" : "Gliding");
    ImGui::TextDisabled("%s", windSmoothDesc);
    ImGui::SameLine();
    show_help_tooltip("Sets how gently the aim eases into the target. Higher smoothness widens the blending zone and shrinks the stick radius, producing a floatier glide.");

    if (ImGui::SliderFloat("Human jitter", &jitterControl, 0.0f, 1.0f, "%.0f%%"))
    {
        config.wind_randomness = jitterControl * 2.0f;
        config.wind_step_randomness = 0.15f + jitterControl * 0.7f;
        config.wind_W = 4.0f + jitterControl * 24.0f;
        simpleWindChanged = true;
    }
    ImGui::SameLine();
    const char* jitterDesc = jitterControl < 0.33f ? "Minimal" : (jitterControl < 0.66f ? "Natural" : "Chaotic");
    ImGui::TextDisabled("%s", jitterDesc);
    ImGui::SameLine();
    show_help_tooltip("Introduces subtle wobble to the motion. Higher values randomize both the wind gusts and the velocity clipping to mimic human imperfections.");

    if (simpleWindChanged)
    {
        float inertiaMix = 0.35f + smoothControl * 0.8f + followControl * 0.25f;
        config.wind_inertia = ImClamp(inertiaMix, 0.0f, 2.0f);
        config.saveConfig();
    }

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextDisabled("Tracking summary");
    float followStrength = config.wind_G * config.wind_speed_multiplier;
    float followScore = ImSaturate(followStrength / 180.0f);
    float smoothScore = ImSaturate(config.wind_inertia / 1.6f);
    float stickScore = ImSaturate(1.0f - (config.wind_target_radius - 0.1f) / (5.0f - 0.1f));

    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "Follow strength %d%%", static_cast<int>(std::round(followScore * 100.0f)));
    ImGui::ProgressBar(followScore, ImVec2(-FLT_MIN, 0.0f), buffer);
    std::snprintf(buffer, sizeof(buffer), "Momentum %.0f%%", smoothScore * 100.0f);
    ImGui::ProgressBar(smoothScore, ImVec2(-FLT_MIN, 0.0f), buffer);
    std::snprintf(buffer, sizeof(buffer), "Stickiness %.0f%%", stickScore * 100.0f);
    ImGui::ProgressBar(stickScore, ImVec2(-FLT_MIN, 0.0f), buffer);

    ImGui::Checkbox("Show advanced wind controls", &g_show_wind_advanced);
    ImGui::TextDisabled("Advanced sliders expose the raw WindMouse parameters.");

    if (g_show_wind_advanced && config.wind_mouse_enabled)
    {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.58f, 0.78f, 1.0f, 1.0f), "Advanced WindMouse parameters");

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

        if (ImGui::SliderFloat("Noise strength", &config.wind_randomness, 0.00f, 2.00f, "%.2f"))
        {
            if (config.wind_randomness < 0.0f)
                config.wind_randomness = 0.0f;
            config.saveConfig();
        }

        if (ImGui::SliderFloat("Velocity memory", &config.wind_inertia, 0.00f, 2.00f, "%.2f"))
        {
            config.wind_inertia = ImClamp(config.wind_inertia, 0.0f, 2.0f);
            config.saveConfig();
        }

        if (ImGui::SliderFloat("Step randomness", &config.wind_step_randomness, 0.00f, 1.00f, "%.2f"))
        {
            config.wind_step_randomness = ImClamp(config.wind_step_randomness, 0.0f, 1.0f);
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
            config.wind_W = 16.0f;
            config.wind_M = 10.0f;
            config.wind_D = 9.0f;
            config.wind_speed_multiplier = 1.05f;
            config.wind_min_velocity = 0.5f;
            config.wind_target_radius = 0.9f;
            config.wind_randomness = 1.0f;
            config.wind_inertia = 1.0f;
            config.wind_step_randomness = 0.55f;
            config.saveConfig();
        }
    }

    draw_wind_mouse_demo();

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
        prev_wind_target_radius != config.wind_target_radius ||
        prev_wind_randomness != config.wind_randomness ||
        prev_wind_inertia != config.wind_inertia ||
        prev_wind_step_randomness != config.wind_step_randomness)
    {
        prev_wind_mouse_enabled = config.wind_mouse_enabled;
        prev_wind_G = config.wind_G;
        prev_wind_W = config.wind_W;
        prev_wind_M = config.wind_M;
        prev_wind_D = config.wind_D;
        prev_wind_speed_multiplier = config.wind_speed_multiplier;
        prev_wind_min_velocity = config.wind_min_velocity;
        prev_wind_target_radius = config.wind_target_radius;
        prev_wind_randomness = config.wind_randomness;
        prev_wind_inertia = config.wind_inertia;
        prev_wind_step_randomness = config.wind_step_randomness;

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