#include "anim_shock.h"
#include "face_state.h"
#include <Arduino.h>
#include <math.h>

namespace AnimShock {

namespace {
    const float CENTER_X = 120.0f;
    const float BASE_Y   = 100.0f;
    const float EYE_SPACING = 104.0f;

    uint32_t startTime = 0;
    uint32_t lastFrame = 0;

    void drawLightning(TFT_eSprite &s, int x1, int y1, int x2, int y2, uint16_t col) {
        int mx = (x1 + x2) / 2 + random(-8, 9);
        int my = (y1 + y2) / 2 + random(-8, 9);
        s.drawLine(x1, y1, mx, my, col);
        s.drawLine(mx, my, x2, y2, col);
    }
}

void init(TFT_eSPI &tft) { (void)tft; }

void reset() {
    uint32_t now = millis();
    startTime = now;
    lastFrame = now;
    // Электрический лазурно-белый неон
    FaceState::setTargetPalette(100, 240, 255, 20, 100, 255, 5, 20, 80);
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    (void)tft;
    uint32_t now = millis();
    if (now - lastFrame < 16) return;
    lastFrame = now;

    uint32_t elapsed = now - startTime;
    float tSec = elapsed * 0.001f;

    // Стробоскоп-эффект: мигание фона
    bool strobe = (random(100) < 25);
    sprite.fillSprite(strobe ? 0x10A2 : TFT_BLACK);

    float lx = CENTER_X - EYE_SPACING * 0.5f + (float)random(-2, 3);
    float rx = CENTER_X + EYE_SPACING * 0.5f + (float)random(-2, 3);
    float ly = BASE_Y + (float)random(-2, 3);
    float ry = BASE_Y + (float)random(-2, 3);

    // Глаза широко распахнуты от шока/тока
    float eyeW = 54.0f, eyeH = 74.0f;
    uint16_t cEye = strobe ? TFT_WHITE : FaceState::current.colorEye;
    uint16_t cGlow = FaceState::current.colorGlow1;

    sprite.fillRoundRect((int)(lx - eyeW*0.5f), (int)(ly - eyeH*0.5f), (int)eyeW, (int)eyeH, 16, cEye);
    sprite.fillRoundRect((int)(rx - eyeW*0.5f), (int)(ry - eyeH*0.5f), (int)eyeW, (int)eyeH, 16, cEye);

    // Зрачки крошечные от шока
    sprite.fillCircle((int)lx, (int)ly, 4, TFT_BLACK);
    sprite.fillCircle((int)rx, (int)ry, 4, TFT_BLACK);

    // Электрические дуги/молнии вокруг глаз
    for (int i = 0; i < 4; i++) {
        int ax = (int)lx + random(-30, 31);
        int ay = (int)ly + random(-40, 41);
        int bx = (int)rx + random(-30, 31);
        int by = (int)ry + random(-40, 41);
        drawLightning(sprite, ax, ay, bx, by, TFT_WHITE);
        drawLightning(sprite, ax, ay, (int)CENTER_X, (int)BASE_Y + random(-20, 21), cEye);
    }

    sprite.pushSprite(0, 40);
}

} // namespace AnimShock
