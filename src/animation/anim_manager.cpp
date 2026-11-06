#include "anim_manager.h"
#include "face_state.h"
#include "anim_idle.h"
#include "anim_anger.h"
#include "anim_dizzy.h"
#include "anim_sad.h"
#include "anim_swear.h"
#include "anim_sleep.h"
#include "config.h"

namespace AnimManager {
    AnimType currentAnim = AnimType::IDLE;

    void init(TFT_eSPI &tft) {
        FaceState::init(tft);
        AnimIdle::init(tft);
        AnimAnger::init(tft);
        AnimDizzy::init(tft);
        AnimSad::init(tft);
        AnimSwear::init(tft);
        AnimSleep::init(tft);

        // Стартуем с IDLE
        AnimIdle::reset();
        currentAnim = AnimType::IDLE;
    }

    AnimType getCurrentAnim() {
        return currentAnim;
    }

    const char* getAnimName(AnimType anim) {
        switch (anim) {
            case AnimType::IDLE:  return "IDLE";
            case AnimType::ANGER: return "ANGER";
            case AnimType::DIZZY: return "DIZZY";
            case AnimType::SAD:   return "SAD";
            case AnimType::SWEAR: return "SWEAR";
            case AnimType::SLEEP: return "SLEEP";
            default:              return "UNKNOWN";
        }
    }

    void setAnimation(AnimType newAnim) {
        FaceState::prepareTransition(true);
        currentAnim = newAnim;

        switch (currentAnim) {
            case AnimType::IDLE:  AnimIdle::reset();  break;
            case AnimType::ANGER: AnimAnger::reset(); break;
            case AnimType::DIZZY: AnimDizzy::reset(); break;
            case AnimType::SAD:   AnimSad::reset();   break;
            case AnimType::SWEAR: AnimSwear::reset(); break;
            case AnimType::SLEEP: AnimSleep::reset(); break;
        }
    }

    void nextState() {
        switch (currentAnim) {
            case AnimType::IDLE:  setAnimation(AnimType::ANGER); break;
            case AnimType::ANGER: setAnimation(AnimType::DIZZY); break;
            case AnimType::DIZZY: setAnimation(AnimType::SAD);   break;
            case AnimType::SAD:   setAnimation(AnimType::SWEAR); break;
            case AnimType::SWEAR: setAnimation(AnimType::SLEEP); break;
            case AnimType::SLEEP: setAnimation(AnimType::IDLE);  break;
        }
    }

    void runCurrentAnimation(TFT_eSPI &tft, TFT_eSprite &sprite) {
        // Плавно интерполируем цвета палитры на каждом кадре
        FaceState::updateColors(tft, 0.15f);

        switch (currentAnim) {
            case AnimType::IDLE:
                AnimIdle::runStep(tft, sprite);
                break;
            case AnimType::ANGER:
                AnimAnger::runStep(tft, sprite);
                break;
            case AnimType::DIZZY:
                AnimDizzy::runStep(tft, sprite);
                break;
            case AnimType::SAD:
                AnimSad::runStep(tft, sprite);
                break;
            case AnimType::SWEAR:
                AnimSwear::runStep(tft, sprite);
                break;
            case AnimType::SLEEP:
                AnimSleep::runStep(tft, sprite);
                break;
        }
    }
}
