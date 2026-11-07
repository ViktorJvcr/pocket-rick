#pragma once
#include <TFT_eSPI.h>

namespace AnimLookAt {
    void init(TFT_eSPI &tft);
    void reset();
    void runStep(TFT_eSPI &tft, TFT_eSprite &sprite);

    bool isFinished();

    // Направление удара (-1..+1 по каждой оси). Вызывать ДО AnimManager::setAnimation(LOOK).
    // dirX > 0 — удар справа (смотрим вправо), dirY > 0 — удар сверху (смотрим вверх).
    void setTarget(float dirX, float dirY);
}
