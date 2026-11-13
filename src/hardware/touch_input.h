#pragma once
#include <Arduino.h>
#include "pins.h"

namespace TouchInput {
    // Временные пороги (мс)
    constexpr unsigned long ARM_DELAY_MS       = 400;  // Защита от помех при старте
    constexpr unsigned long DEBOUNCE_DELAY_MS  = 35;   // Антидребезг
    constexpr unsigned long DOUBLE_CLICK_MS    = 280;  // Окно ожидания следующего клика
    constexpr unsigned long HOLD_DELAY_MS      = 600;  // Порог начала удержания
    constexpr unsigned long LONG_HOLD_MS       = 2500; // Порог долгого удержания (Portal Glitch / Short Circuit)
    constexpr unsigned long BOTH_HOLD_DELAY_MS = 1500; // Удержание обеих кнопок (сервисное меню)

    void init();
    void update();

    // Одиночные клики
    bool isMidClicked();
    bool isRightClicked();

    // Двойные клики
    bool isMidDoubleClicked();
    bool isRightDoubleClicked();

    // Тройные клики
    bool isMidTripleClicked();
    bool isRightTripleClicked();

    // Удержания (уровень)
    bool isMidHold();
    bool isRightHold();
    bool isBothHold();
    bool isTickleDetected();      // Быстрое поочередное нажатие (щекотка)

    // Длительность текущего удержания (мс, 0 если не удерживается)
    uint32_t getMidHoldDuration();
    uint32_t getRightHoldDuration();

    // Текущие состояния
    bool isMidDown();
    bool isRightDown();
    bool isBothDown();

    // Совместимость
    inline bool isMidPressed()   { return isMidClicked(); }
    inline bool isRightPressed() { return isRightClicked(); }
}
