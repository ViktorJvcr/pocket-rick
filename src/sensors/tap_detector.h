#pragma once
#include <Arduino.h>

// Детектор стука по корпусу (BMI160, фильтр первой производной / jerk).
// Интерфейс для main.cpp:
//   TapDetector::init();            // один раз в setup() ПОСЛЕ ImuSensor::init()
//   TapDetector::update();          // каждый loop()
//   if (TapDetector::consumeTap()) {                  // новый удар (одноразово true)
//       uint8_t n = TapDetector::getComboCount();     // 1..5 — номер удара в серии
//       float dx = TapDetector::getTapDirX();         // откуда пришел удар, -1..+1
//       float dy = TapDetector::getTapDirY();
//   }
namespace TapDetector {
    void    init();
    void    update();

    bool    consumeTap();        // true один раз на каждый зарегистрированный удар
    uint8_t getComboCount();     // число ударов в текущей серии (0 — серии нет)
    bool    isComboActive();     // окно серии еще открыто

    float   getTapDirX();        // нормированное направление удара по X (-1..+1)
    float   getTapDirY();        // нормированное направление удара по Y (-1..+1)
    float   getLastJerk();       // величина рывка последнего удара, g (для отладки)

    bool    isReady();           // BMI160 найден и опрашивается
}
