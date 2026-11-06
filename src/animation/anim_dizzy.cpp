#include "anim_dizzy.h"
#include "face_state.h"
#include <math.h>

namespace AnimDizzy {

namespace {
    // --- Константы базовой геометрии ---
    const float CENTER_X = 120.0f;
    const float BASE_Y = 135.0f;
    const float EYE_SPACING = 106.0f;

    // --- Палитра лица (Токсично-зеленовато-желтый неон) ---
    uint16_t colorEye;
    uint16_t colorGlow1;
    uint16_t colorGlow2;

    // --- Палитра Радуги (Nyan Cat style) ---
    uint16_t rainbowColors[6];
    uint16_t rainbowGlow;
    uint16_t colorSparkle;

    // --- Стандартные переменные физики ---
    float cx = CENTER_X, cy = BASE_Y;
    float currentSpacing = EYE_SPACING;
    float lx = CENTER_X - EYE_SPACING / 2.0f, rx = CENTER_X + EYE_SPACING / 2.0f;
    float lw = 52.0f, rw = 52.0f, lh = 52.0f, rh = 52.0f;
    float mw = 28.0f, mh = 6.0f;

    float t_cx = CENTER_X, t_cy = BASE_Y;
    float t_spacing = EYE_SPACING;
    float t_lw = 52.0f, t_rw = 52.0f, t_lh = 52.0f, t_rh = 52.0f;
    float t_mw = 28.0f, t_mh = 6.0f;

    // Специфичные переменные для Dizzy
    float eyeOffsetY = 0.0f;
    float spiralAngle = 0.0f;
    float pukeProgress = 0.0f;
    uint32_t animStartTime = 0;
    uint32_t lastStepTime = 0;

    // --- Система частиц ---
    struct Particle {
        float x, y;
        float vx, vy;
        uint16_t color;
        uint8_t type;
        bool active;
    };
    const int MAX_PARTICLES = 18;
    Particle particles[MAX_PARTICLES];

    void drawSparkle(TFT_eSprite &sprite, int x, int y, int r, uint16_t col) {
        sprite.drawFastHLine(x - r, y, r * 2 + 1, col);
        sprite.drawFastVLine(x, y - r, r * 2 + 1, col);
        sprite.drawPixel(x - 1, y - 1, col);
        sprite.drawPixel(x + 1, y - 1, col);
        sprite.drawPixel(x - 1, y + 1, col);
        sprite.drawPixel(x + 1, y + 1, col);
    }

    // 3-слойная гладкая и яркая спираль Архимеда (Глаза @ @)
    void drawSpiralEye(TFT_eSprite &sprite, float centerX, float centerY, float maxR, float angleOffset, int dir) {
        if (maxR < 4.0f) maxR = 4.0f;
        const float maxTheta = 2.15f * 2.0f * PI;

        for (int pass = 0; pass < 3; pass++) {
            uint16_t col = (pass == 0) ? colorGlow2 : ((pass == 1) ? colorGlow1 : colorEye);
            int brushR = (pass == 0) ? 6 : ((pass == 1) ? 4 : 2);

            float theta = 0.0f;
            while (theta < maxTheta) {
                float r = (theta / maxTheta) * maxR;
                float a = dir * theta + angleOffset;
                int px = (int)(centerX + cosf(a) * r);
                int py = (int)(centerY + sinf(a) * r);
                sprite.fillCircle(px, py, brushR, col);

                float step = (r < 4.0f) ? 0.20f : (1.2f / r);
                theta += step;
            }
        }
    }

    // Асимметричные "пьяные" брови
    void drawDizzyBrows(TFT_eSprite &sprite, float leftX, float leftY, float rightX, float rightY, float phase) {
        for (int pass = 0; pass < 3; pass++) {
            uint16_t col = (pass == 0) ? colorGlow2 : ((pass == 1) ? colorGlow1 : colorEye);
            int brushR = (pass == 0) ? 5 : ((pass == 1) ? 2 : 1);

            for (int i = -18; i <= 18; i += 2) {
                float norm = (float)i / 18.0f;
                int lbx = (int)(leftX + i);
                int lby = (int)(leftY - 30.0f + sinf(norm * 2.0f + phase) * 5.0f - norm * 4.0f);
                if (lby < 14) lby = 14;
                sprite.fillCircle(lbx, lby, brushR, col);

                int rbx = (int)(rightX + i);
                int rby = (int)(rightY - 30.0f + cosf(norm * 2.0f + phase) * 5.0f + norm * 5.0f);
                if (rby < 14) rby = 14;
                sprite.fillCircle(rbx, rby, brushR, col);
            }
        }
    }

    // Рот: плавный морфинг от тошнотворной волны (~) к открытому рту (O)
    void drawDizzyMouth(TFT_eSprite &sprite, float mx, float my, float w, float h, float timeSec, bool isPuking) {
        if (!isPuking && h < 14.0f) {
            for (int pass = 0; pass < 3; pass++) {
                uint16_t col = (pass == 0) ? colorGlow2 : ((pass == 1) ? colorGlow1 : colorEye);
                int brushR = (pass == 0) ? 6 : ((pass == 1) ? 3 : 1);

                int halfW = (int)(w / 2.0f);
                for (int x = -halfW; x <= halfW; x += 2) {
                    float wave = sinf(x * 0.22f + timeSec * 10.0f) * (h * 0.6f);
                    sprite.fillCircle((int)mx + x, (int)my + (int)wave, brushR, col);
                }
                sprite.fillCircle((int)mx - halfW - 3, (int)my, brushR + 1, col);
                sprite.fillCircle((int)mx + halfW + 3, (int)my, brushR + 1, col);
            }
        } else {
            int rx_i = (int)(mx - w / 2.0f);
            int ry_i = (int)(my - h / 2.0f);
            int rw_i = (int)w;
            int rh_i = (int)h;
            int rad = (rh_i > 20) ? 10 : (rh_i / 2);

            sprite.fillRoundRect(rx_i - 4, ry_i - 4, rw_i + 8, rh_i + 8, rad + 3, colorGlow2);
            sprite.fillRoundRect(rx_i - 2, ry_i - 2, rw_i + 4, rh_i + 4, rad + 1, colorGlow1);
            sprite.fillRoundRect(rx_i, ry_i, rw_i, rh_i, rad, colorEye);
            if (rw_i > 8 && rh_i > 8) {
                sprite.fillRoundRect(rx_i + 3, ry_i + 3, rw_i - 6, rh_i - 6, max(1, rad - 2), TFT_BLACK);
            }
        }
    }

    // Извержение радуги (Nyan Cat Waterfall)
    void drawRainbowVomit(TFT_eSprite &sprite, float mx, float my, float w, float progress, float timeSec) {
        if (progress <= 0.01f) return;

        int startY = (int)my - 2;
        int maxEndY = 280;
        int currentEndY = startY + (int)((maxEndY - startY) * progress);

        float stripWidth = (w - 4.0f) / 6.0f;
        if (stripWidth < 4.0f) stripWidth = 4.0f;
        float totalRainbowW = stripWidth * 6.0f;

        for (int y = startY; y < currentEndY; y += 3) {
            float wave = sinf(y * 0.09f - timeSec * 18.0f) * 7.0f;
            int leftX = (int)(mx - totalRainbowW / 2.0f + wave);
            sprite.drawFastHLine(leftX - 4, y, (int)totalRainbowW + 8, rainbowGlow);
            sprite.drawFastHLine(leftX - 4, y + 1, (int)totalRainbowW + 8, rainbowGlow);
            sprite.drawFastHLine(leftX - 4, y + 2, (int)totalRainbowW + 8, rainbowGlow);
        }

        for (int y = startY; y < currentEndY; y += 2) {
            float distFactor = (float)(y - startY) / (float)(maxEndY - startY);
            float spread = 1.0f + distFactor * 0.35f;
            float currentStripW = stripWidth * spread;
            float currentTotalW = currentStripW * 6.0f;

            float wave = sinf(y * 0.09f - timeSec * 18.0f) * (6.0f + distFactor * 6.0f);
            float baseX = mx - currentTotalW / 2.0f + wave;

            for (int i = 0; i < 6; i++) {
                int sx = (int)(baseX + i * currentStripW);
                int sw = (int)(currentStripW + 1.5f);
                sprite.fillRect(sx, y, sw, 2, rainbowColors[i]);
            }
        }

        for (int i = 0; i < 6; i++) {
            float bubbleWave = sinf(timeSec * 15.0f + i * 1.3f) * 3.0f;
            int bx = (int)(mx - totalRainbowW / 2.0f + (i + 0.5f) * stripWidth);
            sprite.fillCircle(bx, startY + (int)bubbleWave, 4, rainbowColors[i]);
            if (progress > 0.95f) {
                int splashX = (int)(mx - (totalRainbowW * 0.7f) + i * (stripWidth * 1.4f) + bubbleWave * 2.0f);
                sprite.fillCircle(splashX, 167, 6, rainbowColors[i]);
            }
        }
    }

    void updateAndDrawParticles(TFT_eSprite &sprite, float mx, float my, bool isPuking, float timeSec) {
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (!particles[i].active) {
                if (isPuking && random(0, 100) < 45) {
                    particles[i].active = true;
                    particles[i].type = (random(0, 100) < 35) ? 0 : 1;
                    particles[i].x = mx + random(-24, 25);
                    particles[i].y = my + random(5, 65);
                    particles[i].vx = random(-35, 36) / 10.0f;
                    particles[i].vy = random(15, 45) / 10.0f;
                    particles[i].color = (particles[i].type == 0) ? colorSparkle : rainbowColors[random(0, 6)];
                    break;
                } else if (!isPuking && random(0, 100) < 15) {
                    particles[i].active = true;
                    particles[i].type = 0;
                    float angle = random(0, 628) / 100.0f;
                    particles[i].x = cx + cosf(angle) * 75.0f;
                    particles[i].y = cy - 35.0f + sinf(angle) * 18.0f;
                    particles[i].vx = -sinf(angle) * 1.5f;
                    particles[i].vy = cosf(angle) * 0.5f;
                    particles[i].color = (random(0, 2) == 0) ? colorSparkle : rainbowColors[1];
                    break;
                }
            }
        }

        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (!particles[i].active) continue;

            particles[i].x += particles[i].vx;
            particles[i].y += particles[i].vy;

            if (particles[i].type == 1) {
                particles[i].vy += 0.25f;
                sprite.fillCircle((int)particles[i].x, (int)particles[i].y, 3, rainbowGlow);
                sprite.fillCircle((int)particles[i].x, (int)particles[i].y, 2, particles[i].color);
            } else {
                int r = 3 + ((int)(timeSec * 20.0f + i) % 2);
                drawSparkle(sprite, (int)particles[i].x, (int)particles[i].y, r, particles[i].color);
            }

            if (particles[i].y > 175 || particles[i].x < -10 || particles[i].x > 330) {
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
    // 1. Загружаем текущую геометрию из предыдущей эмоции
    FaceState::loadGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);

    // 2. Плавно переливаем цвет в кислотно-лаймовый
    FaceState::setTargetPalette(180, 255, 60, 50, 175, 20, 10, 65, 5);

    // Начинаем раскручивать спирали из прищуренного размера глаз (плавное раскрытие спиралей!)
    lw = lh;
    rw = rh;
    t_spacing = EYE_SPACING;

    pukeProgress = 0.0f;
    spiralAngle = 0.0f;
    animStartTime = millis();
    lastStepTime = millis();

    for (int i = 0; i < MAX_PARTICLES; i++) {
        particles[i].active = false;
    }
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    uint32_t now = millis();
    float dt = (now - lastStepTime) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;
    lastStepTime = now;

    // Берем плавно интерполируемый цвет из FaceState
    colorEye   = FaceState::current.colorEye;
    colorGlow1 = FaceState::current.colorGlow1;
    colorGlow2 = FaceState::current.colorGlow2;

    float timeSec = (now - animStartTime) / 1000.0f;
    float cycleTime = fmodf(timeSec, 6.2f);

    bool isGagging = (cycleTime >= 2.1f && cycleTime < 2.55f);
    bool isPuking  = (cycleTime >= 2.55f && cycleTime < 5.7f);

    float shakeX = 0.0f;
    float shakeY = 0.0f;

    if (isGagging) {
        t_cx = CENTER_X;
        t_cy = BASE_Y + 12.0f;
        t_lw = 44.0f; t_rw = 44.0f;
        t_lh = 44.0f; t_rh = 44.0f;
        t_mw = 18.0f; t_mh = 10.0f;
        spiralAngle += 5.0f * dt;
        pukeProgress += (0.0f - pukeProgress) * 0.25f;
    } else if (isPuking) {
        t_cx = CENTER_X + sinf(timeSec * 8.0f) * 6.0f;
        t_cy = BASE_Y - 8.0f;
        t_lw = 58.0f; t_rw = 58.0f;
        t_lh = 58.0f; t_rh = 58.0f;
        t_mw = 48.0f; t_mh = 30.0f;

        shakeX = random(-2, 3);
        shakeY = random(-2, 3);

        spiralAngle += 16.0f * dt;
        pukeProgress += (1.0f - pukeProgress) * 0.18f;
    } else {
        t_cx = CENTER_X + sinf(timeSec * 3.2f) * 16.0f;
        t_cy = BASE_Y + sinf(timeSec * 6.4f) * 7.0f;
        t_lw = 52.0f + sinf(timeSec * 5.0f) * 6.0f;
        t_rw = 52.0f - sinf(timeSec * 5.0f) * 6.0f;
        t_lh = t_lw;
        t_rh = t_rw;
        t_mw = 32.0f;
        t_mh = 6.0f;

        spiralAngle += 7.5f * dt;
        pukeProgress += (0.0f - pukeProgress) * 0.2f;
    }

    // Пружинная физика
    const float speed = 0.18f;
    cx += (t_cx - cx) * speed;
    cy += (t_cy - cy) * speed;
    currentSpacing += (t_spacing - currentSpacing) * speed;
    lw += (t_lw - lw) * speed;
    rw += (t_rw - rw) * speed;
    lh += (t_lh - lh) * speed;
    rh += (t_rh - rh) * speed;
    mw += (t_mw - mw) * 0.25f;
    mh += (t_mh - mh) * 0.25f;

    // Сохраняем актуальную геометрию в FaceState
    FaceState::saveGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);

    eyeOffsetY = sinf(timeSec * 4.5f) * 6.0f;

    lx = cx - currentSpacing / 2.0f + shakeX;
    rx = cx + currentSpacing / 2.0f + shakeX;
    float curCy = cy + shakeY;

    // --- ОТРИСОВКА КАДРА ---
    sprite.fillSprite(TFT_BLACK);

    float mouthY = curCy + 42.0f;

    drawDizzyMouth(sprite, cx + shakeX, mouthY, mw, mh, timeSec, isPuking || (pukeProgress > 0.2f));
    drawRainbowVomit(sprite, cx + shakeX, mouthY, mw, pukeProgress, timeSec);

    drawSpiralEye(sprite, lx, curCy + eyeOffsetY, lw * 0.5f, spiralAngle, 1);
    drawSpiralEye(sprite, rx, curCy - eyeOffsetY, rw * 0.5f, spiralAngle, -1);

    drawDizzyBrows(sprite, lx, curCy + eyeOffsetY, rx, curCy - eyeOffsetY, timeSec * 4.0f);
    updateAndDrawParticles(sprite, cx + shakeX, mouthY, isPuking, timeSec);

    sprite.pushSprite(0, 40);
}

} // namespace AnimDizzy
