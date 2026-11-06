#pragma once
#include <Arduino.h>
#include "Eyes.h"
#include "Emotions.h"

class Brain {
private:
    Eyes* eyes;
    Emotion currentEmotion;
    unsigned long lastActionTime;

public:
    Brain(Eyes* eyes_ptr);
    void init();
    void update(); // Главный цикл мыслей робота
    
    void triggerEvent(String eventName); // Для реакции на датчики
};
