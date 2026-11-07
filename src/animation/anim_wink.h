#pragma once

#include <TFT_eSPI.h>

namespace AnimWink {

void init(TFT_eSPI &tft);
void reset();
void runStep(TFT_eSPI &tft, TFT_eSprite &sprite);

// true только после отрисовки завершающего кадра.
// Сбрасывается в false при reset().
bool isFinished();

} // namespace AnimWink
