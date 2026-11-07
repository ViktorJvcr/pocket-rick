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
#include "anim_disdain.h"   // NEW
#include "anim_lookat.h"    // NEW

namespace AnimManager {

namespace {
    AnimType currentAnim = AnimType::IDLE;
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
    AnimDisdain::init(tft);   // NEW
    AnimLookAt::init(tft);    // NEW

    currentAnim = AnimType::IDLE;
    AnimIdle::reset();
}

void setAnimation(AnimType newAnim) {
    // Взгляд в точку удара должен быть мгновенным — без рефлекторного моргания.
    bool blink = (newAnim != AnimType::LOOK);
    FaceState::prepareTransition(blink);

    currentAnim = newAnim;

    switch (newAnim) {
        case AnimType::IDLE:    AnimIdle::reset();    break;
        case AnimType::JOY:     AnimJoy::reset();     break;
        case AnimType::LAUGH:   AnimLaugh::reset();   break;
        case AnimType::ANGER:   AnimAnger::reset();   break;
        case AnimType::DIZZY:   AnimDizzy::reset();   break;
        case AnimType::SAD:     AnimSad::reset();     break;
        case AnimType::SWEAR:   AnimSwear::reset();   break;
        case AnimType::SLEEP:   AnimSleep::reset();   break;
        case AnimType::WINK:    AnimWink::reset();    break;
        case AnimType::DISDAIN: AnimDisdain::reset(); break;   // NEW
        case AnimType::LOOK:    AnimLookAt::reset();  break;   // NEW
        default:                AnimIdle::reset();    break;
    }
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
        case AnimType::DISDAIN: AnimDisdain::runStep(tft, sprite); break;   // NEW
        case AnimType::LOOK:    AnimLookAt::runStep(tft, sprite);  break;   // NEW
        default:                AnimIdle::runStep(tft, sprite);    break;
    }
}

void nextState() {
    int next = ((int)currentAnim + 1) % ANIM_CYCLE_COUNT;
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
        case AnimType::DISDAIN: return "DISDAIN";   // NEW
        case AnimType::LOOK:    return "LOOK";      // NEW
        default:                return "UNKNOWN";
    }
}

AnimType getCurrentAnim() {
    return currentAnim;
}

} // namespace AnimManager
