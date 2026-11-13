#include "anim_laugh.h"
#include "face_state.h"
#include "sensors/imu_sensor.h"
#include <Arduino.h>
#include <math.h>

namespace AnimLaugh {

namespace {
    // Цвета лица (фирменный неоново-зеленый стиль)
    uint16_t colorEye;
    uint16_t colorGlow1;
    uint16_t colorGlow2;

    // Слёзки радости и румянец
    uint16_t colorTearCore;
    uint16_t colorTearGlow;
    uint16_t colorBlush;
    uint16_t colorBlushGlow;

    // Золотая искра веселья на лбу
    uint16_t colorSparkleCore;
    uint16_t colorSparkleGlow;

    // Базовые параметры (спрайт 240x200, базовый центр Y = 100)
    const float EYE_BASE_W = 52.0f;
    const float EYE_BASE_H = 22.0f;
    const float eyeSpacing = 106.0f;
    const float CENTER_X = 120.0f;
    const float BASE_Y = 100.0f;

    // Текущие координаты физики
    float cx = CENTER_X, cy = BASE_Y;
    float currentSpacing = eyeSpacing;
    float lw = EYE_BASE_W, rw = EYE_BASE_W;
    float lh = EYE_BASE_H, rh = EYE_BASE_H;
    float mw = 20.0f, mh = 6.0f;

    // Целевые координаты
    float t_cx = CENTER_X, t_cy = BASE_Y;
    float t_spacing = eyeSpacing;
    float t_lw = EYE_BASE_W, t_rw = EYE_BASE_W;
    float t_lh = EYE_BASE_H, t_rh = EYE_BASE_H;
    float t_mw = 20.0f, t_mh = 6.0f;

    uint32_t startTime = 0;

    // Брызги слезок смеха
    struct TearSpark {
        float x, y;
        float vx, vy;
        float size;
        bool active;
    };
    const int MAX_TEARS = 6;
    TearSpark tears[MAX_TEARS];
    uint32_t lastTearSpawn = 0;

    // Чистая аниме-искра веселья (✦) справа на лбу без боковых скобок
    void drawAnimeSparkle(TFT_eSprite &s, float fx, float fy, float pulse, uint16_t colCore, uint16_t colGlow) {
        int size = (int)(7.0f + pulse * 4.0f);
        int coreR = size / 3;
        if (coreR < 2) coreR = 2;

        // Внешнее свечение
        s.fillCircle((int)fx, (int)fy, coreR + 3, colGlow);
        s.fillTriangle((int)fx, (int)(fy - size - 2), (int)(fx - coreR - 1), (int)fy, (int)(fx + coreR + 1), (int)fy, colGlow);
        s.fillTriangle((int)fx, (int)(fy + size + 2), (int)(fx - coreR - 1), (int)fy, (int)(fx + coreR + 1), (int)fy, colGlow);
        s.fillTriangle((int)(fx - size - 2), (int)fy, (int)fx, (int)(fy - coreR - 1), (int)fx, (int)(fy + coreR + 1), colGlow);
        s.fillTriangle((int)(fx + size + 2), (int)fy, (int)fx, (int)(fy - coreR - 1), (int)fx, (int)(fy + coreR + 1), colGlow);

        // Яркое ядро четырехконечной звезды
        s.fillTriangle((int)fx, (int)(fy - size), (int)(fx - coreR), (int)fy, (int)(fx + coreR), (int)fy, colCore);
        s.fillTriangle((int)fx, (int)(fy + size), (int)(fx - coreR), (int)fy, (int)(fx + coreR), (int)fy, colCore);
        s.fillTriangle((int)(fx - size), (int)fy, (int)fx, (int)(fy - coreR), (int)fx, (int)(fy + coreR), colCore);
        s.fillTriangle((int)(fx + size), (int)fy, (int)fx, (int)(fy - coreR), (int)fx, (int)(fy + coreR), colCore);

        // Сверкающая белая точка в центре
        s.fillCircle((int)fx, (int)fy, coreR, TFT_WHITE);
    }

    // Горбатые аниме-глаза: арочные пухлые капсулы с выгибом вверх
    void drawHumpCapsuleEye(TFT_eSprite &s, float ex, float ey, float curW, float curH, float humpH) {
        float strokeR = curH * 0.42f;
        if (strokeR < 2.5f) strokeR = 2.5f;
        float halfW = curW * 0.5f - strokeR;
        if (halfW < 6.0f) halfW = 6.0f;

        const int steps = 20;

        // 3 слоя свечения
        for (int layer = 0; layer < 3; layer++) {
            uint16_t col = (layer == 0) ? colorGlow2 : ((layer == 1) ? colorGlow1 : colorEye);
            int r = (int)strokeR + (layer == 0 ? 5 : (layer == 1 ? 2 : 0));

            for (int i = 0; i <= steps; i++) {
                float t = -1.0f + ((float)i / (float)steps) * 2.0f; // -1..1
                // Горбинка: в центре t=0 дуга выгибается вверх
                float hump = (1.0f - t * t) * humpH;
                int px = (int)(ex + t * halfW);
                int py = (int)(ey - hump);
                s.fillCircle(px, py, r, col);
            }
        }

        // Мягкий световой блик по верхушке горбинки для объема
        if (curH >= 10.0f && curW >= 24.0f) {
            for (int i = 3; i <= steps - 3; i++) {
                float t = -1.0f + ((float)i / (float)steps) * 2.0f;
                float hump = (1.0f - t * t) * humpH;
                int px = (int)(ex + t * halfW);
                int py = (int)(ey - hump - strokeR * 0.35f);
                s.fillCircle(px, py, 1, TFT_WHITE);
            }
        }
    }

    // Рот: аккуратная жирная точка, расплывающаяся в милую улыбку
    // morph: 0.0 = жирная точка-бусинка, 1.0 = расплывшаяся улыбка
    void drawCuteDotSmileMouth(TFT_eSprite &s, float mx, float my, float morph, float shakeX, float shakeY) {
        float px = mx + shakeX;
        float py = my + shakeY;

        // Ширина плавно растет от 6px (точка) до 21px (улыбка)
        float curW = 6.0f + morph * 15.0f;
        float halfW = curW * 0.5f;
        // Изгиб дуги углубляется при растяжении
        float arch = morph * 4.2f;
        float dotR = 2.8f - morph * 0.5f;

        const int steps = 14;
        for (int layer = 0; layer < 2; layer++) {
            uint16_t col = (layer == 0) ? colorGlow1 : colorEye;
            int r = (int)dotR + (layer == 0 ? 2 : 0);

            for (int i = 0; i <= steps; i++) {
                float t = -1.0f + ((float)i / (float)steps) * 2.0f;
                int curX = (int)(px + t * halfW);
                int curY = (int)(py + (1.0f - t * t) * arch);
                s.fillCircle(curX, curY, r, col);
            }
        }
    }

    // Капля слёзки от смеха
    void drawTearDrop(TFT_eSprite &s, float tx, float ty, float size) {
        int r = (int)(size * 0.5f);
        if (r < 2) r = 2;
        s.fillCircle((int)tx, (int)ty, r + 2, colorTearGlow);
        s.fillCircle((int)tx, (int)ty, r, colorTearCore);
        s.fillCircle((int)(tx - 1), (int)(ty - 1), 1, TFT_WHITE);
    }
} // anonymous namespace

void init(TFT_eSPI &tft) {
    colorEye         = tft.color565(50, 255, 100);
    colorGlow1       = tft.color565(10, 120, 30);
    colorGlow2       = tft.color565(0, 40, 10);

    colorTearCore    = tft.color565(60, 220, 255);
    colorTearGlow    = tft.color565(10, 70, 130);

    colorBlush       = tft.color565(255, 75, 120);
    colorBlushGlow   = tft.color565(80, 15, 35);

    colorSparkleCore = tft.color565(255, 230, 40);
    colorSparkleGlow = tft.color565(130, 90, 10);

    for (int i = 0; i < MAX_TEARS; i++) {
        tears[i].active = false;
    }
}

void reset() {
    startTime = millis();
    FaceState::loadGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);
    FaceState::setTargetPalette(50, 255, 100, 10, 120, 30, 0, 40, 10);

    t_cx = CENTER_X;
    t_cy = BASE_Y - 4.0f;
    t_spacing = eyeSpacing;
    t_lw = EYE_BASE_W;
    t_rw = EYE_BASE_W;
    t_lh = EYE_BASE_H;
    t_rh = EYE_BASE_H;
    t_mw = 20.0f;
    t_mh = 6.0f;

    for (int i = 0; i < MAX_TEARS; i++) {
        tears[i].active = false;
    }
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    uint32_t now = millis();

    colorEye   = FaceState::current.colorEye;
    colorGlow1 = FaceState::current.colorGlow1;
    colorGlow2 = FaceState::current.colorGlow2;

    // 1. Морфинг рта: жирная точка расплывается в улыбку и обратно
    float morphSine = sinf(now * 0.030f);
    float mouthMorph = (morphSine + 1.0f) * 0.5f;

    // 2. Пульсация горбатых глаз:
    // При смехе ширина чуть увеличивается, высота сжимается, а горбинка становится отчетливее
    float laughPulse = fabsf(sinf(now * 0.036f));
    float dynamicEyeW = EYE_BASE_W + (laughPulse * 5.0f);
    float dynamicEyeH = EYE_BASE_H - (laughPulse * 15.0f);
    float humpAmount  = 5.0f + (laughPulse * 3.5f);

    // Мелкое живое дрожание рта
    float mouthShakeX = sinf(now * 0.090f) * 1.4f;
    float mouthShakeY = cosf(now * 0.100f) * 1.2f;

    // Детское раскачивание и подпрыгивание
    float headRoll = sinf(now * 0.016f) * 3.2f;
    float laughHop = mouthMorph * 5.5f;

    // 3. Брызги слезок смеха при пике улыбки
    if (mouthMorph > 0.80f && (now - lastTearSpawn > 170)) {
        lastTearSpawn = now;
        for (int i = 0; i < MAX_TEARS; i++) {
            if (!tears[i].active) {
                tears[i].active = true;
                int side = (random(0, 2) == 0) ? -1 : 1;
                tears[i].x = cx + side * (eyeSpacing * 0.5f + dynamicEyeW * 0.40f);
                tears[i].y = cy - 4.0f + (float)random(-3, 4);
                tears[i].vx = side * ((float)random(16, 30) / 10.0f);
                tears[i].vy = -((float)random(16, 26) / 10.0f);
                tears[i].size = (float)random(4, 7);
                break;
            }
        }
    }

    // 4. Параллакс от наклона
    float imuX = ImuSensor::getParallaxX() * 0.35f;
    float imuY = ImuSensor::getParallaxY() * 0.25f;
    if (imuX < -14.0f) imuX = -14.0f;
    if (imuX >  14.0f) imuX =  14.0f;
    if (imuY <  -8.0f) imuY =  -8.0f;
    if (imuY >   8.0f) imuY =   8.0f;

    float targetX = t_cx + headRoll + imuX;
    float targetY = t_cy - laughHop + imuY;

    // 5. Пружинная физика
    float speed = 0.24f;
    cx += (targetX - cx) * speed;
    cy += (targetY - cy) * speed;
    currentSpacing += (t_spacing - currentSpacing) * speed;

    lw += (dynamicEyeW - lw) * speed;
    rw += (dynamicEyeW - rw) * speed;
    lh += (dynamicEyeH - lh) * speed;
    rh += (dynamicEyeH - rh) * speed;
    mw += (t_mw - mw) * speed;
    mh += (t_mh - mh) * speed;

    FaceState::saveGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);

    float lx = cx - currentSpacing * 0.5f;
    float rx = cx + currentSpacing * 0.5f;

    // 6. Отрисовка
    sprite.fillSprite(TFT_BLACK);

    // 6.1 Золотая искра веселья (✦) на правом виске/лбу
    drawAnimeSparkle(sprite, rx + 10.0f, cy - 40.0f, mouthMorph, colorSparkleCore, colorSparkleGlow);

    // 6.2 Румянец на щечках
    int blushY = (int)(cy + 20.0f);
    int leftBlushX = (int)(lx - 16.0f);
    int rightBlushX = (int)(rx - 2.0f);

    sprite.fillRoundRect(leftBlushX - 2, blushY - 1, 26, 13, 6, colorBlushGlow);
    sprite.fillRoundRect(rightBlushX - 2, blushY - 1, 26, 13, 6, colorBlushGlow);
    for (int i = 0; i < 3; i++) {
        int bxL = leftBlushX + (i * 8);
        int bxR = rightBlushX + (i * 8);
        for (int w = 0; w < 2; w++) {
            sprite.drawLine(bxL + w, blushY + 8, bxL + 5 + w, blushY + 1, colorBlush);
            sprite.drawLine(bxR + w, blushY + 8, bxR + 5 + w, blushY + 1, colorBlush);
        }
    }

    // 6.3 Горбатые глаза-капсулы (сплющиваются и пружинят)
    drawHumpCapsuleEye(sprite, lx, cy - 2.0f, lw, lh, humpAmount);
    drawHumpCapsuleEye(sprite, rx, cy - 2.0f, rw, rh, humpAmount);

    // 6.4 Естественный ротик: точка -> улыбка -> точка
    drawCuteDotSmileMouth(sprite, cx, cy + 38.0f, mouthMorph, mouthShakeX, mouthShakeY);

    // 6.5 Слезинки от смеха
    for (int i = 0; i < MAX_TEARS; i++) {
        if (tears[i].active) {
            tears[i].x += tears[i].vx;
            tears[i].y += tears[i].vy;
            tears[i].vy += 0.26f;
            tears[i].size -= 0.09f;

            if (tears[i].size <= 1.5f || tears[i].y > 192.0f) {
                tears[i].active = false;
            } else {
                drawTearDrop(sprite, tears[i].x, tears[i].y, tears[i].size);
            }
        }
    }

    sprite.pushSprite(0, 0);
}

} // namespace AnimLaugh
