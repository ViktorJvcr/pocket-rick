#pragma once
#include <Arduino.h>

// ============================================================================
//  Pocket Rick — ESP32-C3 Zero, распиновка v2 (дисплей 1.69" ST7789)
//  Доступные GPIO на C3 Zero: 0..10, 18..21.
//  НЕ трогаем: GPIO9 (BOOT), GPIO8 (страппинг), GPIO18/19 (USB D-/D+).
//  GPIO2 — страппинг (HIGH при загрузке), на нём только вход SCL дисплея.
// ============================================================================

// ===== ДИСПЛЕЙ ST7789 SPI (1.69", 240x280) =====
#define TFT_CS_PIN       0
#define TFT_DC_PIN       1
#define TFT_SCLK_PIN     2
#define TFT_MOSI_PIN     3
#define TFT_RST_PIN      4
#define TFT_BL_PIN       5

#define TFT_CS           TFT_CS_PIN
#define TFT_DC           TFT_DC_PIN
#define TFT_SCLK         TFT_SCLK_PIN
#define TFT_MOSI         TFT_MOSI_PIN
#define TFT_RST          TFT_RST_PIN
#define TFT_BL           TFT_BL_PIN

// ===== I2C ШИНА (BMI160 + APDS-9930) =====
#define I2C_SDA_PIN      6
#define I2C_SCL_PIN      7
#define BMI_INT1_PIN     10

#define I2C_SDA          I2C_SDA_PIN
#define I2C_SCL          I2C_SCL_PIN
#define BMI_INT1         BMI_INT1_PIN

// ===== СЕНСОРНЫЕ КНОПКИ TTP223 (активный HIGH), 2 шт. =====
// ВНИМАНИЕ: GPIO20/21 = UART0 RX/TX. Serial работает по USB-CDC, UART0 свободен,
// но ROM-загрузчик ~100 мс шумит на GPIO21 -> ставим 1 кОм последовательно
// и игнорируем кнопки первые 400 мс после старта (TouchInput::ARM_DELAY_MS).
#define TOUCH_MID_PIN    20
#define TOUCH_RIGHT_PIN  21

#define BTN_MID          TOUCH_MID_PIN
#define BTN_RIGHT        TOUCH_RIGHT_PIN

// Совместимость со старым кодом (3-кнопочная раскладка упразднена).
// TOUCH_BTN_3 НЕ определён намеренно — компилятор покажет все места для правки.
#define TOUCH_BTN_1      TOUCH_MID_PIN
#define TOUCH_BTN_2      TOUCH_RIGHT_PIN
