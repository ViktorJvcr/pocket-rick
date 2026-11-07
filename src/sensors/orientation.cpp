#include "orientation.h"
#include <Wire.h>
#include <math.h>

namespace Orientation {

namespace {
    const uint8_t REG_CHIP_ID    = 0x00;
    const uint8_t REG_PMU_STATUS = 0x03;
    const uint8_t REG_ACC_DATA   = 0x12;   // ACC_X_LSB .. ACC_Z_MSB (6 байт)
    const uint8_t REG_CMD        = 0x7E;
    const uint8_t CMD_ACC_NORMAL = 0x11;
    const uint8_t CHIP_ID_BMI160 = 0xD1;

    uint8_t  i2cAddr    = 0;      // 0 — датчик не найден
    bool     haveSample = false;
    uint32_t lastSampleMs = 0;

    // Отфильтрованный нормализованный вектор гравитации
    float gx = 0.0f, gy = 1.0f, gz = 0.0f;
    float magFilt = 0.0f;         // средняя длина вектора (≈1g в "сырых" единицах)

    float upDot    = 1.0f;
    float sideTilt = 0.0f;
    bool  upsideDown = false;
    uint32_t downSince = 0;

    bool readRegs(uint8_t reg, uint8_t *buf, uint8_t len) {
        Wire.beginTransmission(i2cAddr);
        Wire.write(reg);
        if (Wire.endTransmission(false) != 0) return false;
        uint8_t got = Wire.requestFrom((int)i2cAddr, (int)len);
        if (got != len) return false;
        for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
        return true;
    }

    bool writeReg(uint8_t reg, uint8_t val) {
        Wire.beginTransmission(i2cAddr);
        Wire.write(reg);
        Wire.write(val);
        return Wire.endTransmission() == 0;
    }

    bool probe(uint8_t addr) {
        i2cAddr = addr;
        uint8_t id = 0;
        if (!readRegs(REG_CHIP_ID, &id, 1)) { i2cAddr = 0; return false; }
        if (id != CHIP_ID_BMI160) { i2cAddr = 0; return false; }
        return true;
    }
} // anonymous namespace

void init() {
    haveSample = false;
    upsideDown = false;
    downSince  = 0;
    magFilt    = 0.0f;

    if (!probe(0x68) && !probe(0x69)) {
        Serial.println("[ORI] BMI160 not found, orientation disabled");
        return;
    }

    // Убеждаемся, что акселерометр в режиме Normal (биты 5:4 PMU_STATUS == 01)
    uint8_t pmu = 0;
    if (readRegs(REG_PMU_STATUS, &pmu, 1)) {
        uint8_t accMode = (pmu >> 4) & 0x03;
        if (accMode != 0x01) {
            writeReg(REG_CMD, CMD_ACC_NORMAL);
            delay(5);
        }
    }
    Serial.printf("[ORI] BMI160 at 0x%02X ready\n", i2cAddr);
}

void feedRaw(float ax, float ay, float az) {
    float mag = sqrtf(ax * ax + ay * ay + az * az);
    if (mag < 1e-3f) return;

    // Отбраковка ударов и встрясок: длина вектора далека от средней 1g
    if (magFilt <= 0.0f) {
        magFilt = mag;
    } else {
        float ratio = mag / magFilt;
        if (ratio > 1.5f || ratio < 0.55f) return;
        magFilt += (mag - magFilt) * 0.02f;
    }

    float nx = ax / mag, ny = ay / mag, nz = az / mag;

    if (!haveSample) {
        gx = nx; gy = ny; gz = nz;
        haveSample = true;
    } else {
        gx += (nx - gx) * ORI_LPF_ALPHA;
        gy += (ny - gy) * ORI_LPF_ALPHA;
        gz += (nz - gz) * ORI_LPF_ALPHA;
    }

    // Повторная нормализация после фильтра
    float gm = sqrtf(gx * gx + gy * gy + gz * gz);
    if (gm > 1e-3f) { gx /= gm; gy /= gm; gz /= gm; }

    float comp[3] = { gx, gy, gz };
    upDot    = comp[ORI_UP_AXIS] * ORI_UP_SIGN;
    // Вектор реакции опоры направлен к верху. Если правый край ниже — "верх" смотрит влево,
    // компонента по оси "вправо" отрицательна, поэтому инвертируем:
    sideTilt = -comp[ORI_SIDE_AXIS] * ORI_SIDE_SIGN;
    if (sideTilt < -1.0f) sideTilt = -1.0f;
    if (sideTilt >  1.0f) sideTilt =  1.0f;

    uint32_t now = millis();
    if (!upsideDown) {
        if (upDot < ORI_DOWN_ENTER) { upsideDown = true; downSince = now; }
    } else {
        if (upDot > ORI_DOWN_EXIT)  { upsideDown = false; downSince = 0; }
    }
}

void update() {
#if ORI_INTERNAL_I2C_READ
    if (i2cAddr == 0) return;
    uint32_t now = millis();
    if (now - lastSampleMs < ORI_SAMPLE_MS) return;
    lastSampleMs = now;

    uint8_t b[6];
    if (!readRegs(REG_ACC_DATA, b, 6)) return;
    int16_t rx = (int16_t)(((uint16_t)b[1] << 8) | b[0]);
    int16_t ry = (int16_t)(((uint16_t)b[3] << 8) | b[2]);
    int16_t rz = (int16_t)(((uint16_t)b[5] << 8) | b[4]);
    feedRaw((float)rx, (float)ry, (float)rz);
#endif
}

bool isReady()        { return haveSample; }
bool isUpsideDown()   { return haveSample && upsideDown; }
float getUpDot()      { return upDot; }
float getSideTilt()   { return sideTilt; }

uint32_t getUpsideDownMs() {
    if (!upsideDown || downSince == 0) return 0;
    return millis() - downSince;
}

void getGravity(float &ox, float &oy, float &oz) {
    ox = gx; oy = gy; oz = gz;
}

} // namespace Orientation
