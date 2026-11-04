#pragma once
// =============================================================================
//  AnimIdle — анимация покоя Pocket Rick (ST7789 280x240, 50 FPS)
//
//  Что внутри:
//   • органическое асимметричное дыхание (вдох ~1.4с, пауза, выдох ~2.2с);
//   • саккады с короткими/длинными фиксациями, микро-тремор, медленный дрейф,
//     коррекционные микро-саккады, 3D-перспектива (ближний глаз крупнее);
//   • моргание с быстрым закрытием и мягким открытием, иногда двойное;
//   • микро-поведения: прищуры (одним глазом / «улыбка глазами»),
//     свист в потолок, демонстративное закатывание глаз, глубокий вздох/зевок;
//   • планировщик поведений с весами и «скукой» (чем дольше не трогают —
//     тем чаще закатывает глаза).
//
//  Все переходы — пружинная физика current += (target - current) * k.
//  Никаких delay(), никаких аллокаций в куче, только float и millis().
//  Все настраиваемые тайминги/амплитуды — в «ПАНЕЛИ НАСТРОЕК» anim_idle.cpp.
// =============================================================================
#include <Arduino.h>
#include "../display/display.h"

class AnimIdle {
public:
    // Микро-состояния покоя. Публичны, чтобы менеджер мог их читать/запускать.
    enum Mode : uint8_t {
        IDLE_NORMAL = 0,   // саккады + дыхание + моргание
        IDLE_SQUINT,       // прищур (один глаз подозрительно / оба уютно)
        IDLE_WHISTLE,      // насвистывает, глядя вверх-вбок
        IDLE_EYE_ROLL,     // закатывает глаза по дуге
        IDLE_SIGH          // глубокий вздох (иногда с зевком)
    };

    // Подхватывает текущую геометрию из FaceState, сбрасывает все таймеры.
    void reset();

    // Один кадр (вызывать каждые 20 мс). parX/parY — параллакс от IMU (px).
    void runStep(PocketSprite& sprite, float parX = 0.0f, float parY = 0.0f);

    // --- Вспомогательное API (для менеджера / отладки) ---
    Mode  mode()   const { return _mode; }
    bool  isBusy() const { return _mode != IDLE_NORMAL; } // идёт микро-поведение
    float idleSeconds() const;                            // сколько секунд в покое
    // Принудительно запустить поведение (например, с кнопки для отладки).
    // IDLE_NORMAL — досрочно прервать текущее поведение.
    void  triggerBehavior(Mode m);

private:
    // «Намерение кадра»: что текущее поведение хочет от лица.
    // Заполняется заново каждый кадр, геометрию напрямую не меняет.
    struct Intent {
        bool  gazeOverride;     // поведение управляет взглядом вместо саккад
        float gazeK;            // жёсткость пружины взгляда в режиме override
        float gx, gy;           // целевой взгляд (px), если gazeOverride
        float sqL, sqR;         // целевой прищур левого/правого глаза 0..1
        float bias;             // -1 = опускается верхнее веко (устал/недоволен)
                                // +1 = поднимается нижнее веко (улыбка глазами)
        float mw, mh;           // целевой размер рта
        float mouthOsc;         // мгновенная добавка к размеру рта (свист)
        float mouthDY;          // смещение рта по Y
        float eyeDH, eyeDW;     // добавка к высоте/ширине глаз (вздох/зевок)
        float eyeDY;            // покачивание глаз по Y (свист)
        float faceDX, faceDY;   // покачивание всего лица
    };

    // Пружинные коэффициенты, пересчитанные под реальный dt кадра.
    struct Springs {
        float gaze, soft, roll, shape, squint, mouth, breath, face, tremor, par;
    };

    // ---- этапы кадра ----
    void updateScheduler(uint32_t now);
    void enterMode(Mode m, uint32_t now);
    void leaveMode(uint32_t now);
    void applyBehavior(uint32_t now, float dt, Intent& in);
    void behaviorSquint (float e, Intent& in);
    void behaviorWhistle(uint32_t now, float dt, float e, Intent& in);
    void behaviorEyeRoll(uint32_t now, float e, Intent& in);
    void behaviorSigh   (float e, Intent& in);
    void updateBreathing(float dt);
    void updateGaze(uint32_t now, float dt, const Intent& in);
    void scheduleSaccade(uint32_t now);
    void updateBlink(uint32_t now, float dt);
    void startBlink();
    void drawBlob(PocketSprite& s, float cx, float cy, float w, float h, float r);

    // ---- время ----
    uint32_t _lastMs  = 0;
    uint32_t _startMs = 0;
    float    _t       = 0.0f;     // секунды с reset() (для дрейфа/покачиваний)
    Springs  _k{};

    // ---- лицо ----
    float _faceX = 0, _faceY = 0, _spacing = 0;
    float _parX = 0, _parY = 0;   // сглаженный параллакс

    // ---- взгляд ----
    float _gazeX = 0, _gazeY = 0, _tgtGX = 0, _tgtGY = 0;
    float _tremX = 0, _tremY = 0, _driftX = 0, _driftY = 0;
    float _saccDX = 0, _saccDY = 0;
    uint32_t _nextSaccadeMs = 0, _corrMs = 0;
    bool     _corrPending = false;
    uint8_t  _burstLeft   = 0;

    // ---- дыхание ----
    float _breathT = 0, _breathScale = 1.0f, _breath = 0, _sighAmp = 0;
    uint32_t _nextSighMs = 0;

    // ---- моргание ----
    float    _blinkK = 1.0f;      // 1 = открыт, 0 = закрыт
    uint8_t  _blinkPhase = 0;     // 0 нет, 1 закрывается, 2 открывается
    bool     _blinkDouble = false;
    uint32_t _nextBlinkMs = 0;

    // ---- прищур (текущие значения) ----
    float _sqL = 0, _sqR = 0, _bias = 0;

    // ---- текущие формы (сглаженные) ----
    float _lw = 0, _lh = 0, _lr = 0, _rw = 0, _rh = 0, _rr = 0, _mw = 0, _mh = 0;

    // ---- поведение ----
    Mode     _mode = IDLE_NORMAL;
    uint32_t _modeStartMs = 0, _modeDurMs = 0, _nextBehaviorMs = 0;
    uint8_t  _sqType = 0;         // 0 левый глаз, 1 правый, 2 оба уютно
    int8_t   _dir = 1;            // сторона для свиста / закатывания
    bool     _yawn = false;
    bool     _rollExhaled = false, _rollBlinked = false;
    float    _whPhase = 0, _whFreq = 3.5f, _whAccent = 0;
    uint32_t _nextNoteMs = 0;
};
