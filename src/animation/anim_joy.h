#pragma once
#include <TFT_eSPI.h>

namespace AnimJoy {
    // Инициализация при старте системы (вызывается один раз)
    void init(TFT_eSPI &tft);
    
    // Сброс состояния анимации (вызывается при переключении НА эту эмоцию)
    void reset();

    // Отрисовка одного кадра
    void runStep(TFT_eSPI &tft, TFT_eSprite &sprite);
}
