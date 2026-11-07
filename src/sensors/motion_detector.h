#pragma once
#include <stdint.h>

// ============================================================================
// MotionDetector — детектор движений корпуса по гироскопу BMI160.
//
// Распознает два жеста:
//   1. Rocking (укачивание): плавные ритмичные покачивания 0.6..1.8 Гц,
//      амплитуда 45..260 град/с, без резких ударов, минимум 4 полупериода
//      подряд (~2-3 секунды укачивания).
//   2. Shake (тряска): резкие знакопеременные рывки > 320 град/с,
//      минимум 3 рывка с интервалом < 350 мс.
//
// Использование (main.cpp):
//   MotionDetector::init();           // один раз в setup() ПОСЛЕ ImuSensor::init()
//   MotionDetector::update();         // каждый проход loop()
//   if (MotionDetector::consumeRocking()) { ... }   // флаг сбрасывается при чтении
//   if (MotionDetector::consumeShake())   { ... }
// ============================================================================
namespace MotionDetector {

    void init();
    void update();

    // Датчик найден и отвечает по I2C
    bool isReady();

    // Забрать событие (возвращает true один раз, затем флаг сбрасывается)
    bool consumeRocking();
    bool consumeShake();

    // Подсмотреть флаг без сброса
    bool isRockingDetected();
    bool isShakeDetected();

    // ---- Отладка / сервисное меню ----
    float getGyroX();           // сглаженная угловая скорость, град/с
    float getGyroY();
    float getGyroZ();
    int   getDominantAxis();    // 0 = X, 1 = Y, 2 = Z — ось, по которой качают
    float getRockSignal();      // текущий сигнал доминирующей оси, град/с
    int   getRockHalfCycles();  // сколько «хороших» полупериодов насчитано подряд
    float getRockAmplitude();   // амплитуда последнего полупериода, град/с
    uint32_t getRockingCount(); // сколько раз сработало укачивание с момента старта
    uint32_t getShakeCount();   // сколько раз сработала тряска
}
