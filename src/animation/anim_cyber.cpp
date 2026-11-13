#include "anim_cyber.h"
#include "face_state.h"
#include <Arduino.h>

namespace AnimCyber {

namespace {
    const float CENTER_X = 120.0f;
    const float BASE_Y   = 100.0f;
    const float EYE_SPACING = 104.0f;

    const int DROP_COUNT = 16;
    struct RainDrop {
        int x;
        float y;
        float speed;
        uint8_t length;
    };
    RainDrop drops[DROP_COUNT];

    uint32_t lastFrame = 0;
}

void resetDrops() {
    for (int i = 0; i < DROP_COUNT; i++) {
        drops[i].x = 10 + i * 14;
        drops[i].y = (float)random(-100, 50);
        drops[i].speed = 3.0f + (float)random(10, 35) / 10.0f;
        drops[i].length = random(10, 26);
    }
}

void init(TFT_eSPI &tft) {
    (void)tft;
    resetDrops();
}

void reset() {
    uint32_t now = millis();
    lastFrame = now;
    // Киберпанк ядовито-зеленый Matrix
    FaceState::setTargetPalette(0, 255, 120, 0, 100, 40, 0, 20, 10);
    resetDrops();
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    (void)tft;
    uint32_t now = millis();
    if (now - lastFrame < 16) return;
    lastFrame = now;

    sprite.fillSprite(TFT_BLACK);

    uint16_t cGreen = FaceState::current.colorEye;
    uint16_t cDim   = FaceState::current.colorGlow1;

    // 1. Падающие струи цифрового дождя (Matrix rain)
    for (int i = 0; i < DROP_COUNT; i++) {
        drops[i].y += drops[i].speed;
        if (drops[i].y - drops[i].length > 200) {
            drops[i].y = -random(10, 40);
            drops[i].speed = 3.0f + (float)random(10, 35) / 10.0f;
            drops[i].length = random(10, 26);
        }

        int headY = (int)drops[i].y;
        int tailY = headY - drops[i].length;

        // Хвост
        sprite.drawFastVLine(drops[i].x, (tailY < 0 ? 0 : tailY), (headY - tailY), cDim);
        // Яркая белая головная капля
        if (headY >= 0 && headY < 200) {
            sprite.drawPixel(drops[i].x, headY, TFT_WHITE);
            sprite.drawPixel(drops[i].x, headY - 1, cGreen);
        }
    }

    // 2. Кибернетические глаза поверх дождя (цифровой терминал)
    float lx = CENTER_X - EYE_SPACING * 0.5f;
    float rx = CENTER_X + EYE_SPACING * 0.5f;
    float eyeW = 46.0f, eyeH = 68.0f;

    // Контуры глаз в виде цифрового сканера
    sprite.drawRoundRect((int)(lx - eyeW*0.5f), (int)(BASE_Y - eyeH*0.5f), (int)eyeW, (int)eyeH, 12, cGreen);
    sprite.drawRoundRect((int)(rx - eyeW*0.5f), (int)(BASE_Y - eyeH*0.5f), (int)eyeW, (int)eyeH, 12, cGreen);

    // Внутри глаз - бинарные коды "0 1"
    for (int row = -2; row <= 2; row++) {
        int ry = (int)BASE_Y + row * 10;
        int val = (now / 150 + row) % 4;
        if (val == 0) {
            sprite.drawFastHLine((int)lx - 12, ry, 24, cGreen);
            sprite.drawFastHLine((int)rx - 12, ry, 24, cGreen);
        } else if (val == 1) {
            sprite.drawFastHLine((int)lx - 6, ry, 12, TFT_WHITE);
            sprite.drawFastHLine((int)rx - 6, ry, 12, TFT_WHITE);
        } else {
            sprite.drawFastHLine((int)lx - 16, ry, 8, cDim);
            sprite.drawFastHLine((int)lx + 8, ry, 8, cDim);
            sprite.drawFastHLine((int)rx - 16, ry, 8, cDim);
            sprite.drawFastHLine((int)rx + 8, ry, 8, cDim);
        }
    }

    // Прицельная рамка по центру
    sprite.drawCircle((int)CENTER_X, (int)BASE_Y, 6, cDim);

    sprite.pushSprite(0, 40);
}

} // namespace AnimCyber
