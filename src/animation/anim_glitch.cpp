#include "anim_glitch.h"
#include "face_state.h"
#include <Arduino.h>
#include <math.h>

namespace AnimGlitch {

namespace {
    const float CENTER_X = 120.0f;
    const float BASE_Y   = 100.0f;
    const float EYE_SPACING = 104.0f;

    uint32_t startTime = 0;
    uint32_t lastFrame = 0;

    struct Particle {
        float x, y, vx, vy;
        uint16_t col;
        bool active;
    };
    const int MAX_P = 12;
    Particle particles[MAX_P];
}

void init(TFT_eSPI &tft) {
    (void)tft;
    for (int i = 0; i < MAX_P; i++) particles[i].active = false;
}

void reset() {
    uint32_t now = millis();
    startTime = now;
    lastFrame = now;
    // Кислотно-зеленый портальный неон
    FaceState::setTargetPalette(30, 255, 80, 10, 150, 40, 0, 40, 10);
    for (int i = 0; i < MAX_P; i++) particles[i].active = false;
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    (void)tft;
    uint32_t now = millis();
    if (now - lastFrame < 16) return;
    lastFrame = now;

    uint32_t elapsed = now - startTime;
    float tSec = elapsed * 0.001f;

    sprite.fillSprite(TFT_BLACK);

    // Дрожание и рассинхрон глаз (Portal Glitch)
    float shakeX = (float)((int)(sinf(tSec * 45.0f) * 6.0f));
    float shakeY = (float)((int)(cosf(tSec * 35.0f) * 4.0f));

    // Глаза медленно разъезжаются, а затем схлопываются в точку и пружинят
    float eyeW = 46.0f, eyeH = 68.0f;
    float lx = (CENTER_X - EYE_SPACING * 0.5f) - shakeX - (sinf(tSec * 8.0f) * 12.0f);
    float rx = (CENTER_X + EYE_SPACING * 0.5f) + shakeX + (sinf(tSec * 8.0f) * 12.0f);
    float ly = BASE_Y + shakeY;
    float ry = BASE_Y - shakeY;

    uint16_t cEye = FaceState::current.colorEye;
    uint16_t cGlow = FaceState::current.colorGlow1;

    // Глитч-полосы на фоне
    for (int i = 0; i < 4; i++) {
        int gy = (int)(BASE_Y - 40 + (i * 25) + sinf(tSec * 15.0f + i) * 10.0f);
        int gw = 40 + (int)(sinf(tSec * 20.0f + i * 3) * 30.0f);
        int gx = (int)(CENTER_X + cosf(tSec * 10.0f + i) * 60.0f) - gw / 2;
        sprite.drawFastHLine(gx, gy, gw, cGlow);
    }

    // Левый глаз
    sprite.fillRoundRect((int)(lx - eyeW*0.5f) - 2, (int)(ly - eyeH*0.5f) - 2, (int)eyeW + 4, (int)eyeH + 4, 14, cGlow);
    sprite.fillRoundRect((int)(lx - eyeW*0.5f), (int)(ly - eyeH*0.5f), (int)eyeW, (int)eyeH, 12, cEye);

    // Правый глаз
    sprite.fillRoundRect((int)(rx - eyeW*0.5f) - 2, (int)(ry - eyeH*0.5f) - 2, (int)eyeW + 4, (int)eyeH + 4, 14, cGlow);
    sprite.fillRoundRect((int)(rx - eyeW*0.5f), (int)(ry - eyeH*0.5f), (int)eyeW, (int)eyeH, 12, cEye);

    // Зрачки-аномалии (червоточины)
    float pupilR = 6.0f + sinf(tSec * 18.0f) * 3.0f;
    sprite.fillCircle((int)lx, (int)ly, (int)pupilR, TFT_BLACK);
    sprite.fillCircle((int)rx, (int)ry, (int)pupilR, TFT_BLACK);

    // Смещение строк (Scanline glitch)
    if (((int)(tSec * 10.0f) % 3) == 0) {
        int slY = (int)(BASE_Y - 30.0f + fmodf(tSec * 120.0f, 70.0f));
        sprite.drawFastHLine(20, slY, 200, TFT_WHITE);
    }

    sprite.pushSprite(0, 40);
}

} // namespace AnimGlitch
