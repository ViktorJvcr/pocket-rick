#pragma once
#include <TFT_eSPI.h>

enum class AnimType {
    IDLE    = 0,
    JOY     = 1,
    LAUGH   = 2,
    ANGER   = 3,
    DIZZY   = 4,
    SAD     = 5,
    SWEAR   = 6,
    SLEEP   = 7,
    WINK    = 8,
    DISDAIN = 9,
    LOOK    = 10,
    MELT    = 11
};

namespace AnimManager {
    void init(TFT_eSPI &tft);
    void runCurrentAnimation(TFT_eSPI &tft, TFT_eSprite &sprite);
    void setAnimation(AnimType newAnim, bool blink = true); // <-- здесь добавлен аргумент со значением по умолчанию
    void nextState();
    const char* getAnimName(AnimType anim);
    AnimType getCurrentAnim();
}
