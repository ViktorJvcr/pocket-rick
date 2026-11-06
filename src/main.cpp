#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "pins.h"
#include "display/display.h"
#include "animation/anim_manager.h"
#include "hardware/touch_input.h"
#include "sensors/imu_sensor.h"

// Создаем объекты экрана
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

// Переменные для замера и ограничения FPS
unsigned long lastFpsPrint = 0;
uint32_t frameCount = 0;
unsigned long lastFrameTime = 0;
constexpr unsigned long TARGET_FRAME_MS = 16; // ~60 FPS макс

void setup() {
    Serial.begin(115200);
    delay(1000); 
    Serial.println(">>> СТАРТ ПРОШИВКИ РОБОТА (ESP32-C3) [HIGH FPS MODE] <<<");

    // 1. Инициализация сенсорных кнопок TTP223 (пины 20, 21)
    TouchInput::init();

    // 2. Инициализация дисплея ST7789 (240x280 IPS) и спрайта
    initDisplay(tft, sprite);

    // 3. Инициализация анимационного движка
    AnimManager::init(tft);

    // 4. Инициализация датчика BMI160 на шине I2C (SDA=6, SCL=7)
    ImuSensor::init();

    Serial.printf("Стартовая анимация: %s\n", AnimManager::getAnimName(AnimManager::getCurrentAnim()));
    Serial.println("Частота SPI: 80 МГц. Блокирующие паузы устранены.");
    Serial.println("Для переключения анимаций коснитесь сенсора на GPIO 21.");
    lastFpsPrint = millis();
    lastFrameTime = millis();
}

void loop() {
    // 1. Опрос сенсорных кнопок TTP223
    TouchInput::update();

    if (TouchInput::isRightPressed()) {
        AnimManager::nextState();
        Serial.printf("[TOUCH GPIO 21] Переключение -> %s\n", 
                      AnimManager::getAnimName(AnimManager::getCurrentAnim()));
    }

    if (TouchInput::isMidPressed()) {
        AnimManager::nextState();
        Serial.printf("[TOUCH GPIO 20] Переключение -> %s\n", 
                      AnimManager::getAnimName(AnimManager::getCurrentAnim()));
    }

    // 2. Опрос датчика IMU (параллакс взгляда)
    ImuSensor::update();

    // 3. Отрисовка текущего кадра анимации
    AnimManager::runCurrentAnimation(tft, sprite);

    // 4. Подсчет реального FPS и вывод в Serial раз в 1 секунду
    frameCount++;
    unsigned long now = millis();
    if (now - lastFpsPrint >= 1000) {
        float fps = (frameCount * 1000.0f) / (now - lastFpsPrint);
        Serial.printf("[PERF] Текущий FPS: %.1f | Анимация: %s\n", fps, AnimManager::getAnimName(AnimManager::getCurrentAnim()));
        frameCount = 0;
        lastFpsPrint = now;
    }

    // 5. Неблокирующий тайминг кадра (до 60 FPS)
    unsigned long renderElapsed = millis() - now;
    if (renderElapsed < TARGET_FRAME_MS) {
        // Минимальная разгрузка ядра для работы Wi-Fi/системных задач ESP32
        delay(1);
    } else {
        yield();
    }
}
