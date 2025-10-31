#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <opencv2/opencv.hpp>

#include "exodus.h"
#include "AimbotTarget.h"
#include "config.h"
#include "capture.h"

namespace
{
    bool hasFriendlyMarkerAbove(const cv::Rect& box)
    {
        if (!config.prevent_targeting_friendly_marker)
            return false;

        cv::Mat region;
        {
            std::lock_guard<std::mutex> lock(frameMutex);

            if (latestFrame.empty())
                return false;

            const cv::Mat& frame = latestFrame;

            if (frame.cols <= 0 || frame.rows <= 0)
                return false;

            const float heightRatio = std::clamp(config.friendly_marker_scan_height_ratio, 0.05f, 0.60f);
            const float widthRatio = std::clamp(config.friendly_marker_scan_width_ratio, 0.20f, 1.0f);

            int sampleHeight = std::max(2, static_cast<int>(std::round(box.height * heightRatio)));
            int sampleWidth = std::max(4, static_cast<int>(std::round(box.width * widthRatio)));

            const int maxY = std::max(0, box.y - 1);
            int minY = maxY - sampleHeight + 1;
            if (minY < 0)
            {
                sampleHeight = maxY + 1;
                minY = 0;
            }

            if (sampleHeight <= 0)
                return false;

            if (minY >= frame.rows)
                return false;

            if (sampleHeight > frame.rows)
                sampleHeight = frame.rows;
            if (minY + sampleHeight > frame.rows)
                sampleHeight = frame.rows - minY;

            if (sampleHeight <= 0)
                return false;

            if (frame.cols <= 0)
                return false;

            if (sampleWidth > frame.cols)
                sampleWidth = frame.cols;

            int startX = box.x + (box.width - sampleWidth) / 2;
            startX = std::clamp(startX, 0, std::max(0, frame.cols - sampleWidth));

            if (startX >= frame.cols)
                return false;

            if (sampleWidth <= 0)
                return false;

            if (startX + sampleWidth > frame.cols)
                sampleWidth = frame.cols - startX;

            if (sampleWidth <= 0 || sampleHeight <= 0)
                return false;

            cv::Rect roi(startX, minY, sampleWidth, sampleHeight);
            region = frame(roi).clone();
        }

        if (region.empty())
            return false;

        const std::array<cv::Vec3b, 2> teammateColors = {
            cv::Vec3b(0xD2, 0x9D, 0x0F), // BGR for #0F9DD2 (legacy)
            cv::Vec3b(0xCF, 0x97, 0x00)  // BGR for #0097CF (new)
        };

        const float configuredTolerance = std::clamp(config.friendly_marker_color_tolerance, 5.0f, 200.0f);
        const int tolerance = static_cast<int>(std::round(configuredTolerance));
        const int toleranceSq = tolerance * tolerance;

        auto colorDistanceSq = [](const cv::Vec3b& lhs, const cv::Vec3b& rhs)
        {
            const int db = static_cast<int>(lhs[0]) - static_cast<int>(rhs[0]);
            const int dg = static_cast<int>(lhs[1]) - static_cast<int>(rhs[1]);
            const int dr = static_cast<int>(lhs[2]) - static_cast<int>(rhs[2]);
            return db * db + dg * dg + dr * dr;
        };

        for (int y = 0; y < region.rows; ++y)
        {
            const cv::Vec3b* row = region.ptr<cv::Vec3b>(y);
            for (int x = 0; x < region.cols; ++x)
            {
                const cv::Vec3b& pixel = row[x];
                for (const auto& teammateColor : teammateColors)
                {
                    if (colorDistanceSq(pixel, teammateColor) <= toleranceSq)
                    {
                        return true;
                    }
                }
            }
        }

        return false;
    }
}

AimbotTarget::AimbotTarget(int x_, int y_, int w_, int h_, int cls, double px, double py)
    : x(x_), y(y_), w(w_), h(h_), classId(cls), pivotX(px), pivotY(py)
{
}

AimbotTarget* sortTargets(
    const std::vector<cv::Rect>& boxes,
    const std::vector<int>& classes,
    int screenWidth,
    int screenHeight,
    bool disableHeadshot)
{
    if (boxes.empty() || classes.empty())
    {
        return nullptr;
    }

    cv::Point center(screenWidth / 2, screenHeight / 2);

    double minDistance = std::numeric_limits<double>::max();
    int nearestIdx = -1;
    int targetY = 0;
    auto computeOffsetFactor = [&](const cv::Rect& box, double baseOffset, double distanceComp)
    {
        if (screenHeight <= 0)
            return std::clamp(baseOffset, 0.0, 2.0);
        double normalizedHeight = static_cast<double>(box.height) / static_cast<double>(screenHeight);
        normalizedHeight = std::clamp(normalizedHeight, 0.0, 1.0);
        double adjusted = baseOffset + (1.0 - normalizedHeight) * distanceComp;
        return std::clamp(adjusted, 0.0, 2.0);
    };

    const bool headshotsAllowed = !disableHeadshot && !hipAiming.load(std::memory_order_relaxed);

    if (headshotsAllowed)
    {
        for (size_t i = 0; i < boxes.size(); i++)
        {
            if (classes[i] == config.class_head)
            {
                if (hasFriendlyMarkerAbove(boxes[i]))
                    continue;

                double offsetFactor = computeOffsetFactor(boxes[i], config.head_y_offset, config.head_distance_compensation);
                int headOffsetY = static_cast<int>(boxes[i].height * offsetFactor);
                cv::Point targetPoint(boxes[i].x + boxes[i].width / 2, boxes[i].y + headOffsetY);
                double distance = std::pow(targetPoint.x - center.x, 2) + std::pow(targetPoint.y - center.y, 2);
                if (distance < minDistance)
                {
                    minDistance = distance;
                    nearestIdx = static_cast<int>(i);
                    targetY = targetPoint.y;
                }
            }
        }
    }

    if (!headshotsAllowed || nearestIdx == -1)
    {
        minDistance = std::numeric_limits<double>::max();
        for (size_t i = 0; i < boxes.size(); i++)
        {
            if (!headshotsAllowed && classes[i] == config.class_head)
                continue;

            if (classes[i] == config.class_player ||
                classes[i] == config.class_bot ||
                (classes[i] == config.class_hideout_target_human && config.shooting_range_targets) ||
                (classes[i] == config.class_hideout_target_balls && config.shooting_range_targets) ||
                (classes[i] == config.class_third_person && !config.ignore_third_person))
            {
                if (hasFriendlyMarkerAbove(boxes[i]))
                    continue;

                double offsetFactor = computeOffsetFactor(boxes[i], config.body_y_offset, config.body_distance_compensation);
                int offsetY = static_cast<int>(boxes[i].height * offsetFactor);
                cv::Point targetPoint(boxes[i].x + boxes[i].width / 2, boxes[i].y + offsetY);
                double distance = std::pow(targetPoint.x - center.x, 2) + std::pow(targetPoint.y - center.y, 2);
                if (distance < minDistance)
                {
                    minDistance = distance;
                    nearestIdx = static_cast<int>(i);
                    targetY = targetPoint.y;
                }
            }
        }
    }

    if (nearestIdx == -1)
    {
        return nullptr;
    }

    int finalY = 0;
    if (classes[nearestIdx] == config.class_head && headshotsAllowed)
    {
        double offsetFactor = computeOffsetFactor(boxes[nearestIdx], config.head_y_offset, config.head_distance_compensation);
        int headOffsetY = static_cast<int>(boxes[nearestIdx].height * offsetFactor);
        finalY = boxes[nearestIdx].y + headOffsetY - boxes[nearestIdx].height / 2;
    }
    else
    {
        finalY = targetY - boxes[nearestIdx].height / 2;
    }

    int finalX = boxes[nearestIdx].x;
    int finalW = boxes[nearestIdx].width;
    int finalH = boxes[nearestIdx].height;
    int finalClass = classes[nearestIdx];

    double pivotX = finalX + (finalW / 2.0);
    double pivotY = finalY + (finalH / 2.0);

    return new AimbotTarget(finalX, finalY, finalW, finalH, finalClass, pivotX, pivotY);
}