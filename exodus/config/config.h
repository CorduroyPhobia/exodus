#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

class Config
{
public:
    // Capture
    std::string capture_method; // "duplication_api", "winrt", "virtual_camera"
    int detection_resolution;
    int capture_fps;
    int monitor_idx;
    bool circle_mask;
    bool capture_borders;
    bool capture_cursor;
    std::string virtual_camera_name;
    int virtual_camera_width;
    int virtual_camera_heigth;

    // Target
    bool disable_headshot;
    float body_y_offset;
    float head_y_offset;
    float body_distance_compensation;
    float head_distance_compensation;
    bool ignore_third_person;
    bool shooting_range_targets;
    bool auto_aim;
    bool auto_hip_aim;

    // Mouse
    int fovX;
    int fovY;
    float minSpeedMultiplier;
    float maxSpeedMultiplier;

    float predictionInterval;
    int prediction_futurePositions;
    bool draw_futurePositions;

    float snapRadius;
    float nearRadius;
    float speedCurveExponent;
    float snapBoostFactor;

    double sens;
    double yaw;
    double pitch;
    bool   fovScaled;
    double baseFOV;

    bool easynorecoil;
    float easynorecoilstrength;

    // Wind mouse
    bool wind_mouse_enabled;
    float wind_G;
    float wind_W;
    float wind_M;
    float wind_D;

    // Mouse shooting
    bool auto_shoot;
    float bScope_multiplier;
    float auto_shoot_fire_delay_ms;
    float auto_shoot_press_duration_ms;
    float auto_shoot_full_auto_grace_ms;

    // AI
    std::string backend;
    int dml_device_id;
    std::string ai_model;
    float confidence_threshold;
    float hip_aim_confidence_threshold;
    float hip_aim_min_box_area;
    float nms_threshold;
    int max_detections;
    std::string postprocess;
    int batch_size;
#ifdef USE_CUDA
    bool export_enable_fp8;
    bool export_enable_fp16;
#endif
    bool fixed_input_size;

    // CUDA
#ifdef USE_CUDA
    bool use_cuda_graph;
    bool use_pinned_memory;
#endif
    // Buttons
    std::vector<std::string> button_targeting;
    std::vector<std::string> button_shoot;
    std::vector<std::string> button_zoom;
    std::vector<std::string> button_exit;
    std::vector<std::string> button_pause;
    std::vector<std::string> button_reload_config;
    std::vector<std::string> button_open_overlay;
    bool enable_arrows_settings;

    // Overlay
    int overlay_opacity;
    float overlay_ui_scale;

    // Custom Classes
    int class_player;                  // 0
    int class_bot;                     // 1
    int class_hideout_target_human;    // 5
    int class_hideout_target_balls;    // 6
    int class_head;                    // 7
    int class_third_person;            // 10

    // Debug
    bool show_window;
    bool show_fps;
    std::vector<std::string> screenshot_button;
    int screenshot_delay;
    bool verbose;

    struct GameProfile
    {
        std::string name;
    };

    std::unordered_map<std::string, GameProfile> game_profiles;
    std::string                                  active_game;

    std::pair<double, double> degToCounts(double degX, double degY, double fovNow) const;

    bool loadConfig(const std::string& filename = "config.ini");
    bool saveConfig(const std::string& filename = "config.ini");
    bool savePreset(const std::string& filename, const std::string& presetName) const;

    std::string joinStrings(const std::vector<std::string>& vec, const std::string& delimiter = ",") const;
private:
    std::vector<std::string> splitString(const std::string& str, char delimiter = ',');
};

#endif // CONFIG_H
