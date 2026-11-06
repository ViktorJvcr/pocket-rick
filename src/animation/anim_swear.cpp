#include "animation/anim_swear.h"
#include "face_state.h"
#include <math.h>

namespace AnimSwear {

namespace {
    // Базовые координаты
    const float CENTER_X = 120.0f;
    const float BASE_Y = 100.0f;

    // Цветовая палитра: Фирменный неоново-зеленый (Лицо)
    uint16_t colorEye;
    uint16_t colorGlow1;
    uint16_t colorGlow2;

    // Цвета для символов ругани и плашки цензуры:
    uint16_t colorWarnCore;
    uint16_t colorWarnGlow1;
    uint16_t colorWarnGlow2;

    uint16_t colorCensorCore;
    uint16_t colorCensorGlow1;
    uint16_t colorCensorGlow2;

    uint16_t colorSpitCore;
    uint16_t colorSpitGlow1;
    uint16_t colorSpitGlow2;

    // Стандартные переменные физики
    float cx = CENTER_X, cy = BASE_Y;
    float lx = 110.0f, rx = 210.0f;
    float lw = 50.0f, rw = 50.0f;
    float lh = 56.0f, rh = 56.0f;
    float mw = 24.0f, mh = 6.0f;
    float currentSpacing = 100.0f;

    // Специфичные переменные эмоции
    float browOffset = -15.0f;
    float browSlant = 0.0f;
    float eyeAsymY = 0.0f;
    float tremorAmp = 0.0f;
    float mouthShiftX = 0.0f;
    float mouthOpenRatio = 0.0f;
    float censorAlpha = 0.0f;

    // Целевые переменные
    float t_cx = CENTER_X, t_cy = BASE_Y;
    float t_lw = 50.0f, t_rw = 50.0f;
    float t_lh = 56.0f, t_rh = 56.0f;
    float t_mw = 24.0f, t_mh = 6.0f;
    float t_spacing = 100.0f;
    float t_browOffset = 0.0f;
    float t_browSlant = 12.0f;
    float t_eyeAsymY = 0.0f;
    float t_tremorAmp = 0.0f;
    float t_mouthShiftX = 0.0f;
    float t_mouthOpenRatio = 0.0f;
    float t_censorAlpha = 0.0f;

    uint32_t animStartTime = 0;
    uint32_t lastFrameTime = 0;
    uint32_t lastBlinkTime = 0;
    bool isBlinking = false;
    uint32_t blinkStartTime = 0;

    struct SwearGlyph {
        bool active;
        uint8_t type;
        uint8_t palette;
        float x, y;
        float vx, vy;
        float scale;
        float targetScale;
        uint32_t spawnTime;
        uint32_t lifeTime;
    };
    const int MAX_GLYPHS = 7;
    SwearGlyph glyphs[MAX_GLYPHS];
    uint32_t lastGlyphSpawn = 0;

    struct SpitDrop {
        bool active;
        float x, y;
        float vx, vy;
        uint32_t spawnTime;
    };
    const int MAX_SPIT = 6;
    SpitDrop spitDrops[MAX_SPIT];
    uint32_t lastSpitSpawn = 0;

    void getPaletteColors(uint8_t pal, int pass, uint16_t &col, int &r) {
        if (pal == 0) {
            if (pass == 0) { col = colorGlow2; r = 5; }
            else if (pass == 1) { col = colorGlow1; r = 3; }
            else { col = colorEye; r = 1; }
        } else if (pal == 1) {
            if (pass == 0) { col = colorWarnGlow2; r = 5; }
            else if (pass == 1) { col = colorWarnGlow1; r = 3; }
            else { col = colorWarnCore; r = 1; }
        } else {
            if (pass == 0) { col = colorCensorGlow2; r = 5; }
            else if (pass == 1) { col = colorCensorGlow1; r = 3; }
            else { col = colorCensorCore; r = 1; }
        }
    }

    void drawSegmentPass(TFT_eSprite &sprite, float x0, float y0, float x1, float y1, int r, uint16_t col) {
        float dx = x1 - x0;
        float dy = y1 - y0;
        float dist = sqrtf(dx * dx + dy * dy);
        int steps = (int)(dist / 1.3f) + 1;
        for (int i = 0; i <= steps; i++) {
            float t = (steps == 0) ? 0.0f : (float)i / (float)steps;
            sprite.fillCircle((int)(x0 + dx * t), (int)(y0 + dy * t), r, col);
        }
    }

    void drawGlowingEye(TFT_eSprite &sprite, float x, float y, float w, float h, int r) {
        int iw = (int)w;
        int ih = (int)h;
        if (ih < 4) ih = 4;
        int ix = (int)(x - iw * 0.5f);
        int iy = (int)(y - ih * 0.5f);

        sprite.fillRoundRect(ix - 6, iy - 6, iw + 12, ih + 12, r + 6, colorGlow2);
        sprite.fillRoundRect(ix - 3, iy - 3, iw + 6, ih + 6, r + 3, colorGlow1);
        sprite.fillRoundRect(ix, iy, iw, ih, r, colorEye);
    }

    void drawAngryBrowAndMask(TFT_eSprite &sprite, float ex, float ey, float ew, float eh, bool isLeft, float offset, float slant) {
        float halfW = ew * 0.5f + 8.0f;
        float topY = ey - eh * 0.5f + offset;

        float outerX = isLeft ? (ex - halfW) : (ex + halfW);
        float innerX = isLeft ? (ex + halfW) : (ex - halfW);

        float outerY = topY - slant * 0.6f;
        float innerY = topY + slant * 1.15f;

        if (outerY < 14.0f) outerY = 14.0f;
        if (innerY < 14.0f) innerY = 14.0f;

        int maskTopY = (int)((outerY < innerY ? outerY : innerY) - 22.0f);
        if (maskTopY < 0) maskTopY = 0;

        sprite.fillTriangle((int)outerX, (int)outerY, (int)innerX, (int)innerY, (int)outerX, maskTopY, TFT_BLACK);
        sprite.fillTriangle((int)innerX, (int)innerY, (int)outerX, maskTopY, (int)innerX, maskTopY, TFT_BLACK);

        float ctrlX = (outerX + innerX) * 0.5f;
        float ctrlY = (outerY + innerY) * 0.5f + 4.0f;
        if (ctrlY < 14.0f) ctrlY = 14.0f;

        float width = fabsf(innerX - outerX);
        int steps = (int)(width / 1.3f) + 1;

        for (int pass = 0; pass < 3; pass++) {
            uint16_t col = (pass == 0) ? colorGlow2 : ((pass == 1) ? colorGlow1 : colorEye);
            int r = (pass == 0) ? 6 : ((pass == 1) ? 4 : 2);

            for (int i = 0; i <= steps; i++) {
                float t = (float)i / (float)steps;
                float invT = 1.0f - t;
                float bx = invT * invT * outerX + 2.0f * invT * t * ctrlX + t * t * innerX;
                float by = invT * invT * outerY + 2.0f * invT * t * ctrlY + t * t * innerY;
                if (by < 14.0f) by = 14.0f;
                sprite.fillCircle((int)bx, (int)by, r, col);
            }
        }
    }

    void drawVeinMark(TFT_eSprite &sprite, float x, float y, float scale) {
        if (scale < 0.1f) return;
        float d = 4.5f * scale;
        float l = 6.5f * scale;

        for (int pass = 0; pass < 3; pass++) {
            uint16_t col = (pass == 0) ? colorGlow2 : ((pass == 1) ? colorGlow1 : colorEye);
            int r = (pass == 0) ? 4 : ((pass == 1) ? 2 : 1);

            for (int q = 0; q < 4; q++) {
                float sx = (q == 0 || q == 3) ? 1.0f : -1.0f;
                float sy = (q < 2) ? -1.0f : 1.0f;

                float cxCorner = x + sx * d;
                float cyCorner = y + sy * d;

                drawSegmentPass(sprite, cxCorner, cyCorner + sy * l, cxCorner, cyCorner, r, col);
                drawSegmentPass(sprite, cxCorner, cyCorner, cxCorner + sx * l, cyCorner, r, col);
            }
        }
    }

    void drawSwearingMouth(TFT_eSprite &sprite, float mx, float my, float w, float h, float openRatio, uint32_t now) {
        float halfW = w * 0.5f;
        float halfH = h * 0.5f;

        if (openRatio < 0.2f || h < 7.0f) {
            int steps = (int)(w / 1.3f) + 1;
            float phase = now * 0.025f;
            for (int pass = 0; pass < 3; pass++) {
                uint16_t col = (pass == 0) ? colorGlow2 : ((pass == 1) ? colorGlow1 : colorEye);
                int r = (pass == 0) ? 5 : ((pass == 1) ? 3 : 2);
                for (int i = 0; i <= steps; i++) {
                    float t = (float)i / (float)steps;
                    float px = mx - halfW + t * w;
                    float arch = -4.0f * (1.0f - 4.0f * (t - 0.5f) * (t - 0.5f));
                    float wave = sinf(t * 9.0f + phase) * 2.2f;
                    float py = my + arch + wave;
                    sprite.fillCircle((int)px, (int)py, r, col);
                }
            }
            return;
        }

        int steps = 54;
        float ptsX[55];
        float ptsY[55];

        for (int i = 0; i <= steps; i++) {
            float angle = ((float)i / (float)steps) * 2.0f * PI;
            float cosA = cosf(angle);
            float sinA = sinf(angle);

            float shapeW = halfW * (1.0f + 0.18f * sinA);
            float roarVib = sinf(angle * 4.0f + now * 0.04f) * 1.4f * openRatio;

            ptsX[i] = mx + cosA * shapeW;
            ptsY[i] = my + sinA * halfH + roarVib;
        }

        for (int pass = 0; pass < 2; pass++) {
            uint16_t col = (pass == 0) ? colorGlow2 : colorGlow1;
            int r = (pass == 0) ? 6 : 4;
            for (int i = 0; i <= steps; i++) {
                sprite.fillCircle((int)ptsX[i], (int)ptsY[i], r, col);
            }
        }

        int innerW = (int)(w * 0.85f);
        int innerH = (int)(h * 0.78f);
        if (innerW > 4 && innerH > 4) {
            sprite.fillRoundRect((int)(mx - innerW * 0.5f), (int)(my - innerH * 0.5f), innerW, innerH, 6, TFT_BLACK);
        }

        if (h > 14.0f) {
            int teethCount = 4;
            float toothW = (w * 0.72f) / teethCount;
            float startX = mx - (w * 0.36f);
            float topTeethY = my - halfH + 3.0f;
            float toothH = fminf(5.5f, h * 0.25f);

            for (int t = 0; t < teethCount; t++) {
                float tx0 = startX + t * toothW;
                float txMid = tx0 + toothW * 0.5f;
                float tx1 = tx0 + toothW;
                sprite.fillTriangle((int)tx0, (int)topTeethY, (int)tx1, (int)topTeethY, (int)txMid, (int)(topTeethY + toothH), colorEye);
            }
        }

        for (int i = 0; i <= steps; i++) {
            sprite.fillCircle((int)ptsX[i], (int)ptsY[i], 2, colorEye);
        }
    }

    void drawSingleGlyph(TFT_eSprite &sprite, const SwearGlyph &g) {
        float s = g.scale;
        if (s < 0.15f) return;

        float x = g.x;
        float y = g.y;
        float sz = 8.5f * s;

        for (int pass = 0; pass < 3; pass++) {
            uint16_t col;
            int r;
            getPaletteColors(g.palette, pass, col, r);

            if (g.type == 0) {
                float off = sz * 0.42f;
                drawSegmentPass(sprite, x - off - 1.5f, y - sz, x - off + 1.5f, y + sz, r, col);
                drawSegmentPass(sprite, x + off - 1.5f, y - sz, x + off + 1.5f, y + sz, r, col);
                drawSegmentPass(sprite, x - sz, y - off, x + sz, y - off, r, col);
                drawSegmentPass(sprite, x - sz, y + off, x + sz, y + off, r, col);
            } else if (g.type == 1) {
                drawSegmentPass(sprite, x, y - sz, x, y + sz * 0.25f, r + 1, col);
                sprite.fillCircle((int)x, (int)(y + sz * 0.85f), r + 1, col);
            } else if (g.type == 2) {
                drawSegmentPass(sprite, x - sz * 0.6f, y - sz * 0.5f, x, y - sz, r, col);
                drawSegmentPass(sprite, x, y - sz, x + sz * 0.65f, y - sz * 0.4f, r, col);
                drawSegmentPass(sprite, x + sz * 0.65f, y - sz * 0.4f, x, y + sz * 0.2f, r, col);
                sprite.fillCircle((int)x, (int)(y + sz * 0.85f), r + 1, col);
            } else if (g.type == 3) {
                float maxR = sz * 1.05f;
                float angle = 0.0f;
                float maxAngle = 2.6f * PI;
                while (angle < maxAngle) {
                    float t = angle / maxAngle;
                    float curR = t * maxR;
                    float px = x + cosf(angle) * curR;
                    float py = y + sinf(angle) * curR;
                    sprite.fillCircle((int)px, (int)py, r, col);
                    float step = (curR < 4.0f) ? 0.22f : (1.25f / curR);
                    angle += step;
                }
            } else if (g.type == 4) {
                drawSegmentPass(sprite, x + sz * 0.4f, y - sz, x - sz * 0.5f, y + sz * 0.05f, r, col);
                drawSegmentPass(sprite, x - sz * 0.5f, y + sz * 0.05f, x + sz * 0.45f, y - sz * 0.05f, r, col);
                drawSegmentPass(sprite, x + sz * 0.45f, y - sz * 0.05f, x - sz * 0.35f, y + sz, r, col);
            } else {
                drawSegmentPass(sprite, x - sz * 0.75f, y - sz * 0.75f, x + sz * 0.75f, y + sz * 0.75f, r, col);
                drawSegmentPass(sprite, x + sz * 0.75f, y - sz * 0.75f, x - sz * 0.75f, y + sz * 0.75f, r, col);
            }
        }
    }

    void drawCensorBar(TFT_eSprite &sprite, float mx, float my, float alpha, uint32_t now) {
        if (alpha < 0.08f) return;

        int barW = (int)(88.0f * alpha);
        int barH = (int)(24.0f * alpha);
        if (barW < 12 || barH < 8) return;

        float glitchX = sinf(now * 0.085f) * 3.5f;
        float glitchY = cosf(now * 0.11f) * 1.8f;

        int bx = (int)(mx - barW * 0.5f + glitchX);
        int by = (int)(my - barH * 0.5f + glitchY);

        sprite.fillRoundRect(bx - 5, by - 5, barW + 10, barH + 10, 6, colorCensorGlow2);
        sprite.fillRoundRect(bx - 2, by - 2, barW + 4, barH + 4, 4, colorCensorGlow1);
        sprite.fillRoundRect(bx, by, barW, barH, 3, TFT_BLACK);
        sprite.drawRoundRect(bx, by, barW, barH, 3, colorCensorCore);
        sprite.drawRoundRect(bx + 1, by + 1, barW - 2, barH - 2, 2, colorCensorCore);

        if (((now / 60) % 3) == 0) {
            int sliceY = by + (barH / 2) - 2;
            int shift = ((now / 30) % 2 == 0) ? 6 : -6;
            sprite.drawFastHLine(bx + shift, sliceY, barW, colorWarnCore);
            sprite.drawFastHLine(bx - shift, sliceY + 3, barW, colorCensorCore);
        }

        if (alpha > 0.65f) {
            float symSpacing = barW / 4.5f;
            float startX = mx + glitchX - symSpacing * 1.5f;
            float cyBar = my + glitchY;

            uint8_t stepShift = (now / 90) % 4;
            for (int i = 0; i < 4; i++) {
                SwearGlyph miniG;
                miniG.active = true;
                miniG.type = (i + stepShift) % 6;
                miniG.palette = ((i + stepShift) % 2 == 0) ? 1 : 2;
                miniG.x = startX + i * symSpacing;
                miniG.y = cyBar + sinf(now * 0.05f + i * 1.5f) * 1.5f;
                miniG.scale = 0.62f * alpha;
                drawSingleGlyph(sprite, miniG);
            }
        }
    }

    void updateAndDrawParticles(TFT_eSprite &sprite, uint32_t now, float dt, float mouthX, float mouthY, bool isShouting, bool isCensored) {
        uint32_t glyphInterval = isCensored ? 140 : (isShouting ? 210 : 650);
        if ((isShouting || isCensored) && (now - lastGlyphSpawn > glyphInterval)) {
            for (int i = 0; i < MAX_GLYPHS; i++) {
                if (!glyphs[i].active) {
                    glyphs[i].active = true;
                    glyphs[i].spawnTime = now;
                    glyphs[i].lifeTime = 750 + (now % 350);
                    glyphs[i].type = (now / 37 + i) % 6;
                    glyphs[i].palette = (now / 53 + i) % 3;

                    float spreadAngle = -PI * 0.5f + sinf(now * 0.017f + i * 2.1f) * 1.15f;
                    float speed = 48.0f + (now % 35);
                    glyphs[i].x = mouthX + cosf(spreadAngle) * 14.0f;
                    glyphs[i].y = mouthY - 6.0f;
                    glyphs[i].vx = cosf(spreadAngle) * speed;
                    glyphs[i].vy = sinf(spreadAngle) * speed - 18.0f;
                    glyphs[i].scale = 0.2f;
                    glyphs[i].targetScale = 0.85f + ((now % 30) * 0.012f);
                    lastGlyphSpawn = now;
                    break;
                }
            }
        }

        if ((isShouting || isCensored) && (now - lastSpitSpawn > 120)) {
            for (int i = 0; i < MAX_SPIT; i++) {
                if (!spitDrops[i].active) {
                    spitDrops[i].active = true;
                    spitDrops[i].spawnTime = now;
                    float side = ((i % 2) == 0) ? -1.0f : 1.0f;
                    float ang = -PI * 0.5f + side * (0.45f + (now % 40) * 0.015f);
                    float spd = 75.0f + (now % 45);
                    spitDrops[i].x = mouthX + side * 10.0f;
                    spitDrops[i].y = mouthY;
                    spitDrops[i].vx = cosf(ang) * spd;
                    spitDrops[i].vy = sinf(ang) * spd;
                    lastSpitSpawn = now;
                    break;
                }
            }
        }

        for (int i = 0; i < MAX_SPIT; i++) {
            if (!spitDrops[i].active) continue;
            uint32_t age = now - spitDrops[i].spawnTime;
            if (age > 450) {
                spitDrops[i].active = false;
                continue;
            }
            spitDrops[i].vy += 180.0f * dt;
            spitDrops[i].x += spitDrops[i].vx * dt;
            spitDrops[i].y += spitDrops[i].vy * dt;

            float speed = sqrtf(spitDrops[i].vx * spitDrops[i].vx + spitDrops[i].vy * spitDrops[i].vy);
            if (speed > 1.0f) {
                float dirX = spitDrops[i].vx / speed;
                float dirY = spitDrops[i].vy / speed;
                float tailX = spitDrops[i].x - dirX * 5.0f;
                float tailY = spitDrops[i].y - dirY * 5.0f;

                drawSegmentPass(sprite, tailX, tailY, spitDrops[i].x, spitDrops[i].y, 3, colorSpitGlow2);
                drawSegmentPass(sprite, tailX, tailY, spitDrops[i].x, spitDrops[i].y, 2, colorSpitGlow1);
                sprite.fillCircle((int)spitDrops[i].x, (int)spitDrops[i].y, 1, colorSpitCore);
            }
        }

        for (int i = 0; i < MAX_GLYPHS; i++) {
            if (!glyphs[i].active) continue;
            uint32_t age = now - glyphs[i].spawnTime;
            if (age > glyphs[i].lifeTime) {
                glyphs[i].active = false;
                continue;
            }

            float lifeRatio = (float)age / (float)glyphs[i].lifeTime;
            glyphs[i].x += glyphs[i].vx * dt;
            glyphs[i].y += glyphs[i].vy * dt;
            glyphs[i].vx *= (1.0f - 1.8f * dt);
            glyphs[i].vy *= (1.0f - 1.8f * dt);

            float targetS = (lifeRatio < 0.75f) ? glyphs[i].targetScale : 0.0f;
            glyphs[i].scale += (targetS - glyphs[i].scale) * 0.22f;

            if (glyphs[i].x < 18.0f || glyphs[i].x > 302.0f || glyphs[i].y < 18.0f || glyphs[i].y > 155.0f) {
                continue;
            }

            drawSingleGlyph(sprite, glyphs[i]);
        }
    }

} // anonymous namespace

void init(TFT_eSPI &tft) {
    colorEye   = tft.color565(50, 255, 100);
    colorGlow1 = tft.color565(10, 120, 30);
    colorGlow2 = tft.color565(0, 40, 10);

    colorWarnCore  = tft.color565(255, 240, 40);
    colorWarnGlow1 = tft.color565(150, 110, 5);
    colorWarnGlow2 = tft.color565(50, 35, 0);

    colorCensorCore  = tft.color565(255, 55, 85);
    colorCensorGlow1 = tft.color565(140, 15, 35);
    colorCensorGlow2 = tft.color565(45, 0, 10);

    colorSpitCore  = tft.color565(210, 255, 230);
    colorSpitGlow1 = tft.color565(30, 140, 90);
    colorSpitGlow2 = tft.color565(5, 45, 25);

    reset();
}

void reset() {
    // 1. Подхватываем текущую геометрию из предыдущей эмоции!
    FaceState::loadGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);

    // 2. Задаем целевую неоново-зеленую палитру
    FaceState::setTargetPalette(50, 255, 100, 10, 120, 30, 0, 40, 10);

    t_cx = CENTER_X;
    t_cy = BASE_Y;
    t_spacing = 100.0f;
    t_lw = 50.0f; t_rw = 50.0f;
    t_lh = 56.0f; t_rh = 56.0f;
    t_mw = 24.0f; t_mh = 6.0f;

    browOffset = -18.0f; // Брови плавно опускаются сверху
    t_browOffset = 0.0f;
    browSlant = 0.0f;
    t_browSlant = 12.0f;
    eyeAsymY = t_eyeAsymY = 0.0f;
    tremorAmp = t_tremorAmp = 0.0f;
    mouthShiftX = t_mouthShiftX = 0.0f;
    mouthOpenRatio = t_mouthOpenRatio = 0.0f;
    censorAlpha = t_censorAlpha = 0.0f;

    uint32_t now = millis();
    animStartTime = now;
    lastFrameTime = now;
    lastBlinkTime = now;
    isBlinking = false;

    for (int i = 0; i < MAX_GLYPHS; i++) glyphs[i].active = false;
    for (int i = 0; i < MAX_SPIT; i++) spitDrops[i].active = false;
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    uint32_t now = millis();
    float dt = (now - lastFrameTime) * 0.001f;
    if (dt > 0.05f) dt = 0.05f;
    lastFrameTime = now;

    colorEye   = FaceState::current.colorEye;
    colorGlow1 = FaceState::current.colorGlow1;
    colorGlow2 = FaceState::current.colorGlow2;

    uint32_t cycleMs = (now - animStartTime) % 4800;
    float t = cycleMs * 0.001f;

    bool isShouting = false;
    bool isCensored = false;

    if (t < 0.7f) {
        float p = t / 0.7f;
        t_cx = CENTER_X;
        t_cy = BASE_Y + 5.0f * sinf(p * PI * 0.5f);
        t_spacing = 96.0f;
        t_lw = 52.0f;
        t_rw = 52.0f;
        t_lh = 44.0f - 10.0f * p;
        t_rh = 40.0f - 12.0f * p;
        t_browOffset = 5.0f * p;
        t_browSlant = 14.0f + 6.0f * p;
        t_eyeAsymY = -2.0f;
        t_tremorAmp = 1.2f * p;

        t_mw = 20.0f;
        t_mh = 5.0f;
        t_mouthOpenRatio = 0.0f;
        t_mouthShiftX = 0.0f;
        t_censorAlpha = 0.0f;

    } else if (t < 2.25f) {
        isShouting = true;
        float syllableWave = sinf(now * 0.028f) * cosf(now * 0.011f);
        float sylOpen = fmaxf(0.0f, syllableWave);

        t_cx = CENTER_X + sinf(now * 0.007f) * 12.0f;
        t_cy = BASE_Y - 4.0f + cosf(now * 0.014f) * 4.5f;
        t_spacing = 102.0f;

        t_lw = 50.0f + sylOpen * 4.0f;
        t_rw = 48.0f + sylOpen * 3.0f;
        t_lh = 52.0f + sylOpen * 8.0f;
        t_rh = 42.0f + sinf(now * 0.045f) * 5.0f;

        t_browOffset = 2.0f - sylOpen * 3.0f;
        t_browSlant = 18.0f + sylOpen * 4.0f;
        t_eyeAsymY = sinf(now * 0.009f) * 3.5f;
        t_tremorAmp = 1.8f;

        t_mw = 28.0f + sylOpen * 22.0f;
        t_mh = 7.0f + sylOpen * 23.0f;
        t_mouthOpenRatio = 0.25f + sylOpen * 0.75f;
        t_mouthShiftX = sinf(now * 0.012f) * 5.0f;
        t_censorAlpha = 0.0f;

    } else if (t < 3.7f) {
        isShouting = true;
        isCensored = true;

        t_cx = CENTER_X + sinf(now * 0.015f) * 6.0f;
        t_cy = BASE_Y - 6.0f;
        t_spacing = 104.0f;

        t_lw = 54.0f;
        t_rw = 52.0f;
        t_lh = 62.0f;
        t_rh = 48.0f + sinf(now * 0.06f) * 4.0f;

        t_browOffset = 0.0f;
        t_browSlant = 22.0f;
        t_eyeAsymY = -4.0f;
        t_tremorAmp = 3.2f;

        t_mw = 50.0f;
        t_mh = 30.0f;
        t_mouthOpenRatio = 1.0f;
        t_mouthShiftX = 0.0f;
        t_censorAlpha = 1.0f;

    } else {
        float p = (t - 3.7f) / 1.1f;
        t_cx = CENTER_X + sinf(now * 0.005f) * 4.0f;
        t_cy = BASE_Y + 4.0f * sinf(p * PI);
        t_spacing = 98.0f;

        t_lw = 50.0f;
        t_rw = 50.0f;
        t_lh = 44.0f;
        t_rh = 38.0f;

        t_browOffset = 4.0f;
        t_browSlant = 15.0f;
        t_eyeAsymY = 2.0f;
        t_tremorAmp = 0.5f;

        t_mw = 26.0f + sinf(now * 0.03f) * 4.0f;
        t_mh = 5.0f;
        t_mouthOpenRatio = 0.05f;
        t_mouthShiftX = 7.0f * sinf(p * PI);
        t_censorAlpha = 0.0f;
    }

    if (!isBlinking && (now - lastBlinkTime > 1700)) {
        isBlinking = true;
        blinkStartTime = now;
        lastBlinkTime = now;
    }
    if (isBlinking) {
        if (now - blinkStartTime < 110) {
            t_lh = 6.0f;
            t_rh = 6.0f;
        } else {
            isBlinking = false;
        }
    }

    // Асимптотическая пружина
    const float speed = 0.20f;
    cx += (t_cx - cx) * speed;
    cy += (t_cy - cy) * speed;
    currentSpacing += (t_spacing - currentSpacing) * speed;
    lw += (t_lw - lw) * speed;
    rw += (t_rw - rw) * speed;
    lh += (t_lh - lh) * (isBlinking ? 0.45f : speed);
    rh += (t_rh - rh) * (isBlinking ? 0.45f : speed);
    mw += (t_mw - mw) * 0.28f;
    mh += (t_mh - mh) * 0.28f;

    browOffset += (t_browOffset - browOffset) * speed;
    browSlant += (t_browSlant - browSlant) * speed;
    eyeAsymY += (t_eyeAsymY - eyeAsymY) * speed;
    tremorAmp += (t_tremorAmp - tremorAmp) * speed;
    mouthShiftX += (t_mouthShiftX - mouthShiftX) * speed;
    mouthOpenRatio += (t_mouthOpenRatio - mouthOpenRatio) * 0.28f;
    censorAlpha += (t_censorAlpha - censorAlpha) * 0.25f;

    // Сохраняем актуальную геометрию в FaceState
    FaceState::saveGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);

    float shakeX = sinf(now * 0.075f) * tremorAmp;
    float shakeY = cosf(now * 0.092f) * tremorAmp;

    float drawCx = cx + shakeX;
    float drawCy = cy + shakeY;

    lx = drawCx - currentSpacing * 0.5f;
    rx = drawCx + currentSpacing * 0.5f;
    float ly = drawCy - eyeAsymY;
    float ry = drawCy + eyeAsymY;

    sprite.fillSprite(TFT_BLACK);

    drawGlowingEye(sprite, lx, ly, lw, lh, 16);
    drawGlowingEye(sprite, rx, ry, rw, rh, 16);

    drawAngryBrowAndMask(sprite, lx, ly, lw, lh, true, browOffset, browSlant);
    drawAngryBrowAndMask(sprite, rx, ry, rw, rh, false, browOffset, browSlant);

    float veinScale = 0.85f + 0.25f * sinf(now * 0.018f);
    float veinX = rx + rw * 0.5f + 10.0f;
    float veinY = ry - rh * 0.5f - 2.0f;
    if (veinY < 18.0f) veinY = 18.0f;
    drawVeinMark(sprite, veinX, veinY, veinScale);

    float mouthX = drawCx + mouthShiftX;
    float mouthY = drawCy + 45.0f;
    drawSwearingMouth(sprite, mouthX, mouthY, mw, mh, mouthOpenRatio, now);

    updateAndDrawParticles(sprite, now, dt, mouthX, mouthY, isShouting, isCensored);
    drawCensorBar(sprite, mouthX, mouthY, censorAlpha, now);

    sprite.pushSprite(0, 40);
}

} // namespace AnimSwear
