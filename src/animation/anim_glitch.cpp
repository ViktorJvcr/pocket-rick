#include "anim_glitch.h"
#include "face_state.h"
#include <Arduino.h>
#include <math.h>

// ============================================================================
//  AnimGlitch — «Экзистенциальный кризис»
//  Долгое удержание кнопки Mid. Грустная, созерцательная анимация:
//  веки медленно опускаются, робот тяжело «дышит», по щеке иногда
//  скатывается слеза, а вокруг тихо падают осенние листья, собираясь
//  в кучку у подножия лица. Палитра — холодная, тусклая, без неона.
// ============================================================================

namespace AnimGlitch {

namespace {

    // ---------- Общие константы ----------
    const float LOCAL_PI = 3.14159265f;

    // Геометрия глаз/рта хранится в статических переменных уровня файла,
    // чтобы следующая анимация могла продолжить с тех же значений
    // (см. FaceState::saveGeometry в конце runStep).
    float g_cx, g_cy, g_sp, g_lw, g_rw, g_lh, g_rh, g_mw, g_mh;

    uint32_t startTime = 0;   // момент reset() — точка отсчёта фаз
    uint32_t lastFrame  = 0;  // ограничитель кадровой частоты

    // Насколько сильно опущены веки (0 — открыты, 1 — почти закрыты).
    float eyelidProgress = 0.0f;

    // ---------- Палитра сцены (не связана с палитрой лица) ----------
    uint16_t bgColor     = 0;   // почти чёрный сине-серый фон
    uint16_t groundColor = 0;   // тёмная «земля» внизу
    uint16_t haloColors[3] = {0, 0, 0}; // холодное гало вокруг лица
    uint16_t tearColor   = 0;   // бледный блик слезы
    uint16_t leafColors[4] = {0, 0, 0, 0}; // осенние оттенки листьев

    // ---------- Падающие листья ----------
    const int MAX_FALLING_LEAVES = 8;
    struct FallingLeaf {
        bool  active;
        float x, y;
        float vx, vy;
        float phase;      // фаза флаттера (покачивания)
        float size;
        float settleY;    // высота, на которой лист «ляжет» на землю
        uint8_t colorIdx;
    };
    FallingLeaf fallingLeaves[MAX_FALLING_LEAVES];
    uint32_t nextLeafSpawn = 0;

    // ---------- Кучка осевших листьев (кольцевой буфер) ----------
    const int MAX_PILE_LEAVES = 14;
    struct PileLeaf {
        bool    used;
        float   x, y;
        float   size;
        uint8_t colorIdx;
    };
    PileLeaf pile[MAX_PILE_LEAVES];
    int pileWriteIndex = 0;

    // ---------- Слеза ----------
    enum TearState { TEAR_WAIT = 0, TEAR_FORM = 1, TEAR_FALL = 2 };
    TearState tearState = TEAR_WAIT;
    uint32_t  tearTimer = 0;      // момент следующего события
    uint32_t  tearFormStart = 0;  // момент начала набухания капли
    float     tearBulge = 0.0f;   // 0..1 — рост капли у края века
    float     tearX = 0.0f, tearY = 0.0f, tearVy = 0.0f;

    const int TEAR_TRAIL_LEN = 6;
    float tearTrail[TEAR_TRAIL_LEN]; // позиции Y следа (сверху вниз)
    int   tearTrailCount = 0;

    // ------------------------------------------------------------------
    //  Вспомогательные функции
    // ------------------------------------------------------------------

    inline int clampi(int v, int lo, int hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    inline float clampf(float v, float lo, float hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    // Рисует капсулу глаза (скруглённый прямоугольник), нижний край
    // которой зафиксирован, а верхний «подтаивает» под веком.
    void drawEyeCapsule(TFT_eSprite &spr, float cxEye, float bottomY,
                         float w, float h, uint16_t color) {
        int iw = clampi((int)w, 2, 239);
        int ih = clampi((int)h, 2, 199);
        int ix = clampi((int)(cxEye - w * 0.5f), 0, 239 - iw);
        int iy = clampi((int)(bottomY - h), 0, 199 - ih);
        int r  = (int)(fminf((float)iw, (float)ih) * 0.4f);
        if (r < 1) r = 1;
        spr.fillRoundRect(ix, iy, iw, ih, r, color);
    }

    // Слой свечения вокруг глаза — просто более крупный скруглённый
    // прямоугольник того же центра, масштабированный коэффициентом scale.
    void drawGlowLayer(TFT_eSprite &spr, float cxEye, float centerY,
                        float w, float h, float scale, uint16_t color) {
        float gw = w * scale;
        float gh = h * scale;
        int iw = clampi((int)gw, 2, 239);
        int ih = clampi((int)gh, 2, 199);
        int ix = clampi((int)(cxEye - gw * 0.5f), 0, 239 - iw);
        int iy = clampi((int)(centerY - gh * 0.5f), 0, 199 - ih);
        int r  = (int)(fminf((float)iw, (float)ih) * 0.4f);
        if (r < 1) r = 1;
        spr.fillRoundRect(ix, iy, iw, ih, r, color);
    }

    // Грустный рот — ломаная из коротких отрезков, парабола с вершиной
    // вверху и опущенными уголками (хмурая дуга).
    void drawMouth(TFT_eSprite &spr, float cx, float mouthY,
                    float mw, float mh, float jitter) {
        const int SEGMENTS = 6;
        int prevX = 0, prevY = 0;
        for (int i = 0; i <= SEGMENTS; i++) {
            float tNorm = (float)i / SEGMENTS * 2.0f - 1.0f; // -1..1
            float x = cx + tNorm * mw;
            float y = mouthY + tNorm * tNorm * mh + jitter;
            int ix = clampi((int)x, 0, 239);
            int iy = clampi((int)y, 0, 199);
            if (i > 0) {
                spr.drawLine(prevX, prevY, ix, iy, TFT_WHITE);
            }
            prevX = ix;
            prevY = iy;
        }
    }

    // Создаёт новый падающий лист в свободном слоте массива.
    void spawnLeaf() {
        for (int i = 0; i < MAX_FALLING_LEAVES; i++) {
            if (!fallingLeaves[i].active) {
                FallingLeaf &l = fallingLeaves[i];
                l.active   = true;
                l.x        = (float)random(0, 240);
                l.y        = (float)random(-30, -5);
                l.vx       = (float)random(-15, 16) / 100.0f;
                l.vy       = (float)random(25, 55) / 100.0f;
                l.phase    = (float)random(0, 628) / 100.0f;
                l.size     = (float)random(5, 9);
                l.settleY  = (float)random(180, 197);
                l.colorIdx = (uint8_t)random(0, 4);
                break;
            }
        }
    }

    // Обновляет и рисует падающие листья; долетевшие до земли уходят в кучу.
    void updateAndDrawFallingLeaves(TFT_eSprite &spr, float t) {
        for (int i = 0; i < MAX_FALLING_LEAVES; i++) {
            FallingLeaf &l = fallingLeaves[i];
            if (!l.active) continue;

            // Боковой дрейф и лёгкое покачивание (флаттер).
            l.x += l.vx + sinf(t * 0.0015f + l.phase) * 0.4f;
            l.y += l.vy;
            l.phase += 0.05f;

            // Лист унесло далеко за край экрана — считаем потерянным.
            if (l.x < -20.0f || l.x > 260.0f) {
                l.active = false;
                continue;
            }

            // Долетел до земли — переносим в кучу листьев.
            if (l.y >= l.settleY) {
                PileLeaf &p = pile[pileWriteIndex];
                p.used     = true;
                p.x        = clampf(l.x, 4.0f, 235.0f);
                p.y        = clampf(l.settleY, 180.0f, 197.0f);
                p.size     = l.size * 0.9f;
                p.colorIdx = l.colorIdx;
                pileWriteIndex = (pileWriteIndex + 1) % MAX_PILE_LEAVES;
                l.active = false;
                continue;
            }

            // Отрисовка: эллипс с переменной шириной (имитация вращения
            // листа при падении) + короткий черешок.
            float flutter = sinf(l.phase) * 0.5f + 0.5f;
            int rx = clampi((int)(l.size * (0.35f + 0.65f * flutter)), 1, 20);
            int ry = clampi((int)(l.size * 0.6f), 1, 20);
            int ix = clampi((int)l.x, 0, 239);
            int iy = clampi((int)l.y, 0, 199);
            uint16_t col = leafColors[l.colorIdx];

            spr.fillEllipse(ix, iy, rx, ry, col);
            int stemY1 = clampi(iy - ry, 0, 199);
            int stemY2 = clampi(iy - ry - 3, 0, 199);
            spr.drawLine(ix, stemY1, ix, stemY2, col);
        }
    }

    // Рисует кучку осевших листьев у подножия лица.
    void drawPileLeaves(TFT_eSprite &spr) {
        for (int i = 0; i < MAX_PILE_LEAVES; i++) {
            if (!pile[i].used) continue;
            int rx = clampi((int)(pile[i].size * 0.9f), 1, 16);
            int ry = clampi((int)(pile[i].size * 0.55f), 1, 16);
            int ix = clampi((int)pile[i].x, 0, 239);
            int iy = clampi((int)pile[i].y, 0, 199);
            spr.fillEllipse(ix, iy, rx, ry, leafColors[pile[i].colorIdx]);
        }
    }

    // Обновляет состояние слезы (набухание у края нижнего века / падение).
    void updateTear(uint32_t now, float cornerX, float cornerY) {
        switch (tearState) {
            case TEAR_WAIT:
                if (now >= tearTimer) {
                    tearState = TEAR_FORM;
                    tearFormStart = now;
                    tearBulge = 0.0f;
                    tearX = cornerX;
                    tearY = cornerY;
                }
                break;

            case TEAR_FORM:
                tearBulge = (float)(now - tearFormStart) / 1800.0f;
                if (tearBulge >= 1.0f) {
                    tearBulge = 1.0f;
                    tearState = TEAR_FALL;
                    tearVy = 0.12f;
                    tearTrailCount = 0;
                    for (int i = 0; i < TEAR_TRAIL_LEN; i++) tearTrail[i] = -100.0f;
                }
                break;

            case TEAR_FALL:
                tearVy += 0.015f; // лёгкое ускорение падения
                tearY  += tearVy;
                // сдвигаем след вниз по буферу
                for (int i = TEAR_TRAIL_LEN - 1; i > 0; i--) {
                    tearTrail[i] = tearTrail[i - 1];
                }
                tearTrail[0] = tearY;
                if (tearTrailCount < TEAR_TRAIL_LEN) tearTrailCount++;

                if (tearY > 205.0f) {
                    tearState = TEAR_WAIT;
                    tearTimer = now + (uint32_t)random(7000, 15000);
                }
                break;
        }
    }

    // Отрисовывает слезу и её затухающий след.
    void drawTear(TFT_eSprite &spr) {
        if (tearState == TEAR_FORM) {
            int r = clampi((int)(1.0f + tearBulge * 2.5f), 1, 4);
            int ix = clampi((int)tearX, 0, 239);
            int iy = clampi((int)tearY, 0, 199);
            spr.fillCircle(ix, iy, r, tearColor);
        } else if (tearState == TEAR_FALL) {
            // след — уменьшающиеся кружки позади капли
            for (int i = tearTrailCount - 1; i >= 1; i--) {
                int r = clampi(3 - i, 1, 3);
                if (tearTrail[i] < -50.0f) continue;
                int ix = clampi((int)tearX, 0, 239);
                int iy = clampi((int)tearTrail[i], 0, 199);
                spr.fillCircle(ix, iy, r, tearColor);
            }
            int ix = clampi((int)tearX, 0, 239);
            int iy = clampi((int)tearY, 0, 199);
            spr.fillCircle(ix, iy, 3, tearColor);
        }
    }

    // Рисует холодный почти-чёрный фон, мягкое гало и тёмную землю.
    void drawBackground(TFT_eSprite &spr, float cx, float cy) {
        spr.fillSprite(bgColor);

        int icx = clampi((int)cx, 0, 239);
        int icy = clampi((int)cy, 0, 199);

        // Несколько концентрических кругов — едва заметное холодное гало.
        spr.fillCircle(icx, icy, 110, haloColors[0]);
        spr.fillCircle(icx, icy, 85,  haloColors[1]);
        spr.fillCircle(icx, icy, 60,  haloColors[2]);

        // Приглушённая земля внизу экрана.
        int groundY = 182;
        spr.fillRect(0, groundY, 240, 200 - groundY, groundColor);
    }

} // namespace

// ============================================================================
//  init — вызывается один раз при старте прошивки
// ============================================================================
void init(TFT_eSPI &tft) {
    // Палитра фона/земли/гало — холодные сине-серые тона.
    bgColor     = tft.color565(9, 11, 18);
    groundColor = tft.color565(24, 23, 22);
    haloColors[0] = tft.color565(14, 18, 28);
    haloColors[1] = tft.color565(12, 15, 24);
    haloColors[2] = tft.color565(10, 13, 20);

    // Бледный ледяной блик для слезы.
    tearColor = tft.color565(205, 222, 232);

    // Осенние ржаво-оливковые оттенки листьев.
    leafColors[0] = tft.color565(170, 100, 55);  // ржавый
    leafColors[1] = tft.color565(140, 120, 45);  // приглушённый амбер
    leafColors[2] = tft.color565(110, 85, 40);   // тёмный умбра
    leafColors[3] = tft.color565(150, 140, 85);  // тусклый оливковый

    for (int i = 0; i < MAX_FALLING_LEAVES; i++) fallingLeaves[i].active = false;
    for (int i = 0; i < MAX_PILE_LEAVES; i++) pile[i].used = false;
}

// ============================================================================
//  reset — вызывается при входе в анимацию
// ============================================================================
void reset() {
    uint32_t now = millis();
    startTime = now;
    lastFrame = now;

    // Холодная, тусклая палитра лица: пыльно-бирюзовая радужка,
    // тёмно-стальное внутреннее свечение, почти чёрно-синее внешнее.
    FaceState::setTargetPalette(120, 180, 200,
                                 30, 60, 90,
                                 8, 16, 30);

    // Стартовая геометрия — берём сохранённое состояние лица.
    g_cx = 120; g_cy = 100; g_sp = 100;
    g_lw = 50; g_rw = 50; g_lh = 80; g_rh = 80;
    g_mw = 15; g_mh = 4;
    FaceState::loadGeometry(g_cx, g_cy, g_sp, g_lw, g_rw, g_lh, g_rh, g_mw, g_mh);

    eyelidProgress = 0.0f;

    for (int i = 0; i < MAX_FALLING_LEAVES; i++) fallingLeaves[i].active = false;
    for (int i = 0; i < MAX_PILE_LEAVES; i++) pile[i].used = false;
    pileWriteIndex = 0;
    nextLeafSpawn = now + 400;

    tearState = TEAR_WAIT;
    tearTimer = now + (uint32_t)random(3000, 6000);
    tearBulge = 0.0f;
    tearTrailCount = 0;
}

// ============================================================================
//  runStep — один кадр анимации
// ============================================================================
void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    (void)tft;
    uint32_t now = millis();
    if (now - lastFrame < 16) return;
    lastFrame = now;

    float t = (float)(now - startTime); // мс с начала анимации

    // ---------- Дыхание и вздохи ----------
    float breathe = sinf(t * (2.0f * LOCAL_PI / 7000.0f));
    // Короткий всплеск «вздоха» около пика синусоиды.
    float sighEnvelope = 0.0f;
    if (breathe > 0.85f) {
        sighEnvelope = (breathe - 0.85f) / 0.15f;
    }

    // ---------- Опускание век ----------
    // Цель прогресса растёт первые ~5 секунд, затем чуть «дышит».
    float eyelidTarget = clampf(t / 5000.0f, 0.0f, 1.0f) * 0.78f;
    eyelidTarget += sighEnvelope * 0.12f; // во время вздоха веки чуть сильнее опускаются
    eyelidProgress += (eyelidTarget - eyelidProgress) * 0.02f;
    eyelidProgress = clampf(eyelidProgress, 0.0f, 0.95f);

    // ---------- Лёгкая дрожь/опускание взгляда ----------
    float breatheY = breathe * 1.5f;
    float sighShiftY = sighEnvelope * 1.5f;
    float eyeCenterY = g_cy + breatheY + sighShiftY;

    // ---------- Свечение: медленная пульсация с затуханием к концу цикла ----------
    float pulseFast = 0.5f + 0.5f * sinf(t * (2.0f * LOCAL_PI / 3200.0f));
    float envelopeSlow = 0.55f + 0.45f * sinf(t * (2.0f * LOCAL_PI / 22000.0f));
    float glowScale = 1.15f + 0.10f * pulseFast * envelopeSlow;
    float glowScaleOuter = glowScale + 0.22f;

    // ---------- Эффективные размеры глаз с учётом опущенных век ----------
    float effLH = g_lh * (1.0f - eyelidProgress * 0.55f);
    float effRH = g_rh * (1.0f - eyelidProgress * 0.55f);
    if (effLH < 12.0f) effLH = 12.0f;
    if (effRH < 12.0f) effRH = 12.0f;

    float leftX  = g_cx - g_sp * 0.5f;
    float rightX = g_cx + g_sp * 0.5f;

    // Нижний край глаза фиксирован — верх «подтаивает» под веком.
    float leftBottomY  = eyeCenterY + g_lh * 0.5f;
    float rightBottomY = eyeCenterY + g_rh * 0.5f;

    // ---------- Фон ----------
    drawBackground(sprite, g_cx, eyeCenterY);

    // ---------- Кучка листьев на земле (рисуется до лица) ----------
    drawPileLeaves(sprite);

    // ---------- Свечение вокруг глаз ----------
    float leftGlowCenterY  = leftBottomY  - effLH * 0.5f;
    float rightGlowCenterY = rightBottomY - effRH * 0.5f;

    drawGlowLayer(sprite, leftX,  leftGlowCenterY,  g_lw, g_lh,
                  glowScaleOuter, FaceState::current.colorGlow2);
    drawGlowLayer(sprite, rightX, rightGlowCenterY, g_rw, g_rh,
                  glowScaleOuter, FaceState::current.colorGlow2);

    drawGlowLayer(sprite, leftX,  leftGlowCenterY,  g_lw, g_lh,
                  glowScale, FaceState::current.colorGlow1);
    drawGlowLayer(sprite, rightX, rightGlowCenterY, g_rw, g_rh,
                  glowScale, FaceState::current.colorGlow1);

    // ---------- Сами глаза ----------
    drawEyeCapsule(sprite, leftX,  leftBottomY,  g_lw, effLH, FaceState::current.colorEye);
    drawEyeCapsule(sprite, rightX, rightBottomY, g_rw, effRH, FaceState::current.colorEye);

    // ---------- Слеза (левый глаз, нижний внутренний угол) ----------
    float tearCornerX = leftX + g_lw * 0.28f;
    float tearCornerY = leftBottomY - effLH * 0.12f;
    updateTear(now, tearCornerX, tearCornerY);
    drawTear(sprite);

    // ---------- Рот — маленькая печальная дуга, дрожащая при вздохе ----------
    float mouthY = eyeCenterY + fmaxf(g_lh, g_rh) * 0.5f + 22.0f;
    float mouthJitter = 0.0f;
    if (sighEnvelope > 0.0f) {
        mouthJitter = (float)random(-1, 2) * sighEnvelope;
    }
    drawMouth(sprite, g_cx, mouthY, g_mw, g_mh, mouthJitter);

    // ---------- Падающие листья поверх сцены ----------
    if (now >= nextLeafSpawn) {
        spawnLeaf();
        nextLeafSpawn = now + (uint32_t)random(900, 1800);
    }
    updateAndDrawFallingLeaves(sprite, t);

    // ---------- Сохраняем геометрию для следующей анимации ----------
    FaceState::saveGeometry(g_cx, eyeCenterY, g_sp, g_lw, g_rw, g_lh, g_rh, g_mw, g_mh);

    sprite.pushSprite(0, 40);
}

} // namespace AnimGlitch
