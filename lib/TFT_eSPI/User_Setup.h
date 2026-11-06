#pragma once

#define USER_SETUP_INFO "ESP32-C3 SuperMini ST7789 240x280"

#define ST7789_DRIVER      // Драйвер контроллера ST7789
#define TFT_DRIVER 0x7789

#define TFT_WIDTH  240
#define TFT_HEIGHT 280
#define CGRAM_OFFSET       // Смещение координат для матрицы 240x280

#define TFT_RGB_ORDER TFT_BGR // Цветовая схема BGR
#define TFT_INVERSION_ON      // Инверсия для IPS матрицы

// Пины ESP32-C3 SuperMini
#define TFT_CS    0
#define TFT_DC    1
#define TFT_SCLK  2
#define TFT_MOSI  3
#define TFT_RST   4
#define TFT_BL    5
#define TFT_MISO -1

#define TFT_BACKLIGHT_ON HIGH

#define LOAD_GLCD   // Стандартный шрифт
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY       80000000
#define SPI_READ_FREQUENCY  20000000
