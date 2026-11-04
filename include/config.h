#pragma once
#include <Arduino.h>

// ============================================================================
//  ЭКРАН 1.69" IPS, ST7789V, матрица 240x280, робот работает в альбоме 280x240
// ============================================================================
#define TFT_NATIVE_W        240     // портретная ширина панели (для tft.init)
#define TFT_NATIVE_H        280     // портретная высота панели
#define SCREEN_W            280     // логическая ширина (альбом)
#define SCREEN_H            240     // логическая высота (альбом)

#define DISPLAY_ROTATION    1       // 1 или 3 — зависит от того, где шлейф. Меняем при перевёрнутой картинке
#define DISPLAY_INVERT      1       // IPS: 1 = INVON (норма). Если чёрный фон стал белым — 0
#define TFT_SPI_HZ          80000000UL  // 80 МГц: ~13.5 мс на кадр -> 50 FPS. Артефакты? -> 40000000UL и FRAME_INTERVAL_MS 33
#define TFT_BL_ACTIVE_HIGH  1
#define TFT_BL_USE_PWM      1       // 0 = просто HIGH, 1 = LEDC ШИМ (плавная яркость)
#define TFT_BL_PWM_FREQ     20000   // выше слышимого диапазона
#define TFT_BL_DEFAULT      255

// Если панель смещена (полоса мусора 20px с одной стороны) — включить и подобрать:
#define PANEL_OFFSET_OVERRIDE 0
#define PANEL_COLSTART      0
#define PANEL_ROWSTART      20
#define PANEL_COLSTART2     0
#define PANEL_ROWSTART2     20

// ============================================================================
//  КАДРОВЫЙ БУФЕР
// ============================================================================
#define FRAMEBUFFER_BYTES   ((size_t)SCREEN_W * SCREEN_H * 2)   // 134 400 байт
#define FB_MIN_HEAP_AFTER   (48 * 1024)                          // запас кучи после выделения

// ============================================================================
//  РЕНДЕР-ЦИКЛ
// ============================================================================
#define FRAME_INTERVAL_MS   20      // 50 FPS (при 40 МГц SPI ставить 33)
#define FPS_REPORT_MS       5000    // период отчёта в Serial (0 = выкл)

// ============================================================================
//  ГЕОМЕТРИЯ ЛИЦА. Единый масштаб от "старшего" робота 320x240 (коэфф. 1.0).
//  Старший: cx160 cy114 sp100 W50 H80 R24 mw16 mh4 mdy46 parX28 parY16
//  Прежний 160x80 был 0.5f. Новый 280x240 -> 0.9f (= прежнее * 1.8).
// ============================================================================
#define FACE_SCALE          0.9f

#define FACE_CX             140.0f  // SCREEN_W / 2
#define FACE_CY             114.0f  // SCREEN_H / 2 - 6 (место под рот)
#define FACE_SPACING        90.0f   // 100 * 0.9

#define EYE_W               45.0f   // 50 * 0.9
#define EYE_H               72.0f   // 80 * 0.9
#define EYE_R               22.0f   // 24 * 0.9 = 21.6

#define MOUTH_W             14.0f   // 16 * 0.9
#define MOUTH_H             4.0f    //  4 * 0.9 = 3.6
#define MOUTH_DY            42.0f   // 46 * 0.9 = 41.4 (ниже линии глаз)

// Прищур при переходе между эмоциями (prepareTransition)
#define SQUINT_H            13.0f   // 14 * 0.9
#define SQUINT_R            5.0f    //  6 * 0.9

// Параллакс IMU (используется в imu_sensor.cpp вместо старых 14/8)
#define PARALLAX_MAX_X      30.0f   // 28 * 0.9
#define PARALLAX_MAX_Y      18.0f   // 16 * 0.9

// ============================================================================
//  3-LAYER GLOW. Линейно вышло бы +1.8/+5.4, но пиксель 1.69" мельче (0.116 мм
//  против 0.135 мм) и без антиалиасинга тонкие слои "проваливаются".
// ============================================================================
#define GLOW1_PAD           3       // Glow1: x-3, y-3, w+6,  h+6,  r+3
#define GLOW2_PAD           7       // Glow2: x-7, y-7, w+14, h+14, r+7

// ============================================================================
//  IDLE-АНИМАЦИЯ
// ============================================================================
#define SACCADE_MAX_X       20.0f   // амплитуда саккад
#define SACCADE_MAX_Y       14.0f
#define SACCADE_MIN_MS      450
#define SACCADE_MAX_MS      1600
#define SACCADE_SPRING      0.22f   // пружина взгляда
#define PERSPECTIVE_K       0.18f   // "3D": ближний глаз шире на 12%, дальний уже

#define BREATH_AMP_PX       5.0f    // дыхание по высоте (было ~1.5 на 160x80)
#define BREATH_PERIOD_MS    4200

#define BLINK_MIN_H         4.0f    // высота "закрытого" глаза
#define BLINK_CLOSE_MS      70
#define BLINK_HOLD_MS       40
#define BLINK_OPEN_MS       130
#define BLINK_MIN_INTERVAL  2200
#define BLINK_MAX_INTERVAL  6500
#define BLINK_DOUBLE_PCT    15      // вероятность двойного моргания

#define EYE_SPRING          0.28f   // пружина геометрии глаз в Idle
#define GEOM_SPRING         0.15f   // возврат cx/cy/spacing после эмоций
#define MOUTH_GAZE_K        0.6f    // рот следует за взглядом на 60%

// Палитра Idle (неоново-зелёный)
#define IDLE_EYE_R   50
#define IDLE_EYE_G   255
#define IDLE_EYE_B   100
#define IDLE_G1_R    10
#define IDLE_G1_G    120
#define IDLE_G1_B    30
#define IDLE_G2_R    0
#define IDLE_G2_G    40
#define IDLE_G2_B    10

// ============================================================================
//  САНИТИ-ПРОВЕРКИ (компайл-тайм): лицо не вылезает за экран в худшем случае
// ============================================================================
static_assert(FACE_CX + FACE_SPACING / 2 + EYE_W / 2 + GLOW2_PAD + SACCADE_MAX_X + PARALLAX_MAX_X <= SCREEN_W,
              "Face exceeds right edge");
static_assert(FACE_CX - FACE_SPACING / 2 - EYE_W / 2 - GLOW2_PAD - SACCADE_MAX_X - PARALLAX_MAX_X >= 0,
              "Face exceeds left edge");
static_assert(FACE_CY + MOUTH_DY + MOUTH_H + GLOW1_PAD + SACCADE_MAX_Y + PARALLAX_MAX_Y <= SCREEN_H,
              "Mouth exceeds bottom edge");
static_assert(FACE_CY - EYE_H / 2 - GLOW2_PAD - SACCADE_MAX_Y - PARALLAX_MAX_Y >= 0,
              "Eyes exceed top edge");
