#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "pins.h"
#include "config.h"
#include "display/display.h"
#include "hardware/touch_input.h"
#include "sensors/imu_sensor.h"
#include "animation/face_state.h"
#include "animation/anim_idle.h"

// ============================================================================
//  Глобальные объекты. Спрайт — указатель: выделяется в setup() с проверкой RAM.
// ============================================================================
PocketST7789  tft(&SPI, TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
PocketSprite* sprite = nullptr;
AnimIdle      idle;

static uint32_t lastFrameMs = 0;
static uint32_t fpsFrames = 0, fpsLastReport = 0, pushUsAccum = 0, renderUsAccum = 0;
static uint8_t  paletteIdx = 0;

// Демонстрационные палитры для кнопки MID (пока нет anim_manager)
static void applyPalette(uint8_t idx) {
    switch (idx % 4) {
    case 0: FaceState::setTargetPalette(IDLE_EYE_R, IDLE_EYE_G, IDLE_EYE_B, IDLE_G1_R, IDLE_G1_G, IDLE_G1_B, IDLE_G2_R, IDLE_G2_G, IDLE_G2_B); break; // Idle green
    case 1: FaceState::setTargetPalette(255, 180,  40, 120,  70,  10, 40, 20, 0); break;   // Joy amber
    case 2: FaceState::setTargetPalette(255,  40,  40, 120,  10,  10, 40,  0, 0); break;   // Anger red
    case 3: FaceState::setTargetPalette( 60, 140, 255,  20,  60, 120,  0, 15, 40); break;  // Sad blue
    }
}

void setup() {
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 1500) {}
    Serial.println("\n=== Pocket Rick v2 (ESP32-C3 Zero, ST7789 1.69\" 280x240) ===");

    // 1. Кадровый буфер
    sprite = createFrameBuffer();

    // 2. Дисплей
    SPI.begin(TFT_SCLK_PIN, -1, TFT_MOSI_PIN, -1);
    initDisplay(tft);
    if (!sprite) haltOutOfMemory(tft);

    // 3. I2C шина
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);
    pinMode(BMI_INT1_PIN, INPUT);

    bool imuOk = ImuSensor::init();
    Serial.printf("[I2C] BMI160 %s (0x%02X)\n",
                  imuOk ? "OK" : "MISSING", ImuSensor::getAddress());

    // 4. Кнопки
    TouchInput::init();

    // 5. Состояние лица
    FaceState::init();
    idle.reset();

    lastFrameMs = fpsLastReport = millis();
    Serial.printf("[SYS] heap after init: %u\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
    uint32_t now = millis();

    // --- датчики и ввод: каждый проход, сами ограничивают частоту по millis() ---
    TouchInput::update();
    ImuSensor::update(now);
    //ProxSensor::update();

    // --- события кнопок (временная логика до появления anim_manager) ---
    if (TouchInput::wasClicked(TouchInput::MID)) {
        FaceState::prepareTransition(true);
        applyPalette(++paletteIdx);
        idle.reset();
    }
    if (TouchInput::wasClicked(TouchInput::RIGHT)) {
        FaceState::prepareTransition(true);           // "пощекотали" — рефлекторный прищур
        idle.reset();
    }
    if (TouchInput::wasLongPressed(TouchInput::MID)) {
        setBacklight(60);                             // заглушка "сон"
    }
    if (TouchInput::wasChord()) {
        setBacklight(TFT_BL_DEFAULT);                 // заглушка "сервис"
    }
    if (ImuSensor::isSuddenLiftDetected()) { FaceState::prepareTransition(false); idle.reset(); }
    if (ImuSensor::isShakeDetected())      { FaceState::prepareTransition(true);  idle.reset(); }

    // --- рендер 50 Гц ---
    if (now - lastFrameMs >= FRAME_INTERVAL_MS) {
        lastFrameMs = now;

        FaceState::updateColorLerp();

        float parX = 0.0f, parY = 0.0f;
        if (ImuSensor::isConnected()) { parX = ImuSensor::getParallaxX(); parY = ImuSensor::getParallaxY(); }

        uint32_t tR = micros();
        idle.runStep(*sprite, parX, parY);
        uint32_t tP = micros();
        pushSpriteToDisplay(tft, *sprite);
        uint32_t tE = micros();

        renderUsAccum += tP - tR;
        pushUsAccum   += tE - tP;
        ++fpsFrames;

#if FPS_REPORT_MS > 0
        if (now - fpsLastReport >= FPS_REPORT_MS) {
            float sec = (now - fpsLastReport) * 0.001f;
            Serial.printf("[FPS] %.1f  render %.2f ms  push %.2f ms  heap %u\n",
                          fpsFrames / sec,
                          renderUsAccum / 1000.0f / fpsFrames,
                          pushUsAccum   / 1000.0f / fpsFrames,
                          (unsigned)ESP.getFreeHeap());
            fpsFrames = 0; renderUsAccum = pushUsAccum = 0; fpsLastReport = now;
        }
#endif
    }
}
