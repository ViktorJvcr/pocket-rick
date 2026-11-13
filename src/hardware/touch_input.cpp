#include "touch_input.h"

namespace TouchInput {
namespace {
    unsigned long initTime = 0;
    unsigned long bothDownTime = 0;
    bool bothDownActive = false;
    bool eventBothHold = false;

    // Переменные для детекции щекотки (чередование кнопок)
    int lastTappedBtn = 0;          // 1 = Mid, 2 = Right
    unsigned long lastTapTime = 0;
    int tickleStreak = 0;
    bool eventTickle = false;
    constexpr unsigned long TICKLE_MAX_INTERVAL_MS = 450;

    enum BtnState {
        STATE_IDLE,
        STATE_DEBOUNCE,
        STATE_PRESSED_1,
        STATE_WAIT_DOUBLE,
        STATE_PRESSED_2,
        STATE_WAIT_TRIPLE,
        STATE_HOLD
    };

    struct TouchBtn {
        uint8_t pin;
        BtnState state;
        bool rawState;
        unsigned long stateChangeTime;
        unsigned long pressStartTime;
        bool eventClick;
        bool eventDoubleClick;
        bool eventTripleClick;
        bool eventHold;
    };

    TouchBtn btnMid   = { TOUCH_MID_PIN,   STATE_IDLE, false, 0, 0, false, false, false, false };
    TouchBtn btnRight = { TOUCH_RIGHT_PIN, STATE_IDLE, false, 0, 0, false, false, false, false };

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
                        btn.state = STATE_PRESSED_1;
                        btn.stateChangeTime = now;
                        btn.pressStartTime = now;
                    }
                } else {
                    btn.state = STATE_IDLE;
                }
                break;

            case STATE_PRESSED_1:
                if (isDown) {
                    if (now - btn.pressStartTime >= HOLD_DELAY_MS) {
                        btn.eventHold = true;
                        btn.state = STATE_HOLD;
                    }
                } else {
                    btn.state = STATE_WAIT_DOUBLE;
                    btn.stateChangeTime = now;
                }
                break;

            case STATE_WAIT_DOUBLE:
                if (isDown) {
                    btn.state = STATE_PRESSED_2;
                    btn.stateChangeTime = now;
                } else if (now - btn.stateChangeTime >= DOUBLE_CLICK_MS) {
                    btn.eventClick = true;
                    btn.state = STATE_IDLE;
                }
                break;

            case STATE_PRESSED_2:
                if (isDown) {
                    if (now - btn.stateChangeTime >= HOLD_DELAY_MS) {
                        btn.eventHold = true;
                        btn.state = STATE_HOLD;
                    }
                } else {
                    btn.state = STATE_WAIT_TRIPLE;
                    btn.stateChangeTime = now;
                }
                break;

            case STATE_WAIT_TRIPLE:
                if (isDown) {
                    btn.eventTripleClick = true;
                    btn.state = STATE_HOLD;
                } else if (now - btn.stateChangeTime >= DOUBLE_CLICK_MS) {
                    btn.eventDoubleClick = true;
                    btn.state = STATE_IDLE;
                }
                break;

            case STATE_HOLD:
                if (!isDown) {
                    btn.state = STATE_IDLE;
                    btn.eventHold = false;
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

    // --- Детекция щекотки ---
    bool midHit = (btnMid.rawState && btnMid.state == STATE_PRESSED_1 && btnMid.stateChangeTime == now);
    bool rightHit = (btnRight.rawState && btnRight.state == STATE_PRESSED_1 && btnRight.stateChangeTime == now);

    if (midHit || rightHit) {
        int currentHit = midHit ? 1 : 2;

        if (lastTappedBtn != 0 && currentHit != lastTappedBtn && (now - lastTapTime <= TICKLE_MAX_INTERVAL_MS)) {
            tickleStreak++;
            if (tickleStreak >= 3) {
                eventTickle = true;
            }
        } else {
            tickleStreak = 1;
        }

        lastTappedBtn = currentHit;
        lastTapTime = now;
    } else if (now - lastTapTime > TICKLE_MAX_INTERVAL_MS) {
        tickleStreak = 0;
        lastTappedBtn = 0;
    }

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
            btnMid.eventTripleClick = false;
            btnMid.eventHold = false;
            btnRight.eventClick = false;
            btnRight.eventDoubleClick = false;
            btnRight.eventTripleClick = false;
            btnRight.eventHold = false;
        }
    } else {
        bothDownActive = false;
    }
}

bool isMidClicked() {
    if (btnMid.eventClick) { btnMid.eventClick = false; return true; }
    return false;
}

bool isRightClicked() {
    if (btnRight.eventClick) { btnRight.eventClick = false; return true; }
    return false;
}

bool isMidDoubleClicked() {
    if (btnMid.eventDoubleClick) { btnMid.eventDoubleClick = false; return true; }
    return false;
}

bool isRightDoubleClicked() {
    if (btnRight.eventDoubleClick) { btnRight.eventDoubleClick = false; return true; }
    return false;
}

bool isMidTripleClicked() {
    if (btnMid.eventTripleClick) { btnMid.eventTripleClick = false; return true; }
    return false;
}

bool isRightTripleClicked() {
    if (btnRight.eventTripleClick) { btnRight.eventTripleClick = false; return true; }
    return false;
}

bool isMidHold()   { return btnMid.eventHold || (btnMid.rawState && btnMid.state == STATE_HOLD); }
bool isRightHold() { return btnRight.eventHold || (btnRight.rawState && btnRight.state == STATE_HOLD); }
bool isBothHold()  {
    if (eventBothHold) { eventBothHold = false; return true; }
    return false;
}

uint32_t getMidHoldDuration() {
    if (!btnMid.rawState) return 0;
    return millis() - btnMid.pressStartTime;
}

uint32_t getRightHoldDuration() {
    if (!btnRight.rawState) return 0;
    return millis() - btnRight.pressStartTime;
}

bool isMidDown()   { return btnMid.rawState; }
bool isRightDown() { return btnRight.rawState; }
bool isBothDown()  { return btnMid.rawState && btnRight.rawState; }

bool isTickleDetected() {
    if (eventTickle) { eventTickle = false; return true; }
    return false;
}

} // namespace TouchInput
