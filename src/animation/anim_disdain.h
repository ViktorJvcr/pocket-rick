#pragma once
#include <TFT_eSPI.h>

namespace AnimDisdain {
    void init(TFT_eSPI &tft);
    void reset();
    void runStep(TFT_eSPI &tft, TFT_eSprite &sprite);

    bool isFinished();

    // С какой стороны пришёл удар (-1..+1). Вызывать ДО AnimManager::setAnimation(DISDAIN).
    // Глаз со стороны удара прищуривается, противоположный удивлённо приподнимается.
    void setTapSide(float dirX);

    // Ещё один стук во время анимации: повторное «ну что ещё?» (покачивание + «?»).
    void nudge();
}
