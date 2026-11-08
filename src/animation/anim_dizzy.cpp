#include "anim_dizzy.h"
#include "face_state.h"
#include <math.h>

namespace AnimDizzy {

namespace {
    // --- Константы базовой геометрии ---
    const float CENTER_X = 120.0f;
    const float BASE_Y = 100.0f;
    const float EYE_SPACING = 106.0f;

    // Палитра лица (Токсично-зеленовато-желтый неон)
    uint16_t colorEye;
    uint16_t colorGlow1;
    uint16_t colorGlow2;

    // Палитра Радуги (6 цветов Nyan Cat)
    uint16_t rainbowColors[6];
    uint16_t rainbowGlow;
    uint16_t colorSparkle;

    // Переменные физики лица
    float cx = CENTER_X, cy = BASE_Y;
    float currentSpacing = EYE_SPACING;
    float lx = CENTER_X - EYE_SPACING / 2.0f, rx = CENTER_X + EYE_SPACING / 2.0f;
    float lw = 52.0f, rw = 52.0f, lh = 52.0f, rh = 52.0f;
    float mw = 28.0f, mh = 6.0f;

    float t_cx = CENTER_X, t_cy = BASE_Y;
    float t_spacing = EYE_SPACING;
    float t_lw = 54.0f, t_rw = 54.0f, t_lh = 54.0f, t_rh = 54.0f;
    float t_mw = 28.0f, t_mh = 6.0f;

    // Переменные анимации
    float eyeOffsetY = 0.0f;
    float pukeProgress = 0.0f;
    float puddleScale = 0.0f;
    uint32_t animStartTime = 0;
    uint32_t lastStepTime = 0;

    // --- Система частиц радуги ---
    struct Particle {
        float x, y;
        float vx, vy;
        uint16_t color;
        uint8_t type;
        bool active;
    };
    const int MAX_PARTICLES = 16;
    Particle particles[MAX_PARTICLES];

    // Рисование 4-конечной звездочки (эффект оглушения / головокружения)
    void drawCartoonStar(TFT_eSprite &sprite, int sx, int sy, int r, uint16_t colorCore, uint16_t colorGlow) {
        if (r < 2) return;
        sprite.drawFastHLine(sx - r - 1, sy, (r + 1) * 2 + 1, colorGlow);
        sprite.drawFastVLine(sx, sy - r - 1, (r + 1) * 2 + 1, colorGlow);
        sprite.drawLine(sx - r/2 - 1, sy - r/2 - 1, sx + r/2 + 1, sy + r/2 + 1, colorGlow);
        sprite.drawLine(sx - r/2 - 1, sy + r/2 + 1, sx + r/2 + 1, sy - r/2 - 1, colorGlow);

        sprite.drawFastHLine(sx - r, sy, r * 2 + 1, colorCore);
        sprite.drawFastVLine(sx, sy - r, r * 2 + 1, colorCore);
        sprite.drawLine(sx - r/2, sy - r/2, sx + r/2, sy + r/2, colorCore);
        sprite.drawLine(sx - r/2, sy + r/2, sx + r/2, sy - r/2, colorCore);
        sprite.fillCircle(sx, sy, 1, TFT_WHITE);
    }

    // Значок головокружения над правым глазом (крутящиеся лучи-искорки)
    void drawDizzyIconOverRightEye(TFT_eSprite &sprite, float rightX, float rightY, float timeSec) {
        float iconX = rightX + 18.0f + sinf(timeSec * 6.0f) * 3.0f;
        float iconY = rightY - 34.0f + cosf(timeSec * 5.0f) * 3.0f;

        float pulse = 0.85f + 0.25f * sinf(timeSec * 8.0f);
        float spin = timeSec * 6.5f;

        for (int i = 0; i < 3; i++) {
            float ang = spin + i * 2.094f; // 120 градусов
            int x1 = (int)(iconX + cosf(ang) * (4.0f * pulse));
            int y1 = (int)(iconY + sinf(ang) * (4.0f * pulse));
            int x2 = (int)(iconX + cosf(ang) * (11.0f * pulse));
            int y2 = (int)(iconY + sinf(ang) * (11.0f * pulse));

            sprite.drawLine(x1, y1, x2, y2, colorGlow1);
            sprite.drawLine(x1, y1 + 1, x2, y2 + 1, colorGlow2);
            sprite.drawPixel(x2, y2, colorSparkle);
        }

        drawCartoonStar(sprite, (int)iconX, (int)iconY, (int)(4 * pulse), colorSparkle, colorGlow1);

        int miniX = (int)(iconX + 13.0f);
        int miniY = (int)(iconY - 6.0f);
        drawCartoonStar(sprite, miniX, miniY, 2, colorEye, colorGlow2);
    }

    // Летающие звездочки вокруг обоих глаз
    void drawOrbitingDizzyStars(TFT_eSprite &sprite, float leftX, float leftY, float rightX, float rightY, float timeSec) {
        for (int i = 0; i < 3; i++) {
            float ang = timeSec * 4.2f + i * 2.094f;
            float ox = cosf(ang) * 34.0f;
            float oy = sinf(ang) * 18.0f;
            int starX = (int)(leftX + ox);
            int starY = (int)(leftY + oy - 4.0f);
            int sz = 3 + (i % 2);
            drawCartoonStar(sprite, starX, starY, sz, colorSparkle, colorGlow1);
        }

        for (int i = 0; i < 3; i++) {
            float ang = -timeSec * 3.8f + i * 2.094f + 1.0f;
            float ox = cosf(ang) * 34.0f;
            float oy = sinf(ang) * 18.0f;
            int starX = (int)(rightX + ox);
            int starY = (int)(rightY + oy - 4.0f);
            int sz = 3 + ((i + 1) % 2);
            drawCartoonStar(sprite, starX, starY, sz, rainbowColors[i * 2], rainbowGlow);
        }
    }

    // ОДНОРОДНЫЕ КРУГЛЫЕ ГЛАЗА В СТИЛЕ IDLE (без спиралей, 3 сплошных светящихся слоя)
    void drawRoundSolidEye(TFT_eSprite &sprite, float ex, float ey, float ew, float eh) {
        int ix = (int)(ex - ew / 2.0f);
        int iy = (int)(ey - eh / 2.0f);
        int rad = (int)(fminf(ew, eh) * 0.5f);

        // Внешний ореол свечения (glow2)
        sprite.fillRoundRect(ix - 5, iy - 5, (int)ew + 10, (int)eh + 10, rad + 4, colorGlow2);
        // Средний ореол свечения (glow1)
        sprite.fillRoundRect(ix - 3, iy - 3, (int)ew + 6,  (int)eh + 6,  rad + 2, colorGlow1);
        // Сплошное монолитное ядро глаза (colorEye), полностью заполненное
        sprite.fillRoundRect(ix,     iy,     (int)ew,      (int)eh,      rad,     colorEye);
    }

    // Брови
    void drawDizzyBrows(TFT_eSprite &sprite, float leftX, float leftY, float rightX, float rightY, float phase) {
        int prevLX = (int)(leftX - 18), prevLY = (int)(leftY - 30.0f + sinf(-2.0f + phase) * 5.0f + 4.0f);
        int prevRX = (int)(rightX - 18), prevRY = (int)(rightY - 30.0f + cosf(-2.0f + phase) * 5.0f - 5.0f);

        for (int i = -14; i <= 18; i += 4) {
            float norm = (float)i / 18.0f;
            int lbx = (int)(leftX + i);
            int lby = (int)(leftY - 30.0f + sinf(norm * 2.0f + phase) * 5.0f - norm * 4.0f);
            int rbx = (int)(rightX + i);
            int rby = (int)(rightY - 30.0f + cosf(norm * 2.0f + phase) * 5.0f + norm * 5.0f);

            sprite.drawLine(prevLX, prevLY - 1, lbx, lby - 1, colorGlow1);
            sprite.drawLine(prevLX, prevLY, lbx, lby, colorEye);
            sprite.drawLine(prevLX, prevLY + 1, lbx, lby + 1, colorEye);

            sprite.drawLine(prevRX, prevRY - 1, rbx, rby - 1, colorGlow1);
            sprite.drawLine(prevRX, prevRY, rbx, rby, colorEye);
            sprite.drawLine(prevRX, prevRY + 1, rbx, rby + 1, colorEye);

            prevLX = lbx; prevLY = lby;
            prevRX = rbx; prevRY = rby;
        }
    }

    // РОТ: волна либо ПОЛНОСТЬЮ ЗАПОЛНЕННЫЙ КРУГ БЕЗ ОТВЕРСТИЯ
    void drawDizzyMouth(TFT_eSprite &sprite, float mx, float my, float w, float h, float timeSec, bool isPuking) {
        if (!isPuking && h < 16.0f) {
            int halfW = (int)(w / 2.0f);
            int prevX = (int)mx - halfW;
            int prevY = (int)my;
            for (int x = -halfW + 3; x <= halfW; x += 3) {
                float wave = sinf(x * 0.22f + timeSec * 10.0f) * (h * 0.6f);
                int curX = (int)mx + x;
                int curY = (int)my + (int)wave;
                sprite.drawLine(prevX, prevY - 1, curX, curY - 1, colorGlow1);
                sprite.drawLine(prevX, prevY,     curX, curY,     colorEye);
                sprite.drawLine(prevX, prevY + 1, curX, curY + 1, colorEye);
                sprite.drawLine(prevX, prevY + 2, curX, curY + 2, colorGlow1);
                prevX = curX;
                prevY = curY;
            }
        } else {
            // ПОЛНОСТЬЮ ЗАПОЛНЕННЫЙ КРУГЛЫЙ РОТ (без черного отверстия внутри)
            int rOuter = (int)(w * 0.5f);
            if (rOuter < 16) rOuter = 10;

            sprite.fillCircle((int)mx, (int)my, rOuter + 4, colorGlow2);
            sprite.fillCircle((int)mx, (int)my, rOuter + 2, colorGlow1);
            sprite.fillCircle((int)mx, (int)my, rOuter,     colorEye);
        }
    }

    // РАДУГА: сплошная монолитная струя + лужа на дне спрайта
    void drawRainbowAndPuddle(TFT_eSprite &sprite, float mx, float my, float w, float progress, float timeSec) {
        if (progress <= 0.02f) return;

        int startY = (int)my + 4; // вытекает из круглого рта
        int maxEndY = 196;        // дно спрайта
        int currentEndY = startY + (int)((maxEndY - startY) * progress);
        if (currentEndY > maxEndY) currentEndY = maxEndY;

        float baseStripW = (w - 4.0f) / 6.0f;
        if (baseStripW < 4.0f) baseStripW = 4.0f;

        // Поток радуги (шаг 2px для плотного заполнения)
        for (int y = startY; y < currentEndY; y += 2) {
            float distFactor = (float)(y - startY) / (float)(maxEndY - startY);
            float spread = 1.0f + distFactor * 0.50f;
            float stripW = baseStripW * spread;
            float totalW = stripW * 6.0f;

            float wave = sinf(y * 0.075f - timeSec * 16.0f) * (4.0f + distFactor * 6.0f);
            float baseX = mx - totalW / 2.0f + wave;

            sprite.drawFastHLine((int)baseX - 4, y, (int)totalW + 8, rainbowGlow);
            sprite.drawFastHLine((int)baseX - 4, y + 1, (int)totalW + 8, rainbowGlow);

            for (int i = 0; i < 6; i++) {
                int sx = (int)(baseX + i * stripW);
                int sw = (int)(stripW + 1.4f);
                sprite.fillRect(sx, y, sw, 2, rainbowColors[i]);
            }
        }

        // Радужная лужа на дне
        if (puddleScale > 0.02f) {
            float pW = 45.0f + puddleScale * 115.0f;
            float pH = 8.0f + puddleScale * 18.0f;
            float wobble = sinf(timeSec * 8.0f) * 3.0f;

            sprite.fillRoundRect((int)(mx - (pW + 10) / 2.0f + wobble), 199 - (int)pH - 3, (int)pW + 10, (int)pH + 4, 8, rainbowGlow);

            for (int i = 5; i >= 0; i--) {
                float layerFactor = (float)(i + 1) / 6.0f;
                int lw = (int)(pW * layerFactor);
                int lh = (int)(pH * layerFactor);
                if (lh < 3) lh = 3;
                int lx = (int)(mx - lw / 2.0f + wobble * (1.0f - layerFactor * 0.4f));
                int ly = 199 - lh;
                sprite.fillRoundRect(lx, ly, lw, lh, lh / 2 + 1, rainbowColors[i]);
            }

            float splash = sinf(timeSec * 14.0f) * 2.5f;
            sprite.fillCircle((int)(mx - pW * 0.54f), 194 + (int)splash, 3, rainbowColors[0]);
            sprite.fillCircle((int)(mx + pW * 0.54f), 194 - (int)splash, 3, rainbowColors[5]);
            sprite.fillCircle((int)(mx - pW * 0.38f), 188, 2, rainbowColors[1]);
            sprite.fillCircle((int)(mx + pW * 0.38f), 188, 2, rainbowColors[4]);
        }
    }

    void updateAndDrawParticles(TFT_eSprite &sprite, float mx, float my, bool isPuking, float timeSec) {
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (!particles[i].active) {
                if (isPuking && random(0, 100) < 40) {
                    particles[i].active = true;
                    particles[i].type = (random(0, 100) < 40) ? 0 : 1;
                    particles[i].x = mx + random(-20, 21);
                    particles[i].y = my + random(5, 50);
                    particles[i].vx = random(-25, 26) / 10.0f;
                    particles[i].vy = random(12, 35) / 10.0f;
                    particles[i].color = (particles[i].type == 0) ? colorSparkle : rainbowColors[random(0, 6)];
                    break;
                }
            }
        }

        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (!particles[i].active) continue;

            particles[i].x += particles[i].vx;
            particles[i].y += particles[i].vy;

            if (particles[i].type == 1) {
                particles[i].vy += 0.20f;
                sprite.fillRect((int)particles[i].x, (int)particles[i].y, 2, 2, particles[i].color);
            } else {
                drawCartoonStar(sprite, (int)particles[i].x, (int)particles[i].y, 2, particles[i].color, rainbowGlow);
            }

            if (particles[i].y > 196 || particles[i].x < 5 || particles[i].x > 235) {
                particles[i].active = false;
            }
        }
    }

} // anonymous namespace

void init(TFT_eSPI &tft) {
    colorEye   = tft.color565(180, 255, 60);
    colorGlow1 = tft.color565(50, 175, 20);
    colorGlow2 = tft.color565(10, 65, 5);

    rainbowColors[0] = tft.color565(255, 30, 70);
    rainbowColors[1] = tft.color565(255, 150, 0);
    rainbowColors[2] = tft.color565(255, 240, 20);
    rainbowColors[3] = tft.color565(30, 255, 90);
    rainbowColors[4] = tft.color565(20, 190, 255);
    rainbowColors[5] = tft.color565(190, 50, 255);

    rainbowGlow  = tft.color565(45, 20, 60);
    colorSparkle = tft.color565(255, 255, 255);

    reset();
}

void reset() {
    // Подхватываем текущую геометрию лица из IDLE
    FaceState::loadGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);
    // Плавная смена палитры на токсично-лаймовую
    FaceState::setTargetPalette(180, 255, 60, 50, 175, 20, 10, 65, 5);

    t_spacing = EYE_SPACING;
    // Целевая форма глаз — идеальные круги диаметром 54 x 54 px
    t_lw = 54.0f;
    t_rw = 54.0f;
    t_lh = 54.0f;
    t_rh = 54.0f;

    pukeProgress = 0.0f;
    puddleScale = 0.0f;
    animStartTime = millis();
    lastStepTime = millis();

    for (int i = 0; i < MAX_PARTICLES; i++) {
        particles[i].active = false;
    }
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    (void)tft; // Рендер строго в спрайт, мерцание экрана исключено

    uint32_t now = millis();
    float dt = (now - lastStepTime) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;
    lastStepTime = now;

    colorEye   = FaceState::current.colorEye;
    colorGlow1 = FaceState::current.colorGlow1;
    colorGlow2 = FaceState::current.colorGlow2;

    float timeSec = (now - animStartTime) / 1000.0f;
    float cycleTime = fmodf(timeSec, 6.2f);

    bool isGagging = (cycleTime >= 2.1f && cycleTime < 2.55f);
    bool isPuking  = (cycleTime >= 2.55f && cycleTime < 5.7f);

    // СИЛЬНАЯ ХАОТИЧНАЯ ТРЯСКА
    float shakeX = 0.0f;
    float shakeY = 0.0f;

    // Раздельная хаотичная дрожь для каждого глаза (эффект ошеломления)
    float lJitterX = 0.0f, lJitterY = 0.0f;
    float rJitterX = 0.0f, rJitterY = 0.0f;

    if (isGagging) {
        t_cx = CENTER_X;
        t_cy = BASE_Y + 12.0f;
        t_lw = 46.0f; t_rw = 46.0f;
        t_lh = 46.0f; t_rh = 46.0f;
        t_mw = 20.0f; t_mh = 12.0f;

        shakeX = (float)random(-3, 4);
        shakeY = (float)random(-3, 4);

        pukeProgress += (0.0f - pukeProgress) * 0.25f;
        puddleScale += (0.0f - puddleScale) * 0.15f;
    } else if (isPuking) {
        t_cx = CENTER_X + sinf(timeSec * 8.0f) * 6.0f;
        t_cy = BASE_Y - 8.0f;
        t_lw = 54.0f; t_rw = 54.0f;
        t_lh = 54.0f; t_rh = 54.0f;

        // ИДЕАЛЬНО КРУГЛЫЙ РОТ: диаметр 38 x 38 px
        t_mw = 22.0f;
        t_mh = 22.0f;

        // Мощная дрожь головы при извержении
        shakeX = (float)random(-6, 7) + sinf(timeSec * 35.0f) * 3.0f;
        shakeY = (float)random(-5, 6) + cosf(timeSec * 40.0f) * 2.5f;

        // Асинхронная вибрация глаз
        lJitterX = (float)random(-3, 4);
        lJitterY = (float)random(-3, 4);
        rJitterX = (float)random(-3, 4);
        rJitterY = (float)random(-3, 4);

        pukeProgress += (1.0f - pukeProgress) * 0.18f;

        if (pukeProgress > 0.60f) {
            puddleScale += (1.0f - puddleScale) * 0.14f;
        }
    } else {
        // Сильная дрожь и покачивание головы при тошноте
        t_cx = CENTER_X + sinf(timeSec * 3.2f) * 16.0f;
        t_cy = BASE_Y + sinf(timeSec * 6.4f) * 7.0f;
        t_lw = 54.0f + sinf(timeSec * 5.0f) * 6.0f;
        t_rw = 54.0f - sinf(timeSec * 5.0f) * 6.0f;
        t_lh = t_lw;
        t_rh = t_rw;
        t_mw = 32.0f;
        t_mh = 6.0f;

        shakeX = (float)random(-4, 5) + sinf(timeSec * 28.0f) * 2.5f;
        shakeY = (float)random(-3, 4) + cosf(timeSec * 32.0f) * 2.0f;

        lJitterX = (float)random(-2, 3);
        lJitterY = (float)random(-2, 3);
        rJitterX = (float)random(-2, 3);
        rJitterY = (float)random(-2, 3);

        pukeProgress += (0.0f - pukeProgress) * 0.2f;
        puddleScale += (0.0f - puddleScale) * 0.12f;
    }

    // Плавная физика перетекания из геометрии IDLE в круги
    const float speed = 0.15f;
    cx += (t_cx - cx) * speed;
    cy += (t_cy - cy) * speed;
    currentSpacing += (t_spacing - currentSpacing) * speed;
    lw += (t_lw - lw) * speed;
    rw += (t_rw - rw) * speed;
    lh += (t_lh - lh) * speed; // плавно уменьшается с 80 до 54
    rh += (t_rh - rh) * speed;
    mw += (t_mw - mw) * 0.22f;
    mh += (t_mh - mh) * 0.22f;

    FaceState::saveGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);

    eyeOffsetY = sinf(timeSec * 4.5f) * 6.0f;

    lx = cx - currentSpacing / 2.0f + shakeX;
    rx = cx + currentSpacing / 2.0f + shakeX;
    float curCy = cy + shakeY;

    // --- ОТРИСОВКА В БУФЕРЕ СПРАЙТА ---
    sprite.fillSprite(TFT_BLACK);

    float mouthY = curCy + 40.0f;

    // 1. Монолитный поток радуги + лужа на дне спрайта
    drawRainbowAndPuddle(sprite, cx + shakeX, mouthY, mw, pukeProgress, timeSec);

    // 2. ПОЛНОСТЬЮ ЗАПОЛНЕННЫЙ КРУГЛЫЙ РОТ
    drawDizzyMouth(sprite, cx + shakeX, mouthY, mw, mh, timeSec, isPuking || (pukeProgress > 0.2f));

    // 3. ОДНОРОДНЫЕ КРУГЛЫЕ ГЛАЗА В СТИЛЕ IDLE С СИЛЬНОЙ ДРОЖЬЮ
    drawRoundSolidEye(sprite, lx + lJitterX, curCy + eyeOffsetY + lJitterY, lw, lh);
    drawRoundSolidEye(sprite, rx + rJitterX, curCy - eyeOffsetY + rJitterY, rw, rh);

    // 4. Летающие звездочки вокруг глаз
    drawOrbitingDizzyStars(sprite, lx, curCy + eyeOffsetY, rx, curCy - eyeOffsetY, timeSec);

    // 5. Значок головокружения над правым глазом
    drawDizzyIconOverRightEye(sprite, rx, curCy - eyeOffsetY, timeSec);

    // 6. Брови и частицы
    drawDizzyBrows(sprite, lx, curCy + eyeOffsetY, rx, curCy - eyeOffsetY, timeSec * 4.0f);
    updateAndDrawParticles(sprite, cx + shakeX, mouthY, isPuking, timeSec);

    sprite.pushSprite(0, 40);
}

} // namespace AnimDizzy
