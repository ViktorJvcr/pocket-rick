#include "touch_input.h"

namespace TouchInput {
namespace {
    unsigned long initTime = 0;
    unsigned long bothDownTime = 0;
    bool bothDownActive = false;
    bool eventBothHold = false;

    enum BtnState {
        STATE_IDLE,
        STATE_DEBOUNCE,
        STATE_PRESSED,
        STATE_HOLD,
        STATE_WAIT_DOUBLE
    };

    struct TouchBtn {
        uint8_t pin;
        BtnState state;
        bool rawState;
        unsigned long stateChangeTime;
        bool eventClick;
        bool eventDoubleClick;
        bool eventHold;
    };

    TouchBtn btnMid   = { TOUCH_MID_PIN,   STATE_IDLE, false, 0, false, false, false };
    TouchBtn btnRight = { TOUCH_RIGHT_PIN, STATE_IDLE, false, 0, false, false, false };

    void processButton(TouchBtn &btn, unsigned long now) {
        bool isDown = (digitalRead(btn.pin) == HIGH);

        switch (btn.state) {
            case STATE_IDLE:
                if (isDown) {
                    btn.state = STATE_DEBOUNCE;
                    btn.stateChangeTime = now;
                }
                break;

            case STATE_DEBOUNCE:
                if (isDown) {
                    if (now - btn.stateChangeTime >= DEBOUNCE_DELAY_MS) {
                        btn.state = STATE_PRESSED;
                        btn.stateChangeTime = now;
                    }
                } else {
                    btn.state = STATE_IDLE;
                }
                break;

            case STATE_PRESSED:
                if (isDown) {
                    if (now - btn.stateChangeTime >= HOLD_DELAY_MS) {
                        btn.eventHold = true;
                        btn.state = STATE_HOLD;
                    }
                } else {
                    btn.state = STATE_WAIT_DOUBLE;
                    btn.stateChangeTime = now;
                }
                break;

            case STATE_HOLD:
                if (!isDown) {
                    btn.state = STATE_IDLE;
                }
                break;

            case STATE_WAIT_DOUBLE:
                if (isDown) {
                    btn.eventDoubleClick = true;
                    btn.state = STATE_HOLD;
                } else if (now - btn.stateChangeTime >= DOUBLE_CLICK_MS) {
                    btn.eventClick = true;
                    btn.state = STATE_IDLE;
                }
                break;
        }

        btn.rawState = isDown;
    }
} // anonymous namespace

void init() {
    pinMode(TOUCH_MID_PIN, INPUT);
    pinMode(TOUCH_RIGHT_PIN, INPUT);

    initTime = millis();
    btnMid.state = STATE_IDLE;
    btnRight.state = STATE_IDLE;
}

void update() {
    unsigned long now = millis();

    if (now - initTime < ARM_DELAY_MS) {
        return;
    }

    processButton(btnMid, now);
    processButton(btnRight, now);

    // Логика зажатия обеих кнопок
    if (btnMid.rawState && btnRight.rawState) {
        if (!bothDownActive) {
            bothDownActive = true;
            bothDownTime = now;
        } else if (now - bothDownTime >= BOTH_HOLD_DELAY_MS) {
            eventBothHold = true;
            bothDownActive = false;

            btnMid.eventClick = false;
            btnMid.eventDoubleClick = false;
            btnMid.eventHold = false;
            btnRight.eventClick = false;
            btnRight.eventDoubleClick = false;
            btnRight.eventHold = false;
        }
    } else {
        bothDownActive = false;
    }
}

bool isMidClicked() {
    if (btnMid.eventClick) {
        btnMid.eventClick = false;
        return true;
    }
    return false;
}

bool isRightClicked() {
    if (btnRight.eventClick) {
        btnRight.eventClick = false;
        return true;
    }
    return false;
}

bool isMidDoubleClicked() {
    if (btnMid.eventDoubleClick) {
        btnMid.eventDoubleClick = false;
        return true;
    }
    return false;
}

bool isRightDoubleClicked() {
    if (btnRight.eventDoubleClick) {
        btnRight.eventDoubleClick = false;
        return true;
    }
    return false;
}

bool isMidHold() {
    if (btnMid.eventHold) {
        btnMid.eventHold = false;
        return true;
    }
    return false;
}

bool isRightHold() {
    if (btnRight.eventHold) {
        btnRight.eventHold = false;
        return true;
    }
    return false;
}

bool isBothHold() {
    if (eventBothHold) {
        eventBothHold = false;
        return true;
    }
    return false;
}

bool isMidDown() {
    return btnMid.rawState;
}

bool isRightDown() {
    return btnRight.rawState;
}

bool isBothDown() {
    return btnMid.rawState && btnRight.rawState;
}

} // namespace TouchInput
