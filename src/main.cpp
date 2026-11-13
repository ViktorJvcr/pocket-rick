#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "anim_intro.h"


#include "pins.h"
#include "hardware/touch_input.h"
#include "sensors/imu_sensor.h"
#include "sensors/tap_detector.h"
#include "sensors/orientation.h"
#include "sensors/motion_detector.h"

#include "animation/anim_manager.h"
#include "animation/anim_wink.h"
#include "animation/anim_lookat.h"
#include "animation/anim_disdain.h"
#include "animation/anim_melt.h"
#include "animation/anim_sleep.h"
#include "animation/face_state.h"

// ============================================================================
//  Глобальные объекты экрана (единственный буфер 240x200 на весь проект)
// ============================================================================
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

namespace {
    // ------------------------------------------------------------------------
    // Тайминги и пороги
    // ------------------------------------------------------------------------
    const uint32_t TARGET_FRAME_MS        = 16;     // ~60 FPS
    const uint32_t SERVICE_FRAME_MS       = 125;    // ~8 FPS в сервисном дашборде

    const uint32_t LAUGH_MS               = 1800;   // длительность смеха от щекотки
    const uint32_t JOY_TAP_MS             = 2000;   // радость от короткого тапа
    const uint32_t JOY_HOLD_MIN_MS        = 300;    // порог удержания для поглаживания
    const uint32_t ANGER_HOLD_MS          = 4500;   // порог зажатия кнопки для ярости
    const uint32_t ANGER_AFTER_RELEASE_MS = 3000;   // ярость после отпускания / пробуждения
    const uint32_t LOOK_MS                = 1350;   // взгляд в точку удара
    const uint32_t DISDAIN_MS             = 2800;   // презрение при 3 стуках
    const uint32_t SWEAR_MS               = 2600;   // ругань при 5 стуках
    const uint32_t DIZZY_MS               = 6200;   // укачивание/головокружение от тряски (6.2 с)
    const uint32_t WINK_MS                = 1800;   // длительность подмигивания (1.8 с)
    const uint32_t MELT_DELAY_MS          = 1800;   // удержание вверх ногами до стекания
    const uint32_t MELT_MIN_SHOW_MS       = 600;    // минимальное время показа лужи

    // ------------------------------------------------------------------------
    // Переменные состояния
    // ------------------------------------------------------------------------
    bool     inServiceMenu         = false;
    bool     bothHoldLatch         = false;
    uint32_t lastFrameTime         = 0;
    uint32_t lastServiceDraw       = 0;

   const uint32_t INACTIVITY_SLEEP_MS    = 60000;  // Время бездействия до сна (60 сек, настройте под себя)

    uint32_t autoIdleAt            = 0;      // таймер возврата в IDLE (0 = выключен)
    uint32_t rightDownStart        = 0;      // момент нажатия правой кнопки (0 = отпущена)
    bool     angerLatched          = false;  // срыв в ярость зафиксирован
    uint32_t meltStartMs           = 0;
    uint32_t meltCooldownUntil     = 0;      // защита от сна во время и после сборки лужи
    uint32_t lastActivityTime      = 0;      // время последнего действия/жеста

    // Метрики FPS и статистика
    uint32_t fpsFrames             = 0;
    uint32_t fpsWindowStart        = 0;
    float    currentFps            = 0.0f;
    uint32_t sleepEvents           = 0;
    uint32_t wakeByShake           = 0;
    uint32_t wakeByTaps            = 0;

    // ------------------------------------------------------------------------
    // Вспомогательные функции переключения
    // ------------------------------------------------------------------------
    bool isCalmState(AnimType a) {
        return a == AnimType::IDLE || a == AnimType::LOOK;
    }

    void goIdle() {
        AnimManager::setAnimation(AnimType::IDLE, true);
        autoIdleAt       = 0;
        angerLatched     = false;
        lastActivityTime = millis(); // сбрасываем таймер неактивности при переходе в IDLE
        (void)MotionDetector::consumeRocking(); // сбрасываем счетчики и флаг укачивания
    }

    void startTimedAnim(AnimType a, uint32_t durationMs, uint32_t now, bool blink = true) {
        AnimManager::setAnimation(a, blink);
        autoIdleAt = now + durationMs;
    }

    void goToSleep(const char* reason = "Сон") {
        AnimManager::setAnimation(AnimType::SLEEP, true);
        autoIdleAt     = 0;
        rightDownStart = 0;
        angerLatched   = false;
        sleepEvents++;
        Serial.printf("[main] %s -> SLEEP\n", reason);
    }

    // Во сне жесты кнопок сбрасываем, чтобы они не выстрелили после сна
    void drainInputsWhileSleeping() {
        (void)TouchInput::isRightClicked();
        (void)TouchInput::isMidClicked();
        (void)TouchInput::isRightDoubleClicked();
        (void)TouchInput::isRightHold();
        (void)TouchInput::isTickleDetected();
        (void)MotionDetector::consumeRocking();
        rightDownStart = 0;
        angerLatched   = false;
    }

    // ------------------------------------------------------------------------
    // 1. Поведение во сне: разделение тряски и стуков
    // ------------------------------------------------------------------------
    void handleSleepMode(uint32_t now) {
        drainInputsWhileSleeping();

        // 1. Ворчливое пробуждение завершилось -> возврат в IDLE
        if (AnimSleep::isFinished()) {
            goIdle();
            lastActivityTime = now; // пошёл новый отсчёт 60 секунд
            (void)MotionDetector::consumeRocking(); // гасим ложное укачивание
            Serial.println("[main] Проснулся -> IDLE");
            return;
        }

        // 2. Злое пробуждение завершилось -> передаём эстафету в ANGER на 3 с
        if (AnimSleep::isAngerReady()) {
            startTimedAnim(AnimType::ANGER, ANGER_AFTER_RELEASE_MS, now, false);
            return;
        }

        // 3. Если робот уже просыпается — блокируем повторные жесты
        if (!AnimSleep::isAsleep()) return;

        // Текущий уровень угловой скорости корпуса
        float rotSpeed = fabsf(MotionDetector::getGyroX());
        if (fabsf(MotionDetector::getGyroY()) > rotSpeed) rotSpeed = fabsf(MotionDetector::getGyroY());
        if (fabsf(MotionDetector::getGyroZ()) > rotSpeed) rotSpeed = fabsf(MotionDetector::getGyroZ());

        // А) ТРЯСКА КОРПУСА -> плавное ворчливое пробуждение (wakeUpGrumpy)
        // Требуется ЗАМЕТНАЯ осознанная тряска: специализированный детектор тряски
        // (знакопеременные рывки >170 °/с) ЛИБО очень резкое вращение (>250 °/с).
        // Обычные плавные движения, подъем со стола и переноска робота не будят его.
        bool isShaking = MotionDetector::consumeShake() || (rotSpeed > 250.0f);
        if (isShaking) {
            wakeByShake++;
            lastActivityTime = now; // фиксируем активность сразу при встряхивании
            Serial.println("[main] Заметная тряска -> wakeUpGrumpy (вялое ворчание)");
            // Сбрасываем любые стуки и укачивания от тряски
            while (TapDetector::consumeTap()) {}
            (void)MotionDetector::consumeRocking();
            AnimSleep::wakeUpGrumpy();
            return;
        }

        // Б) НАСТОЙЧИВЫЙ СТУК ПО СПЯЩЕМУ РОБОТУ -> ярость
        // Принимается ТОЛЬКО при строгом покое корпуса (порог rotSpeed < 25 °/с)
        if (TapDetector::consumeTap()) {
            if (rotSpeed < 25.0f) {
                int combo = TapDetector::getComboCount();
                Serial.printf("[SLEEP] Стук в покое! combo = %d\n", combo);

                // Серия из 3 стуков по неподвижному роботу будит его в ярости
                if (combo >= 3) {
                    wakeByTaps++;
                    Serial.println("[main] Разбудили стуком -> wakeUpAngry (ярость)");
                    AnimSleep::wakeUpAngry();
                    return;
                }
            } else {
                // Если робот в этот момент двигался или трясся — стук отбрасывается как помеха
                Serial.printf("[SLEEP] Стук во время движения отклонён (rotSpeed=%.1f)\n", rotSpeed);
            }
        }
    }

    // ------------------------------------------------------------------------
    // 2. Поведение «Вверх тормашками» (переворот на 180°)
    // ------------------------------------------------------------------------
    void handleUpsideDown(uint32_t now) {
        if (!Orientation::isReady()) return;

        AnimType cur = AnimManager::getCurrentAnim();
        bool down = Orientation::isUpsideDown();

        // Если робот перевёрнут — глушим укачивание
        if (down) {
            meltCooldownUntil = now + 6000;
            (void)MotionDetector::consumeRocking();
        }

        if (cur != AnimType::MELT) {
            // Перевёрнут непрерывно 1.8 с -> стекаем в лужу
            if (down && Orientation::getUpsideDownMs() >= MELT_DELAY_MS && cur != AnimType::SLEEP) {
                AnimMelt::setSideTilt(Orientation::getSideTilt());
                AnimManager::setAnimation(AnimType::MELT, false);
                meltStartMs       = now;
                autoIdleAt        = 0;
                rightDownStart    = 0;
                angerLatched      = false;
                meltCooldownUntil = now + 6000;
                (void)MotionDetector::consumeRocking();
            }
            return;
        }

        // В луже и при восстановлении непрерывно продлеваем защиту от сна
        AnimMelt::setSideTilt(Orientation::getSideTilt());
        meltCooldownUntil = now + 6000;
        (void)MotionDetector::consumeRocking();
        lastActivityTime  = now;

        // Вернули в нормальное положение -> запускаем сборку глаз
        if (!down && !AnimMelt::isRecovering() && !AnimMelt::isFinished()
            && (now - meltStartMs >= MELT_MIN_SHOW_MS)) {
            AnimMelt::startRecovery();
            meltCooldownUntil = now + 6000;
            (void)MotionDetector::consumeRocking();
        }

        // Восстановление завершено -> IDLE
        if (AnimMelt::isFinished()) {
            meltCooldownUntil = now + 6000; // ещё 6 секунд иммунитета после возврата к середине
            (void)MotionDetector::consumeRocking();
            lastActivityTime = now;
            goIdle();
        }
    }

    // ------------------------------------------------------------------------
    // 3. Сенсорные кнопки (поглаживание, ярость, щекотка, клики)
    // ------------------------------------------------------------------------
    void handleTouch(uint32_t now) {
        AnimType cur = AnimManager::getCurrentAnim();
        if (TouchInput::isRightDown() || TouchInput::isMidDown()) {
            lastActivityTime = now;
        }

        // Щекотка (быстрое поочерёдное нажатие)
        if (TouchInput::isTickleDetected() && cur != AnimType::ANGER && cur != AnimType::MELT) {
            startTimedAnim(AnimType::LAUGH, LAUGH_MS, now);
            rightDownStart = 0;
            angerLatched   = false;
            TouchInput::isRightClicked();
            return;
        }

        // Правая кнопка: поглаживание / срыв в ярость
        bool rightDown = TouchInput::isRightDown();
        if (rightDown) {
            if (rightDownStart == 0) {
                rightDownStart = now;
                angerLatched   = false;
            }
            uint32_t held = now - rightDownStart;

            if (held >= ANGER_HOLD_MS && !angerLatched) {
                angerLatched = true;
                AnimManager::setAnimation(AnimType::ANGER, false);
                autoIdleAt = 0;
            } else if (held >= JOY_HOLD_MIN_MS && !angerLatched && isCalmState(cur)) {
                AnimManager::setAnimation(AnimType::JOY, true);
                autoIdleAt = 0;
            }
        } else if (rightDownStart != 0) {
            uint32_t held = now - rightDownStart;
            rightDownStart = 0;

            if (angerLatched && cur == AnimType::ANGER) {
                autoIdleAt = now + ANGER_AFTER_RELEASE_MS; // злится ещё 3 с
            } else if (cur == AnimType::JOY) {
                if (held >= JOY_HOLD_MIN_MS) {
                    goIdle();
                }
            }
        }

        cur = AnimManager::getCurrentAnim();

        // Быстрый одиночный клик правой кнопки -> радость на 2 с
        if (TouchInput::isRightClicked() && rightDownStart == 0 && cur == AnimType::IDLE) {
            startTimedAnim(AnimType::JOY, JOY_TAP_MS, now);
            cur = AnimType::JOY;
        }

        // Центральная кнопка -> подмигивание
        if (TouchInput::isMidClicked() && isCalmState(cur)) {
            startTimedAnim(AnimType::WINK, WINK_MS, now);
            lastActivityTime = now;
            (void)MotionDetector::consumeRocking(); // отсекаем ложное укачивание от касания кнопки
            cur = AnimType::WINK;
        }
    }

    // ------------------------------------------------------------------------
    // 4. Стуки по корпусу (1-2 взгляд, 3 презрение, 5 ругань)
    // ------------------------------------------------------------------------
    void handleTaps(uint32_t now) {
        if (!TapDetector::consumeTap()) return;

        // Фильтр: игнорируем ложные стуки от инерции, если робота трясут или перемещают
        float rotSpeed = fabsf(MotionDetector::getGyroX());
        if (fabsf(MotionDetector::getGyroY()) > rotSpeed) rotSpeed = fabsf(MotionDetector::getGyroY());
        if (fabsf(MotionDetector::getGyroZ()) > rotSpeed) rotSpeed = fabsf(MotionDetector::getGyroZ());
        if (rotSpeed > 55.0f) {
            return;
        }

        lastActivityTime = now;
        AnimType cur = AnimManager::getCurrentAnim();
        if (!isCalmState(cur) && cur != AnimType::DISDAIN) return;
        if (rightDownStart != 0) return;

        int combo  = TapDetector::getComboCount();
        float dirX = TapDetector::getTapDirX();
        float dirY = TapDetector::getTapDirY();

        if (combo >= 5) {
            startTimedAnim(AnimType::SWEAR, SWEAR_MS, now, false);
            return;
        }

        if (cur == AnimType::DISDAIN) {
            AnimDisdain::setTapSide(dirX);
            AnimDisdain::nudge();
            return;
        }

        if (combo >= 3) {
            AnimDisdain::setTapSide(dirX);
            startTimedAnim(AnimType::DISDAIN, DISDAIN_MS, now, true);
            return;
        }

        // 1-2 стука: удивлённый взгляд в точку удара
        AnimLookAt::setTarget(dirX, dirY);
        startTimedAnim(AnimType::LOOK, LOOK_MS, now, false);
    }

    // ------------------------------------------------------------------------
    // 4b. Сильная тряска корпуса -> анимация укачивания/головокружения (DIZZY)
    // ------------------------------------------------------------------------
    void handleShake(uint32_t now) {
        float rotSpeed = fabsf(MotionDetector::getGyroX());
        if (fabsf(MotionDetector::getGyroY()) > rotSpeed) rotSpeed = fabsf(MotionDetector::getGyroY());
        if (fabsf(MotionDetector::getGyroZ()) > rotSpeed) rotSpeed = fabsf(MotionDetector::getGyroZ());

        // Используем строго гироскопический детектор знакопеременных колебаний
        // либо резкое интенсивное кручение корпуса (> 320 °/с).
        // Акселерометр убран, поэтому плавный или быстрый подъем со стола не вызывает укачивание.
        // Детектор знакопеременной тряски либо быстрое кручение (> 320 °/с)
        bool shake = MotionDetector::consumeShake() || (rotSpeed > 320.0f);
        if (!shake) return;

        AnimType cur = AnimManager::getCurrentAnim();
        // Тряска имеет приоритет над IDLE и начальными реакциями на стук (LOOK, DISDAIN)
        if (cur == AnimType::IDLE || cur == AnimType::LOOK || cur == AnimType::DISDAIN) {
            Serial.println("[main] Сильная тряска -> DIZZY (укачивание/головокружение)");
            startTimedAnim(AnimType::DIZZY, DIZZY_MS, now, false); // blink=false для плавного входа из IDLE
            lastActivityTime = now;

            while (TapDetector::consumeTap()) {}
            (void)MotionDetector::consumeRocking();
        }
    }

    // ------------------------------------------------------------------------
    // 5. Укачивание (Rocking) -> переход в сон
    // ------------------------------------------------------------------------
    void handleRocking(uint32_t now) {
        bool rocking = MotionDetector::consumeRocking();
        if (!rocking) return;

        // Блокируем сон, если робот был в луже или восстановился менее 6 секунд назад
        if (now < meltCooldownUntil) return;

        AnimType cur = AnimManager::getCurrentAnim();
        if (!isCalmState(cur)) return;
        if (rightDownStart != 0) return;
        if (Orientation::isReady() && Orientation::isUpsideDown()) return;

        goToSleep("Укачали");
    }

    // ------------------------------------------------------------------------
    // 6. Таймеры автовозврата в IDLE
    // ------------------------------------------------------------------------
    void handleAutoIdle(uint32_t now) {
        if (autoIdleAt == 0) return;
        if (now < autoIdleAt) return;

        AnimType cur = AnimManager::getCurrentAnim();
        if (cur == AnimType::JOY     || cur == AnimType::LAUGH   ||
            cur == AnimType::LOOK    || cur == AnimType::DISDAIN ||
            cur == AnimType::SWEAR   || cur == AnimType::ANGER   ||
            cur == AnimType::DIZZY   || cur == AnimType::WINK) {
            goIdle();
            lastActivityTime = now; // робот только что успокоился, таймер бездействия пошёл
        } else {
            autoIdleAt = 0;
        }
    }

    // ------------------------------------------------------------------------
    // 7. Автоматическое засыпание при бездействии
    // ------------------------------------------------------------------------
    void handleInactivity(uint32_t now) {
        // Блокируем сон во время лужи и 6 секунд после восстановления
        if (now < meltCooldownUntil) {
            lastActivityTime = now;
            return;
        }

        AnimType cur = AnimManager::getCurrentAnim();

        // Если играет активная эмоция — робот занят
        if (cur != AnimType::IDLE) {
            lastActivityTime = now;
            return;
        }

        // Если удерживается сенсорная кнопка
        if (rightDownStart != 0) {
            lastActivityTime = now;
            return;
        }

        // Если корпус шевелят (угловая скорость > 20 °/с) — активность
        float rotSpeed = fabsf(MotionDetector::getGyroX());
        if (fabsf(MotionDetector::getGyroY()) > rotSpeed) rotSpeed = fabsf(MotionDetector::getGyroY());
        if (fabsf(MotionDetector::getGyroZ()) > rotSpeed) rotSpeed = fabsf(MotionDetector::getGyroZ());
        if (rotSpeed > 20.0f) {
            lastActivityTime = now;
            return;
        }

        // Защита от разницы millis() и проверка честного истечения времени бездействия
        if (now >= lastActivityTime && (now - lastActivityTime >= INACTIVITY_SLEEP_MS)) {
            goToSleep("Бездействие");
        }
    }

    // ------------------------------------------------------------------------
    // Сервисный дашборд (зажатие обеих кнопок > 1.5 с)
    // ------------------------------------------------------------------------
    void drawServiceMenu(uint32_t now) {
        char buf[64];
        sprite.fillSprite(TFT_BLACK);
        sprite.setTextDatum(TL_DATUM);
        sprite.setTextFont(2);
        sprite.setTextSize(1);

        int y = 2;
        const int LINE = 16;

        sprite.setTextColor(TFT_YELLOW, TFT_BLACK);
        sprite.drawString("SERVICE MODE", 6, y); y += LINE;

        sprite.setTextColor(TFT_WHITE, TFT_BLACK);
        snprintf(buf, sizeof(buf), "Uptime: %lus  Heap: %u B", (unsigned long)(now / 1000), (unsigned)ESP.getFreeHeap());
        sprite.drawString(buf, 6, y); y += LINE;

        snprintf(buf, sizeof(buf), "FPS: %.1f  Anim: %s", currentFps, AnimManager::getAnimName(AnimManager::getCurrentAnim()));
        sprite.drawString(buf, 6, y); y += LINE;

        snprintf(buf, sizeof(buf), "IMU: %s  Px: %.1f Py: %.1f",
                 ImuSensor::isReady() ? "ON" : "OFF", ImuSensor::getParallaxX(), ImuSensor::getParallaxY());
        sprite.drawString(buf, 6, y); y += LINE;

        snprintf(buf, sizeof(buf), "Tap: %s  jerk: %.2fg", TapDetector::isReady() ? "ON" : "OFF", TapDetector::getLastJerk());
        sprite.drawString(buf, 6, y); y += LINE;

        snprintf(buf, sizeof(buf), "Orient: UpDot %.2f  %s",
                 Orientation::getUpDot(), Orientation::isUpsideDown() ? "UPSIDE-DOWN" : "OK");
        sprite.drawString(buf, 6, y); y += LINE;

        sprite.setTextColor(TFT_CYAN, TFT_BLACK);
        snprintf(buf, sizeof(buf), "Gyro: %4.0f %4.0f %4.0f",
                 MotionDetector::getGyroX(), MotionDetector::getGyroY(), MotionDetector::getGyroZ());
        sprite.drawString(buf, 6, y); y += LINE;

        const char axisName[3] = { 'X', 'Y', 'Z' };
        snprintf(buf, sizeof(buf), "Rock: ax %c  half %d  amp %.0f",
                 axisName[MotionDetector::getDominantAxis()],
                 MotionDetector::getRockHalfCycles(), MotionDetector::getRockAmplitude());
        sprite.drawString(buf, 6, y); y += LINE;

        snprintf(buf, sizeof(buf), "Sleep#:%lu  Shake#:%lu", (unsigned long)sleepEvents, (unsigned long)wakeByShake);
        sprite.drawString(buf, 6, y); y += LINE;

        snprintf(buf, sizeof(buf), "Wake taps#:%lu", (unsigned long)wakeByTaps);
        sprite.drawString(buf, 6, y); y += LINE;

        sprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
        sprite.drawString("Hold both buttons 1.5s = exit", 6, 182);

        sprite.pushSprite(0, 0);
    }
} // anonymous namespace

// ============================================================================
//  setup()
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(50);

    // Подсветка дисплея
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

    // Инициализация экрана
    tft.init();
    tft.setRotation(2);
    tft.fillScreen(TFT_BLACK);

    // Единый буфер кадра 240x200
    sprite.setColorDepth(16);
    sprite.createSprite(240, 200);
    sprite.fillSprite(TFT_BLACK);

    // ==========================================
    // Заставка "Любовь. Смерть. Роботы"
    // ==========================================
    AnimIntro::play(tft, sprite);

    // Сенсорные кнопки
    TouchInput::init();

    // Датчики шины I2C
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);
    ImuSensor::init();
    TapDetector::init();
    Orientation::init();
    MotionDetector::init();

    // Диспетчер анимаций
    AnimManager::init(tft);
    AnimManager::setAnimation(AnimType::IDLE, true);

    lastFrameTime    = millis();
    fpsWindowStart   = lastFrameTime;
    lastActivityTime = lastFrameTime; // отсчет неактивности пойдет строго ПОСЛЕ заставки
    Serial.println("[main] Robot ready");

}

// ============================================================================
//  loop()
// ============================================================================
void loop() {
    uint32_t now = millis();

    // 1. Опрос сенсоров
    TouchInput::update();
    ImuSensor::update();
    TapDetector::update();
    Orientation::update();
    MotionDetector::update();

    // 2. Вход/выход из сервисного меню
    bool both = TouchInput::isBothHold();
    if (both && !bothHoldLatch) {
        bothHoldLatch = true;
        inServiceMenu = !inServiceMenu;
        if (!inServiceMenu) goIdle();
    }
    if (!both) bothHoldLatch = false;

    if (inServiceMenu) {
        if (now - lastServiceDraw >= SERVICE_FRAME_MS) {
            lastServiceDraw = now;
            drawServiceMenu(now);
        }
        vTaskDelay(1);
        return;
    }

    // 3. Логика поведений
    AnimType cur = AnimManager::getCurrentAnim();

    if (cur == AnimType::SLEEP) {
        // Робот спит: слушаем только тряску и 5 стуков
        handleSleepMode(now);
    } else {
        // Робот бодрствует: переворот, поглаживания, щекотка, стуки и укачивание
        handleUpsideDown(now);
        handleTouch(now);
        handleShake(now); // Тряска проверяется ДО стуков, очищая ложные удары
        handleTaps(now);
        handleRocking(now);
        handleAutoIdle(now);
        handleInactivity(now); // <-- добавили вызов проверки бездействия
    }

    // 4. Отрисовка рабочего кадра (~60 FPS)
    if (now - lastFrameTime >= TARGET_FRAME_MS) {
        lastFrameTime = now;
        AnimManager::runCurrentAnimation(tft, sprite);

        fpsFrames++;
        if (now - fpsWindowStart >= 1000) {
            currentFps = (float)fpsFrames * 1000.0f / (float)(now - fpsWindowStart);
            fpsFrames = 0;
            fpsWindowStart = now;
        }
    }

    vTaskDelay(1);
}
