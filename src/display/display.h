#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "config.h"
#include "pins.h"

// ----------------------------------------------------------------------------
//  PocketSprite — кадровый буфер RGB565 (GFXcanvas16). 280x240 = 134 400 байт.
//  Создавать ТОЛЬКО через createFrameBuffer() — там проверка памяти.
// ----------------------------------------------------------------------------
class PocketSprite : public GFXcanvas16 {
public:
    PocketSprite(uint16_t w, uint16_t h) : GFXcanvas16(w, h) {}

    bool   isValid()  const { return getBuffer() != nullptr; }
    size_t sizeBytes() const { return (size_t)width() * (size_t)height() * 2u; }

    inline void fillSprite(uint16_t color) { fillScreen(color); }
};

// ----------------------------------------------------------------------------
//  PocketST7789 — Adafruit_ST7789 + доступ к оффсетам GRAM для нестандартных
//  ревизий панели 240x280 (поля _colstart/_rowstart/... в базовом классе protected).
// ----------------------------------------------------------------------------
class PocketST7789 : public Adafruit_ST7789 {
public:
    using Adafruit_ST7789::Adafruit_ST7789;

    void setPanelOffsets(uint8_t col, uint8_t row, uint8_t col2, uint8_t row2) {
        _colstart  = col;  _rowstart  = row;
        _colstart2 = col2; _rowstart2 = row2;
        setRotation(getRotation());   // пересчитать _xstart/_ystart
    }
};

// Инициализация панели (SPI.begin() должен быть вызван до этого!)
bool          initDisplay(PocketST7789& tft);

// Выделение кадрового буфера с проверкой кучи. nullptr при нехватке.
PocketSprite* createFrameBuffer();

// Вывод кадра целиком (одно окно, непрерывный поток пикселей)
void          pushSpriteToDisplay(PocketST7789& tft, PocketSprite& sprite);

// Вывод прямоугольной области буфера (задел под dirty-rect / полубуфер)
void          pushSpriteRegion(PocketST7789& tft, PocketSprite& sprite,
                               int16_t x, int16_t y, int16_t w, int16_t h);

// Подсветка 0..255 (при TFT_BL_USE_PWM=0 — порог 128)
void          setBacklight(uint8_t level);

// Диагностика: рамка 1px по периметру + крест. Все 4 стороны видны = оффсеты верны
void          drawCalibrationFrame(PocketST7789& tft);

// Аварийная остановка при нехватке RAM (сообщение на экран + мигание подсветкой)
void          haltOutOfMemory(PocketST7789& tft) __attribute__((noreturn));
