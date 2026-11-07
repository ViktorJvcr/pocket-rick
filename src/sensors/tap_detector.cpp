#include "tap_detector.h"
#include "pins.h"
#include <Wire.h>
#include <math.h>

namespace TapDetector {

namespace {
    // ------------------------------------------------------------------
    // BMI160: регистры
    // ------------------------------------------------------------------
    const uint8_t BMI160_ADDR_A    = 0x68;
    const uint8_t BMI160_ADDR_B    = 0x69;
    const uint8_t REG_CHIP_ID      = 0x00;   // ожидаем 0xD1
    const uint8_t REG_ACC_DATA     = 0x12;   // X LSB, X MSB, Y LSB, Y MSB, Z LSB, Z MSB
    const uint8_t REG_ACC_CONF     = 0x40;
    const uint8_t REG_CMD          = 0x7E;
    const uint8_t CHIP_ID_VALUE    = 0xD1;

    // ODR 1600 Гц, фильтр normal (bwp = 2): 0b0010_1100.
    // Высокая частота нужна, чтобы «пачка» чтений внутри одного кадра
    // давала реально РАЗНЫЕ семплы, а не повторы одного и того же.
    const uint8_t ACC_CONF_1600HZ  = 0x2C;

    const float   LSB_PER_G        = 16384.0f; // диапазон ±2g (по умолчанию BMI160)

    // ------------------------------------------------------------------
    // Параметры детектора
    // ------------------------------------------------------------------
    // Порог рывка (разность ускорений между двумя соседними семплами).
    // Стук пальцем по пластику дает 0.8..3.0g, наклон/поворот робота — < 0.1g.
    const float    TAP_JERK_THRESHOLD_G = 0.65f;

    // Антидребезг: после удара корпус «звенит» 20..80 мс, плюс отскок пальца.
    const uint32_t DEAD_TIME_MS         = 150;

    // Окно серии: следующий удар должен прийти не позже, чем через это время.
    const uint32_t COMBO_WINDOW_MS      = 850;

    const uint8_t  COMBO_MAX            = 5;

    // Сколько семплов читать за один вызов update() и с каким шагом.
    // loop() идет ~60 FPS (16 мс), удар длится 2..6 мс — одним чтением за кадр
    // его легко проскочить. Пачка из 3 чтений с шагом ~1 мс занимает ~2.5 мс
    // и заметно повышает вероятность поймать пик.
    const uint8_t  BURST_SAMPLES        = 3;
    const uint32_t BURST_GAP_US         = 1000;

    // Знаки направления. Удар слева толкает корпус ВПРАВО — ускорение
    // направлено от точки удара. Нам нужно "откуда пришел удар", поэтому
    // базовый знак -1. Если взгляд уходит не в ту сторону — поменяйте знак.
    const float    TAP_DIR_X            = -1.0f;
    const float    TAP_DIR_Y            = -1.0f;

    // Первые миллисекунды после старта игнорируем: датчик просыпается,
    // первые чтения могут быть нулями, а скачок 0 -> 1g выглядит как удар.
    const uint32_t ARM_DELAY_MS         = 600;

    // ------------------------------------------------------------------
    // Состояние
    // ------------------------------------------------------------------
    uint8_t  devAddr        = 0;
    bool     ready          = false;
    uint32_t startTime      = 0;

    bool     havePrev       = false;
    float    prevAx = 0.0f, prevAy = 0.0f, prevAz = 0.0f;

    uint32_t lastTapTime    = 0;       // для DEAD_TIME и окна серии
    uint8_t  comboCount     = 0;
    bool     tapPending     = false;   // флаг для consumeTap()

    float    tapDirX        = 0.0f;
    float    tapDirY        = 0.0f;
    float    lastJerk       = 0.0f;

    // ------------------------------------------------------------------
    // I2C helpers
    // ------------------------------------------------------------------
    bool readReg(uint8_t reg, uint8_t *buf, uint8_t len) {
        Wire.beginTransmission(devAddr);
        Wire.write(reg);
        if (Wire.endTransmission(false) != 0) return false;
        uint8_t got = Wire.requestFrom((int)devAddr, (int)len);
        if (got != len) return false;
        for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
        return true;
    }

    bool writeReg(uint8_t reg, uint8_t val) {
        Wire.beginTransmission(devAddr);
        Wire.write(reg);
        Wire.write(val);
        return Wire.endTransmission() == 0;
    }

    bool probeAddr(uint8_t addr) {
        devAddr = addr;
        uint8_t id = 0;
        if (!readReg(REG_CHIP_ID, &id, 1)) return false;
        return id == CHIP_ID_VALUE;
    }

    bool readAccelG(float &ax, float &ay, float &az) {
        uint8_t b[6];
        if (!readReg(REG_ACC_DATA, b, 6)) return false;
        int16_t rx = (int16_t)((b[1] << 8) | b[0]);
        int16_t ry = (int16_t)((b[3] << 8) | b[2]);
        int16_t rz = (int16_t)((b[5] << 8) | b[4]);
        ax = rx / LSB_PER_G;
        ay = ry / LSB_PER_G;
        az = rz / LSB_PER_G;
        return true;
    }

    // ------------------------------------------------------------------
    // Регистрация удара
    // ------------------------------------------------------------------
    void registerTap(float dX, float dY, float jerk, uint32_t now) {
        // Серия: если предыдущий удар был недавно — продолжаем, иначе начинаем заново.
        if (comboCount > 0 && (now - lastTapTime) <= COMBO_WINDOW_MS) {
            if (comboCount < COMBO_MAX) comboCount++;
        } else {
            comboCount = 1;
        }

        lastTapTime = now;
        lastJerk    = jerk;

        // Направление удара в плоскости экрана (dZ — удар "в лицо", направления не дает).
        float len = sqrtf(dX * dX + dY * dY);
        if (len > 0.05f) {
            tapDirX = (dX / len) * TAP_DIR_X;
            tapDirY = (dY / len) * TAP_DIR_Y;
        } else {
            tapDirX = 0.0f;
            tapDirY = 0.0f;
        }

        tapPending = true;

        Serial.printf("[TAP] jerk=%.2fg dir=(%.2f, %.2f) combo=%u\n",
                      jerk, tapDirX, tapDirY, (unsigned)comboCount);
    }

    // Обработка одного семпла. Фильтр считается ВСЕГДА, без заморозок.
    void processSample(float ax, float ay, float az, uint32_t now) {
        if (!havePrev) {
            prevAx = ax; prevAy = ay; prevAz = az;
            havePrev = true;
            return;
        }

        // Разностный фильтр первой производной (ФВЧ / jerk).
        float dX = ax - prevAx;
        float dY = ay - prevAy;
        float dZ = az - prevAz;

        // Обновляем prev на КАЖДОМ семпле — это и есть исправление бага.
        prevAx = ax; prevAy = ay; prevAz = az;

        float jerkMag = sqrtf(dX * dX + dY * dY + dZ * dZ);

        // Мертвое время после удара: затухающие колебания корпуса не считаем новыми ударами.
        if (lastTapTime != 0 && (now - lastTapTime) < DEAD_TIME_MS) return;

        // Защита от стартовых нулей датчика.
        if ((now - startTime) < ARM_DELAY_MS) return;

        if (jerkMag >= TAP_JERK_THRESHOLD_G) {
            registerTap(dX, dY, jerkMag, now);
        }
    }
} // anonymous namespace

// ----------------------------------------------------------------------
// Публичный интерфейс
// ----------------------------------------------------------------------
void init() {
    startTime  = millis();
    ready      = false;
    havePrev   = false;
    comboCount = 0;
    tapPending = false;
    lastTapTime = 0;

    // Шина уже поднята ImuSensor::init(); повторный begin на тех же пинах безопасен.
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);

    if (probeAddr(BMI160_ADDR_A) || probeAddr(BMI160_ADDR_B)) {
        ready = true;
        // Поднимаем частоту акселерометра. Для ImuSensor это безвредно:
        // он просто будет получать более свежие данные.
        writeReg(REG_ACC_CONF, ACC_CONF_1600HZ);
        Serial.printf("[TAP] BMI160 found at 0x%02X, jerk threshold %.2fg, dead %lu ms, combo window %lu ms\n",
                      devAddr, TAP_JERK_THRESHOLD_G,
                      (unsigned long)DEAD_TIME_MS, (unsigned long)COMBO_WINDOW_MS);
    } else {
        devAddr = 0;
        Serial.println("[TAP] BMI160 NOT found - tap detection disabled");
    }
}

void update() {
    if (!ready) return;

    uint32_t now = millis();

    // Пачка быстрых чтений внутри одного кадра, чтобы не проскочить короткий импульс.
    for (uint8_t i = 0; i < BURST_SAMPLES; i++) {
        float ax, ay, az;
        if (readAccelG(ax, ay, az)) {
            processSample(ax, ay, az, now);
        }
        if (i + 1 < BURST_SAMPLES) delayMicroseconds(BURST_GAP_US);
    }

    // Серия закончилась — окно истекло без нового удара.
    if (comboCount > 0 && (now - lastTapTime) > COMBO_WINDOW_MS) {
        Serial.printf("[TAP] combo finished: %u\n", (unsigned)comboCount);
        comboCount = 0;
    }
}

bool consumeTap() {
    if (!tapPending) return false;
    tapPending = false;
    return true;
}

uint8_t getComboCount() { return comboCount; }

bool isComboActive() {
    return comboCount > 0 && (millis() - lastTapTime) <= COMBO_WINDOW_MS;
}

float getTapDirX()  { return tapDirX; }
float getTapDirY()  { return tapDirY; }
float getLastJerk() { return lastJerk; }
bool  isReady()     { return ready; }

} // namespace TapDetector
