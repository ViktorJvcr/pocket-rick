#include "anim_disco.h"
#include "face_state.h"
#include <Arduino.h>
#include <math.h>

namespace AnimDisco {

namespace {

// ============================ Константы =====================================
constexpr int16_t  SPR_W   = 240;
constexpr int16_t  SPR_H   = 200;
constexpr float    CENTER_X = 120.0f;
constexpr float    BASE_Y   = 100.0f;

constexpr float    K_TAU    = 6.2831853f;
constexpr float    K_PI     = 3.1415927f;
constexpr float    BPM      = 124.0f;
constexpr float    BEAT_MS  = 60000.0f / BPM; // ~484 мс
constexpr uint32_t FRAME_MS = 16;             // ~60 FPS

constexpr int16_t  EYE_W    = 46;
constexpr int16_t  EYE_H    = 52;
constexpr int16_t  EYE_R    = 14;
constexpr float    EYE_DX   = 48.0f; // полурасстояние между глазами

constexpr uint8_t  NOTE_MAX    = 8;
constexpr uint8_t  STAR_MAX    = 8;
constexpr uint8_t  BRIM_PTS    = 24;

// ============================ Цвета RGB565 ==================================
inline constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (uint16_t)(b >> 3));
}

const uint16_t C_BG       = rgb565(10, 2, 24);    // Глубокий ночной фиолетовый
const uint16_t C_BG_PULSE = rgb565(26, 6, 52);    // Пульсация на сильной доле
const uint16_t C_RING     = rgb565(180, 40, 255); // Неоновое басовое кольцо
const uint16_t C_EYE_CORE = rgb565(50, 255, 230); // Неоновая бирюза
const uint16_t C_EYE_HI   = rgb565(220, 255, 250);// Бело-бирюзовый блик
const uint16_t C_EYE_GLOW = rgb565(10, 80, 80);
const uint16_t C_LID_EDGE = rgb565(255, 60, 210); // Неоновый контур века
const uint16_t C_MOUTH    = rgb565(255, 80, 200); // Сочная диско-фуксия

const uint16_t C_HAT_BODY = rgb565(24, 6, 42);    // Тёмный корпус шляпы
const uint16_t C_HAT_DARK = rgb565(14, 2, 26);
const uint16_t C_HAT_EDGE = rgb565(255, 60, 220); // Неоновая окантовка шляпы

const uint16_t NOTE_COLORS[4] = {
    rgb565(255, 40, 220),  // Маджента / фуксия
    rgb565(30, 240, 255),  // Неоновый циан
    rgb565(255, 235, 40),  // Неоновый желтый
    rgb565(120, 255, 60)   // Лайм
};

// ============================ Вспомогательные функции =======================
inline int32_t ri(float v) { return (int32_t)lroundf(v); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

uint16_t lerp565(uint16_t a, uint16_t b, uint8_t t) {
    if (t == 0) return a;
    if (t >= 255) return b;
    int32_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    int32_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    int32_t r = ar + (((br - ar) * t) >> 8);
    int32_t g = ag + (((bg - ag) * t) >> 8);
    int32_t bl = ab + (((bb - ab) * t) >> 8);
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

uint16_t hsv565(uint8_t h, uint8_t s, uint8_t v) {
    uint8_t region = h / 43;
    uint8_t rem = (uint8_t)((h - region * 43) * 6);
    uint8_t p = (uint8_t)((v * (255 - s)) >> 8);
    uint8_t q = (uint8_t)((v * (255 - ((s * rem) >> 8))) >> 8);
    uint8_t t = (uint8_t)((v * (255 - ((s * (255 - rem)) >> 8))) >> 8);
    uint8_t r, g, b;
    switch (region) {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    return rgb565(r, g, b);
}

// Быстрый xorshift генератор псевдослучайных чисел
uint32_t rngState = 0x2451A89Eu;
inline uint32_t rnd() {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}
inline float rndf(float a, float b) {
    return a + (b - a) * ((rnd() & 0xFFFF) / 65535.0f);
}

// ============================ Структуры 2D геометрии =======================
struct Vec2 { float x, y; };
struct Pose { float x, y, ca, sa; };

inline Vec2 xf(const Pose &p, float lx, float ly) {
    Vec2 res;
    res.x = p.x + lx * p.ca - ly * p.sa;
    res.y = p.y + lx * p.sa + ly * p.ca;
    return res;
}

void fillPolygon(TFT_eSprite &s, const Vec2 &center, const Vec2 *pts, uint8_t n, uint16_t col) {
    for (uint8_t i = 0; i < n; ++i) {
        const Vec2 &a = pts[i];
        const Vec2 &b = pts[(i + 1) % n];
        s.fillTriangle(ri(center.x), ri(center.y), ri(a.x), ri(a.y), ri(b.x), ri(b.y), col);
    }
}

// ============================ Частицы (Ноты) ================================
struct NoteParticle {
    bool     active;
    uint8_t  type;    // 0 = одинарная ♪, 1 = сдвоенная ♫
    float    x, y;
    float    vx, vy;
    float    amp, freq, phase;
    float    age, life;
    uint16_t color;
};

struct StarParticle {
    float    x, y;
    float    vy;
    float    phase, speed;
    uint8_t  size;
    uint16_t color;
};

struct HatPhysics {
    float x, y, tilt;
    float vx, vy, vt;
};

NoteParticle notes[NOTE_MAX];
StarParticle stars[STAR_MAX];
HatPhysics   hat;

uint32_t animStartTime = 0;
uint32_t lastFrameMs   = 0;
int32_t  lastBeatIdx   = -1;
int32_t  lastHalfIdx   = -1;
bool     spawnLeftSide = false;

// ============================ Реализация Ноты ===============================
void spawnNote(float hx, float hy, float preAge = 0.0f) {
    for (uint8_t i = 0; i < NOTE_MAX; ++i) {
        if (notes[i].active) continue;
        spawnLeftSide = !spawnLeftSide;
        float side = spawnLeftSide ? -1.0f : 1.0f;

        NoteParticle &n = notes[i];
        n.active = true;
        n.type   = ((rnd() % 3) == 0) ? 1 : 0; // треть нот сдвоенные
        n.x      = hx + side * rndf(50.0f, 85.0f);
        n.y      = hy + rndf(-15.0f, 25.0f);
        n.vx     = side * rndf(4.0f, 10.0f);
        n.vy     = -rndf(24.0f, 38.0f);
        n.amp    = rndf(6.0f, 12.0f);
        n.freq   = rndf(1.8f, 2.8f);
        n.phase  = rndf(0.0f, K_TAU);
        n.life   = rndf(2.2f, 3.0f);
        n.age    = preAge;
        n.x     += n.vx * preAge;
        n.y     += n.vy * preAge;
        n.color  = NOTE_COLORS[rnd() & 3];
        return;
    }
}

void updateNotes(float dt) {
    for (uint8_t i = 0; i < NOTE_MAX; ++i) {
        if (!notes[i].active) continue;
        notes[i].age += dt;
        notes[i].x   += notes[i].vx * dt;
        notes[i].y   += notes[i].vy * dt;
        if (notes[i].age >= notes[i].life || notes[i].y < -20.0f) {
            notes[i].active = false;
        }
    }
}

void drawNoteSingle(TFT_eSprite &s, int32_t x, int32_t y, uint16_t col, uint16_t glow) {
    // Одинарная нота ♪: головка + штиль + флажок
    s.fillCircle(x, y, 4, glow);
    s.fillCircle(x, y, 3, col);
    s.drawFastVLine(x + 2, y - 12, 13, col);
    s.drawFastVLine(x + 3, y - 12, 13, col);
    s.drawLine(x + 3, y - 12, x + 8, y - 8, col);
    s.drawLine(x + 8, y - 8, x + 7, y - 3, col);
}

void drawNoteDouble(TFT_eSprite &s, int32_t x, int32_t y, uint16_t col, uint16_t glow) {
    // Сдвоенная нота ♫: две головки + два штиля + перекладина
    const int gap = 11;
    s.fillCircle(x, y, 4, glow);
    s.fillCircle(x + gap, y - 2, 4, glow);
    s.fillCircle(x, y, 3, col);
    s.fillCircle(x + gap, y - 2, 3, col);
    s.drawFastVLine(x + 2, y - 13, 14, col);
    s.drawFastVLine(x + 2 + gap, y - 15, 14, col);
    // Двойная перекладина
    s.drawLine(x + 2, y - 13, x + 2 + gap, y - 15, col);
    s.drawLine(x + 2, y - 12, x + 2 + gap, y - 14, col);
    s.drawLine(x + 2, y - 10, x + 2 + gap, y - 12, col);
}

void drawNotes(TFT_eSprite &s, float punch, uint16_t bg) {
    for (uint8_t i = 0; i < NOTE_MAX; ++i) {
        const NoteParticle &n = notes[i];
        if (!n.active) continue;
        float alpha = clampf(n.age / 0.2f, 0.0f, 1.0f) * clampf((n.life - n.age) / 0.6f, 0.0f, 1.0f);
        uint16_t col  = lerp565(bg, n.color, (uint8_t)(alpha * 255.0f));
        uint16_t glow = lerp565(bg, n.color, (uint8_t)(alpha * 80.0f));
        int32_t px = ri(n.x + n.amp * sinf(n.freq * n.age + n.phase));
        int32_t py = ri(n.y - 2.5f * punch);
        if (n.type == 0) drawNoteSingle(s, px, py, col, glow);
        else             drawNoteDouble(s, px, py, col, glow);
    }
}

// ============================ Реализация Звёзд ===============================
void initStars() {
    for (uint8_t i = 0; i < STAR_MAX; ++i) {
        StarParticle &st = stars[i];
        float ang = K_TAU * i / STAR_MAX + rndf(-0.3f, 0.3f);
        float rad = rndf(70.0f, 105.0f);
        st.x = clampf(CENTER_X + cosf(ang) * rad * 1.15f, 10.0f, SPR_W - 10.0f);
        st.y = clampf(BASE_Y + sinf(ang) * rad * 0.85f, 10.0f, SPR_H - 25.0f);
        st.vy    = rndf(-2.5f, 2.5f);
        st.phase = rndf(0.0f, K_TAU);
        st.speed = rndf(2.0f, 4.0f);
        st.size  = (uint8_t)(3 + (rnd() % 3));
        st.color = (rnd() % 3 == 0) ? TFT_WHITE : NOTE_COLORS[rnd() & 3];
    }
}

void updateStars(float dt, float tSec) {
    for (uint8_t i = 0; i < STAR_MAX; ++i) {
        StarParticle &st = stars[i];
        st.y += st.vy * dt;
        st.x += 1.5f * sinf(tSec * 0.4f + st.phase) * dt;
        if (st.y < 8.0f) st.y = SPR_H - 30.0f;
        if (st.y > SPR_H - 25.0f) st.y = 10.0f;
    }
}

void drawStar(TFT_eSprite &s, int32_t x, int32_t y, uint8_t size, float br, uint16_t col, uint16_t bg) {
    int32_t L = (int32_t)(size * (0.6f + 0.4f * br) + 0.5f);
    if (L < 1) L = 1;
    uint16_t c    = lerp565(bg, col, (uint8_t)(br * 255.0f));
    uint16_t core = lerp565(bg, TFT_WHITE, (uint8_t)(br * 255.0f));

    s.drawFastHLine(x - L, y, 2 * L + 1, c);
    s.drawFastVLine(x, y - L, 2 * L + 1, c);
    int32_t d = (L * 2) / 3;
    if (d >= 1) {
        s.drawLine(x - d, y - d, x + d, y + d, c);
        s.drawLine(x - d, y + d, x + d, y - d, c);
    }
    s.drawPixel(x, y, core);
}

void drawStars(TFT_eSprite &s, float tSec, float punch, uint16_t bg) {
    for (uint8_t i = 0; i < STAR_MAX; ++i) {
        const StarParticle &st = stars[i];
        float br = 0.35f + 0.65f * powf(0.5f + 0.5f * sinf(st.speed * tSec + st.phase), 2.5f);
        br += 0.35f * punch;
        br = clampf(br, 0.0f, 1.0f);
        drawStar(s, ri(st.x), ri(st.y), st.size, br, st.color, bg);
    }
}

// ============================ Шляпа (Диско-федора) ===========================
void updateHatPhysics(float dt, float targetX, float targetY, float targetTilt, bool newBeat) {
    const float kx = 160.0f, cx = 15.0f;
    const float ky = 200.0f, cy = 16.0f;
    const float kt = 110.0f, ct = 12.0f;

    // Импульс подскока на каждом бите
    if (newBeat) {
        hat.vy -= 45.0f;
        hat.vt += (rnd() & 1) ? 0.35f : -0.35f;
    }

    // Полунеявное интегрирование
    hat.vx += ((targetX - hat.x) * kx - hat.vx * cx) * dt;
    hat.x  += hat.vx * dt;

    hat.vy += ((targetY - hat.y) * ky - hat.vy * cy) * dt;
    hat.y  += hat.vy * dt;

    hat.vt += ((targetTilt - hat.tilt) * kt - hat.vt * ct) * dt;
    hat.tilt += hat.vt * dt;
    hat.tilt = clampf(hat.tilt, -0.4f, 0.4f);
}

void drawHat(TFT_eSprite &s, const HatPhysics &h, float punch, uint8_t bandHue, uint16_t bg) {
    Pose p;
    p.x = h.x;
    p.y = h.y;
    p.ca = cosf(h.tilt);
    p.sa = sinf(h.tilt);

    float squash = 1.0f - 0.08f * punch;

    // 1. Поля шляпы (плавный эллипс с изогнутыми полями)
    Vec2 brim[BRIM_PTS];
    for (uint8_t i = 0; i < BRIM_PTS; ++i) {
        float th = K_TAU * i / BRIM_PTS;
        float c = cosf(th), sn = sinf(th);
        brim[i] = xf(p, 54.0f * c, 8.5f * sn - 4.5f * c * c);
    }
    fillPolygon(s, xf(p, 0.0f, 0.0f), brim, BRIM_PTS, C_HAT_DARK);
    for (uint8_t i = 0; i < BRIM_PTS; ++i) {
        const Vec2 &a = brim[i];
        const Vec2 &b = brim[(i + 1) % BRIM_PTS];
        s.drawLine(ri(a.x), ri(a.y), ri(b.x), ri(b.y), C_HAT_EDGE);
    }

    // 2. Тулья шляпы с заломом
    float crownH = 38.0f * squash;
    Vec2 crown[7] = {
        xf(p, -34.0f, -2.0f),
        xf(p,  34.0f, -2.0f),
        xf(p,  30.0f, -crownH + 3.0f),
        xf(p,  13.0f, -crownH),
        xf(p,   0.0f, -crownH + 7.0f), // центральный залом
        xf(p, -13.0f, -crownH),
        xf(p, -30.0f, -crownH + 3.0f)
    };
    fillPolygon(s, xf(p, 0.0f, -crownH * 0.45f), crown, 7, C_HAT_BODY);

    // 3. Неоновая светящаяся лента
    uint16_t bandCol = hsv565(bandHue, 255, 255);
    Vec2 band[4] = {
        xf(p, -33.0f, -3.0f),
        xf(p,  33.0f, -3.0f),
        xf(p,  32.0f, -12.0f),
        xf(p, -32.0f, -12.0f)
    };
    fillPolygon(s, xf(p, 0.0f, -7.5f), band, 4, bandCol);
    // Бликующая верхняя линия ленты
    s.drawLine(ri(band[2].x), ri(band[2].y), ri(band[3].x), ri(band[3].y), TFT_WHITE);

    // 4. Неоновая обводка тульи
    for (uint8_t i = 1; i < 7; ++i) {
        const Vec2 &a = crown[i];
        const Vec2 &b = crown[(i + 1) % 7];
        s.drawLine(ri(a.x), ri(a.y), ri(b.x), ri(b.y), C_HAT_EDGE);
    }

    // 5. Блик на заломе тульи
    Vec2 creaseA = xf(p, 0.0f, -crownH + 7.0f);
    Vec2 creaseB = xf(p, 0.0f, -crownH + 18.0f);
    s.drawLine(ri(creaseA.x), ri(creaseA.y), ri(creaseB.x), ri(creaseB.y), C_HAT_DARK);
}

// ============================ Расслабленные глаза ===========================
void drawRelaxedEye(TFT_eSprite &s, float eyeX, float eyeY, float tilt, int side, uint16_t bg) {
    int32_t x0 = ri(eyeX - EYE_W * 0.5f);
    int32_t y0 = ri(eyeY - EYE_H * 0.5f);

    // Свечение и базовое тело глаза
    s.fillRoundRect(x0 - 2, y0 - 2, EYE_W + 4, EYE_H + 4, EYE_R + 2, C_EYE_GLOW);
    s.fillRoundRect(x0, y0, EYE_W, EYE_H, EYE_R, C_EYE_CORE);

    // Блик внизу глаза
    s.fillCircle(x0 + EYE_W - 12, y0 + EYE_H - 12, 3, C_EYE_HI);

    // Расслабленный прищур век (полуприкрытые глаза в такт кайфа)
    // Верхнее веко срезает 50-55% верхнего контура глаза плавной дугой
    float lidBase = 0.52f;
    for (int32_t i = 0; i < EYE_W; ++i) {
        float u = (i * 2.0f / (EYE_W - 1)) - 1.0f; // -1..1
        // Форма века: в центре чуть глубже, к краям приподнято, угол наклонен
        float f = lidBase + 0.08f * (1.0f - u * u) + 0.04f * u * side + 0.4f * tilt * u;
        int32_t h = (int32_t)(f * EYE_H + 0.5f);
        if (h > EYE_H) h = EYE_H;
        if (h > 0) {
            s.drawFastVLine(x0 + i, y0 - 2, h + 2, bg);
            // Неоновая ресничная кромка века
            s.drawPixel(x0 + i, y0 + h, C_LID_EDGE);
            if (h + 1 < EYE_H) {
                s.drawPixel(x0 + i, y0 + h + 1, lerp565(C_LID_EDGE, C_EYE_CORE, 80));
            }
        }
    }
}

void drawSmile(TFT_eSprite &s, float hx, float hy, float tilt, float punch) {
    float my = hy + EYE_H * 0.5f + 18.0f;
    float open = 1.0f + 0.25f * punch;
    const int N = 12;
    for (int i = 0; i <= N; ++i) {
        float u = (i / (float)N) * 2.0f - 1.0f;
        float x = hx + 18.0f * u + 3.0f; // легкая ухмылка
        float y = my + 4.5f * open * (1.0f - u * u) + tilt * (x - hx);
        if (u > 0.0f) y -= 1.5f * u;
        s.fillCircle(ri(x), ri(y), 2, C_MOUTH);
    }
}

// Диско-танцпол снизу экрана
void drawDancefloor(TFT_eSprite &s, int32_t beatIdx, float punch, uint8_t hueBase, uint16_t bg) {
    const int32_t tiles = 8;
    const int32_t tw = SPR_W / tiles;
    const int32_t ty = SPR_H - 12;

    for (int32_t i = 0; i < tiles; ++i) {
        uint8_t hue = (uint8_t)(hueBase + i * 32);
        bool on = ((i + beatIdx) & 1) == 0;
        float bright = on ? (0.6f + 0.4f * punch) : 0.25f;
        uint16_t col = lerp565(bg, hsv565(hue, 255, 255), (uint8_t)(bright * 255.0f));
        s.fillRect(i * tw + 1, ty, tw - 2, 10, col);
    }
}

} // anonymous namespace

// ============================ Публичный API =================================
void init(TFT_eSPI &tft) {
    (void)tft;
    rngState ^= (uint32_t)micros() | 1u;
    for (uint8_t i = 0; i < NOTE_MAX; ++i) notes[i].active = false;
    initStars();
}

void reset() {
    uint32_t now = millis();
    animStartTime = now;
    lastFrameMs   = now - FRAME_MS;
    lastBeatIdx   = -1;
    lastHalfIdx   = -1;
    spawnLeftSide = (rnd() & 1) != 0;

    for (uint8_t i = 0; i < NOTE_MAX; ++i) notes[i].active = false;
    for (uint8_t i = 0; i < 4; ++i) spawnNote(CENTER_X, BASE_Y, rndf(0.2f, 1.5f));
    initStars();

    // Шляпа упруго опускается на голову
    hat.x = CENTER_X;
    hat.y = BASE_Y - EYE_H * 0.5f - 35.0f;
    hat.tilt = -0.2f;
    hat.vx = hat.vy = hat.vt = 0.0f;

    // Задаем фирменную сочную диско-палитру
    FaceState::setTargetPalette(50, 255, 230, 20, 100, 100, 10, 2, 24);
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    (void)tft;
    uint32_t now = millis();
    if (now - lastFrameMs < FRAME_MS) return;
    float dt = (now - lastFrameMs) * 0.001f;
    if (dt > 0.1f) dt = 0.1f;
    lastFrameMs = now;

    // 1. Бит и ритм (~124 BPM)
    uint32_t tMs   = now - animStartTime;
    float tSec     = tMs * 0.001f;
    float beatF    = tMs / BEAT_MS;
    int32_t beatIdx = (int32_t)beatF;
    float b        = beatF - (float)beatIdx; // 0..1 внутри удара
    int32_t halfIdx = (int32_t)(beatF * 2.0f);

    bool newBeat = (beatIdx != lastBeatIdx);
    lastBeatIdx = beatIdx;
    bool newHalf = (halfIdx != lastHalfIdx);
    lastHalfIdx = halfIdx;

    float punch = expf(-6.0f * b); // вспышка/акцент на бите

    // 2. Ритмичное покачивание лица (X-sway + Y-кач + наклон)
    float sway  = sinf(K_PI * beatF); // покачивание влево-вправо на каждые 2 удара
    float bob   = 4.5f * sinf(K_TAU * b) + 2.0f * sinf(2.0f * K_TAU * b); // пружинящий кач вверх-вниз
    float tilt  = 0.10f * sway; // легкий наклон головы

    float headX = CENTER_X + 9.0f * sway;
    float headY = BASE_Y + bob;

    // 3. Физика шляпы (secondary motion с инерцией)
    float hatTargetY = headY - EYE_H * 0.5f - 14.0f;
    updateHatPhysics(dt, headX + 2.0f * sway, hatTargetY, tilt * 1.25f, newBeat);

    // 4. Обновление частиц
    if (newHalf) spawnNote(headX, headY, 0.0f);
    updateNotes(dt);
    updateStars(dt, tSec);

    // 5. Отрисовка кадра
    uint16_t bg = lerp565(C_BG, C_BG_PULSE, (uint8_t)(punch * 180.0f));
    sprite.fillSprite(bg);

    // Басовое неоновое кольцо, расходящееся от головы на бите
    float ringR = 48.0f + 65.0f * b;
    uint16_t ringCol = lerp565(bg, C_RING, (uint8_t)((1.0f - b) * (1.0f - b) * 110.0f));
    sprite.drawCircle(ri(headX), ri(headY), ri(ringR), ringCol);
    sprite.drawCircle(ri(headX), ri(headY), ri(ringR) + 1, ringCol);

    drawDancefloor(sprite, beatIdx, punch, (uint8_t)(tMs / 20), bg);
    drawStars(sprite, tSec, punch, bg);
    drawNotes(sprite, punch, bg);

    // Глаза и улыбка
    drawRelaxedEye(sprite, headX - EYE_DX, headY - tilt * EYE_DX, tilt, -1, bg);
    drawRelaxedEye(sprite, headX + EYE_DX, headY + tilt * EYE_DX, tilt,  1, bg);
    drawSmile(sprite, headX, headY, tilt, punch);

    // Шляпа поверх головы
    drawHat(sprite, hat, punch, (uint8_t)(tMs / 12), bg);

    // Вывод кадра на дисплей со стандартным смещением по Y (40 px)
    sprite.pushSprite(0, 40);
}

} // namespace AnimDisco
