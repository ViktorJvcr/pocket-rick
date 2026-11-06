#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

namespace FaceState {

    struct State {
        // Геометрия лица (текущие физические координаты)
        float cx = 120.0f;
        float cy = 100.0f;
        float spacing = 100.0f;
        float lw = 50.0f;
        float rw = 50.0f;
        float lh = 80.0f;
        float rh = 80.0f;
        float mw = 15.0f;
        float mh = 4.0f;

        // Текущие плавающие RGB компоненты для плавного перелива цвета
        float eyeR = 50.0f,  eyeG = 255.0f, eyeB = 100.0f;
        float gl1R = 10.0f,  gl1G = 120.0f, gl1B = 30.0f;
        float gl2R = 0.0f,   gl2G = 40.0f,  gl2B = 10.0f;

        // Целевые RGB компоненты текущей эмоции
        float t_eyeR = 50.0f,  t_eyeG = 255.0f, t_eyeB = 100.0f;
        float t_gl1R = 10.0f,  t_gl1G = 120.0f, t_gl1B = 30.0f;
        float t_gl2R = 0.0f,   t_gl2G = 40.0f,  t_gl2B = 10.0f;

        // Готовые 16-битные цвета RGB565 для отрисовки на текущем кадре
        uint16_t colorEye   = 0;
        uint16_t colorGlow1 = 0;
        uint16_t colorGlow2 = 0;
    };

    // Глобальный экземпляр состояния лица
    extern State current;

    // Инициализация базовых цветов
    void init(TFT_eSPI &tft);

    // Задать целевую палитру эмоции (вызывается в reset() каждой анимации)
    void setTargetPalette(uint8_t eR, uint8_t eG, uint8_t eB,
                          uint8_t g1R, uint8_t g1G, uint8_t g1B,
                          uint8_t g2R, uint8_t g2G, uint8_t g2B);

    // Плавное обновление цвета (вызывается на каждом кадре)
    void updateColors(TFT_eSPI &tft, float speed = 0.14f);

    // Подготовка к переходу (blink = true делает рефлекторный прищур при смене эмоции)
    void prepareTransition(bool blink = true);

    // Сохранить текущие координаты из активной анимации в общее состояние
    void saveGeometry(float cx, float cy, float spacing,
                      float lw, float rw, float lh, float rh,
                      float mw, float mh);

    // Загрузить координаты из общего состояния в новую анимацию при ее старте
    void loadGeometry(float &cx, float &cy, float &spacing,
                      float &lw, float &rw, float &lh, float &rh,
                      float &mw, float &mh);

} // namespace FaceState
