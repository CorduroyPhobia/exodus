#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "imgui/imgui.h"

#include "draw_settings.h"
#include "overlay.h"
#include "exodus.h"

struct PresetEntry
{
    std::string displayName;
    std::filesystem::path path;
};

static std::vector<PresetEntry> gPresetEntries;
static int gSelectedPreset = -1;
static char gPresetNameBuffer[128] = "";
static std::string gStatusMessage;
static bool gStatusIsError = false;
static bool gPresetsInitialized = false;

static std::string trim(const std::string& value)
{
    const char* whitespace = " \t\r\n";
    size_t start = value.find_first_not_of(whitespace);
    if (start == std::string::npos)
        return "";
    size_t end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

static std::filesystem::path presetsDirectory()
{
    std::filesystem::path dir = std::filesystem::current_path() / "presets";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

static std::string safeFilenameFromName(const std::string& name)
{
    std::string result = name;
    const std::string invalid = "<>:\"/\\|?*";
    for (char& ch : result)
    {
        if (invalid.find(ch) != std::string::npos)
            ch = '_';
    }
    while (!result.empty() && (result.back() == ' ' || result.back() == '.'))
        result.pop_back();
    if (result.empty())
        result = "preset";
    return result;
}

static std::string readPresetDisplayName(const std::filesystem::path& path)
{
    std::string display = path.stem().string();
    std::ifstream file(path);
    if (!file.is_open())
        return display;

    std::string line;
    while (std::getline(file, line))
    {
        line = trim(line);
        if (line.empty())
            continue;
        if (line.front() != '#')
            break;

        const std::string prefix = "# Preset name =";
        if (line.rfind(prefix, 0) == 0)
        {
            std::string candidate = trim(line.substr(prefix.size()));
            if (!candidate.empty())
                display = candidate;
            break;
        }
    }
    return display;
}

static void refreshPresetEntries()
{
    std::filesystem::path previouslySelected;
    if (gSelectedPreset >= 0 && gSelectedPreset < static_cast<int>(gPresetEntries.size()))
    {
        previouslySelected = gPresetEntries[gSelectedPreset].path;
    }

    gPresetEntries.clear();

    std::filesystem::path dir = presetsDirectory();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec))
    {
        gSelectedPreset = -1;
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file())
            continue;
        auto path = entry.path();
        if (path.extension() != ".ini")
            continue;

        PresetEntry preset;
        preset.path = path;
        preset.displayName = readPresetDisplayName(path);
        gPresetEntries.push_back(std::move(preset));
    }

    std::sort(gPresetEntries.begin(), gPresetEntries.end(), [](const PresetEntry& a, const PresetEntry& b)
    {
        std::string lowerA = a.displayName;
        std::string lowerB = b.displayName;
        std::transform(lowerA.begin(), lowerA.end(), lowerA.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(lowerB.begin(), lowerB.end(), lowerB.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lowerA == lowerB)
            return a.displayName < b.displayName;
        return lowerA < lowerB;
    });

    gSelectedPreset = -1;
    if (!previouslySelected.empty())
    {
        for (size_t i = 0; i < gPresetEntries.size(); ++i)
        {
            if (std::filesystem::equivalent(gPresetEntries[i].path, previouslySelected, ec))
            {
                gSelectedPreset = static_cast<int>(i);
                break;
            }
        }
    }

    if (gSelectedPreset == -1 && !config.active_preset.empty())
    {
        std::filesystem::path activePath = config.activePresetPath();
        for (size_t i = 0; i < gPresetEntries.size(); ++i)
        {
            if (std::filesystem::equivalent(gPresetEntries[i].path, activePath, ec) ||
                gPresetEntries[i].path.filename().string() == config.active_preset)
            {
                gSelectedPreset = static_cast<int>(i);
                break;
            }
        }
    }
}

static std::filesystem::path presetPathForName(const std::string& name)
{
    std::filesystem::path dir = presetsDirectory();
    std::string base = safeFilenameFromName(name);
    std::filesystem::path path = dir / (base + ".ini");

    if (!std::filesystem::exists(path))
        return path;

    std::string trimmedName = trim(name);
    std::string existing = trim(readPresetDisplayName(path));
    if (existing == trimmedName)
        return path;

    for (int i = 1; ; ++i)
    {
        std::filesystem::path candidate = dir / (base + "_" + std::to_string(i) + ".ini");
        if (!std::filesystem::exists(candidate))
            return candidate;
        existing = trim(readPresetDisplayName(candidate));
        if (existing == trimmedName)
            return candidate;
    }
}

static void applyPresetSideEffects(const Config& previous)
{
    bool mouseConfigChanged =
        previous.detection_resolution != config.detection_resolution ||
        previous.fovX != config.fovX ||
        previous.fovY != config.fovY ||
        previous.minSpeedMultiplier != config.minSpeedMultiplier ||
        previous.maxSpeedMultiplier != config.maxSpeedMultiplier ||
        previous.predictionInterval != config.predictionInterval ||
        previous.snapRadius != config.snapRadius ||
        previous.nearRadius != config.nearRadius ||
        previous.speedCurveExponent != config.speedCurveExponent ||
        previous.snapBoostFactor != config.snapBoostFactor ||
        previous.auto_shoot != config.auto_shoot ||
        previous.bScope_multiplier != config.bScope_multiplier ||
        previous.auto_shoot_fire_delay_ms != config.auto_shoot_fire_delay_ms ||
        previous.auto_shoot_press_duration_ms != config.auto_shoot_press_duration_ms ||
        previous.auto_shoot_full_auto_grace_ms != config.auto_shoot_full_auto_grace_ms ||
        previous.wind_mouse_enabled != config.wind_mouse_enabled ||
        previous.wind_G != config.wind_G ||
        previous.wind_W != config.wind_W ||
        previous.wind_M != config.wind_M ||
        previous.wind_D != config.wind_D ||
        previous.wind_speed_multiplier != config.wind_speed_multiplier ||
        previous.wind_min_velocity != config.wind_min_velocity ||
        previous.wind_target_radius != config.wind_target_radius;

    if (mouseConfigChanged && globalMouseThread)
    {
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
            config.auto_shoot_full_auto_grace_ms);
    }

    if (previous.detection_resolution != config.detection_resolution)
    {
        detection_resolution_changed.store(true);
        detector_model_changed.store(true);
    }

    if (previous.capture_method != config.capture_method ||
        previous.circle_mask != config.circle_mask ||
        previous.monitor_idx != config.monitor_idx ||
        previous.virtual_camera_name != config.virtual_camera_name ||
        previous.virtual_camera_width != config.virtual_camera_width ||
        previous.virtual_camera_heigth != config.virtual_camera_heigth)
    {
        capture_method_changed.store(true);
    }

    if (previous.capture_cursor != config.capture_cursor)
        capture_cursor_changed.store(true);
    if (previous.capture_borders != config.capture_borders)
        capture_borders_changed.store(true);
    if (previous.capture_fps != config.capture_fps)
        capture_fps_changed.store(true);

    if (previous.fixed_input_size != config.fixed_input_size)
    {
        capture_method_changed.store(true);
        detector_model_changed.store(true);
    }

    if (previous.backend != config.backend ||
        previous.ai_model != config.ai_model ||
        previous.postprocess != config.postprocess ||
        previous.batch_size != config.batch_size ||
        previous.dml_device_id != config.dml_device_id ||
        previous.confidence_threshold != config.confidence_threshold ||
        previous.hip_aim_confidence_threshold != config.hip_aim_confidence_threshold ||
        previous.hip_aim_min_box_area != config.hip_aim_min_box_area ||
        previous.nms_threshold != config.nms_threshold ||
        previous.max_detections != config.max_detections)
    {
        detector_model_changed.store(true);
    }

    if (previous.overlay_opacity != config.overlay_opacity)
    {
        BYTE opacity = static_cast<BYTE>(config.overlay_opacity);
        SetLayeredWindowAttributes(g_hwnd, 0, opacity, LWA_ALPHA);
    }

    if (previous.show_window != config.show_window)
    {
        show_window_changed.store(true);
    }
}

void draw_presets()
{
    if (!gPresetsInitialized)
    {
        refreshPresetEntries();
        gPresetsInitialized = true;
    }

    ImGui::TextWrapped("Presets capture every option from the other tabs so you can switch setups instantly.");
    ImGui::Separator();

    ImGui::InputText("Preset name", gPresetNameBuffer, IM_ARRAYSIZE(gPresetNameBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Save Preset"))
    {
        std::string name = trim(gPresetNameBuffer);
        if (name.empty())
        {
            gStatusMessage = "Please enter a preset name.";
            gStatusIsError = true;
        }
        else
        {
            std::filesystem::path targetPath = presetPathForName(name);
            bool existed = std::filesystem::exists(targetPath);
            if (config.savePreset(targetPath.string(), name))
            {
                gStatusMessage = (existed ? "Updated preset '" : "Saved preset '") + name + "'.";
                gStatusIsError = false;
                refreshPresetEntries();
                for (size_t i = 0; i < gPresetEntries.size(); ++i)
                {
                    if (gPresetEntries[i].path == targetPath)
                    {
                        gSelectedPreset = static_cast<int>(i);
                        break;
                    }
                }
            }
            else
            {
                gStatusMessage = "Failed to save preset.";
                gStatusIsError = true;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
    {
        refreshPresetEntries();
    }

    if (!gStatusMessage.empty())
    {
        ImVec4 color = gStatusIsError ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(0.4f, 0.85f, 0.4f, 1.0f);
        ImGui::TextColored(color, "%s", gStatusMessage.c_str());
    }

    ImGui::Separator();

    if (ImGui::BeginListBox("Saved presets", ImVec2(-FLT_MIN, 8.0f * ImGui::GetTextLineHeightWithSpacing())))
    {
        for (int i = 0; i < static_cast<int>(gPresetEntries.size()); ++i)
        {
            bool selected = (gSelectedPreset == i);
            if (ImGui::Selectable(gPresetEntries[i].displayName.c_str(), selected))
            {
                gSelectedPreset = i;
            }
        }
        ImGui::EndListBox();
    }
    else if (gPresetEntries.empty())
    {
        ImGui::TextDisabled("No presets saved yet.");
    }

    if (gSelectedPreset >= 0 && gSelectedPreset < static_cast<int>(gPresetEntries.size()))
    {
        const PresetEntry& entry = gPresetEntries[gSelectedPreset];
        if (ImGui::Button("Load Preset"))
        {
            Config previous = config;
            if (config.loadConfig(entry.path.string()))
            {
                config.active_preset = entry.path.filename().string();
                applyPresetSideEffects(previous);
                config.saveConfig();
                gStatusMessage = "Loaded preset '" + entry.displayName + "'.";
                gStatusIsError = false;
            }
            else
            {
                config = previous;
                gStatusMessage = "Failed to load preset.";
                gStatusIsError = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Preset"))
        {
            bool wasActive = false;
            if (!config.active_preset.empty())
            {
                std::error_code activeEc;
                std::filesystem::path activePath = config.activePresetPath();
                if ((!activePath.empty() && std::filesystem::equivalent(activePath, entry.path, activeEc)) ||
                    entry.path.filename().string() == config.active_preset)
                {
                    wasActive = true;
                }
            }
            std::error_code ec;
            if (std::filesystem::remove(entry.path, ec))
            {
                gStatusMessage = "Deleted preset '" + entry.displayName + "'.";
                gStatusIsError = false;
                if (wasActive)
                {
                    config.active_preset.clear();
                    config.saveConfig();
                }
            }
            else
            {
                gStatusMessage = "Failed to delete preset.";
                gStatusIsError = true;
            }
            refreshPresetEntries();
        }
    }
    else if (gPresetEntries.empty())
    {
        ImGui::TextDisabled("Use 'Save Preset' to create your first preset.");
    }
}
