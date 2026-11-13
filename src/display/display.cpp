#include "display.h"
#include "config.h"
#include "pins.h"
#include <SPI.h>

void initDisplay(TFT_eSPI &tft, TFT_eSprite &sprite) {
    Serial.println("\n========================================");
    Serial.println("[DISPLAY] Старт инициализации ST7789 SPI...");

    // 1. Настройка управляющих пинов вручную для гарантированного старта
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);

    pinMode(TFT_DC, OUTPUT);
    digitalWrite(TFT_DC, HIGH);

    // 2. Аппаратный сброс ST7789 (RES / PIN 4)
    pinMode(TFT_RST, OUTPUT);
    digitalWrite(TFT_RST, HIGH);
    delay(10);
    digitalWrite(TFT_RST, LOW);
    delay(30);
    digitalWrite(TFT_RST, HIGH);
    delay(120); // ST7789 требует до 120 мс после сброса перед отправкой команд

    // 3. Включаем подсветку экрана (PIN 5)
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    // 4. Инициализация TFT_eSPI
    tft.init();
    tft.setRotation(0); // Портрет 240x280
    tft.invertDisplay(true); // Включаем инверсию IPS

    // 5. Тестовая заливка (Красный -> Зеленый -> Синий -> Черный)
    Serial.println("[DISPLAY] Проверка SPI: цветная тестовая заливка...");
    tft.fillScreen(TFT_RED);
    delay(200);
    tft.fillScreen(TFT_GREEN);
    delay(200);
    tft.fillScreen(TFT_BLUE);
    delay(200);
    tft.fillScreen(TFT_BLACK);

    // 6. Выделение буфера кадра (спрайта)
    // Лицо робота центрировано и занимает область 240x200, что гарантированно
    // помещается в свободную непрерывную RAM ESP32-C3 без ошибок аллокации!
    Serial.printf("[SYSTEM] Свободная RAM: %u байт, Макс. блок: %u байт\n", 
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    void* spritePtr = sprite.createSprite(SCREEN_WIDTH, 200);
    if (spritePtr == nullptr) {
        // Fallback на 240x180
        Serial.println("[DISPLAY] Аллокация 240x200 не удалась, пробую 240x180...");
        spritePtr = sprite.createSprite(SCREEN_WIDTH, 180);
    }

    if (spritePtr != nullptr) {
        Serial.println("[DISPLAY] Буфер спрайта успешно создан!");
        sprite.fillSprite(TFT_BLACK);
        sprite.pushSprite(0, 40); // Центрируем по вертикали (отступ 40 сверху)
    } else {
        Serial.println("[DISPLAY] ОШИБКА: Не удалось выделить память под спрайт!");
    }
    Serial.println("========================================\n");
}
