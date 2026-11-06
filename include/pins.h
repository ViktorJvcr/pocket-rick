#pragma once

// ===== ДИСПЛЕЙ ST7789 SPI =====
#define TFT_CS_PIN 0
#define TFT_DC_PIN 1
#define TFT_SCLK_PIN 2
#define TFT_MOSI_PIN 3
#define TFT_RST_PIN 4
#define TFT_BL_PIN 5
#define TFT_CS TFT_CS_PIN // короткие синонимы
#define TFT_DC TFT_DC_PIN
#define TFT_SCLK TFT_SCLK_PIN
#define TFT_MOSI TFT_MOSI_PIN
#define TFT_RST TFT_RST_PIN
#define TFT_BL TFT_BL_PIN

// ===== I2C ШИНА (BMI160, APDS-9930) =====
#define I2C_SDA_PIN 6
#define I2C_SCL_PIN 7
#define BMI_INT1_PIN 10
#define I2C_SDA I2C_SDA_PIN
#define I2C_SCL I2C_SCL_PIN
#define BMI_INT1 BMI_INT1_PIN

// ===== СЕНСОРНЫЕ КНОПКИ TTP223 =====
#define TOUCH_MID_PIN 20
#define TOUCH_RIGHT_PIN 21
#define BTN_MID TOUCH_MID_PIN
#define BTN_RIGHT TOUCH_RIGHT_PIN
