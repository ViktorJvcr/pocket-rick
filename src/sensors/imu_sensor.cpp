#include "imu_sensor.h"
#include <Wire.h>
#include <math.h>
#include "pins.h"

namespace ImuSensor {
namespace {
    constexpr uint8_t REG_CHIP_ID   = 0x00; // Ожидается 0xD1
    constexpr uint8_t REG_DATA_GYR  = 0x0C; // Начало 12 байт (GYR_X..GYR_Z, ACC_X..ACC_Z)
    constexpr uint8_t REG_ACC_CONF  = 0x40; // ODR и фильтрация акселерометра
    constexpr uint8_t REG_ACC_RANGE = 0x41; // Диапазон акселерометра
    constexpr uint8_t REG_GYR_CONF  = 0x42; // ODR и фильтрация гироскопа
    constexpr uint8_t REG_GYR_RANGE = 0x43; // Диапазон гироскопа
    constexpr uint8_t REG_CMD       = 0x7E; // Командный регистр PMU

    constexpr uint8_t CMD_SOFT_RESET = 0xB6;
    constexpr uint8_t CMD_ACC_NORMAL = 0x11;
    constexpr uint8_t CMD_GYR_NORMAL = 0x15;

    uint8_t bmi160Address = 0x68;

    constexpr float ACC_SCALE = 1.0f / 8192.0f;
    constexpr float GYR_SCALE = 1.0f / 65.6f;

    constexpr float DIR_X = 1.0f;
    constexpr float DIR_Y = 1.0f;

    constexpr float TILT_GAIN_X  = 70.0f;
    constexpr float TILT_GAIN_Y  = 50.0f;
    constexpr float GYRO_GAIN_X  = 0.25f;
    constexpr float GYRO_GAIN_Y  = 0.20f;

    constexpr float MAX_OFFSET_X = 45.0f;
    constexpr float MAX_OFFSET_Y = 24.0f;

    bool sensorReady = false;
    bool suddenLiftFlag = false;
    bool shakeDetectedFlag = false;
    bool baselineInitialized = false;

    unsigned long lastPollTime = 0;
    unsigned long lastLiftTriggerTime = 0;
    unsigned long lastShakeTriggerTime = 0;

    constexpr unsigned long POLL_INTERVAL_MS = 20;        // 50 Гц
    constexpr unsigned long LIFT_COOLDOWN_MS = 4000;
    constexpr unsigned long SHAKE_COOLDOWN_MS = 6500;
    constexpr float LIFT_G_THRESHOLD = 1.85f;

    float shakeEnergy = 0.0f;
    constexpr float SHAKE_TRIGGER_LEVEL = 100.0f;

    float baseAx = 0.0f, baseAy = 0.0f, baseAz = 1.0f;
    float baseGx = 0.0f, baseGy = 0.0f, baseGz = 0.0f;

    float parallaxX = 0.0f;
    float parallaxY = 0.0f;

    void writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
        Wire.beginTransmission(addr);
        Wire.write(reg);
        Wire.write(val);
        Wire.endTransmission(true);
    }

    uint8_t readReg(uint8_t addr, uint8_t reg) {
        Wire.beginTransmission(addr);
        Wire.write(reg);
        if (Wire.endTransmission(true) != 0) return 0xFF;
        uint8_t readCount = Wire.requestFrom((uint8_t)addr, (uint8_t)1);
        if (readCount > 0 && Wire.available()) {
            return Wire.read();
        }
        return 0xFF;
    }

    float clampVal(float v, float minV, float maxV) {
        if (v < minV) return minV;
        if (v > maxV) return maxV;
        return v;
    }
} // anonymous namespace

void init() {
    Serial.println("[IMU] Инициализация шины I2C (SDA=6, SCL=7)...");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);
    Wire.setTimeOut(10); // Защита от зависания при отсутствии ответа на шине
    delay(20);

    // 1. Автопоиск адреса BMI160 (0x68 или 0x69)
    uint8_t id = readReg(0x68, REG_CHIP_ID);
    if (id == 0xD1) {
        bmi160Address = 0x68;
        sensorReady = true;
    } else {
        id = readReg(0x69, REG_CHIP_ID);
        if (id == 0xD1) {
            bmi160Address = 0x69;
            sensorReady = true;
        } else {
            sensorReady = false;
            Serial.printf("[IMU] BMI160 не найден на 0x68/0x69 (код ответа: 0x%02X). Робот работает без IMU.\n", id);
            return;
        }
    }

    Serial.printf("[IMU] BMI160 обнаружен по адресу 0x%02X, CHIP_ID=0xD1\n", bmi160Address);

    // 2. Мягкий сброс
    writeReg(bmi160Address, REG_CMD, CMD_SOFT_RESET);
    delay(50);

    // 3. Включение сенсоров
    writeReg(bmi160Address, REG_CMD, CMD_ACC_NORMAL);
    delay(10);
    writeReg(bmi160Address, REG_CMD, CMD_GYR_NORMAL);
    delay(60);

    // 4. Настройка ODR 50 Гц, +/-4g, +/-500 dps
    writeReg(bmi160Address, REG_ACC_CONF, 0x27);
    writeReg(bmi160Address, REG_ACC_RANGE, 0x05);
    writeReg(bmi160Address, REG_GYR_CONF, 0x27);
    writeReg(bmi160Address, REG_GYR_RANGE, 0x02);

    delay(20);
    Serial.println("[IMU] BMI160 готов к работе (Акселерометр +/-4g, Гироскоп +/-500dps, 50Hz)");
}

void update() {
    if (!sensorReady) return;

    unsigned long now = millis();
    if (now - lastPollTime < POLL_INTERVAL_MS) return;
    lastPollTime = now;

    Wire.beginTransmission(bmi160Address);
    Wire.write(REG_DATA_GYR);
    if (Wire.endTransmission(false) != 0) return;

    uint8_t count = Wire.requestFrom((uint8_t)bmi160Address, (uint8_t)12);
    if (count == 12) {
        int16_t rawGx = (int16_t)(Wire.read() | (Wire.read() << 8));
        int16_t rawGy = (int16_t)(Wire.read() | (Wire.read() << 8));
        int16_t rawGz = (int16_t)(Wire.read() | (Wire.read() << 8));

        int16_t rawAx = (int16_t)(Wire.read() | (Wire.read() << 8));
        int16_t rawAy = (int16_t)(Wire.read() | (Wire.read() << 8));
        int16_t rawAz = (int16_t)(Wire.read() | (Wire.read() << 8));

        float ax = rawAx * ACC_SCALE;
        float ay = rawAy * ACC_SCALE;
        float az = rawAz * ACC_SCALE;

        float gx = rawGx * GYR_SCALE;
        float gy = rawGy * GYR_SCALE;
        float gz = rawGz * GYR_SCALE;

        if (!baselineInitialized) {
            baseAx = ax; baseAy = ay; baseAz = az;
            baseGx = gx; baseGy = gy; baseGz = gz;
            baselineInitialized = true;
            return;
        }

        baseAx += (ax - baseAx) * 0.0025f;
        baseAy += (ay - baseAy) * 0.0025f;
        baseAz += (az - baseAz) * 0.0025f;

        baseGx += (gx - baseGx) * 0.02f;
        baseGy += (gy - baseGy) * 0.02f;
        baseGz += (gz - baseGz) * 0.02f;

        float cleanGx = gx - baseGx;
        float cleanGy = gy - baseGy;
        float cleanGz = gz - baseGz;

        float totalG = sqrtf(ax * ax + ay * ay + az * az);
        float dynamicG = fabsf(totalG - 1.0f);

        // 1. ТРЯСКА (DIZZY)
        if (dynamicG > 0.65f) {
            float add = (dynamicG - 0.50f) * 14.0f;
            if (add > 18.0f) add = 18.0f;
            shakeEnergy += add;
        } else {
            shakeEnergy -= 3.0f;
            if (shakeEnergy < 0.0f) shakeEnergy = 0.0f;
        }

        if (shakeEnergy >= SHAKE_TRIGGER_LEVEL && (now - lastShakeTriggerTime > SHAKE_COOLDOWN_MS)) {
            shakeDetectedFlag = true;
            shakeEnergy = 0.0f;
            lastShakeTriggerTime = now;
            lastLiftTriggerTime = now;
        }

        // 2. РЕЗКИЙ ПОДЪЕМ (SCARED)
        if (totalG > LIFT_G_THRESHOLD &&
            (now - lastLiftTriggerTime > LIFT_COOLDOWN_MS) &&
            (now - lastShakeTriggerTime > SHAKE_COOLDOWN_MS)) {
            suddenLiftFlag = true;
            lastLiftTriggerTime = now;
        }

        // 3. ПАРАЛЛАКС
        float deltaAx = ax - baseAx;
        float deltaAy = ay - baseAy;
        float deltaAz = az - baseAz;

        float rawTargetX = DIR_X * (deltaAy * TILT_GAIN_X - (cleanGz + cleanGx) * GYRO_GAIN_X);
        float rawTargetY = DIR_Y * ((deltaAx - deltaAz) * TILT_GAIN_Y + cleanGy * GYRO_GAIN_Y);

        rawTargetX = clampVal(rawTargetX, -MAX_OFFSET_X, MAX_OFFSET_X);
        rawTargetY = clampVal(rawTargetY, -MAX_OFFSET_Y, MAX_OFFSET_Y);

        parallaxX += (rawTargetX - parallaxX) * 0.35f;
        parallaxY += (rawTargetY - parallaxY) * 0.35f;
    }
}

bool isSuddenLiftDetected() {
    if (suddenLiftFlag) {
        suddenLiftFlag = false;
        return true;
    }
    return false;
}

bool isShakeDetected() {
    if (shakeDetectedFlag) {
        shakeDetectedFlag = false;
        return true;
    }
    return false;
}

float getParallaxX() {
    return parallaxX;
}

float getParallaxY() {
    return parallaxY;
}

bool isReady() {
    return sensorReady;
}

} // namespace ImuSensor
