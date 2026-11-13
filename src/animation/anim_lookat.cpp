#include "anim_lookat.h"
#include "face_state.h"
#include "sensors/imu_sensor.h"
#include <Arduino.h>
#include <math.h>

namespace AnimLookAt {

namespace {
    uint16_t colorEye = 0, colorGlow1 = 0, colorGlow2 = 0;

    const float EYE_BASE_W = 48.0f;
    const float EYE_BASE_H = 60.0f;
    const float eyeSpacing = 104.0f;
    const float CENTER_X   = 120.0f;
    const float BASE_Y     = 100.0f;
    const float PI_F       = 3.14159265f;

    // --- Настройки выразительности удивления ---
    const float    LOOK_SHIFT_X = 26.0f;   // смещение взгляда к точке удара по X
    const float    LOOK_SHIFT_Y = 16.0f;   // смещение взгляда к точке удара по Y
    const float    STARTLE_W    = 56.0f;   // распахнутые глаза: ширина (было 50, базовый 48)
    const float    STARTLE_H    = 74.0f;   // распахнутые глаза: высота (было 66, базовый 60)
    const float    STARTLE_MW   = 18.0f;   // удивлённый открытый рот «:O» (ширина)
    const float    STARTLE_MH   = 20.0f;   // удивлённый открытый рот «:O» (высота)

    // --- Тайминги фаз удивления ---
    const uint32_t BOUNCE_MS    = 240;     // упругий подскок в момент удара
    const uint32_t WIDE_MS      = 520;     // длительность распахнутых глаз и открытого рта
    const uint32_t HOLD_MS      = 950;     // удержание взгляда в сторону удара
    const uint32_t TOTAL_MS     = 1350;    // полное время анимации до возврата в IDLE

    const float    SNAP_SPEED   = 0.44f;   // резкая реакция испуга в начале
    const float    RETURN_SPEED = 0.12f;   // мягкий возврат в нейтраль

    float cx = CENTER_X, cy = BASE_Y;
    float currentSpacing = eyeSpacing;
    float lw = EYE_BASE_W, rw = EYE_BASE_W;
    float lh = EYE_BASE_H, rh = EYE_BASE_H;
    float mw = 20.0f, mh = 6.0f;

    float t_cx = CENTER_X, t_cy = BASE_Y;
    float t_spacing = eyeSpacing;
    float t_lw = EYE_BASE_W, t_rw = EYE_BASE_W;
    float t_lh = EYE_BASE_H, t_rh = EYE_BASE_H;
    float t_mw = 20.0f, t_mh = 6.0f;

    float    dirX = 0.0f, dirY = 0.0f;
    float    lookX = 0.0f, lookY = 0.0f;
    uint32_t startTime = 0;
    bool     finished  = false;

    float clamp1(float v) {
        if (v < -1.0f) return -1.0f;
        if (v >  1.0f) return  1.0f;
        return v;
    }

    void drawCapsule(TFT_eSprite &sprite, float x, float y, float w, float h) {
        int width  = (int)(w + 0.5f);
        int height = (int)(h + 0.5f);
        if (width < 1) width = 1;
        if (height < 1) height = 1;
        int left = (int)(x - width * 0.5f + 0.5f);
        int top  = (int)(y - height * 0.5f + 0.5f);
        int r = (width < height ? width : height) / 2;

        sprite.fillRoundRect(left - 4, top - 4, width + 8, height + 8, r + 4, colorGlow2);
        sprite.fillRoundRect(left - 2, top - 2, width + 4, height + 4, r + 2, colorGlow1);
        sprite.fillRoundRect(left, top, width, height, r, colorEye);
    }
} // anonymous namespace

void init(TFT_eSPI &tft) {
    (void)tft;
}

void setTarget(float dx, float dy) {
    dirX = clamp1(dx);
    dirY = clamp1(dy);
}

void reset() {
    startTime = millis();
    finished  = false;

    FaceState::loadGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);
    FaceState::setTargetPalette(50, 255, 100, 10, 120, 30, 0, 40, 10);

    lookX = dirX * LOOK_SHIFT_X;
    lookY = -dirY * LOOK_SHIFT_Y;   // удар сверху -> смотрим вверх (cy меньше)
    if (fabsf(dirX) < 0.05f && fabsf(dirY) < 0.05f) {
        lookY = -9.0f;              // стук спереди/сзади: удивлённый взгляд чуть вверх
    }

    // Мгновенный бросок целевой геометрии в состояние сильного удивления
    t_cx = CENTER_X + lookX;
    t_cy = BASE_Y + lookY;
    t_spacing = eyeSpacing;
    t_lw = STARTLE_W;
    t_rw = STARTLE_W;
    t_lh = STARTLE_H;
    t_rh = STARTLE_H;
    t_mw = STARTLE_MW;
    t_mh = STARTLE_MH;
}

bool isFinished() {
    return finished;
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    (void)tft;
    colorEye   = FaceState::current.colorEye;
    colorGlow1 = FaceState::current.colorGlow1;
    colorGlow2 = FaceState::current.colorGlow2;

    uint32_t t = millis() - startTime;

    // Параллакс от лёгких движений руки
    float imuX = 0.0f, imuY = 0.0f;
    if (ImuSensor::isReady()) {
        imuX = ImuSensor::getParallaxX() * 0.35f;
        imuY = ImuSensor::getParallaxY() * 0.25f;
    }
    if (imuX < -14.0f) imuX = -14.0f;
    if (imuX >  14.0f) imuX =  14.0f;
    if (imuY <  -8.0f) imuY =  -8.0f;
    if (imuY >   8.0f) imuY =   8.0f;

    // Фаза 1: испуг прошёл, глаза возвращаются к обычной форме, рот полуоткрыт
    if (t >= WIDE_MS && t < HOLD_MS) {
        t_lw = EYE_BASE_W;
        t_rw = EYE_BASE_W;
        t_lh = EYE_BASE_H;
        t_rh = EYE_BASE_H;
        t_mw = 18.0f;
        t_mh = 10.0f;
    }

    // Фаза 2: взгляд и рот плавно центрируются обратно в спокойное состояние
    float speed = SNAP_SPEED;
    if (t >= HOLD_MS) {
        t_cx = CENTER_X;
        t_cy = BASE_Y;
        t_lw = EYE_BASE_W;
        t_rw = EYE_BASE_W;
        t_lh = EYE_BASE_H;
        t_rh = EYE_BASE_H;
        t_mw = 20.0f;
        t_mh = 6.0f;
        speed = RETURN_SPEED;
    }

    // Упругий вертикальный подскок мордочки от удара
    float bounceY = 0.0f;
    if (t < BOUNCE_MS) {
        bounceY = -5.0f * sinf(PI_F * (float)t / (float)BOUNCE_MS);
    }

    // Плавное следование к целевым координатам
    cx += (t_cx + imuX - cx) * speed;
    cy += (t_cy + imuY + bounceY - cy) * speed;
    currentSpacing += (t_spacing - currentSpacing) * speed;
    lw += (t_lw - lw) * speed;
    rw += (t_rw - rw) * speed;
    lh += (t_lh - lh) * speed;
    rh += (t_rh - rh) * speed;
    mw += (t_mw - mw) * speed;
    mh += (t_mh - mh) * speed;

    FaceState::saveGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);

    float lx = cx - currentSpacing * 0.5f;
    float rx = cx + currentSpacing * 0.5f;

    // Отрисовка в буфер
    sprite.fillSprite(TFT_BLACK);
    drawCapsule(sprite, lx, cy, lw, lh);
    drawCapsule(sprite, rx, cy, rw, rh);
    drawCapsule(sprite, cx, cy + 48.0f, mw, mh);

    if (t >= TOTAL_MS) {
        finished = true;
    }

    sprite.pushSprite(0, 0);
}

} // namespace AnimLookAt
