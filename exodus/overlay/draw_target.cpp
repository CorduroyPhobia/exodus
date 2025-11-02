#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <cmath>
#include <cstdlib>

#include "d3d11.h"
#include "imgui/imgui.h"

#include "overlay.h"
#include "draw_settings.h"
#include "exodus.h"
#include "other_tools.h"
#include "memory_images.h"
#include "../mouse/AimbotTarget.h"

ID3D11ShaderResourceView* bodyTexture = nullptr;
ImVec2 bodyImageSize;

bool prev_disable_headshot = config.disable_headshot;
float prev_body_y_offset = config.body_y_offset;
float prev_head_y_offset = config.head_y_offset;
float prev_body_distance_compensation = config.body_distance_compensation;
float prev_head_distance_compensation = config.head_distance_compensation;
bool prev_ignore_third_person = config.ignore_third_person;
bool prev_shooting_range_targets = config.shooting_range_targets;
bool prev_auto_aim = config.auto_aim;
bool prev_auto_hip_aim = config.auto_hip_aim;
bool prev_easynorecoil = config.easynorecoil;
float prev_easynorecoilstrength = config.easynorecoilstrength;
bool prev_prevent_targeting_friendly_marker = config.prevent_targeting_friendly_marker;
float prev_friendly_marker_scan_height_ratio = config.friendly_marker_scan_height_ratio;
float prev_friendly_marker_scan_width_ratio = config.friendly_marker_scan_width_ratio;
float prev_friendly_marker_scan_vertical_offset_ratio = config.friendly_marker_scan_vertical_offset_ratio;
float prev_friendly_marker_color_tolerance = config.friendly_marker_color_tolerance;

void draw_target()
{
    ImGui::Checkbox("Disable Headshot", &config.disable_headshot);

    ImGui::Separator();

    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Arrow keys: Adjust body offset");
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Shift+Arrow keys: Adjust head offset");

    ImGui::SliderFloat("Approximate Body Y Offset", &config.body_y_offset, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Approximate Head Y Offset", &config.head_y_offset, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Body distance compensation", &config.body_distance_compensation, -0.50f, 0.50f, "%.2f");
    ImGui::SliderFloat("Head distance compensation", &config.head_distance_compensation, -0.50f, 0.50f, "%.2f");
    ImGui::TextDisabled("Positive values raise the aim for distant (smaller) targets.");
    
    if (bodyTexture)
    {
        ImGui::Image((void*)bodyTexture, bodyImageSize);

        ImVec2 image_pos = ImGui::GetItemRectMin();
        ImVec2 image_size = ImGui::GetItemRectSize();

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        float normalized_body_value = (config.body_y_offset - 1.0f) / 1.0f;
        float body_line_y = image_pos.y + (1.0f + normalized_body_value) * image_size.y;
        ImVec2 body_line_start = ImVec2(image_pos.x, body_line_y);
        ImVec2 body_line_end = ImVec2(image_pos.x + image_size.x, body_line_y);
        draw_list->AddLine(body_line_start, body_line_end, IM_COL32(255, 0, 0, 255), 2.0f);
        
        float body_y_pos_at_015 = image_pos.y + (1.0f + (0.15f - 1.0f) / 1.0f) * image_size.y;
        float head_top_pos = image_pos.y;
        float head_line_y = head_top_pos + (config.head_y_offset * (body_y_pos_at_015 - head_top_pos));
        
        ImVec2 head_line_start = ImVec2(image_pos.x, head_line_y);
        ImVec2 head_line_end = ImVec2(image_pos.x + image_size.x, head_line_y);
        draw_list->AddLine(head_line_start, head_line_end, IM_COL32(0, 255, 0, 255), 2.0f);
        
        draw_list->AddText(ImVec2(body_line_end.x + 5, body_line_y - 7), IM_COL32(255, 0, 0, 255), "Body");
        draw_list->AddText(ImVec2(head_line_end.x + 5, head_line_y - 7), IM_COL32(0, 255, 0, 255), "Head");
    }
    else
    {
        ImGui::Text("Image not found!");
    }
    ImGui::Text("Note: There is a different value for each game, as the sizes of the player models may vary.");
    ImGui::Separator();
    ImGui::Checkbox("Ignore Third Person", &config.ignore_third_person);
    ImGui::Checkbox("Shooting range targets", &config.shooting_range_targets);
    ImGui::Checkbox("Auto Aim", &config.auto_aim);
    ImGui::SameLine();
    ImGui::Checkbox("Auto Hip-Aim", &config.auto_hip_aim);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.55f, 0.86f, 1.0f, 1.0f), "Friendly Marker Safety");
    ImGui::Checkbox("Ignore targets with teammate marker", &config.prevent_targeting_friendly_marker);
    ImGui::SliderFloat("Marker scan height (x target height)", &config.friendly_marker_scan_height_ratio, 0.05f, 0.60f, "%.2f");
    ImGui::SliderFloat("Marker scan width (x target width)", &config.friendly_marker_scan_width_ratio, 0.20f, 1.00f, "%.2f");
    ImGui::SliderFloat("Marker scan vertical offset", &config.friendly_marker_scan_vertical_offset_ratio, -0.50f, 1.50f, "%.2f x height");
    ImGui::SliderFloat("Marker color tolerance", &config.friendly_marker_color_tolerance, 5.0f, 200.0f, "%.0f");
    ImGui::TextDisabled("Increase tolerance or scan size if teammate markers are missed.");

    const float referenceTargetHeightPx = 200.0f;
    const float estimatedScanHeightPx = referenceTargetHeightPx * config.friendly_marker_scan_height_ratio;
    const float estimatedOffsetPx = referenceTargetHeightPx * config.friendly_marker_scan_vertical_offset_ratio;
    const char* estimatedOffsetLabel = estimatedOffsetPx >= 0.0f ? "above" : "inside";
    ImGui::TextDisabled("~%.0fpx tall target -> scan ~%.0fpx tall, bottom %.0fpx %s the target", referenceTargetHeightPx, estimatedScanHeightPx, std::fabs(estimatedOffsetPx), estimatedOffsetLabel);

    int debugHeightPx = 0;
    int debugWidthPx = 0;
    int debugBottomOffsetPx = 0;
    int debugStartXPx = 0;
    int debugTopYPx = 0;
    bool debugMarkerDetected = false;
    getFriendlyMarkerDebugInfo(debugHeightPx, debugWidthPx, debugBottomOffsetPx, debugStartXPx, debugTopYPx, debugMarkerDetected);
    if (debugHeightPx > 0 && debugWidthPx > 0)
    {
        const char* debugOffsetLabel = debugBottomOffsetPx >= 0 ? "above" : "inside";
        ImGui::TextDisabled("Last scan window: %d x %d px, bottom offset %d px %s the target", debugWidthPx, debugHeightPx, std::abs(debugBottomOffsetPx), debugOffsetLabel);
        ImGui::TextDisabled("Last scan origin: (%d, %d) — marker %s detected", debugStartXPx, debugTopYPx, debugMarkerDetected ? "was" : "was not");
    }

    if (prev_disable_headshot != config.disable_headshot ||
        prev_body_y_offset != config.body_y_offset ||
        prev_head_y_offset != config.head_y_offset ||
        prev_body_distance_compensation != config.body_distance_compensation ||
        prev_head_distance_compensation != config.head_distance_compensation ||
        prev_ignore_third_person != config.ignore_third_person ||
        prev_shooting_range_targets != config.shooting_range_targets ||
        prev_auto_aim != config.auto_aim ||
        prev_auto_hip_aim != config.auto_hip_aim ||
        prev_easynorecoil != config.easynorecoil ||
        prev_easynorecoilstrength != config.easynorecoilstrength ||
        prev_prevent_targeting_friendly_marker != config.prevent_targeting_friendly_marker ||
        prev_friendly_marker_scan_height_ratio != config.friendly_marker_scan_height_ratio ||
        prev_friendly_marker_scan_width_ratio != config.friendly_marker_scan_width_ratio ||
        prev_friendly_marker_scan_vertical_offset_ratio != config.friendly_marker_scan_vertical_offset_ratio ||
        prev_friendly_marker_color_tolerance != config.friendly_marker_color_tolerance)
    {
        prev_disable_headshot = config.disable_headshot;
        prev_body_y_offset = config.body_y_offset;
        prev_head_y_offset = config.head_y_offset;
        prev_body_distance_compensation = config.body_distance_compensation;
        prev_head_distance_compensation = config.head_distance_compensation;
        prev_ignore_third_person = config.ignore_third_person;
        prev_shooting_range_targets = config.shooting_range_targets;
        prev_auto_aim = config.auto_aim;
        prev_auto_hip_aim = config.auto_hip_aim;
        prev_easynorecoil = config.easynorecoil;
        prev_easynorecoilstrength = config.easynorecoilstrength;
        prev_prevent_targeting_friendly_marker = config.prevent_targeting_friendly_marker;
        prev_friendly_marker_scan_height_ratio = config.friendly_marker_scan_height_ratio;
        prev_friendly_marker_scan_width_ratio = config.friendly_marker_scan_width_ratio;
        prev_friendly_marker_scan_vertical_offset_ratio = config.friendly_marker_scan_vertical_offset_ratio;
        prev_friendly_marker_color_tolerance = config.friendly_marker_color_tolerance;
        config.saveConfig();
    }
}

void load_body_texture()
{
    int image_width = 0;
    int image_height = 0;

    std::string body_image = std::string(bodyImageBase64_1) + std::string(bodyImageBase64_2) + std::string(bodyImageBase64_3);

    bool ret = LoadTextureFromMemory(body_image, g_pd3dDevice, &bodyTexture, &image_width, &image_height);
    if (!ret)
    {
        std::cerr << "[Overlay] Can't load image!" << std::endl;
    }
    else
    {
        bodyImageSize = ImVec2((float)image_width, (float)image_height);
    }
}

void release_body_texture()
{
    if (bodyTexture)
    {
        bodyTexture->Release();
        bodyTexture = nullptr;
    }
}