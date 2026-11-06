#include "anim_joy.h"
#include "face_state.h"
#include "sensors/imu_sensor.h"
#include <Arduino.h>
#include <math.h>

namespace AnimJoy {

namespace {
    // Цвета лица (фирменный неоново-зеленый стиль)
    uint16_t colorEye;
    uint16_t colorGlow1;
    uint16_t colorGlow2;

    // Цвета аниме-эффектов (красные сердечки и румянец)
    uint16_t colorHeartCore;
    uint16_t colorHeartGlow;
    uint16_t colorBlush;
    uint16_t colorBlushGlow;

    // Базовые параметры геометрии (спрайт 240x200, базовый Y = 100)
    const int eyeBaseW = 46;
    const int eyeBaseH = 68;
    const float eyeSpacing = 104.0f;
    const float CENTER_X = 120.0f;
    const float BASE_Y = 100.0f;

    // Текущие координаты физики
    float cx = CENTER_X, cy = BASE_Y;
    float currentSpacing = eyeSpacing;
    float lw = eyeBaseW, rw = eyeBaseW;
    float lh = eyeBaseH, rh = eyeBaseH;
    float mw = 22.0f, mh = 6.0f;
    float happySquint = 0.0f; // 0.0 = обычные глаза, 1.0 = острые дуги ^ ^
    float browOffsetY = 0.0f;

    // Целевые координаты
    float t_cx = CENTER_X, t_cy = BASE_Y;
    float t_spacing = eyeSpacing;
    float t_lw = eyeBaseW, t_rw = eyeBaseW;
    float t_lh = eyeBaseH, t_rh = eyeBaseH;
    float t_mw = 22.0f, t_mh = 6.0f;
    float t_happySquint = 0.0f;
    float t_browOffsetY = 0.0f;

    uint32_t lastActionTime = 0;
    int joyPhase = 0;

    // --- СИСТЕМА ЧАСТИЦ: КРАСНЫЕ СЕРДЕЧКИ ---
    struct HeartParticle {
        float x, startX;
        float y;
        float size;
        float speedY;
        float phase;
        bool active;
    };
    const int MAX_HEARTS = 7;
    HeartParticle hearts[MAX_HEARTS];
    uint32_t lastHeartSpawn = 0;

    // Процедурное векторное сердечко с бликом
    void drawVectorHeart(TFT_eSprite &s, float hx, float hy, float size, uint16_t colCore, uint16_t colGlow) {
        int r = (int)(size * 0.5f);
        if (r < 2) r = 2;
        int hW = r * 2;
        int hH = (int)(size * 1.6f);

        // Внешнее свечение сердечка
        s.fillCircle((int)(hx - r), (int)(hy - r * 0.5f), r + 2, colGlow);
        s.fillCircle((int)(hx + r), (int)(hy - r * 0.5f), r + 2, colGlow);
        s.fillTriangle((int)(hx - hW - 2), (int)(hy - r * 0.5f),
                       (int)(hx + hW + 2), (int)(hy - r * 0.5f),
                       (int)hx, (int)(hy + hH + 2), colGlow);

        // Ярко-красное ядро
        s.fillCircle((int)(hx - r), (int)(hy - r * 0.5f), r, colCore);
        s.fillCircle((int)(hx + r), (int)(hy - r * 0.5f), r, colCore);
        s.fillTriangle((int)(hx - hW), (int)(hy - r * 0.5f),
                       (int)(hx + hW), (int)(hy - r * 0.5f),
                       (int)hx, (int)(hy + hH), colCore);

        // Аниме-блик
        if (size >= 5.0f) {
            s.fillCircle((int)(hx - r * 0.8f), (int)(hy - r * 0.8f), 1, TFT_WHITE);
        }
    }

    // Острые аниме-глаза (^ ^) с ресничками
    void drawAnimeEye(TFT_eSprite &s, float x, float y, float w, float h, float squint, bool isRightEye) {
        float archHeight = 14.0f * squint;
        float strokeR = (w * 0.38f) - (13.0f * squint);
        float spanX = (w * 0.5f) - strokeR + (6.0f * squint);
        float stretchY = (h * 0.5f - strokeR) * (1.0f - squint);
        if (stretchY < 0.0f) stretchY = 0.0f;

        // Плотный шаг для гладких граней без пропусков
        for (int layer = 0; layer < 3; layer++) {
            uint16_t col = (layer == 0) ? colorGlow2 : ((layer == 1) ? colorGlow1 : colorEye);
            int r = (int)strokeR + (layer == 0 ? 5 : (layer == 1 ? 2 : 0));

            for (float t = -1.0f; t <= 1.01f; t += 0.08f) {
                float px = x + t * spanX;
                // fabsf(t) формирует чёткий острый шеврон в центре
                float peak = (1.0f - fabsf(t));
                float py = y - (peak - 0.35f) * archHeight;

                if (stretchY < 1.0f) {
                    s.fillCircle((int)px, (int)py, r, col);
                } else {
                    s.fillRoundRect((int)(px - r), (int)(py - stretchY - r), r * 2, (int)(stretchY * 2 + r * 2), r, col);
                }
            }
        }

        // Аниме-реснички на внешнем крае глаза
        if (squint > 0.45f) {
            float lashX = isRightEye ? (x + spanX + 1.0f) : (x - spanX - 1.0f);
            float lashY = y + 0.35f * archHeight;
            float dir = isRightEye ? 1.0f : -1.0f;

            s.drawLine((int)lashX, (int)lashY, (int)(lashX + dir * 6.0f), (int)(lashY - 4.0f), colorEye);
            s.drawLine((int)lashX, (int)(lashY + 1), (int)(lashX + dir * 6.0f), (int)(lashY - 3.0f), colorEye);
            s.drawLine((int)lashX, (int)(lashY + 4.0f), (int)(lashX + dir * 4.0f), (int)(lashY + 1.0f), colorGlow1);
        }
    }

    // Гладкий сплошной кошачий аниме-ротик (:3 / ω) без сегментации
    void drawCatMouth(TFT_eSprite &s, float mx, float my, float w, float squint) {
        float halfW = (w * 0.5f) * (0.85f + squint * 0.35f);
        float arch  = 4.5f * (0.5f + squint * 0.5f);

        const int steps = 24;

        // 1. Левая дуга (:3)
        int prevLx = (int)(mx - halfW);
        int prevLy = (int)my;

        for (int i = 1; i <= steps; i++) {
            float t = -1.0f + ((float)i / (float)steps);
            float norm = (t + 0.5f) * 2.0f;
            int curX = (int)(mx + t * halfW);
            int curY = (int)(my + (1.0f - norm * norm) * arch);

            for (int dy = -1; dy <= 2; dy++) {
                s.drawLine(prevLx, prevLy + dy, curX, curY + dy, colorGlow1);
            }
            s.drawLine(prevLx, prevLy, curX, curY, colorEye);
            s.drawLine(prevLx, prevLy + 1, curX, curY + 1, colorEye);

            prevLx = curX;
            prevLy = curY;
        }

        // 2. Правая дуга (:3)
        int prevRx = (int)mx;
        int prevRy = (int)my;

        for (int i = 1; i <= steps; i++) {
            float t = ((float)i / (float)steps);
            float norm = (t - 0.5f) * 2.0f;
            int curX = (int)(mx + t * halfW);
            int curY = (int)(my + (1.0f - norm * norm) * arch);

            for (int dy = -1; dy <= 2; dy++) {
                s.drawLine(prevRx, prevRy + dy, curX, curY + dy, colorGlow1);
            }
            s.drawLine(prevRx, prevRy, curX, curY, colorEye);
            s.drawLine(prevRx, prevRy + 1, curX, curY + 1, colorEye);

            prevRx = curX;
            prevRy = curY;
        }

        // Центральный стык
        s.fillCircle((int)mx, (int)my, 1, colorEye);
    }

} // anonymous namespace

void init(TFT_eSPI &tft) {
    // Фирменный неоново-зеленый цвет робота
    colorEye         = tft.color565(50, 255, 100);
    colorGlow1       = tft.color565(10, 120, 30);
    colorGlow2       = tft.color565(0, 40, 10);

    // Красные сердечки
    colorHeartCore   = tft.color565(255, 25, 45);
    colorHeartGlow   = tft.color565(110, 10, 20);

    // Аниме-румянец
    colorBlush       = tft.color565(255, 80, 120);
    colorBlushGlow   = tft.color565(75, 15, 30);

    for (int i = 0; i < MAX_HEARTS; i++) {
        hearts[i].active = false;
    }
}

void reset() {
    lastActionTime = millis();
    joyPhase = 0;

    FaceState::loadGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);
    // Целевая палитра сохраняет фирменный зелёный цвет лица
    FaceState::setTargetPalette(50, 255, 100, 10, 120, 30, 0, 40, 10);

    happySquint = 0.0f;
    browOffsetY = 0.0f;

    t_cx = CENTER_X;
    t_cy = BASE_Y - 4.0f;
    t_spacing = eyeSpacing;
    t_lw = eyeBaseW;      t_rw = eyeBaseW;
    t_lh = eyeBaseH + 2;  t_rh = eyeBaseH + 2;
    t_mw = 22.0f;         t_mh = 6.0f;
    t_happySquint = 0.0f;
    t_browOffsetY = -5.0f;

    for (int i = 0; i < MAX_HEARTS; i++) {
        hearts[i].active = false;
    }
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    uint32_t now = millis();

    colorEye   = FaceState::current.colorEye;
    colorGlow1 = FaceState::current.colorGlow1;
    colorGlow2 = FaceState::current.colorGlow2;

    // 1. Дыхание и микро-пульсация мурлыканья
    float breathY = sinf(now * 0.009f) * 3.0f;
    float purrPulse = sinf(now * 0.022f) * 1.5f;

    // 2. Фазы эмоции
    if (joyPhase == 0 && (now - lastActionTime > 150)) {
        joyPhase = 1;
        lastActionTime = now;
        t_cy = BASE_Y - 6.0f;
        t_mw = 28.0f;
        t_happySquint = 1.0f; // Переход в острые аниме-дуги ^ ^
        t_browOffsetY = -4.0f;
    } else if (joyPhase == 1 && (now - lastActionTime > random(1400, 2400))) {
        joyPhase = 0;
        lastActionTime = now;
        t_cy = BASE_Y - 2.0f;
        t_mw = 24.0f;
        t_happySquint = 0.85f;
    }

    // 3. Спавн красных сердечек
    if (now - lastHeartSpawn > 220) {
        lastHeartSpawn = now;
        for (int i = 0; i < MAX_HEARTS; i++) {
            if (!hearts[i].active) {
                hearts[i].active = true;
                int side = (random(0, 2) == 0) ? -1 : 1;
                hearts[i].startX = cx + side * (eyeSpacing * 0.5f + random(-15, 20));
                hearts[i].x = hearts[i].startX;
                hearts[i].y = cy + 20.0f + random(0, 15);
                hearts[i].size = random(5, 9);
                hearts[i].speedY = random(11, 20) / 10.0f;
                hearts[i].phase = random(0, 100) / 10.0f;
                break;
            }
        }
    }

    // 4. Параллакс от наклона брелка
    float imuX = ImuSensor::getParallaxX() * 0.35f;
    float imuY = ImuSensor::getParallaxY() * 0.25f;
    if (imuX < -14.0f) imuX = -14.0f;
    if (imuX >  14.0f) imuX =  14.0f;
    if (imuY <  -8.0f) imuY =  -8.0f;
    if (imuY >   8.0f) imuY =   8.0f;

    float targetX = t_cx + imuX;
    float targetY = t_cy + breathY + imuY;

    // 5. Пружинная физика
    float speed = 0.16f;
    cx += (targetX - cx) * speed;
    cy += (targetY - cy) * speed;
    currentSpacing += (t_spacing - currentSpacing) * speed;

    lw += (t_lw - lw) * speed;
    rw += (t_rw - rw) * speed;
    lh += (t_lh - lh) * speed;
    rh += (t_rh - rh) * speed;
    mw += (t_mw - mw) * 0.14f;
    mh += (t_mh - mh) * 0.14f;
    happySquint += (t_happySquint - happySquint) * 0.14f;
    browOffsetY += (t_browOffsetY - browOffsetY) * 0.15f;

    // Сохраняем геометрию в FaceState
    float effLh = lh * (1.0f - happySquint * 0.80f);
    float effRh = rh * (1.0f - happySquint * 0.80f);
    FaceState::saveGeometry(cx, cy, currentSpacing, lw, rw, effLh, effRh, mw, mh);

    float lx = cx - currentSpacing * 0.5f;
    float rx = cx + currentSpacing * 0.5f;

    // 6. Отрисовка
    sprite.fillSprite(TFT_BLACK);

    // 6.1 Бровки
    int browY = (int)(cy - 27.0f + (happySquint * 5.0f) + browOffsetY);
    for (int t = 0; t < 2; t++) {
        sprite.drawLine((int)(lx - 12), browY + 2 + t, (int)(lx + 8), browY - 2 + t, colorEye);
        sprite.drawLine((int)(rx - 8), browY - 2 + t, (int)(rx + 12), browY + 2 + t, colorEye);
    }

    // 6.2 Острые аниме-глаза (с ресничками)
    drawAnimeEye(sprite, lx, cy, lw, lh + purrPulse, happySquint, false);
    drawAnimeEye(sprite, rx, cy, rw, rh + purrPulse, happySquint, true);

    // 6.3 Манга-румянец (Anime Blush)
    if (happySquint > 0.30f) {
        int blushY = (int)(cy + 22.0f);
        int leftBlushX = (int)(lx - 15.0f);
        int rightBlushX = (int)(rx - 1.0f);

        sprite.fillRoundRect(leftBlushX - 2, blushY - 1, 24, 12, 5, colorBlushGlow);
        sprite.fillRoundRect(rightBlushX - 2, blushY - 1, 24, 12, 5, colorBlushGlow);

        for (int i = 0; i < 3; i++) {
            int bxL = leftBlushX + (i * 7);
            int bxR = rightBlushX + (i * 7);
            for (int w = 0; w < 2; w++) {
                sprite.drawLine(bxL + w, blushY + 8, bxL + 5 + w, blushY + 1, colorBlush);
                sprite.drawLine(bxR + w, blushY + 8, bxR + 5 + w, blushY + 1, colorBlush);
            }
        }
    }

    // 6.4 Кошачий аниме-ротик (:3)
    drawCatMouth(sprite, cx, cy + 39.0f, mw, happySquint);

    // 6.5 Всплывающие красные сердечки
    for (int i = 0; i < MAX_HEARTS; i++) {
        if (hearts[i].active) {
            hearts[i].y -= hearts[i].speedY;
            hearts[i].x = hearts[i].startX + sinf(now * 0.007f + hearts[i].phase) * 7.0f;
            hearts[i].size -= 0.08f;

            if (hearts[i].size <= 1.5f || hearts[i].y < 5.0f) {
                hearts[i].active = false;
            } else {
                drawVectorHeart(sprite, hearts[i].x, hearts[i].y, hearts[i].size, colorHeartCore, colorHeartGlow);
            }
        }
    }

    sprite.pushSprite(0, 40);
}

} // namespace AnimJoy
