#include <Arduino.h>
#include <TFT_eSPI.h>
#include "pins.h"
#include "animation/anim_manager.h"
#include "animation/anim_wink.h"
#include "animation/anim_disdain.h"
#include "animation/anim_lookat.h"
#include "hardware/touch_input.h"
#include "sensors/imu_sensor.h"
#include "sensors/tap_detector.h"

// ----------------------------------------------------------------------------
// Дисплей и единый экранный буфер (240x200, 16-bit RGB565)
// ----------------------------------------------------------------------------
TFT_eSPI    tft    = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

namespace {
    // ------------------------------------------------------------------------
    // Временные константы и тайминги
    // ------------------------------------------------------------------------
    const uint32_t TARGET_FRAME_MS   = 16;     // ~60 FPS для анимаций
    const uint32_t SERVICE_FRAME_MS  = 125;    // ~8 FPS для сервисного дашборда

    const uint32_t JOY_START_MS      = 300;    // порог удержания для радости (поглаживание)
    const uint32_t ANGER_START_MS    = 4500;   // порог удержания для срыва в ярость
    const uint32_t ANGER_HOLD_MS     = 3000;   // ярость после отпускания кнопки
    const uint32_t LAUGH_DURATION_MS = 1800;   // длительность смеха от щекотки
    const uint32_t SWEAR_DURATION_MS = 2600;   // длительность ругани (5 стуков)
    const uint32_t CLICK_IGNORE_MS   = 500;    // окно подавления ложного клика после жеста
    const uint32_t DEBUG_ANIM_MS     = 3000;   // авто-возврат в IDLE при ручном переборе реакций

    // ------------------------------------------------------------------------
    // Состояние системы и таймеры
    // ------------------------------------------------------------------------
    bool     inServiceMenu         = false;
    bool     bothHoldLatched       = false;
    uint32_t lastFrameMs           = 0;
    uint32_t lastServiceMs         = 0;

    bool     rightWasDown          = false;
    uint32_t rightPressStart       = 0;
    bool     joyActivated          = false;
    bool     angerActive           = false;

    uint32_t angerHoldUntil        = 0;
    uint32_t laughUntil            = 0;
    uint32_t swearUntil            = 0;
    uint32_t debugAnimUntil        = 0;
    uint32_t ignoreRightClickUntil = 0;

    uint32_t fpsCounter            = 0;
    uint32_t fpsLastMs             = 0;
    uint32_t fpsValue              = 0;

    // Вспомогательная проверка истечения таймера
    bool timerExpired(uint32_t &until, uint32_t now) {
        if (until != 0 && (int32_t)(now - until) >= 0) {
            until = 0;
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------------------
    // Обработка стуков по корпусу (IMU Jerk Detector)
    // ------------------------------------------------------------------------
    void handleTaps(uint32_t now) {
        if (!TapDetector::consumeTap()) return;

        AnimType cur = AnimManager::getCurrentAnim();

        // Во время активных эмоциональных сцен стуки не прерывают действие
        bool busy = (cur == AnimType::JOY   || cur == AnimType::ANGER ||
                     cur == AnimType::LAUGH || cur == AnimType::SWEAR ||
                     cur == AnimType::WINK  || cur == AnimType::DIZZY ||
                     cur == AnimType::SAD);
        if (busy) return;

        int   combo = TapDetector::getComboCount();
        float dirX  = TapDetector::getTapDirX();
        float dirY  = TapDetector::getTapDirY();

        Serial.printf("[TAP] #%d dir=(%.2f, %.2f) jerk=%.2fg\n",
                      combo, dirX, dirY, TapDetector::getLastJerk());

        // 5 стуков подряд -> Срыв в ругань (SWEAR)
        if (combo >= 5) {
            AnimManager::setAnimation(AnimType::SWEAR);
            swearUntil = now + SWEAR_DURATION_MS;
            debugAnimUntil = 0;
            return;
        }

        // Если робот уже презирает -> повторный стук вызывает недоуменное покачивание
        if (cur == AnimType::DISDAIN) {
            AnimDisdain::setTapSide(dirX);
            AnimDisdain::nudge();
            return;
        }

        // 3 стука подряд -> Анимация презрения (DISDAIN)
        if (combo >= 3) {
            AnimDisdain::setTapSide(dirX);
            AnimManager::setAnimation(AnimType::DISDAIN);
            debugAnimUntil = 0;
            return;
        }

        // 1-2 стука -> Взгляд в точку удара с сильным удивлением (LOOK)
        AnimLookAt::setTarget(dirX, dirY);
        AnimManager::setAnimation(AnimType::LOOK);
        debugAnimUntil = 0;
    }

    // ------------------------------------------------------------------------
    // Обработка сенсорных кнопок и жестов
    // ------------------------------------------------------------------------
    void handleInputs(uint32_t now) {
        // 1. Щекотка (поочередное быстрое касание обеих кнопок)
        if (TouchInput::isTickleDetected()) {
            if (AnimManager::getCurrentAnim() != AnimType::LAUGH) {
                AnimManager::setAnimation(AnimType::LAUGH);
            }
            laughUntil     = now + LAUGH_DURATION_MS;
            debugAnimUntil = 0;
        }

        // 2. Правая кнопка: поглаживание (JOY) -> ярость (ANGER)
        bool rightDown = TouchInput::isRightDown();
        if (rightDown) {
            if (!rightWasDown) {
                rightWasDown    = true;
                rightPressStart = now;
                joyActivated    = false;
            }
            uint32_t held = now - rightPressStart;
            AnimType cur  = AnimManager::getCurrentAnim();

            if (held >= ANGER_START_MS) {
                if (cur != AnimType::ANGER) {
                    AnimManager::setAnimation(AnimType::ANGER);
                }
                angerActive    = true;
                debugAnimUntil = 0;
            } else if (held >= JOY_START_MS) {
                joyActivated = true;
                if (cur != AnimType::JOY && cur != AnimType::LAUGH && !angerActive) {
                    AnimManager::setAnimation(AnimType::JOY);
                }
                debugAnimUntil = 0;
            }
        } else if (rightWasDown) {
            // Отпускание правой кнопки
            rightWasDown = false;

            if (angerActive) {
                angerActive           = false;
                angerHoldUntil        = now + ANGER_HOLD_MS;
                ignoreRightClickUntil = now + CLICK_IGNORE_MS; // глушим случайный клик после долгого удержания
            } else if (joyActivated) {
                if (AnimManager::getCurrentAnim() == AnimType::JOY) {
                    AnimManager::setAnimation(AnimType::IDLE);
                }
                ignoreRightClickUntil = now + CLICK_IGNORE_MS; // глушим клик после поглаживания, чтобы JOY не залипала
            }
            joyActivated = false;
        }

        // 3. Центральная кнопка: подмигивание (WINK)
        if (TouchInput::isMidClicked()) {
            if (!angerActive && AnimManager::getCurrentAnim() != AnimType::ANGER) {
                AnimManager::setAnimation(AnimType::WINK);
                debugAnimUntil = 0;
            }
        }

        // 4. Таймеры автоматического возврата в IDLE
        if (timerExpired(angerHoldUntil, now)) {
            if (AnimManager::getCurrentAnim() == AnimType::ANGER) {
                AnimManager::setAnimation(AnimType::IDLE);
            }
        }
        if (timerExpired(laughUntil, now)) {
            if (AnimManager::getCurrentAnim() == AnimType::LAUGH) {
                AnimManager::setAnimation(AnimType::IDLE);
            }
        }
        if (timerExpired(swearUntil, now)) {
            if (AnimManager::getCurrentAnim() == AnimType::SWEAR) {
                AnimManager::setAnimation(AnimType::IDLE);
            }
        }
        if (timerExpired(debugAnimUntil, now)) {
            AnimType cur = AnimManager::getCurrentAnim();
            if (cur == AnimType::JOY || cur == AnimType::LAUGH || cur == AnimType::WINK) {
                AnimManager::setAnimation(AnimType::IDLE);
            }
        }

        // Авто-возврат по завершению сюжетных анимаций
        AnimType curAnim = AnimManager::getCurrentAnim();
        if (curAnim == AnimType::WINK && AnimWink::isFinished()) {
            AnimManager::setAnimation(AnimType::IDLE);
        } else if (curAnim == AnimType::DISDAIN && AnimDisdain::isFinished()) {
            AnimManager::setAnimation(AnimType::IDLE);
        } else if (curAnim == AnimType::LOOK && AnimLookAt::isFinished()) {
            AnimManager::setAnimation(AnimType::IDLE);
        }

        // 5. Отладочный перебор анимаций (короткий одиночный клик правой кнопкой)
        bool rightClicked = TouchInput::isRightClicked();
        bool clickAllowed = (int32_t)(now - ignoreRightClickUntil) >= 0;

        if (rightClicked && clickAllowed && !rightDown && !angerActive) {
            AnimManager::nextState();
            AnimType cur = AnimManager::getCurrentAnim();
            Serial.printf("[DEBUG] nextState -> %s\n", AnimManager::getAnimName(cur));

            // Для коротких эмоциональных реакций ставим таймер, чтобы они не зависали
            if (cur == AnimType::JOY || cur == AnimType::LAUGH || cur == AnimType::WINK) {
                debugAnimUntil = now + DEBUG_ANIM_MS;
            } else {
                debugAnimUntil = 0;
            }
        }
    }

    // ------------------------------------------------------------------------
    // Сервисный дашборд
    // ------------------------------------------------------------------------
    void drawServiceDashboard(uint32_t now) {
        char line[48];

        sprite.fillSprite(TFT_BLACK);
        sprite.setTextDatum(TL_DATUM);
        sprite.setTextFont(2);

        sprite.setTextColor(TFT_CYAN, TFT_BLACK);
        sprite.drawString("SERVICE MODE (hold both to exit)", 8, 4);

        sprite.setTextColor(TFT_GREEN, TFT_BLACK);
        uint32_t sec = now / 1000;
        snprintf(line, sizeof(line), "Uptime: %02lu:%02lu:%02lu",
                 (unsigned long)(sec / 3600), (unsigned long)((sec / 60) % 60), (unsigned long)(sec % 60));
        sprite.drawString(line, 8, 26);

        snprintf(line, sizeof(line), "Free heap: %lu B", (unsigned long)ESP.getFreeHeap());
        sprite.drawString(line, 8, 44);

        snprintf(line, sizeof(line), "FPS: %lu", (unsigned long)fpsValue);
        sprite.drawString(line, 8, 62);

        snprintf(line, sizeof(line), "IMU: %s", ImuSensor::isReady() ? "ONLINE" : "OFFLINE");
        sprite.drawString(line, 8, 80);

        snprintf(line, sizeof(line), "Parallax X: %.1f  Y: %.1f",
                 ImuSensor::getParallaxX(), ImuSensor::getParallaxY());
        sprite.drawString(line, 8, 98);

        snprintf(line, sizeof(line), "Tap: %s  combo: %d",
                 TapDetector::isReady() ? "ONLINE" : "OFFLINE", TapDetector::getComboCount());
        sprite.drawString(line, 8, 116);

        snprintf(line, sizeof(line), "Tap jerk: %.2fg",
                 TapDetector::getLastJerk());
        sprite.drawString(line, 8, 134);

        snprintf(line, sizeof(line), "Anim: %s", AnimManager::getAnimName(AnimManager::getCurrentAnim()));
        sprite.drawString(line, 8, 152);

        sprite.pushSprite(0, 40);
    }

    void countFrame(uint32_t now) {
        fpsCounter++;
        if (now - fpsLastMs >= 1000) {
            fpsValue   = fpsCounter;
            fpsCounter = 0;
            fpsLastMs  = now;
        }
    }
} // anonymous namespace

// ----------------------------------------------------------------------------
// Инициализация
// ----------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(50);

    // Включаем подсветку экрана
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

    // Инициализация ST7789
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    // Выделение единственного буфера 240x200
    sprite.setColorDepth(16);
    sprite.createSprite(240, 200);
    sprite.fillSprite(TFT_BLACK);

    // Сенсоры и аппаратная часть
    TouchInput::init();
    ImuSensor::init();
    TapDetector::init(); // Обязательно после ImuSensor::init()

    // Диспетчер анимаций
    AnimManager::init(tft);
    AnimManager::setAnimation(AnimType::IDLE);

    lastFrameMs = millis();
    fpsLastMs   = lastFrameMs;
    Serial.println("[MAIN] Robot ready");
}

// ----------------------------------------------------------------------------
// Главный цикл
// ----------------------------------------------------------------------------
void loop() {
    uint32_t now = millis();

    // 1. Опрос сенсоров
    TouchInput::update();
    ImuSensor::update();
    TapDetector::update();

    // 2. Вход/выход из сервисного меню (зажаты обе кнопки > 1.5 с)
    if (TouchInput::isBothHold()) {
        if (!bothHoldLatched) {
            bothHoldLatched = true;
            inServiceMenu   = !inServiceMenu;
            if (!inServiceMenu) {
                AnimManager::setAnimation(AnimType::IDLE);
            }
            Serial.printf("[MAIN] service menu: %s\n", inServiceMenu ? "ON" : "OFF");
        }
    } else {
        bothHoldLatched = false;
    }

    // Отрисовка сервисного экрана
    if (inServiceMenu) {
        if (now - lastServiceMs >= SERVICE_FRAME_MS) {
            lastServiceMs = now;
            drawServiceDashboard(now);
            countFrame(now);
        }
        vTaskDelay(1);
        return;
    }

    // 3. Обработка стуков и жестов
    handleTaps(now);
    handleInputs(now);

    // 4. Отрисовка кадра анимации (~60 FPS)
    if (now - lastFrameMs >= TARGET_FRAME_MS) {
        lastFrameMs = now;
        AnimManager::runCurrentAnimation(tft, sprite);
        countFrame(now);
    }

    // Системная пауза для фоновых задач RTOS и watchdog
    vTaskDelay(1);
}
