#include "anim_wink.h"
#include "face_state.h"
#include "sensors/imu_sensor.h"

#include <Arduino.h>
#include <math.h>

namespace AnimWink {

namespace {

// -----------------------------------------------------------------------------
// Геометрия и длительность
// -----------------------------------------------------------------------------

constexpr float CENTER_X   = 120.0f;
constexpr float BASE_Y     = 100.0f;

constexpr float EYE_BASE_W = 48.0f;
constexpr float EYE_BASE_H = 60.0f;
constexpr float EYE_SPACING = 104.0f;

constexpr float MOUTH_BASE_W = 20.0f;
constexpr float MOUTH_BASE_H = 6.0f;

constexpr float LID_THICKNESS = 4.0f;
constexpr float LID_BEND      = 8.0f;

// Высота закрытого глаза включает изгиб и толщину дужки.
constexpr float CLOSED_EYE_H = LID_THICKNESS + LID_BEND;

constexpr uint32_t DURATION_MS = 1800;

uint16_t colorEye   = TFT_GREEN;
uint16_t colorGlow1 = TFT_DARKGREEN;
uint16_t colorGlow2 = TFT_BLACK;

float cx = CENTER_X;
float cy = BASE_Y;
float currentSpacing = EYE_SPACING;

float lw = EYE_BASE_W;
float rw = EYE_BASE_W;
float lh = EYE_BASE_H;
float rh = EYE_BASE_H;

float mw = MOUTH_BASE_W;
float mh = MOUTH_BASE_H;

uint32_t startTime = 0;
uint32_t lastFrameTime = 0;
bool finished = true;

// -----------------------------------------------------------------------------
// Математика и RGB565
// -----------------------------------------------------------------------------

float smaller(float a, float b) {
    return a < b ? a : b;
}

float larger(float a, float b) {
    return a > b ? a : b;
}

float clampValue(float value, float low, float high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

float clamp01(float value) {
    return clampValue(value, 0.0f, 1.0f);
}

float mixValue(float a, float b, float amount) {
    return a + (b - a) * amount;
}

float smooth01(float value) {
    value = clamp01(value);
    return value * value * (3.0f - 2.0f * value);
}

float ramp(float begin, float end, float time) {
    return smooth01((time - begin) / (end - begin));
}

int pixel(float value) {
    return static_cast<int>(floorf(value + 0.5f));
}

void follow(float &value, float target, float speed) {
    value += (target - value) * speed;

    if (fabsf(target - value) < 0.02f) {
        value = target;
    }
}

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(
        ((r & 0xF8u) << 8) |
        ((g & 0xFCu) << 3) |
        (b >> 3)
    );
}

uint16_t mixColor(uint16_t from, uint16_t to, float amount) {
    const uint32_t a =
        static_cast<uint32_t>(clamp01(amount) * 256.0f + 0.5f);
    const uint32_t b = 256u - a;

    const uint32_t r =
        (((from >> 11) & 31u) * b +
         ((to   >> 11) & 31u) * a + 128u) >> 8;

    const uint32_t g =
        (((from >> 5) & 63u) * b +
         ((to   >> 5) & 63u) * a + 128u) >> 8;

    const uint32_t blue =
        ((from & 31u) * b +
         (to   & 31u) * a + 128u) >> 8;

    return static_cast<uint16_t>((r << 11) | (g << 5) | blue);
}

uint16_t dimColor(uint16_t color, float amount) {
    return mixColor(TFT_BLACK, color, amount);
}

// -----------------------------------------------------------------------------
// Капсула / изогнутая округлая полоска
//
// Открытый глаз:
//   width = 48, height = 60, bend = 0
//   radius = halfW = 24 — полноценная вертикальная капсула.
//
// Закрытый глаз:
//   height = 4, bend = 8
//   получается тонкая округлая дужка.
//
// catShape используется только для двух мягких лепестков ротика :3.
// -----------------------------------------------------------------------------

void drawBandLayer(
    TFT_eSprite &sprite,
    float x,
    float y,
    float width,
    float height,
    float bend,
    bool catShape,
    float padding,
    uint16_t color
) {
    width  = larger(width, 1.0f);
    height = larger(height, 1.0f);

    const float halfW = width * 0.5f + padding;
    const float halfH = height * 0.5f + padding;

    // Никакого ограничения радиуса значением 14!
    // У вертикальной капсулы радиус строго равен половине ширины.
    // Ограничение половиной высоты нужно лишь при схлопывании глаза.
    const float radius = halfH >= halfW ? halfW : halfH;

    // Быстрый путь для открытого глаза и прямых капсул.
    if (fabsf(bend) < 0.02f) {
        const int w = pixel(halfW * 2.0f);
        const int h = pixel(halfH * 2.0f);
        const int r = (w < h ? w : h) / 2;

        sprite.fillRoundRect(
            pixel(x - w * 0.5f),
            pixel(y - h * 0.5f),
            w,
            h,
            r,
            color
        );
        return;
    }

    // Изогнутый контур рисуется вертикальными отрезками:
    // максимум около 56 столбцов на слой, без отдельного буфера.
    int firstX = static_cast<int>(ceilf(x - halfW - 0.5f));
    int lastX  = static_cast<int>(floorf(x + halfW - 0.5f));

    if (firstX < 0) firstX = 0;
    if (lastX > 239) lastX = 239;

    const float straightHalfW = halfW - radius;
    const float radiusSquared = radius * radius;
    const float inverseCoreHalfW = 2.0f / width;

    for (int px = firstX; px <= lastX; ++px) {
        const float dx = (static_cast<float>(px) + 0.5f) - x;
        const float capX = fabsf(dx) - straightHalfW;

        float extent = halfH;

        if (capX > 0.0f) {
            const float inside =
                larger(0.0f, radiusSquared - capX * capX);

            extent = halfH - radius + sqrtf(inside);
        }

        const float u =
            clampValue(dx * inverseCoreHalfW, -1.0f, 1.0f);

        float profile;

        if (catShape) {
            // Два округлых лепестка: кошачий ротик :3.
            const float q = 2.0f * fabsf(u) - 1.0f;
            profile = 1.0f - q * q - 0.5f;
        } else {
            // Мягкая дужка ∩: центр выше краёв.
            profile = u * u - 0.5f;
        }

        const float lineY = y + bend * profile;

        int top =
            static_cast<int>(ceilf(lineY - extent - 0.5f));
        int bottom =
            static_cast<int>(floorf(lineY + extent - 0.5f));

        if (top < 0) top = 0;
        if (bottom > 199) bottom = 199;

        if (bottom >= top) {
            sprite.drawFastVLine(px, top, bottom - top + 1, color);
        }
    }
}

void drawNeonBand(
    TFT_eSprite &sprite,
    float x,
    float y,
    float width,
    float height,
    float bend,
    bool catShape = false
) {
    drawBandLayer(
        sprite, x, y, width, height, bend, catShape,
        4.0f, colorGlow2
    );

    drawBandLayer(
        sprite, x, y, width, height, bend, catShape,
        2.0f, colorGlow1
    );

    drawBandLayer(
        sprite, x, y, width, height, bend, catShape,
        0.0f, colorEye
    );
}

// -----------------------------------------------------------------------------
// Румянец: округлые розовые щёчки с мягким неоновым ореолом
// -----------------------------------------------------------------------------

void drawBlush(
    TFT_eSprite &sprite,
    float x,
    float y,
    float pulse,
    float opacity
) {
    if (opacity < 0.015f) return;

    const float width  = 18.0f + 2.0f * pulse;
    const float height = 9.0f  + 2.0f * pulse;

    const uint16_t outer =
        dimColor(rgb565(38, 5, 17), opacity);
    const uint16_t inner =
        dimColor(rgb565(120, 27, 61), opacity);
    const uint16_t core =
        dimColor(rgb565(255, 112, 160), opacity);

    drawBandLayer(
        sprite, x, y, width, height, 0.0f, false, 4.0f, outer
    );
    drawBandLayer(
        sprite, x, y, width, height, 0.0f, false, 2.0f, inner
    );
    drawBandLayer(
        sprite, x, y, width, height, 0.0f, false, 0.0f, core
    );
}

// -----------------------------------------------------------------------------
// Загнутая ресничка — короткая квадратичная кривая с тонким кончиком
// -----------------------------------------------------------------------------

struct Point {
    float x;
    float y;
};

void drawLash(
    TFT_eSprite &sprite,
    float eyeX,
    float eyeY,
    float eyeWidth,
    float bend,
    float opacity
) {
    if (opacity < 0.02f) return;

    constexpr int POINT_COUNT = 13;
    Point points[POINT_COUNT];

    const float halfW = larger(eyeWidth * 0.5f, 1.0f);
    const float startDX = larger(0.0f, halfW - 2.0f);
    const float u = startDX / halfW;

    const Point p0 = {
        eyeX + startDX,
        eyeY + bend * (u * u - 0.5f)
    };

    const Point p1 = {
        p0.x + 6.0f,
        p0.y + 2.0f
    };

    const Point p2 = {
        p0.x + 9.0f,
        p0.y - 6.0f
    };

    for (int i = 0; i < POINT_COUNT; ++i) {
        const float t =
            static_cast<float>(i) / (POINT_COUNT - 1);
        const float s = 1.0f - t;

        points[i].x =
            s * s * p0.x + 2.0f * s * t * p1.x + t * t * p2.x;
        points[i].y =
            s * s * p0.y + 2.0f * s * t * p1.y + t * t * p2.y;
    }

    const uint16_t colors[3] = {
        dimColor(colorGlow2, opacity),
        dimColor(colorGlow1, opacity),
        dimColor(colorEye, opacity)
    };

    const int radii[3] = {3, 2, 1};

    for (int layer = 0; layer < 3; ++layer) {
        for (int i = 0; i < POINT_COUNT; ++i) {
            const int x = pixel(points[i].x);
            const int y = pixel(points[i].y);

            if (i > 0) {
                sprite.drawLine(
                    pixel(points[i - 1].x),
                    pixel(points[i - 1].y),
                    x, y, colors[layer]
                );
            }

            int radius = radii[layer];

            // Последние точки становятся тоньше.
            if (i >= POINT_COUNT - 3) {
                --radius;
            }

            if (radius > 0) {
                sprite.fillCircle(x, y, radius, colors[layer]);
            } else {
                sprite.drawPixel(x, y, colors[layer]);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Блик на открытом глазу
// -----------------------------------------------------------------------------

void drawEyeHighlight(
    TFT_eSprite &sprite,
    float eyeX,
    float eyeY,
    float width,
    float height,
    float opacity
) {
    if (opacity < 0.02f || height < 28.0f || width < 20.0f) {
        return;
    }

    const float x = eyeX - width * 0.18f;
    const float y = eyeY - height * 0.18f;

    const float w = smaller(5.0f, width * 0.12f);
    const float h = smaller(11.0f, height * 0.22f);

    // Интерполяция от цвета самого глаза:
    // при исчезновении блика на глазу не остаётся тёмного пятна.
    const uint16_t soft =
        mixColor(colorEye, rgb565(184, 255, 213), opacity * 0.75f);
    const uint16_t bright =
        mixColor(colorEye, TFT_WHITE, opacity * 0.95f);

    drawBandLayer(
        sprite, x, y, w, h, 0.0f, false, 0.0f, soft
    );
    drawBandLayer(
        sprite, x - 0.5f, y - 1.0f,
        2.0f, h * 0.55f, 0.0f, false, 0.0f, bright
    );
}

// -----------------------------------------------------------------------------
// Четырёхлучевые алмазные flare-звёздочки
// -----------------------------------------------------------------------------

void drawFlareLayer(
    TFT_eSprite &sprite,
    float x,
    float y,
    float radius,
    float waist,
    uint16_t color
) {
    // Четыре длинных луча и четыре узких промежуточных вершины.
    const float dx[8] = {
        0.0f, waist, radius * 0.82f, waist,
        0.0f, -waist, -radius * 0.82f, -waist
    };

    const float dy[8] = {
        -radius, -waist, 0.0f, waist,
        radius, waist, 0.0f, -waist
    };

    const int centerX = pixel(x);
    const int centerY = pixel(y);

    for (int i = 0; i < 8; ++i) {
        const int next = (i + 1) & 7;

        sprite.fillTriangle(
            centerX, centerY,
            pixel(x + dx[i]), pixel(y + dy[i]),
            pixel(x + dx[next]), pixel(y + dy[next]),
            color
        );
    }
}

void drawFlare(
    TFT_eSprite &sprite,
    float x,
    float y,
    float radius,
    float opacity
) {
    if (opacity < 0.025f) return;

    drawFlareLayer(
        sprite, x, y, radius + 3.0f, radius * 0.20f + 1.5f,
        dimColor(colorGlow2, opacity)
    );

    drawFlareLayer(
        sprite, x, y, radius + 1.2f, radius * 0.18f + 0.7f,
        dimColor(colorGlow1, opacity)
    );

    drawFlareLayer(
        sprite, x, y, radius, radius * 0.17f,
        dimColor(rgb565(167, 255, 205), opacity)
    );

    drawFlareLayer(
        sprite, x, y, radius * 0.58f, radius * 0.08f,
        dimColor(TFT_WHITE, opacity)
    );

    sprite.drawPixel(
        pixel(x), pixel(y), dimColor(TFT_WHITE, opacity)
    );
}

struct FlareSpec {
    float dx;
    float dy;
    float radius;
    float begin;
    float duration;
};

const FlareSpec FLARES[] = {
    {30.0f, -40.0f, 10.5f, 430.0f, 780.0f},
    { 6.0f, -56.0f,  6.5f, 610.0f, 740.0f},
    {38.0f, -16.0f,  5.0f, 820.0f, 600.0f}
};

struct MicroSpec {
    float dx;
    float dy;
    float begin;
    float duration;
};

const MicroSpec MICROS[] = {
    {13.0f, -37.0f,  520.0f, 420.0f},
    {40.0f, -46.0f,  760.0f, 430.0f},
    {23.0f, -63.0f,  920.0f, 400.0f},
    {42.0f, -29.0f, 1070.0f, 440.0f}
};

float particleEnvelope(float progress) {
    return smooth01(progress / 0.20f) *
           (1.0f - smooth01((progress - 0.55f) / 0.45f));
}

void drawSparkles(
    TFT_eSprite &sprite,
    float rightEyeX,
    float eyeY,
    float time
) {
    for (const FlareSpec &flare : FLARES) {
        const float progress =
            (time - flare.begin) / flare.duration;

        if (progress <= 0.0f || progress >= 1.0f) continue;

        const float opacity = particleEnvelope(progress);
        const float radius =
            flare.radius * (0.60f + 0.40f * opacity);

        drawFlare(
            sprite,
            rightEyeX + flare.dx + 3.0f * progress,
            eyeY + flare.dy - 6.0f * progress,
            radius,
            opacity
        );
    }

    for (const MicroSpec &micro : MICROS) {
        const float progress =
            (time - micro.begin) / micro.duration;

        if (progress <= 0.0f || progress >= 1.0f) continue;

        const float opacity = particleEnvelope(progress);

        if (opacity < 0.03f) continue;

        const int x =
            pixel(rightEyeX + micro.dx + 2.0f * progress);
        const int y =
            pixel(eyeY + micro.dy - 5.0f * progress);

        sprite.fillCircle(
            x, y, 2, dimColor(colorGlow2, opacity)
        );

        const uint16_t glow = dimColor(colorGlow1, opacity);
        sprite.drawFastHLine(x - 2, y, 5, glow);
        sprite.drawFastVLine(x, y - 2, 5, glow);

        const uint16_t light = dimColor(colorEye, opacity);
        sprite.drawFastHLine(x - 1, y, 3, light);
        sprite.drawFastVLine(x, y - 1, 3, light);

        sprite.drawPixel(x, y, dimColor(TFT_WHITE, opacity));
    }
}

} // anonymous namespace

// -----------------------------------------------------------------------------
// Публичный интерфейс
// -----------------------------------------------------------------------------

void init(TFT_eSPI &tft) {
    (void)tft;

    // Используем уже созданный спрайт AnimManager.
    // Собственные буферы и динамические аллокации не нужны.
}

void reset() {
    startTime = millis();
    lastFrameTime = startTime;
    finished = false;

    FaceState::loadGeometry(
        cx, cy, currentSpacing,
        lw, rw, lh, rh,
        mw, mh
    );

    FaceState::setTargetPalette(
        50, 255, 100,
        10, 120, 30,
        0, 40, 10
    );
}

bool isFinished() {
    return finished;
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    (void)tft;

    const uint32_t now = millis();

    const uint32_t elapsed =
        finished ? DURATION_MS : now - startTime;

    const float time = static_cast<float>(
        elapsed < DURATION_MS ? elapsed : DURATION_MS
    );

    float dt = static_cast<float>(now - lastFrameTime);
    lastFrameTime = now;
    dt = clampValue(dt, 1.0f, 48.0f);

    colorEye   = FaceState::current.colorEye;
    colorGlow1 = FaceState::current.colorGlow1;
    colorGlow2 = FaceState::current.colorGlow2;

    // Общая выразительность: мягкий вход и спокойное завершение.
    const float gesture =
        ramp(80.0f, 380.0f, time) *
        (1.0f - ramp(1280.0f, 1720.0f, time));

    // Закрытие -> удержание -> раскрытие.
    const float wink =
        ramp(220.0f, 460.0f, time) *
        (1.0f - ramp(1050.0f, 1420.0f, time));

    float imuX = 0.0f;
    float imuY = 0.0f;

    if (ImuSensor::isReady()) {
        imuX = clampValue(
            ImuSensor::getParallaxX() * 0.35f,
            -14.0f, 14.0f
        );

        imuY = clampValue(
            ImuSensor::getParallaxY() * 0.25f,
            -8.0f, 8.0f
        );
    }

    // Сглаживание с учётом времени между кадрами.
    const float speed = dt / (55.0f + dt);

    const float targetX =
        CENTER_X + imuX + 2.0f * gesture;

    const float targetY =
        BASE_Y + imuY - 2.0f * gesture +
        0.6f * gesture * sinf(time * 0.008f);

    follow(cx, targetX, speed);
    follow(cy, targetY, speed);
    follow(currentSpacing, EYE_SPACING, speed);

    // Открытый левый глаз сохраняет размеры IDLE.
    follow(lw, EYE_BASE_W, speed);
    follow(rw, EYE_BASE_W, speed);
    follow(lh, EYE_BASE_H, speed);

    follow(
        rh,
        mixValue(EYE_BASE_H, CLOSED_EYE_H, wink),
        speed
    );

    follow(mw, mixValue(MOUTH_BASE_W, 24.0f, gesture), speed);
    follow(mh, mixValue(MOUTH_BASE_H, 9.0f, gesture), speed);

    // Финальный кадр имеет точные базовые размеры.
    // Положение лица остаётся плавным и сохраняет параллакс.
    if (elapsed >= DURATION_MS) {
        currentSpacing = EYE_SPACING;
        lw = rw = EYE_BASE_W;
        lh = rh = EYE_BASE_H;
        mw = MOUTH_BASE_W;
        mh = MOUTH_BASE_H;
    }

    // Геометрия сохраняется каждый кадр, в том числе при прерывании.
    FaceState::saveGeometry(
        cx, cy, currentSpacing,
        lw, rw, lh, rh,
        mw, mh
    );

    const float lx = cx - currentSpacing * 0.5f;
    const float rx = cx + currentSpacing * 0.5f;

    // Изгиб привязан к фактической высоте, а не только к таймеру:
    // капсула и дужка остаются одной непрерывно меняющейся фигурой.
    const float actualClosure = clamp01(
        (EYE_BASE_H - rh) / (EYE_BASE_H - CLOSED_EYE_H)
    );

    const float bendGate = ramp(180.0f, 400.0f, time);

    const float lidBend = smaller(
        LID_BEND * smooth01(actualClosure) * bendGate,
        larger(0.0f, rh - LID_THICKNESS)
    );

    const float lidBodyH = larger(1.0f, rh - lidBend);

    sprite.fillSprite(TFT_BLACK);

    // Щёчки рисуются первыми, чтобы их ореол не перекрывал глаза.
    const float blushEnvelope =
        ramp(180.0f, 420.0f, time) *
        (1.0f - ramp(1200.0f, 1680.0f, time));

    const float blushPulse =
        0.5f + 0.5f * sinf(time * 0.017f);

    const float blushOpacity =
        blushEnvelope * (0.82f + 0.18f * blushPulse);

    const float blushY =
        cy + larger(40.0f, larger(lh, rh) * 0.5f + 10.0f);

    drawBlush(
        sprite, lx - 9.0f, blushY,
        blushPulse, blushOpacity
    );
    drawBlush(
        sprite, rx + 9.0f, blushY,
        blushPulse, blushOpacity
    );

    // Ресничка под глазом по порядку рисования:
    // место соединения аккуратно скрывается контуром века.
    const float lashOpacity =
        smooth01((actualClosure - 0.45f) / 0.55f) * bendGate;

    drawLash(
        sprite, rx, cy, rw, lidBend, lashOpacity
    );

    // Три одинаковых неоновых слоя у обоих глаз.
    drawNeonBand(sprite, lx, cy, lw, lh, 0.0f);
    drawNeonBand(sprite, rx, cy, rw, lidBodyH, lidBend);

    drawEyeHighlight(
        sprite, lx, cy, lw, lh, gesture
    );

    // Из нейтральной капсулы рот плавно превращается в :3.
    const float mouthBodyH = larger(
        1.0f, mixValue(mh, 3.0f, gesture)
    );
    const float mouthBend = larger(0.0f, mh - mouthBodyH);

    drawNeonBand(
        sprite,
        cx,
        cy + 43.0f,
        mw,
        mouthBodyH,
        mouthBend,
        true
    );

    drawSparkles(sprite, rx, cy, time);

    sprite.pushSprite(0, 0);

    // main.cpp получает true только после полностью раскрытых глаз
    // и исчезновения декоративных элементов.
    if (elapsed >= DURATION_MS) {
        finished = true;
    }
}

} // namespace AnimWink
