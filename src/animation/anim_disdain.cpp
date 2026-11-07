#include "anim_disdain.h"
#include "face_state.h"
#include "sensors/imu_sensor.h"
#include <Arduino.h>
#include <math.h>

namespace AnimDisdain {

namespace {
    uint16_t colorEye = 0, colorGlow1 = 0, colorGlow2 = 0;

    const float EYE_BASE_W = 48.0f;
    const float EYE_BASE_H = 60.0f;
    const float eyeSpacing = 104.0f;
    const float CENTER_X   = 120.0f;
    const float BASE_Y     = 100.0f;
    const float PI_F       = 3.14159265f;

    // ---------------- Тайминги ----------------
    const uint32_t DURATION_MS     = 2800;   // полная длительность
    const uint32_t RELEASE_MS      = 2350;   // с этого момента лицо отпускает гримасу
    const uint32_t MIN_REMAIN_MS   = 1500;   // после nudge() остаётся минимум столько
    const uint32_t SHAKE_DELAY_MS  = 320;    // покачивание начинается после прищура
    const uint32_t GLANCE_START_MS = 1350;   // отвод глаз в сторону
    const uint32_t GLANCE_END_MS   = 2300;
    const uint32_t QMARK_DELAY_MS  = 420;    // задержка появления «?»
    const uint32_t QMARK_LIFE_MS   = 1050;   // время жизни «?»

    // ---------------- Поза ----------------
    const float SQUINT_W     = 54.0f;   // прищуренный глаз: приплюснутая капсула
    const float SQUINT_H     = 22.0f;
    const float SQUINT_DROP  = 6.0f;    // прищуренный глаз слегка опускается
    const float WIDE_W       = 46.0f;   // удивлённый глаз: выше и чуть уже
    const float WIDE_H       = 66.0f;
    const float WIDE_LIFT    = 8.0f;    // удивлённый глаз приподнимается
    const float LID_OUTER_PX = 11.0f;   // глубина века у внешнего угла
    const float LID_INNER_PX = 2.0f;    // глубина века у внутреннего угла
    const float SHAKE_AMP_PX = 8.0f;
    const float SHAKE_FREQ_HZ = 2.4f;
    const float SHAKE_DECAY_S = 0.75f;
    const float GLANCE_X_PX  = 13.0f;
    const float GLANCE_Y_PX  = 5.0f;

    // ---------------- Текущая геометрия ----------------
    float cx = CENTER_X, cy = BASE_Y;
    float currentSpacing = eyeSpacing;
    float lw = EYE_BASE_W, rw = EYE_BASE_W;
    float lh = EYE_BASE_H, rh = EYE_BASE_H;
    float mw = 20.0f, mh = 6.0f;
    float lyOff = 0.0f, ryOff = 0.0f;   // индивидуальные смещения глаз по Y
    float pose  = 0.0f;                 // 0 = нейтраль, 1 = полная гримаса

    // ---------------- Целевая геометрия ----------------
    float t_cx = CENTER_X, t_cy = BASE_Y;
    float t_spacing = eyeSpacing;
    float t_lw = EYE_BASE_W, t_rw = EYE_BASE_W;
    float t_lh = EYE_BASE_H, t_rh = EYE_BASE_H;
    float t_mw = 20.0f, t_mh = 6.0f;
    float t_lyOff = 0.0f, t_ryOff = 0.0f;
    float t_pose  = 0.0f;

    bool     squintRight  = false;  // какой глаз прищурен
    float    side         = -1.0f;  // -1 = удар слева, +1 = удар справа
    uint32_t startTime    = 0;
    uint32_t shakeStartMs = 0;
    uint32_t qmarkStartMs = 0;
    bool     finished     = false;
    bool     paletteReleased = false;

    // ---------------- Вспомогательные функции ----------------
    int ri(float v) {
        return (int)floorf(v + 0.5f);
    }

    float clamp01(float v) {
        if (v < 0.0f) return 0.0f;
        if (v > 1.0f) return 1.0f;
        return v;
    }

    float smoothstep01(float v) {
        v = clamp01(v);
        return v * v * (3.0f - 2.0f * v);
    }

    void applyDisdainPalette() {
        FaceState::setTargetPalette(255, 175, 20, 140, 60, 5, 50, 20, 0);
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

    // Толстая линия с круглыми концами (два треугольника + две точки)
    void drawThickLine(TFT_eSprite &s, float x0, float y0, float x1, float y1, float thick, uint16_t color) {
        float dx = x1 - x0;
        float dy = y1 - y0;
        float len = sqrtf(dx * dx + dy * dy);
        int r = ri(thick * 0.5f);
        if (len < 0.5f) {
            s.fillCircle(ri(x0), ri(y0), r, color);
            return;
        }
        float nx = -dy / len * thick * 0.5f;
        float ny =  dx / len * thick * 0.5f;
        int ax = ri(x0 + nx), ay = ri(y0 + ny);
        int bx = ri(x1 + nx), by = ri(y1 + ny);
        int cxp = ri(x1 - nx), cyp = ri(y1 - ny);
        int dxp = ri(x0 - nx), dyp = ri(y0 - ny);
        s.fillTriangle(ax, ay, bx, by, cxp, cyp, color);
        s.fillTriangle(ax, ay, cxp, cyp, dxp, dyp, color);
        s.fillCircle(ri(x0), ri(y0), r, color);
        s.fillCircle(ri(x1), ri(y1), r, color);
    }

    // Неоновая ломаная: три прохода (glow2 -> glow1 -> ядро), чтобы стыки были чистыми
    void drawNeonPolyline(TFT_eSprite &s, const float *xs, const float *ys, int n, float core, float glowStep) {
        for (int i = 0; i + 1 < n; i++) {
            drawThickLine(s, xs[i], ys[i], xs[i + 1], ys[i + 1], core + 2.0f * glowStep, colorGlow2);
        }
        for (int i = 0; i + 1 < n; i++) {
            drawThickLine(s, xs[i], ys[i], xs[i + 1], ys[i + 1], core + glowStep, colorGlow1);
        }
        for (int i = 0; i + 1 < n; i++) {
            drawThickLine(s, xs[i], ys[i], xs[i + 1], ys[i + 1], core, colorEye);
        }
    }

    // Прищуренный глаз: щель + скошенное веко (внешний угол закрыт сильнее).
    // dir = -1 для левого глаза, +1 для правого (внешняя сторона = ex + dir*...)
    void drawSquintEye(TFT_eSprite &sprite, float ex, float ey, float w, float h, float dir, float amount) {
        drawCapsule(sprite, ex, ey, w, h);
        if (amount < 0.03f) return;

        float halfW  = w * 0.5f;
        float top    = ey - h * 0.5f;
        float outerX = ex + dir * (halfW + 10.0f);
        float innerX = ex - dir * (halfW + 10.0f);
        float outerY = top + LID_OUTER_PX * amount;
        float innerY = top + LID_INNER_PX * amount;

        // Чёрная «шторка» века: всё, что выше наклонной линии, стирается (вместе с ореолом)
        int ceilY = ri(top - 50.0f);
        sprite.fillTriangle(ri(outerX), ri(outerY), ri(innerX), ri(innerY), ri(innerX), ceilY, TFT_BLACK);
        sprite.fillTriangle(ri(outerX), ri(outerY), ri(innerX), ceilY,      ri(outerX), ceilY, TFT_BLACK);

        // Неоновый край века — возвращает ореол по линии среза
        float k  = (innerY - outerY) / (innerX - outerX);
        float x0 = ex - halfW - 3.0f;
        float x1 = ex + halfW + 3.0f;
        float y0 = outerY + (x0 - outerX) * k;
        float y1 = outerY + (x1 - outerX) * k;
        float xs[2] = { x0, x1 };
        float ys[2] = { y0, y1 };
        drawNeonPolyline(sprite, xs, ys, 2, 3.0f, 3.0f);
    }

    // Удивлённый глаз: высокая капсула + приподнятая бровь «домиком»
    void drawWideEye(TFT_eSprite &sprite, float ex, float ey, float w, float h, float dir, float amount) {
        drawCapsule(sprite, ex, ey, w, h);
        if (amount < 0.03f) return;

        float top = ey - h * 0.5f;
        float by  = top - 7.0f - 9.0f * amount;   // бровь уезжает вверх по мере удивления
        float xs[3] = { ex + dir * 21.0f, ex - dir * 4.0f, ex - dir * 20.0f };
        float ys[3] = { by + 6.0f,        by - 3.0f,       by + 4.0f };
        drawNeonPolyline(sprite, xs, ys, 3, 4.0f, 3.0f);
    }

    // Рот: скептическая кривая черта. Уголок со стороны удара опущен, противоположный приподнят.
    void drawMouth(TFT_eSprite &sprite, float mx, float my, float amount) {
        float spread = 10.0f + 5.0f * amount;
        float leftDrop  = (side < 0.0f) ?  4.0f : -3.0f;
        float rightDrop = (side > 0.0f) ?  4.0f : -3.0f;
        float xs[3] = { mx - spread,              mx,                  mx + spread };
        float ys[3] = { my + leftDrop * amount,   my + 1.0f * amount,  my + rightDrop * amount };
        drawNeonPolyline(sprite, xs, ys, 3, 5.0f, 2.0f);
    }

    // Неоновый знак вопроса, нарисованный примитивами (не зависит от шрифтов)
    void drawQuestionMark(TFT_eSprite &sprite, float x, float y) {
        const int ARC_POINTS = 8;
        float xs[ARC_POINTS + 1];
        float ys[ARC_POINTS + 1];
        const float R   = 8.0f;
        float hcx = x;
        float hcy = y - 9.0f;

        // Дуга от 200° до -70° (крюк знака вопроса)
        for (int i = 0; i < ARC_POINTS; i++) {
            float deg = 200.0f - (270.0f * (float)i) / (float)(ARC_POINTS - 1);
            float a   = deg * PI_F / 180.0f;
            xs[i] = hcx + R * cosf(a);
            ys[i] = hcy - R * sinf(a);
        }
        // Ножка вниз
        xs[ARC_POINTS] = x;
        ys[ARC_POINTS] = y + 3.0f;
        drawNeonPolyline(sprite, xs, ys, ARC_POINTS + 1, 3.0f, 2.0f);

        // Точка
        sprite.fillCircle(ri(x), ri(y + 10.0f), 6, colorGlow2);
        sprite.fillCircle(ri(x), ri(y + 10.0f), 4, colorGlow1);
        sprite.fillCircle(ri(x), ri(y + 10.0f), 2, colorEye);
    }
} // anonymous namespace

void init(TFT_eSPI &tft) {
    (void)tft;
}

void setTapSide(float dirX) {
    if (dirX > 0.15f) {
        squintRight = true;
    } else if (dirX < -0.15f) {
        squintRight = false;
    }
    // при |dirX| <= 0.15 сохраняем прошлую сторону
    side = squintRight ? 1.0f : -1.0f;
}

void reset() {
    uint32_t now = millis();
    startTime    = now;
    shakeStartMs = now + SHAKE_DELAY_MS;
    qmarkStartMs = now;
    finished        = false;
    paletteReleased = false;

    FaceState::loadGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);
    applyDisdainPalette();

    lyOff = 0.0f;
    ryOff = 0.0f;
    pose  = 0.0f;

    t_cx = CENTER_X; t_cy = BASE_Y;
    t_spacing = eyeSpacing;
    t_lw = EYE_BASE_W; t_rw = EYE_BASE_W;
    t_lh = EYE_BASE_H; t_rh = EYE_BASE_H;
    t_mw = 20.0f;      t_mh = 6.0f;
    t_lyOff = 0.0f;    t_ryOff = 0.0f;
    t_pose = 1.0f;
}

void nudge() {
    uint32_t now = millis();
    shakeStartMs = now;
    qmarkStartMs = now - QMARK_DELAY_MS + 150;   // «?» появится через 150 мс
    uint32_t t = now - startTime;
    if (t > DURATION_MS - MIN_REMAIN_MS) {
        // продлеваем анимацию так, чтобы до конца осталось MIN_REMAIN_MS
        startTime = now - (DURATION_MS - MIN_REMAIN_MS);
        paletteReleased = false;
        applyDisdainPalette();
    }
}

bool isFinished() {
    return finished;
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    (void)tft;
    colorEye   = FaceState::current.colorEye;
    colorGlow1 = FaceState::current.colorGlow1;
    colorGlow2 = FaceState::current.colorGlow2;

    uint32_t now = millis();
    uint32_t t   = now - startTime;

    float imuX = 0.0f, imuY = 0.0f;
    if (ImuSensor::isReady()) {
        imuX = ImuSensor::getParallaxX() * 0.35f;
        imuY = ImuSensor::getParallaxY() * 0.25f;
    }
    if (imuX < -14.0f) imuX = -14.0f;
    if (imuX >  14.0f) imuX =  14.0f;
    if (imuY <  -8.0f) imuY =  -8.0f;
    if (imuY >   8.0f) imuY =   8.0f;

    // 1. Целевая поза
    if (t < RELEASE_MS) {
        t_pose = 1.0f;
        if (squintRight) {
            t_rw = SQUINT_W; t_rh = SQUINT_H; t_ryOff = SQUINT_DROP;
            t_lw = WIDE_W;   t_lh = WIDE_H;   t_lyOff = -WIDE_LIFT;
        } else {
            t_lw = SQUINT_W; t_lh = SQUINT_H; t_lyOff = SQUINT_DROP;
            t_rw = WIDE_W;   t_rh = WIDE_H;   t_ryOff = -WIDE_LIFT;
        }
    } else {
        t_pose = 0.0f;
        t_lw = EYE_BASE_W; t_rw = EYE_BASE_W;
        t_lh = EYE_BASE_H; t_rh = EYE_BASE_H;
        t_lyOff = 0.0f;    t_ryOff = 0.0f;
        if (!paletteReleased) {
            FaceState::setTargetPalette(50, 255, 100, 10, 120, 30, 0, 40, 10);
            paletteReleased = true;
        }
    }
    t_spacing = eyeSpacing;
    t_mw = 20.0f;
    t_mh = 6.0f;

    // 2. Демонстративный отвод глаз в сторону от обидчика
    float glance = 0.0f;
    if (t > GLANCE_START_MS && t < GLANCE_END_MS) {
        float up   = smoothstep01((float)(t - GLANCE_START_MS) / 250.0f);
        float down = 1.0f - smoothstep01(((float)t - (float)(GLANCE_END_MS - 300)) / 300.0f);
        glance = (up < down) ? up : down;
    }
    t_cx = CENTER_X - side * GLANCE_X_PX * glance;
    t_cy = BASE_Y - GLANCE_Y_PX * glance;

    // 3. Недоуменное покачивание головой с затуханием
    float shakeX = 0.0f;
    if (now >= shakeStartMs) {
        float ts = (float)(now - shakeStartMs) * 0.001f;
        shakeX = SHAKE_AMP_PX * sinf(2.0f * PI_F * SHAKE_FREQ_HZ * ts) * expf(-ts / SHAKE_DECAY_S);
    }

    // 4. Плавное движение к целям
    const float speed = 0.18f;
    cx += (t_cx + imuX - cx) * speed;
    cy += (t_cy + imuY - cy) * speed;
    currentSpacing += (t_spacing - currentSpacing) * speed;
    lw += (t_lw - lw) * speed; rw += (t_rw - rw) * speed;
    lh += (t_lh - lh) * speed; rh += (t_rh - rh) * speed;
    mw += (t_mw - mw) * speed; mh += (t_mh - mh) * speed;
    lyOff += (t_lyOff - lyOff) * speed;
    ryOff += (t_ryOff - ryOff) * speed;
    pose  += (t_pose - pose) * speed;

    float drawCx = cx + shakeX;
    FaceState::saveGeometry(drawCx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);

    float lx = drawCx - currentSpacing * 0.5f;
    float rx = drawCx + currentSpacing * 0.5f;

    // 5. Рисуем
    sprite.fillSprite(TFT_BLACK);

    if (squintRight) {
        drawWideEye(sprite, lx, cy + lyOff, lw, lh, -1.0f, pose);
        drawSquintEye(sprite, rx, cy + ryOff, rw, rh, 1.0f, pose);
    } else {
        drawSquintEye(sprite, lx, cy + lyOff, lw, lh, -1.0f, pose);
        drawWideEye(sprite, rx, cy + ryOff, rw, rh, 1.0f, pose);
    }

    drawMouth(sprite, drawCx, cy + 46.0f, pose);

    // 6. «?» над удивлённым глазом
    uint32_t tq = now - qmarkStartMs;
    if (tq >= QMARK_DELAY_MS && tq < QMARK_DELAY_MS + QMARK_LIFE_MS && t < RELEASE_MS) {
        float p     = (float)(tq - QMARK_DELAY_MS) / (float)QMARK_LIFE_MS;
        float popIn = smoothstep01(p / 0.15f);
        float wideX   = squintRight ? lx : rx;
        float wideTop = squintRight ? (cy + lyOff - lh * 0.5f) : (cy + ryOff - rh * 0.5f);
        float qx = wideX - side * 30.0f + 2.0f * sinf(p * 9.0f);
        float qy = wideTop - 36.0f + 10.0f * (1.0f - popIn) - 4.0f * p;
        drawQuestionMark(sprite, qx, qy);
    }

    if (t >= DURATION_MS) {
        finished = true;
    }

    sprite.pushSprite(0, 40);
}

} // namespace AnimDisdain
