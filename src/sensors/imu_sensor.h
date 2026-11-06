#pragma once
#include <Arduino.h>

namespace ImuSensor {
    void init();
    void update();
    
    // Одиночное резкое поднятие / рывок (для триггера испуга SCARED)
    bool isSuddenLiftDetected();

    // Продолжительная тряска или кручение (для триггера головокружения DIZZY)
    bool isShakeDetected();

    // Плавное смещение взгляда от наклонов и поворотов (Живой параллакс в пикселях)
    float getParallaxX();
    float getParallaxY();

    // Флаг успешной инициализации BMI160
    bool isReady();
}
