#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

namespace AnimIntro {
    // Воспроизводит заставку LDR (Любовь. Смерть. Роботы)
    // Рисует через существующий буфер sprite 240x200
    void play(TFT_eSPI &tft, TFT_eSprite &sprite);
}
