#include "anim_wind_easy.h"
#include "face_state.h"
#include <math.h>

namespace AnimWindEasy {
namespace {

// --- Базовые константы геометрии ---
const float CENTER_X = 120.0f;
const float BASE_Y = 135.0f;

// --- Цветовая палитра (генерируется в init) ---
uint16_t colorEye       = 0;
uint16_t colorGlow1     = 0;
uint16_t colorGlow2     = 0;

uint16_t colorWindCore  = 0;
uint16_t colorWindGlow1 = 0;
uint16_t colorWindGlow2 = 0;

// --- Текущие переменные физики лица ---
float cx = CENTER_X, cy = BASE_Y;
float lx = 110.0f, rx = 210.0f;
float lw = 50.0f, rw = 50.0f;
float lh = 80.0f, rh = 80.0f;
float mw = 15.0f, mh = 4.0f;

float currentSpacing = 100.0f;
float browOffset     = 0.0f;
float browSlant      = 0.0f;
float eyeAsymY       = 0.0f;
float mouthWaveAmp   = 0.0f;
float mouthShiftX    = 0.0f;
float windIntensity  = 0.0f;

// --- Целевые переменные физики лица ---
float t_cx = CENTER_X, t_cy = BASE_Y;
float t_lw = 50.0f, t_rw = 50.0f;
float t_lh = 80.0f, t_rh = 80.0f;
float t_mw = 15.0f, t_mh = 4.0f;

float t_spacing       = 100.0f;
float t_browOffset    = 0.0f;
float t_browSlant     = 0.0f;
float t_eyeAsymY      = 0.0f;
float t_mouthWave     = 0.0f;
float t_mouthShiftX   = 0.0f;
float t_windIntensity = 0.0f;

// --- Тайминги и моргание ---
uint32_t animStartTime = 0;
uint32_t lastStepTime  = 0;
uint32_t nextBlinkTime = 0;
bool     isBlinking    = false;
uint32_t blinkStart    = 0;

// --- Частицы ветра ---
struct WindStreak {
    bool  active;
    float x, y;
    float vx, vy;
    float length;
    float wavePhase;
    bool  hasSwirl;
};

const int MAX_STREAKS = 7;
WindStreak streaks[MAX_STREAKS];

void spawnStreak(int idx, bool randomX) {
    streaks[idx].active    = true;
    streaks[idx].x         = randomX ? (float)random(-60, 260) : (float)random(-90, -20);
    streaks[idx].y         = (float)random(16, 154);
    float speed            = (float)random(115, 175) * 0.1f;
    streaks[idx].vx        = speed;
    streaks[idx].vy        = speed * 0.08f;
    streaks[idx].length    = (float)random(34, 68);
    streaks[idx].wavePhase = (float)random(0, 628) * 0.01f;
    streaks[idx].hasSwirl  = (random(0, 100) < 35);
}

void drawGlowingEye(TFT_eSprite &sprite, int x, int y, int w, int h, int r) {
    if (h < 4) h = 4;
    int x0 = x - w / 2;
    int y0 = y - h / 2;

    int r2 = r + 3;
    int r1 = r + 1;
    if (r2 > (h + 12) / 2) r2 = (h + 12) / 2;
    if (r1 > (h + 6) / 2)  r1 = (h + 6) / 2;
    if (r > h / 2)         r  = h / 2;

    sprite.fillRoundRect(x0 - 6, y0 - 6, w + 12, h + 12, r2, colorGlow2);
    sprite.fillRoundRect(x0 - 3, y0 - 3, w + 6,  h + 6,  r1, colorGlow1);
    sprite.fillRoundRect(x0,     y0,     w,      h,      r,  colorEye);
}

void drawWindSquintBrow(TFT_eSprite &sprite, float ex, float ey, float ew, float eh, bool isLeft, float slant, float offset) {
    float halfW = ew * 0.5f + 6.0f;
    float topY  = ey - eh * 0.5f + offset;

    float yLeft, yRight;
    if (isLeft) {
        yLeft  = topY + slant * 0.45f;
        yRight = topY - slant * 0.15f;
    } else {
        yLeft  = topY + slant * 0.20f;
        yRight = topY + slant * 0.05f;
    }

    float xLeft  = ex - halfW;
    float xRight = ex + halfW;

    if (yLeft < 14.0f)  yLeft  = 14.0f;
    if (yRight < 14.0f) yRight = 14.0f;

    int maskTop = (int)((yLeft < yRight ? yLeft : yRight) - 22.0f);
    if (maskTop < 0) maskTop = 0;
    sprite.fillTriangle((int)xLeft - 4, maskTop, (int)xRight + 4, maskTop, (int)xLeft - 4, (int)yLeft, TFT_BLACK);
    sprite.fillTriangle((int)xRight + 4, maskTop, (int)xRight + 4, (int)yRight, (int)xLeft - 4, (int)yLeft, TFT_BLACK);

    int steps = (int)((xRight - xLeft) / 1.4f);
    if (steps < 36) steps = 36;

    const int radii[3] = {5, 3, 2};
    const uint16_t cols[3] = {colorGlow2, colorGlow1, colorEye};

    for (int pass = 0; pass < 3; ++pass) {
        int r = radii[pass];
        uint16_t col = cols[pass];
        for (int i = 0; i <= steps; ++i) {
            float t  = (float)i / (float)steps;
            float bx = xLeft + (xRight - xLeft) * t;
            float gentleCurve = sinf(t * 3.14159f) * 1.0f;
            float by = yLeft + (yRight - yLeft) * t + gentleCurve;
            if (by < 14.0f) by = 14.0f;
            sprite.fillCircle((int)bx, (int)by, r, col);
        }
    }
}

void drawWindyMouth(TFT_eSprite &sprite, float mx, float my, float w, float h, float waveAmp, float timeSec) {
    int steps = (int)(w / 1.2f);
    if (steps < 24) steps = 24;

    const int radii[3] = {5, 3, 2};
    const uint16_t cols[3] = {colorGlow2, colorGlow1, colorEye};

    float halfW = w * 0.5f;

    for (int pass = 0; pass < 3; ++pass) {
        int r = radii[pass];
        uint16_t col = cols[pass];
        for (int i = 0; i <= steps; ++i) {
            float t = (float)i / (float)steps;
            float px = mx - halfW + w * t;

            float mildPout = -sinf(t * 3.14159f) * (h * 0.35f);
            float windSkew = (t - 0.5f) * -2.2f;
            float flutter  = sinf(t * 7.5f - timeSec * 28.0f) * waveAmp;

            float py = my + mildPout + windSkew + flutter;
            sprite.fillCircle((int)px, (int)py, r, col);
        }
    }
}

void drawWindParticles(TFT_eSprite &sprite, float timeSec, float intensity) {
    if (intensity < 0.08f) return;

    const int radii[3] = {5, 3, 2};
    const uint16_t cols[3] = {colorWindGlow2, colorWindGlow1, colorWindCore};

    for (int pass = 0; pass < 3; ++pass) {
        int r = radii[pass];
        uint16_t col = cols[pass];

        for (int i = 0; i < MAX_STREAKS; ++i) {
            if (!streaks[i].active) continue;

            float sx = streaks[i].x;
            float sy = streaks[i].y;
            float len = streaks[i].length * (0.5f + 0.5f * intensity);

            if (sx + len < -10.0f || sx - 20.0f > 330.0f) continue;

            int segs = (int)(len / 1.5f);
            if (segs < 14) segs = 14;

            for (int s = 0; s <= segs; ++s) {
                float t = (float)s / (float)segs;
                float px = sx + t * len;
                float py = sy + t * (len * 0.08f) + sinf(t * 5.0f + streaks[i].wavePhase + timeSec * 12.0f) * 2.8f;

                if (px >= -6.0f && px <= 326.0f && py >= 6.0f && py <= 164.0f) {
                    sprite.fillCircle((int)px, (int)py, r, col);
                }
            }

            if (streaks[i].hasSwirl && intensity > 0.45f) {
                float headX = sx + len;
                float headY = sy + len * 0.08f - 7.0f;
                if (headX >= -10.0f && headX <= 330.0f) {
                    float maxAngle = 4.4f;
                    float a = 0.0f;
                    while (a < maxAngle) {
                        float sr = 7.5f * (1.0f - a / 5.8f);
                        float wx = headX + cosf(a + 1.57f) * sr;
                        float wy = headY + sinf(a + 1.57f) * sr;
                        sprite.fillCircle((int)wx, (int)wy, r, col);

                        float step = (sr < 4.0f) ? 0.20f : (1.2f / sr);
                        a += step;
                    }
                }
            }
        }
    }
}

} // anonymous namespace

void init(TFT_eSPI &tft) {
    colorEye   = tft.color565(50, 255, 100);
    colorGlow1 = tft.color565(10, 120, 30);
    colorGlow2 = tft.color565(0, 40, 10);

    colorWindCore  = tft.color565(210, 255, 255);
    colorWindGlow1 = tft.color565(35, 145, 175);
    colorWindGlow2 = tft.color565(5, 40, 55);

    reset();
}

void reset() {
    uint32_t now = millis();
    animStartTime = now;
    lastStepTime  = now;
    nextBlinkTime = now + random(700, 1400);
    isBlinking    = false;

    // 1. Подхватываем текущую геометрию из предыдущей эмоции
    FaceState::loadGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);

    // 2. Задаем целевую неоново-зеленую палитру
    FaceState::setTargetPalette(50, 255, 100, 10, 120, 30, 0, 40, 10);

    browOffset     = -14.0f; // Брови плавно опускаются сверху
    browSlant      = 0.0f;
    eyeAsymY       = 0.0f;
    mouthWaveAmp   = 0.0f;
    mouthShiftX    = 0.0f;
    windIntensity  = 0.0f;

    for (int i = 0; i < MAX_STREAKS; ++i) {
        spawnStreak(i, true);
    }
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    uint32_t now = millis();
    uint32_t elapsed = now - animStartTime;
    float timeSec = (float)elapsed * 0.001f;

    colorEye   = FaceState::current.colorEye;
    colorGlow1 = FaceState::current.colorGlow1;
    colorGlow2 = FaceState::current.colorGlow2;

    uint32_t cycleMs = elapsed % 4200;
    float gustWave   = sinf(timeSec * 8.0f);
    float buffeting  = sinf(timeSec * 24.0f);

    if (cycleMs < 1500) {
        t_windIntensity = 1.0f;

        t_cx = CENTER_X + 22.0f + gustWave * 4.0f + buffeting * 1.4f;
        t_cy = BASE_Y + 4.0f + cosf(timeSec * 6.0f) * 2.0f;
        t_spacing = 94.0f;

        t_lw = 52.0f;
        t_rw = 50.0f;
        t_lh = 23.0f + gustWave * 2.5f;
        t_rh = 34.0f + gustWave * 3.0f;

        t_eyeAsymY   = 3.0f;
        t_browOffset = 2.0f;
        t_browSlant  = 5.0f + gustWave * 1.5f;

        t_mw          = 21.0f;
        t_mh          = 3.5f;
        t_mouthWave   = 2.0f;
        t_mouthShiftX = 11.0f;

    } else if (cycleMs < 3000) {
        t_windIntensity = 0.75f;

        t_cx = CENTER_X + 15.0f + gustWave * 3.0f + buffeting * 1.0f;
        t_cy = BASE_Y + 3.0f + sinf(timeSec * 5.0f) * 1.8f;
        t_spacing = 96.0f;

        t_lw = 50.0f;
        t_rw = 50.0f;
        t_lh = 30.0f + gustWave * 2.5f;
        t_rh = 42.0f + gustWave * 3.0f;

        t_eyeAsymY   = 2.0f;
        t_browOffset = 1.5f;
        t_browSlant  = 3.5f;

        t_mw          = 19.0f;
        t_mh          = 3.0f;
        t_mouthWave   = 1.3f;
        t_mouthShiftX = 8.0f;

    } else {
        t_windIntensity = 0.12f;

        float shake = 0.0f;
        if (cycleMs > 3250 && cycleMs < 3850) {
            float env = sinf(((float)(cycleMs - 3250) / 600.0f) * 3.14159f);
            shake = sinf(timeSec * 40.0f) * 8.0f * env;
        }

        t_cx = CENTER_X + shake;
        t_cy = BASE_Y;
        t_spacing = 100.0f;

        t_lw = 50.0f;
        t_rw = 50.0f;
        t_lh = 52.0f;
        t_rh = 54.0f;

        t_eyeAsymY   = 0.0f;
        t_browOffset = 0.5f;
        t_browSlant  = 1.5f;

        t_mw          = 17.0f;
        t_mh          = 2.0f;
        t_mouthWave   = 0.2f;
        t_mouthShiftX = 1.0f;
    }

    if (!isBlinking && now >= nextBlinkTime) {
        isBlinking = true;
        blinkStart = now;
    }
    if (isBlinking) {
        uint32_t bDur = now - blinkStart;
        if (bDur < 110) {
            t_lh = 6.0f;
            t_rh = 6.0f;
        } else {
            isBlinking = false;
            nextBlinkTime = now + (t_windIntensity > 0.5f ? random(450, 950) : random(900, 1600));
        }
    }

    // --- Пружинная физика ---
    const float smooth = 0.22f;
    cx += (t_cx - cx) * smooth;
    cy += (t_cy - cy) * smooth;
    lw += (t_lw - lw) * smooth;
    rw += (t_rw - rw) * smooth;
    lh += (t_lh - lh) * (isBlinking ? 0.45f : smooth);
    rh += (t_rh - rh) * (isBlinking ? 0.45f : smooth);
    mw += (t_mw - mw) * smooth;
    mh += (t_mh - mh) * smooth;

    currentSpacing += (t_spacing - currentSpacing) * smooth;
    browOffset     += (t_browOffset - browOffset) * smooth;
    browSlant      += (t_browSlant - browSlant) * smooth;
    eyeAsymY       += (t_eyeAsymY - eyeAsymY) * smooth;
    mouthWaveAmp   += (t_mouthWave - mouthWaveAmp) * smooth;
    mouthShiftX    += (t_mouthShiftX - mouthShiftX) * smooth;
    windIntensity  += (t_windIntensity - windIntensity) * 0.15f;

    // Сохраняем актуальную геометрию в FaceState
    FaceState::saveGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);

    lx = cx - currentSpacing * 0.5f;
    rx = cx + currentSpacing * 0.5f;

    float ly = cy + eyeAsymY;
    float ry = cy - eyeAsymY * 0.5f;

    float speedMul = 0.35f + 0.85f * windIntensity;
    for (int i = 0; i < MAX_STREAKS; ++i) {
        if (!streaks[i].active) continue;
        streaks[i].x += streaks[i].vx * speedMul;
        streaks[i].y += streaks[i].vy * speedMul;

        if (streaks[i].x > 340.0f) {
            spawnStreak(i, false);
        }
    }

    sprite.fillSprite(TFT_BLACK);

    drawGlowingEye(sprite, (int)lx, (int)ly, (int)lw, (int)lh, 15);
    drawGlowingEye(sprite, (int)rx, (int)ry, (int)rw, (int)rh, 15);

    drawWindSquintBrow(sprite, lx, ly, lw, lh, true,  browSlant, browOffset);
    drawWindSquintBrow(sprite, rx, ry, rw, rh, false, browSlant, browOffset);

    float mouthX = cx + mouthShiftX;
    float mouthY = cy + 43.0f;
    drawWindyMouth(sprite, mouthX, mouthY, mw, mh, mouthWaveAmp, timeSec);

    drawWindParticles(sprite, timeSec, windIntensity);

    sprite.pushSprite(0, 0);
}

} // namespace AnimWindEasy
