#include <Arduino.h>
#include <TFT_eSPI.h>
#include "pins.h"
#include "animation/anim_manager.h"
#include "animation/anim_wink.h"
#include "animation/anim_lookat.h"
#include "animation/anim_disdain.h"
#include "animation/anim_melt.h"
#include "animation/face_state.h"
#include "hardware/touch_input.h"
#include "sensors/imu_sensor.h"
#include "sensors/tap_detector.h"
#include "sensors/orientation.h"

// ============================================================================
//  Глобальные объекты (единственный буфер кадра 240x200 на весь проект)
// ============================================================================
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

// ============================================================================
//  Настройки поведения и таймингов
// ============================================================================
const uint32_t TARGET_FRAME_MS      = 16;     // ~60 FPS
const uint32_t SERVICE_FRAME_MS     = 125;    // ~8 FPS в сервисном меню

const uint32_t LAUGH_DURATION_MS    = 1800;   // хохот от щекотки
const uint32_t JOY_HOLD_MS          = 300;    // порог удержания для непрерывного поглаживания
const uint32_t JOY_TAP_MS           = 2000;   // длительность радости от быстрого тапа
const uint32_t ANGER_HOLD_MS        = 4500;   // порог срыва в ярость при долгом зажатии
const uint32_t ANGER_RELEASE_MS     = 3000;   // ярость держится ещё 3 с после отпускания
const uint32_t LOOK_DURATION_MS     = 1350;   // взгляд в точку удара
const uint32_t DISDAIN_DURATION_MS  = 2800;   // презрение при 3 стуках
const uint32_t SWEAR_DURATION_MS    = 2600;   // ругань при 5 стуках

// «Вверх тормашками»
const uint32_t UPSIDE_DOWN_DELAY_MS = 1800;   // столько непрерывно вверх ногами -> MELT
const uint32_t MELT_MIN_SHOW_MS     = 600;    // минимальное время показа до восстановления

// ============================================================================
//  Состояние главного цикла
// ============================================================================
bool     inServiceMenu   = false;
bool     bothHoldLatch   = false;
uint32_t lastFrameMs     = 0;
uint32_t lastServiceMs   = 0;

uint32_t autoReturnAt    = 0;     // таймер авто-возврата в IDLE
uint32_t rightDownStart  = 0;     // момент нажатия правой кнопки (0 — не нажата)
bool     angerLatched    = false;
uint32_t angerReleaseAt  = 0;
uint32_t meltStartMs     = 0;

// FPS
uint32_t fpsCounter = 0, fpsLastMs = 0, fpsValue = 0;

// ============================================================================
//  Вспомогательные функции переключения состояний
// ============================================================================
void goIdle() {
    AnimManager::setAnimation(AnimType::IDLE);
    autoReturnAt   = 0;
    angerLatched   = false;
    angerReleaseAt = 0;
}

void startTimed(AnimType anim, uint32_t durationMs, uint32_t now) {
    AnimManager::setAnimation(anim);
    autoReturnAt = now + durationMs;
}

// ============================================================================
//  «Вверх тормашками» (Orientation -> MELT)
// ============================================================================
void handleUpsideDown(uint32_t now) {
    if (!Orientation::isReady()) return;

    AnimType cur = AnimManager::getCurrentAnim();
    bool down = Orientation::isUpsideDown();

    if (cur != AnimType::MELT) {
        // Перевёрнут непрерывно дольше порога -> стекаем в лужу
        if (down && Orientation::getUpsideDownMs() >= UPSIDE_DOWN_DELAY_MS) {
            AnimMelt::setSideTilt(Orientation::getSideTilt());
            AnimManager::setAnimation(AnimType::MELT);
            meltStartMs    = now;
            autoReturnAt   = 0;
            angerLatched   = false;
            angerReleaseAt = 0;
            rightDownStart = 0;
        }
        return;
    }

    // Если уже в луже — она следует за боковым наклоном
    AnimMelt::setSideTilt(Orientation::getSideTilt());

    // Вернули в нормальное положение -> собираемся обратно
    if (!down && !AnimMelt::isRecovering() && !AnimMelt::isFinished()
        && (now - meltStartMs >= MELT_MIN_SHOW_MS)) {
        AnimMelt::startRecovery();
    }

    // Восстановление завершено -> спокойный режим IDLE
    if (AnimMelt::isFinished()) {
        goIdle();
    }
}

// ============================================================================
//  Жесты стука по корпусу (TapDetector: 1, 3, 5 стуков)
// ============================================================================
void handleTapGestures(uint32_t now) {
    if (!TapDetector::consumeTap()) return;

    AnimType cur = AnimManager::getCurrentAnim();
    // Не прерываем активные сюжетные состояния
    if (cur == AnimType::ANGER || cur == AnimType::JOY ||
        cur == AnimType::LAUGH || cur == AnimType::MELT) return;

    int combo = TapDetector::getComboCount();
    float dirX = TapDetector::getTapDirX();
    float dirY = TapDetector::getTapDirY();

    // 5 стуков: ругань с плашкой цензуры
    if (combo >= 5) {
        startTimed(AnimType::SWEAR, SWEAR_DURATION_MS, now);
        return;
    }

    // Если уже в презрении — повторный стук вызывает недоуменное покачивание
    if (cur == AnimType::DISDAIN) {
        AnimDisdain::setTapSide(dirX);
        AnimDisdain::nudge();
        return;
    }

    // 3 стука: презрение со стороны удара
    if (combo >= 3) {
        AnimDisdain::setTapSide(dirX);
        startTimed(AnimType::DISDAIN, DISDAIN_DURATION_MS, now);
        return;
    }

    // 1-2 стука: удивлённый взгляд в точку удара
    AnimLookAt::setTarget(dirX, dirY);
    startTimed(AnimType::LOOK, LOOK_DURATION_MS, now);
}

// ============================================================================
//  Сенсорные кнопки
// ============================================================================
void handleTouch(uint32_t now) {
    AnimType cur = AnimManager::getCurrentAnim();

    // 1. Щекотка
    if (TouchInput::isTickleDetected() && cur != AnimType::ANGER) {
        startTimed(AnimType::LAUGH, LAUGH_DURATION_MS, now);
        TouchInput::isRightClicked(); // поглощаем возможный клик
        return;
    }

    // 2. Удержание правой кнопки: JOY -> ANGER
    if (TouchInput::isRightDown()) {
        if (rightDownStart == 0) rightDownStart = now;
        uint32_t held = now - rightDownStart;

        if (held >= ANGER_HOLD_MS && !angerLatched) {
            angerLatched = true;
            angerReleaseAt = 0;
            autoReturnAt = 0;
            AnimManager::setAnimation(AnimType::ANGER);
            cur = AnimType::ANGER;
        } else if (held >= JOY_HOLD_MS && !angerLatched
                   && cur != AnimType::JOY && cur != AnimType::LAUGH) {
            autoReturnAt = 0;
            AnimManager::setAnimation(AnimType::JOY);
            cur = AnimType::JOY;
        }
    } else {
        if (rightDownStart != 0) {
            uint32_t held = now - rightDownStart;
            rightDownStart = 0;

            if (angerLatched && cur == AnimType::ANGER) {
                angerReleaseAt = now; // ярость держится ещё ANGER_RELEASE_MS
            } else if (cur == AnimType::JOY) {
                // Если держали дольше порога — гасим сразу при отпускании
                if (held >= JOY_HOLD_MS) {
                    goIdle();
                    cur = AnimType::IDLE;
                }
            }
        }
    }

    // Завершение ярости по таймеру после отпускания кнопки
    if (angerLatched && angerReleaseAt != 0 && (now - angerReleaseAt >= ANGER_RELEASE_MS)) {
        goIdle();
        cur = AnimType::IDLE;
    }

    // 3. Быстрый одиночный клик правой кнопки — короткая радость на 2 с
    if (TouchInput::isRightClicked() && rightDownStart == 0) {
        if (cur == AnimType::IDLE) {
            startTimed(AnimType::JOY, JOY_TAP_MS, now);
            cur = AnimType::JOY;
        }
    }

    // 4. Двойной клик правой кнопки — безопасный отладочный перебор анимаций
    if (TouchInput::isRightDoubleClicked() && rightDownStart == 0) {
        AnimManager::nextState();
        autoReturnAt = 0;
        cur = AnimManager::getCurrentAnim();
        Serial.printf("[DEBUG] nextState -> %s\n", AnimManager::getAnimName(cur));
    }

    // 5. Клик центральной кнопки: подмигивание
    if (TouchInput::isMidClicked() && cur == AnimType::IDLE) {
        AnimManager::setAnimation(AnimType::WINK);
        autoReturnAt = 0;
        cur = AnimType::WINK;
    }
    if (cur == AnimType::WINK && AnimWink::isFinished()) {
        goIdle();
        cur = AnimType::IDLE;
    }
}

// ============================================================================
//  Сервисный дашборд (рисуется в тот же sprite)
// ============================================================================
void drawServiceMenu(uint32_t now) {
    char buf[48];
    sprite.fillSprite(TFT_BLACK);
    sprite.setTextDatum(TL_DATUM);

    sprite.setTextColor(TFT_CYAN, TFT_BLACK);
    sprite.setTextSize(2);
    sprite.drawString("SERVICE", 6, 4, 1);

    sprite.setTextSize(1);
    sprite.setTextColor(TFT_WHITE, TFT_BLACK);
    int y = 28;
    const int dy = 12;

    snprintf(buf, sizeof(buf), "Uptime: %lu s", (unsigned long)(now / 1000));
    sprite.drawString(buf, 6, y, 1); y += dy;

    snprintf(buf, sizeof(buf), "Heap: %lu B", (unsigned long)ESP.getFreeHeap());
    sprite.drawString(buf, 6, y, 1); y += dy;

    snprintf(buf, sizeof(buf), "FPS: %lu", (unsigned long)fpsValue);
    sprite.drawString(buf, 6, y, 1); y += dy;

    snprintf(buf, sizeof(buf), "IMU: %s", ImuSensor::isReady() ? "ONLINE" : "OFFLINE");
    sprite.drawString(buf, 6, y, 1); y += dy;

    snprintf(buf, sizeof(buf), "Parallax X:%6.2f Y:%6.2f",
             ImuSensor::getParallaxX(), ImuSensor::getParallaxY());
    sprite.drawString(buf, 6, y, 1); y += dy;

    snprintf(buf, sizeof(buf), "Tap jerk: %5.2f g", TapDetector::getLastJerk());
    sprite.drawString(buf, 6, y, 1); y += dy;

    // --- Ориентация ---
    sprite.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
    float gx, gy, gz;
    Orientation::getGravity(gx, gy, gz);
    snprintf(buf, sizeof(buf), "G: %5.2f %5.2f %5.2f", gx, gy, gz);
    sprite.drawString(buf, 6, y, 1); y += dy;

    snprintf(buf, sizeof(buf), "UpDot: %5.2f  Side: %5.2f",
             Orientation::getUpDot(), Orientation::getSideTilt());
    sprite.drawString(buf, 6, y, 1); y += dy;

    snprintf(buf, sizeof(buf), "UpsideDown: %s  %lu ms",
             Orientation::isUpsideDown() ? "YES" : "no",
             (unsigned long)Orientation::getUpsideDownMs());
    sprite.drawString(buf, 6, y, 1); y += dy;

    sprite.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(buf, sizeof(buf), "Anim: %s", AnimManager::getAnimName(AnimManager::getCurrentAnim()));
    sprite.drawString(buf, 6, y, 1); y += dy;

    sprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sprite.drawString("Hold both buttons to exit", 6, 186, 1);

    sprite.pushSprite(0, 40);
}

// ============================================================================
//  setup / loop
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[BOOT] AIBI keychain robot");

    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, LOW);

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    sprite.setColorDepth(16);
    sprite.createSprite(240, 200);
    sprite.fillSprite(TFT_BLACK);

    TouchInput::init();
    ImuSensor::init();
    TapDetector::init();
    Orientation::init(); // строго после ImuSensor::init()

    AnimManager::init(tft);
    AnimManager::setAnimation(AnimType::IDLE);

    digitalWrite(TFT_BL_PIN, HIGH);

    lastFrameMs = millis();
    fpsLastMs   = lastFrameMs;
    Serial.println("[BOOT] ready");
}

void loop() {
    uint32_t now = millis();

    // 1. Опрос сенсоров
    TouchInput::update();
    ImuSensor::update();
    TapDetector::update();
    Orientation::update();

    // 2. Сервисное меню (удержание обеих кнопок > 1.5 с)
    bool both = TouchInput::isBothHold();
    if (both && !bothHoldLatch) {
        bothHoldLatch = true;
        inServiceMenu = !inServiceMenu;
        if (!inServiceMenu) goIdle();
        Serial.printf("[MODE] service menu %s\n", inServiceMenu ? "ON" : "OFF");
    }
    if (!both) bothHoldLatch = false;

    if (inServiceMenu) {
        if (now - lastServiceMs >= SERVICE_FRAME_MS) {
            lastServiceMs = now;
            drawServiceMenu(now);
            fpsCounter++;
        }
    } else {
        // 3. Рабочий режим
        handleUpsideDown(now);

        bool melting = (AnimManager::getCurrentAnim() == AnimType::MELT);
        if (!melting) {
            handleTapGestures(now);
            handleTouch(now);

            // Авто-возврат по таймеру (JOY / LAUGH / LOOK / DISDAIN / SWEAR)
            if (autoReturnAt != 0 && now >= autoReturnAt) {
                AnimType cur = AnimManager::getCurrentAnim();
                if (cur == AnimType::JOY || cur == AnimType::LAUGH ||
                    cur == AnimType::LOOK || cur == AnimType::DISDAIN ||
                    cur == AnimType::SWEAR) {
                    goIdle();
                }
                autoReturnAt = 0;
            }
        }

        // 4. Отрисовка кадра (~60 FPS)
        if (now - lastFrameMs >= TARGET_FRAME_MS) {
            lastFrameMs = now;
            AnimManager::runCurrentAnimation(tft, sprite);
            fpsCounter++;
        }
    }

    // Расчет FPS раз в секунду
    if (now - fpsLastMs >= 1000) {
        fpsLastMs = now;
        fpsValue = fpsCounter;
        fpsCounter = 0;
    }

    vTaskDelay(1);
}
