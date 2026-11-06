#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Emotions.h"

struct Note { float x, y, life; bool active; };

class Eyes {
private:
    TFT_eSPI* tft;
    TFT_eSprite* sprite;

    Emotion currentEmotion; // <--- ДОБАВИЛИ ТЕКУЩУЮ ЭМОЦИЮ

    uint16_t colorEye, colorGlow1, colorGlow2;
    int eyeBaseW = 50, eyeBaseH = 80, eyeRadius = 25, eyeSpacing = 110;

    float cx, cy, lw, lh, rw, rh, mw, mh;
    float t_cx, t_cy, t_lw, t_lh, t_rw, t_rh, t_mw, t_mh;

    bool isBlinking, isRecovering;
    unsigned long lastBlinkTime;
    
    Note notes[3];
    unsigned long lastNoteTime;

    void updatePhysics();
    void drawFrame();

public:
    Eyes(TFT_eSPI* tft_ptr, TFT_eSprite* sprite_ptr);
    void init();
    void update();
    void setEmotion(Emotion emo);
    void blink();
    // НОВАЯ ФУНКЦИЯ: Посмотреть в сторону (смещение по X и Y, и флаг прищура)
    void lookAt(int offsetX, int offsetY, bool squint); 
};
