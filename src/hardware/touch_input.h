#pragma once
#include <Arduino.h>
#include "pins.h"

namespace TouchInput {
    // Временные пороги (мс)
    constexpr unsigned long ARM_DELAY_MS       = 400;  // Защита от помех UART0 при старте
    constexpr unsigned long DEBOUNCE_DELAY_MS  = 35;   // Антидребезг
    constexpr unsigned long DOUBLE_CLICK_MS    = 280;  // Окно ожидания второго клика
    constexpr unsigned long HOLD_DELAY_MS      = 800;  // Одиночное удержание
    constexpr unsigned long BOTH_HOLD_DELAY_MS = 1500; // Удержание обеих кнопок (сервисное меню)

    void init();
    void update();

    // Одиночные клики
    bool isMidClicked();
    bool isRightClicked();

    // Двойные клики
    bool isMidDoubleClicked();
    bool isRightDoubleClicked();

    // Удержания
    bool isMidHold();
    bool isRightHold();
    bool isBothHold();
    bool isTickleDetected();      // Сработало быстрое поочередное нажатие (щекотка)

    // Текущие состояния
    bool isMidDown();
    bool isRightDown();
    bool isBothDown();

    // Совместимость
    inline bool isMidPressed()   { return isMidClicked(); }
    inline bool isRightPressed() { return isRightClicked(); }
}
