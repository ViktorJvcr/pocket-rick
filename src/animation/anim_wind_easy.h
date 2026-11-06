#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

namespace AnimWindEasy {
    void init(TFT_eSPI &tft);
    void reset();
    void runStep(TFT_eSPI &tft, TFT_eSprite &sprite);
}
