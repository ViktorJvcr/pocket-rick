#include "anim_reboot.h"
#include "face_state.h"
#include <Arduino.h>
#include <stdio.h>
#include <math.h>

// ============================================================================
//  AnimReboot — «Reboot Wink» (зрелищная версия)
//
//  Сюжет:
//    1) глаза-капсулы со свечением (ровно как в IDLE) быстро схлопываются
//       в светящуюся полоску и питание «вырубается»;
//    2) короткая темнота с помехами;
//    3) консольная загрузка: строки POST, надпись Loading с бегущими точками
//       и растущая неоновая полоса прогресса с процентами;
//    4) глаза «включаются» снизу вверх с пружинным перехлёстом, летят искры,
//       расходится импульс-волна и проходит белый скан;
//    5) мягкая упругая посадка, появляется рот.
//
//  Цвет — системный ледяной (110,205,255), отличается от зелёного IDLE.
//  Геометрия капсул идентична IDLE: W50 H80 R25, шаг 110, центр (120,100).
//
//  ВАЖНО: полная длительность ~3400 мс. Поставьте в src/main.cpp
//  const uint32_t REBOOT_MS = 3600;   // было 1700
// ============================================================================

namespace AnimReboot {

namespace {

// ------------------------------------------------------------------ Геометрия
constexpr float CENTER_X = 120.0f;
constexpr float BASE_Y   = 100.0f;

constexpr float EYE_W    = 50.0f;    // как eyeBaseW в IDLE
constexpr float EYE_H    = 80.0f;    // как eyeBaseH в IDLE
constexpr int   EYE_RAD  = 25;       // как eyeRadius в IDLE
constexpr float EYE_SP   = 110.0f;   // как eyeSpacing в IDLE

constexpr float MOUTH_W  = 15.0f;    // как рот в IDLE
constexpr float MOUTH_H  = 4.0f;
constexpr float MOUTH_DY = 42.0f;

// ------------------------------------------------------------------ Тайминги
constexpr uint32_t FRAME_MS    = 16;    // ~60 FPS
constexpr uint32_t T_COLLAPSE  = 210;   // схлопывание капсул в полоску
constexpr uint32_t T_TERM      = 330;   // старт консольной загрузки
constexpr uint32_t T_LOADING   = 1030;  // появляются Loading, точки, полоса
constexpr uint32_t T_KILL      = 2180;  // терминал гаснет
constexpr uint32_t T_EYES      = 2260;  // начинают включаться глаза
constexpr uint32_t T_SETTLE    = 2760;  // глаза открыты, идёт посадка
constexpr uint32_t T_END       = 3400;  // конец (REBOOT_MS лучше 3600)

constexpr uint32_t EYE_GROW_MS = 430;   // раскрытие одного глаза
constexpr uint32_t EYE_SKEW_MS = 110;   // правый глаз включается позже

// ------------------------------------------------------------------ Цвета
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (uint16_t)(b >> 3));
}

constexpr uint16_t C_BG     = rgb565(4, 6, 16);       // фон с лёгкой синевой
constexpr uint16_t C_GRID   = rgb565(12, 24, 48);     // сетка терминала
constexpr uint16_t C_TEXT   = rgb565(150, 215, 255);  // строки загрузки
constexpr uint16_t C_ACCENT = rgb565(120, 180, 255);  // акценты
constexpr uint16_t C_WHITE  = rgb565(255, 255, 255);

// ------------------------------------------------------------------ Утилиты
inline int i_round(float v) { return (int)(v < 0.0f ? v - 0.5f : v + 0.5f); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

uint16_t lerp565(uint16_t a, uint16_t b, uint8_t t) {
    if (t == 0)   return a;
    if (t == 255) return b;
    const int32_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    const int32_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    const int32_t r = ar + (((br - ar) * t) / 255);
    const int32_t g = ag + (((bg - ag) * t) / 255);
    const int32_t c = ab + (((bb - ab) * t) / 255);
    return (uint16_t)((r << 11) | (g << 5) | c);
}

// Яркость: k = 1.0 — цвет как есть, k = 0.0 — полностью уходит в фон
inline uint16_t fade565(uint16_t col, float k) {
    return lerp565(C_BG, col, (uint8_t)(clampf(k, 0.0f, 1.0f) * 255.0f));
}

inline float easeOutCubic(float t) {
    const float u = 1.0f - clampf(t, 0.0f, 1.0f);
    return 1.0f - u * u * u;
}

inline float easeOutBack(float t) {
    t = clampf(t, 0.0f, 1.0f);
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    const float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

// Псевдослучайность без heap и без вмешательства в глобальный random()
uint32_t rngState = 0x9E3779B9u;
inline uint32_t rnd() {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}
inline float rndf(float a, float b) { return a + (b - a) * ((rnd() & 0xFFFF) / 65535.0f); }

// ------------------------------------------------------------------ Состояние
uint32_t startTime   = 0;
uint32_t lastFrame   = 0;
uint32_t lastSparkMs = 0;

// Геометрия, из которой стартуем (для бесшовного входа из предыдущей эмоции)
float seedCx = CENTER_X, seedCy = BASE_Y, seedSp = EYE_SP;
float seedLw = EYE_W, seedLh = EYE_H, seedMh = MOUTH_H;

constexpr int MAX_SPARKS = 12;
struct Spark {
    bool     active;
    float    x, y, vx, vy;
    float    life;
    float    size;
    uint16_t col;
};
Spark sparks[MAX_SPARKS];

constexpr int BOOT_LINES = 6;
const char* const bootLines[BOOT_LINES] = {
    "RICK.OS  BOOT",
    "CORE     OK",
    "SENSORS  OK",
    "OPTICS   OK",
    "NEON     OK",
    "REBOOTING"
};

// ------------------------------------------------------------------ Капсула глаза
// Три слоя ровно как в IDLE: внешнее свечение, среднее свечение, ядро.
void drawCapsule(TFT_eSprite &s, float cxx, float cyy, float w, float h,
                 int rad, uint16_t core, uint16_t g1, uint16_t g2) {
    const int x  = i_round(cxx - w * 0.5f);
    const int y  = i_round(cyy - h * 0.5f);
    int ww = i_round(w); if (ww < 3) ww = 3;
    int hh = i_round(h); if (hh < 3) hh = 3;
    int r = rad;
    if (r > ww / 2) r = ww / 2;
    if (r > hh / 2) r = hh / 2;
    s.fillRoundRect(x - 6, y - 6, ww + 12, hh + 12, r + 4, g2);
    s.fillRoundRect(x - 3, y - 3, ww + 6,  hh + 6,  r + 2, g1);
    s.fillRoundRect(x,     y,     ww,     hh,      r,     core);
}

// ------------------------------------------------------------------ Искры
void spawnSpark(float cxx, float cyy) {
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (sparks[i].active) continue;
        Spark &sp = sparks[i];
        const float ang = rndf(0.0f, 6.28318f);
        const float spd = rndf(18.0f, 46.0f);
        sp.active = true;
        sp.x    = cxx + cosf(ang) * rndf(24.0f, 46.0f);
        sp.y    = cyy + sinf(ang) * rndf(18.0f, 34.0f);
        sp.vx   = cosf(ang) * spd;
        sp.vy   = sinf(ang) * spd - 6.0f;
        sp.life = 1.0f;
        sp.size = rndf(2.0f, 4.0f);
        sp.col  = ((rnd() & 3u) == 0u) ? C_WHITE : C_ACCENT;
        return;
    }
}

void updateSparks(float dt) {
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (!sparks[i].active) continue;
        Spark &sp = sparks[i];
        sp.x    += sp.vx * dt;
        sp.y    += sp.vy * dt;
        sp.vy   += 26.0f * dt;
        sp.life -= dt * 2.0f;
        if (sp.life <= 0.0f) sparks[i].active = false;
    }
}

void drawSparks(TFT_eSprite &s) {
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (!sparks[i].active) continue;
        const Spark &sp = sparks[i];
        const uint16_t col = fade565(sp.col, sp.life);
        const int x = i_round(sp.x);
        const int y = i_round(sp.y);
        const int r = (int)sp.size;
        s.drawFastHLine(x - r, y, r * 2 + 1, col);
        s.drawFastVLine(x, y - r, r * 2 + 1, col);
    }
}

// ------------------------------------------------------------------ Фон терминала
void drawTerminalBg(TFT_eSprite &s, uint32_t dt, float k) {
    const uint16_t grid = fade565(C_GRID, k);
    const int off = (int)((dt / 40u) % 30u);
    for (int y = -10 + off; y < 200; y += 30) {
        if (y >= 0) s.drawFastHLine(0, y, 240, grid);
    }
    for (int x = 0; x < 240; x += 30) s.drawFastVLine(x, 16, 168, grid);
    // бегущая строка развёртки
    const int sy = 16 + (int)((dt / 12u) % 168u);
    s.drawFastHLine(0, sy, 240, fade565(C_ACCENT, 0.18f * k));
}

} // anonymous namespace

// ============================================================================
void init(TFT_eSPI &tft) {
    (void)tft;
    for (int i = 0; i < MAX_SPARKS; i++) sparks[i].active = false;
    seedCx = CENTER_X; seedCy = BASE_Y; seedSp = EYE_SP;
    seedLw = EYE_W;    seedLh = EYE_H;  seedMh = MOUTH_H;
}

void reset() {
    const uint32_t now = millis();
    startTime   = now;
    lastFrame   = now - FRAME_MS;
    lastSparkMs = 0;
    rngState   ^= (uint32_t)micros() | 1u;

    // Бесшовный вход: подхватываем текущую геометрию лица
    float cx, cy, sp, lw, rw, lh, rh, mw, mh;
    FaceState::loadGeometry(cx, cy, sp, lw, rw, lh, rh, mw, mh);
    seedCx = cx;
    seedCy = cy;
    seedSp = clampf(sp, 60.0f, 160.0f);
    seedLw = clampf((lw + rw) * 0.5f, 18.0f, 90.0f);
    seedLh = clampf((lh + rh) * 0.5f, 18.0f, 100.0f);
    seedMh = clampf(mh, 0.0f, 30.0f);

    // Системная ледяная палитра — заметно отличается от зелёного IDLE
    FaceState::setTargetPalette(110, 205, 255,   // ядро глаз
                                40,  90,  230,   // среднее свечение
                                8,   18,  70);   // внешнее свечение

    for (int i = 0; i < MAX_SPARKS; i++) sparks[i].active = false;
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    (void)tft;
    const uint32_t now = millis();
    if (now - lastFrame < FRAME_MS) return;             // ограничение ~60 FPS
    const float    dtSec = (now - lastFrame) * 0.001f;  // шаг физики искр
    lastFrame = now;

    const uint32_t dt = now - startTime;

    const uint16_t cEye   = FaceState::current.colorEye;
    const uint16_t cGlow1 = FaceState::current.colorGlow1;
    const uint16_t cGlow2 = FaceState::current.colorGlow2;

    sprite.fillSprite(C_BG);

    // ------------------------------------------- 1. Питание выключается
    if (dt < T_COLLAPSE) {
        const float t = easeOutCubic((float)dt / (float)T_COLLAPSE);
        const float h = lerpf(seedLh, 3.0f, t);
        const float w = lerpf(seedLw, seedLw * 0.88f, t);
        const float bottom = seedCy + seedLh * 0.5f;
        const float cy = bottom - h * 0.5f;             // капсула оседает вниз
        const float k  = 1.0f - 0.7f * t;               // гаснет к концу

        drawCapsule(sprite, seedCx - seedSp * 0.5f, cy, w, h, EYE_RAD,
                    fade565(cEye, k), fade565(cGlow1, k), fade565(cGlow2, k));
        drawCapsule(sprite, seedCx + seedSp * 0.5f, cy, w, h, EYE_RAD,
                    fade565(cEye, k), fade565(cGlow1, k), fade565(cGlow2, k));

        if (seedMh > 0.5f && t < 0.9f) {                // рот тоже гаснет
            const float mh = seedMh * (1.0f - t);
            sprite.fillRoundRect(i_round(seedCx - MOUTH_W * 0.5f),
                                 i_round(seedCy + MOUTH_DY),
                                 (int)MOUTH_W, (int)mh, 2, fade565(cEye, k));
        }

        if (dt > T_COLLAPSE - 70) {                     // помеха на выходе питания
            sprite.fillRect(0, 40 + (int)(rnd() % 120u), 240, 2, fade565(C_WHITE, 0.7f));
        }

    // ------------------------------------------- 2. Темнота
    } else if (dt < T_TERM) {
        const uint32_t d = dt - T_COLLAPSE;
        if (d < 30 || (d > 60 && d < 80)) {
            sprite.fillRect(0, 70 + (int)(d % 60u), 240, 3, fade565(C_WHITE, 0.55f));
        }

    // ------------------------------------------- 3. Консольная загрузка
    } else if (dt < T_KILL) {
        drawTerminalBg(sprite, dt, clampf((float)(dt - T_TERM) / 220.0f, 0.0f, 1.0f));

        sprite.setTextFont(2);
        sprite.setTextDatum(TL_DATUM);

        const uint32_t lineStep = 108;
        for (int i = 0; i < BOOT_LINES; i++) {
            const uint32_t at = T_TERM + 70u + (uint32_t)i * lineStep;
            if (dt < at) break;
            const float a = clampf((float)(dt - at) / 90.0f, 0.0f, 1.0f);
            sprite.setTextColor((i == BOOT_LINES - 1) ? fade565(C_ACCENT, a)
                                                      : fade565(C_TEXT, a));
            sprite.drawString(bootLines[i], 44, 34 + i * 16);
        }

        const int nextLine = (int)((dt - T_TERM) / lineStep);   // мигающий курсор
        if (nextLine < BOOT_LINES && ((dt / 220u) % 2u) == 0u) {
            sprite.fillRect(44, 34 + nextLine * 16 + 12, 8, 2, fade565(C_TEXT, 0.8f));
        }

        if (dt >= T_LOADING) {
            const float a = clampf((float)(dt - T_LOADING) / 150.0f, 0.0f, 1.0f);

            sprite.setTextDatum(MC_DATUM);
            sprite.setTextColor(fade565(C_TEXT, a));
            sprite.drawString("Loading", 108, 150);

            // бегущие точки: одна яркая, две притушены
            const int phase = (int)(((dt - T_LOADING) / 200u) % 3u);
            for (int i = 0; i < 3; i++) {
                const bool on = (i == phase);
                sprite.fillCircle(146 + i * 12, 155, on ? 3 : 2,
                                  on ? C_WHITE : fade565(C_ACCENT, 0.45f));
            }

            // полоса прогресса с яркой «головой» и процентами
            const float p = clampf((float)(dt - T_LOADING) / 1060.0f, 0.0f, 1.0f);
            const int bx = 50, bw = 140, by = 174, bh = 8;
            sprite.drawRoundRect(bx - 2, by - 2, bw + 4, bh + 4, 4, fade565(C_ACCENT, 0.55f));
            const int fill = (int)(bw * p);
            if (fill > 0) sprite.fillRoundRect(bx, by, fill, bh, 3, fade565(cEye, 0.9f));
            if (p < 1.0f) sprite.fillRect(bx + fill - 2, by - 3, 3, bh + 6, C_WHITE);

            char buf[8];
            snprintf(buf, sizeof(buf), "%d%%", (int)(p * 100.0f));
            sprite.setTextDatum(MR_DATUM);
            sprite.setTextColor(fade565(C_TEXT, 0.9f));
            sprite.drawString(buf, 234, by + bh / 2);
        }

    // ------------------------------------------- 4. Терминал гаснет
    } else if (dt < T_EYES) {
        const uint32_t d = dt - T_KILL;
        if (d < 25 || (d > 45 && d < 62)) {
            sprite.fillRect(0, 60,  240, 4, fade565(C_WHITE, 0.6f));
            sprite.fillRect(0, 140, 240, 3, fade565(C_WHITE, 0.35f));
        }

    // ------------------------------------------- 5. Глаза включаются
    } else {
        const float tL = clampf((float)(dt - T_EYES) / (float)EYE_GROW_MS, 0.0f, 1.0f);
        const float tR = clampf((float)(dt - T_EYES - EYE_SKEW_MS) / (float)EYE_GROW_MS, 0.0f, 1.0f);

        float hL = lerpf(4.0f,  EYE_H, easeOutBack(tL));
        float hR = lerpf(4.0f,  EYE_H, easeOutBack(tR));
        float wL = lerpf(36.0f, EYE_W, easeOutBack(tL));
        float wR = lerpf(36.0f, EYE_W, easeOutBack(tR));

        // упругая посадка после раскрытия
        const float settleT = (float)(dt - T_SETTLE) / 1000.0f;
        if (settleT > 0.0f) {
            const float wob = expf(-3.2f * settleT) * sinf(settleT * 22.0f) * 0.055f;
            hL *= (1.0f + wob);
            hR *= (1.0f + wob);
        }

        const float bottomY = BASE_Y + EYE_H * 0.5f;   // капсулы растут снизу вверх
        const float cyL = bottomY - hL * 0.5f;
        const float cyR = bottomY - hR * 0.5f;
        const int   exL = i_round(CENTER_X - EYE_SP * 0.5f);
        const int   exR = i_round(CENTER_X + EYE_SP * 0.5f);

        // волна включения вокруг всей «головы»
        const float pt = (float)(dt - T_EYES) / 420.0f;
        if (pt >= 0.0f && pt <= 1.0f) {
            const float g = easeOutCubic(pt);
            const int kk = i_round(6.0f + 34.0f * g);
            sprite.drawRoundRect(30 - kk / 3, 52 - kk / 3,
                                 180 + kk * 2 / 3, 96 + kk * 2 / 3,
                                 26 + kk / 4, fade565(C_ACCENT, (1.0f - pt) * 0.75f));
        }

        drawCapsule(sprite, exL, cyL, wL, hL, EYE_RAD, cEye, cGlow1, cGlow2);
        drawCapsule(sprite, exR, cyR, wR, hR, EYE_RAD, cEye, cGlow1, cGlow2);

        // вспышка-импульс в момент полного раскрытия
        const float ft = (float)(dt - (T_EYES + EYE_GROW_MS)) / 220.0f;
        if (ft >= 0.0f && ft <= 1.0f) {
            const int kk = i_round(6.0f + 26.0f * easeOutCubic(ft));
            const uint16_t col = fade565(C_WHITE, (1.0f - ft) * 0.6f);
            int w1 = i_round(wL) + kk * 2;
            int h1 = i_round(hL) + kk * 2;
            int r1 = EYE_RAD + kk; if (r1 > h1 / 2) r1 = h1 / 2; if (r1 > w1 / 2) r1 = w1 / 2;
            sprite.drawRoundRect(i_round(CENTER_X - EYE_SP * 0.5f) - i_round(wL * 0.5f) - kk,
                                 i_round(cyL) - i_round(hL * 0.5f) - kk, w1, h1, r1, col);
            int w2 = i_round(wR) + kk * 2;
            int h2 = i_round(hR) + kk * 2;
            int r2 = EYE_RAD + kk; if (r2 > h2 / 2) r2 = h2 / 2; if (r2 > w2 / 2) r2 = w2 / 2;
            sprite.drawRoundRect(i_round(CENTER_X + EYE_SP * 0.5f) - i_round(wR * 0.5f) - kk,
                                 i_round(cyR) - i_round(hR * 0.5f) - kk, w2, h2, r2, col);
        }

        // искры при включении
        if (dt < T_SETTLE + 240u) {
            if (lastSparkMs == 0u || dt - lastSparkMs >= 85u) {
                lastSparkMs = dt;
                spawnSpark((float)exL, cyL);
                spawnSpark((float)exR, cyR);
            }
        }
        updateSparks(dtSec);
        drawSparks(sprite);

        // белый скан слева направо
        const float st = (float)(dt - T_SETTLE) / 260.0f;
        if (st >= 0.0f && st <= 1.0f) {
            const int sx = i_round(lerpf(34.0f, 206.0f, easeOutCubic(st)));
            sprite.drawFastVLine(sx,     56, 92, fade565(C_WHITE, 0.55f * (1.0f - st * 0.5f)));
            sprite.drawFastVLine(sx - 1, 62, 80, fade565(C_ACCENT, 0.35f));
        }

        // рот проявляется после включения
        if (dt >= T_SETTLE) {
            const float mt = easeOutCubic((float)(dt - T_SETTLE) / 260.0f);
            sprite.fillRoundRect(i_round(CENTER_X - MOUTH_W * 0.5f),
                                 i_round(BASE_Y + MOUTH_DY),
                                 (int)MOUTH_W, i_round(lerpf(1.0f, MOUTH_H, mt)),
                                 2, cEye);
        }
    }

    sprite.pushSprite(0, 40);   // как во всех анимациях проекта (экран 240x240)
}

} // namespace AnimReboot
