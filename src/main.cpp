#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "pins.h"
#include "display/display.h"
#include "animation/anim_manager.h"
#include "hardware/touch_input.h"
#include "sensors/imu_sensor.h"

// -------------------------------------------------------------
// 1. Глобальные объекты и переменные
// -------------------------------------------------------------
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

// Переменные для расчета и ограничения FPS
unsigned long lastFpsPrint = 0;
uint32_t frameCount = 0;
float currentFpsValue = 0.0f;
constexpr unsigned long TARGET_FRAME_MS = 16; // Стремимся к 60 FPS

// Флаг сервисного меню
bool inServiceMenu = false;

// -------------------------------------------------------------
// 2. Отрисовка сервисного экрана диагностики
// -------------------------------------------------------------
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

    // Время работы с включения
    unsigned long sec = millis() / 1000;
    unsigned int m = (sec / 60) % 60;
    unsigned int h = (sec / 3600);
    unsigned int s = sec % 60;
    spr.drawString(" Uptime:       " + String(h < 10 ? "0" : "") + h + ":" + (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s, 14, y);
    y += lineH;

    // Свободная память RAM
    uint32_t freeHeap = ESP.getFreeHeap() / 1024;
    uint32_t maxBlock = ESP.getMaxAllocHeap() / 1024;
    spr.drawString(" Free RAM:     " + String(freeHeap) + " KB (blk " + String(maxBlock) + "k)", 14, y);
    y += lineH;

    // Частота кадров
    spr.drawString(" Render FPS:   " + String(currentFpsValue, 1), 14, y);
    y += lineH;

    // Статус датчика движения
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

    // Углы параллакса
    spr.drawString(" Parallax:     X:" + String(ImuSensor::getParallaxX(), 0) + "  Y:" + String(ImuSensor::getParallaxY(), 0), 14, y);
    y += lineH;

    // Текущая анимация
    spr.drawString(" Current Anim: " + String(AnimManager::getAnimName(AnimManager::getCurrentAnim())), 14, y);
    y += lineH + 6;

    // Подсказка выхода
    spr.setTextDatum(BC_DATUM);
    spr.setTextColor(TFT_CYAN, TFT_BLACK);
    spr.drawString("Tap any button to EXIT", 120, 185, 2);

    spr.pushSprite(0, 40);
}

// -------------------------------------------------------------
// 3. Инициализация при включении (setup)
// -------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(1000); 
    Serial.println(">>> СТАРТ РОБОТА: ПОГЛАЖИВАНИЕ + ЩЕКОТКА + СЕРВИСНОЕ МЕНЮ <<<");

    TouchInput::init();
    initDisplay(tft, sprite);
    AnimManager::init(tft);
    ImuSensor::init();

    Serial.printf("Стартовая эмоция: %s\n", AnimManager::getAnimName(AnimManager::getCurrentAnim()));
    Serial.println("Управление:");
    Serial.println(" - Быстрое чередование кнопок -> Щекотка (LAUGH)");
    Serial.println(" - Удержание правой кнопки -> Поглаживание (JOY)");
    Serial.println(" - Зажатие обеих кнопок 1.5 сек -> Сервисное меню");
    Serial.println(" - Одиночный клик -> Переключение следующей эмоции");

    lastFpsPrint = millis();
}

// -------------------------------------------------------------
// 4. Главный рабочий цикл (loop)
// -------------------------------------------------------------
void loop() {
    // 1. Опрашиваем сенсоры кнопок
    TouchInput::update();

    // 2. Проверяем вход в сервисное меню (обе кнопки зажаты 1.5 сек)
    if (TouchInput::isBothHold()) {
        inServiceMenu = !inServiceMenu;
        Serial.printf("[SYSTEM] Сервисное меню: %s\n", inServiceMenu ? "ОТКРЫТО" : "ЗАКРЫТО");
    }

    // Если сейчас открыто сервисное меню:
    if (inServiceMenu) {
        // Одиночный клик любой кнопки закрывает меню
        if (TouchInput::isRightClicked() || TouchInput::isMidClicked()) {
            inServiceMenu = false;
            Serial.println("[SYSTEM] Выход из сервисного меню к роботу");
        }
        drawServiceMenu(sprite);
    } 
    // Если меню закрыто — работает живой робот:
    else {
        static unsigned long petHoldStart = 0;       // Засекает время поглаживания
        static unsigned long angerCooldownStart = 0; // Время остывания после злости
        static unsigned long tickleUntilTime = 0;    // До какого момента робот смеётся от щекотки

        // -----------------------------------------------------
        // А) ЩЕКОТКА (быстрое поочередное нажатие кнопок)
        // -----------------------------------------------------
        if (TouchInput::isTickleDetected()) {
            tickleUntilTime = millis() + 1800; // Робот хохочет 1.8 секунды после последнего тапа
            if (AnimManager::getCurrentAnim() != AnimType::LAUGH && 
                AnimManager::getCurrentAnim() != AnimType::ANGER) {
                AnimManager::setAnimation(AnimType::LAUGH);
                Serial.println("[ROBOT] Ахаха, щекотно! -> Включен LAUGH XD");
            }
        }

        // Если время щекотки истекло — возвращаемся обратно в спокойный IDLE
        if (tickleUntilTime != 0 && millis() >= tickleUntilTime) {
            tickleUntilTime = 0;
            if (AnimManager::getCurrentAnim() == AnimType::LAUGH) {
                AnimManager::setAnimation(AnimType::IDLE);
                Serial.println("[ROBOT] Щекотка окончена -> Возврат в IDLE");
            }
        }

        // -----------------------------------------------------
        // Б) ПОГЛАЖИВАНИЕ (удержание правой кнопки GPIO 21)
        // -----------------------------------------------------
        // Работает только если робота сейчас не щекочут
        if (TouchInput::isRightDown() && tickleUntilTime == 0) {
            if (petHoldStart == 0) {
                petHoldStart = millis(); // Засекаем старт касания
            }

            unsigned long holdTime = millis() - petHoldStart;

            // Касание от 300 мс до 4.5 сек -> Робот радуется (JOY)
            if (holdTime > 300 && holdTime < 4500) {
                if (AnimManager::getCurrentAnim() != AnimType::JOY && 
                    AnimManager::getCurrentAnim() != AnimType::ANGER) {
                    AnimManager::setAnimation(AnimType::JOY);
                    Serial.println("[ROBOT] Робота гладят -> Включен JOY ^^");
                }
            } 
            // Передержали ласку дольше 4.5 сек -> Срыв в ярость (ANGER)
            else if (holdTime >= 4500) {
                if (AnimManager::getCurrentAnim() != AnimType::ANGER) {
                    AnimManager::setAnimation(AnimType::ANGER);
                    Serial.println("[ROBOT] Перегрузка лаской -> Срыв в ANGER!");
                }
            }
        } 
        // Палец с правой кнопки убрали:
        else {
            if (petHoldStart != 0) {
                // Если робот просто радовался — сразу возвращаем в IDLE
                if (AnimManager::getCurrentAnim() == AnimType::JOY && tickleUntilTime == 0) {
                    AnimManager::setAnimation(AnimType::IDLE);
                    Serial.println("[ROBOT] Поглаживание окончено -> IDLE");
                } 
                // Если робот был зол — запускаем таймер остывания на 3 секунды
                else if (AnimManager::getCurrentAnim() == AnimType::ANGER) {
                    angerCooldownStart = millis();
                    Serial.println("[ROBOT] Палец убран: робот злится еще 3 секунды...");
                }
                petHoldStart = 0;
            }

            // Автоматическое успокоение после злости (через 3 секунды)
            if (angerCooldownStart != 0 && (millis() - angerCooldownStart >= 3000)) {
                if (AnimManager::getCurrentAnim() == AnimType::ANGER) {
                    AnimManager::setAnimation(AnimType::IDLE);
                    Serial.println("[ROBOT] Робот остыл и успокоился -> IDLE");
                }
                angerCooldownStart = 0;
            }

            // Короткий клик правой кнопки (GPIO 21) листает анимацию для тестов
            if (TouchInput::isRightClicked() && tickleUntilTime == 0) {
                angerCooldownStart = 0;
                AnimManager::nextState();
                Serial.printf("[TOUCH GPIO 21] Клик -> %s\n", 
                              AnimManager::getAnimName(AnimManager::getCurrentAnim()));
            }
        }

        // -----------------------------------------------------
        // В) ЦЕНТРАЛЬНАЯ КНОПКА (GPIO 20)
        // -----------------------------------------------------
        // Короткий клик центральной кнопки также листает анимации
        if (TouchInput::isMidClicked() && tickleUntilTime == 0) {
            AnimManager::nextState();
            Serial.printf("[TOUCH GPIO 20] Клик -> %s\n", 
                          AnimManager::getAnimName(AnimManager::getCurrentAnim()));
        }

        // -----------------------------------------------------
        // Г) ДАТЧИК IMU И ОТРИСОВКА
        // -----------------------------------------------------
        // Считываем параллакс от наклона
        ImuSensor::update();

        // Рисуем текущую активную анимацию
        AnimManager::runCurrentAnimation(tft, sprite);
    }

    // ---------------------------------------------------------
    // 5. Замер FPS и ограничение кадров (до 60 FPS)
    // ---------------------------------------------------------
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
