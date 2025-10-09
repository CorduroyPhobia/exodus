#ifndef EXODUS_H
#define EXODUS_H

#include "config.h"
#ifdef USE_CUDA
#include "trt_detector.h"
#endif
#include "dml_detector.h"
#include "mouse.h"
#include "detection_buffer.h"

extern Config config;
#ifdef USE_CUDA
extern TrtDetector trt_detector;
#endif
extern DirectMLDetector* dml_detector;
extern DetectionBuffer detectionBuffer;
extern MouseThread* globalMouseThread;
extern std::atomic<bool> aiming;
extern std::atomic<bool> hipAiming;
extern std::atomic<bool> overlayVisible;
extern std::atomic<bool> shooting;
extern std::atomic<bool> zooming;

#endif // EXODUS_H
