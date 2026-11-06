#include "anim_joy.h"
#include "face_state.h"
#include <Arduino.h>
#include <math.h>

namespace AnimJoy {

    namespace {
        // --- Цвета ---
        uint16_t colorEye;
        uint16_t colorGlow1;
        uint16_t colorGlow2;
        uint16_t colorSparkle;
        uint16_t colorSparkleCore;
        uint16_t colorBlush;

        // --- Базовые параметры ---
        const int eyeBaseW = 44;
        const int eyeBaseH = 75;
        const int eyeRadius = 22;
        const float eyeSpacing = 105.0f;
        const int CENTER_X = 120;
        const int BASE_Y = 135; 

        // Текущие координаты (Физика)
        float cx = CENTER_X, cy = BASE_Y;
        float currentSpacing = eyeSpacing;
        float lw = eyeBaseW, rw = eyeBaseW;
        float lh = eyeBaseH, rh = eyeBaseH;
        float mw = 16, mh = 4;
        float happySquint = 0.0f; // От 0.0 (обычные глаза) до 1.0 (мягкие дуги)
        float browOffsetY = 0.0f;

        // Целевые координаты
        float t_cx = CENTER_X, t_cy = BASE_Y;
        float t_spacing = eyeSpacing;
        float t_lw = eyeBaseW, t_rw = eyeBaseW;
        float t_lh = eyeBaseH, t_rh = eyeBaseH;
        float t_mw = 16, t_mh = 4;
        float t_happySquint = 0.0f;
        float t_browOffsetY = 0.0f;

        // Тайминги
        uint32_t lastActionTime = 0;
        int joyPhase = 0; 

        // Звездочки
        struct Sparkle {
            float x, startX, y, size, speedY, phase;
            bool active;
        };
        const int MAX_SPARKLES = 8;
        Sparkle sparkles[MAX_SPARKLES];
        uint32_t lastSparkleTime = 0;

        // --- ПРОЦЕДУРНАЯ КИСТЬ ДЛЯ МЯГКИХ ДУГ ---
        void drawSoftEye(TFT_eSprite &s, float x, float y, float w, float h, float squint) {
            float archHeight = 14.0f * squint;
            float strokeR = (w * 0.42f) - (16.0f * squint);
            float spanX = (w * 0.5f) - strokeR + (8.0f * squint);
            float stretchY = (h * 0.5f - strokeR) * (1.0f - squint);
            if (stretchY < 0.0f) stretchY = 0.0f;

            for (int layer = 0; layer < 3; layer++) {
                uint16_t col = (layer == 0) ? colorGlow2 : ((layer == 1) ? colorGlow1 : colorEye);
                int r = (int)strokeR + (layer == 0 ? 5 : (layer == 1 ? 2 : 0));

                for (float t = -1.0f; t <= 1.01f; t += 0.1f) {
                    float px = x + t * spanX;
                    float py = y + (t * t - 0.4f) * archHeight; 

                    if (stretchY < 1.0f) {
                        s.fillCircle((int)px, (int)py, r, col);
                    } else {
                        s.fillRoundRect((int)(px - r), (int)(py - stretchY - r), r * 2, (int)(stretchY * 2 + r * 2), r, col);
                    }
                }
            }
        }

        // Рисует мягкую расплывающуюся улыбку-скобку
        void drawSoftMouth(TFT_eSprite &s, float x, float y, float w, float squint) {
            float archDepth = 8.0f * squint;
            float strokeR = 3.5f + (0.5f * squint);
            float spanX = w * 0.5f;

            for (int layer = 0; layer < 3; layer++) {
                uint16_t col = (layer == 0) ? colorGlow2 : ((layer == 1) ? colorGlow1 : colorEye);
                int r = (int)strokeR + (layer == 0 ? 4 : (layer == 1 ? 2 : 0));

                for (float t = -1.0f; t <= 1.01f; t += 0.1f) {
                    float px = x + t * spanX;
                    float py = y + (1.0f - t * t) * archDepth; 
                    s.fillCircle((int)px, (int)py, r, col);
                }
            }
        }
    }

    void init(TFT_eSPI &tft) {
        colorEye   = tft.color565(50, 255, 100);
        colorGlow1 = tft.color565(10, 120, 30);
        colorGlow2 = tft.color565(0, 40, 10);
        
        colorSparkle     = tft.color565(255, 220, 0); 
        colorSparkleCore = tft.color565(255, 255, 200);
        colorBlush       = tft.color565(255, 60, 100); 

        for (int i = 0; i < MAX_SPARKLES; i++) {
            sparkles[i].active = false;
        }
    }

    void reset() {
        lastActionTime = millis();
        joyPhase = 0;
        
        // Подхватываем координаты из предыдущей эмоции для бесшовного перехода
        FaceState::loadGeometry(cx, cy, currentSpacing, lw, rw, lh, rh, mw, mh);
        FaceState::setTargetPalette(50, 255, 100, 10, 120, 30, 0, 40, 10);

        happySquint = 0.0f;
        browOffsetY = 0.0f;
        
        // Фаза 0: Мягкое узнавание (небольшой подскок)
        t_cx = CENTER_X;
        t_cy = BASE_Y - 4;
        t_spacing = eyeSpacing;
        t_lw = eyeBaseW + 2;  t_rw = eyeBaseW + 2;
        t_lh = eyeBaseH + 6;  t_rh = eyeBaseH + 6;
        t_mw = 22; t_mh = 6;
        t_happySquint = 0.0f;
        t_browOffsetY = -8.0f;
    }

    void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
        uint32_t now = millis();

        colorEye   = FaceState::current.colorEye;
        colorGlow1 = FaceState::current.colorGlow1;
        colorGlow2 = FaceState::current.colorGlow2;

        // --- 1. ЭФФЕКТ ДЫХАНИЯ ---
        float breathY = sin(now * 0.006f) * 3.5f; 
        float wiggleX = cos(now * 0.004f) * 4.0f; 

        // --- 2. ЛОГИКА ПОВЕДЕНИЯ ---
        if (joyPhase == 0 && (now - lastActionTime > 300)) {
            joyPhase = 1;
            lastActionTime = now;
            t_cy = BASE_Y - 12;
            t_lw = eyeBaseW; t_rw = eyeBaseW;
            t_mw = 42;
            t_happySquint = 1.0f;
            t_browOffsetY = -6.0f;
        }
        else if (joyPhase == 1 && (now - lastActionTime > 250)) {
            joyPhase = 2;
            lastActionTime = now;
            t_cy = BASE_Y - 2; 
            t_mw = 36;
            t_happySquint = 0.9f; 
            t_browOffsetY = -2.0f;
        }
        else if (joyPhase == 2 && (now - lastActionTime > random(800, 1600))) {
            joyPhase = 1; 
            lastActionTime = now;
            t_cy = BASE_Y - 14; 
            t_mw = 45;
            t_happySquint = 1.0f;
            t_browOffsetY = -8.0f; 
        }

        // --- 3. ЗВЕЗДОЧКИ ---
        if (now - lastSparkleTime > 200) {
            lastSparkleTime = now;
            for (int i = 0; i < MAX_SPARKLES; i++) {
                if (!sparkles[i].active) {
                    sparkles[i].active = true;
                    sparkles[i].startX = random(20, 300);
                    sparkles[i].x = sparkles[i].startX;
                    sparkles[i].y = random(110, 165);
                    sparkles[i].size = random(5, 10);
                    sparkles[i].speedY = random(12, 25) / 10.0f;
                    sparkles[i].phase = random(0, 100) / 10.0f;
                    break;
                }
            }
        }

        // --- 4. ФИЗИКА ---
        float speed = 0.15f; 
        cx += ((t_cx + wiggleX) - cx) * speed;
        cy += ((t_cy + breathY) - cy) * speed;
        currentSpacing += (t_spacing - currentSpacing) * speed;
        
        lw += (t_lw - lw) * speed;
        rw += (t_rw - rw) * speed;
        lh += (t_lh - lh) * speed;
        rh += (t_rh - rh) * speed;
        mw += (t_mw - mw) * 0.12f;
        mh += (t_mh - mh) * 0.12f;
        happySquint += (t_happySquint - happySquint) * 0.12f;
        browOffsetY += (t_browOffsetY - browOffsetY) * 0.15f;

        // Когда глаза превратились в радостные скобки (happySquint -> 1), сохраняем малую эффективную высоту,
        // чтобы следующая анимация плавно открывала глаза из этой скобки!
        float effLh = lh * (1.0f - happySquint * 0.78f);
        float effRh = rh * (1.0f - happySquint * 0.78f);
        FaceState::saveGeometry(cx, cy, currentSpacing, lw, rw, effLh, effRh, mw, mh);

        float lx = cx - currentSpacing * 0.5f;
        float rx = cx + currentSpacing * 0.5f;

        // --- 5. ОТРИСОВКА ---
        sprite.fillSprite(TFT_BLACK);

        // 5.1 БРОВКИ
        int browY = (int)(cy - 32 + (happySquint * 8.0f) + browOffsetY);
        for (int t = 0; t < 3; t++) { 
            sprite.drawLine(lx - 14, browY + 3 + t, lx + 10, browY - 2 + t, colorEye);
            sprite.drawLine(rx - 10, browY - 2 + t, rx + 14, browY + 3 + t, colorEye);
        }

        // 5.2 ГЛАЗА
        drawSoftEye(sprite, lx, cy, lw, lh, happySquint);
        drawSoftEye(sprite, rx, cy, rw, rh, happySquint);

        // 5.3 КРАСНЫЕ ЩЕЧКИ ///
        if (happySquint > 0.3f) {
            int blushY = (int)(cy + 35);
            int leftBlushX = (int)(lx - 18);
            int rightBlushX = (int)(rx - 2);

            for (int i = 0; i < 3; i++) {
                int bxL = leftBlushX + (i * 9);
                int bxR = rightBlushX + (i * 9);
                for (int w = 0; w < 3; w++) {
                    sprite.drawLine(bxL + w, blushY + 9, bxL + 5 + w, blushY, colorBlush);
                    sprite.drawLine(bxR + w, blushY + 9, bxR + 5 + w, blushY, colorBlush);
                }
            }
        }

        // 5.4 МЯГКАЯ УЛЫБКА-СКОБКА
        drawSoftMouth(sprite, cx, cy + 55, mw, happySquint);

        // 5.5 ЖЕЛТЫЕ ЗВЕЗДОЧКИ ✨
        for (int i = 0; i < MAX_SPARKLES; i++) {
            if (sparkles[i].active) {
                sparkles[i].y -= sparkles[i].speedY;
                sparkles[i].x = sparkles[i].startX + sin(now * 0.008f + sparkles[i].phase) * 6.0f;
                sparkles[i].size -= 0.1f;

                if (sparkles[i].size <= 1.0f) {
                    sparkles[i].active = false;
                } else {
                    int sx = (int)sparkles[i].x;
                    int sy = (int)sparkles[i].y;
                    int s = (int)sparkles[i].size;

                    sprite.fillTriangle(sx - s, sy, sx + s, sy, sx, sy - s/3, colorSparkle);
                    sprite.fillTriangle(sx - s, sy, sx + s, sy, sx, sy + s/3, colorSparkle);
                    sprite.fillTriangle(sx, sy - s, sx, sy + s, sx - s/3, sy, colorSparkle);
                    sprite.fillTriangle(sx, sy - s, sx, sy + s, sx + s/3, sy, colorSparkle);
                    sprite.fillCircle(sx, sy, s / 3, colorSparkleCore);
                }
            }
        }

        sprite.pushSprite(0, 40); 
    }
}
