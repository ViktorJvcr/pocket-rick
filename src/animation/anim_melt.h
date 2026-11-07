#pragma once
#include <TFT_eSPI.h>

// ============================================================================
//  AnimMelt — «Вверх тормашками»: глаза стекают в неоновую лужу
//  Фазы: STARTLE (испуг) -> STRETCH (вытягивание, капли) -> PUDDLE (лужа)
//        -> RECOVER (по startRecovery(): сборка обратно с желейным отскоком)
// ============================================================================
namespace AnimMelt {
    void init(TFT_eSPI &tft);
    void reset();
    void runStep(TFT_eSPI &tft, TFT_eSprite &sprite);

    // Робот вернули в нормальное положение — собрать глаза обратно
    void startRecovery();

    // true, пока идёт фаза восстановления
    bool isRecovering();

    // true, когда восстановление полностью завершено (main возвращает IDLE)
    bool isFinished();

    // Боковой крен -1..+1: лужа сползает к более низкому краю экрана
    void setSideTilt(float side);
}
