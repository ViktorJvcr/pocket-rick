#pragma once
#include <Arduino.h>
#include "pins.h"

namespace TouchInput {
    // Первые 400 мс после init() все касания игнорируются (UART0 пины 20/21)
    constexpr unsigned long ARM_DELAY_MS = 400;
    // Время подавления дребезга контактов
    constexpr unsigned long DEBOUNCE_DELAY_MS = 40;

    void init();
    void update();

    // Одиночные клики (возвращают true ровно один раз за касание)
    bool isMidPressed();
    bool isRightPressed();

    // Текущее состояние (удержание кнопки)
    bool isMidDown();
    bool isRightDown();
}
