#include "anim_manager.h"
#include "face_state.h"
#include "anim_idle.h"
#include "anim_joy.h"
#include "anim_laugh.h"
#include "anim_anger.h"
#include "anim_dizzy.h"
#include "anim_sad.h"
#include "anim_swear.h"
#include "anim_sleep.h"
#include "anim_wink.h"
#include "anim_disdain.h"
#include "anim_lookat.h"
#include "anim_melt.h"
#include <Arduino.h>

namespace AnimManager {

namespace {
    AnimType currentAnim = AnimType::IDLE;
    const int CYCLE_MAX = (int)AnimType::LOOK; // MELT активируется только физическим переворотом
}

void init(TFT_eSPI &tft) {
    AnimIdle::init(tft);
    AnimJoy::init(tft);
    AnimLaugh::init(tft);
    AnimAnger::init(tft);
    AnimDizzy::init(tft);
    AnimSad::init(tft);
    AnimSwear::init(tft);
    AnimSleep::init(tft);
    AnimWink::init(tft);
    AnimDisdain::init(tft);
    AnimLookAt::init(tft);
    AnimMelt::init(tft);
    currentAnim = AnimType::IDLE;
    AnimIdle::reset();
}

void setAnimation(AnimType newAnim, bool blink) {
    // Если blink == false (например, бесшовный переход сон -> ярость), рефлекторный прищур отключается
    FaceState::prepareTransition(blink);
    currentAnim = newAnim;

    switch (currentAnim) {
        case AnimType::IDLE:    AnimIdle::reset();    break;
        case AnimType::JOY:     AnimJoy::reset();     break;
        case AnimType::LAUGH:   AnimLaugh::reset();   break;
        case AnimType::ANGER:   AnimAnger::reset();   break;
        case AnimType::DIZZY:   AnimDizzy::reset();   break;
        case AnimType::SAD:     AnimSad::reset();     break;
        case AnimType::SWEAR:   AnimSwear::reset();   break;
        case AnimType::SLEEP:   AnimSleep::reset();   break;
        case AnimType::WINK:    AnimWink::reset();    break;
        case AnimType::DISDAIN: AnimDisdain::reset(); break;
        case AnimType::LOOK:    AnimLookAt::reset();  break;
        case AnimType::MELT:    AnimMelt::reset();    break;
    }
    Serial.printf("[ANIM] -> %s\n", getAnimName(currentAnim));
}

void runCurrentAnimation(TFT_eSPI &tft, TFT_eSprite &sprite) {
    FaceState::updateColors(tft, 0.15f);

    switch (currentAnim) {
        case AnimType::IDLE:    AnimIdle::runStep(tft, sprite);    break;
        case AnimType::JOY:     AnimJoy::runStep(tft, sprite);     break;
        case AnimType::LAUGH:   AnimLaugh::runStep(tft, sprite);   break;
        case AnimType::ANGER:   AnimAnger::runStep(tft, sprite);   break;
        case AnimType::DIZZY:   AnimDizzy::runStep(tft, sprite);   break;
        case AnimType::SAD:     AnimSad::runStep(tft, sprite);     break;
        case AnimType::SWEAR:   AnimSwear::runStep(tft, sprite);   break;
        case AnimType::SLEEP:   AnimSleep::runStep(tft, sprite);   break;
        case AnimType::WINK:    AnimWink::runStep(tft, sprite);    break;
        case AnimType::DISDAIN: AnimDisdain::runStep(tft, sprite); break;
        case AnimType::LOOK:    AnimLookAt::runStep(tft, sprite);  break;
        case AnimType::MELT:    AnimMelt::runStep(tft, sprite);    break;
    }
}

void nextState() {
    int next = (int)currentAnim + 1;
    if (next > CYCLE_MAX) next = 0;
    setAnimation((AnimType)next);
}

const char* getAnimName(AnimType anim) {
    switch (anim) {
        case AnimType::IDLE:    return "IDLE";
        case AnimType::JOY:     return "JOY";
        case AnimType::LAUGH:   return "LAUGH";
        case AnimType::ANGER:   return "ANGER";
        case AnimType::DIZZY:   return "DIZZY";
        case AnimType::SAD:     return "SAD";
        case AnimType::SWEAR:   return "SWEAR";
        case AnimType::SLEEP:   return "SLEEP";
        case AnimType::WINK:    return "WINK";
        case AnimType::DISDAIN: return "DISDAIN";
        case AnimType::LOOK:    return "LOOK";
        case AnimType::MELT:    return "MELT";
    }
    return "UNKNOWN";
}

AnimType getCurrentAnim() {
    return currentAnim;
}

} // namespace AnimManager
