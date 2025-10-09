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

std::string Config::joinStrings(const std::vector<std::string>& vec, const std::string& delimiter)
{
    std::ostringstream oss;
    for (size_t i = 0; i < vec.size(); ++i)
    {
        if (i != 0) oss << delimiter;
        oss << vec[i];
    }
    return oss.str();
}

bool Config::loadConfig(const std::string& filename)
{
    if (!std::filesystem::exists(filename))
    {
        std::cerr << "[Config] Config file does not exist, creating default config: " << filename << std::endl;

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
        ignore_third_person = false;
        shooting_range_targets = false;
        auto_aim = false;
        auto_hip_aim = true;

        // Mouse
        fovX = 96;
        fovY = 73;
        minSpeedMultiplier = 0.19f;
        maxSpeedMultiplier = 0.20f;

        predictionInterval = 0.01f;
        prediction_futurePositions = 9;
        draw_futurePositions = true;

        snapRadius = 0.62f;
        nearRadius = 13.89f;
        speedCurveExponent = 1.81f;
        snapBoostFactor = 1.14f;

        sens = 0.54;
        yaw = 0.02;
        pitch = 0.02;
        fovScaled = false;
        baseFOV = 0.0;

        easynorecoil = false;
        easynorecoilstrength = 0.0f;

        // Wind mouse
        wind_mouse_enabled = true;
        wind_G = 10.0f;
        wind_W = 5.0f;
        wind_M = 5.0f;
        wind_D = 3.0f;

        // Mouse shooting
        auto_shoot = false;
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
#ifdef USE_CUDA
        export_enable_fp8 = false;
        export_enable_fp16 = true;
#endif
        fixed_input_size = false;

        // CUDA
#ifdef USE_CUDA
        use_cuda_graph = false;
        use_pinned_memory = false;
#endif

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

    easynorecoil = get_bool("easynorecoil", false);
    easynorecoilstrength = (float)get_double("easynorecoilstrength", 0.0);

    // Wind mouse
    wind_mouse_enabled = get_bool("wind_mouse_enabled", true);
    wind_G = (float)get_double("wind_G", 10.0f);
    wind_W = (float)get_double("wind_W", 5.0f);
    wind_M = (float)get_double("wind_M", 5.0f);
    wind_D = (float)get_double("wind_D", 3.0f);

    // Mouse shooting
    auto_shoot = get_bool("auto_shoot", false);
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

#ifdef USE_CUDA
    export_enable_fp8 = get_bool("export_enable_fp8", true);
    export_enable_fp16 = get_bool("export_enable_fp16", true);
#endif
    fixed_input_size = get_bool("fixed_input_size", false);

    // CUDA
#ifdef USE_CUDA
    use_cuda_graph = get_bool("use_cuda_graph", false);
    use_pinned_memory = get_bool("use_pinned_memory", true);
#endif

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

    file << "# An explanation of the options can be found at:\n";
    file << "# https://github.com/SunOner/sunone_aimbot_docs/blob/main/config/config_cpp.md\n\n";

    // Capture
    file << "# Capture\n"
        << "capture_method = " << capture_method << "\n"
        << "detection_resolution = " << detection_resolution << "\n"
        << "capture_fps = " << capture_fps << "\n"
        << "monitor_idx = " << monitor_idx << "\n"
        << "circle_mask = " << (circle_mask ? "true" : "false") << "\n"
        << "capture_borders = " << (capture_borders ? "true" : "false") << "\n"
        << "capture_cursor = " << (capture_cursor ? "true" : "false") << "\n"
        << "virtual_camera_name = " << virtual_camera_name << "\n"
        << "virtual_camera_width = " << virtual_camera_width << "\n"
        << "virtual_camera_heigth = " << virtual_camera_heigth << "\n\n";

    // Target
    file << "# Target\n"
        << "disable_headshot = " << (disable_headshot ? "true" : "false") << "\n"
        << std::fixed << std::setprecision(2)
        << "body_y_offset = " << body_y_offset << "\n"
        << "head_y_offset = " << head_y_offset << "\n"
        << "ignore_third_person = " << (ignore_third_person ? "true" : "false") << "\n"
        << "shooting_range_targets = " << (shooting_range_targets ? "true" : "false") << "\n"
        << "auto_aim = " << (auto_aim ? "true" : "false") << "\n"
        << "auto_hip_aim = " << (auto_hip_aim ? "true" : "false") << "\n\n";

    // Mouse
    double normalizedPitch = fovScaled ? pitch : yaw;

    file << "# Mouse move\n"
        << "fovX = " << fovX << "\n"
        << "fovY = " << fovY << "\n"
        << "minSpeedMultiplier = " << minSpeedMultiplier << "\n"
        << "maxSpeedMultiplier = " << maxSpeedMultiplier << "\n"
        << std::fixed << std::setprecision(4)
        << "sensitivity = " << sens << "\n"
        << "yaw = " << yaw << "\n"
        << "pitch = " << normalizedPitch << "\n"
        << "fov_scaled = " << (fovScaled ? "true" : "false") << "\n"
        << std::setprecision(2)
        << "base_fov = " << baseFOV << "\n"

        << "predictionInterval = " << predictionInterval << "\n"
        << "prediction_futurePositions = " << prediction_futurePositions << "\n"
        << "draw_futurePositions = " << (draw_futurePositions ? "true" : "false") << "\n"

        << "snapRadius = " << snapRadius << "\n"
        << "nearRadius = " << nearRadius << "\n"
        << "speedCurveExponent = " << speedCurveExponent << "\n"
        << std::fixed << std::setprecision(2)
        << "snapBoostFactor = " << snapBoostFactor << "\n"

        << "easynorecoil = " << (easynorecoil ? "true" : "false") << "\n"
        << std::fixed << std::setprecision(1)
        << "easynorecoilstrength = " << easynorecoilstrength << "\n\n";

    // Wind mouse
    file << "# Wind mouse\n"
        << "wind_mouse_enabled = " << (wind_mouse_enabled ? "true" : "false") << "\n"
        << "wind_G = " << wind_G << "\n"
        << "wind_W = " << wind_W << "\n"
        << "wind_M = " << wind_M << "\n"
        << "wind_D = " << wind_D << "\n\n";

    // Mouse shooting
    file << "# Mouse shooting\n"
        << "auto_shoot = " << (auto_shoot ? "true" : "false") << "\n"
        << std::fixed << std::setprecision(1)
        << "bScope_multiplier = " << bScope_multiplier << "\n"
        << "auto_shoot_fire_delay_ms = " << auto_shoot_fire_delay_ms << "\n"
        << "auto_shoot_press_duration_ms = " << auto_shoot_press_duration_ms << "\n"
        << "auto_shoot_full_auto_grace_ms = " << auto_shoot_full_auto_grace_ms << "\n\n";

    // AI
    file << "# AI\n"
        << "backend = " << backend << "\n"
        << "dml_device_id = " << dml_device_id << "\n"
        << "ai_model = " << ai_model << "\n"
        << std::fixed << std::setprecision(2)
        << "confidence_threshold = " << confidence_threshold << "\n"
        << "hip_aim_confidence_threshold = " << hip_aim_confidence_threshold << "\n"
        << "hip_aim_min_box_area = " << hip_aim_min_box_area << "\n"
        << "nms_threshold = " << nms_threshold << "\n"
        << std::setprecision(0)
        << "max_detections = " << max_detections << "\n"
        << "postprocess = " << postprocess << "\n"
        << "batch_size = " << batch_size << "\n"
#ifdef USE_CUDA
        << "export_enable_fp8 = " << (export_enable_fp8 ? "true" : "false") << "\n"
        << "export_enable_fp16 = " << (export_enable_fp16 ? "true" : "false") << "\n"
#endif
        << "fixed_input_size = " << (fixed_input_size ? "true" : "false") << "\n";
    
    // CUDA
#ifdef USE_CUDA
    file << "\n# CUDA\n"
        << "use_cuda_graph = " << (use_cuda_graph ? "true" : "false") << "\n"
        << "use_pinned_memory = " << (use_pinned_memory ? "true" : "false") << "\n\n";
#endif

    // Buttons
    file << "# Buttons\n"
        << "button_targeting = " << joinStrings(button_targeting) << "\n"
        << "button_shoot = " << joinStrings(button_shoot) << "\n"
        << "button_zoom = " << joinStrings(button_zoom) << "\n"
        << "button_exit = " << joinStrings(button_exit) << "\n"
        << "button_pause = " << joinStrings(button_pause) << "\n"
        << "button_reload_config = " << joinStrings(button_reload_config) << "\n"
        << "button_open_overlay = " << joinStrings(button_open_overlay) << "\n"
        << "enable_arrows_settings = " << (enable_arrows_settings ? "true" : "false") << "\n\n";

    // Overlay
    file << "# Overlay\n"
        << "overlay_opacity = " << overlay_opacity << "\n"
        << std::fixed << std::setprecision(2)
        << "overlay_ui_scale = " << overlay_ui_scale << "\n\n";

    // Custom Classes
    file << "# Custom Classes\n"
        << "class_player = " << class_player << "\n"
        << "class_bot = " << class_bot << "\n"
        << "class_hideout_target_human = " << class_hideout_target_human << "\n"
        << "class_hideout_target_balls = " << class_hideout_target_balls << "\n"
        << "class_head = " << class_head << "\n"
        << "class_third_person = " << class_third_person << "\n\n";

    // Debug
    file << "# Debug\n"
        << "show_window = " << (show_window ? "true" : "false") << "\n"
        << "show_fps = " << (show_fps ? "true" : "false") << "\n"
        << "screenshot_button = " << joinStrings(screenshot_button) << "\n"
        << "screenshot_delay = " << screenshot_delay << "\n"
        << "verbose = " << (verbose ? "true" : "false") << "\n\n";

    // Active game
    file << "# Active game profile\n";
    file << "active_game = " << active_game << "\n\n";
    file << "[Games]\n";
    for (auto& kv : game_profiles)
    {
        const auto& gp = kv.second;
        file << gp.name << " = true\n";
    }

    file.close();
    return true;
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