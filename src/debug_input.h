#pragma once
#include <Arduino.h>

namespace DebugInput {
    // Инициализация пина кнопки
    void init(uint8_t pin);
    
    // Возвращает true только один раз при нажатии (с защитой от дребезга)
    bool isButtonPressed();
}
