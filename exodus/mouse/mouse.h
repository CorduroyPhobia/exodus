#ifndef MOUSE_H
#define MOUSE_H

#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <utility>
#include <queue>
#include <thread>
#include <condition_variable>
#include <string>

#include "AimbotTarget.h"
#ifndef MOUSEEVENTF_MOVE_NOCOALESCE
#define MOUSEEVENTF_MOVE_NOCOALESCE 0x2000
#endif

class MouseThread
{
private:
    double screen_width;
    double screen_height;
    double prediction_interval;
    double fov_x;
    double fov_y;
    double max_distance;
    double min_speed_multiplier;
    double max_speed_multiplier;
    double center_x;
    double center_y;
    bool   auto_shoot;
    bool   auto_shoot_hold_until_off_target;
    float  bScope_multiplier;
    double auto_shoot_fire_delay_ms;
    double auto_shoot_press_duration_ms;
    double auto_shoot_full_auto_grace_ms;

    double prev_x, prev_y;
    double prev_velocity_x, prev_velocity_y;
    std::chrono::time_point<std::chrono::steady_clock> prev_time;
    std::chrono::steady_clock::time_point last_target_time;
    std::chrono::steady_clock::time_point last_press_time;
    std::chrono::steady_clock::time_point last_release_time;
    std::chrono::steady_clock::time_point last_in_scope_time;
    std::atomic<bool> target_detected{ false };
    std::atomic<bool> mouse_pressed{ false };

    void sendMovementToDriver(int dx, int dy);

    struct Move { int dx; int dy; };

    std::queue<Move>              moveQueue;
    std::mutex                    queueMtx;
    std::condition_variable       queueCv;
    const size_t                  queueLimit = 5;
    std::thread                   moveWorker;
    std::atomic<bool>             workerStop{ false };

    enum class MovementBackend
    {
        SendInput,
        SendInputNoCoalesce,
        MouseEvent,
        CursorWarp
    };

    MovementBackend movement_backend = MovementBackend::SendInput;

    std::vector<std::pair<double, double>> futurePositions;
    std::mutex                    futurePositionsMutex;

    double residual_move_x = 0.0;
    double residual_move_y = 0.0;

    bool   target_switching_enabled = false;
    double target_switch_reaction_ms = 0.0;
    double target_switch_slowdown_ms = 0.0;
    double target_switch_speed_factor = 1.0;
    double target_switch_overshoot_px = 0.0;
    double target_switch_detection_px = 1.0;

    bool   pending_target_switch = false;
    bool   current_target_valid = false;
    double current_target_x = 0.0;
    double current_target_y = 0.0;

    bool   target_switch_active = false;
    bool   target_switch_delaying = false;
    std::chrono::steady_clock::time_point target_switch_start_time{};
    std::chrono::steady_clock::time_point target_switch_move_time{};
    double switch_overshoot_dir_x = 0.0;
    double switch_overshoot_dir_y = 0.0;

    void moveWorkerLoop();
    void queueMove(int dx, int dy);
    bool sendInputMovement(int dx, int dy, bool noCoalesce);
    bool warpCursor(int dx, int dy);

    bool   wind_mouse_enabled = true;
    double wind_G, wind_W, wind_M, wind_D;
    double wind_speed_multiplier, wind_min_velocity, wind_target_radius;
    double wind_randomness, wind_inertia, wind_step_randomness;
    double wind_max_step_config;
    void   windMouseMoveRelative(int dx, int dy);

    void sendMousePress();
    void sendMouseRelease();

    std::pair<double, double> calc_movement(double target_x, double target_y);
    std::pair<int, int> resolveMovementSteps(double moveX, double moveY);
    double calculate_speed_multiplier(double distance);
    void beginTargetSwitch(double previousPivotX, double previousPivotY, double newPivotX, double newPivotY);
    void setMovementMethod(const std::string& methodName);

public:
    std::mutex input_method_mutex;

    MouseThread(
        int  resolution,
        int  fovX,
        int  fovY,
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
    );
    ~MouseThread();

    void updateConfig(
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
    );

    void moveMousePivot(double pivotX, double pivotY);
    void updatePivotTracking(double pivotX, double pivotY, bool allowMovement);
    std::pair<double, double> predict_target_position(double target_x, double target_y);
    void moveMouse(const AimbotTarget& target);
    void pressMouse(const AimbotTarget& target);
    void releaseMouse();
    void resetPrediction();
    void checkAndResetPredictions();
    bool check_target_in_scope(double target_x, double target_y,
        double target_w, double target_h, double reduction_factor);

    std::vector<std::pair<double, double>> predictFuturePositions(double pivotX, double pivotY, int frames);
    void storeFuturePositions(const std::vector<std::pair<double, double>>& positions);
    void clearFuturePositions();
    std::vector<std::pair<double, double>> getFuturePositions();

    void setTargetDetected(bool detected);
    void setLastTargetTime(const std::chrono::steady_clock::time_point& t) { last_target_time = t; }
};

#endif // MOUSE_H
