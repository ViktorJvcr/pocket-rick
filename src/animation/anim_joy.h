#pragma once
#include <TFT_eSPI.h>

namespace AnimJoy {
    // Инициализация при старте микроконтроллера
    void init(TFT_eSPI &tft);
    
    // Сброс и подхват геометрии лица при переходе в радость
    void reset();

    // Отрисовка каждого кадра
    void runStep(TFT_eSPI &tft, TFT_eSprite &sprite);
}
