#pragma once
#include <TFT_eSPI.h>

enum class AnimType {
    IDLE,
    JOY,
    LAUGH,
    ANGER,
    DIZZY,
    SAD,
    SWEAR,
    SLEEP,
    WINK    // 8. Подмигивание при клике
};

namespace AnimManager {
    void init(TFT_eSPI &tft);
    void runCurrentAnimation(TFT_eSPI &tft, TFT_eSprite &sprite);
    void setAnimation(AnimType newAnim);
    void nextState();
    const char* getAnimName(AnimType anim);
    AnimType getCurrentAnim();
}
