#include "touch_input.h"
#include "pins.h"

namespace TouchInput {

namespace {
    const uint8_t kPins[COUNT] = { TOUCH_MID_PIN, TOUCH_RIGHT_PIN };

    struct BtnState {
        bool     raw        = false;
        bool     stable     = false;
        bool     prevStable = false;
        uint32_t lastChange = 0;
        uint32_t pressedAt  = 0;
        bool     longFired  = false;
        bool     clickFlag  = false;
        bool     longFlag   = false;
        bool     inChord    = false;   // участвовала в аккорде -> клик не генерировать
    } st[COUNT];

    uint32_t armedAt   = 0;
    bool     chordFlag = false;
    bool     chordLatched = false;
}

uint8_t pinOf(Button b) { return kPins[b]; }

void init() {
    for (uint8_t i = 0; i < COUNT; ++i) pinMode(kPins[i], INPUT);   // TTP223 push-pull, подтяжки не нужны
    armedAt = millis() + ARM_DELAY_MS;
}

void update() {
    uint32_t now = millis();
    if ((int32_t)(now - armedAt) < 0) return;      // ещё "не взведено"

    // --- дебаунс каждой кнопки ---
    for (uint8_t i = 0; i < COUNT; ++i) {
        BtnState& b = st[i];
        bool r = digitalRead(kPins[i]) == HIGH;
        if (r != b.raw) { b.raw = r; b.lastChange = now; }
        if (now - b.lastChange >= DEBOUNCE_MS) b.stable = b.raw;
    }

    // --- аккорд: обе стабильно нажаты ---
    bool both = st[MID].stable && st[RIGHT].stable;
    if (both && !chordLatched) {
        chordLatched = true;
        chordFlag = true;
        st[MID].inChord = st[RIGHT].inChord = true;
    }
    if (!st[MID].stable && !st[RIGHT].stable) chordLatched = false;

    // --- фронты, клики, долгие нажатия ---
    for (uint8_t i = 0; i < COUNT; ++i) {
        BtnState& b = st[i];
        if (b.stable && !b.prevStable) {          // нажатие
            b.pressedAt = now; b.longFired = false; b.inChord = false;
        }
        if (b.stable && !b.longFired && !b.inChord && now - b.pressedAt >= LONG_PRESS_MS) {
            b.longFired = true; b.longFlag = true;
        }
        if (!b.stable && b.prevStable) {          // отпускание
            if (!b.longFired && !b.inChord && now - b.pressedAt >= DEBOUNCE_MS) b.clickFlag = true;
            b.inChord = false;
        }
        b.prevStable = b.stable;
    }
}

bool isPressed(Button b)       { return st[b].stable; }
bool wasClicked(Button b)      { bool f = st[b].clickFlag; st[b].clickFlag = false; return f; }
bool wasLongPressed(Button b)  { bool f = st[b].longFlag;  st[b].longFlag  = false; return f; }
bool wasChord()                { bool f = chordFlag; chordFlag = false; return f; }
bool bothHeld()                { return st[MID].stable && st[RIGHT].stable; }

}  // namespace TouchInput
