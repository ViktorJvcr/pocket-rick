#pragma once
#include <TFT_eSPI.h>

enum class AnimType {
    IDLE    = 0,   // Спокойствие
    JOY     = 1,   // Поглаживание
    LAUGH   = 2,   // Щекотка
    ANGER   = 3,   // Ярость
    DIZZY   = 4,   // Головокружение
    SAD     = 5,   // Грусть
    SWEAR   = 6,   // Ругань (5 стуков)
    SLEEP   = 7,   // Сон
    WINK    = 8,   // Подмигивание
    DISDAIN = 9,   // NEW: Презрение / недоумение (3 стука)
    LOOK    = 10   // NEW: Служебная: взгляд в точку удара (1 стук)
};

namespace AnimManager {
    // nextState() перебирает 0..ANIM_CYCLE_COUNT-1 (LOOK в перебор не входит)
    const int ANIM_CYCLE_COUNT = 10;

    void init(TFT_eSPI &tft);
    void runCurrentAnimation(TFT_eSPI &tft, TFT_eSprite &sprite);
    void setAnimation(AnimType newAnim);
    void nextState();
    const char* getAnimName(AnimType anim);
    AnimType getCurrentAnim();
}
