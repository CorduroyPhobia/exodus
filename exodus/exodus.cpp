#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <Windows.h>

#include <iostream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <vector>

#include "capture.h"
#include "mouse.h"
#include "exodus.h"
#include "keyboard_listener.h"
#include "overlay.h"
#include "other_tools.h"
#include "virtual_camera.h"
#include "pi/pi_serial_manager.h"

std::condition_variable frameCV;
std::atomic<bool> shouldExit(false);
std::atomic<bool> aiming(false);
std::atomic<bool> hipAiming(false);
std::atomic<bool> overlayVisible(false);
std::atomic<bool> detectionPaused(false);
std::mutex configMutex;

#ifdef USE_CUDA
TrtDetector trt_detector;
#endif

DirectMLDetector* dml_detector = nullptr;
MouseThread* globalMouseThread = nullptr;
Config config;

std::atomic<bool> detection_resolution_changed(false);
std::atomic<bool> capture_method_changed(false);
std::atomic<bool> capture_cursor_changed(false);
std::atomic<bool> capture_borders_changed(false);
std::atomic<bool> capture_fps_changed(false);
std::atomic<bool> capture_window_changed(false);
std::atomic<bool> detector_model_changed(false);
std::atomic<bool> show_window_changed(false);
std::atomic<bool> zooming(false);
std::atomic<bool> shooting(false);

static std::vector<std::string> collectPresetFilenames()
{
    std::vector<std::string> names;
    std::filesystem::path dir = std::filesystem::current_path() / "presets";
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec))
        return names;

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file())
            continue;
        const auto& path = entry.path();
        if (path.extension() != ".ini")
            continue;
        names.push_back(path.filename().string());
    }

    std::sort(names.begin(), names.end());
    return names;
}

static void applyPresetSideEffectsFromPi(const Config& previous)
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
        previous.auto_shoot_hold_until_off_target != config.auto_shoot_hold_until_off_target ||
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
            config.auto_shoot_hold_until_off_target,
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
        if (g_hwnd)
        {
            SetLayeredWindowAttributes(g_hwnd, 0, opacity, LWA_ALPHA);
        }
    }

    if (previous.show_window != config.show_window)
    {
        show_window_changed.store(true);
    }
}

static void handlePresetSelectionFromPi(const std::string& presetName)
{
    std::lock_guard<std::mutex> lock(configMutex);

    std::filesystem::path dir = std::filesystem::current_path() / "presets";
    std::filesystem::path presetPath = dir / presetName;
    presetPath = presetPath.lexically_normal();

    if (presetPath.filename().string() != presetName)
    {
        std::cerr << "[PiSerial] Ignoring invalid preset selection: " << presetName << std::endl;
        return;
    }

    std::error_code ec;
    if (!std::filesystem::exists(presetPath, ec))
    {
        std::cerr << "[PiSerial] Selected preset not found: " << presetPath << std::endl;
        return;
    }

    Config previous = config;
    if (!config.loadConfig(presetPath.string()))
    {
        std::cerr << "[PiSerial] Failed to load preset: " << presetPath << std::endl;
        config = previous;
        return;
    }

    config.active_preset = presetPath.filename().string();
    applyPresetSideEffectsFromPi(previous);
    config.saveConfig();

    std::cout << "[PiSerial] Loaded preset from Pi: " << config.active_preset << std::endl;
}
static void ensureActivePresetLoaded()
{
    if (config.active_preset.empty())
        return;

    std::filesystem::path presetPath = config.activePresetPath();
    if (presetPath.empty())
        return;

    std::error_code ec;
    if (!std::filesystem::exists(presetPath, ec))
    {
        std::cerr << "[Config] Active preset not found: " << presetPath << std::endl;
        config.active_preset.clear();
        config.saveConfig();
        return;
    }

    Config previous = config;
    std::string storedIdentifier = config.active_preset;
    if (!config.loadConfig(presetPath.string()))
    {
        std::cerr << "[Config] Failed to load active preset: " << presetPath << std::endl;
        config = previous;
        config.active_preset.clear();
        config.saveConfig();
        return;
    }

    config.active_preset = storedIdentifier;
    config.saveConfig();
}

void handleEasyNoRecoil(MouseThread& mouseThread)
{
    if (config.easynorecoil && shooting.load() && zooming.load())
    {
        std::lock_guard<std::mutex> lock(mouseThread.input_method_mutex);
        int recoil_compensation = static_cast<int>(config.easynorecoilstrength);

        INPUT input = { 0 };
        input.type = INPUT_MOUSE;
        input.mi.dx = 0;
        input.mi.dy = recoil_compensation;
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_VIRTUALDESK;
        SendInput(1, &input, sizeof(INPUT));
    }
}

void mouseThreadFunction(MouseThread& mouseThread)
{
    int lastVersion = -1;
    bool previousAutoShootActive = false;

    while (!shouldExit)
    {
        std::vector<cv::Rect> boxes;
        std::vector<int> classes;

        {
            std::unique_lock<std::mutex> lock(detectionBuffer.mutex);
            detectionBuffer.cv.wait(lock, [&] {
                return detectionBuffer.version > lastVersion || shouldExit;
                });
            if (shouldExit) break;
            boxes = detectionBuffer.boxes;
            classes = detectionBuffer.classes;
            lastVersion = detectionBuffer.version;
        }

        if (detection_resolution_changed.load())
        {
            {
                std::lock_guard<std::mutex> cfgLock(configMutex);
                mouseThread.updateConfig(
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
                    config.auto_shoot_full_auto_grace_ms
                );
            }
            detection_resolution_changed.store(false);
        }

        bool overlayPause = config.pause_when_overlay_open && overlayVisible.load(std::memory_order_acquire);
        bool aimingActive = aiming.load(std::memory_order_relaxed);
        bool hipActive = hipAiming.load(std::memory_order_relaxed);
        bool canMoveMouse = aimingActive && !overlayPause;
        bool autoShootActive = config.auto_shoot || (config.auto_shoot_with_auto_hip_aim && hipActive);

        AimbotTarget* target = sortTargets(
            boxes,
            classes,
            config.detection_resolution,
            config.detection_resolution,
            config.disable_headshot
        );

        if (target)
        {
            mouseThread.setLastTargetTime(std::chrono::steady_clock::now());
            mouseThread.setTargetDetected(true);

            mouseThread.updatePivotTracking(target->pivotX, target->pivotY, canMoveMouse);

            auto futurePositions = mouseThread.predictFuturePositions(
                target->pivotX,
                target->pivotY,
                config.prediction_futurePositions
            );
            mouseThread.storeFuturePositions(futurePositions);
        }
        else
        {
            mouseThread.clearFuturePositions();
            mouseThread.setTargetDetected(false);
        }

        if (autoShootActive)
        {
            if (canMoveMouse && target)
            {
                mouseThread.pressMouse(*target);
            }
            else
            {
                mouseThread.releaseMouse();
            }
        }
        else if (previousAutoShootActive)
        {
            mouseThread.releaseMouse();
        }

        if (overlayPause)
        {
            mouseThread.releaseMouse();
        }

        previousAutoShootActive = autoShootActive;

        handleEasyNoRecoil(mouseThread);

        mouseThread.checkAndResetPredictions();

        delete target;
    }
}

int main()
{
    try
    {
#ifdef USE_CUDA
        int cuda_devices = 0;
        if (cudaGetDeviceCount(&cuda_devices) != cudaSuccess || cuda_devices == 0)
        {
            std::cerr << "[MAIN] CUDA required but no devices found." << std::endl;
            std::cin.get();
            return -1;
        }
#endif

        SetConsoleOutputCP(CP_UTF8);
        cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_FATAL);

        if (!CreateDirectory(L"screenshots", NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        {
            std::cout << "[MAIN] Error with screenshoot folder" << std::endl;
            std::cin.get();
            return -1;
        }

        if (!CreateDirectory(L"models", NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        {
            std::cout << "[MAIN] Error with models folder" << std::endl;
            std::cin.get();
            return -1;
        }

        if (!CreateDirectory(L"presets", NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        {
            std::cout << "[MAIN] Error with presets folder" << std::endl;
            std::cin.get();
            return -1;
        }

        if (!config.loadConfig())
        {
            std::cerr << "[Config] Error with loading config!" << std::endl;
            std::cin.get();
            return -1;
        }

        ensureActivePresetLoaded();

        if (config.capture_method == "virtual_camera")
        {
            auto cams = VirtualCameraCapture::GetAvailableVirtualCameras();
            if (!cams.empty())
            {
                if (config.virtual_camera_name == "None" ||
                    std::find(cams.begin(), cams.end(), config.virtual_camera_name) == cams.end())
                {
                    config.virtual_camera_name = cams[0];
                    config.saveConfig("config.ini");
                    std::cout << "[MAIN] Set virtual_camera_name = " << config.virtual_camera_name << std::endl;
                }
                std::cout << "[MAIN] Virtual cameras loaded: " << cams.size() << std::endl;
            }
            else
            {
                std::cerr << "[MAIN] No virtual cameras found" << std::endl;
            }
        }

        std::string modelPath = "models/" + config.ai_model;

        if (!std::filesystem::exists(modelPath))
        {
            std::cerr << "[MAIN] Specified model does not exist: " << modelPath << std::endl;

            std::vector<std::string> modelFiles = getModelFiles();

            if (!modelFiles.empty())
            {
                config.ai_model = modelFiles[0];
                config.saveConfig();
                std::cout << "[MAIN] Loaded first available model: " << config.ai_model << std::endl;
            }
            else
            {
                std::cerr << "[MAIN] No models found in 'models' directory." << std::endl;
                std::cin.get();
                return -1;
            }
        }

        MouseThread mouseThread(
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
            config.auto_shoot_full_auto_grace_ms
        );

        globalMouseThread = &mouseThread;

        gPiSerialManager.setPresetSelectionCallback(handlePresetSelectionFromPi);
        gPiSerialManager.updatePresetList(collectPresetFilenames());
        gPiSerialManager.connect();

        std::vector<std::string> availableModels = getAvailableModels();

        if (!config.ai_model.empty())
        {
            std::string modelPath = "models/" + config.ai_model;
            if (!std::filesystem::exists(modelPath))
            {
                std::cerr << "[MAIN] Specified model does not exist: " << modelPath << std::endl;

                if (!availableModels.empty())
                {
                    config.ai_model = availableModels[0];
                    config.saveConfig("config.ini");
                    std::cout << "[MAIN] Loaded first available model: " << config.ai_model << std::endl;
                }
                else
                {
                    std::cerr << "[MAIN] No models found in 'models' directory." << std::endl;
                    std::cin.get();
                    return -1;
                }
            }
        }
        else
        {
            if (!availableModels.empty())
            {
                config.ai_model = availableModels[0];
                config.saveConfig();
                std::cout << "[MAIN] No AI model specified in config. Loaded first available model: " << config.ai_model << std::endl;
            }
            else
            {
                std::cerr << "[MAIN] No AI models found in 'models' directory." << std::endl;
                std::cin.get();
                return -1;
            }
        }

        std::thread dml_detThread;

        if (config.backend == "DML")
        {
            dml_detector = new DirectMLDetector("models/" + config.ai_model);
            std::cout << "[MAIN] DML detector initialized." << std::endl;
            dml_detThread = std::thread(&DirectMLDetector::dmlInferenceThread, dml_detector);
        }
#ifdef USE_CUDA
        else
        {
            trt_detector.initialize("models/" + config.ai_model);
        }
#endif

        detection_resolution_changed.store(true);

        std::thread keyThread(keyboardListener);
        std::thread capThread(captureThread, config.detection_resolution, config.detection_resolution);

#ifdef USE_CUDA
        std::thread trt_detThread(&TrtDetector::inferenceThread, &trt_detector);
#endif
        std::thread mouseMovThread(mouseThreadFunction, std::ref(mouseThread));
        std::thread overlayThread(OverlayThread);

        welcome_message();

        keyThread.join();
        capThread.join();
        if (dml_detThread.joinable())
        {
            dml_detector->shouldExit = true;
            dml_detector->inferenceCV.notify_all();
            dml_detThread.join();
        }

#ifdef USE_CUDA
        trt_detThread.join();
#endif
        mouseMovThread.join();
        overlayThread.join();

        if (dml_detector)
        {
            delete dml_detector;
            dml_detector = nullptr;
        }

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[MAIN] An error has occurred in the main stream: " << e.what() << std::endl;
        std::cout << "Press Enter to exit...";
        std::cin.get();
        return -1;
    }
}