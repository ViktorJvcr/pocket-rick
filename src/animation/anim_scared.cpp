#include "anim_scared.h"
#include "face_state.h"
#include <math.h>

namespace AnimScared {

namespace {
    const float CENTER_X = 120.0f;
    const float BASE_Y = 135.0f;
    const float EYE_SPACING = 104.0f;

    // Палитра: Фирменный неоново-зеленый + эффекты
    uint16_t colorEye, colorGlow1, colorGlow2;
    uint16_t colorBlushCore, colorBlushGlow1, colorBlushGlow2;
    uint16_t colorSweatCore, colorSweatGlow1, colorSweatGlow2;
    uint16_t colorCatchlight;

    // Текущие координаты физики
    float cx = CENTER_X, cy = BASE_Y;
    float lx = CENTER_X - EYE_SPACING / 2.0f;
    float rx = CENTER_X + EYE_SPACING / 2.0f;
    float lw = 50.0f, rw = 50.0f;
    float lh = 80.0f, rh = 80.0f;
    float mw = 15.0f, mh = 4.0f;

    // Переменные паники
    float currentSpacing = EYE_SPACING;
    float eyeAsymY = 0.0f;
    float pupilOffsetX = 0.0f;
    float pupilOffsetY = 0.0f;
    float pupilScale = 0.15f;
    float browOffset = 8.0f;
    float browSlant = 0.0f;
    float tremorAmp = 0.0f;
    float mouthWaveAmp = 0.0f;

    // Целевые координаты
    float t_cx = CENTER_X, t_cy = BASE_Y;
    float t_lw = 54.0f, t_rw = 54.0f;
    float t_lh = 66.0f, t_rh = 66.0f;
    float t_mw = 24.0f, t_mh = 24.0f;
    float t_spacing = 100.0f;
    float t_eyeAsymY = 0.0f;
    float t_pupilX = 0.0f, t_pupilY = 0.0f;
    float t_pupilScale = 1.0f;
    float t_browOffset = 10.0f;
    float t_browSlant = 10.0f;
    float t_tremorAmp = 1.5f;
    float t_mouthWave = 2.0f;

    uint32_t startTime = 0;

    // Частое моргание в панике
    uint32_t nextBlinkTime = 0;
    bool isBlinking = false;
    uint32_t blinkEndTime = 0;

    struct SweatDrop {
        bool active;
        float x, y;
        float vx, vy;
        uint32_t spawnTime;
        uint32_t maxLife;
    };
    const int MAX_SWEAT = 6;
    SweatDrop sweats[MAX_SWEAT];
    uint32_t lastSweatSpawn = 0;

    void spawnSweat(uint32_t now) {
        for (int i = 0; i < MAX_SWEAT; i++) {
            if (!sweats[i].active) {
                sweats[i].active = true;
                bool leftSide = (random(0, 2) == 0);
                sweats[i].x = leftSide ? (lx - lw * 0.46f) : (rx + rw * 0.46f);
                sweats[i].y = cy - lh * 0.25f + random(-8, 8);
                sweats[i].vx = leftSide ? (-random(24, 42) * 0.1f) : (random(24, 42) * 0.1f);
                sweats[i].vy = -random(15, 28) * 0.1f;
                sweats[i].spawnTime = now;
                sweats[i].maxLife = random(350, 520);
                break;
            }
        }
    }

    void drawScaredEye(TFT_eSprite &sprite, float x, float y, float w, float h, float jx, float jy) {
        int ix = (int)(x + jx);
        int iy = (int)(y + jy);
        int iw = (int)w;
        int ih = (int)h;
        if (ih < 6) ih = 6;
        int r = (iw < ih ? iw : ih) / 2;

        sprite.fillRoundRect(ix - iw / 2 - 6, iy - ih / 2 - 6, iw + 12, ih + 12, r + 6, colorGlow2);
        sprite.fillRoundRect(ix - iw / 2 - 3, iy - ih / 2 - 3, iw + 6, ih + 6, r + 3, colorGlow1);
        sprite.fillRoundRect(ix - iw / 2, iy - ih / 2, iw, ih, r, colorEye);

        int baseHoleW = iw - 24;
        int baseHoleH = ih - 26;
        int holeW = (int)(baseHoleW * pupilScale);
        int holeH = (int)(baseHoleH * pupilScale);

        if (holeW > 8 && holeH > 8 && ih > 24) {
            int holeR = (holeW < holeH ? holeW : holeH) / 2;

            float maxOffX = (iw - holeW) * 0.22f;
            float maxOffY = (ih - holeH) * 0.22f;
            float pxOff = constrain(pupilOffsetX, -maxOffX, maxOffX);
            float pyOff = constrain(pupilOffsetY, -maxOffY, maxOffY);

            int px = ix + (int)pxOff;
            int py = iy + (int)pyOff;

            sprite.fillRoundRect(px - holeW / 2 - 2, py - holeH / 2 - 2, holeW + 4, holeH + 4, holeR + 2, colorGlow1);
            sprite.fillRoundRect(px - holeW / 2, py - holeH / 2, holeW, holeH, holeR, TFT_BLACK);

            int hl1X = px - holeW / 4 + (int)(jx * 0.5f);
            int hl1Y = py - holeH / 4 + (int)(jy * 0.5f);
            sprite.fillCircle(hl1X, hl1Y, 4, colorGlow1);
            sprite.fillCircle(hl1X, hl1Y, 3, colorCatchlight);

            int hl2X = px + holeW / 4;
            int hl2Y = py + holeH / 4;
            sprite.fillCircle(hl2X, hl2Y, 3, colorGlow1);
            sprite.fillCircle(hl2X, hl2Y, 2, colorCatchlight);
        }
    }

    void drawScaredBrow(TFT_eSprite &sprite, float eyeX, float eyeY, float eyeW, float eyeH, bool isLeft, float jx, float jy, uint32_t now) {
        float dir = isLeft ? -1.0f : 1.0f;
        float wave = sinf(now * 0.025f + (isLeft ? 0.0f : 1.8f)) * 2.5f;

        float x0 = eyeX - dir * (eyeW * 0.22f) + jx * 0.5f;
        float y0 = eyeY - eyeH * 0.5f - browOffset - browSlant * 0.35f + jy * 0.5f + wave;

        float x2 = eyeX + dir * (eyeW * 0.52f) + jx * 0.5f;
        float y2 = eyeY - eyeH * 0.5f - browOffset + browSlant * 0.65f + jy * 0.5f - wave;

        if (y0 < 14.0f) y0 = 14.0f;
        if (y2 < 18.0f) y2 = 18.0f;

        float x1 = (x0 + x2) * 0.5f;
        float y1 = y0 - 4.0f;

        const int steps = 14;
        for (int pass = 0; pass < 3; pass++) {
            uint16_t col = (pass == 0) ? colorGlow2 : ((pass == 1) ? colorGlow1 : colorEye);
            int rad = (pass == 0) ? 5 : ((pass == 1) ? 3 : 2);

            for (int i = 0; i <= steps; i++) {
                float t = (float)i / steps;
                float u = 1.0f - t;
                float px = u * u * x0 + 2.0f * u * t * x1 + t * t * x2;
                float py = u * u * y0 + 2.0f * u * t * y1 + t * t * y2;
                sprite.fillCircle((int)px, (int)py, rad, col);
            }
        }
    }

    void drawWobblyMouth(TFT_eSprite &sprite, float mx, float my, float w, float h, float jx, float jy, uint32_t now) {
        float cxm = mx + jx * 0.7f;
        float cym = my + jy * 0.7f;
        float rxm = w * 0.5f;
        float rym = h * 0.5f;

        const int pts = 24;
        for (int pass = 0; pass < 3; pass++) {
            uint16_t col = (pass == 0) ? colorGlow2 : ((pass == 1) ? colorGlow1 : colorEye);
            int rad = (pass == 0) ? 5 : ((pass == 1) ? 3 : 2);

            for (int i = 0; i < pts; i++) {
                float ang = (float)i * TWO_PI / pts;
                float wobble = sinf(ang * 3.0f + now * 0.038f) * mouthWaveAmp;
                float arch = -sinf(ang) * (mouthWaveAmp * 1.25f);

                float px = cxm + cosf(ang) * (rxm + wobble);
                float py = cym + sinf(ang) * (rym + wobble * 0.5f) + arch;
                sprite.fillCircle((int)px, (int)py, rad, col);
            }
        }
    }

    void drawBlush(TFT_eSprite &sprite, float eyeX, float eyeY, float eyeW, float eyeH, bool isLeft) {
        float baseX = eyeX + (isLeft ? -eyeW * 0.35f : eyeW * 0.35f);
        float baseY = eyeY + eyeH * 0.5f + 9.0f;

        for (int pass = 0; pass < 3; pass++) {
            uint16_t col = (pass == 0) ? colorBlushGlow2 : ((pass == 1) ? colorBlushGlow1 : colorBlushCore);
            int rad = (pass == 0) ? 4 : ((pass == 1) ? 2 : 1);

            for (int line = -1; line <= 1; line++) {
                float lx0 = baseX + line * 7.0f - 2.0f;
                float ly0 = baseY + 3.0f;
                float lx1 = baseX + line * 7.0f + 2.0f;
                float ly1 = baseY - 3.0f;

                for (int s = 0; s <= 4; s++) {
                    float t = s / 4.0f;
                    int px = (int)(lx0 + (lx1 - lx0) * t);
                    int py = (int)(ly0 + (ly1 - ly0) * t);
                    sprite.fillCircle(px, py, rad, col);
                }
            }
        }
    }

    void updateAndDrawSweat(TFT_eSprite &sprite, uint32_t now) {
        for (int i = 0; i < MAX_SWEAT; i++) {
            if (!sweats[i].active) continue;

            uint32_t age = now - sweats[i].spawnTime;
            if (age > sweats[i].maxLife) {
                sweats[i].active = false;
                continue;
            }

            sweats[i].x += sweats[i].vx;
            sweats[i].y += sweats[i].vy;
            sweats[i].vy += 0.16f;

            float speed = sqrtf(sweats[i].vx * sweats[i].vx + sweats[i].vy * sweats[i].vy);
            if (speed < 0.1f) speed = 0.1f;
            float dirX = -sweats[i].vx / speed;
            float dirY = -sweats[i].vy / speed;

            for (int pass = 0; pass < 3; pass++) {
                uint16_t col = (pass == 0) ? colorSweatGlow2 : ((pass == 1) ? colorSweatGlow1 : colorSweatCore);
                int baseR = (pass == 0) ? 5 : ((pass == 1) ? 3 : 2);

                for (int s = 3; s >= 0; s--) {
                    float px = sweats[i].x + dirX * (s * 2.5f);
                    float py = sweats[i].y + dirY * (s * 2.5f);
                    int r = baseR - (s / 2);
                    if (r < 1) r = 1;
                    sprite.fillCircle((int)px, (int)py, r, col);
                }
            }
        }
    }

} // anonymous namespace

void init(TFT_eSPI &tft) {
    colorEye        = tft.color565(50, 255, 100);
    colorGlow1      = tft.color565(10, 120, 30);
    colorGlow2      = tft.color565(0, 40, 10);
    colorCatchlight = tft.color565(220, 255, 230);

    colorBlushCore  = tft.color565(255, 110, 180);
    colorBlushGlow1 = tft.color565(130, 30, 80);
    colorBlushGlow2 = tft.color565(45, 5, 25);

    colorSweatCore  = tft.color565(200, 250, 255);
    colorSweatGlow1 = tft.color565(30, 120, 160);
    colorSweatGlow2 = tft.color565(5, 35, 55);

    reset();
}

void reset() {
    startTime = millis();
    lastSweatSpawn = startTime;
    nextBlinkTime = startTime + 600;
    isBlinking = false;

    // 1. Подхватываем точное положение глаз и рта из предыдущей эмоции!
    FaceState::loadGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);

    // 2. Задаем целевую неоново-зеленую палитру
    FaceState::setTargetPalette(50, 255, 100, 10, 120, 30, 0, 40, 10);

    eyeAsymY = 0.0f;
    pupilOffsetX = 0.0f;
    pupilOffsetY = 0.0f;
    pupilScale = 0.15f; // Зрачки плавно раскрываются из центра глаза!
    browOffset = 20.0f; // Брови плавно опускаются сверху
    browSlant = 0.0f;
    tremorAmp = 0.0f;
    mouthWaveAmp = 0.0f;

    for (int i = 0; i < MAX_SWEAT; i++) {
        sweats[i].active = false;
    }
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    uint32_t now = millis();
    uint32_t cycleTime = (now - startTime) % 3800;

    // Берем плавно интерполируемый цвет из FaceState
    colorEye   = FaceState::current.colorEye;
    colorGlow1 = FaceState::current.colorGlow1;
    colorGlow2 = FaceState::current.colorGlow2;

    if (!isBlinking && now >= nextBlinkTime) {
        isBlinking = true;
        blinkEndTime = now + 85;
        nextBlinkTime = now + ((random(0, 100) < 40) ? 220 : random(750, 1400));
    }
    if (isBlinking && now >= blinkEndTime) {
        isBlinking = false;
    }

    float heartBeat = sinf(now * 0.032f);

    if (cycleTime < 240) {
        t_cx = CENTER_X;
        t_cy = BASE_Y - 8.0f;
        t_lw = 60.0f; t_rw = 60.0f;
        t_lh = 48.0f; t_rh = 48.0f;
        t_spacing = 94.0f;
        t_eyeAsymY = 0.0f;
        t_pupilX = 0.0f;
        t_pupilY = -3.0f;
        t_pupilScale = 0.62f;
        t_mw = 34.0f; t_mh = 18.0f;
        t_mouthWave = 2.4f;
        t_browOffset = 10.0f;
        t_browSlant = 11.0f;
        t_tremorAmp = 2.0f;

    } else if (cycleTime < 2200) {
        float panicSweep = sinf((cycleTime - 240) * 0.0095f);
        float fastWobble = cosf(now * 0.020f);

        t_cx = CENTER_X + panicSweep * 15.0f;
        t_cy = BASE_Y - 2.0f + fastWobble * 4.0f;

        t_pupilX = panicSweep * 7.0f;
        t_pupilY = fastWobble * 4.0f;
        t_pupilScale = 0.78f + heartBeat * 0.14f;

        float asym = panicSweep * 9.0f;
        t_lw = 55.0f - asym * 0.35f;
        t_rw = 55.0f + asym * 0.35f;
        t_lh = 66.0f - asym;
        t_rh = 66.0f + asym;

        t_eyeAsymY = panicSweep * 5.5f;
        t_spacing = 98.0f + heartBeat * 2.0f;

        t_mw = 26.0f + heartBeat * 5.0f;
        t_mh = 26.0f - heartBeat * 5.0f;
        t_mouthWave = 2.0f;

        t_browOffset = 10.0f;
        t_browSlant = 11.0f;
        t_tremorAmp = 1.5f;

        if (now - lastSweatSpawn > 180) {
            spawnSweat(now);
            lastSweatSpawn = now;
        }

    } else {
        float dizzySway = sinf(now * 0.014f) * 11.0f;
        t_cx = CENTER_X + dizzySway;
        t_cy = BASE_Y - 5.0f + heartBeat * 3.0f;

        t_pupilX = dizzySway * 0.5f;
        t_pupilY = 6.5f;
        t_pupilScale = 0.72f + heartBeat * 0.12f;

        t_lw = 57.0f + heartBeat * 2.0f;
        t_rw = 57.0f - heartBeat * 2.0f;
        t_lh = 68.0f + sinf(now * 0.024f) * 4.0f;
        t_rh = 68.0f - sinf(now * 0.024f) * 4.0f;

        t_eyeAsymY = cosf(now * 0.024f) * 4.0f;
        t_spacing = 96.0f;

        t_mw = 34.0f + heartBeat * 4.0f;
        t_mh = 25.0f + cosf(now * 0.032f) * 4.0f;
        t_mouthWave = 2.8f;

        t_browOffset = 11.0f;
        t_browSlant = 12.0f;
        t_tremorAmp = 1.8f;

        if (now - lastSweatSpawn > 150) {
            spawnSweat(now);
            lastSweatSpawn = now;
        }
    }

    if (isBlinking) {
        t_lh = 8.0f;
        t_rh = 8.0f;
    }

    float bodySpeed = (cycleTime < 240) ? 0.25f : 0.16f;
    float eyeSpeed  = isBlinking ? 0.42f : 0.18f;

    cx += (t_cx - cx) * bodySpeed;
    cy += (t_cy - cy) * bodySpeed;
    lw += (t_lw - lw) * eyeSpeed;
    rw += (t_rw - rw) * eyeSpeed;
    lh += (t_lh - lh) * eyeSpeed;
    rh += (t_rh - rh) * eyeSpeed;
    mw += (t_mw - mw) * bodySpeed;
    mh += (t_mh - mh) * bodySpeed;
    currentSpacing += (t_spacing - currentSpacing) * bodySpeed;
    eyeAsymY += (t_eyeAsymY - eyeAsymY) * 0.18f;

    pupilOffsetX += (t_pupilX - pupilOffsetX) * 0.22f;
    pupilOffsetY += (t_pupilY - pupilOffsetY) * 0.22f;
    pupilScale   += (t_pupilScale - pupilScale) * 0.22f;

    browOffset += (t_browOffset - browOffset) * bodySpeed;
    browSlant += (t_browSlant - browSlant) * bodySpeed;
    tremorAmp += (t_tremorAmp - tremorAmp) * 0.15f;
    mouthWaveAmp += (t_mouthWave - mouthWaveAmp) * 0.18f;

    // Сохраняем текущую геометрию в общее состояние (с учетом асимметрии глаз)
    FaceState::saveGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);

    lx = cx - currentSpacing * 0.5f;
    rx = cx + currentSpacing * 0.5f;

    float jx = sinf(now * 0.075f) * tremorAmp;
    float jy = cosf(now * 0.085f) * tremorAmp;

    sprite.fillSprite(TFT_BLACK);

    float leftEyeY  = cy + eyeAsymY;
    float rightEyeY = cy - eyeAsymY;

    drawBlush(sprite, lx, leftEyeY, lw, lh, true);
    drawBlush(sprite, rx, rightEyeY, rw, rh, false);

    drawScaredEye(sprite, lx, leftEyeY, lw, lh, jx, jy);
    drawScaredEye(sprite, rx, rightEyeY, rw, rh, -jx, jy);

    drawScaredBrow(sprite, lx, leftEyeY, lw, lh, true, jx, jy, now);
    drawScaredBrow(sprite, rx, rightEyeY, rw, rh, false, jx, jy, now);

    drawWobblyMouth(sprite, cx, cy + 44.0f, mw, mh, jx, jy, now);

    updateAndDrawSweat(sprite, now);

    sprite.pushSprite(0, 0);
}

} // namespace AnimScared
