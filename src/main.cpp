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

// Глобальные переменные замера FPS и сервисного меню
unsigned long lastFpsPrint = 0;
uint32_t frameCount = 0;
unsigned long lastFrameTime = 0;
constexpr unsigned long TARGET_FRAME_MS = 16; // Ограничение ~60 FPS
float currentFpsValue = 0.0f;
bool inServiceMenu = false;

// Отрисовка сервисного меню
void drawServiceMenu(TFT_eSprite &spr) {
    spr.fillSprite(TFT_BLACK);

    // Рамка меню
    spr.drawRoundRect(4, 4, 232, 192, 6, TFT_DARKGREY);
    spr.drawRoundRect(6, 6, 228, 188, 4, TFT_NAVY);

    // Заголовок
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_YELLOW, TFT_BLACK);
    spr.drawString("=== SERVICE MENU ===", 120, 14, 2);

    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    int y = 42;
    int lineH = 18;

    // 1. Время работы
    unsigned long sec = millis() / 1000;
    unsigned int m = (sec / 60) % 60;
    unsigned int h = (sec / 3600);
    unsigned int s = sec % 60;
    spr.drawString(" Uptime:       " + String(h < 10 ? "0" : "") + h + ":" + (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s, 14, y);
    y += lineH;

    // 2. Свободная память RAM
    uint32_t freeHeap = ESP.getFreeHeap() / 1024;
    uint32_t maxBlock = ESP.getMaxAllocHeap() / 1024;
    spr.drawString(" Free RAM:     " + String(freeHeap) + " KB (blk " + String(maxBlock) + "k)", 14, y);
    y += lineH;

    // 3. Фактический FPS
    spr.drawString(" Render FPS:   " + String(currentFpsValue, 1), 14, y);
    y += lineH;

    // 4. Статус датчика BMI160
    bool imuOk = ImuSensor::isReady();
    spr.drawString(" IMU (BMI160): ", 14, y);
    if (imuOk) {
        spr.setTextColor(TFT_GREEN, TFT_BLACK);
        spr.drawString("ONLINE", 130, y);
    } else {
        spr.setTextColor(TFT_RED, TFT_BLACK);
        spr.drawString("OFFLINE", 130, y);
    }
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    y += lineH;

    // 5. Значения параллакса
    spr.drawString(" Parallax:     X:" + String(ImuSensor::getParallaxX(), 0) + "  Y:" + String(ImuSensor::getParallaxY(), 0), 14, y);
    y += lineH;

    // 6. Текущая анимация
    spr.drawString(" Current Anim: " + String(AnimManager::getAnimName(AnimManager::getCurrentAnim())), 14, y);
    y += lineH + 6;

    // Подсказка выхода
    spr.setTextDatum(BC_DATUM);
    spr.setTextColor(TFT_CYAN, TFT_BLACK);
    spr.drawString("Tap any button to EXIT", 120, 185, 2);

    spr.pushSprite(0, 40);
}

void setup() {
    Serial.begin(115200);
    delay(1000); 
    Serial.println(">>> СТАРТ ПРОШИВКИ РОБОТА (ESP32-C3) [HIGH FPS MODE] <<<");

    TouchInput::init();
    initDisplay(tft, sprite);
    AnimManager::init(tft);
    ImuSensor::init();

    Serial.printf("Стартовая анимация: %s\n", AnimManager::getAnimName(AnimManager::getCurrentAnim()));
    Serial.println("Удерживайте обе кнопки 1.5 сек для входа в сервисное меню.");

    lastFpsPrint = millis();
    lastFrameTime = millis();
}

void loop() {
    // 1. Опрос сенсорных кнопок
    TouchInput::update();

    // Проверка входа в сервисное меню (две кнопки > 1.5 сек)
    if (TouchInput::isBothHold()) {
        inServiceMenu = !inServiceMenu;
        Serial.printf("[SYSTEM] Сервисное меню: %s\n", inServiceMenu ? "ОТКРЫТО" : "ЗАКРЫТО");
    }

    if (inServiceMenu) {
        if (TouchInput::isRightClicked() || TouchInput::isMidClicked()) {
            inServiceMenu = false;
        }
        drawServiceMenu(sprite);
    } else {
        // === ЛОГИКА ПОГЛАЖИВАНИЯ И УСПОКОЕНИЯ (СЕНСОР GPIO 21) ===
        static unsigned long petHoldStart = 0;
        static unsigned long angerCooldownStart = 0; // Таймер остывания после злости

        if (TouchInput::isRightDown()) {
            if (petHoldStart == 0) {
                petHoldStart = millis();
            }

            unsigned long holdTime = millis() - petHoldStart;

            // 1. Прижали палец от 300 мс до 4.5 сек -> Радость (JOY)
            if (holdTime > 300 && holdTime < 4500) {
                if (AnimManager::getCurrentAnim() != AnimType::JOY && 
                    AnimManager::getCurrentAnim() != AnimType::ANGER) {
                    AnimManager::setAnimation(AnimType::JOY);
                    Serial.println("[ROBOT] Робота гладят -> JOY ^^");
                }
            } 
            // 2. Затискали дольше 4.5 секунд -> Перегрузка и срыв в ярость (ANGER)
            else if (holdTime >= 4500) {
                if (AnimManager::getCurrentAnim() != AnimType::ANGER) {
                    AnimManager::setAnimation(AnimType::ANGER);
                    Serial.println("[ROBOT] Перегрузка лаской -> ANGER!");
                }
            }
        } else {
            // Палец убрали
            if (petHoldStart != 0) {
                // Если робот радовался — сразу возвращаем в IDLE
                if (AnimManager::getCurrentAnim() == AnimType::JOY) {
                    AnimManager::setAnimation(AnimType::IDLE);
                    Serial.println("[ROBOT] Поглаживание окончено -> IDLE");
                }
                // Если робот был в ANGER из-за перегрузки — запускаем таймер остывания
                else if (AnimManager::getCurrentAnim() == AnimType::ANGER) {
                    angerCooldownStart = millis();
                    Serial.println("[ROBOT] Палец убран: робот остывает 3 секунды...");
                }
                petHoldStart = 0;
            }

            // Автоматическое успокоение: спустя 3 секунды после того, как убрали палец
            if (angerCooldownStart != 0 && (millis() - angerCooldownStart >= 3000)) {
                if (AnimManager::getCurrentAnim() == AnimType::ANGER) {
                    AnimManager::setAnimation(AnimType::IDLE);
                    Serial.println("[ROBOT] Робот успокоился -> IDLE");
                }
                angerCooldownStart = 0;
            }

            // Обычный короткий клик по-прежнему листает анимации
            if (TouchInput::isRightClicked()) {
                angerCooldownStart = 0; // Сбрасываем таймер при ручном клике
                AnimManager::nextState();
                Serial.printf("[TOUCH GPIO 21] Клик -> %s\n", 
                              AnimManager::getAnimName(AnimManager::getCurrentAnim()));
            }
        }

        // 2. Опрос IMU (параллакс глаз)
        ImuSensor::update();

        // 3. Отрисовка кадра
        AnimManager::runCurrentAnimation(tft, sprite);
    }

    // 4. Расчет FPS и задержка кадра
    frameCount++;
    unsigned long now = millis();
    if (now - lastFpsPrint >= 1000) {
        currentFpsValue = (frameCount * 1000.0f) / (now - lastFpsPrint);
        frameCount = 0;
        lastFpsPrint = now;
    }

    unsigned long renderElapsed = millis() - now;
    if (renderElapsed < TARGET_FRAME_MS) {
        delay(1);
    } else {
        yield();
    }
}
