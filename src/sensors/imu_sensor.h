#pragma once

#include <stdint.h>

namespace ImuSensor {

bool init();
void update(uint32_t now);

bool isConnected();
uint8_t getAddress();

// Дополнительный диагностический метод.
// true после успешной калибровки неподвижного датчика.
bool isCalibrated();

// Ускорение, g.
float getAccX();
float getAccY();
float getAccZ();

// Угловая скорость, градусы/секунду.
// После калибровки — с компенсацией смещения нуля.
float getGyrX();
float getGyrY();
float getGyrZ();

// Смещение взгляда, пиксели.
float getParallaxX();
float getParallaxY();

// Флаги событий сбрасываются при чтении.
bool isSuddenLiftDetected();
bool isShakeDetected();

} // namespace ImuSensor
