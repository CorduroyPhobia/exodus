#ifndef EXODUS_H
#define EXODUS_H

#include "config.h"
#include "dml_detector.h"
#include "mouse.h"
#include "detection_buffer.h"

extern Config config;
extern DirectMLDetector* dml_detector;
extern DetectionBuffer detectionBuffer;
extern MouseThread* globalMouseThread;
extern std::atomic<bool> aiming;
extern std::atomic<bool> hipAiming;
extern std::atomic<bool> overlayVisible;
extern std::atomic<bool> shooting;
extern std::atomic<bool> zooming;

#endif // EXODUS_H
