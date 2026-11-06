#include "anim_sleep.h"
#include "face_state.h"

namespace AnimSleep {
    uint16_t COLOR_BRIGHT; 
    uint16_t COLOR_DIM;    
    uint16_t COLOR_ZZZ;

    float colorFade = 1.0f; 
    unsigned long lastRunTime = 0;

    // --- ПЕРЕМЕННЫЕ ДЛЯ ПОДЕРГИВАНИЯ (REM-сон) ---
    unsigned long lastTwitchTime = 0;
    bool isTwitching = false;
    int twitchOffsetX = 0;
    int twitchOffsetY = 0;

    struct ZParticle {
        float x, y, life, size;
        bool active;
    };
    ZParticle z_particles[4];
    unsigned long lastZSpawnTime = 0;

    void init(TFT_eSPI &tft) {
        COLOR_BRIGHT = tft.color565(50, 255, 100); 
        COLOR_DIM = tft.color565(15, 80, 30); 
        COLOR_ZZZ = tft.color565(30, 140, 60); 
        for (int i = 0; i < 4; i++) z_particles[i].active = false;
    }

    void drawVectorZ(TFT_eSprite &sprite, float x, float y, float size, uint16_t color) {
        for (int i = 0; i < 2; i++) {
            sprite.drawLine(x, y + i, x + size, y + i, color);                 
            sprite.drawLine(x + size, y + i, x, y + size + i, color);          
            sprite.drawLine(x, y + size + i, x + size, y + size + i, color);   
        }
    }

        void reset() {
        colorFade = 1.0f;
        for (int i = 0; i < 4; i++) z_particles[i].active = false;
        FaceState::setTargetPalette(50, 255, 100, 10, 120, 30, 0, 40, 10);
    }

void runStep(TFT_eSPI &tft, TFT_eSprite &sprite) {
        unsigned long ms = millis();

        if (ms - lastRunTime > 100) {
            colorFade = 1.0f; 
            for (int i = 0; i < 4; i++) z_particles[i].active = false;
            FaceState::setTargetPalette(50, 255, 100, 10, 120, 30, 0, 40, 10);
        }
        lastRunTime = ms;

        sprite.fillSprite(TFT_BLACK);

        // Плавное затухание цвета
        if (colorFade > 0.0f) colorFade -= 0.03f;
        if (colorFade < 0.0f) colorFade = 0.0f;

        uint16_t currentEyeColor = tft.alphaBlend((uint8_t)(colorFade * 255), COLOR_BRIGHT, COLOR_DIM);

        // --- ДЫХАНИЕ И ПОДЕРГИВАНИЯ ---
        float breathPhase = ms / 800.0f;
        float breathY = sin(breathPhase) * 4.0f; 
        float breathW = sin(breathPhase) * 3.0f; 

        if (!isTwitching && (ms - lastTwitchTime > random(3000, 8000))) {
            isTwitching = true;
            lastTwitchTime = ms;
        }
        if (isTwitching) {
            twitchOffsetX = random(-2, 3);
            twitchOffsetY = random(-1, 2);
            if (ms - lastTwitchTime > random(100, 300)) {
                isTwitching = false;
                twitchOffsetX = 0; twitchOffsetY = 0;
                lastTwitchTime = ms;
            }
        }

        // --- ГЕНЕРАЦИЯ Zzz ---
        if (colorFade == 0.0f && (ms - lastZSpawnTime > 1200)) { 
            for (int i = 0; i < 4; i++) {
                if (!z_particles[i].active) {
                    z_particles[i].x = 130;
                    z_particles[i].y = 100;
                    z_particles[i].life = 1.0f;
                    z_particles[i].active = true;
                    z_particles[i].size = (random(0, 10) > 7) ? 14 : 8;
                    break;
                }
            }
            lastZSpawnTime = ms;
        }

        // --- ОТРИСОВКА ЛИЦА ---
        float cx = 120.0f + twitchOffsetX;
        float cy = 135.0f + breathY + twitchOffsetY;
        float eyeSpacing = 110.0f;
        float eyeW = 50.0f + breathW; 
        float eyeH = 6.0f; 

        int lx = (int)(cx - (eyeSpacing * 0.5f));
        int rx = (int)(cx + (eyeSpacing * 0.5f));

        // 1. Рисуем закрытые глаза
        sprite.fillRoundRect(lx - (int)(eyeW * 0.5f), (int)(cy - eyeH * 0.5f), (int)eyeW, (int)eyeH, 3, currentEyeColor);
        sprite.fillRoundRect(rx - (int)(eyeW * 0.5f), (int)(cy - eyeH * 0.5f), (int)eyeW, (int)eyeH, 3, currentEyeColor);

        // 2. Рисуем храпящий рот
        float snorePhase = sin(breathPhase);
        float mouthW = 8.0f + snorePhase * 3.0f;
        float mouthH = 5.0f + snorePhase * 4.0f;
        
        int m_x = (int)(cx - mouthW * 0.5f);
        int m_y = (int)(cy + 45.0f);
        
        sprite.fillRoundRect(m_x, m_y, (int)mouthW, (int)mouthH, 2, currentEyeColor);

        // Сохраняем текущую спящую геометрию в FaceState для плавного пробуждения!
        FaceState::saveGeometry(cx, cy, eyeSpacing, eyeW, eyeW, eyeH, eyeH, mouthW, mouthH);

        // --- ОТРИСОВКА Zzz ---
        for (int i = 0; i < 4; i++) {
            if (z_particles[i].active) {
                z_particles[i].y -= 0.6f; 
                z_particles[i].x += sin(ms / 300.0f + i) * 1.5f; 
                z_particles[i].life -= 0.008f; 

                if (z_particles[i].life <= 0.0f) {
                    z_particles[i].active = false;
                } else {
                    uint16_t zColor = (z_particles[i].life < 0.2f) ? tft.color565(10, 50, 20) : COLOR_ZZZ;
                    drawVectorZ(sprite, z_particles[i].x, z_particles[i].y, z_particles[i].size, zColor);
                }
            }
        }

        sprite.pushSprite(0, 40);
    }
}
