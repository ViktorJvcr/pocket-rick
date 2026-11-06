#include "anim_idle.h"
#include "face_state.h"
#include "sensors/imu_sensor.h"
#include <math.h>

namespace AnimIdle {

    // --- ЦВЕТА ---
    #define COLOR_BG    0x0000
    uint16_t COLOR_EYE;
    uint16_t COLOR_GLOW1;
    uint16_t COLOR_GLOW2;

    // --- БАЗОВЫЕ ПАРАМЕТРЫ ---
    const int eyeBaseW = 50;
    const int eyeBaseH = 80;
    const int eyeRadius = 25;
    const float eyeSpacing = 110.0f;

    // Текущие значения (Физика)
    float cx = 120.0f, cy = 100.0f;
    float currentSpacing = eyeSpacing;
    float lw = eyeBaseW, lh = eyeBaseH;
    float rw = eyeBaseW, rh = eyeBaseH;
    float mw = 15.0f, mh = 4.0f;

    // Целевые значения (Куда стремимся)
    float t_cx = 120.0f, t_cy = 100.0f;
    float t_spacing = eyeSpacing;
    float t_lw = eyeBaseW, t_lh = eyeBaseH;
    float t_rw = eyeBaseW, t_rh = eyeBaseH;
    float t_mw = 15.0f, t_mh = 4.0f;

    // Таймеры и состояния
    unsigned long lastActionTime = 0;
    unsigned long lastBlinkTime = 0;
    unsigned long lastNoteTime = 0;

    int currentState = 0;
    bool isBlinking = false;
    bool isRecovering = false;
    bool isGoingToSleep = false;

    // --- СИСТЕМА ЧАСТИЦ (НОТЫ) ---
    struct Note {
        float x, y;
        float life;
        bool active;
    };
    Note notes[3];

    void applyCurrentState();
    void updateLogic();
    void updatePhysics();
    void drawFrame(TFT_eSPI &tft, TFT_eSprite &sprite);

    void reset() {
        // Подхватываем текущую геометрию лица из предыдущей эмоции для бесшовного перехода
        FaceState::loadGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);

        // Фирменная неоново-зеленая палитра
        FaceState::setTargetPalette(50, 255, 100, 10, 120, 30, 0, 40, 10);

        isGoingToSleep = false;
        isBlinking = false;
        isRecovering = true;

        currentState = 0;
        t_lh = eyeBaseH; t_rh = eyeBaseH;
        t_lw = eyeBaseW; t_rw = eyeBaseW;
        t_cx = 120.0f;   t_cy = 100.0f;
        t_spacing = eyeSpacing;
        t_mw = 15.0f;    t_mh = 4.0f;

        for (int i = 0; i < 3; i++) notes[i].active = false;

        unsigned long ms = millis();
        lastActionTime = ms;
        lastBlinkTime = ms;
    }

    void wakeUp() {
        reset();
    }

    void startSleepTransition() {
        isGoingToSleep = true;
        isBlinking = false;
        t_lh = 4.0f; t_rh = 4.0f;
        t_lw = eyeBaseW; t_rw = eyeBaseW;
        t_cx = 120.0f; t_cy = 135.0f;
        t_spacing = eyeSpacing;
        t_mw = 0.0f; t_mh = 0.0f;
    }

    bool isReadyForSleep() {
        return (lh <= 6.0f && rh <= 6.0f); 
    }

    void init(TFT_eSPI &tft) {
        COLOR_EYE   = tft.color565(50, 255, 100);
        COLOR_GLOW1 = tft.color565(10, 120, 30);
        COLOR_GLOW2 = tft.color565(0, 40, 10);
        FaceState::setTargetPalette(50, 255, 100, 10, 120, 30, 0, 40, 10);
        for (int i = 0; i < 3; i++) notes[i].active = false;
        reset();
    }

    void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
        // Берем плавно интерполированные цвета из FaceState
        COLOR_EYE   = FaceState::current.colorEye;
        COLOR_GLOW1 = FaceState::current.colorGlow1;
        COLOR_GLOW2 = FaceState::current.colorGlow2;

        updateLogic();
        updatePhysics();
        drawFrame(tft, sprite);
    }

    void updateLogic() {
        if (isGoingToSleep) return;
        unsigned long ms = millis();

        // Логика моргания
        if (ms - lastBlinkTime > (unsigned long)random(2500, 6000) && !isBlinking) {
            isBlinking = true;
            isRecovering = false;
            t_lh = 4.0f; t_rh = 4.0f;
            lastBlinkTime = ms;
        }

        if (isBlinking && (ms - lastBlinkTime > 120)) {
            isBlinking = false;
            isRecovering = true;
            applyCurrentState();
            lastBlinkTime = ms;
        }

        if (isRecovering && (ms - lastBlinkTime > 400)) {
            isRecovering = false;
        }

        // Периодическая смена мимики и направления взгляда
        if (ms - lastActionTime > (unsigned long)random(3000, 6000)) {
            currentState = random(0, 6);
            applyCurrentState();
            lastActionTime = ms;
        }

        // Спавн музыкальных нот в состоянии 3
        if (currentState == 3 && (ms - lastNoteTime > 600)) {
            for (int i = 0; i < 3; i++) {
                if (!notes[i].active) {
                    notes[i].x = cx;
                    notes[i].y = cy + 45.0f;
                    notes[i].life = 1.0f;
                    notes[i].active = true;
                    break;
                }
            }
            lastNoteTime = ms;
        }
    }

        void applyCurrentState() {
        if (!isBlinking) {
            t_lh = eyeBaseH; t_rh = eyeBaseH;
        }
        t_lw = eyeBaseW; t_rw = eyeBaseW;
        t_cx = 120.0f;   t_cy = 100.0f;
        t_spacing = eyeSpacing;
        t_mw = 15.0f;    t_mh = 4.0f;

        switch (currentState) {
            case 1:
                t_cx = 120.0f + random(-16, 17);
                t_cy = 100.0f + random(-10, 11);
                t_mw = 10.0f;
                break;

            case 2:
                t_cy = 112.0f;
                if (!isBlinking) { t_lh = 40.0f; t_rh = 40.0f; }
                t_mw = 25.0f; t_mh = 2.0f;
                break;

            case 3:
                t_lw = 35.0f; if (!isBlinking) t_lh = 55.0f;
                t_rw = 60.0f; if (!isBlinking) t_rh = 90.0f;
                t_cy = 95.0f;
                t_mw = 12.0f; t_mh = 12.0f;
                break;

            case 4:
                t_lw = 65.0f; t_rw = 65.0f;
                if (!isBlinking) { t_lh = 25.0f; t_rh = 25.0f; }
                t_mw = 20.0f; t_mh = 6.0f;
                t_cy = 105.0f;
                break;

            case 5: {
                int dir = random(0, 2) == 0 ? -1 : 1;
                t_cx = 120.0f + (dir * 22.0f);
                t_cy = 100.0f;
                t_mw = 8.0f; t_mh = 2.0f;

                if (!isBlinking) {
                    if (dir == 1) {
                        t_rw = 50.0f; t_rh = 15.0f;
                        t_lw = 50.0f; t_lh = 35.0f;
                    } else {
                        t_lw = 50.0f; t_lh = 15.0f;
                        t_rw = 50.0f; t_rh = 35.0f;
                    }
                }
                break;
            } // закрываем фигурные скобки блока case 5
        }     // закрываем switch (currentState)
    }         // закрываем саму функцию applyCurrentState()


    void updatePhysics() {
        float breathY = sinf(millis() / 400.0f) * 3.0f;

        // Складываем целевые координаты эмоции с живым параллаксом от IMU (наклоны + повороты)
        float current_t_cx = t_cx + (isGoingToSleep ? 0.0f : ImuSensor::getParallaxX());
        float current_t_cy = t_cy + breathY + (isGoingToSleep ? 0.0f : ImuSensor::getParallaxY());

        // Мягкая защита границ экрана, чтобы глаза со свечением не уезжали за край
        if (current_t_cx < 72.0f)  current_t_cx = 72.0f;
        if (current_t_cx > 168.0f) current_t_cx = 168.0f;
        if (current_t_cy < 65.0f)  current_t_cy = 65.0f;
        if (current_t_cy > 135.0f) current_t_cy = 135.0f;

        float speed = 0.12f;
        if (isGoingToSleep) speed = 0.05f;
        if (isBlinking) speed = 0.5f;
        else if (isRecovering) speed = 0.18f;

        cx += (current_t_cx - cx) * speed;
        cy += (current_t_cy - cy) * speed;
        currentSpacing += (t_spacing - currentSpacing) * speed;
        lw += (t_lw - lw) * speed;
        lh += (t_lh - lh) * speed;
        rw += (t_rw - rw) * speed;
        rh += (t_rh - rh) * speed;
        mw += (t_mw - mw) * speed;
        mh += (t_mh - mh) * speed;

        // Сохраняем актуальную геометрию в глобальное состояние лица
        FaceState::saveGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);

        for (int i = 0; i < 3; i++) {
            if (notes[i].active) {
                notes[i].y -= 1.5f;
                notes[i].x += sinf(millis() / 150.0f + i) * 1.0f;
                notes[i].life -= 0.015f;
                if (notes[i].life <= 0.0f) notes[i].active = false;
            }
        }
    }

    void drawFrame(TFT_eSPI &tft, TFT_eSprite &sprite) {
        sprite.fillSprite(TFT_BLACK);

        float lx = cx - (currentSpacing * 0.5f);
        float rx = cx + (currentSpacing * 0.5f);

        // Отрисовка нот
        for (int i = 0; i < 3; i++) {
            if (notes[i].active) {
                int nx = (int)notes[i].x; int ny = (int)notes[i].y;
                sprite.fillCircle(nx, ny, 4, TFT_CYAN);
                sprite.drawLine(nx + 3, ny, nx + 3, ny - 10, TFT_CYAN);
                sprite.drawLine(nx + 3, ny - 10, nx + 7, ny - 7, TFT_CYAN);
            }
        }

        // Левый глаз
        int l_x = (int)(lx - (lw / 2.0f)); 
        int l_y = (int)(cy - (lh / 2.0f));
        int safeRadiusL = eyeRadius;
        if (safeRadiusL > lw / 2) safeRadiusL = (int)(lw / 2);
        if (safeRadiusL > lh / 2) safeRadiusL = (int)(lh / 2);

        sprite.fillRoundRect(l_x - 6, l_y - 6, (int)lw + 12, (int)lh + 12, safeRadiusL + 4, COLOR_GLOW2);
        sprite.fillRoundRect(l_x - 3, l_y - 3, (int)lw + 6,  (int)lh + 6,  safeRadiusL + 2, COLOR_GLOW1);
        sprite.fillRoundRect(l_x, l_y, (int)lw, (int)lh, safeRadiusL, COLOR_EYE);

        // Правый глаз
        int r_x = (int)(rx - (rw / 2.0f)); 
        int r_y = (int)(cy - (rh / 2.0f));
        int safeRadiusR = eyeRadius;
        if (safeRadiusR > rw / 2) safeRadiusR = (int)(rw / 2);
        if (safeRadiusR > rh / 2) safeRadiusR = (int)(rh / 2);

        sprite.fillRoundRect(r_x - 6, r_y - 6, (int)rw + 12, (int)rh + 12, safeRadiusR + 4, COLOR_GLOW2);
        sprite.fillRoundRect(r_x - 3, r_y - 3, (int)rw + 6,  (int)rh + 6,  safeRadiusR + 2, COLOR_GLOW1);
        sprite.fillRoundRect(r_x, r_y, (int)rw, (int)rh, safeRadiusR, COLOR_EYE);

        // Рот
        int m_x = (int)(cx - (mw / 2.0f));
        int m_y = (int)(cy + 42.0f);

        if (mh > 8.0f) {
            sprite.fillCircle((int)cx, m_y + (int)(mh / 2.0f), (int)(mw / 2.0f), COLOR_EYE);
        } else if (mw > 0.0f && mh > 0.0f) {
            sprite.fillRoundRect(m_x, m_y, (int)mw, (int)mh, 2, COLOR_EYE); 
        }

        sprite.pushSprite(0, 40);
    }

} // namespace AnimIdle
