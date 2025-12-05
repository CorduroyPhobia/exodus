#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <cmath>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <atomic>
#include <vector>
#include <cctype>
#include <string>

#include "mouse.h"
#include "capture.h"
#include "exodus.h"

MouseThread::MouseThread(
    int resolution,
    int fovX,
    int fovY,
    double minSpeedMultiplier,
    double maxSpeedMultiplier,
    double predictionInterval,
    bool auto_shoot,
    bool auto_shoot_hold_until_off_target,
    float bScope_multiplier,
    double auto_shoot_fire_delay_ms,
    double auto_shoot_press_duration_ms,
    double auto_shoot_full_auto_grace_ms,
    const std::string& mouse_move_method)
    : screen_width(resolution),
    screen_height(resolution),
    prediction_interval(predictionInterval),
    fov_x(fovX),
    fov_y(fovY),
    max_distance(std::hypot(resolution, resolution) / 2.0),
    min_speed_multiplier(minSpeedMultiplier),
    max_speed_multiplier(maxSpeedMultiplier),
    center_x(resolution / 2.0),
    center_y(resolution / 2.0),
    auto_shoot(auto_shoot),
    auto_shoot_hold_until_off_target(auto_shoot_hold_until_off_target),
    bScope_multiplier(bScope_multiplier),
    auto_shoot_fire_delay_ms(std::max(0.0, auto_shoot_fire_delay_ms)),
    auto_shoot_press_duration_ms(std::max(0.0, auto_shoot_press_duration_ms)),
    auto_shoot_full_auto_grace_ms(std::max(0.0, auto_shoot_full_auto_grace_ms)),

    prev_velocity_x(0.0),
    prev_velocity_y(0.0),
    prev_x(0.0),
    prev_y(0.0)
{
    prev_time = std::chrono::steady_clock::time_point();
    auto now = std::chrono::steady_clock::now();
    last_target_time = now;
    last_press_time = now;
    last_release_time = now;
    last_in_scope_time = now;

    wind_mouse_enabled = config.wind_mouse_enabled;
    wind_G = config.wind_G;
    wind_W = config.wind_W;
    wind_M = config.wind_M;
    wind_D = config.wind_D;
    wind_speed_multiplier = config.wind_speed_multiplier;
    wind_min_velocity = config.wind_min_velocity;
    wind_target_radius = config.wind_target_radius;
    wind_randomness = config.wind_randomness;
    wind_inertia = config.wind_inertia;
    wind_step_randomness = config.wind_step_randomness;
    wind_max_step_config = config.wind_M;

    target_switching_enabled = config.target_switching_enabled;
    target_switch_reaction_ms = config.target_switch_reaction_ms;
    target_switch_slowdown_ms = config.target_switch_slowdown_ms;
    target_switch_speed_factor = std::clamp(static_cast<double>(config.target_switch_speed_factor), 0.0, 1.0);
    target_switch_overshoot_px = std::max(0.0, static_cast<double>(config.target_switch_overshoot_px));
    target_switch_detection_px = std::max(1.0, static_cast<double>(config.target_switch_detection_px));

    setMovementMethod(mouse_move_method);

    moveWorker = std::thread(&MouseThread::moveWorkerLoop, this);
}

void MouseThread::updateConfig(
    int resolution,
    int fovX,
    int fovY,
    double minSpeedMultiplier,
    double maxSpeedMultiplier,
    double predictionInterval,
    bool auto_shoot,
    bool auto_shoot_hold_until_off_target,
    float bScope_multiplier,
    double auto_shoot_fire_delay_ms,
    double auto_shoot_press_duration_ms,
    double auto_shoot_full_auto_grace_ms,
    const std::string& mouse_move_method
)
{
    screen_width = screen_height = resolution;
    fov_x = fovX;  fov_y = fovY;
    min_speed_multiplier = minSpeedMultiplier;
    max_speed_multiplier = maxSpeedMultiplier;
    prediction_interval = predictionInterval;
    this->auto_shoot = auto_shoot;
    this->auto_shoot_hold_until_off_target = auto_shoot_hold_until_off_target;
    this->bScope_multiplier = bScope_multiplier;
    this->auto_shoot_fire_delay_ms = std::max(0.0, auto_shoot_fire_delay_ms);
    this->auto_shoot_press_duration_ms = std::max(0.0, auto_shoot_press_duration_ms);
    this->auto_shoot_full_auto_grace_ms = std::max(0.0, auto_shoot_full_auto_grace_ms);

    center_x = center_y = resolution / 2.0;
    max_distance = std::hypot(resolution, resolution) / 2.0;

    wind_mouse_enabled = config.wind_mouse_enabled;
    wind_G = config.wind_G; wind_W = config.wind_W;
    wind_M = config.wind_M; wind_D = config.wind_D;
    wind_speed_multiplier = config.wind_speed_multiplier;
    wind_min_velocity = config.wind_min_velocity;
    wind_target_radius = config.wind_target_radius;
    wind_randomness = config.wind_randomness;
    wind_inertia = config.wind_inertia;
    wind_step_randomness = config.wind_step_randomness;
    wind_max_step_config = config.wind_M;

    target_switching_enabled = config.target_switching_enabled;
    target_switch_reaction_ms = config.target_switch_reaction_ms;
    target_switch_slowdown_ms = config.target_switch_slowdown_ms;
    target_switch_speed_factor = std::clamp(static_cast<double>(config.target_switch_speed_factor), 0.0, 1.0);
    target_switch_overshoot_px = std::max(0.0, static_cast<double>(config.target_switch_overshoot_px));
    target_switch_detection_px = std::max(1.0, static_cast<double>(config.target_switch_detection_px));

    setMovementMethod(mouse_move_method);

    if (!target_switching_enabled)
    {
        target_switch_active = false;
        target_switch_delaying = false;
        pending_target_switch = false;
        switch_overshoot_dir_x = 0.0;
        switch_overshoot_dir_y = 0.0;
    }

    residual_move_x = 0.0;
    residual_move_y = 0.0;
}

MouseThread::~MouseThread()
{
    workerStop = true;
    queueCv.notify_all();
    if (moveWorker.joinable()) moveWorker.join();
}

void MouseThread::queueMove(int dx, int dy)
{
    if (dx == 0 && dy == 0)
    {
        return;
    }

    std::lock_guard lg(queueMtx);
    if (moveQueue.size() >= queueLimit) moveQueue.pop();
    moveQueue.push({ dx,dy });
    queueCv.notify_one();
}

void MouseThread::setMovementMethod(const std::string& methodName)
{
    std::lock_guard<std::mutex> guard(input_method_mutex);
    std::string lower = methodName;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
        });

    if (lower == "sendinput_no_coalesce" || lower == "send_input_no_coalesce")
    {
        movement_backend = MovementBackend::SendInputNoCoalesce;
    }
    else if (lower == "mouse_event" || lower == "mouseevent")
    {
        movement_backend = MovementBackend::MouseEvent;
    }
    else
    {
        movement_backend = MovementBackend::SendInput;
    }
}

bool MouseThread::sendInputMovement(int dx, int dy, bool noCoalesce)
{
    INPUT in{ 0 };
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_VIRTUALDESK;
    if (noCoalesce)
    {
        in.mi.dwFlags |= MOUSEEVENTF_MOVE_NOCOALESCE;
    }

    return SendInput(1, &in, sizeof(INPUT)) == 1;
}

void MouseThread::moveWorkerLoop()
{
    while (!workerStop)
    {
        std::unique_lock ul(queueMtx);
        queueCv.wait(ul, [&] { return workerStop || !moveQueue.empty(); });

        while (!moveQueue.empty())
        {
            Move m = moveQueue.front();
            moveQueue.pop();
            ul.unlock();
            sendMovementToDriver(m.dx, m.dy);
            ul.lock();
        }
    }
}

void MouseThread::windMouseMoveRelative(int dx, int dy)
{
    if (dx == 0 && dy == 0) return;

    constexpr double SQRT3 = 1.7320508075688772;
    constexpr double SQRT5 = 2.23606797749979;

    double sx = 0, sy = 0;
    double dxF = static_cast<double>(dx);
    double dyF = static_cast<double>(dy);
    double vx = 0, vy = 0, wX = 0, wY = 0;
    int    cx = 0, cy = 0;

    double tolerance = std::max(0.1, static_cast<double>(wind_target_radius));
    double speedScale = std::max(0.1, static_cast<double>(wind_speed_multiplier));
    double minSpeed = std::max(0.0, static_cast<double>(wind_min_velocity));
    double minSpeedClamp = wind_max_step_config > 0.0
        ? std::min(minSpeed, static_cast<double>(wind_max_step_config))
        : minSpeed;
    double randomnessScale = std::max(0.0, static_cast<double>(wind_randomness));
    double inertiaFactor = std::clamp(static_cast<double>(wind_inertia), 0.0, 2.0);
    double clipRandomness = std::clamp(static_cast<double>(wind_step_randomness), 0.0, 1.0);

    const int maxIterations = 4096;
    int iterations = 0;

    while (true)
    {
        double remaining = std::hypot(dxF - sx, dyF - sy);
        if (remaining < tolerance || iterations++ >= maxIterations)
            break;

        double dist = remaining;
        double wMag = std::min(wind_W, dist);

        if (dist >= wind_D)
        {
            double randX = ((double)rand() / RAND_MAX * 2.0 - 1.0);
            double randY = ((double)rand() / RAND_MAX * 2.0 - 1.0);
            wX = wX / SQRT3 + randX * wMag / SQRT5 * randomnessScale;
            wY = wY / SQRT3 + randY * wMag / SQRT5 * randomnessScale;
        }
        else
        {
            wX /= SQRT3;  wY /= SQRT3;
            wind_M = wind_M < 3.0 ? ((double)rand() / RAND_MAX) * 3.0 + 3.0 : wind_M / SQRT5;
        }

        double divisor = dist > 1e-6 ? dist : 1.0;
        vx *= inertiaFactor;
        vy *= inertiaFactor;
        vx += wX + wind_G * (dxF - sx) / divisor;
        vy += wY + wind_G * (dyF - sy) / divisor;

        vx *= speedScale;
        vy *= speedScale;

        double vMag = std::hypot(vx, vy);
        if (vMag > wind_M && wind_M > 0.0)
        {
            double minFactor = 1.0 - clipRandomness;
            minFactor = std::clamp(minFactor, 0.0, 1.0);
            double vClip = wind_M * minFactor;
            if (clipRandomness > 0.0)
            {
                vClip = wind_M * (minFactor + clipRandomness * ((double)rand() / RAND_MAX));
            }
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
                double angle = std::atan2(dyF - sy, dxF - sx);
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

        sx += vx;  sy += vy;
        int rx = static_cast<int>(std::round(sx));
        int ry = static_cast<int>(std::round(sy));
        int step_x = rx - cx;
        int step_y = ry - cy;
        if (step_x || step_y)
        {
            queueMove(step_x, step_y);
            cx = rx; cy = ry;
        }
    }

    int final_x = dx - cx;
    int final_y = dy - cy;
    if (final_x || final_y)
    {
        queueMove(final_x, final_y);
    }
}

void MouseThread::sendMousePress()
{
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &input, sizeof(INPUT));
}

void MouseThread::sendMouseRelease()
{
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &input, sizeof(INPUT));
}

std::pair<double, double> MouseThread::predict_target_position(double target_x, double target_y)
{
    auto current_time = std::chrono::steady_clock::now();

    if (prev_time.time_since_epoch().count() == 0 || !target_detected.load())
    {
        prev_time = current_time;
        prev_x = target_x;
        prev_y = target_y;
        prev_velocity_x = 0.0;
        prev_velocity_y = 0.0;
        return { target_x, target_y };
    }

    double dt = std::chrono::duration<double>(current_time - prev_time).count();
    if (dt < 1e-8) dt = 1e-8;

    double vx = (target_x - prev_x) / dt;
    double vy = (target_y - prev_y) / dt;

    vx = std::clamp(vx, -20000.0, 20000.0);
    vy = std::clamp(vy, -20000.0, 20000.0);

    prev_time = current_time;
    prev_x = target_x;
    prev_y = target_y;
    prev_velocity_x = vx;
    prev_velocity_y = vy;

    double predictedX = target_x + vx * prediction_interval;
    double predictedY = target_y + vy * prediction_interval;

    double detectionDelay = dml_detector ? dml_detector->lastInferenceTimeDML.count() : 0.05;
    predictedX += vx * detectionDelay;
    predictedY += vy * detectionDelay;

    return { predictedX, predictedY };
}

void MouseThread::sendMovementToDriver(int dx, int dy)
{
    if (dx == 0 && dy == 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(input_method_mutex);
    bool success = false;

    switch (movement_backend)
    {
    case MovementBackend::SendInput:
        success = sendInputMovement(dx, dy, false);
        break;
    case MovementBackend::SendInputNoCoalesce:
        success = sendInputMovement(dx, dy, true);
        break;
    case MovementBackend::MouseEvent:
        mouse_event(MOUSEEVENTF_MOVE, dx, dy, 0, 0);
        success = true;
        break;
    default:
        success = sendInputMovement(dx, dy, false);
        break;
    }

    if (!success && movement_backend != MovementBackend::MouseEvent)
    {
        mouse_event(MOUSEEVENTF_MOVE, dx, dy, 0, 0);
    }
}

std::pair<double, double> MouseThread::calc_movement(double tx, double ty)
{
    double offx = tx - center_x;
    double offy = ty - center_y;
    double dist = std::hypot(offx, offy);
    double speed = calculate_speed_multiplier(dist);

    double degPerPxX = fov_x / screen_width;
    double degPerPxY = fov_y / screen_height;

    double mmx = offx * degPerPxX;
    double mmy = offy * degPerPxY;

    double corr = 1.0;
    double fps = static_cast<double>(captureFps.load());
    if (fps > 30.0) corr = 30.0 / fps;

    auto counts_pair = config.degToCounts(mmx, mmy, fov_x);
    double move_x = counts_pair.first * speed * corr;
    double move_y = counts_pair.second * speed * corr;

    return { move_x, move_y };
}

double MouseThread::calculate_speed_multiplier(double distance)
{
    if (distance < config.snapRadius)
        return min_speed_multiplier * config.snapBoostFactor;

    if (distance < config.nearRadius)
    {
        double t = distance / config.nearRadius;
        double curve = 1.0 - std::pow(1.0 - t, config.speedCurveExponent);
        return min_speed_multiplier +
            (max_speed_multiplier - min_speed_multiplier) * curve;
    }

    double norm = std::clamp(distance / max_distance, 0.0, 1.0);
    return min_speed_multiplier +
        (max_speed_multiplier - min_speed_multiplier) * norm;
}

void MouseThread::beginTargetSwitch(double previousPivotX, double previousPivotY, double newPivotX, double newPivotY)
{
    if (!target_switching_enabled)
        return;

    target_switch_active = true;
    target_switch_delaying = target_switch_reaction_ms > 0.0;
    target_switch_start_time = std::chrono::steady_clock::now();
    target_switch_move_time = target_switch_start_time;
    switch_overshoot_dir_x = 0.0;
    switch_overshoot_dir_y = 0.0;

    double dirX = newPivotX - previousPivotX;
    double dirY = newPivotY - previousPivotY;
    double length = std::hypot(dirX, dirY);
    if (length > 1e-5)
    {
        switch_overshoot_dir_x = dirX / length;
        switch_overshoot_dir_y = dirY / length;
    }
}

std::pair<int, int> MouseThread::resolveMovementSteps(double moveX, double moveY)
{
    double totalX = moveX + residual_move_x;
    double totalY = moveY + residual_move_y;

    int stepX = static_cast<int>(std::lround(totalX));
    int stepY = static_cast<int>(std::lround(totalY));

    residual_move_x = totalX - static_cast<double>(stepX);
    residual_move_y = totalY - static_cast<double>(stepY);

    return { stepX, stepY };
}

bool MouseThread::check_target_in_scope(double target_x, double target_y, double target_w, double target_h, double reduction_factor)
{
    double center_target_x = target_x + target_w / 2.0;
    double center_target_y = target_y + target_h / 2.0;

    double reduced_w = target_w * (reduction_factor / 2.0);
    double reduced_h = target_h * (reduction_factor / 2.0);

    double x1 = center_target_x - reduced_w;
    double x2 = center_target_x + reduced_w;
    double y1 = center_target_y - reduced_h;
    double y2 = center_target_y + reduced_h;

    return (center_x > x1 && center_x < x2 && center_y > y1 && center_y < y2);
}

void MouseThread::moveMouse(const AimbotTarget& target)
{
    std::lock_guard lg(input_method_mutex);

    auto predicted = predict_target_position(
        target.x + target.w / 2.0,
        target.y + target.h / 2.0);

    auto mv = calc_movement(predicted.first, predicted.second);
    auto steps = resolveMovementSteps(mv.first, mv.second);
    if (steps.first == 0 && steps.second == 0)
    {
        return;
    }
    queueMove(steps.first, steps.second);
}

void MouseThread::moveMousePivot(double pivotX, double pivotY)
{
    updatePivotTracking(pivotX, pivotY, true);
}

void MouseThread::updatePivotTracking(double pivotX, double pivotY, bool allowMovement)
{
    std::lock_guard lg(input_method_mutex);

    auto current_time = std::chrono::steady_clock::now();

    if (target_switching_enabled)
    {
        double previousTrackedX = current_target_x;
        double previousTrackedY = current_target_y;
        bool hadPrevious = current_target_valid;

        bool triggered = false;
        if (pending_target_switch)
        {
            triggered = true;
        }
        else if (hadPrevious)
        {
            double dist = std::hypot(pivotX - previousTrackedX, pivotY - previousTrackedY);
            if (dist >= target_switch_detection_px)
            {
                triggered = true;
            }
        }

        if (triggered)
        {
            beginTargetSwitch(hadPrevious ? previousTrackedX : pivotX,
                hadPrevious ? previousTrackedY : pivotY,
                pivotX,
                pivotY);
            pending_target_switch = false;
        }
    }
    else
    {
        pending_target_switch = false;
        if (target_switch_active)
        {
            target_switch_active = false;
            target_switch_delaying = false;
            switch_overshoot_dir_x = 0.0;
            switch_overshoot_dir_y = 0.0;
        }
    }

    current_target_x = pivotX;
    current_target_y = pivotY;
    current_target_valid = true;

    bool initialFrame = (prev_time.time_since_epoch().count() == 0 || !target_detected.load());

    double vx = 0.0;
    double vy = 0.0;

    if (initialFrame)
    {
        prev_time = current_time;
        prev_x = pivotX;
        prev_y = pivotY;
        prev_velocity_x = 0.0;
        prev_velocity_y = 0.0;
    }
    else
    {
        double dt = std::chrono::duration<double>(current_time - prev_time).count();
        prev_time = current_time;
        dt = std::max(dt, 1e-8);

        vx = std::clamp((pivotX - prev_x) / dt, -20000.0, 20000.0);
        vy = std::clamp((pivotY - prev_y) / dt, -20000.0, 20000.0);
        prev_x = pivotX;
        prev_y = pivotY;
        prev_velocity_x = vx;
        prev_velocity_y = vy;
    }

    if (!allowMovement)
    {
        return;
    }

    double predX = pivotX + vx * prediction_interval + vx * 0.002;
    double predY = pivotY + vy * prediction_interval + vy * 0.002;

    double speedFactor = 1.0;
    auto applySwitch = [&](double& targetX, double& targetY) -> bool
    {
        if (!target_switch_active || !target_switching_enabled)
        {
            speedFactor = 1.0;
            return true;
        }

        double elapsedReaction = std::chrono::duration<double, std::milli>(current_time - target_switch_start_time).count();
        if (target_switch_delaying)
        {
            if (elapsedReaction < target_switch_reaction_ms)
            {
                return false;
            }
            target_switch_delaying = false;
            target_switch_move_time = current_time;
        }

        double elapsedMoveMs = std::chrono::duration<double, std::milli>(current_time - target_switch_move_time).count();
        if (elapsedMoveMs < 0.0)
            elapsedMoveMs = 0.0;

        double rampMs = target_switch_slowdown_ms;
        double baseFactor = std::clamp(target_switch_speed_factor, 0.0, 1.0);

        if (rampMs > 1e-3)
        {
            double t = std::clamp(elapsedMoveMs / rampMs, 0.0, 1.0);
            speedFactor = baseFactor + (1.0 - baseFactor) * t;
            speedFactor = std::clamp(speedFactor, 0.0, 1.0);

            double overshoot = target_switch_overshoot_px;
            if (overshoot > 0.0 && (switch_overshoot_dir_x != 0.0 || switch_overshoot_dir_y != 0.0))
            {
                double decay = 1.0 - t;
                targetX += switch_overshoot_dir_x * overshoot * decay;
                targetY += switch_overshoot_dir_y * overshoot * decay;
                if (t >= 1.0)
                {
                    switch_overshoot_dir_x = 0.0;
                    switch_overshoot_dir_y = 0.0;
                }
            }

            if (t >= 1.0 && switch_overshoot_dir_x == 0.0 && switch_overshoot_dir_y == 0.0)
            {
                target_switch_active = false;
            }
        }
        else
        {
            speedFactor = std::clamp(baseFactor, 0.0, 1.0);
            switch_overshoot_dir_x = 0.0;
            switch_overshoot_dir_y = 0.0;
            target_switch_active = false;
        }

        return true;
    };

    if (!applySwitch(predX, predY))
    {
        return;
    }

    auto mv = calc_movement(predX, predY);
    mv.first *= speedFactor;
    mv.second *= speedFactor;
    auto steps = resolveMovementSteps(mv.first, mv.second);
    int mx = steps.first;
    int my = steps.second;

    if (mx == 0 && my == 0)
    {
        return;
    }

    if (wind_mouse_enabled)
    {
        windMouseMoveRelative(mx, my);
    }
    else
    {
        queueMove(mx, my);
    }
}

void MouseThread::pressMouse(const AimbotTarget& target)
{
    std::lock_guard<std::mutex> lock(input_method_mutex);

    bool inScope = check_target_in_scope(target.x, target.y, target.w, target.h, bScope_multiplier);
    auto now = std::chrono::steady_clock::now();

    if (inScope)
    {
        last_in_scope_time = now;

        if (!mouse_pressed)
        {
            double sinceRelease = std::chrono::duration<double, std::milli>(now - last_release_time).count();
            if (sinceRelease >= auto_shoot_fire_delay_ms)
            {
                sendMousePress();
                mouse_pressed = true;
                last_press_time = now;
            }
        }
        else if (!auto_shoot_hold_until_off_target && auto_shoot_press_duration_ms > 0.0)
        {
            double held = std::chrono::duration<double, std::milli>(now - last_press_time).count();
            if (held >= auto_shoot_press_duration_ms)
            {
                sendMouseRelease();
                mouse_pressed = false;
                last_release_time = now;
            }
        }
    }
    else if (mouse_pressed)
    {
        if (auto_shoot_hold_until_off_target)
        {
            sendMouseRelease();
            mouse_pressed = false;
            last_release_time = now;
            return;
        }

        double held = std::chrono::duration<double, std::milli>(now - last_press_time).count();
        double sinceScope = std::chrono::duration<double, std::milli>(now - last_in_scope_time).count();

        bool releaseDueToHold = auto_shoot_press_duration_ms > 0.0 && held >= auto_shoot_press_duration_ms;
        bool releaseDueToGrace = sinceScope >= auto_shoot_full_auto_grace_ms;

        if (releaseDueToHold || releaseDueToGrace)
        {
            sendMouseRelease();
            mouse_pressed = false;
            last_release_time = now;
        }
    }
}

void MouseThread::releaseMouse()
{
    std::lock_guard<std::mutex> lock(input_method_mutex);

    if (mouse_pressed)
    {
        sendMouseRelease();
        mouse_pressed = false;
        last_release_time = std::chrono::steady_clock::now();
    }
}

void MouseThread::resetPrediction()
{
    prev_time = std::chrono::steady_clock::time_point();
    prev_x = 0;
    prev_y = 0;
    prev_velocity_x = 0;
    prev_velocity_y = 0;
    target_detected.store(false);
    residual_move_x = 0.0;
    residual_move_y = 0.0;
    pending_target_switch = false;
    current_target_valid = false;
    current_target_x = 0.0;
    current_target_y = 0.0;
    target_switch_active = false;
    target_switch_delaying = false;
    switch_overshoot_dir_x = 0.0;
    switch_overshoot_dir_y = 0.0;
    target_switch_start_time = std::chrono::steady_clock::time_point();
    target_switch_move_time = std::chrono::steady_clock::time_point();
}

void MouseThread::checkAndResetPredictions()
{
    auto current_time = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(current_time - last_target_time).count();

    if (elapsed > 0.5 && target_detected.load())
    {
        resetPrediction();
    }
}

std::vector<std::pair<double, double>> MouseThread::predictFuturePositions(double pivotX, double pivotY, int frames)
{
    std::vector<std::pair<double, double>> result;
    result.reserve(frames);

    const double fixedFps = 30.0;
    double frame_time = 1.0 / fixedFps;

    auto current_time = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(current_time - prev_time).count();

    if (prev_time.time_since_epoch().count() == 0 || dt > 0.5)
    {
        return result;
    }

    double vx = prev_velocity_x;
    double vy = prev_velocity_y;
    
    for (int i = 1; i <= frames; i++)
    {
        double t = frame_time * i;

        double px = pivotX + vx * t;
        double py = pivotY + vy * t;

        result.push_back({ px, py });
    }

    return result;
}

void MouseThread::storeFuturePositions(const std::vector<std::pair<double, double>>& positions)
{
    std::lock_guard<std::mutex> lock(futurePositionsMutex);
    futurePositions = positions;
}

void MouseThread::clearFuturePositions()
{
    std::lock_guard<std::mutex> lock(futurePositionsMutex);
    futurePositions.clear();
}

std::vector<std::pair<double, double>> MouseThread::getFuturePositions()
{
    std::lock_guard<std::mutex> lock(futurePositionsMutex);
    return futurePositions;
}

void MouseThread::setTargetDetected(bool detected)
{
    bool previous = target_detected.exchange(detected);
    if (detected)
    {
        if (!previous)
        {
            pending_target_switch = true;
        }
    }
    else
    {
        pending_target_switch = false;
        current_target_valid = false;
        target_switch_active = false;
        target_switch_delaying = false;
        switch_overshoot_dir_x = 0.0;
        switch_overshoot_dir_y = 0.0;
        target_switch_start_time = std::chrono::steady_clock::time_point();
        target_switch_move_time = std::chrono::steady_clock::time_point();
    }
}

