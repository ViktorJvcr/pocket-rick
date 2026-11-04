#pragma once
#include <Arduino.h>
#include "config.h"

struct FacePalette {
    float rEye, gEye, bEye;
    float rGlow1, gGlow1, bGlow1;
    float rGlow2, gGlow2, bGlow2;
    uint16_t colorEye, colorGlow1, colorGlow2, colorBg;
};

class FaceState {
public:
    static FacePalette current;
    static FacePalette target;
    static float cx, cy, spacing;
    static float lw, lh, lr, rw, rh, rr, mw, mh;

    static void init();
    static void setTargetPalette(uint8_t rE, uint8_t gE, uint8_t bE,
                                 uint8_t rG1, uint8_t gG1, uint8_t bG1,
                                 uint8_t rG2, uint8_t gG2, uint8_t bG2);
    static void updateColorLerp(float speed = 0.12f);

    // Рефлекторный прищур перед сменой эмоции: lh/rh -> SQUINT_H (13px), lr/rr -> SQUINT_R (5px)
    static void prepareTransition(bool squint = true);

    static void loadGeometry(float &outCx, float &outCy, float &outSp,
                             float &outLw, float &outLh, float &outLr,
                             float &outRw, float &outRh, float &outRr,
                             float &outMw, float &outMh);
    static void saveGeometry(float inCx, float inCy, float inSp,
                             float inLw, float inLh, float inLr,
                             float inRw, float inRh, float inRr,
                             float inMw, float inMh);
    static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);

private:
    static void refreshPacked(FacePalette& p);
};
