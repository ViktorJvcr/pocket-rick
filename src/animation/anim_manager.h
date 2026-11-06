#pragma once
#include <TFT_eSPI.h>

enum class AnimType {
    IDLE,
    ANGER,
    DIZZY,
    SAD,
    SWEAR,
    SLEEP
};

namespace AnimManager {
    void init(TFT_eSPI &tft);
    void runCurrentAnimation(TFT_eSPI &tft, TFT_eSprite &sprite);
    void setAnimation(AnimType newAnim);
    void nextState();
    const char* getAnimName(AnimType anim);
    AnimType getCurrentAnim();
}
