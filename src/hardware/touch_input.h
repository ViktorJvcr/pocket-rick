#pragma once
#include <Arduino.h>

// ============================================================================
//  TouchInput — 2 сенсорные кнопки TTP223 (MID=GPIO20, RIGHT=GPIO21) + события:
//  клик, долгое нажатие, аккорд (обе сразу). Аккорд и долгое нажатие
//  заменяют потерянную третью физическую кнопку.
//  isPressed() читает пин напрямую (как раньше); события требуют update() в loop().
// ============================================================================
namespace TouchInput {

enum Button : uint8_t { MID = 0, RIGHT = 1, COUNT = 2 };

constexpr uint32_t ARM_DELAY_MS   = 400;   // игнор после старта (GPIO21 = UART0 TX шумит при boot)
constexpr uint32_t DEBOUNCE_MS    = 30;
constexpr uint32_t LONG_PRESS_MS  = 600;
constexpr uint32_t CHORD_WINDOW_MS = 150;  // окно, в котором обе кнопки считаются аккордом

void init();
void update();                       // вызывать каждый проход loop()

bool isPressed(Button b);            // текущее стабильное состояние (после дебаунса)
bool wasClicked(Button b);           // короткий клик (сбрасывается при чтении)
bool wasLongPressed(Button b);       // долгое нажатие (один раз, сбрасывается при чтении)
bool wasChord();                     // MID+RIGHT одновременно (сбрасывается при чтении)
bool bothHeld();                     // обе удерживаются прямо сейчас

uint8_t pinOf(Button b);
}
