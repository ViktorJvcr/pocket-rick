#include "anim_sleep.h"
#include "face_state.h"
#include "sensors/imu_sensor.h"
#include <math.h>

namespace AnimSleep {

namespace {

// ===========================================================================
// Геометрия лица (строго как в IDLE)
// ===========================================================================
const float CENTER_X    = 120.0f;
const float BASE_Y      = 100.0f;
const float EYE_W       = 48.0f;
const float EYE_H       = 60.0f;
const float EYE_SPACING = 104.0f;
const float MOUTH_W     = 20.0f;
const float MOUTH_H     = 6.0f;
const float MOUTH_DY    = 48.0f;
const float TWO_PI_F    = 6.28318530718f;

// Палитры (eyeR,G,B, glow1R,G,B, glow2R,G,B)
const float PAL_GREEN[9] = {  50.0f, 255.0f, 100.0f,  10.0f, 120.0f,  30.0f,  0.0f, 40.0f, 10.0f };
const float PAL_NIGHT[9] = {  70.0f, 100.0f, 220.0f,  22.0f,  36.0f, 120.0f,  6.0f, 10.0f, 48.0f };
const float PAL_ANGER[9] = { 255.0f,  50.0f,  20.0f, 160.0f,  10.0f,   5.0f, 65.0f,  0.0f,  0.0f };

// ===========================================================================
// Каналы ключевых кадров
// ===========================================================================
enum Channel {
    CH_CL = 0,  // закрытие верхнего века левого глаза, 0..1
    CH_CR,      // закрытие верхнего века правого глаза, 0..1
    CH_SL,      // скос верхнего века левого глаза, px (минус = внутренний угол ниже)
    CH_SR,      // скос верхнего века правого глаза
    CH_BU,      // провис верхнего века в середине, px (плюс = вниз)
    CH_DY,      // смещение головы по Y, px (плюс = вниз)
    CH_YAWN,    // зевок 0..1
    CH_GR,      // ворчливая волна рта 0..1
    CH_BOT,     // прищур нижним веком 0..1
    CH_PAL,     // микс палитры (для сна: 0 зеленый .. 1 индиго; для ярости: 0 .. 1 алый)
    CH_MW,      // ширина рта, px
    CH_DX,      // смещение головы по X, px
    CH_COUNT
};

struct Key {
    float t;
    float v[CH_COUNT];
};

// ---------------------------------------------------------------------------
// ЗАСЫПАНИЕ: тяжелеют веки -> клюет носом -> вздрагивает -> зевает -> засыпает
//     t       CL     CR     SL    SR    BU    DY     YAWN  GR    BOT    PAL    MW     DX
// ---------------------------------------------------------------------------
const Key FALL_KEYS[] = {
    {    0.0f, { 0.00f, 0.00f, 0.0f, 0.0f, 0.0f,  0.0f, 0.0f, 0.0f, 0.00f, 0.00f, 20.0f, 0.0f } },
    {  900.0f, { 0.18f, 0.16f, 0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f, 0.02f, 0.15f, 20.0f, 0.0f } },
    { 1700.0f, { 0.42f, 0.40f, 0.0f, 0.0f, 3.0f,  3.0f, 0.0f, 0.0f, 0.04f, 0.30f, 19.0f, 0.0f } },
    { 2300.0f, { 0.78f, 0.76f, 0.0f, 0.0f, 5.0f,  9.0f, 0.0f, 0.0f, 0.06f, 0.40f, 18.0f, 0.0f } },
    { 2500.0f, { 0.88f, 0.86f, 0.0f, 0.0f, 5.0f, 11.0f, 0.0f, 0.0f, 0.06f, 0.42f, 18.0f, 0.0f } },
    { 2720.0f, { 0.14f, 0.12f, 0.0f, 0.0f, 1.0f, -3.0f, 0.0f, 0.0f, 0.00f, 0.42f, 20.0f, 0.0f } },
    { 3100.0f, { 0.22f, 0.20f, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.00f, 0.45f, 20.0f, 0.0f } },
    { 3600.0f, { 0.36f, 0.34f, 0.0f, 0.0f, 2.0f,  0.0f, 0.0f, 0.0f, 0.02f, 0.50f, 20.0f, 0.0f } },
    { 4050.0f, { 0.64f, 0.62f, 0.0f, 0.0f, 3.0f, -2.0f, 1.0f, 0.0f, 0.10f, 0.55f, 20.0f, 0.0f } },
    { 4650.0f, { 0.44f, 0.42f, 0.0f, 0.0f, 3.0f,  2.0f, 0.0f, 0.0f, 0.04f, 0.62f, 19.0f, 0.0f } },
    { 5200.0f, { 0.56f, 0.54f, 0.0f, 0.0f, 4.0f,  4.0f, 0.0f, 0.0f, 0.04f, 0.72f, 17.0f, 0.0f } },
    { 6600.0f, { 1.00f, 1.00f, 0.0f, 0.0f, 6.0f,  8.0f, 0.0f, 0.0f, 0.00f, 1.00f, 14.0f, 0.0f } },
};
const int   FALL_COUNT = (int)(sizeof(FALL_KEYS) / sizeof(FALL_KEYS[0]));
const float FALL_END   = 6600.0f;

// ---------------------------------------------------------------------------
// ВОРЧЛИВОЕ ПРОБУЖДЕНИЕ: толчок -> тяжелые веки -> прищур и ворчание ->
// зевок -> промаргивание -> норма. PAL здесь: 1 = индиго, 0 = зеленый.
//     t       CL     CR     SL    SR     BU    DY    YAWN  GR    BOT    PAL    MW     DX
// ---------------------------------------------------------------------------
const Key GRUMPY_KEYS[] = {
    {    0.0f, { 1.00f, 1.00f, 0.0f,  0.0f, 6.0f, 8.0f, 0.0f, 0.0f, 0.00f, 1.00f, 14.0f, 0.0f } },
    {  300.0f, { 1.00f, 1.00f, 0.0f,  0.0f, 6.0f, 7.0f, 0.0f, 0.0f, 0.00f, 1.00f, 14.0f, 0.0f } },
    {  600.0f, { 0.80f, 0.82f, 0.0f,  0.0f, 4.0f, 6.0f, 0.0f, 0.0f, 0.06f, 1.00f, 15.0f, 0.0f } },
    {  740.0f, { 0.85f, 0.86f, 0.0f,  0.0f, 4.0f, 6.0f, 0.0f, 0.0f, 0.06f, 1.00f, 15.0f, 0.0f } },
    {  950.0f, { 0.62f, 0.63f, 0.0f,  0.0f, 3.0f, 5.0f, 0.0f, 0.1f, 0.08f, 1.00f, 16.0f, 0.0f } },
    { 1250.0f, { 0.52f, 0.46f, 0.0f, -5.0f, 2.0f, 3.0f, 0.0f, 0.7f, 0.08f, 1.00f, 18.0f, 0.0f } },
    { 1550.0f, { 0.50f, 0.42f, 0.0f, -7.0f, 2.0f, 2.0f, 0.0f, 1.0f, 0.10f, 1.00f, 20.0f, 0.0f } },
    { 1800.0f, { 0.52f, 0.46f, 0.0f, -4.0f, 2.0f, 2.0f, 0.0f, 0.0f, 0.08f, 1.00f, 20.0f, 0.0f } },
    { 2050.0f, { 0.72f, 0.72f, 0.0f, -1.0f, 3.0f, 0.0f, 1.0f, 0.0f, 0.14f, 0.95f, 20.0f, 0.0f } },
    { 2300.0f, { 0.30f, 0.30f, 0.0f,  0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.04f, 0.80f, 20.0f, 0.0f } },
    { 2420.0f, { 0.98f, 0.98f, 0.0f,  0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.00f, 0.70f, 20.0f, 0.0f } },
    { 2540.0f, { 0.18f, 0.18f, 0.0f,  0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.00f, 0.60f, 20.0f, 0.0f } },
    { 2660.0f, { 0.95f, 0.95f, 0.0f,  0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.00f, 0.50f, 20.0f, 0.0f } },
    { 2800.0f, { 0.10f, 0.10f, 0.0f,  0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.00f, 0.35f, 20.0f, 0.0f } },
    { 3200.0f, { 0.00f, 0.00f, 0.0f,  0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.00f, 0.00f, 20.0f, 0.0f } },
};
const int   GRUMPY_COUNT = (int)(sizeof(GRUMPY_KEYS) / sizeof(GRUMPY_KEYS[0]));
const float GRUMPY_END   = 3200.0f;

// ---------------------------------------------------------------------------
// ЗЛОЕ ПРОБУЖДЕНИЕ (5 стуков): рывок, распах, сердитая маска. PAL: 0..1 = ярость.
//     t       CL     CR     SL     SR     BU     DY    YAWN  GR    BOT    PAL    MW     DX
// ---------------------------------------------------------------------------
const Key ANGRY_KEYS[] = {
    {    0.0f, { 1.00f, 1.00f,  0.0f,  0.0f,  6.0f, 8.0f, 0.0f, 0.0f, 0.00f, 0.00f, 14.0f, 0.0f } },
    {   80.0f, { 1.00f, 1.00f,  0.0f,  0.0f,  6.0f, 7.0f, 0.0f, 0.0f, 0.00f, 0.00f, 14.0f, 0.0f } },
    {  260.0f, { 0.12f, 0.12f,  0.0f,  0.0f,  0.0f, -2.0f, 0.0f, 0.5f, 0.00f, 0.30f, 18.0f, 0.0f } },
    {  650.0f, { 0.38f, 0.38f, -9.0f, -9.0f, -4.0f, 0.0f, 0.0f, 1.0f, 0.12f, 1.00f, 24.0f, 0.0f } },
    {  700.0f, { 0.38f, 0.38f, -9.0f, -9.0f, -4.0f, 0.0f, 0.0f, 1.0f, 0.12f, 1.00f, 24.0f, 0.0f } },
};
const int   ANGRY_COUNT = (int)(sizeof(ANGRY_KEYS) / sizeof(ANGRY_KEYS[0]));
const float ANGRY_END   = 700.0f;

// ===========================================================================
// Состояние
// ===========================================================================
enum class Mode { FALL_ASLEEP, DEEP, WAKE_GRUMPY, WAKE_ANGRY };

Mode     mode = Mode::FALL_ASLEEP;
uint32_t tStart = 0;          // старт текущего режима
uint32_t tDeepStart = 0;      // момент начала глубокого сна (для Zzz и дыхания)

float ch[CH_COUNT];           // каналы, использованные в последнем кадре
float startCh[CH_COUNT];      // каналы, захваченные при смене режима (carry)

float mixNight    = 0.0f;     // 0 зеленый .. 1 индиго
float mixAnger    = 0.0f;     // 0 .. 1 алый
float frozenNight = 0.0f;     // индиго-микс, замороженный на момент wakeUpAngry
float zAlpha      = 0.0f;     // общая яркость Zzz
float zAlphaAtWake = 0.0f;    // яркость Zzz в момент пробуждения
float zScatter    = 0.0f;     // 0..1 степень разлета Zzz при пробуждении
bool  zScatterAngry = false;
float lineExtra   = 0.0f;     // дыхание толщины спящей линии
float parallaxW   = 0.0f;     // вес параллакса IMU (0 во сне, 1 в конце пробуждения)

uint16_t colorEye = 0, colorGlow1 = 0, colorGlow2 = 0;

// ===========================================================================
// Математика
// ===========================================================================
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float k) { return a + (b - a) * k; }
inline float smooth01(float u) { u = clamp01(u); return u * u * (3.0f - 2.0f * u); }
inline float smoothRange(float t, float a, float b) { return smooth01((t - a) / (b - a)); }
inline float window(float t, float a, float b, float c) {
    return smoothRange(t, a, b) * (1.0f - smoothRange(t, b, c));
}

// Интерполяция по ключам (smoothstep на каждом участке: мягкий старт и стоп)
void evalKeys(const Key *keys, int n, float t, float out[CH_COUNT]) {
    if (t <= keys[0].t) {
        for (int i = 0; i < CH_COUNT; i++) out[i] = keys[0].v[i];
        return;
    }
    if (t >= keys[n - 1].t) {
        for (int i = 0; i < CH_COUNT; i++) out[i] = keys[n - 1].v[i];
        return;
    }
    int k = 0;
    while (k < n - 2 && t > keys[k + 1].t) k++;
    float span = keys[k + 1].t - keys[k].t;
    float u = span > 0.001f ? (t - keys[k].t) / span : 1.0f;
    float s = smooth01(u);
    for (int i = 0; i < CH_COUNT; i++) out[i] = lerpf(keys[k].v[i], keys[k + 1].v[i], s);
}

// Мягко подмешиваем реальное состояние на момент смены режима к первому ключу
void applyCarry(float out[CH_COUNT], const float first[CH_COUNT], float t) {
    float w = 1.0f - smoothRange(t, 0.0f, 400.0f);
    if (w <= 0.0f) return;
    for (int i = 0; i < CH_COUNT; i++) out[i] += (startCh[i] - first[i]) * w;
}

void captureStart() {
    for (int i = 0; i < CH_COUNT; i++) startCh[i] = ch[i];
}

// Вертикальная полувысота капсулы W x H в колонке dx от центра (<0 = вне капсулы)
float capsuleHalfExtent(float dx, float W, float H) {
    float hw = W * 0.5f, hh = H * 0.5f;
    float adx = fabsf(dx);
    if (adx >= hw) return -1.0f;
    if (H >= W) {
        float r = hw;
        float s = r * r - adx * adx;
        if (s < 0.0f) s = 0.0f;
        return (hh - r) + sqrtf(s);
    }
    float r = hh;
    float flat = hw - r;
    if (adx <= flat) return r;
    float u = adx - flat;
    float s = r * r - u * u;
    if (s < 0.0f) s = 0.0f;
    return sqrtf(s);
}

uint16_t scaledColor(TFT_eSprite &s, float r, float g, float b, float a) {
    a = clamp01(a);
    return s.color565((uint8_t)(r * a + 0.5f), (uint8_t)(g * a + 0.5f), (uint8_t)(b * a + 0.5f));
}

// ===========================================================================
// Палитра
// ===========================================================================
void applyPalette() {
    float p[9];
    for (int i = 0; i < 9; i++) {
        p[i] = lerpf(PAL_GREEN[i], PAL_NIGHT[i], clamp01(mixNight));
        p[i] = lerpf(p[i], PAL_ANGER[i], clamp01(mixAnger));
    }
    FaceState::setTargetPalette(
        (int)(p[0] + 0.5f), (int)(p[1] + 0.5f), (int)(p[2] + 0.5f),
        (int)(p[3] + 0.5f), (int)(p[4] + 0.5f), (int)(p[5] + 0.5f),
        (int)(p[6] + 0.5f), (int)(p[7] + 0.5f), (int)(p[8] + 0.5f));
}

// ===========================================================================
// ГЛАЗ: капсула + криволинейные веки, рисуется колонками.
// Слой k (0 ядро, 2 glow1, 4 glow2) = та же капсула, расширенная на 2k,
// веки сдвинуты наружу на k. Так неон следует за веком без клиньев.
// ===========================================================================
void drawEyeLayer(TFT_eSprite &s, float ex, float ey,
                  float closeTop, float bulge, float slant, float closeBot,
                  float lineThick, float mirror, float k, uint16_t color) {
    float W = EYE_W + 2.0f * k;
    float H = EYE_H + 2.0f * k;
    float half = W * 0.5f;
    float eyeTop = ey - EYE_H * 0.5f;
    float eyeBot = ey + EYE_H * 0.5f;
    float thick = (lineThick > 0.01f) ? (lineThick + 2.0f * k) : 0.0f;

    int x0 = (int)floorf(ex - half);
    int x1 = (int)ceilf(ex + half);
    for (int ix = x0; ix <= x1; ix++) {
        float dx = ((float)ix + 0.5f) - ex;
        float ext = capsuleHalfExtent(dx, W, H);
        if (ext <= 0.0f) continue;

        float nx = clampf(dx / (EYE_W * 0.5f), -1.0f, 1.0f);
        float curve = 1.0f - nx * nx;
        float nOut = nx * mirror;

        float capTop = ey - ext;
        float capBot = ey + ext;
        float lidTop = eyeTop + closeTop * EYE_H + bulge * curve + slant * nOut - k;
        float lidBot = eyeBot - closeBot * EYE_H * (0.5f + 0.5f * curve) + k;

        float top = capTop > lidTop ? capTop : lidTop;
        float bot = capBot < lidBot ? capBot : lidBot;
        if (bot - top < thick) bot = top + thick;

        int y0 = (int)floorf(top + 0.5f);
        int y1 = (int)floorf(bot + 0.5f);
        if (y1 > y0) s.drawFastVLine(ix, y0, y1 - y0, color);
    }
}

void drawEye(TFT_eSprite &s, float ex, float ey,
             float closeTop, float bulge, float slant, float closeBot, float mirror) {
    closeTop = clamp01(closeTop);
    closeBot = clamp01(closeBot);
    // Толщина спящей линии плавно появляется, когда веко закрыто более чем на 62%
    float lineThick = 5.0f * clamp01((closeTop - 0.62f) / 0.38f) + lineExtra * clamp01((closeTop - 0.9f) / 0.1f);
    drawEyeLayer(s, ex, ey, closeTop, bulge, slant, closeBot, lineThick, mirror, 4.0f, colorGlow2);
    drawEyeLayer(s, ex, ey, closeTop, bulge, slant, closeBot, lineThick, mirror, 2.0f, colorGlow1);
    drawEyeLayer(s, ex, ey, closeTop, bulge, slant, closeBot, lineThick, mirror, 0.0f, colorEye);
}

// ===========================================================================
// РОТ: ровная черта / ворчливая волна (колонками) или зевок-овал
// ===========================================================================
void drawMouthLayer(TFT_eSprite &s, float mx, float my, float mw, float grumpy, float k, uint16_t color) {
    float W = mw + 2.0f * k;
    float H = MOUTH_H + 2.0f * k;
    float half = W * 0.5f;
    float amp = 2.6f * grumpy;
    float frown = 2.0f * grumpy;

    int x0 = (int)floorf(mx - half);
    int x1 = (int)ceilf(mx + half);
    for (int ix = x0; ix <= x1; ix++) {
        float dx = ((float)ix + 0.5f) - mx;
        float ext = capsuleHalfExtent(dx, W, H);
        if (ext <= 0.0f) continue;
        float nx = clampf(dx / (mw * 0.5f), -1.0f, 1.0f);
        float yc = my + amp * sinf(nx * 6.5f) + frown * nx * nx;
        int y0 = (int)floorf(yc - ext + 0.5f);
        int y1 = (int)floorf(yc + ext + 0.5f);
        if (y1 > y0) s.drawFastVLine(ix, y0, y1 - y0, color);
    }
}

void drawMouth(TFT_eSprite &s, float mx, float my, float mw, float yawn, float grumpy) {
    yawn = clamp01(yawn);
    grumpy = clamp01(grumpy);
    if (mw < 6.0f) mw = 6.0f;

    if (yawn < 0.01f) {
        drawMouthLayer(s, mx, my, mw, grumpy, 4.0f, colorGlow2);
        drawMouthLayer(s, mx, my, mw, grumpy, 2.0f, colorGlow1);
        drawMouthLayer(s, mx, my, mw, grumpy, 0.0f, colorEye);
        return;
    }

    // Зевок: капсула 20x6 непрерывно раскрывается в вертикальный овал с темной серединой
    float e = smooth01(yawn);
    float w = lerpf(mw, 24.0f, e);
    float h = lerpf(MOUTH_H, 28.0f, e);
    float cy = my + 4.0f * e;
    int wi = (int)(w + 0.5f), hi = (int)(h + 0.5f);
    if (wi < 1) wi = 1;
    if (hi < 1) hi = 1;
    int left = (int)(mx - wi * 0.5f + 0.5f);
    int top  = (int)(cy - hi * 0.5f + 0.5f);
    int r = (wi < hi ? wi : hi) / 2;

    s.fillRoundRect(left - 4, top - 4, wi + 8, hi + 8, r + 4, colorGlow2);
    s.fillRoundRect(left - 2, top - 2, wi + 4, hi + 4, r + 2, colorGlow1);
    s.fillRoundRect(left, top, wi, hi, r, colorEye);
    if (hi - 8 >= 4 && wi - 8 >= 4) {
        uint16_t dark = scaledColor(s, FaceState::current.eyeR, FaceState::current.eyeG, FaceState::current.eyeB, 0.10f);
        int ri = r - 4;
        if (ri < 1) ri = 1;
        s.fillRoundRect(left + 4, top + 4, wi - 8, hi - 8, ri, dark);
    }
}

// ===========================================================================
// Zzz: три буквы, улетают вверх-вправо, растут и тают
// ===========================================================================
void drawZ(TFT_eSprite &s, float x, float y, float size, float a) {
    float hw = size * 0.42f;
    float hh = size * 0.5f;
    uint16_t glow = scaledColor(s, FaceState::current.gl1R, FaceState::current.gl1G, FaceState::current.gl1B, a * 0.9f);
    uint16_t core = scaledColor(s, FaceState::current.eyeR, FaceState::current.eyeG, FaceState::current.eyeB, a);

    s.drawWideLine(x - hw, y - hh, x + hw, y - hh, 4.5f, glow, TFT_BLACK);
    s.drawWideLine(x + hw, y - hh, x - hw, y + hh, 4.5f, glow, TFT_BLACK);
    s.drawWideLine(x - hw, y + hh, x + hw, y + hh, 4.5f, glow, TFT_BLACK);

    s.drawWideLine(x - hw, y - hh, x + hw, y - hh, 2.0f, core, TFT_BLACK);
    s.drawWideLine(x + hw, y - hh, x - hw, y + hh, 2.0f, core, TFT_BLACK);
    s.drawWideLine(x - hw, y + hh, x + hw, y + hh, 2.0f, core, TFT_BLACK);
}

void drawZzz(TFT_eSprite &s, float baseX, float baseY, float zTime) {
    if (zAlpha < 0.02f) return;
    for (int i = 0; i < 3; i++) {
        float life = fmodf(zTime + (float)i * 870.0f, 2600.0f) / 2600.0f;
        float x = baseX + 10.0f * life + 9.0f * sinf(life * 3.5f + (float)i * 1.3f);
        float y = baseY - 58.0f * life;
        float size = 7.0f + 9.0f * life;
        float a = smoothRange(life, 0.0f, 0.15f) * (1.0f - smoothRange(life, 0.62f, 1.0f)) * zAlpha;

        if (zScatterAngry) {
            x += (float)(i - 1) * 30.0f * zScatter;
            y -= 25.0f * zScatter;
        } else {
            y -= 30.0f * zScatter;
            x += 8.0f * zScatter;
        }
        if (a < 0.03f) continue;
        drawZ(s, x, y, size, a);
    }
}

} // anonymous namespace

// ===========================================================================
// API
// ===========================================================================
void init(TFT_eSPI &tft) {
    (void)tft;
}

void reset() {
    mode = Mode::FALL_ASLEEP;
    tStart = millis();
    tDeepStart = tStart;
    mixNight = 0.0f;
    mixAnger = 0.0f;
    frozenNight = 0.0f;
    zAlpha = 0.0f;
    zAlphaAtWake = 0.0f;
    zScatter = 0.0f;
    zScatterAngry = false;
    lineExtra = 0.0f;
    parallaxW = 0.0f;

    for (int i = 0; i < CH_COUNT; i++) {
        ch[i] = FALL_KEYS[0].v[i];
        startCh[i] = FALL_KEYS[0].v[i];
    }

    // Бесшовный вход из IDLE: подхватываем текущую геометрию и мягко вливаем в таблицу
    float gcx = CENTER_X, gcy = BASE_Y, gsp = EYE_SPACING;
    float glw = EYE_W, grw = EYE_W, glh = EYE_H, grh = EYE_H, gmw = MOUTH_W, gmh = MOUTH_H;
    FaceState::loadGeometry(gcx, gcy, gsp, glw, grw, glh, grh, gmw, gmh);
    startCh[CH_DX] = clampf(gcx - CENTER_X, -20.0f, 20.0f);
    startCh[CH_DY] = clampf(gcy - BASE_Y, -20.0f, 20.0f);
    startCh[CH_CL] = clamp01(1.0f - glh / EYE_H);
    startCh[CH_CR] = clamp01(1.0f - grh / EYE_H);
    startCh[CH_MW] = clampf(gmw, 8.0f, 32.0f);

    applyPalette();
}

void wakeUpGrumpy() {
    if (mode != Mode::FALL_ASLEEP && mode != Mode::DEEP) return;
    captureStart();
    zAlphaAtWake = zAlpha;
    zScatterAngry = false;
    mode = Mode::WAKE_GRUMPY;
    tStart = millis();
}

void wakeUpAngry() {
    if (mode == Mode::WAKE_ANGRY) return;
    captureStart();
    frozenNight = mixNight;
    startCh[CH_PAL] = 0.0f;       // в таблице ярости PAL означает алый микс, стартуем с 0
    zAlphaAtWake = zAlpha;
    zScatterAngry = true;
    mode = Mode::WAKE_ANGRY;
    tStart = millis();
}

bool isFinished() {
    return mode == Mode::WAKE_GRUMPY && (float)(millis() - tStart) >= GRUMPY_END;
}

bool isAngerReady() {
    return mode == Mode::WAKE_ANGRY && (float)(millis() - tStart) >= ANGRY_END;
}

bool isAsleep() {
    if (mode == Mode::DEEP) return true;
    if (mode == Mode::FALL_ASLEEP) return (float)(millis() - tStart) > 1500.0f;
    return false;
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    (void)tft;
    colorEye   = FaceState::current.colorEye;
    colorGlow1 = FaceState::current.colorGlow1;
    colorGlow2 = FaceState::current.colorGlow2;

    uint32_t now = millis();
    float t = (float)(now - tStart);
    float out[CH_COUNT];
    parallaxW = 0.0f;
    zScatter = 0.0f;

    if (mode == Mode::FALL_ASLEEP && t >= FALL_END) {
        mode = Mode::DEEP;
        tDeepStart = now;
    }

    if (mode == Mode::FALL_ASLEEP) {
        evalKeys(FALL_KEYS, FALL_COUNT, t, out);
        applyCarry(out, FALL_KEYS[0].v, t);

        // Сонное подрагивание тяжелеющих век
        float env = window(t, 1000.0f, 1800.0f, 2500.0f) + 0.6f * window(t, 4900.0f, 5600.0f, 6300.0f);
        float tremor = 0.025f * env * sinf(t * (TWO_PI_F / 140.0f));
        out[CH_CL] += tremor;
        out[CH_CR] += tremor * 0.8f;

        // Упругий отскок головы при вздрагивании «я не сплю!»
        if (t > 2500.0f) {
            float s = t - 2500.0f;
            out[CH_DY] += -3.0f * expf(-s / 260.0f) * sinf(TWO_PI_F * s / 420.0f);
        }

        mixNight = out[CH_PAL];
        mixAnger = 0.0f;
        zAlpha = 0.0f;
        lineExtra *= 0.9f;
    }
    else if (mode == Mode::DEEP) {
        float td = (float)(now - tDeepStart);
        for (int i = 0; i < CH_COUNT; i++) out[i] = FALL_KEYS[FALL_COUNT - 1].v[i];

        // Умиротворенное дыхание (начинается с 0 и нулевой скорости: без шва после засыпания)
        float breath = 0.5f * (1.0f - cosf(td * (TWO_PI_F / 3800.0f)));
        out[CH_DY] -= 1.6f * breath;
        out[CH_BU] += 0.9f * breath;
        out[CH_MW] += 1.5f * breath;
        lineExtra = 0.7f * breath;

        mixNight = 1.0f;
        mixAnger = 0.0f;
        zAlpha = smoothRange(td, 300.0f, 1600.0f);
    }
    else if (mode == Mode::WAKE_GRUMPY) {
        evalKeys(GRUMPY_KEYS, GRUMPY_COUNT, t, out);
        applyCarry(out, GRUMPY_KEYS[0].v, t);

        // Физичный толчок от тряски: затухающая пружина, пик ~7 px вверх
        if (t < 900.0f) {
            out[CH_DY] += -10.0f * expf(-t / 250.0f) * sinf(TWO_PI_F * t / 500.0f);
        }
        // Укоризненное покачивание головой: влево-вправо-центр
        if (t > 950.0f) {
            float s = t - 950.0f;
            out[CH_DX] += 6.0f * sinf(TWO_PI_F * s / 720.0f) * expf(-s / 520.0f);
        }

        mixNight = out[CH_PAL];
        mixAnger = 0.0f;
        zScatter = smoothRange(t, 0.0f, 320.0f);
        zAlpha = zAlphaAtWake * (1.0f - zScatter);
        lineExtra *= 0.9f;
        parallaxW = smoothRange(t, 2700.0f, GRUMPY_END);
    }
    else {
        evalKeys(ANGRY_KEYS, ANGRY_COUNT, t, out);
        applyCarry(out, ANGRY_KEYS[0].v, t);

        mixNight = frozenNight;
        mixAnger = out[CH_PAL];
        zScatter = smoothRange(t, 0.0f, 220.0f);
        zAlpha = zAlphaAtWake * (1.0f - zScatter);
        lineExtra *= 0.85f;
    }

    out[CH_CL]   = clamp01(out[CH_CL]);
    out[CH_CR]   = clamp01(out[CH_CR]);
    out[CH_BOT]  = clamp01(out[CH_BOT]);
    out[CH_YAWN] = clamp01(out[CH_YAWN]);
    out[CH_GR]   = clamp01(out[CH_GR]);
    for (int i = 0; i < CH_COUNT; i++) ch[i] = out[i];

    applyPalette();

    // Параллакс IMU: во сне выключен, к концу пробуждения плавно включается для стыка с IDLE
    float imuX = 0.0f, imuY = 0.0f;
    if (ImuSensor::isReady() && parallaxW > 0.001f) {
        imuX = clampf(ImuSensor::getParallaxX() * 0.35f, -14.0f, 14.0f) * parallaxW;
        imuY = clampf(ImuSensor::getParallaxY() * 0.25f, -8.0f, 8.0f) * parallaxW;
    }

    float fx = CENTER_X + out[CH_DX] + imuX;
    float fy = BASE_Y + out[CH_DY] + imuY;
    float lx = fx - EYE_SPACING * 0.5f;
    float rx = fx + EYE_SPACING * 0.5f;

    // ---- отрисовка ----
    sprite.fillSprite(TFT_BLACK);
    drawZzz(sprite, rx + 26.0f, fy - 34.0f, (float)(now - tDeepStart));
    drawEye(sprite, lx, fy, out[CH_CL], out[CH_BU], out[CH_SL], out[CH_BOT], -1.0f);
    drawEye(sprite, rx, fy, out[CH_CR], out[CH_BU], out[CH_SR], out[CH_BOT],  1.0f);
    drawMouth(sprite, fx, fy + MOUTH_DY, out[CH_MW], out[CH_YAWN], out[CH_GR]);
    sprite.pushSprite(0, 40);

    // Геометрия для бесшовной передачи в IDLE / ANGER
    float visL = EYE_H * (1.0f - out[CH_CL]);
    float visR = EYE_H * (1.0f - out[CH_CR]);
    if (visL < 4.0f) visL = 4.0f;
    if (visR < 4.0f) visR = 4.0f;
    FaceState::saveGeometry(fx, fy, EYE_SPACING, EYE_W, EYE_W, visL, visR, out[CH_MW], MOUTH_H);
}

} // namespace AnimSleep
