#include "display.h"
#include <esp_heap_caps.h>
#include <esp_arduino_version.h>

// ============================================================================
//  Подсветка
// ============================================================================
static void backlightInit() {
#if TFT_BL_USE_PWM
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(TFT_BL_PIN, TFT_BL_PWM_FREQ, 8);
  #else
    ledcSetup(0, TFT_BL_PWM_FREQ, 8);
    ledcAttachPin(TFT_BL_PIN, 0);
  #endif
#else
    pinMode(TFT_BL_PIN, OUTPUT);
#endif
}

void setBacklight(uint8_t level) {
#if TFT_BL_USE_PWM
    uint32_t duty = TFT_BL_ACTIVE_HIGH ? level : (255 - level);
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(TFT_BL_PIN, duty);
  #else
    ledcWrite(0, duty);
  #endif
#else
    bool on = level >= 128;
    digitalWrite(TFT_BL_PIN, (on == (bool)TFT_BL_ACTIVE_HIGH) ? HIGH : LOW);
#endif
}

// ============================================================================
//  Инициализация панели
// ============================================================================
bool initDisplay(PocketST7789& tft) {
    backlightInit();
    setBacklight(0);                         // гасим, пока идёт init (нет мусора на экране)

    // init(240, 280): библиотека сама выставит rowstart=20 (матрица центрирована в GRAM 320)
    tft.init(TFT_NATIVE_W, TFT_NATIVE_H, SPI_MODE0);
    tft.setSPISpeed(TFT_SPI_HZ);

#if PANEL_OFFSET_OVERRIDE
    tft.setPanelOffsets(PANEL_COLSTART, PANEL_ROWSTART, PANEL_COLSTART2, PANEL_ROWSTART2);
#endif

    tft.setRotation(DISPLAY_ROTATION);
    tft.invertDisplay(DISPLAY_INVERT);
    tft.fillScreen(ST77XX_BLACK);

    bool ok = (tft.width() == SCREEN_W) && (tft.height() == SCREEN_H);
    Serial.printf("[TFT] ST7789 %dx%d rot=%d spi=%lu Hz -> %s\n",
                  tft.width(), tft.height(), DISPLAY_ROTATION,
                  (unsigned long)TFT_SPI_HZ, ok ? "OK" : "SIZE MISMATCH!");

    setBacklight(TFT_BL_DEFAULT);
    return ok;
}

// ============================================================================
//  Кадровый буфер с проверкой памяти
// ============================================================================
PocketSprite* createFrameBuffer() {
    size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t largest  = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    Serial.printf("[MEM] free=%u largest=%u need=%u (+%u reserve)\n",
                  (unsigned)freeHeap, (unsigned)largest,
                  (unsigned)FRAMEBUFFER_BYTES, (unsigned)FB_MIN_HEAP_AFTER);

    if (largest < FRAMEBUFFER_BYTES || freeHeap < FRAMEBUFFER_BYTES + FB_MIN_HEAP_AFTER) {
        Serial.println("[MEM] NOT ENOUGH RAM for 280x240 framebuffer!");
        return nullptr;
    }

    PocketSprite* s = new (std::nothrow) PocketSprite(SCREEN_W, SCREEN_H);
    if (!s)            { Serial.println("[MEM] new PocketSprite failed"); return nullptr; }
    if (!s->isValid()) { Serial.println("[MEM] canvas malloc failed");  delete s; return nullptr; }

    Serial.printf("[MEM] framebuffer OK (%u bytes), heap left=%u\n",
                  (unsigned)s->sizeBytes(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    return s;
}

// ============================================================================
//  Вывод кадра. Один setAddrWindow + непрерывный writePixels: без построчных
//  команд и без мерцания (экран не очищается, просто перезаписывается).
//  На ESP32 writePixels(..., bigEndian=false) делает byte-swap чанками по 64 байта.
// ============================================================================
void pushSpriteToDisplay(PocketST7789& tft, PocketSprite& sprite) {
    uint16_t* buf = sprite.getBuffer();
    if (!buf) return;
    tft.startWrite();
    tft.setAddrWindow(0, 0, SCREEN_W, SCREEN_H);
    tft.writePixels(buf, (uint32_t)SCREEN_W * SCREEN_H, true, false);
    tft.endWrite();
}

void pushSpriteRegion(PocketST7789& tft, PocketSprite& sprite,
                      int16_t x, int16_t y, int16_t w, int16_t h) {
    uint16_t* buf = sprite.getBuffer();
    if (!buf) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;

    tft.startWrite();
    tft.setAddrWindow(x, y, w, h);
    for (int16_t row = 0; row < h; ++row) {
        tft.writePixels(buf + (size_t)(y + row) * SCREEN_W + x, (uint32_t)w, true, false);
    }
    tft.endWrite();
}

// ============================================================================
//  Диагностика
// ============================================================================
void drawCalibrationFrame(PocketST7789& tft) {
    tft.fillScreen(ST77XX_BLACK);
    tft.drawRect(0, 0, SCREEN_W, SCREEN_H, ST77XX_GREEN);
    tft.drawFastHLine(0, SCREEN_H / 2, SCREEN_W, ST77XX_RED);
    tft.drawFastVLine(SCREEN_W / 2, 0, SCREEN_H, ST77XX_BLUE);
    tft.setCursor(4, 4); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1);
    tft.printf("%dx%d rot%d", SCREEN_W, SCREEN_H, DISPLAY_ROTATION);
}

void haltOutOfMemory(PocketST7789& tft) {
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_RED); tft.setTextSize(2);
    tft.setCursor(20, 90);  tft.print("OUT OF RAM");
    tft.setTextSize(1);     tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(20, 120); tft.printf("need %u bytes", (unsigned)FRAMEBUFFER_BYTES);
    tft.setCursor(20, 132); tft.printf("free %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    Serial.println("[FATAL] framebuffer allocation failed — halted");
    for (;;) {                               // мигание подсветкой — виден сбой даже без Serial
        setBacklight(255); delay(300);
        setBacklight(40);  delay(300);
    }
}
