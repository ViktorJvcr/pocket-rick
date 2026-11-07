#include <Arduino.h>
#include <TFT_eSPI.h>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>

#include "pins.h"
#include "hardware/touch_input.h"
#include "sensors/imu_sensor.h"
#include "animation/face_state.h"
#include "animation/anim_manager.h"
#include "animation/anim_wink.h"

TFT_eSPI tft;
TFT_eSprite sprite(&tft);

namespace {

// -----------------------------------------------------------------------------
// Настройки
// -----------------------------------------------------------------------------

constexpr int16_t SPRITE_WIDTH  = 240;
constexpr int16_t SPRITE_HEIGHT = 200;

constexpr uint32_t TARGET_FRAME_MS       = 16;
constexpr uint32_t SERVICE_UI_INTERVAL_MS = 125;  // 8 Гц

constexpr uint32_t STROKE_START_MS = 300;
constexpr uint32_t OVERLOAD_MS     = 4500;
constexpr uint32_t LAUGH_DURATION_MS = 1800;
constexpr uint32_t ANGER_HOLD_MS     = 3000;

// Порог > 1500 мс и стартовая блокировка 400 мс реализованы в TouchInput.
bool inServiceMenu = false;
bool bothHoldLatched = false;
bool inputBlockedUntilRelease = false;

// -----------------------------------------------------------------------------
// Кадрирование и реальный FPS
// -----------------------------------------------------------------------------

uint32_t lastFrameStartedAt = 0;
uint32_t fpsWindowStartedAt = 0;
uint32_t framesInWindow = 0;
float realFps = 0.0f;

// -----------------------------------------------------------------------------
// Состояние жестов
// -----------------------------------------------------------------------------

enum class Reaction : uint8_t {
    NONE,
    STROKE,
    LAUGH,
    OVERLOAD,
    ANGER_HOLD
};

Reaction reaction = Reaction::NONE;

bool rightWasDown = false;
bool rightClickEligible = false;
bool midClickEligible = false;

uint32_t rightPressStartTime = 0;
uint32_t laughUntil = 0;
uint32_t angerHoldUntil = 0;

struct InputSnapshot {
    bool rightDown;
    bool midPhysicalDown;
    bool rightPhysicalDown;

    bool bothHold;
    bool tickle;
    bool midClicked;
    bool rightClicked;
};

// -----------------------------------------------------------------------------
// Время и общие вспомогательные функции
// -----------------------------------------------------------------------------

// Корректно работает при переполнении millis() для наших коротких интервалов.
bool deadlineReached(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
}

void selectAnimation(AnimType animation) {
    // Не перезапускаем reset() анимации на каждой итерации удержания.
    if (AnimManager::getCurrentAnim() != animation) {
        AnimManager::setAnimation(animation);
    }
}

void resetGestureState() {
    reaction = Reaction::NONE;

    rightWasDown = false;
    rightClickEligible = false;
    midClickEligible = false;

    rightPressStartTime = 0;
    laughUntil = 0;
    angerHoldUntil = 0;
}

void resetFpsCounter(uint32_t now) {
    fpsWindowStartedAt = now;
    framesInWindow = 0;
    realFps = 0.0f;
}

void updateFpsCounter(uint32_t now) {
    const uint32_t elapsed = now - fpsWindowStartedAt;

    if (elapsed < 1000U) {
        return;
    }

    // Используем фактическую продолжительность окна, а не предполагаемые 1000 мс.
    realFps = static_cast<float>(framesInWindow) * 1000.0f
            / static_cast<float>(elapsed);

    framesInWindow = 0;
    fpsWindowStartedAt = now;
}

bool allButtonsReleased(const InputSnapshot& input) {
    return !input.midPhysicalDown
        && !input.rightPhysicalDown
        && !input.rightDown;
}

InputSnapshot readInputs() {
    InputSnapshot input{};

    input.rightDown = TouchInput::isRightDown();

    // Сырые уровни нужны только для контроля полного отпускания кнопок
    // и защиты от отложенных кликов после сервисного удержания.
    // Распознавание жестов остаётся задачей TouchInput.
    input.midPhysicalDown = digitalRead(TOUCH_MID_PIN) == HIGH;
    input.rightPhysicalDown = digitalRead(TOUCH_RIGHT_PIN) == HIGH;

    // Читаем каждое событие ровно один раз за итерацию, в том числе
    // в сервисном меню: события не должны накапливаться на выход.
    input.bothHold = TouchInput::isBothHold();
    input.tickle = TouchInput::isTickleDetected();
    input.midClicked = TouchInput::isMidClicked();
    input.rightClicked = TouchInput::isRightClicked();

    return input;
}

// -----------------------------------------------------------------------------
// Сервисный экран
// -----------------------------------------------------------------------------



void drawServiceStaticUi() {
    // Полная очистка выполняется только при входе в меню.
    tft.fillScreen(TFT_BLACK);
    tft.setTextWrap(false);
    tft.setTextFont(1);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);

    tft.drawString("SERVICE DASHBOARD", 120, 18, 1);
    tft.drawFastHLine(8, 36, 224, TFT_DARKGREY);

    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("Hold BOTH >1.5s to exit", 120, 251, 1);

    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("ESP32-C3 / ST7789 / BMI160", 120, 268, 1);

    tft.setTextDatum(TL_DATUM);
}

void drawServiceDashboard() {
    // Переиспользуем лицевой буфер: дополнительный спрайт и дополнительные
    // 96 КБ памяти не нужны. Сначала собираем UI в RAM, затем выводим целиком.
    sprite.fillSprite(TFT_BLACK);
    sprite.setTextWrap(false);
    sprite.setTextFont(1);
    sprite.setTextDatum(TL_DATUM);
    sprite.setTextSize(2);
    sprite.setTextColor(TFT_WHITE, TFT_BLACK);

    char text[80];

    // 64-битный uptime не обнуляется при переполнении millis().
    const uint64_t uptimeSeconds =
        static_cast<uint64_t>(esp_timer_get_time()) / 1000000ULL;

    const unsigned long long minutes =
        static_cast<unsigned long long>(uptimeSeconds / 60ULL);

    const unsigned long long seconds =
        static_cast<unsigned long long>(uptimeSeconds % 60ULL);

    std::snprintf(
        text, sizeof(text), "Up %llum %02llus", minutes, seconds
    );
    sprite.drawString(text, 8, 6, 1);

    const float freeHeapKb =
        static_cast<float>(ESP.getFreeHeap()) / 1024.0f;

    std::snprintf(
        text, sizeof(text), "Heap %.1f KB",
        static_cast<double>(freeHeapKb)
    );
    sprite.drawString(text, 8, 30, 1);

    std::snprintf(
        text, sizeof(text), "FPS %.1f",
        static_cast<double>(realFps)
    );
    sprite.drawString(text, 8, 54, 1);

    sprite.drawString("IMU", 8, 78, 1);

    const bool imuReady = ImuSensor::isReady();

    sprite.setTextColor(
        imuReady ? TFT_GREEN : TFT_RED, TFT_BLACK
    );
    sprite.drawString(
        imuReady ? "ONLINE" : "OFFLINE", 68, 78, 1
    );

    sprite.setTextSize(1);
    sprite.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

    std::snprintf(
        text,
        sizeof(text),
        "Parallax X:%+.2f  Y:%+.2f",
        static_cast<double>(ImuSensor::getParallaxX()),
        static_cast<double>(ImuSensor::getParallaxY())
    );
    sprite.drawString(text, 8, 106, 1);

    sprite.drawFastHLine(8, 120, 224, TFT_DARKGREY);

    

    // Возвращаем нейтральные настройки текста для последующих анимаций.
    sprite.setTextSize(1);
    sprite.setTextColor(TFT_WHITE, TFT_BLACK);

    sprite.pushSprite(0, 40);
}

// -----------------------------------------------------------------------------
// Переключение сервисного режима
// -----------------------------------------------------------------------------

void toggleServiceMenu() {
    inServiceMenu = !inServiceMenu;

    resetGestureState();
    inputBlockedUntilRelease = true;
    selectAnimation(AnimType::IDLE);

    if (inServiceMenu) {
        drawServiceStaticUi();
    } else {
        // Убираем заголовок и подвал сервисного режима.
        tft.fillScreen(TFT_BLACK);

        sprite.fillSprite(TFT_BLACK);
        sprite.setTextFont(1);
        sprite.setTextSize(1);
        sprite.setTextDatum(TL_DATUM);
        sprite.setTextColor(TFT_WHITE, TFT_BLACK);
    }

    const uint32_t now = millis();
    resetFpsCounter(now);

    // Первый кадр нового режима разрешён немедленно.
    lastFrameStartedAt = now - (
        inServiceMenu ? SERVICE_UI_INTERVAL_MS : TARGET_FRAME_MS
    );
}

void handleServiceToggle(const InputSnapshot& input) {
    // Поддерживает и импульсное событие isBothHold(), и длительный true.
    // Повторный вход/выход разрешаем только после отпускания обеих кнопок.
    if (allButtonsReleased(input) && !input.bothHold) {
        bothHoldLatched = false;
    }

    if (input.bothHold && !bothHoldLatched) {
        bothHoldLatched = true;
        toggleServiceMenu();
    }
}

// -----------------------------------------------------------------------------
// Жесты рабочего режима
// -----------------------------------------------------------------------------

void handleWorkGestures(const InputSnapshot& input, uint32_t now) {
    if (inputBlockedUntilRelease) {
        if (allButtonsReleased(input)) {
            inputBlockedUntilRelease = false;
        }

        // Не обрабатываем события отпускания сервисного удержания.
        return;
    }

    // Разрешаем центральный клик только после нажатия, увиденного
    // в рабочем режиме. Отложенный клик из сервисного меню игнорируется.
    if (input.midPhysicalDown) {
        midClickEligible = true;
    }

    const bool midClick = input.midClicked && midClickEligible;

    if (input.midClicked) {
        midClickEligible = false;
    }

    // Начало удержания правой кнопки.
    if (input.rightDown && !rightWasDown) {
        rightPressStartTime = now;
        rightClickEligible = false;
    }

    // Отпускание правой кнопки.
    if (!input.rightDown && rightWasDown) {
        const uint32_t heldMs = now - rightPressStartTime;

        // Проверяем длительность также на отпускании: порог мог быть
        // пересечён непосредственно во время передачи предыдущего кадра.
        const bool releasedAfterOverload =
            reaction == Reaction::OVERLOAD ||
            (heldMs >= OVERLOAD_MS && reaction != Reaction::ANGER_HOLD);

        if (releasedAfterOverload) {
            reaction = Reaction::ANGER_HOLD;
            angerHoldUntil = now + ANGER_HOLD_MS;
            rightClickEligible = false;
            selectAnimation(AnimType::ANGER);
        } else if (reaction == Reaction::STROKE) {
            reaction = Reaction::NONE;
            rightClickEligible = false;
            selectAnimation(AnimType::IDLE);
        } else {
            // Удержание от 300 мс уже является поглаживанием,
            // а не отладочным одиночным кликом.
            rightClickEligible =
                heldMs < STROKE_START_MS &&
                reaction == Reaction::NONE;
        }
    }

    rightWasDown = input.rightDown;

    const uint32_t heldMs =
        input.rightDown ? now - rightPressStartTime : 0U;

    if (input.rightDown && heldMs >= STROKE_START_MS) {
        rightClickEligible = false;
    }

    const bool rightClick =
        input.rightClicked && rightClickEligible;

    if (input.rightClicked) {
        rightClickEligible = false;
    }

    // Приоритет:
    // 1. Перегрузка и её трёхсекундное последействие.
    // 2. Щекотка.
    // 3. Поглаживание.
    // 4. Одиночные клики.

    if (reaction == Reaction::ANGER_HOLD) {
        if (deadlineReached(now, angerHoldUntil)) {
            reaction = Reaction::NONE;
            selectAnimation(AnimType::IDLE);
        }
        return;
    }

    if (input.rightDown && heldMs >= OVERLOAD_MS) {
        reaction = Reaction::OVERLOAD;
        selectAnimation(AnimType::ANGER);
        return;
    }

    if (input.tickle) {
        reaction = Reaction::LAUGH;
        laughUntil = now + LAUGH_DURATION_MS;
        rightClickEligible = false;
        selectAnimation(AnimType::LAUGH);
    }

    if (reaction == Reaction::LAUGH) {
        if (deadlineReached(now, laughUntil)) {
            reaction = Reaction::NONE;
            selectAnimation(AnimType::IDLE);
        }
        return;
    }

    if (input.rightDown && heldMs >= STROKE_START_MS) {
        reaction = Reaction::STROKE;
        selectAnimation(AnimType::JOY);
        return;
    }

    if (midClick) {
        rightClickEligible = false;
        AnimManager::setAnimation(AnimType::WINK);
    } else if (rightClick) {
        AnimManager::nextState();
    }

    if (AnimManager::getCurrentAnim() == AnimType::WINK &&
        AnimWink::isFinished()) {
        selectAnimation(AnimType::IDLE);
    }
}

} // namespace

// -----------------------------------------------------------------------------
// Arduino setup
// -----------------------------------------------------------------------------

void setup() {
    pinMode(TFT_BL_PIN, OUTPUT);

    // Не подсвечиваем неинициализированную матрицу.
    digitalWrite(TFT_BL_PIN, LOW);

    // Пины SPI, ST7789, 240x280, CGRAM_OFFSET и частота шины
    // должны быть настроены в конфигурации TFT_eSPI.
    tft.init();
    tft.setRotation(0);
    tft.invertDisplay(true);
    tft.fillScreen(TFT_BLACK);
    tft.setTextWrap(false);

    sprite.setColorDepth(16);

    if (sprite.createSprite(SPRITE_WIDTH, SPRITE_HEIGHT) == nullptr) {
        tft.setTextFont(1);
        tft.setTextDatum(TL_DATUM);
        tft.setTextSize(1);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("SPRITE ALLOCATION FAILED", 12, 90, 1);

        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("Need 96000 contiguous bytes", 12, 112, 1);
        tft.drawString("Check heap usage and restart", 12, 130, 1);

        digitalWrite(TFT_BL_PIN, HIGH);

        // Аварийное состояние: нельзя запускать анимации без буфера.
        // Отдаём процессор системным задачам, не создавая busy-loop.
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    sprite.fillSprite(TFT_BLACK);
    sprite.setTextWrap(false);

    TouchInput::init();
    ImuSensor::init();

    // AnimManager инициализирует анимации и управляет FaceState.
    // Повторно обновлять палитру FaceState из main.cpp не требуется.
    AnimManager::init(tft);
    AnimManager::setAnimation(AnimType::IDLE);

    resetGestureState();

    const uint32_t now = millis();
    resetFpsCounter(now);
    lastFrameStartedAt = now - TARGET_FRAME_MS;

    digitalWrite(TFT_BL_PIN, HIGH);
}

// -----------------------------------------------------------------------------
// Arduino loop
// -----------------------------------------------------------------------------

void loop() {
    // Ввод и батарея обслуживаются независимо от частоты отрисовки.
    TouchInput::update();

    const InputSnapshot input = readInputs();

    handleServiceToggle(input);

    if (!inServiceMenu) {
        handleWorkGestures(input, millis());
    }

    // IMU продолжает обновляться и в сервисном режиме.
    ImuSensor::update();

    const uint32_t now = millis();
    const uint32_t frameInterval =
        inServiceMenu ? SERVICE_UI_INTERVAL_MS : TARGET_FRAME_MS;

    // Неблокирующее кадрирование: не ждём окончания интервала в цикле.
    // При медленном кадре пропускаем потерянное время, не рисуем
    // серию догоняющих кадров.
    if (now - lastFrameStartedAt >= frameInterval) {
        lastFrameStartedAt = now;

        if (inServiceMenu) {
            drawServiceDashboard();
        } else {
            // По контракту проекта runStep() каждой анимации уже вызывает
            // sprite.pushSprite(0, 40). Второй push здесь не нужен.
            AnimManager::runCurrentAnimation(tft, sprite);
        }

        // Считаем реально выполненные кадры, а не итерации loop().
        ++framesInWindow;
    }

    updateFpsCounter(millis());

    // Один системный тик для USB, планировщика и watchdog.
    // Это не задержка на 16 мс: срок следующего кадра проверяется выше.
    vTaskDelay(1);
}
