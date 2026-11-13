#pragma once
#include <TFT_eSPI.h>

namespace AnimShock {
    void init(TFT_eSPI &tft);
    void reset();
    void runStep(TFT_eSPI &tft, TFT_eSprite &sprite);
}
