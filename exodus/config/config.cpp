#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <windows.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <filesystem>
#include <unordered_map>

#include "config.h"
#include "modules/SimpleIni.h"

std::vector<std::string> Config::splitString(const std::string& str, char delimiter)
{
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delimiter))
    {
        while (!item.empty() && (item.front() == ' ' || item.front() == '\t'))
            item.erase(item.begin());
        while (!item.empty() && (item.back() == ' ' || item.back() == '\t'))
            item.pop_back();

        tokens.push_back(item);
    }
    return tokens;
}

std::string Config::joinStrings(const std::vector<std::string>& vec, const std::string& delimiter) const
{
    std::ostringstream oss;
    for (size_t i = 0; i < vec.size(); ++i)
    {
        if (i != 0) oss << delimiter;
        oss << vec[i];
    }
    return oss.str();
}

static void writeConfigToStream(std::ostream& file, const Config& cfg, const std::string* presetName)
{
    file << "# An explanation of the options can be found at:\n";
    file << "# https://github.com/ExodusTeam/exodus-docs/blob/main/config/config_cpp.md\n";
    if (presetName)
    {
        file << "# Preset name = " << *presetName << "\n";
    }
    file << "\n";

    // Capture
    file << "# Capture\n"
        << "capture_method = " << cfg.capture_method << "\n"
        << "detection_resolution = " << cfg.detection_resolution << "\n"
        << "capture_fps = " << cfg.capture_fps << "\n"
        << "monitor_idx = " << cfg.monitor_idx << "\n"
        << "circle_mask = " << (cfg.circle_mask ? "true" : "false") << "\n"
        << "capture_borders = " << (cfg.capture_borders ? "true" : "false") << "\n"
        << "capture_cursor = " << (cfg.capture_cursor ? "true" : "false") << "\n"
        << "virtual_camera_name = " << cfg.virtual_camera_name << "\n"
        << "virtual_camera_width = " << cfg.virtual_camera_width << "\n"
        << "virtual_camera_heigth = " << cfg.virtual_camera_heigth << "\n\n";

    // Target
    file << "# Target\n"
        << "disable_headshot = " << (cfg.disable_headshot ? "true" : "false") << "\n"
        << std::fixed << std::setprecision(2)
        << "body_y_offset = " << cfg.body_y_offset << "\n"
        << "head_y_offset = " << cfg.head_y_offset << "\n"
        << "body_distance_compensation = " << cfg.body_distance_compensation << "\n"
        << "head_distance_compensation = " << cfg.head_distance_compensation << "\n"
        << "ignore_third_person = " << (cfg.ignore_third_person ? "true" : "false") << "\n"
        << "shooting_range_targets = " << (cfg.shooting_range_targets ? "true" : "false") << "\n"
        << "auto_aim = " << (cfg.auto_aim ? "true" : "false") << "\n"
        << "auto_hip_aim = " << (cfg.auto_hip_aim ? "true" : "false") << "\n\n";

    // Mouse
    double normalizedPitch = cfg.fovScaled ? cfg.pitch : cfg.yaw;

    file << "# Mouse move\n"
        << "fovX = " << cfg.fovX << "\n"
        << "fovY = " << cfg.fovY << "\n"
        << "minSpeedMultiplier = " << cfg.minSpeedMultiplier << "\n"
        << "maxSpeedMultiplier = " << cfg.maxSpeedMultiplier << "\n"
        << std::fixed << std::setprecision(4)
        << "sensitivity = " << cfg.sens << "\n"
        << "yaw = " << cfg.yaw << "\n"
        << "pitch = " << normalizedPitch << "\n"
        << "fov_scaled = " << (cfg.fovScaled ? "true" : "false") << "\n"
        << std::setprecision(2)
        << "base_fov = " << cfg.baseFOV << "\n"

        << "predictionInterval = " << cfg.predictionInterval << "\n"
        << "prediction_futurePositions = " << cfg.prediction_futurePositions << "\n"
        << "draw_futurePositions = " << (cfg.draw_futurePositions ? "true" : "false") << "\n"

        << "snapRadius = " << cfg.snapRadius << "\n"
        << "nearRadius = " << cfg.nearRadius << "\n"
        << "speedCurveExponent = " << cfg.speedCurveExponent << "\n"
        << "snapBoostFactor = " << cfg.snapBoostFactor << "\n"
        << "aim_response_control = " << cfg.aim_response_control << "\n"
        << "aim_smooth_control = " << cfg.aim_smooth_control << "\n"
        << "aim_stickiness_control = " << cfg.aim_stickiness_control << "\n"
        << "tracking_noise_floor = " << cfg.tracking_noise_floor << "\n"
        << "tracking_prediction_boost = " << cfg.tracking_prediction_boost << "\n"

        << "easynorecoil = " << (cfg.easynorecoil ? "true" : "false") << "\n"
        << std::fixed << std::setprecision(1)
        << "easynorecoilstrength = " << cfg.easynorecoilstrength << "\n\n";

    // Wind mouse
    file << "# Wind mouse\n"
        << "wind_mouse_enabled = " << (cfg.wind_mouse_enabled ? "true" : "false") << "\n"
        << "wind_G = " << cfg.wind_G << "\n"
        << "wind_W = " << cfg.wind_W << "\n"
        << "wind_M = " << cfg.wind_M << "\n"
        << "wind_D = " << cfg.wind_D << "\n"
        << "wind_speed_multiplier = " << cfg.wind_speed_multiplier << "\n"
        << "wind_min_velocity = " << cfg.wind_min_velocity << "\n"
        << "wind_target_radius = " << cfg.wind_target_radius << "\n"
        << "wind_randomness = " << cfg.wind_randomness << "\n"
        << "wind_inertia = " << cfg.wind_inertia << "\n"
        << "wind_step_randomness = " << cfg.wind_step_randomness << "\n\n";

    // Mouse shooting
    file << "# Mouse shooting\n"
        << "auto_shoot = " << (cfg.auto_shoot ? "true" : "false") << "\n"
        << "auto_shoot_hold_until_off_target = " << (cfg.auto_shoot_hold_until_off_target ? "true" : "false") << "\n"
        << "auto_shoot_with_auto_hip_aim = " << (cfg.auto_shoot_with_auto_hip_aim ? "true" : "false") << "\n"
        << std::fixed << std::setprecision(1)
        << "bScope_multiplier = " << cfg.bScope_multiplier << "\n"
        << "auto_shoot_fire_delay_ms = " << cfg.auto_shoot_fire_delay_ms << "\n"
        << "auto_shoot_press_duration_ms = " << cfg.auto_shoot_press_duration_ms << "\n"
        << "auto_shoot_full_auto_grace_ms = " << cfg.auto_shoot_full_auto_grace_ms << "\n\n";

    // AI
    file << "# AI\n"
        << "backend = " << cfg.backend << "\n"
        << "dml_device_id = " << cfg.dml_device_id << "\n"
        << "ai_model = " << cfg.ai_model << "\n"
        << std::fixed << std::setprecision(2)
        << "confidence_threshold = " << cfg.confidence_threshold << "\n"
        << "hip_aim_confidence_threshold = " << cfg.hip_aim_confidence_threshold << "\n"
        << "hip_aim_min_box_area = " << cfg.hip_aim_min_box_area << "\n"
        << "nms_threshold = " << cfg.nms_threshold << "\n"
        << std::setprecision(0)
        << "max_detections = " << cfg.max_detections << "\n"
        << "postprocess = " << cfg.postprocess << "\n"
        << "batch_size = " << cfg.batch_size << "\n"
        << "fixed_input_size = " << (cfg.fixed_input_size ? "true" : "false") << "\n";

    // Buttons
    file << "# Buttons\n"
        << "button_targeting = " << cfg.joinStrings(cfg.button_targeting) << "\n"
        << "button_shoot = " << cfg.joinStrings(cfg.button_shoot) << "\n"
        << "button_zoom = " << cfg.joinStrings(cfg.button_zoom) << "\n"
        << "button_exit = " << cfg.joinStrings(cfg.button_exit) << "\n"
        << "button_pause = " << cfg.joinStrings(cfg.button_pause) << "\n"
        << "button_reload_config = " << cfg.joinStrings(cfg.button_reload_config) << "\n"
        << "button_open_overlay = " << cfg.joinStrings(cfg.button_open_overlay) << "\n"
        << "enable_arrows_settings = " << (cfg.enable_arrows_settings ? "true" : "false") << "\n\n";

    // Overlay
    file << "# Overlay\n"
        << "overlay_opacity = " << cfg.overlay_opacity << "\n"
        << std::fixed << std::setprecision(2)
        << "overlay_ui_scale = " << cfg.overlay_ui_scale << "\n"
        << "pause_when_overlay_open = " << (cfg.pause_when_overlay_open ? "true" : "false") << "\n\n";

    // Presets
    file << "# Presets\n"
        << "active_preset = " << cfg.active_preset << "\n\n";

    // Custom Classes
    file << "# Custom Classes\n"
        << "class_player = " << cfg.class_player << "\n"
        << "class_bot = " << cfg.class_bot << "\n"
        << "class_hideout_target_human = " << cfg.class_hideout_target_human << "\n"
        << "class_hideout_target_balls = " << cfg.class_hideout_target_balls << "\n"
        << "class_head = " << cfg.class_head << "\n"
        << "class_third_person = " << cfg.class_third_person << "\n\n";

    // Debug
    file << "# Debug\n"
        << "show_window = " << (cfg.show_window ? "true" : "false") << "\n"
        << "show_fps = " << (cfg.show_fps ? "true" : "false") << "\n"
        << "screenshot_button = " << cfg.joinStrings(cfg.screenshot_button) << "\n"
        << "screenshot_delay = " << cfg.screenshot_delay << "\n"
        << "verbose = " << (cfg.verbose ? "true" : "false") << "\n\n";

    // Active game
    file << "# Active game profile\n";
    file << "active_game = " << cfg.active_game << "\n\n";
    file << "[Games]\n";
    for (const auto& kv : cfg.game_profiles)
    {
        const auto& gp = kv.second;
        file << gp.name << " = true\n";
    }
}

bool Config::loadConfig(const std::string& filename)
{
    if (!std::filesystem::exists(filename))
    {

        // Capture
        capture_method = "duplication_api";
        detection_resolution = 320;
        capture_fps = 120;
        monitor_idx = 0;
        circle_mask = true;
        capture_borders = true;
        capture_cursor = true;
        virtual_camera_name = "None";
        virtual_camera_width = 1920;
        virtual_camera_heigth = 1080;

        // Target
        disable_headshot = false;
        body_y_offset = 0.15f;
        head_y_offset = 1.00f;
        body_distance_compensation = 0.0f;
        head_distance_compensation = 0.0f;
        ignore_third_person = false;
        shooting_range_targets = false;
        auto_aim = false;
        auto_hip_aim = true;

        // Mouse
        fovX = 96;
        fovY = 73;

        aim_response_control = 0.62f;
        aim_smooth_control = 0.55f;
        aim_stickiness_control = 0.58f;
        tracking_noise_floor = 0.0f;
        tracking_prediction_boost = 0.0f;

        applyAimProfile(aim_response_control, aim_smooth_control, aim_stickiness_control);

        prediction_futurePositions = 9;
        draw_futurePositions = true;

        sens = 0.54;
        yaw = 0.02;
        pitch = 0.02;
        fovScaled = false;
        baseFOV = 0.0;

        easynorecoil = false;
        easynorecoilstrength = 0.0f;

        // Wind mouse
        wind_mouse_enabled = true;
        wind_G = 18.0f;
        wind_W = 16.0f;
        wind_M = 10.0f;
        wind_D = 9.0f;
        wind_speed_multiplier = 1.05f;
        wind_min_velocity = 0.5f;
        wind_target_radius = 0.9f;
        wind_randomness = 1.0f;
        wind_inertia = 1.0f;
        wind_step_randomness = 0.55f;

        // Mouse shooting
        auto_shoot = false;
        auto_shoot_hold_until_off_target = false;
        auto_shoot_with_auto_hip_aim = false;
        bScope_multiplier = 1.1f;
        auto_shoot_fire_delay_ms = 139.9f;
        auto_shoot_press_duration_ms = 98.3f;
        auto_shoot_full_auto_grace_ms = 180.8f;

        // AI
        backend = "DML";
        dml_device_id = 0;

        ai_model = "sunxds_0.7.6.onnx";

        confidence_threshold = 0.50f;
        hip_aim_confidence_threshold = 0.80f;
        hip_aim_min_box_area = 0.10f;
        nms_threshold = 0.50f;
        max_detections = 100;

        postprocess = "yolo12";
        batch_size = 8;
        fixed_input_size = false;

        // Buttons
        button_targeting = splitString("RightMouseButton");
        button_shoot = splitString("LeftMouseButton");
        button_zoom = splitString("RightMouseButton");
        button_exit = splitString("F2");
        button_pause = splitString("F3");
        button_reload_config = splitString("F4");
        button_open_overlay = splitString("F5");
        enable_arrows_settings = false;

        // Overlay
        overlay_opacity = 225;
        overlay_ui_scale = 1.0f;
        pause_when_overlay_open = true;

        // Custom classes
        class_player = 0;
        class_bot = 1;
        class_hideout_target_human = 5;
        class_hideout_target_balls = 6;
        class_head = 7;
        class_third_person = 10;

        // Debug
        show_window = true;
        show_fps = false;
        screenshot_button = splitString("None");
        screenshot_delay = 500;
        verbose = false;

        game_profiles.clear();
        GameProfile unified{};
        unified.name = "UNIFIED";
        game_profiles[unified.name] = unified;
        active_game = unified.name;

        saveConfig(filename);
        return true;
    }

    CSimpleIniA ini;
    ini.SetUnicode();
    SI_Error rc = ini.LoadFile(filename.c_str());
    if (rc < 0)
    {
        std::cerr << "[Config] Error parsing INI file: " << filename << std::endl;
        return false;
    }

    auto get_string = [&](const char* key, const std::string& defval)
    {
        const char* val = ini.GetValue("", key, defval.c_str());
        return val ? std::string(val) : defval;
    };

    auto get_bool = [&](const char* key, bool defval)
        {
            return ini.GetBoolValue("", key, defval);
        };

    auto get_long = [&](const char* key, long defval)
        {
            return (int)ini.GetLongValue("", key, defval);
        };

    auto get_double = [&](const char* key, double defval)
        {
            return ini.GetDoubleValue("", key, defval);
        };

    bool has_sens_key = ini.GetValue("", "sensitivity", nullptr) != nullptr;
    bool has_yaw_key = ini.GetValue("", "yaw", nullptr) != nullptr;
    bool has_pitch_key = ini.GetValue("", "pitch", nullptr) != nullptr;
    bool has_fov_scaled_key = ini.GetValue("", "fov_scaled", nullptr) != nullptr;
    bool has_base_fov_key = ini.GetValue("", "base_fov", nullptr) != nullptr;

    sens = get_double("sensitivity", 0.54);
    yaw = get_double("yaw", 0.02);
    pitch = get_double("pitch", yaw);
    fovScaled = get_bool("fov_scaled", false);
    baseFOV = get_double("base_fov", 0.0);

    game_profiles.clear();

    struct LegacyProfileData
    {
        double sens = 0.0;
        double yaw = 0.0;
        double pitch = 0.0;
        bool hasPitch = false;
        bool hasFovScaled = false;
        bool fovScaled = false;
        bool hasBaseFOV = false;
        double baseFOV = 0.0;
    };
    std::unordered_map<std::string, LegacyProfileData> legacy_profiles;

    CSimpleIniA::TNamesDepend keys;
    ini.GetAllKeys("Games", keys);

    for (const auto& k : keys)
    {
        std::string name = k.pItem;
        std::string val = ini.GetValue("Games", k.pItem, "");

        GameProfile gp;
        gp.name = name;
        game_profiles[name] = gp;

        if (val.empty())
            continue;

        auto parts = splitString(val, ',');
        if (parts.size() < 2)
            continue;

        try
        {
            LegacyProfileData legacy;
            legacy.sens = std::stod(parts[0]);
            legacy.yaw = std::stod(parts[1]);
            legacy.pitch = legacy.yaw;
            if (parts.size() > 2)
            {
                legacy.pitch = std::stod(parts[2]);
                legacy.hasPitch = true;
            }
            if (parts.size() > 3)
            {
                legacy.hasFovScaled = true;
                legacy.fovScaled = (parts[3] == "true" || parts[3] == "1");
            }
            if (parts.size() > 4)
            {
                legacy.baseFOV = std::stod(parts[4]);
                legacy.hasBaseFOV = true;
            }

            legacy_profiles[name] = legacy;
        }
        catch (const std::exception& e)
        {
            std::cerr << "[Config] Failed to parse legacy profile: " << name
                << " = " << val << " (" << e.what() << ")" << std::endl;
        }
    }

    if (!game_profiles.count("UNIFIED"))
    {
        GameProfile uni;
        uni.name = "UNIFIED";
        game_profiles[uni.name] = uni;
    }

    active_game = "UNIFIED";
    active_game = get_string("active_game", active_game);
    if (!game_profiles.count(active_game) && !game_profiles.empty())
        active_game = game_profiles.begin()->first;

    auto legacyLookup = [&](const std::string& key) -> const LegacyProfileData*
    {
        auto it = legacy_profiles.find(key);
        if (it != legacy_profiles.end())
            return &it->second;
        return nullptr;
    };

    const LegacyProfileData* legacy = legacyLookup(active_game);
    if (!legacy)
        legacy = legacyLookup("UNIFIED");
    if (!legacy && !legacy_profiles.empty())
        legacy = &legacy_profiles.begin()->second;

    if (legacy)
    {
        if (!has_sens_key)
            sens = legacy->sens;
        if (!has_yaw_key)
            yaw = legacy->yaw;
        if (!has_pitch_key)
        {
            pitch = legacy->hasPitch ? legacy->pitch : legacy->yaw;
        }
        if (!has_fov_scaled_key && legacy->hasFovScaled)
            fovScaled = legacy->fovScaled;
        if (!has_base_fov_key && legacy->hasBaseFOV)
            baseFOV = legacy->baseFOV;
    }

    if (!fovScaled)
        pitch = yaw;

    if (sens < 0.0)
        sens = 0.0;
    if (yaw < 0.0)
        yaw = 0.0;
    if (pitch < 0.0)
        pitch = 0.0;
    if (baseFOV < 0.0)
        baseFOV = 0.0;

    // Capture
    capture_method = get_string("capture_method", "duplication_api");
    detection_resolution = get_long("detection_resolution", 320);
    if (detection_resolution != 160 && detection_resolution != 320 && detection_resolution != 640)
        detection_resolution = 320;

    capture_fps = get_long("capture_fps", 120);
    monitor_idx = get_long("monitor_idx", 0);
    circle_mask = get_bool("circle_mask", true);
    capture_borders = get_bool("capture_borders", true);
    capture_cursor = get_bool("capture_cursor", true);
    virtual_camera_name = get_string("virtual_camera_name", "None");
    virtual_camera_width = get_long("virtual_camera_width", 1920);
    virtual_camera_heigth = get_long("virtual_camera_heigth", 1080);

    // Target
    disable_headshot = get_bool("disable_headshot", false);
    body_y_offset = (float)get_double("body_y_offset", 0.15);
    head_y_offset = (float)get_double("head_y_offset", 1.00);
    body_distance_compensation = (float)std::clamp(get_double("body_distance_compensation", 0.0), -1.0, 1.0);
    head_distance_compensation = (float)std::clamp(get_double("head_distance_compensation", 0.0), -1.0, 1.0);
    ignore_third_person = get_bool("ignore_third_person", false);
    shooting_range_targets = get_bool("shooting_range_targets", false);
    auto_aim = get_bool("auto_aim", false);
    auto_hip_aim = get_bool("auto_hip_aim", true);

    // Mouse
    fovX = get_long("fovX", 96);
    fovY = get_long("fovY", 73);
    minSpeedMultiplier = (float)get_double("minSpeedMultiplier", 0.19);
    maxSpeedMultiplier = (float)get_double("maxSpeedMultiplier", 0.20);

    predictionInterval = (float)get_double("predictionInterval", 0.01);
    prediction_futurePositions = get_long("prediction_futurePositions", 9);
    draw_futurePositions = get_bool("draw_futurePositions", true);
    
    snapRadius = (float)get_double("snapRadius", 0.62);
    nearRadius = (float)get_double("nearRadius", 13.89);
    speedCurveExponent = (float)get_double("speedCurveExponent", 1.81);
    snapBoostFactor = (float)get_double("snapBoostFactor", 1.14);

    aim_response_control = (float)get_double("aim_response_control", -1.0);
    aim_smooth_control = (float)get_double("aim_smooth_control", -1.0);
    aim_stickiness_control = (float)get_double("aim_stickiness_control", -1.0);
    tracking_noise_floor = (float)get_double("tracking_noise_floor", 0.0);
    tracking_prediction_boost = (float)get_double("tracking_prediction_boost", 0.0);

    if (aim_response_control < 0.0f || aim_smooth_control < 0.0f || aim_stickiness_control < 0.0f)
    {
        deriveAimProfileFromRaw();
    }
    else
    {
        aim_response_control = std::clamp(aim_response_control, 0.0f, 1.0f);
        aim_smooth_control = std::clamp(aim_smooth_control, 0.0f, 1.0f);
        aim_stickiness_control = std::clamp(aim_stickiness_control, 0.0f, 1.0f);

        if (tracking_noise_floor <= 0.0f)
            tracking_noise_floor = 0.12f + aim_stickiness_control * 0.9f;
        if (tracking_prediction_boost <= 0.0f)
            tracking_prediction_boost = 0.35f + aim_response_control * 0.45f;
    }

    easynorecoil = get_bool("easynorecoil", false);
    easynorecoilstrength = (float)get_double("easynorecoilstrength", 0.0);

    // Wind mouse
    wind_mouse_enabled = get_bool("wind_mouse_enabled", true);
    wind_G = (float)get_double("wind_G", 18.0f);
    wind_W = (float)get_double("wind_W", 16.0f);
    wind_M = (float)get_double("wind_M", 10.0f);
    wind_D = (float)get_double("wind_D", 9.0f);
    wind_speed_multiplier = (float)get_double("wind_speed_multiplier", 1.05f);
    wind_min_velocity = (float)get_double("wind_min_velocity", 0.5f);
    wind_target_radius = (float)get_double("wind_target_radius", 0.9f);
    wind_randomness = (float)get_double("wind_randomness", 1.0f);
    wind_inertia = (float)get_double("wind_inertia", 1.0f);
    wind_step_randomness = (float)get_double("wind_step_randomness", 0.55f);

    if (wind_speed_multiplier < 0.1f)
        wind_speed_multiplier = 0.1f;
    if (wind_min_velocity < 0.0f)
        wind_min_velocity = 0.0f;
    if (wind_target_radius < 0.1f)
        wind_target_radius = 0.1f;
    if (wind_randomness < 0.0f)
        wind_randomness = 0.0f;
    if (wind_inertia < 0.0f)
        wind_inertia = 0.0f;
    if (wind_inertia > 2.0f)
        wind_inertia = 2.0f;
    if (wind_step_randomness < 0.0f)
        wind_step_randomness = 0.0f;
    if (wind_step_randomness > 1.0f)
        wind_step_randomness = 1.0f;

    // Mouse shooting
    auto_shoot = get_bool("auto_shoot", false);
    auto_shoot_hold_until_off_target = get_bool("auto_shoot_hold_until_off_target", false);
    auto_shoot_with_auto_hip_aim = get_bool("auto_shoot_with_auto_hip_aim", false);
    bScope_multiplier = (float)get_double("bScope_multiplier", 1.1);
    auto_shoot_fire_delay_ms = (float)get_double("auto_shoot_fire_delay_ms", 139.9);
    if (auto_shoot_fire_delay_ms < 0.0f) auto_shoot_fire_delay_ms = 0.0f;
    auto_shoot_press_duration_ms = (float)get_double("auto_shoot_press_duration_ms", 98.3);
    if (auto_shoot_press_duration_ms < 0.0f) auto_shoot_press_duration_ms = 0.0f;
    auto_shoot_full_auto_grace_ms = (float)get_double("auto_shoot_full_auto_grace_ms", 180.8);
    if (auto_shoot_full_auto_grace_ms < 0.0f) auto_shoot_full_auto_grace_ms = 0.0f;

    // AI
    backend = get_string("backend", "DML");

    dml_device_id = get_long("dml_device_id", 0);

    ai_model = get_string("ai_model", "sunxds_0.7.6.onnx");
    confidence_threshold = (float)get_double("confidence_threshold", 0.50);
    hip_aim_confidence_threshold = (float)get_double("hip_aim_confidence_threshold", 0.80);
    hip_aim_min_box_area = std::clamp((float)get_double("hip_aim_min_box_area", 0.10), 0.0f, 1.0f);
    nms_threshold = (float)get_double("nms_threshold", 0.50);
    max_detections = get_long("max_detections", 100);

    postprocess = get_string("postprocess", "yolo12");

    batch_size = get_long("batch_size", 8);
    if (batch_size < 1) batch_size = 1;
    if (batch_size > 8) batch_size = 8;

    fixed_input_size = get_bool("fixed_input_size", false);

    // Buttons
    button_targeting = splitString(get_string("button_targeting", "RightMouseButton"));
    button_shoot = splitString(get_string("button_shoot", "LeftMouseButton"));
    button_zoom = splitString(get_string("button_zoom", "RightMouseButton"));
    button_exit = splitString(get_string("button_exit", "F2"));
    button_pause = splitString(get_string("button_pause", "F3"));
    button_reload_config = splitString(get_string("button_reload_config", "F4"));
    button_open_overlay = splitString(get_string("button_open_overlay", "F5"));
    enable_arrows_settings = get_bool("enable_arrows_settings", false);

    // Overlay
    overlay_opacity = get_long("overlay_opacity", 225);
    overlay_ui_scale = (float)get_double("overlay_ui_scale", 1.0);
    pause_when_overlay_open = get_bool("pause_when_overlay_open", true);

    // Presets
    active_preset = get_string("active_preset", "");

    // Custom Classes
    class_player = get_long("class_player", 0);
    class_bot = get_long("class_bot", 1);
    class_hideout_target_human = get_long("class_hideout_target_human", 5);
    class_hideout_target_balls = get_long("class_hideout_target_balls", 6);
    class_head = get_long("class_head", 7);
    class_third_person = get_long("class_third_person", 10);

    // Debug window
    show_window = get_bool("show_window", true);
    show_fps = get_bool("show_fps", false);
    screenshot_button = splitString(get_string("screenshot_button", "None"));
    screenshot_delay = get_long("screenshot_delay", 500);
    verbose = get_bool("verbose", false);

    return true;
}

bool Config::saveConfig(const std::string& filename)
{
    std::ofstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error opening config for writing: " << filename << std::endl;
        return false;
    }

    writeConfigToStream(file, *this, nullptr);
    file.close();
    return true;
}

bool Config::savePreset(const std::string& filename, const std::string& presetName) const
{
    std::ofstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error opening preset for writing: " << filename << std::endl;
        return false;
    }

    writeConfigToStream(file, *this, &presetName);
    file.close();
    return true;
}

void Config::applyAimProfile(float response, float smooth, float stick)
{
    aim_response_control = std::clamp(response, 0.0f, 1.0f);
    aim_smooth_control = std::clamp(smooth, 0.0f, 1.0f);
    aim_stickiness_control = std::clamp(stick, 0.0f, 1.0f);

    const float baseMin = 0.18f;
    const float baseMax = 0.35f;
    const float minRange = 0.52f;
    const float maxRange = 1.20f;

    minSpeedMultiplier = baseMin + minRange * aim_response_control;
    maxSpeedMultiplier = baseMax + maxRange * aim_response_control;
    if (maxSpeedMultiplier < minSpeedMultiplier + 0.05f)
        maxSpeedMultiplier = minSpeedMultiplier + 0.05f;

    predictionInterval = 0.006f + 0.028f * aim_response_control;
    nearRadius = 9.0f + (1.0f - aim_response_control) * 18.0f;
    snapBoostFactor = 1.05f + 0.5f * aim_response_control;

    speedCurveExponent = 1.2f + 3.3f * aim_smooth_control;
    snapRadius = 0.32f + (1.0f - aim_stickiness_control) * 0.88f;

    tracking_noise_floor = 0.08f + aim_stickiness_control * 1.0f;
    tracking_prediction_boost = 0.35f + aim_response_control * 0.45f;
}

void Config::deriveAimProfileFromRaw()
{
    const float baseMin = 0.18f;
    const float baseMax = 0.35f;
    const float minRange = 0.52f;
    const float maxRange = 1.20f;

    float respFromMin = (minSpeedMultiplier - baseMin) / std::max(minRange, 0.001f);
    float respFromMax = (maxSpeedMultiplier - baseMax) / std::max(maxRange, 0.001f);
    float respFromNear = 1.0f - (nearRadius - 9.0f) / 18.0f;

    float response = (respFromMin + respFromMax + respFromNear) / 3.0f;
    aim_response_control = std::clamp(response, 0.0f, 1.0f);

    float smooth = (speedCurveExponent - 1.2f) / 3.3f;
    aim_smooth_control = std::clamp(smooth, 0.0f, 1.0f);

    float stickFromSnap = 1.0f - (snapRadius - 0.32f) / 0.88f;
    float stickFromNoise = (tracking_noise_floor - 0.08f) / 1.0f;
    float stick = (stickFromSnap + stickFromNoise) * 0.5f;
    aim_stickiness_control = std::clamp(stick, 0.0f, 1.0f);

    tracking_noise_floor = 0.08f + aim_stickiness_control * 1.0f;
    tracking_prediction_boost = 0.35f + aim_response_control * 0.45f;
}

std::pair<double, double> Config::degToCounts(double degX, double degY, double fovNow) const
{
    double scale = (fovScaled && baseFOV > 1.0) ? (fovNow / baseFOV) : 1.0;
    double effectivePitch = fovScaled ? pitch : yaw;

    if (sens == 0.0 || yaw == 0.0 || effectivePitch == 0.0)
        return { 0.0, 0.0 };

    double cx = degX / (sens * yaw * scale);
    double cy = degY / (sens * effectivePitch * scale);
    return { cx, cy };
}

std::filesystem::path Config::activePresetPath() const
{
    if (active_preset.empty())
        return {};

    std::filesystem::path path(active_preset);
    if (path.is_absolute())
        return path;

    return std::filesystem::current_path() / "presets" / path;
}
