#include "anim_sad.h"
#include "face_state.h"
#include <math.h>

namespace AnimSad {
namespace {

    // --- КОНСТАНТЫ И БАЗОВЫЕ РАЗМЕРЫ ---
    const float CENTER_X = 120.0f;
    const float BASE_Y = 135.0f;        // Опущенный вниз грустный взгляд
    const float EYE_SPACING = 100.0f;  // Чуть ближе к центру для уязвимого вида

    const float EYE_BASE_W = 48.0f;
    const float EYE_BASE_H = 58.0f;
    const int EYE_RADIUS = 18;

    // --- ЦВЕТА (565) ---
    uint16_t colorEye;
    uint16_t colorGlow1;
    uint16_t colorGlow2;

    uint16_t colorTearCore;
    uint16_t colorTearGlow1;
    uint16_t colorTearGlow2;

    uint16_t colorGloomCore;  // Цвет для значка уныния |||
    uint16_t colorGloomGlow1;
    uint16_t colorGloomGlow2;

    // --- ПЕРЕМЕННЫЕ ФИЗИКИ (Текущие и Целевые) ---
    float cx = CENTER_X, cy = BASE_Y;
    float currentSpacing = EYE_SPACING;
    float lx = 110.0f, rx = 210.0f;
    float lw = EYE_BASE_W, rw = EYE_BASE_W;
    float lh = EYE_BASE_H, rh = EYE_BASE_H;
    float mw = 24.0f, mh = 6.0f;

    float t_cx = CENTER_X, t_cy = BASE_Y;
    float t_spacing = EYE_SPACING;
    float t_lw = EYE_BASE_W, t_rw = EYE_BASE_W;
    float t_lh = EYE_BASE_H, t_rh = EYE_BASE_H;
    float t_mw = 24.0f, t_mh = 6.0f;
    
    float browSlant = 0.0f;
    float t_browSlant = 16.0f;

    // --- ТАЙМЕРЫ СОСТОЯНИЙ (Моргание и Вздохи) ---
    uint32_t lastBlinkTime = 0;
    uint32_t nextBlinkDelay = 4000;
    bool isBlinking = false;
    uint32_t blinkStartTime = 0;

    // Фазы вздоха: 0 - грустит внизу, 1 - медленный вдох (вверх), 2 - тяжелый выдох (вниз)
    int sighPhase = 0;
    uint32_t sighTimer = 0;

    // --- СИСТЕМА ЧАСТИЦ: СЛЕЗЫ ---
    struct Tear {
        bool active;
        bool isFalling; // false = копится в глазу, true = сорвалась и летит вниз
        float x, y;
        float vy;
        float size;
        float maxSize;
        bool fromLeftEye;
    };

    const int MAX_TEARS = 4;
    Tear tears[MAX_TEARS];
    uint32_t lastTearSpawn = 0;
    bool nextTearSide = true;

    // --- ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ РЕНДЕРИНГА ---

    // 1. Глаз с 3-слойным свечением + влажный блик во ВНУТРЕННЕЙ части глаза
    void drawGlowingEye(TFT_eSprite &sprite, int x, int y, int w, int h, int r, bool isLeft, uint32_t now) {
        if (h < 4) h = 4;
        int rCore = r;
        if (rCore > h / 2) rCore = h / 2;
        if (rCore > w / 2) rCore = w / 2;

        // Слой 2 (Внешнее свечение)
        sprite.fillRoundRect(x - w / 2 - 3, y - h / 2 - 3, w + 6, h + 6, rCore + 3, colorGlow2);
        // Слой 1 (Среднее свечение)
        sprite.fillRoundRect(x - w / 2 - 1, y - h / 2 - 1, w + 2, h + 2, rCore + 1, colorGlow1);
        // Ядро
        sprite.fillRoundRect(x - w / 2, y - h / 2, w, h, rCore, colorEye);

        // ДЕТАЛЬ: "Мокрые глаза" (дрожащий водяной блик во ВНУТРЕННЕЙ части глаза, ближе к центру лица)
        if (h > 25) {
            float shimmer = sin(now * 0.015f + (isLeft ? 0 : 2)) * 1.5f;
            int gx = isLeft ? (x + w / 2 - 11) : (x - w / 2 + 11);
            int gy = y + h / 2 - 11;
            int gr = 4 + (int)(shimmer * 0.5f);

            sprite.fillCircle(gx, gy, gr + 2, colorTearGlow1);
            sprite.fillCircle(gx, gy, gr, colorTearCore);
            
            int gx2 = isLeft ? (gx - 7) : (gx + 7);
            sprite.fillCircle(gx2, gy + 3, 2, colorTearCore);
        }
    }

    // 2. Срез глаз "Грустный домик" (/ \) и отрисовка бровей (БЕЗ РАЗРЫВОВ)
    void drawSadBrows(TFT_eSprite &sprite, int x, int y, int w, int h, float slant, bool isLeft) {
        int topY = y - h / 2 - 8;
        int innerX = isLeft ? (x + w / 2 + 6) : (x - w / 2 - 6);
        int outerX = isLeft ? (x - w / 2 - 6) : (x + w / 2 + 6);

        int innerY = topY + 2;
        int outerY = topY + 8 + (int)slant;

        // Маскируем внешний верхний угол черным треугольником
        sprite.fillTriangle(innerX, topY, outerX, topY, outerX, outerY, TFT_BLACK);

        // Рисуем светящуюся бровь поверх линии среза
        int bx1 = isLeft ? (x + w / 2 + 2) : (x - w / 2 - 2);
        int by1 = y - h / 2 - 2;
        int bx2 = isLeft ? (x - w / 2 - 4) : (x + w / 2 + 4);
        int by2 = by1 + (int)slant;

        const int steps = 40;
        
        // Pass 1: Glow 2
        for (int i = 0; i <= steps; i++) {
            float t = (float)i / steps;
            float px = bx1 + (bx2 - bx1) * t;
            float py = by1 + (by2 - by1) * t - sin(t * PI) * 2.5f;
            sprite.fillCircle((int)px, (int)py, 6, colorGlow2);
        }
        // Pass 2: Glow 1
        for (int i = 0; i <= steps; i++) {
            float t = (float)i / steps;
            float px = bx1 + (bx2 - bx1) * t;
            float py = by1 + (by2 - by1) * t - sin(t * PI) * 2.5f;
            sprite.fillCircle((int)px, (int)py, 4, colorGlow1);
        }
        // Pass 3: Core
        for (int i = 0; i <= steps; i++) {
            float t = (float)i / steps;
            float px = bx1 + (bx2 - bx1) * t;
            float py = by1 + (by2 - by1) * t - sin(t * PI) * 2.5f;
            sprite.fillCircle((int)px, (int)py, 2, colorEye);
        }
    }

    // 3. ЗНАЧОК УНЫНИЯ В УГЛУ ПРАВОГО ГЛАЗА: Аниме-линии подавленности (|||)
    void drawGloomMark(TFT_eSprite &sprite, int eyeRightX, int eyeTopY, uint32_t now) {
        int baseX = eyeRightX + 12;
        int baseY = eyeTopY - 6 + (int)(sin(now * 0.004f) * 3.0f);

        int lineHeights[3] = {12, 19, 14};
        
        // Pass 1: Glow 2
        for (int i = 0; i < 3; i++) {
            int lx_i = baseX + i * 7;
            int waveY = (int)(sin(now * 0.006f + i * 1.2f) * 2.5f);
            int ly_i = baseY + waveY + (i == 1 ? -3 : 0);
            sprite.fillRoundRect(lx_i - 3, ly_i - 3, 8, lineHeights[i] + 6, 4, colorGloomGlow2);
        }
        // Pass 2: Glow 1
        for (int i = 0; i < 3; i++) {
            int lx_i = baseX + i * 7;
            int waveY = (int)(sin(now * 0.006f + i * 1.2f) * 2.5f);
            int ly_i = baseY + waveY + (i == 1 ? -3 : 0);
            sprite.fillRoundRect(lx_i - 1, ly_i - 1, 4, lineHeights[i] + 2, 2, colorGloomGlow1);
        }
        // Pass 3: Core
        for (int i = 0; i < 3; i++) {
            int lx_i = baseX + i * 7;
            int waveY = (int)(sin(now * 0.006f + i * 1.2f) * 2.5f);
            int ly_i = baseY + waveY + (i == 1 ? -3 : 0);
            sprite.fillRoundRect(lx_i, ly_i, 2, lineHeights[i], 1, colorGloomCore);
        }
    }

    // 4. Дрожащий грустный рот скобочкой вниз (БЕЗ РАЗРЫВОВ)
    void drawSadMouth(TFT_eSprite &sprite, float mx, float my, float w, float h, float quiver) {
        const int steps = 32;

        // Pass 1: Glow 2
        for (int i = 0; i <= steps; i++) {
            float t = (float)i / steps * 2.0f - 1.0f;
            float px = mx + t * (w / 2.0f);
            float py = my + (t * t) * h + sin(t * 6.0f) * quiver;
            sprite.fillCircle((int)px, (int)py, 6, colorGlow2);
        }
        // Pass 2: Glow 1
        for (int i = 0; i <= steps; i++) {
            float t = (float)i / steps * 2.0f - 1.0f;
            float px = mx + t * (w / 2.0f);
            float py = my + (t * t) * h + sin(t * 6.0f) * quiver;
            sprite.fillCircle((int)px, (int)py, 4, colorGlow1);
        }
        // Pass 3: Core
        for (int i = 0; i <= steps; i++) {
            float t = (float)i / steps * 2.0f - 1.0f;
            float px = mx + t * (w / 2.0f);
            float py = my + (t * t) * h + sin(t * 6.0f) * quiver;
            sprite.fillCircle((int)px, (int)py, 2, colorEye);
        }
    }

    // 5. Отрисовка и физика крупных слез из глаз
    void updateAndDrawTears(TFT_eSprite &sprite, uint32_t now) {
        if (now - lastTearSpawn > 1600) {
            for (int i = 0; i < MAX_TEARS; i++) {
                if (!tears[i].active) {
                    tears[i].active = true;
                    tears[i].isFalling = false;
                    tears[i].fromLeftEye = nextTearSide;
                    nextTearSide = !nextTearSide;
                    tears[i].size = 1.0f;
                    tears[i].maxSize = 4.5f + (random(0, 15) / 10.0f);
                    tears[i].vy = 0.0f;
                    lastTearSpawn = now;
                    break;
                }
            }
        }

        for (int i = 0; i < MAX_TEARS; i++) {
            if (!tears[i].active) continue;

            if (!tears[i].isFalling) {
                float eyeX = tears[i].fromLeftEye ? lx : rx;
                float eyeW = tears[i].fromLeftEye ? lw : rw;
                float eyeH = tears[i].fromLeftEye ? lh : rh;

                float wobble = sin(now * 0.025f + i) * 0.8f;
                tears[i].x = eyeX + (tears[i].fromLeftEye ? (-eyeW / 2.0f + 7.0f) : (eyeW / 2.0f - 7.0f)) + wobble;
                tears[i].y = cy + eyeH / 2.0f - 3.0f;

                tears[i].size += 0.055f;
                if (tears[i].size >= tears[i].maxSize) {
                    tears[i].isFalling = true;
                }
            } else {
                tears[i].vy += 0.22f;
                tears[i].y += tears[i].vy;

                if (tears[i].y > 185.0f) {
                    tears[i].active = false;
                    continue;
                }
            }

            int tx = (int)tears[i].x;
            int ty = (int)tears[i].y;
            int r = (int)tears[i].size;
            if (r < 1) r = 1;

            int tailHeight = tears[i].isFalling ? (r * 2 + 2) : 0;

            sprite.fillCircle(tx, ty, r + 3, colorTearGlow2);
            if (tears[i].isFalling) {
                sprite.fillTriangle(tx - r - 2, ty, tx + r + 2, ty, tx, ty - tailHeight - 3, colorTearGlow2);
            }

            sprite.fillCircle(tx, ty, r + 1, colorTearGlow1);
            if (tears[i].isFalling) {
                sprite.fillTriangle(tx - r, ty, tx + r, ty, tx, ty - tailHeight - 1, colorTearGlow1);
            }

            sprite.fillCircle(tx, ty, r, colorTearCore);
            if (tears[i].isFalling) {
                sprite.fillTriangle(tx - r + 1, ty, tx + r - 1, ty, tx, ty - tailHeight + 1, colorTearCore);
            }
        }
    }

} // конец анонимного namespace

void init(TFT_eSPI &tft) {
    colorEye   = tft.color565(50, 255, 100);
    colorGlow1 = tft.color565(10, 120, 30);
    colorGlow2 = tft.color565(0, 40, 10);

    colorTearCore  = tft.color565(190, 255, 210);
    colorTearGlow1 = tft.color565(30, 180, 70);
    colorTearGlow2 = tft.color565(0, 60, 20);

    colorGloomCore  = tft.color565(40, 220, 90);
    colorGloomGlow1 = tft.color565(5, 90, 25);
    colorGloomGlow2 = tft.color565(0, 30, 8);

    reset();
}

void reset() {
    // 1. Подхватываем текущую геометрию из предыдущей эмоции (например, из ANGER)!
    FaceState::loadGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);

    // 2. Плавно переливаем цвет обратно в неоново-зеленый
    FaceState::setTargetPalette(50, 255, 100, 10, 120, 30, 0, 40, 10);

    t_cx = CENTER_X;
    t_cy = BASE_Y;
    t_spacing = EYE_SPACING;

    t_lw = t_rw = EYE_BASE_W;
    t_lh = t_rh = EYE_BASE_H;

    t_mw = 24.0f;
    t_mh = 6.0f;

    browSlant = 0.0f; // Брови плавно опускаются в грустный домик
    t_browSlant = 16.0f;

    lx = cx - currentSpacing / 2.0f;
    rx = cx + currentSpacing / 2.0f;

    uint32_t now = millis();
    lastBlinkTime = now;
    sighTimer = now;
    sighPhase = 0;
    lastTearSpawn = now - 600;

    for (int i = 0; i < MAX_TEARS; i++) {
        tears[i].active = false;
    }
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    uint32_t now = millis();

    // Берем плавно интерполируемый цвет из FaceState
    colorEye   = FaceState::current.colorEye;
    colorGlow1 = FaceState::current.colorGlow1;
    colorGlow2 = FaceState::current.colorGlow2;

    // 1. ЛОГИКА ТЯЖЕЛЫХ ВЗДОХОВ (Sighing)
    if (sighPhase == 0) {
        t_cy = BASE_Y + sin(now * 0.002f) * 2.0f;
        t_lh = t_rh = EYE_BASE_H;
        t_lw = t_rw = EYE_BASE_W;
        t_browSlant = 16.0f;
        t_mw = 24.0f;
        t_mh = 6.0f;

        if (now - sighTimer > 5000) {
            sighPhase = 1;
            sighTimer = now;
        }
    } else if (sighPhase == 1) {
        t_cy = BASE_Y - 12.0f;
        t_lh = t_rh = EYE_BASE_H + 6.0f;
        t_lw = t_rw = EYE_BASE_W + 2.0f;
        t_browSlant = 12.0f;
        t_mw = 18.0f;
        t_mh = 4.0f;

        if (now - sighTimer > 1300) {
            sighPhase = 2;
            sighTimer = now;
        }
    } else if (sighPhase == 2) {
        t_cy = BASE_Y + 9.0f;
        t_lh = t_rh = EYE_BASE_H - 8.0f;
        t_lw = t_rw = EYE_BASE_W - 2.0f;
        t_browSlant = 20.0f;
        t_mw = 28.0f;
        t_mh = 8.0f;

        if (now - sighTimer > 2200) {
            sighPhase = 0;
            sighTimer = now;
        }
    }

    t_cx = CENTER_X + cos(now * 0.0012f) * 3.0f;

    // 2. ЛОГИКА МЕДЛЕННОГО МОРГАНИЯ
    if (!isBlinking && (now - lastBlinkTime > nextBlinkDelay)) {
        isBlinking = true;
        blinkStartTime = now;
        nextBlinkDelay = random(3500, 6500);
    }

    if (isBlinking) {
        uint32_t blinkElapsed = now - blinkStartTime;
        if (blinkElapsed < 160) {
            t_lh = t_rh = 6.0f;
        } else if (blinkElapsed >= 320) {
            isBlinking = false;
            lastBlinkTime = now;
        }
    }

    // 3. ФИЗИКА (Пружина)
    float moveSpeed = (sighPhase == 1) ? 0.04f : 0.12f;
    cx += (t_cx - cx) * moveSpeed;
    cy += (t_cy - cy) * moveSpeed;
    currentSpacing += (t_spacing - currentSpacing) * 0.12f;

    float eyeMorphSpeed = isBlinking ? 0.25f : 0.14f;
    lw += (t_lw - lw) * eyeMorphSpeed;
    rw += (t_rw - rw) * eyeMorphSpeed;
    lh += (t_lh - lh) * eyeMorphSpeed;
    rh += (t_rh - rh) * eyeMorphSpeed;

    mw += (t_mw - mw) * 0.12f;
    mh += (t_mh - mh) * 0.12f;
    browSlant += (t_browSlant - browSlant) * 0.12f;

    // Сохраняем актуальную геометрию в FaceState
    FaceState::saveGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);

    float faceQuiverX = 0.0f;
    float mouthQuiver = 0.3f;
    if (sighPhase == 2 || cy > BASE_Y + 4.0f) {
        faceQuiverX = sin(now * 0.055f) * 1.2f;
        mouthQuiver = 1.1f * sin(now * 0.06f);
    }

    lx = cx - currentSpacing / 2.0f + faceQuiverX;
    rx = cx + currentSpacing / 2.0f + faceQuiverX;

    // 4. РЕНДЕРИНГ В СПРАЙТ
    sprite.fillSprite(TFT_BLACK);

    drawGlowingEye(sprite, (int)lx, (int)cy, (int)lw, (int)lh, EYE_RADIUS, true, now);
    drawGlowingEye(sprite, (int)rx, (int)cy, (int)rw, (int)rh, EYE_RADIUS, false, now);

    drawSadBrows(sprite, (int)lx, (int)cy, (int)lw, (int)lh, browSlant, true);
    drawSadBrows(sprite, (int)rx, (int)cy, (int)rw, (int)rh, browSlant, false);

    drawGloomMark(sprite, (int)(rx + rw / 2), (int)(cy - rh / 2), now);

    float mouthY = cy + 44.0f;
    drawSadMouth(sprite, cx + faceQuiverX, mouthY, mw, mh, mouthQuiver);

    updateAndDrawTears(sprite, now);

    sprite.pushSprite(0, 40);
}

} // namespace AnimSad
