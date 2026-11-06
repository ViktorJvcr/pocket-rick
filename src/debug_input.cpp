#include "debug_input.h"

namespace DebugInput {
    uint8_t buttonPin;
    bool lastButtonState = HIGH;      // Кнопка не нажата (из-за INPUT_PULLUP)
    bool buttonActive = false;        // Флаг, что нажатие уже обработано
    unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 50; // 50 мс для подавления дребезга контактов

    void init(uint8_t pin) {
        buttonPin = pin;
        // Используем внутренний подтягивающий резистор. 
        // Не нажата = HIGH, нажата = LOW.
        pinMode(buttonPin, INPUT_PULLUP); 
    }

    bool isButtonPressed() {
        bool currentState = digitalRead(buttonPin);
        bool pressed = false;

        // Если состояние изменилось (начался дребезг или реальное нажатие)
        if (currentState != lastButtonState) {
            lastDebounceTime = millis();
        }

        // Если состояние стабильно дольше, чем debounceDelay
        if ((millis() - lastDebounceTime) > debounceDelay) {
            // Если кнопка нажата (LOW) и мы это еще не зафиксировали
            if (currentState == LOW && !buttonActive) {
                pressed = true;       // Отдаем true только один раз!
                buttonActive = true;  // Запоминаем, что обработали
            } 
            // Если кнопку отпустили (HIGH), сбрасываем флаг
            else if (currentState == HIGH) {
                buttonActive = false;
            }
        }

        lastButtonState = currentState;
        return pressed;
    }
}
