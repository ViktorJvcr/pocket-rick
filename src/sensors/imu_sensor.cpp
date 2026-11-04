#include "imu_sensor.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>

namespace ImuSensor {
namespace {

constexpr uint8_t REG_CHIP_ID    = 0x00;
constexpr uint8_t REG_PMU_STATUS = 0x03;
constexpr uint8_t REG_DATA       = 0x04;

constexpr uint8_t REG_ACC_CONF   = 0x40;
constexpr uint8_t REG_ACC_RANGE  = 0x41;
constexpr uint8_t REG_GYR_CONF   = 0x42;
constexpr uint8_t REG_GYR_RANGE  = 0x43;
constexpr uint8_t REG_CMD        = 0x7E;

constexpr uint8_t CHIP_ID = 0xD1;

constexpr uint32_t SAMPLE_MS        = 20;
constexpr uint32_t LIFT_COOLDOWN_MS  = 2000;
constexpr uint32_t SHAKE_COOLDOWN_MS = 4000;

constexpr uint16_t CALIBRATION_SAMPLES = 48;

// Настроенные диапазоны:
// ACC: +/-4g   -> 8192 LSB/g.
// GYR: +/-500  -> 65.6 LSB/(градус/с).
constexpr float ACC_SCALE = 8192.0f;
constexpr float GYR_SCALE = 65.6f;

constexpr float RAD_TO_DEG_F = 57.2957795f;

// Меняй эти знаки, если взгляд движется не в нужную сторону.
constexpr float PARALLAX_SIGN_X = -1.0f;
constexpr float PARALLAX_SIGN_Y =  1.0f;

constexpr float PARALLAX_LIMIT_X = PARALLAX_MAX_X;  // 25.0f
constexpr float PARALLAX_LIMIT_Y = PARALLAX_MAX_Y;  // 14.0f

constexpr float SHAKE_THRESHOLD = 3.0f;

struct Vec3 {
    float x;
    float y;
    float z;
};

struct FlipTracker {
    int8_t sign = 0;
    uint32_t lastStrong = 0;
    uint32_t lastFlip = 0;
};

bool connected = false;
bool calibrated = false;
uint8_t address = 0;
uint8_t readErrors = 0;

uint32_t lastSample = 0;

Vec3 acc = {};
Vec3 gyr = {};
Vec3 bias = {};
Vec3 gravity = {};

bool gravityValid = false;

float neutralTiltX = 0.0f;
float neutralTiltY = 0.0f;

float parallaxX = 0.0f;
float parallaxY = 0.0f;

// Калибровка: среднее и дисперсия по алгоритму Уэлфорда.
uint16_t calCount = 0;
Vec3 calAccMean = {};
Vec3 calAccM2 = {};
Vec3 calGyrMean = {};
Vec3 calGyrM2 = {};

// События.
bool liftFlag = false;
bool shakeFlag = false;

bool liftArmed = true;
bool liftEver = false;
bool shakeEver = false;

uint32_t lastLift = 0;
uint32_t lastShake = 0;

float shakeScore = 0.0f;
FlipTracker flips[6];

bool shakePulseSeen = false;
uint32_t lastShakePulse = 0;

float clampF(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

float length(const Vec3& v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vec3 subtract(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

void smooth(Vec3& value, const Vec3& target, float alpha) {
    value.x += (target.x - value.x) * alpha;
    value.y += (target.y - value.y) * alpha;
    value.z += (target.z - value.z) * alpha;
}

float filterAlpha(float dt, float timeConstant) {
    return dt / (timeConstant + dt);
}

int16_t readLE16(const uint8_t* p) {
    const uint16_t u =
        static_cast<uint16_t>(p[0]) |
        (static_cast<uint16_t>(p[1]) << 8);

    // Явное знаковое расширение без зависимости от преобразования
    // большого uint16_t непосредственно в int16_t.
    const int32_t signedValue =
        (u & 0x8000u) ? static_cast<int32_t>(u) - 65536
                      : static_cast<int32_t>(u);

    return static_cast<int16_t>(signedValue);
}

bool writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission(true) == 0;
}

bool readRegisters(uint8_t addr, uint8_t reg,
                   uint8_t* data, uint8_t count) {
    Wire.beginTransmission(addr);
    Wire.write(reg);

    if (Wire.endTransmission(false) != 0) {
        return false;
    }

    const size_t received =
        Wire.requestFrom(addr, count, static_cast<uint8_t>(true));

    if (received != count) {
        // Ограниченное число чтений: максимум count байтов.
        for (uint8_t i = 0; i < count && Wire.available(); ++i) {
            Wire.read();
        }
        return false;
    }

    for (uint8_t i = 0; i < count; ++i) {
        data[i] = static_cast<uint8_t>(Wire.read());
    }

    return true;
}

bool readRegister(uint8_t reg, uint8_t& value) {
    return readRegisters(address, reg, &value, 1);
}

void resetCalibration() {
    calCount = 0;
    calAccMean = {};
    calAccM2 = {};
    calGyrMean = {};
    calGyrM2 = {};
}

void accumulateAxis(float value, float& mean,
                    float& m2, uint16_t count) {
    const float delta = value - mean;
    mean += delta / static_cast<float>(count);
    m2 += delta * (value - mean);
}

void accumulate(const Vec3& value, Vec3& mean,
                Vec3& m2, uint16_t count) {
    accumulateAxis(value.x, mean.x, m2.x, count);
    accumulateAxis(value.y, mean.y, m2.y, count);
    accumulateAxis(value.z, mean.z, m2.z, count);
}

// Наклоны относительно осей платы датчика.
// Это оценка по гравитации, не полноценный AHRS.
float tiltX(const Vec3& v) {
    return atan2f(v.x, sqrtf(v.y * v.y + v.z * v.z)) *
           RAD_TO_DEG_F;
}

float tiltY(const Vec3& v) {
    return atan2f(v.y, sqrtf(v.x * v.x + v.z * v.z)) *
           RAD_TO_DEG_F;
}

void calibrate(const Vec3& rawGyr, float totalG) {
    // Калибровка допускается только в состоянии, похожем
    // на неподвижность. Движение сбрасывает набранное окно.
    const bool stationary =
        fabsf(totalG - 1.0f) < 0.10f &&
        fabsf(rawGyr.x) < 8.0f &&
        fabsf(rawGyr.y) < 8.0f &&
        fabsf(rawGyr.z) < 8.0f;

    if (!stationary) {
        resetCalibration();
        return;
    }

    ++calCount;
    accumulate(acc, calAccMean, calAccM2, calCount);
    accumulate(rawGyr, calGyrMean, calGyrM2, calCount);

    if (calCount < CALIBRATION_SAMPLES) {
        return;
    }

    const float divisor = static_cast<float>(calCount - 1);

    const float accVariance =
        (calAccM2.x + calAccM2.y + calAccM2.z) / divisor;

    const float gyrVariance =
        (calGyrM2.x + calGyrM2.y + calGyrM2.z) / divisor;

    // Проверяем не только средний модуль ускорения,
    // но и стабильность всего окна.
    if (accVariance > 0.0009f || gyrVariance > 1.5f) {
        resetCalibration();
        return;
    }

    bias = calGyrMean;
    gravity = calAccMean;
    gravityValid = true;

    // Положение при включении становится нейтральным.
    neutralTiltX = tiltX(gravity);
    neutralTiltY = tiltY(gravity);

    calibrated = true;
    liftArmed = true;
    shakeScore = 0.0f;

    for (auto& tracker : flips) {
        tracker = FlipTracker{};
    }
}

// Возвращает true только при достаточно быстрой смене
// значимого положительного/отрицательного движения.
// Мелкий шум вокруг нуля игнорируется.
bool detectFlip(FlipTracker& tracker, float value,
                float threshold, uint32_t now) {
    if (fabsf(value) < threshold) {
        return false;
    }

    const int8_t sign = value > 0.0f ? 1 : -1;

    if (tracker.sign == 0 ||
        static_cast<uint32_t>(now - tracker.lastStrong) > 300) {
        tracker.sign = sign;
        tracker.lastStrong = now;
        tracker.lastFlip = now;
        return false;
    }

    bool detected = false;

    if (sign != tracker.sign) {
        const uint32_t sinceFlip = now - tracker.lastFlip;

        // Отбрасываем слишком быстрый шум и слишком медленные
        // изменения направления.
        detected = sinceFlip >= 40 && sinceFlip <= 300;

        tracker.sign = sign;
        tracker.lastFlip = now;
    }

    tracker.lastStrong = now;
    return detected;
}

void updateGestures(uint32_t now, float dt, float totalG) {
    const float deviation = fabsf(totalG - 1.0f);

    // Гистерезис: повторно вооружаем детектор после
    // возвращения ускорения ближе к обычному.
    if (deviation < 0.35f) {
        liftArmed = true;
    }

    if (deviation > 0.65f && liftArmed) {
        liftArmed = false;

        if (!liftEver ||
            static_cast<uint32_t>(now - lastLift) >= LIFT_COOLDOWN_MS) {
            liftFlag = true;
            liftEver = true;
            lastLift = now;
        }
    }

    // Высокочастотная составляющая ускорения.
    const Vec3 highAcc = subtract(acc, gravity);

    bool reversal = false;

    // Вызываем каждый детектор, без короткого замыкания ||.
    reversal |= detectFlip(flips[0], highAcc.x, 0.22f, now);
    reversal |= detectFlip(flips[1], highAcc.y, 0.22f, now);
    reversal |= detectFlip(flips[2], highAcc.z, 0.22f, now);

    reversal |= detectFlip(flips[3], gyr.x, 35.0f, now);
    reversal |= detectFlip(flips[4], gyr.y, 35.0f, now);
    reversal |= detectFlip(flips[5], gyr.z, 35.0f, now);

    shakeScore = fmaxf(0.0f, shakeScore - 1.5f * dt);

    // Движение может одновременно проявляться на нескольких осях.
    // Оно должно давать один импульс, а не шесть.
    if (reversal &&
        (!shakePulseSeen ||
         static_cast<uint32_t>(now - lastShakePulse) >= 60)) {
        shakeScore = fminf(6.0f, shakeScore + 0.75f);
        lastShakePulse = now;
        shakePulseSeen = true;
    }

    if (shakeScore >= SHAKE_THRESHOLD &&
        (!shakeEver ||
         static_cast<uint32_t>(now - lastShake) >= SHAKE_COOLDOWN_MS)) {
        shakeFlag = true;
        shakeEver = true;
        lastShake = now;
        shakeScore = 0.0f;
    }
}

void updateParallax(float dt) {
    // Вычисляем наклоны
    const float angleX = tiltX(gravity) - neutralTiltX;
    const float angleY = tiltY(gravity) - neutralTiltY;

    // Альбомная ориентация:
    // angleY рулит горизонталью (X), angleX рулит вертикалью (Y).
    // Коэффициенты увеличены с 0.45f до 1.3f (глаза сразу идут за рукой):
    const float targetX = clampF(
        PARALLAX_SIGN_X * (angleY * 1.30f - gyr.x * 0.06f),
        -PARALLAX_LIMIT_X, PARALLAX_LIMIT_X
    );

    const float targetY = clampF(
        PARALLAX_SIGN_Y * (angleX * 1.10f - gyr.y * 0.05f),
        -PARALLAX_LIMIT_Y, PARALLAX_LIMIT_Y
    );

    // Быстрый LPF (0.035f вместо 0.10f): реакция моментальная, без ощущения «резины»
    const float alpha = filterAlpha(dt, 0.035f);

    parallaxX += (targetX - parallaxX) * alpha;
    parallaxY += (targetY - parallaxY) * alpha;
}



void resetRuntime() {
    connected = false;
    calibrated = false;
    address = 0;
    readErrors = 0;

    acc = {};
    gyr = {};
    bias = {};
    gravity = {};
    gravityValid = false;

    neutralTiltX = 0.0f;
    neutralTiltY = 0.0f;
    parallaxX = 0.0f;
    parallaxY = 0.0f;

    liftFlag = false;
    shakeFlag = false;
    liftArmed = true;
    liftEver = false;
    shakeEver = false;

    lastLift = 0;
    lastShake = 0;

    shakeScore = 0.0f;
    shakePulseSeen = false;
    lastShakePulse = 0;

    resetCalibration();

    for (auto& tracker : flips) {
        tracker = FlipTracker{};
    }
}

} // namespace

bool init() {
    resetRuntime();

    // Wire.begin(SDA, SCL) вызывается один раз в setup().
    // Ограничиваем ожидание при неисправной/отключенной шине.
    Wire.setTimeOut(5);

    const uint8_t candidates[] = {0x68, 0x69};

    for (uint8_t candidate : candidates) {
        uint8_t id = 0;

        if (readRegisters(candidate, REG_CHIP_ID, &id, 1) &&
            id == CHIP_ID) {
            address = candidate;
            break;
        }
    }

    if (address == 0) {
        return false;
    }

    // Задержки разрешены только здесь, при запуске оборудования.
    if (!writeRegister(REG_CMD, 0xB6)) { // Soft reset
        return false;
    }
    delay(15);

    uint8_t id = 0;
    if (!readRegister(REG_CHIP_ID, id) || id != CHIP_ID) {
        return false;
    }

    if (!writeRegister(REG_CMD, 0x11)) { // ACC normal
        return false;
    }
    delay(5);

    if (!writeRegister(REG_CMD, 0x15)) { // GYR normal
        return false;
    }
    delay(85);

    // 0x27: normal filtering, ODR 50 Hz.
    if (!writeRegister(REG_ACC_CONF, 0x27) ||
        !writeRegister(REG_ACC_RANGE, 0x05) || // +/-4g
        !writeRegister(REG_GYR_CONF, 0x27) ||
        !writeRegister(REG_GYR_RANGE, 0x02)) { // +/-500 dps
        return false;
    }

    delay(25);

    uint8_t pmu = 0;
    if (!readRegister(REG_PMU_STATUS, pmu)) {
        return false;
    }

    // ACC normal: bits 5:4 = 01.
    // GYR normal: bits 3:2 = 01.
    if ((pmu & 0x3C) != 0x14) {
        return false;
    }

    // Проверяем, что диапазоны действительно записались.
    uint8_t accRange = 0;
    uint8_t gyrRange = 0;

    if (!readRegister(REG_ACC_RANGE, accRange) ||
        !readRegister(REG_GYR_RANGE, gyrRange) ||
        (accRange & 0x0F) != 0x05 ||
        (gyrRange & 0x07) != 0x02) {
        return false;
    }

    connected = true;
    lastSample = millis();
    return true;
}

void update(uint32_t now) {
    const uint32_t elapsed = now - lastSample;

    if (elapsed < SAMPLE_MS) {
        return;
    }

    lastSample = now;

    // Не догоняем пропущенные кадры циклом.
    const float dt =
        clampF(static_cast<float>(elapsed) * 0.001f, 0.001f, 0.10f);

    if (!connected) {
        // При потере датчика взгляд спокойно возвращается в центр.
        const float alpha = filterAlpha(dt, 0.20f);
        parallaxX += (0.0f - parallaxX) * alpha;
        parallaxY += (0.0f - parallaxY) * alpha;
        return;
    }

    // Один burst-read всего диапазона 0x04..0x17:
    // 0x04..0x0B — данные внешнего магнитометра, игнорируются;
    // 0x0C..0x11 — GYR XYZ;
    // 0x12..0x17 — ACC XYZ.
    uint8_t data[20];

    if (!readRegisters(address, REG_DATA, data, sizeof(data))) {
        if (!calibrated) {
            resetCalibration();
        }

        if (++readErrors >= 5) {
            connected = false;
            calibrated = false;
            liftFlag = false;
            shakeFlag = false;
        }
        return;
    }

    readErrors = 0;

    Vec3 rawGyr = {
        readLE16(&data[8])  / GYR_SCALE,
        readLE16(&data[10]) / GYR_SCALE,
        readLE16(&data[12]) / GYR_SCALE
    };

    acc = {
        readLE16(&data[14]) / ACC_SCALE,
        readLE16(&data[16]) / ACC_SCALE,
        readLE16(&data[18]) / ACC_SCALE
    };

    // Если датчик установлен повернутым относительно экрана,
    // переназначать оси нужно здесь — для acc И rawGyr одинаково.

    const float totalG = length(acc);

    if (!gravityValid) {
        gravity = acc;
        gravityValid = true;
    }

    if (!calibrated) {
        gyr = rawGyr;
        calibrate(rawGyr, totalG);

        if (calibrated) {
            gyr = subtract(rawGyr, bias);
        }

        // Во время калибровки не выдаем жесты и параллакс.
        return;
    }

    gyr = subtract(rawGyr, bias);

    // Во время сильных линейных ускорений не считаем их
    // изменением направления гравитации.
    if (totalG > 0.50f && totalG < 1.80f) {
        smooth(gravity, acc, filterAlpha(dt, 0.06f));
    }

    // Если калибровка еще идет, мы всё равно берем сырой гироскоп и обновляем параллакс,
    // чтобы глаза оживали сразу при старте!
    if (!calibrated) {
        gyr = rawGyr;
        calibrate(rawGyr, totalG);
    } else {
        gyr = subtract(rawGyr, bias);
    }

    updateParallax(dt);
    if (calibrated) {
        updateGestures(now, dt, totalG);
    }

    updateParallax(dt);
    updateGestures(now, dt, totalG);
}

bool isConnected() {
    return connected;
}

uint8_t getAddress() {
    return address;
}

bool isCalibrated() {
    return calibrated;
}

float getAccX() { return acc.x; }
float getAccY() { return acc.y; }
float getAccZ() { return acc.z; }

float getGyrX() { return gyr.x; }
float getGyrY() { return gyr.y; }
float getGyrZ() { return gyr.z; }

float getParallaxX() { return parallaxX; }
float getParallaxY() { return parallaxY; }

bool isSuddenLiftDetected() {
    const bool result = liftFlag;
    liftFlag = false;
    return result;
}

bool isShakeDetected() {
    const bool result = shakeFlag;
    shakeFlag = false;
    return result;
}

} // namespace ImuSensor
