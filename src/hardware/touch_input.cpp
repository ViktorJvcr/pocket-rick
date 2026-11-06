#include "touch_input.h"

namespace TouchInput {
namespace {
    unsigned long initTime = 0;

    struct TouchBtn {
        uint8_t pin;
        bool lastStableState;
        bool lastRawState;
        unsigned long lastDebounceTime;
        bool pressedEvent;
    };

    TouchBtn btnMid   = { TOUCH_MID_PIN,   false, false, 0, false };
    TouchBtn btnRight = { TOUCH_RIGHT_PIN, false, false, 0, false };

    void processButton(TouchBtn &btn, unsigned long now) {
        // TTP223 выдает активный HIGH при касании
        bool rawState = (digitalRead(btn.pin) == HIGH);

        if (rawState != btn.lastRawState) {
            btn.lastDebounceTime = now;
        }

        if ((now - btn.lastDebounceTime) > DEBOUNCE_DELAY_MS) {
            if (rawState != btn.lastStableState) {
                btn.lastStableState = rawState;
                // Фронт нажатия: переход LOW -> HIGH
                if (btn.lastStableState) {
                    btn.pressedEvent = true;
                }
            }
        }

        btn.lastRawState = rawState;
    }
} // anonymous namespace

void init() {
    // Вход без подтяжки, так как TTP223 имеет собственный выход CMOS/Push-Pull (активный HIGH)
    pinMode(TOUCH_MID_PIN, INPUT);
    pinMode(TOUCH_RIGHT_PIN, INPUT);

    initTime = millis();
    btnMid.lastDebounceTime = initTime;
    btnRight.lastDebounceTime = initTime;
    Serial.println("[TOUCH] TTP223 initialized on pins 20/21 (Arm delay 400ms)");
}

void update() {
    unsigned long now = millis();

    // Первые ARM_DELAY_MS игнорируем любые уровни при перезапуске UART0 пинов
    if (now - initTime < ARM_DELAY_MS) {
        btnMid.lastStableState = false;
        btnMid.lastRawState = false;
        btnMid.pressedEvent = false;

        btnRight.lastStableState = false;
        btnRight.lastRawState = false;
        btnRight.pressedEvent = false;
        return;
    }

    processButton(btnMid, now);
    processButton(btnRight, now);
}

bool isMidPressed() {
    if (btnMid.pressedEvent) {
        btnMid.pressedEvent = false;
        return true;
    }
    return false;
}

bool isRightPressed() {
    if (btnRight.pressedEvent) {
        btnRight.pressedEvent = false;
        return true;
    }
    return false;
}

bool isMidDown() {
    return btnMid.lastStableState;
}

bool isRightDown() {
    return btnRight.lastStableState;
}

} // namespace TouchInput
