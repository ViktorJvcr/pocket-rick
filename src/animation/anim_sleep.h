#pragma once
// ВАЖНО: Arduino.h подключаем первым, в заголовке нет ни одного #define,
// чтобы не конфликтовать с макросами ядра ESP32.
#include <Arduino.h>
#include <TFT_eSPI.h>

namespace AnimSleep {
    void init(TFT_eSPI &tft);
    void reset();                                        // старт засыпания (из IDLE)
    void runStep(TFT_eSPI &tft, TFT_eSprite &sprite);    // кадр
    void wakeUpGrumpy();                                 // разбудили тряской
    void wakeUpAngry();                                  // разбудили 5 стуками
    bool isFinished();                                   // ворчливое пробуждение завершено -> IDLE
    bool isAngerReady();                                 // пора передавать в AnimType::ANGER
    bool isAsleep();                                     // робот спит (его можно будить)
}
