#pragma once
#include "pins.h"

// Настройки экрана (ST7789 240 × 280 портрет)
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 280

// Центральные базовые координаты лица робота
#define SCREEN_CENTER_X (SCREEN_WIDTH / 2.0f)   // 120.0f
#define SCREEN_BASE_Y   (SCREEN_HEIGHT / 2.0f)  // 140.0f

// Настройки физики/времени
#define FRAME_DELAY   1      // Задержка между кадрами (~50 FPS)
#define TIME_TO_SLEEP 100000  // Время бездействия до сна (мс)
