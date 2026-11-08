#include "motion_detector.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "pins.h"

namespace MotionDetector {

namespace {
    // ------------------------------------------------------------------
    // Регистры BMI160
    // ------------------------------------------------------------------
    const uint8_t REG_CHIP_ID     = 0x00;   // должен вернуть 0xD1
    const uint8_t CHIP_ID_BMI160  = 0xD1;
    const uint8_t REG_PMU_STATUS  = 0x03;   // биты [3:2] — режим гироскопа
    const uint8_t REG_GYR_DATA    = 0x0C;   // GYR_X_LSB .. GYR_Z_MSB (6 байт)
    const uint8_t REG_GYR_RANGE   = 0x43;   // диапазон гироскопа
    const uint8_t REG_CMD         = 0x7E;
    const uint8_t CMD_GYR_NORMAL  = 0x15;   // включить гироскоп в normal mode

    // ------------------------------------------------------------------
    // Параметры детектора укачивания (более чуткие и естественные)
    // ------------------------------------------------------------------
    const uint32_t SAMPLE_MS            = 10;      // опрос 100 Гц
    const float    ROCK_LP_ALPHA        = 0.20f;   // плавный фильтр НЧ
    const float    ROCK_THR             = 16.0f;   // комфортный порог перехода через ноль
    const float    ROCK_MIN_PEAK        = 32.0f;   // минимальная скорость (естественное покачивание в руке)
    const float    ROCK_MAX_PEAK        = 300.0f;  // максимальная скорость
    const uint32_t ROCK_HALF_MIN_MS     = 200;     // быстрый полупериод
    const uint32_t ROCK_HALF_MAX_MS     = 1350;    // медленный полупериод (запас под плавный темп)
    const float    ROCK_RATIO_MIN       = 0.38f;   // мягкий допуск на естественную неравномерность руки
    const float    ROCK_RATIO_MAX       = 2.6f;
    const float    ROCK_MAX_HF          = 190.0f;  // фильтрация дрожи
    const int      ROCK_HALF_CYCLES     = 4;       // ровно 4 полупериода (2 полных качания: влево-вправо-влево-вправо)
    const uint32_t ROCK_TIMEOUT_MS      = 1500;    // запас времени на паузу между взмахами
    const float    ENV_DECAY            = 0.97f;   // затухание огибающей для выбора оси
    const uint32_t ROCK_REFRACTORY_MS   = 2500;

    // ------------------------------------------------------------------
    // Параметры детектора тряски (Shake)
    // ------------------------------------------------------------------
    const float    SHAKE_THR            = 280.0f;  // град/с (порог уверенной тряски)
    const uint32_t SHAKE_EVENT_GAP_MS   = 350;     // максимум между рывками
    const int      SHAKE_EVENTS_NEEDED  = 4;       // 3 резких знакопеременных рывка подряд
    const uint32_t SHAKE_RESET_MS       = 500;     // пауза — сброс счетчика рывков
    const uint32_t SHAKE_REFRACTORY_MS  = 1200;    // пауза после срабатывания

    // ------------------------------------------------------------------
    // Состояние
    // ------------------------------------------------------------------
    uint8_t  i2cAddr    = 0x68;
    bool     ready      = false;
    float    gyroScale  = 1.0f / 16.4f;  // LSB -> град/с (по умолчанию ±2000 dps)
    uint32_t lastSample = 0;
    int      i2cErrors  = 0;

    float gRaw[3] = {0, 0, 0};   // сырые значения, град/с
    float gLp[3]  = {0, 0, 0};   // сглаженные
    float env[3]  = {0, 0, 0};   // огибающая для выбора доминирующей оси
    int   domAxis = 1;

    // Rocking
    int      rockSign        = 0;    // +1 / -1 / 0 (мертвая зона)
    uint32_t lastCrossMs     = 0;
    uint32_t prevHalfPeriod  = 0;
    float    halfPeak        = 0.0f; // максимум |сигнала| в текущем полупериоде
    float    hfPeak          = 0.0f; // максимум |ВЧ-части| в текущем полупериоде
    int      halfCycles      = 0;
    float    lastAmplitude   = 0.0f;
    uint32_t rockRefractory  = 0;
    bool     rockingFlag     = false;
    uint32_t rockingCount    = 0;

    // Shake
    int      shakeSign       = 0;
    int      shakeEvents     = 0;
    uint32_t lastShakeEvent  = 0;
    uint32_t shakeRefractory = 0;
    bool     shakeFlag       = false;
    uint32_t shakeCount      = 0;

    // ------------------------------------------------------------------
    // I2C helpers
    // ------------------------------------------------------------------
    bool readRegs(uint8_t reg, uint8_t *buf, uint8_t len) {
        Wire.beginTransmission(i2cAddr);
        Wire.write(reg);
        if (Wire.endTransmission(false) != 0) return false;
        uint8_t got = Wire.requestFrom((int)i2cAddr, (int)len);
        if (got != len) return false;
        for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
        return true;
    }

    bool readReg(uint8_t reg, uint8_t &value) {
        return readRegs(reg, &value, 1);
    }

    bool writeReg(uint8_t reg, uint8_t value) {
        Wire.beginTransmission(i2cAddr);
        Wire.write(reg);
        Wire.write(value);
        return Wire.endTransmission() == 0;
    }

    bool probeAddress(uint8_t addr) {
        i2cAddr = addr;
        uint8_t id = 0;
        if (!readReg(REG_CHIP_ID, id)) return false;
        return id == CHIP_ID_BMI160;
    }

    // Пересчитать масштаб LSB -> град/с по текущему регистру диапазона
    void readGyroScale() {
        uint8_t range = 0;
        if (!readReg(REG_GYR_RANGE, range)) return;
        switch (range & 0x07) {
            case 0: gyroScale = 1.0f / 16.4f;  break; // ±2000 dps
            case 1: gyroScale = 1.0f / 32.8f;  break; // ±1000 dps
            case 2: gyroScale = 1.0f / 65.6f;  break; // ±500 dps
            case 3: gyroScale = 1.0f / 131.2f; break; // ±250 dps
            case 4: gyroScale = 1.0f / 262.4f; break; // ±125 dps
            default: gyroScale = 1.0f / 16.4f; break;
        }
    }

    // Убедиться, что гироскоп включен (ImuSensor обычно уже сделал это)
    void ensureGyroOn() {
        writeReg(REG_CMD, CMD_GYR_NORMAL);
        delay(60);
    }

    bool readGyro() {
        uint8_t b[6];
        if (!readRegs(REG_GYR_DATA, b, 6)) return false;
        int16_t x = (int16_t)((b[1] << 8) | b[0]);
        int16_t y = (int16_t)((b[3] << 8) | b[2]);
        int16_t z = (int16_t)((b[5] << 8) | b[4]);
        gRaw[0] = x * gyroScale;
        gRaw[1] = y * gyroScale;
        gRaw[2] = z * gyroScale;
        return true;
    }

    void resetRocking() {
        rockSign = 0;
        halfCycles = 0;
        prevHalfPeriod = 0;
        halfPeak = 0.0f;
        hfPeak = 0.0f;
    }

    // ------------------------------------------------------------------
    // Детектор тряски: резкие знакопеременные рывки на любой оси
    // ------------------------------------------------------------------
    void processShake(uint32_t now) {
        // Ось с максимальной мгновенной скоростью
        int ax = 0;
        float best = fabsf(gRaw[0]);
        for (int i = 1; i < 3; i++) {
            float a = fabsf(gRaw[i]);
            if (a > best) { best = a; ax = i; }
        }
        float v = gRaw[ax];

        bool event = false;
        if (v > SHAKE_THR && shakeSign != +1) {
            shakeSign = +1;
            event = true;
        } else if (v < -SHAKE_THR && shakeSign != -1) {
            shakeSign = -1;
            event = true;
        } else if (fabsf(v) < SHAKE_THR * 0.5f) {
            shakeSign = 0;    // вернулись в спокойную зону — можно считать следующий рывок
        }

        if (event) {
            if (now - lastShakeEvent <= SHAKE_EVENT_GAP_MS) {
                shakeEvents++;
            } else {
                shakeEvents = 1;
            }
            lastShakeEvent = now;

            // Любой резкий рывок обнуляет накопленное укачивание
            resetRocking();

            if (shakeEvents >= SHAKE_EVENTS_NEEDED && now >= shakeRefractory) {
                shakeFlag = true;
                shakeCount++;
                shakeEvents = 0;
                shakeRefractory = now + SHAKE_REFRACTORY_MS;
            }
        }

        if (shakeEvents > 0 && now - lastShakeEvent > SHAKE_RESET_MS) {
            shakeEvents = 0;
        }
    }

    // ------------------------------------------------------------------
    // Детектор укачивания: zero-crossing с гистерезисом на сглаженном сигнале
    // ------------------------------------------------------------------
    void processRocking(uint32_t now) {
        // 1. НЧ-фильтр и ВЧ-остаток по каждой оси
        float hf[3];
        for (int i = 0; i < 3; i++) {
            gLp[i] += (gRaw[i] - gLp[i]) * ROCK_LP_ALPHA;
            hf[i] = gRaw[i] - gLp[i];
            float a = fabsf(gLp[i]);
            env[i] *= ENV_DECAY;
            if (a > env[i]) env[i] = a;
        }

        // 2. Доминирующая ось — по которой качают сильнее всего
        int newDom = 0;
        if (env[1] > env[newDom]) newDom = 1;
        if (env[2] > env[newDom]) newDom = 2;
        if (newDom != domAxis) {
            domAxis = newDom;
            resetRocking();   // ось сменилась — начинаем счет заново
        }

        float s  = gLp[domAxis];
        float ah = fabsf(hf[domAxis]);
        if (fabsf(s) > halfPeak) halfPeak = fabsf(s);
        if (ah > hfPeak) hfPeak = ah;

        // 3. Переход через ноль с гистерезисом
        int newSign = rockSign;
        if (s > ROCK_THR)       newSign = +1;
        else if (s < -ROCK_THR) newSign = -1;

        if (newSign != rockSign) {
            if (rockSign != 0) {
                // Завершился полупериод
                uint32_t hp = now - lastCrossMs;
                bool ok = (hp >= ROCK_HALF_MIN_MS) && (hp <= ROCK_HALF_MAX_MS)
                       && (halfPeak >= ROCK_MIN_PEAK) && (halfPeak <= ROCK_MAX_PEAK)
                       && (hfPeak < ROCK_MAX_HF);

                if (ok && prevHalfPeriod > 0) {
                    float ratio = (float)hp / (float)prevHalfPeriod;
                    if (ratio < ROCK_RATIO_MIN || ratio > ROCK_RATIO_MAX) ok = false;
                }

                lastAmplitude = halfPeak;

                if (ok) {
                    halfCycles++;
                    prevHalfPeriod = hp;
                } else {
                    halfCycles = 0;
                    prevHalfPeriod = 0;
                }

                if (halfCycles >= ROCK_HALF_CYCLES && now >= rockRefractory) {
                    rockingFlag = true;
                    rockingCount++;
                    halfCycles = 0;
                    prevHalfPeriod = 0;
                    rockRefractory = now + ROCK_REFRACTORY_MS;
                }
            }
            lastCrossMs = now;
            halfPeak = fabsf(s);
            hfPeak = 0.0f;
            rockSign = newSign;
        }

        // 4. Робота положили — давно нет переходов, сброс
        if (rockSign != 0 && now - lastCrossMs > ROCK_TIMEOUT_MS) {
            resetRocking();
        }
    }
} // anonymous namespace

// ======================================================================
// Публичный API
// ======================================================================
void init() {
    // Wire уже запущен в ImuSensor::init(); повторный begin на ESP32 безопасен
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);

    ready = false;
    if (probeAddress(0x68) || probeAddress(0x69)) {
        ready = true;
        ensureGyroOn();
        readGyroScale();
        Serial.printf("[MotionDetector] BMI160 найден на 0x%02X, scale=%.4f\n", i2cAddr, gyroScale);
    } else {
        Serial.println("[MotionDetector] BMI160 не найден!");
    }

    for (int i = 0; i < 3; i++) { gRaw[i] = 0; gLp[i] = 0; env[i] = 0; }
    domAxis = 1;
    resetRocking();
    rockingFlag = false;
    shakeFlag = false;
    shakeEvents = 0;
    shakeSign = 0;
    lastSample = millis();
    i2cErrors = 0;
}

void update() {
    if (!ready) return;

    uint32_t now = millis();
    if (now - lastSample < SAMPLE_MS) return;
    lastSample = now;

    if (!readGyro()) {
        i2cErrors++;
        if (i2cErrors > 50) {
            ready = false;
            Serial.println("[MotionDetector] потеря связи с BMI160");
        }
        return;
    }
    i2cErrors = 0;

    processShake(now);
    processRocking(now);
}

bool isReady() { return ready; }

bool consumeRocking() {
    bool f = rockingFlag;
    rockingFlag = false;
    return f;
}

bool consumeShake() {
    bool f = shakeFlag;
    shakeFlag = false;
    return f;
}

bool isRockingDetected() { return rockingFlag; }
bool isShakeDetected()   { return shakeFlag; }

float getGyroX() { return gLp[0]; }
float getGyroY() { return gLp[1]; }
float getGyroZ() { return gLp[2]; }
int   getDominantAxis()   { return domAxis; }
float getRockSignal()     { return gLp[domAxis]; }
int   getRockHalfCycles() { return halfCycles; }
float getRockAmplitude()  { return lastAmplitude; }
uint32_t getRockingCount() { return rockingCount; }
uint32_t getShakeCount()   { return shakeCount; }

} // namespace MotionDetector
