#include "anim_anger.h"
#include "face_state.h"
#include <math.h>
#include "sensors/imu_sensor.h"

namespace AnimAnger {

namespace {
    // Цвета (Температурная палитра огня)
    uint16_t colorEye;      // Ярко-красный (база)
    uint16_t colorGlow1;    // Среднее свечение (темно-красный)
    uint16_t colorGlow2;    // Внешнее свечение (бордовый дым)
    uint16_t colorSpark;    // Ярко-желтый/оранжевый (пламя и искры)
    uint16_t colorFireHot;  // Почти белый (раскаленное ядро внутри глаза)

    // Константы координат
    const float CENTER_X = 120.0f;
    const float BASE_Y = 100.0f;
    const float EYE_SPACING = 104.0f;

    // Переменные физики (текущие)
    float cx = CENTER_X, cy = BASE_Y;
    float currentSpacing = EYE_SPACING;
    float lw = 50.0f, rw = 50.0f;
    float lh = 80.0f, rh = 80.0f;
    float mw = 15.0f, mh = 4.0f;
    float browOffset = -32.0f;

    // Переменные физики (целевые)
    float t_cx = CENTER_X, t_cy = BASE_Y;
    float t_spacing = EYE_SPACING;
    float t_lw = 48.0f, t_rw = 48.0f;
    float t_lh = 54.0f, t_rh = 54.0f;
    float t_mw = 38.0f, t_mh = 10.0f;
    float t_browOffset = 0.0f;

    // Тайминги
    uint32_t startTime = 0;
    uint32_t lastTwitchTime = 0;

    // Частицы (Искры по бокам экрана)
    struct Spark {
        float x, y;
        float vy, vx;
        uint8_t size;
        bool active;
    };
    const int MAX_SPARKS = 10;
    Spark sparks[MAX_SPARKS];

    // Базовый прямоугольник глаза со свечением
    void drawGlowingRoundRect(TFT_eSprite &sprite, int x, int y, int w, int h, int r) {
        sprite.fillRoundRect(x - 6, y - 6, w + 12, h + 12, r + 4, colorGlow2);
        sprite.fillRoundRect(x - 3, y - 3, w + 6, h + 6, r + 2, colorGlow1);
        sprite.fillRoundRect(x, y, w, h, r, colorEye);
    }

    // Аккуратное, плотное и стильное пламя без визуальной каши
    void drawEyeFlames(TFT_eSprite &sprite, float ex, float ey, float ew, float eh, float elapsed, int seedOffset) {
        float topY = ey - eh / 2.0f;
        float bottomY = ey + eh / 2.0f;

        int numOuter = 4;
        float outSpacing = 6.5f; 
        float outStartX = ex - (outSpacing * (numOuter - 1)) / 2.0f;

        for (int i = 0; i < numOuter; i++) {
            float bx = outStartX + i * outSpacing;
            float by = topY + 4.0f;

            float phase = elapsed * 12.0f + i * 1.3f + seedOffset;
            float flameH = 9.0f + sin(phase) * 4.5f; 
            float swayX = cos(elapsed * 8.0f + i) * 1.5f; 

            int tipX = (int)(bx + swayX);
            int tipY = (int)(by - flameH);

            sprite.fillTriangle(bx - 6, by, bx + 6, by, tipX, tipY - 3, colorGlow2);
            sprite.fillTriangle(bx - 4, by, bx + 4, by, tipX, tipY - 1, colorGlow1);
            sprite.fillTriangle(bx - 3, by + 1, bx + 3, by + 1, tipX, tipY, colorEye);
        }

        int numInner = 5;
        float inSpacing = 5.0f; 
        float inStartX = ex - (inSpacing * (numInner - 1)) / 2.0f;
        float baseBy = bottomY - 6.0f;

        sprite.fillRoundRect(ex - 13, baseBy - 3, 26, 6, 3, colorSpark);
        for (int i = 0; i < numInner; i++) {
            float bx = inStartX + i * inSpacing;
            float centerBoost = (i == 2) ? 6.0f : ((i == 1 || i == 3) ? 3.0f : 0.0f);
            float phase = elapsed * 14.0f + i * 1.1f + seedOffset;
            
            float flameH = 9.0f + centerBoost + sin(phase) * 3.5f;
            float swayX = sin(elapsed * 10.0f + i * 0.8f) * 1.2f;

            sprite.fillTriangle(bx - 4, baseBy, bx + 4, baseBy, bx + swayX, baseBy - flameH, colorSpark);
        }

        sprite.fillRoundRect(ex - 8, baseBy - 2, 16, 4, 2, colorFireHot);
        for (int i = 1; i <= 3; i++) {
            float bx = inStartX + i * inSpacing;
            float centerBoost = (i == 2) ? 4.0f : 1.5f;
            float phase = elapsed * 14.0f + i * 1.1f + seedOffset;
            float flameH = 5.0f + centerBoost + sin(phase) * 2.0f;

            sprite.fillTriangle(bx - 3, baseBy, bx + 3, baseBy, bx, baseBy - flameH, colorFireHot);
        }
    }

    void drawVeinSymbol(TFT_eSprite &sprite, int vx, int vy, float scale) {
        int offset = 6 * scale;
        int len = 7 * scale;
        auto drawVeinLine = [&](int x0, int y0, int x1, int y1) {
            sprite.drawWideLine(x0, y0, x1, y1, 4, colorGlow1);
            sprite.drawWideLine(x0, y0, x1, y1, 2, colorSpark);
        };
        drawVeinLine(vx - offset, vy - offset - len, vx - offset, vy - offset);
        drawVeinLine(vx - offset - len, vy - offset, vx - offset, vy - offset);
        drawVeinLine(vx + offset, vy - offset - len, vx + offset, vy - offset);
        drawVeinLine(vx + offset + len, vy - offset, vx + offset, vy - offset);
        drawVeinLine(vx - offset, vy + offset + len, vx - offset, vy + offset);
        drawVeinLine(vx - offset - len, vy + offset, vx - offset, vy + offset);
        drawVeinLine(vx + offset, vy + offset + len, vx + offset, vy + offset);
        drawVeinLine(vx + offset + len, vy + offset, vx + offset, vy + offset);
    }

    void updateSparks() {
        for (int i = 0; i < MAX_SPARKS; i++) {
            if (!sparks[i].active) {
                if (random(0, 100) < 20) {
                    sparks[i].active = true;
                    sparks[i].x = (random(0, 2) == 0) ? random(8, 55) : random(265, 312);
                    sparks[i].y = 175.0f;
                    sparks[i].vy = -random(35, 70) / 10.0f;
                    sparks[i].vx = random(-15, 16) / 10.0f;
                    sparks[i].size = random(2, 4);
                }
            } else {
                sparks[i].y += sparks[i].vy;
                sparks[i].x += sparks[i].vx;
                if (sparks[i].y < -10) sparks[i].active = false;
            }
        }
    }

    void drawSparks(TFT_eSprite &sprite) {
        for (int i = 0; i < MAX_SPARKS; i++) {
            if (sparks[i].active) {
                sprite.fillCircle((int)sparks[i].x, (int)sparks[i].y, sparks[i].size + 2, colorGlow1);
                sprite.fillCircle((int)sparks[i].x, (int)sparks[i].y, sparks[i].size, colorSpark);
            }
        }
    }

} // конец анонимного namespace

void init(TFT_eSPI &tft) {
    colorEye     = tft.color565(255, 50, 20);  // Ярко-красный
    colorGlow1   = tft.color565(160, 10, 5);   // Темно-красный
    colorGlow2   = tft.color565(65, 0, 0);     // Бордовый дым
    colorSpark   = tft.color565(255, 190, 20); // Оранжево-желтое пламя
    colorFireHot = tft.color565(255, 255, 210);// Белое раскаленное ядро

    for (int i = 0; i < MAX_SPARKS; i++) sparks[i].active = false;
}

void reset() {
    startTime = millis();
    // Подхватываем текущую геометрию из предыдущей эмоции!
    FaceState::loadGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);

    // Плавно переливаем цвет лица в огненно-красный
    FaceState::setTargetPalette(255, 50, 20, 160, 10, 5, 65, 0, 0);

    // Брови начинают сверху и плавно опускаются на глаза
    browOffset = -32.0f;

    t_cx = CENTER_X;
    t_cy = BASE_Y;
    t_spacing = EYE_SPACING;
    t_lw = 48.0f; t_rw = 48.0f;
    t_lh = 54.0f; t_rh = 54.0f;
    t_mw = 38.0f; t_mh = 10.0f;
    t_browOffset = 0.0f;
}

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
    uint32_t now = millis();
    float elapsed = (now - startTime) / 1000.0f;

    // Берем плавно интерполируемый цвет из FaceState
    colorEye   = FaceState::current.colorEye;
    colorGlow1 = FaceState::current.colorGlow1;
    colorGlow2 = FaceState::current.colorGlow2;

    // 1. Дыхание и целевые координаты с умеренным параллаксом
    float breath = sin(elapsed * 6.0f); 

    // Смягченный параллакс (чтобы маска бровей не наползала на глаза)
    float imuX = ImuSensor::getParallaxX() * 0.35f;
    float imuY = ImuSensor::getParallaxY() * 0.25f;

    // Защитные границы хода лица
    if (imuX < -14.0f) imuX = -14.0f;
    if (imuX >  14.0f) imuX =  14.0f;
    if (imuY <  -8.0f) imuY =  -8.0f;
    if (imuY >   8.0f) imuY =   8.0f;

    t_cx = CENTER_X + imuX;
    t_cy = BASE_Y + breath * 3.0f + imuY;

    float rageWave = (sin(elapsed * 2.5f) + 1.0f) * 0.5f; 
    
    // Нервный тик правого глаза
    if (now - lastTwitchTime > 1500) {
        if (random(0, 100) < 20) {
            rh -= 12.0f;
            lastTwitchTime = now;
        }
    }

    // 2. Пружинная физика (плавный догон целевых координат)
    const float speed = 0.20f;
    cx += (t_cx - cx) * speed;
    cy += (t_cy - cy) * speed;
    currentSpacing += (t_spacing - currentSpacing) * speed;
    lw += (t_lw - lw) * speed;
    rw += (t_rw - rw) * speed;
    lh += (t_lh - lh) * speed;
    rh += (t_rh - rh) * speed;
    mw += (t_mw - mw) * speed;
    mh += (t_mh - mh) * speed;
    browOffset += (t_browOffset - browOffset) * 0.15f;

    // Сохраняем геометрию в общее состояние лица
    FaceState::saveGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);

    // Случайная микротряска при пике ярости
    int shakeX = 0, shakeY = 0;
    if (rageWave > 0.6f) {
        shakeX = random(-2, 3);
        shakeY = random(-2, 3);
    }

    float cur_cx = cx + shakeX;
    float cur_cy = cy + shakeY;
    float lx = cur_cx - currentSpacing / 2.0f;
    float rx = cur_cx + currentSpacing / 2.0f;

    // 3. Отрисовка кадра
    sprite.fillSprite(TFT_BLACK);

    updateSparks();
    drawSparks(sprite);

    int eyeRadius = 16;
    if (eyeRadius > (int)(lw / 2.0f)) eyeRadius = (int)(lw / 2.0f);
    if (eyeRadius > (int)(lh / 2.0f)) eyeRadius = (int)(lh / 2.0f);

    // Базовые глаза со свечением
    drawGlowingRoundRect(sprite, (int)(lx - lw/2.0f), (int)(cur_cy - lh/2.0f), (int)lw, (int)lh, eyeRadius);
    drawGlowingRoundRect(sprite, (int)(rx - rw/2.0f), (int)(cur_cy - rh/2.0f), (int)rw, (int)rh, eyeRadius);

    // Огненные языки внутри глаз
    if (lh > 24.0f) {
        drawEyeFlames(sprite, lx, cur_cy, lw, lh, elapsed, 0);
        drawEyeFlames(sprite, rx, cur_cy, rw, rh, elapsed, 10);
    }

    // Срезающая "маска злости"
    int l_topY = (int)(cur_cy - lh/2.0f - 8.0f + browOffset);
    sprite.fillTriangle(
        (int)(lx - lw/2.0f - 10.0f), l_topY - 15,
        (int)(lx + lw/2.0f + 10.0f), l_topY - 15,
        (int)(lx + lw/2.0f + 10.0f), l_topY + 28,
        TFT_BLACK
    );

    int r_topY = (int)(cur_cy - rh/2.0f - 8.0f + browOffset);
    sprite.fillTriangle(
        (int)(rx - rw/2.0f - 10.0f), r_topY - 15,
        (int)(rx + rw/2.0f + 10.0f), r_topY - 15,
        (int)(rx - rw/2.0f - 10.0f), r_topY + 28,
        TFT_BLACK
    );

    // Злые брови поверх среза
    int browSteps = 28;
    for (int layer = 0; layer < 3; layer++) {
        uint16_t col = (layer == 0) ? colorGlow2 : (layer == 1 ? colorGlow1 : colorEye);
        int rad = (layer == 0) ? 7 : (layer == 1 ? 5 : 3);

        for (int i = 0; i <= browSteps; i++) {
            float t = (float)i / (float)browSteps;
            float blx = (lx - lw/2.0f - 4.0f) + t * (lw + 8.0f);
            float bly = (l_topY + 2.0f) + t * 22.0f;
            sprite.fillCircle((int)blx, (int)bly, rad, col);

            float brx = (rx + rw/2.0f + 4.0f) - t * (rw + 8.0f);
            float bry = (r_topY + 2.0f) + t * 22.0f;
            sprite.fillCircle((int)brx, (int)bry, rad, col);
        }
    }

    // Рычащий зигзагообразный рот
    float mouthY = cur_cy + 46.0f;
    int mSteps = 20;
    for (int layer = 0; layer < 3; layer++) {
        uint16_t col = (layer == 0) ? colorGlow2 : (layer == 1 ? colorGlow1 : colorEye);
        int rad = (layer == 0) ? 6 : (layer == 1 ? 4 : 2);

        for (int i = 0; i <= mSteps; i++) {
            float t = (float)i / (float)mSteps;
            float normX = (t - 0.5f) * 2.0f;
            float mx = cur_cx + normX * (mw / 2.0f);
            float arch = (normX * normX) * (mh * 0.6f); 
            float teeth = sinf(t * 3.14159f * 4.0f) * 2.5f;
            float my = mouthY + arch + teeth;

            sprite.fillCircle((int)mx, (int)my, rad, col);
        }
    }

    // Значок вздувшейся вены 💢 справа сверху от лица
    float veinScale = 0.8f + (breath + 1.0f) * 0.25f;
    drawVeinSymbol(sprite, (int)(rx + 34.0f), (int)(cur_cy - 48.0f), veinScale);

    // Вывод буфера на дисплей со смещением 40 пикселей сверху
    sprite.pushSprite(0, 0);
}

} // namespace AnimAnger
