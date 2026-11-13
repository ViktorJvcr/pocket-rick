#include "anim_sparkle.h"
#include "face_state.h"
#include <Arduino.h>
#include <math.h>

namespace AnimSparkle {

namespace {
    const float CENTER_X = 120.0f;
    const float BASE_Y   = 100.0f;
    const float EYE_SPACING = 104.0f;

    uint32_t startTime = 0;
    uint32_t lastFrame = 0;

    void drawStar(TFT_eSprite &s, int x, int y, int r, uint16_t col) {
        if (r < 2) return;
        s.drawFastHLine(x - r, y, 2 * r + 1, col);
        s.drawFastVLine(x, y - r, 2 * r + 1, col);
        int d = (r * 2) / 3;
        if (d >= 1) {
            s.drawLine(x - d, y - d, x + d, y + d, col);
            s.drawLine(x - d, y + d, x + d, y - d, col);
        }
        s.fillCircle(x, y, 2, TFT_WHITE);
    }
}

void init(TFT_eSPI &tft) { (void)tft; }

void reset() {
    uint32_t now = millis();
    startTime = now;
    lastFrame = now;
    // Золотисто-розовый праздничный неон
    FaceState::setTargetPalette(255, 220, 60, 200, 40, 150, 40, 10, 30);
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    (void)tft;
    uint32_t now = millis();
    if (now - lastFrame < 16) return;
    lastFrame = now;

    uint32_t dt = now - startTime;
    float tSec = dt * 0.001f;

    sprite.fillSprite(TFT_BLACK);

    float lx = CENTER_X - EYE_SPACING * 0.5f;
    float rx = CENTER_X + EYE_SPACING * 0.5f;

    uint16_t cEye = FaceState::current.colorEye;

    // Счастливые глазки-арки ^^
    float arcW = 46.0f;
    for (int i = 0; i <= 14; i++) {
        float u = (i / 14.0f) * 2.0f - 1.0f; // -1..1
        float dy = -16.0f * (1.0f - u * u);
        sprite.fillCircle((int)(lx + u * (arcW * 0.5f)), (int)(BASE_Y + dy + 8), 3, cEye);
        sprite.fillCircle((int)(rx + u * (arcW * 0.5f)), (int)(BASE_Y + dy + 8), 3, cEye);
    }

    // Две волны звезд, разлетающихся из глаз
    for (int wave = 0; wave < 2; wave++) {
        float waveTime = tSec - wave * 0.35f;
        if (waveTime > 0.0f) {
            float dist = waveTime * 65.0f;
            int rStar = (int)(6.0f + sinf(waveTime * 8.0f) * 3.0f);
            if (dist < 80.0f) {
                drawStar(sprite, (int)(lx - dist * 0.7f), (int)(BASE_Y - dist * 0.8f), rStar, cEye);
                drawStar(sprite, (int)(rx + dist * 0.7f), (int)(BASE_Y - dist * 0.8f), rStar, cEye);
                drawStar(sprite, (int)(lx + dist * 0.2f), (int)(BASE_Y - dist * 1.1f), rStar - 1, TFT_WHITE);
                drawStar(sprite, (int)(rx - dist * 0.2f), (int)(BASE_Y - dist * 1.1f), rStar - 1, TFT_WHITE);
            }
        }
    }

    // Довольная широкая улыбка
    for (int i = 0; i <= 12; i++) {
        float u = (i / 12.0f) * 2.0f - 1.0f;
        float sy = BASE_Y + 28.0f + 6.0f * (u * u);
        sprite.fillCircle((int)(CENTER_X + u * 16.0f), (int)sy, 2, cEye);
    }

    sprite.pushSprite(0, 40);
}

} // namespace AnimSparkle
