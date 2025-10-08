#ifndef SUNONE_AIMBOT_CPP_H
#define SUNONE_AIMBOT_CPP_H

#include <mutex>
#include <atomic>

#include "config.h"
#ifdef USE_CUDA
#include "trt_detector.h"
#endif
#include "dml_detector.h"
#include "mouse.h"
#include "SerialConnection.h"
#include "detection_buffer.h"
#include "Kmbox_b.h"
#include "KmboxNetConnection.h"

extern Config config;
#ifdef USE_CUDA
extern TrtDetector trt_detector;
#endif
extern DirectMLDetector* dml_detector;
extern DetectionBuffer detectionBuffer;
extern MouseThread* globalMouseThread;
extern std::mutex configMutex;
extern SerialConnection* arduinoSerial;
extern Kmbox_b_Connection* kmboxSerial;
extern KmboxNetConnection* kmboxNetSerial;
extern std::atomic<bool> input_method_changed;
extern std::atomic<bool> aiming;
extern std::atomic<bool> shooting;
extern std::atomic<bool> zooming;

class MouseAITuner;
extern MouseAITuner mouseAITuner;

#endif // SUNONE_AIMBOT_CPP_H