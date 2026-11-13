#include "anim_melt.h"
#include "face_state.h"
#include <Arduino.h>
#include <math.h>

namespace AnimMelt {

namespace {

// ------------------------------ Константы -----------------------------------
const float CENTER_X    = 120.0f;
const float BASE_Y      = 100.0f;
const float EYE_BASE_W  = 48.0f;
const float EYE_BASE_H  = 60.0f;
const float EYE_SPACING = 104.0f;
const float MOUTH_W     = 20.0f;
const float MOUTH_H     = 6.0f;
const float MOUTH_DY    = 48.0f;

const uint32_t T_STARTLE_MS = 400;    // испуг
const uint32_t T_STRETCH_MS = 1000;   // вытягивание и капли (400..1400 мс)
const uint32_t T_RECOVER_MS = 1500;   // восстановление

const float PUDDLE_H_MAX      = 22.0f;   // высота лужи у верхнего края спрайта
const float PUDDLE_W_MAX      = 210.0f;  // ширина лужи
const float PUDDLE_SIDE_SHIFT = 38.0f;   // максимальное боковое смещение лужи

const int MAX_DROPS   = 8;
const int MAX_BUBBLES = 5;
const int SPRITE_W    = 240;
const int SPRITE_H    = 200;

enum Phase { PH_STARTLE, PH_STRETCH, PH_PUDDLE, PH_RECOVER, PH_DONE };

struct Drop   { float x, y, vy, r; bool active; };
struct Bubble { float x, y, r, speed; bool active; uint32_t nextSpawnMs; };

// ------------------------------ Состояние -----------------------------------
Phase    phase = PH_STARTLE;
uint32_t startTime = 0, recoverStart = 0, lastFrameMs = 0;
bool     finished = false;

uint16_t colorEye = 0, colorGlow1 = 0, colorGlow2 = 0;

// Геометрия глаз: верхний и нижний край, ширина "луковицы" и ширина "шейки" у верха
float eyeTopY = 70.0f, eyeBotY = 130.0f, eyeW = EYE_BASE_W, neckW = EYE_BASE_W;
float spacing = EYE_SPACING, eyeCx = CENTER_X;
float jitterX = 0.0f, jitterY = 0.0f;
bool  eyesVisible = true;

float mouthY = BASE_Y + MOUTH_DY, mouthW = MOUTH_W, mouthH = MOUTH_H;
bool  mouthVisible = true;
bool  mouthDropped = false;

float puddleH = 0.0f, puddleW = 0.0f;
float puddleCx = CENTER_X, puddleTargetCx = CENTER_X;
float ripple = 0.0f, splash = 0.0f;
float sideTilt = 0.0f;

// Снимок геометрии в момент startRecovery()
float capTop = 2.0f, capBot = 14.0f, capW = 14.0f, capNeck = 6.0f;
float capSpacing = EYE_SPACING, capPuddleH = 0.0f, capPuddleW = 0.0f, capEyeCx = CENTER_X;

Drop     drops[MAX_DROPS];
uint32_t lastDropMs = 0;
int      dropSide = 0;

Bubble   bubbles[MAX_BUBBLES];

float    colH[SPRITE_W];          // высота лужи по столбцам (-1 = нет лужи)

float    slitH = 3.5f;
bool     slitClosed = false;
uint32_t slitBlinkNext = 0;

// ------------------------------ Математика ----------------------------------
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float lerpf(float a, float b, float t)    { return a + (b - a) * t; }
float easeIn(float t)    { return t * t; }
float easeOut(float t)   { float u = 1.0f - t; return 1.0f - u * u; }
float easeInOut(float t) { return t * t * (3.0f - 2.0f * t); }
float frand(float a, float b) { return a + (b - a) * (float)random(0, 1001) * 0.001f; }

// ------------------------------ Рисование -----------------------------------

// Капсула/круг с отступом pad (для слоёв ореола)
void drawCapsuleP(TFT_eSprite &s, float x, float y, float w, float h, int pad, uint16_t color) {
    int width  = (int)(w + 0.5f) + pad * 2;
    int height = (int)(h + 0.5f) + pad * 2;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    int left = (int)(x - w * 0.5f + 0.5f) - pad;
    int top  = (int)(y - h * 0.5f + 0.5f) - pad;
    int r = (width < height ? width : height) / 2;
    s.fillRoundRect(left, top, width, height, r, color);
}

// Капля/капсула, построенная по строкам:
// снизу округлая "луковица" шириной wBot, кверху сужается до wTop с округлым кончиком.
// Если wTop == wBot — получается точная капсула (как в IDLE).
void drawBlob(TFT_eSprite &s, float x, float topY, float botY, float wBot, float wTop,
              int pad, uint16_t color) {
    topY -= pad; botY += pad;
    wBot += pad * 2.0f; wTop += pad * 2.0f;

    float h = botY - topY;
    if (h < 1.0f || wBot < 1.0f) return;
    if (wTop < 1.0f) wTop = 1.0f;

    float rb = wBot * 0.5f;   // высота нижней дуги
    float rt = wTop * 0.5f;   // высота верхней дуги
    if (rb + rt > h) { float k = h / (rb + rt); rb *= k; rt *= k; }
    float neckSpan = h - rb - rt;
    if (neckSpan < 0.001f) neckSpan = 0.001f;

    int y0 = (int)floorf(topY);
    int y1 = (int)ceilf(botY);
    if (y1 < 0 || y0 >= SPRITE_H) return;

    for (int y = y0; y <= y1; y++) {
        if (y < 0 || y >= SPRITE_H) continue;
        float yc   = (float)y + 0.5f;
        float dBot = botY - yc;
        float dTop = yc - topY;
        if (dBot < 0.0f || dTop < 0.0f) continue;

        float u = clampf((dBot - rb) / neckSpan, 0.0f, 1.0f);
        float w = wBot + (wTop - wBot) * sqrtf(u);   // шейка сужается быстро, потом плавно

        if (dBot < rb) {   // нижняя округлая дуга
            float q = 1.0f - ((rb - dBot) / rb) * ((rb - dBot) / rb);
            if (q < 0.0f) q = 0.0f;
            float wc = wBot * sqrtf(q);
            if (wc < w) w = wc;
        }
        if (dTop < rt) {   // верхний округлый кончик
            float q = 1.0f - ((rt - dTop) / rt) * ((rt - dTop) / rt);
            if (q < 0.0f) q = 0.0f;
            float wc = wTop * sqrtf(q);
            if (wc < w) w = wc;
        }

        int wi = (int)(w + 0.5f);
        if (wi < 1) continue;
        s.drawFastHLine((int)(x - w * 0.5f + 0.5f), y, wi, color);
    }
}

// Летящая капля: шарик с хвостиком, направленным назад (к центру экрана)
void drawDrop(TFT_eSprite &s, const Drop &d, int pad, uint16_t color) {
    int r = (int)(d.r + 0.5f) + pad;
    if (r < 1) r = 1;
    int x = (int)(d.x + 0.5f);
    int y = (int)(d.y + 0.5f);
    s.fillCircle(x, y, r, color);
    s.fillTriangle(x - (int)(d.r * 0.7f) - pad, y,
                   x + (int)(d.r * 0.7f) + pad, y,
                   x, y + (int)(d.r * 2.0f) + pad, color);
}

// Высоты лужи по столбцам: плоский верх с округлыми краями + две волны + всплески
void computePuddle(uint32_t nowMs) {
    for (int x = 0; x < SPRITE_W; x++) colH[x] = -1.0f;
    if (puddleH < 0.5f || puddleW < 2.0f) return;

    float half = puddleW * 0.5f;
    int x0 = (int)(puddleCx - half);
    int x1 = (int)(puddleCx + half);
    if (x0 < 0) x0 = 0;
    if (x1 > SPRITE_W - 1) x1 = SPRITE_W - 1;

    float tt  = (float)(nowMs & 0x3FFFFF);
    float amp = ripple + splash;

    for (int x = x0; x <= x1; x++) {
        float u = ((float)x - puddleCx) / half;
        u = clampf(u, -1.0f, 1.0f);
        float edge = sqrtf(sqrtf(1.0f - u * u));   // (1-u²)^0.25 — плоско в центре, круглые края
        float wave = amp * (0.6f * sinf((float)x * 0.085f + tt * 0.0045f)
                          + 0.4f * sinf((float)x * 0.047f - tt * 0.0031f));
        float h = puddleH * edge * (1.0f + wave);
        if (h < 0.0f) h = 0.0f;
        colH[x] = h;
    }
}

void drawPuddleLayer(TFT_eSprite &s, int pad, uint16_t color) {
    for (int x = 0; x < SPRITE_W; x++) {
        float hx = -1.0f;
        int a = x - pad; if (a < 0) a = 0;
        int b = x + pad; if (b > SPRITE_W - 1) b = SPRITE_W - 1;
        for (int k = a; k <= b; k++) if (colH[k] > hx) hx = colH[k];
        if (hx < 0.0f) continue;
        int hh = (int)(hx + (float)pad + 0.5f);
        if (hh < 1) continue;
        s.drawFastVLine(x, 0, hh, color);
    }
}

// Вся сцена одним цветом с отступом pad — вызывается 3 раза (glow2, glow1, eye)
void drawScene(TFT_eSprite &s, int pad, uint16_t color) {
    drawPuddleLayer(s, pad, color);

    if (eyesVisible) {
        float lx = eyeCx - spacing * 0.5f + jitterX;
        float rx = eyeCx + spacing * 0.5f + jitterX;
        drawBlob(s, lx, eyeTopY + jitterY, eyeBotY + jitterY, eyeW, neckW, pad, color);
        drawBlob(s, rx, eyeTopY + jitterY, eyeBotY + jitterY, eyeW, neckW, pad, color);
    }

    for (int i = 0; i < MAX_DROPS; i++) {
        if (drops[i].active) drawDrop(s, drops[i], pad, color);
    }

    if (mouthVisible && mouthH > 0.5f) {
        drawCapsuleP(s, eyeCx + jitterX, mouthY + jitterY, mouthW, mouthH, pad, color);
    }
}

// Тёмные детали поверх лужи: пузырьки и комичные глазки-щёлочки
void drawDetails(TFT_eSprite &s) {
    for (int i = 0; i < MAX_BUBBLES; i++) {
        if (!bubbles[i].active) continue;
        int xi = (int)bubbles[i].x;
        if (xi < 0 || xi >= SPRITE_W) continue;
        if (colH[xi] < bubbles[i].y + bubbles[i].r + 1.0f) continue;
        int r = (int)(bubbles[i].r + 0.5f);
        if (r < 1) r = 1;
        s.drawCircle(xi, (int)bubbles[i].y, r, TFT_BLACK);
        s.drawPixel(xi + 1, (int)bubbles[i].y + 1, TFT_WHITE);
    }

    bool showSlits = (phase == PH_PUDDLE) || (phase == PH_RECOVER && puddleH > 12.0f);
    if (showSlits) {
        int sy = 9;
        int lx = (int)(puddleCx - 34.0f);
        int rx = (int)(puddleCx + 34.0f);
        int sh = (int)(slitH + 0.5f);
        if (sh < 1) sh = 1;
        if (lx >= 0 && lx < SPRITE_W && colH[lx] > 14.0f) s.fillEllipse(lx, sy, 9, sh, TFT_BLACK);
        if (rx >= 0 && rx < SPRITE_W && colH[rx] > 14.0f) s.fillEllipse(rx, sy, 9, sh, TFT_BLACK);
    }
}

// ------------------------------ Частицы -------------------------------------
void spawnDrop(float x, float y, float r) {
    for (int i = 0; i < MAX_DROPS; i++) {
        if (!drops[i].active) {
            drops[i].active = true;
            drops[i].x = x; drops[i].y = y; drops[i].r = r;
            drops[i].vy = -40.0f;             // летит к верху экрана (физически — вниз)
            return;
        }
    }
}

void updateDrops(float dt) {
    for (int i = 0; i < MAX_DROPS; i++) {
        if (!drops[i].active) continue;
        drops[i].vy -= 520.0f * dt;           // "гравитация" к Y = 0
        drops[i].y  += drops[i].vy * dt;
        int xi = (int)drops[i].x;
        if (xi < 0) xi = 0;
        if (xi > SPRITE_W - 1) xi = SPRITE_W - 1;
        bool landed = (colH[xi] >= 0.0f && drops[i].y <= colH[xi] + 1.0f) || drops[i].y < 1.0f;
        if (landed) {
            drops[i].active = false;
            splash += 0.12f;
            if (splash > 0.35f) splash = 0.35f;
        }
    }
}

void updateBubbles(uint32_t now, float dt) {
    float half = puddleW * 0.5f;
    for (int i = 0; i < MAX_BUBBLES; i++) {
        Bubble &b = bubbles[i];
        if (!b.active) {
            if (phase == PH_PUDDLE && puddleH > 10.0f && now >= b.nextSpawnMs) {
                b.active = true;
                b.x = puddleCx + frand(-half * 0.8f, half * 0.8f);
                b.y = 2.0f;
                b.r = frand(2.0f, 4.0f);
                b.speed = frand(9.0f, 20.0f);
            }
            continue;
        }
        b.y += b.speed * dt;                  // пузырёк всплывает к поверхности лужи
        int xi = (int)b.x;
        if (xi < 0) xi = 0;
        if (xi > SPRITE_W - 1) xi = SPRITE_W - 1;
        if (colH[xi] < 0.0f || b.y + b.r + 1.0f >= colH[xi]) {
            b.active = false;                 // лопнул: небольшая рябь
            b.nextSpawnMs = now + (uint32_t)random(400, 1400);
            splash += 0.06f;
            if (splash > 0.35f) splash = 0.35f;
        }
    }
}

void resetParticles(uint32_t now) {
    for (int i = 0; i < MAX_DROPS; i++) drops[i].active = false;
    for (int i = 0; i < MAX_BUBBLES; i++) {
        bubbles[i].active = false;
        bubbles[i].nextSpawnMs = now + (uint32_t)random(300, 1500);
    }
}

// ------------------------------ Фазы ----------------------------------------
void stepStartle(uint32_t t) {
    float p = clampf((float)t / (float)T_STARTLE_MS, 0.0f, 1.0f);
    float e = easeOut(p);
    float shake = 1.0f - p * 0.5f;

    eyeW  = lerpf(EYE_BASE_W, 56.0f, e);
    neckW = eyeW;
    float h = lerpf(EYE_BASE_H, 68.0f, e);
    eyeTopY = BASE_Y - h * 0.5f;
    eyeBotY = BASE_Y + h * 0.5f;
    spacing = EYE_SPACING;
    eyeCx   = CENTER_X;
    eyesVisible = true;

    float tf = (float)t;
    jitterX = (sinf(tf * 0.090f) * 1.6f + sinf(tf * 0.151f) * 1.0f) * shake;
    jitterY = (sinf(tf * 0.077f) * 1.2f + sinf(tf * 0.123f) * 0.8f) * shake;

    mouthVisible = true;
    mouthW = lerpf(MOUTH_W, 12.0f, e);
    mouthH = lerpf(MOUTH_H, 12.0f, e);     // ротик округляется в "o"
    mouthY = BASE_Y + MOUTH_DY;

    puddleH = 0.0f; puddleW = 0.0f; ripple = 0.0f;

    if (t >= T_STARTLE_MS) phase = PH_STRETCH;
}

void stepStretch(uint32_t t, uint32_t now) {
    float p = clampf((float)(t - T_STARTLE_MS) / (float)T_STRETCH_MS, 0.0f, 1.0f);

    jitterX = sinf((float)t * 0.09f) * 0.6f * (1.0f - p);
    jitterY = 0.0f;

    eyeTopY = lerpf(66.0f, 2.0f, easeOut(p));     // верх глаза быстро тянется к потолку
    eyeBotY = lerpf(134.0f, 14.0f, easeIn(p));    // низ подтягивается позже
    eyeW    = lerpf(56.0f, 14.0f, p);             // луковица истончается
    neckW   = lerpf(56.0f, 6.0f, sqrtf(p));       // шейка становится тонкой нитью
    spacing = lerpf(EYE_SPACING, 88.0f, p);       // струйки сходятся к центру лужи
    eyeCx   = lerpf(CENTER_X, puddleCx, easeIn(p));
    eyesVisible = true;

    puddleH = PUDDLE_H_MAX * easeInOut(p);
    puddleW = lerpf(40.0f, PUDDLE_W_MAX, easeOut(p));
    ripple  = 0.12f * p;

    if (!mouthDropped) {
        float mp = clampf(p / 0.3f, 0.0f, 1.0f);
        mouthVisible = true;
        mouthY = lerpf(BASE_Y + MOUTH_DY, 120.0f, easeIn(mp));
        mouthW = lerpf(12.0f, 8.0f, mp);
        mouthH = lerpf(12.0f, 20.0f, mp);
        if (p >= 0.3f) {
            mouthDropped = true;
            mouthVisible = false;
            spawnDrop(eyeCx, mouthY - 8.0f, 5.0f);   // ротик отрывается каплей
        }
    }

    if (p > 0.10f && p < 0.92f && now - lastDropMs > 110) {
        lastDropMs = now;
        dropSide = dropSide ? 0 : 1;
        float ex = eyeCx + (dropSide ? spacing * 0.5f : -spacing * 0.5f);
        spawnDrop(ex + frand(-4.0f, 4.0f), eyeTopY - 3.0f, frand(2.5f, 4.5f));
    }

    if (p >= 1.0f) {
        phase = PH_PUDDLE;
        eyesVisible = false;
    }
}

void stepPuddle(uint32_t t) {
    float tf = (float)t;
    puddleH = PUDDLE_H_MAX + 1.5f * sinf(tf * 0.0028f);     // лужа "дышит"
    puddleW = PUDDLE_W_MAX + 6.0f * sinf(tf * 0.0019f);
    ripple  = 0.11f;
    eyesVisible  = false;
    mouthVisible = false;
    jitterX = 0.0f; jitterY = 0.0f;

    // Геометрия "спрятанных" глаз — точка старта восстановления
    eyeTopY = 2.0f; eyeBotY = 14.0f; eyeW = 14.0f; neckW = 6.0f;
    spacing = 88.0f;
    eyeCx   = puddleCx;
}

void stepRecover(uint32_t now) {
    float q = clampf((float)(now - recoverStart) / (float)T_RECOVER_MS, 0.0f, 1.0f);
    eyesVisible = true;
    jitterX = 0.0f; jitterY = 0.0f;
    ripple = 0.0f;

    if (q < 0.40f) {
        // A: из лужи вниз вытягиваются две струи, лужа собирается в них
        float a = easeInOut(q / 0.40f);
        eyeTopY = capTop;
        eyeBotY = lerpf(capBot, 134.0f, a);
        eyeW    = lerpf(capW, 44.0f, a);
        neckW   = lerpf(capNeck, 12.0f, a);
        spacing = lerpf(capSpacing, EYE_SPACING, a);
        eyeCx   = lerpf(capEyeCx, CENTER_X, a);
        puddleH = capPuddleH * (1.0f - a);
        puddleW = capPuddleW * (1.0f - 0.6f * a);
        ripple  = 0.10f * (1.0f - a);
        mouthVisible = false;
    } else if (q < 0.65f) {
        // B: струи отрываются от потолка и надуваются в капсулы
        float b = easeOut((q - 0.40f) / 0.25f);
        eyeTopY = lerpf(capTop, BASE_Y - EYE_BASE_H * 0.5f, b);
        eyeBotY = lerpf(134.0f, BASE_Y + EYE_BASE_H * 0.5f, b);
        eyeW    = lerpf(44.0f, EYE_BASE_W, b);
        neckW   = lerpf(12.0f, EYE_BASE_W, b);
        spacing = EYE_SPACING;
        eyeCx   = CENTER_X;
        puddleH = 0.0f; puddleW = 0.0f;
        mouthVisible = false;
    } else {
        // C: желейный отскок (squash & stretch) + возврат ротика
        float c = (q - 0.65f) / 0.35f;
        float s = sinf(c * 3.14159f * 3.0f) * (1.0f - c) * 0.18f;
        float h = EYE_BASE_H * (1.0f + s);
        eyeW    = EYE_BASE_W * (1.0f - s * 0.7f);
        neckW   = eyeW;
        eyeTopY = BASE_Y - h * 0.5f;
        eyeBotY = BASE_Y + h * 0.5f;
        spacing = EYE_SPACING;
        eyeCx   = CENTER_X;
        puddleH = 0.0f; puddleW = 0.0f;
        mouthVisible = true;
        mouthW = MOUTH_W;
        mouthH = MOUTH_H * easeOut(c);
        mouthY = BASE_Y + MOUTH_DY;
    }

    if (q >= 1.0f) {
        phase = PH_DONE;
        finished = true;
    }
}

void stepDone() {
    eyesVisible = true;
    eyeW = EYE_BASE_W; neckW = EYE_BASE_W;
    eyeTopY = BASE_Y - EYE_BASE_H * 0.5f;
    eyeBotY = BASE_Y + EYE_BASE_H * 0.5f;
    spacing = EYE_SPACING; eyeCx = CENTER_X;
    jitterX = 0.0f; jitterY = 0.0f;
    mouthVisible = true; mouthW = MOUTH_W; mouthH = MOUTH_H; mouthY = BASE_Y + MOUTH_DY;
    puddleH = 0.0f; puddleW = 0.0f; ripple = 0.0f;
}

void updateSlits(uint32_t now) {
    if (now >= slitBlinkNext) {
        if (!slitClosed) { slitClosed = true;  slitBlinkNext = now + 120; }
        else             { slitClosed = false; slitBlinkNext = now + (uint32_t)random(1500, 3500); }
    }
    float target = slitClosed ? 1.0f : 3.5f;
    slitH += (target - slitH) * 0.45f;
}

} // anonymous namespace

// ============================================================================
void init(TFT_eSPI &tft) {
    (void)tft;
    for (int i = 0; i < MAX_DROPS; i++) drops[i].active = false;
    for (int i = 0; i < MAX_BUBBLES; i++) { bubbles[i].active = false; bubbles[i].nextSpawnMs = 0; }
    for (int x = 0; x < SPRITE_W; x++) colH[x] = -1.0f;
}

void reset() {
    uint32_t now = millis();
    startTime   = now;
    lastFrameMs = now;
    phase       = PH_STARTLE;
    finished    = false;
    mouthDropped = false;
    lastDropMs  = now;
    dropSide    = 0;
    splash      = 0.0f;
    ripple      = 0.0f;
    puddleH     = 0.0f;
    puddleW     = 0.0f;
    puddleCx    = CENTER_X;
    slitH       = 3.5f;
    slitClosed  = false;
    slitBlinkNext = now + 1500;

    // Берём текущую геометрию лица, чтобы старт был бесшовным
    float cx, cy, sp, lw, rw, lh, rh, mw, mh;
    FaceState::loadGeometry(cx, cy, sp, lw, rw, lh, rh, mw, mh);
    eyeCx   = CENTER_X;
    spacing = EYE_SPACING;
    eyeW    = lw; neckW = lw;
    eyeTopY = cy - lh * 0.5f;
    eyeBotY = cy + lh * 0.5f;
    mouthW  = mw; mouthH = mh; mouthY = cy + MOUTH_DY;
    eyesVisible = true; mouthVisible = true;

    // "Жидкая" бирюзово-циановая палитра
    FaceState::setTargetPalette(20, 240, 220, 5, 100, 120, 0, 30, 50);

    resetParticles(now);
}

void setSideTilt(float side) {
    sideTilt = clampf(side, -1.0f, 1.0f);
}

void startRecovery() {
    if (phase == PH_RECOVER || phase == PH_DONE) return;
    uint32_t now = millis();

    capTop     = eyeTopY;
    capBot     = eyeBotY;
    capW       = eyeW;
    capNeck    = neckW;
    capSpacing = spacing;
    capEyeCx   = eyeCx;
    capPuddleH = puddleH;
    capPuddleW = puddleW;

    for (int i = 0; i < MAX_DROPS; i++) drops[i].active = false;
    for (int i = 0; i < MAX_BUBBLES; i++) bubbles[i].active = false;

    recoverStart = now;
    phase = PH_RECOVER;

    // Возвращаем фирменный зелёный
    FaceState::setTargetPalette(50, 255, 100, 10, 120, 30, 0, 40, 10);
}

bool isRecovering() { return phase == PH_RECOVER; }
bool isFinished()   { return finished; }

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    (void)tft;
    colorEye   = FaceState::current.colorEye;
    colorGlow1 = FaceState::current.colorGlow1;
    colorGlow2 = FaceState::current.colorGlow2;

    uint32_t now = millis();
    float dt = (float)(now - lastFrameMs) * 0.001f;
    if (dt < 0.0f)   dt = 0.0f;
    if (dt > 0.05f)  dt = 0.05f;
    lastFrameMs = now;
    uint32_t t = now - startTime;

    // Лужа сползает к более низкому краю при боковом крене
    puddleTargetCx = clampf(CENTER_X + sideTilt * PUDDLE_SIDE_SHIFT, 60.0f, 180.0f);
    puddleCx += (puddleTargetCx - puddleCx) * 0.05f;

    switch (phase) {
        case PH_STARTLE: stepStartle(t);          break;
        case PH_STRETCH: stepStretch(t, now);     break;
        case PH_PUDDLE:  stepPuddle(t);           break;
        case PH_RECOVER: stepRecover(now);        break;
        case PH_DONE:    stepDone();              break;
    }

    computePuddle(now);
    updateDrops(dt);
    updateBubbles(now, dt);
    updateSlits(now);
    splash -= splash * 3.0f * dt;
    if (splash < 0.0f) splash = 0.0f;

    float cy = (eyeTopY + eyeBotY) * 0.5f;
    float eh = eyeBotY - eyeTopY;
    FaceState::saveGeometry(eyeCx, cy, spacing, eyeW, eyeW, eh, eh, mouthW, mouthH);

    sprite.fillSprite(TFT_BLACK);
    drawScene(sprite, 4, colorGlow2);
    drawScene(sprite, 2, colorGlow1);
    drawScene(sprite, 0, colorEye);
    drawDetails(sprite);
    sprite.pushSprite(0, 0);
}

} // namespace AnimMelt
