#include "anim_idle.h"
#include "face_state.h"
#include "config.h"
#include <math.h>

// =============================================================================
//                         П А Н Е Л Ь   Н А С Т Р О Е К
//  Все величины «доля за кадр» рассчитаны на 50 Гц и автоматически
//  пересчитываются под реальный dt (см. springK), так что при просадке FPS
//  скорость движений остаётся прежней.
// =============================================================================
namespace {

constexpr float kPI     = 3.14159265f;
constexpr float kTwoPI  = 6.28318531f;
constexpr float kHalfPI = 1.57079633f;

// ---------- ВЗГЛЯД / САККАДЫ ----------
constexpr float SACC_X = 18.0f;          // максимум взгляда по X (px)
constexpr float SACC_Y = 10.0f;          // максимум взгляда по Y (px)
constexpr float GAZE_SPRING      = 0.30f; // саккада: ~100 мс до цели
constexpr float GAZE_SPRING_SOFT = 0.13f; // плавный перевод взгляда в поведениях
constexpr uint32_t FIX_SHORT_MIN = 350,  FIX_SHORT_MAX = 1100;  // короткая фиксация
constexpr uint32_t FIX_LONG_MIN  = 1600, FIX_LONG_MAX  = 4200;  // «засмотрелся»
constexpr float FIX_LONG_CHANCE  = 0.45f;
constexpr float BURST_CHANCE     = 0.35f; // серия из 1–2 мелких саккад подряд
constexpr float CORR_FRACTION    = 0.08f; // коррекционная микро-саккада (доля)
constexpr float TREMOR_AMP       = 0.45f; // микро-тремор, px
constexpr float TREMOR_SPRING    = 0.30f;
constexpr float DRIFT_AMP_X = 1.3f, DRIFT_AMP_Y = 0.8f; // медленное блуждание, px
constexpr float PERSP_GAIN      = 0.07f;  // ближний глаз на 7% шире при взгляде вбок
constexpr float FAR_EYE_LAG     = 0.85f;  // дальний глаз сдвигается на 85% амплитуды
constexpr float LOOK_UP_STRETCH = 0.15f;  // взгляд вверх — глаза чуть выше
constexpr float MOUTH_FOLLOW_X = 0.40f, MOUTH_FOLLOW_Y = 0.50f;

// ---------- ДЫХАНИЕ ----------
// Асимметричный цикл: вдох быстрее, микропауза, выдох длиннее и мягче,
// затем короткая пауза с едва заметным «проседанием».
constexpr float BR_IN = 1.40f, BR_HOLD = 0.22f, BR_OUT = 2.20f, BR_PAUSE = 0.40f;
constexpr float BR_SCALE_MIN = 0.90f, BR_SCALE_MAX = 1.12f; // вариация длины цикла
constexpr float BR_H_AMP     = 2.6f;  // +px к высоте глаз на пике вдоха
constexpr float BR_W_AMP     = 0.6f;  // -px к ширине (грудь «вытягивается»)
constexpr float BR_Y_AMP     = 1.6f;  // подъём глаз на вдохе, px
constexpr float BR_MOUTH_Y   = 0.8f;  // подъём рта на вдохе, px
constexpr float BR_SPRING    = 0.45f;

// ---------- ВЗДОХ / ЗЕВОК ----------
constexpr uint32_t SIGH_MIN_MS = 25000, SIGH_MAX_MS = 45000; // период вздохов
constexpr float SIGH_IN = 1.10f, SIGH_HOLD = 0.35f, SIGH_OUT = 1.90f, SIGH_RELAX = 0.90f;
constexpr float SIGH_DEPTH   = 2.2f;  // глубина вдоха (в единицах обычного дыхания)
constexpr float SIGH_UNDER   = 0.45f; // «проседание» после выдоха
constexpr float SIGH_WIDEN_H = 6.0f;  // расширение глаз на вдохе, px
constexpr float SIGH_WIDEN_W = 2.5f;
constexpr float SIGH_DROOP   = 0.28f; // тяжесть век на выдохе (0..1)
constexpr float YAWN_CHANCE  = 0.40f; // доля вздохов, переходящих в зевок
constexpr float YAWN_MOUTH_W = 15.0f, YAWN_MOUTH_H = 18.0f;
constexpr float YAWN_SQUEEZE = 0.60f; // глаза жмурятся на пике зевка

// ---------- МОРГАНИЕ ----------
constexpr float BLINK_CLOSE_S = 0.07f, BLINK_OPEN_S = 0.14f;
constexpr uint32_t BLINK_MIN = 2400, BLINK_MAX = 6500;
constexpr float BLINK_DOUBLE_CHANCE = 0.18f;
//constexpr float BLINK_MIN_H = 2.0f;

// ---------- ПРИЩУРЫ ----------
constexpr float SQ_DEPTH         = 0.55f; // максимум: глаз теряет 55% высоты
constexpr float SQ_ONE_EYE       = 0.50f; // подозрительный прищур одного глаза
constexpr float SQ_ONE_EYE_OTHER = 0.08f; // второй глаз лишь чуть-чуть
constexpr float SQ_ONE_EYE_BIAS  = -0.6f; // в основном опускается верхнее веко
constexpr float SQ_COZY          = 0.38f; // «улыбка глазами», оба глаза
constexpr float SQ_COZY_MOUTH_W  = 20.0f; // рот чуть растягивается в улыбку
constexpr float SQ_SPRING        = 0.12f;
constexpr uint32_t SQ_DUR_MIN = 1200, SQ_DUR_MAX = 2800;

// ---------- СВИСТ ----------
constexpr float WH_MOUTH      = 11.0f; // трубочка: круг ~11x11
constexpr float WH_OSC_MOUTH  = 1.6f;  // пульсация рта «в ноты», ±px
constexpr float WH_OSC_EYE_Y  = 0.7f;  // покачивание глаз, ±px
constexpr float WH_SWAY_X     = 1.3f;  // покачивание всего лица, ±px
constexpr float WH_FREQ_MIN = 3.0f, WH_FREQ_MAX = 4.2f; // Гц, темп «нот»
constexpr uint32_t WH_NOTE_MIN = 280, WH_NOTE_MAX = 700; // длительность ноты, мс
constexpr float WH_ACCENT_CHANCE = 0.30f; // «высокая нота» — рот округляется сильнее
constexpr float WH_ACCENT        = 2.2f;
constexpr uint32_t WH_DUR_MIN = 2500, WH_DUR_MAX = 4500; // длительность фразы
constexpr float WH_GAZE_X = 0.55f, WH_GAZE_Y = -0.95f; // поза «в потолок и вбок»
constexpr float WH_SQUINT = 0.18f;    // довольный лёгкий прищур
constexpr float WH_MOUTH_SPRING = 0.18f;

// ---------- ЗАКАТЫВАНИЕ ГЛАЗ ----------
// Ключевые точки траектории. gx/gy — в долях SACC_X / SACC_Y (gx умножается
// на сторону _dir). Между ключами — smoothstep + пружина → гладкая дуга.
struct RollKey { float t, gx, gy, sq, bias; };
constexpr RollKey ROLL_KEYS[] = {
    { 0.00f, 0.00f, -1.60f, 0.00f,  0.0f }, // резко вверх
    { 0.40f, 0.90f, -1.45f, 0.12f, -1.0f }, // по дуге вверх-вбок
    { 0.80f, 1.15f, -0.65f, 0.35f, -1.0f }, // вбок, веки устало опускаются
    { 1.35f, 1.00f, -0.20f, 0.38f, -1.0f }, // задержка «ну сколько можно…»
    { 1.90f, 0.10f,  0.15f, 0.15f, -1.0f }, // медленный возврат с выдохом
    { 2.70f, 0.00f,  0.00f, 0.00f,  0.0f }, // покой
};
constexpr uint8_t ROLL_N = sizeof(ROLL_KEYS) / sizeof(ROLL_KEYS[0]);
constexpr float ROLL_DUR_S   = 2.70f;
constexpr float ROLL_EXHALE_T = 1.35f; // момент начала выдоха
constexpr float ROLL_BLINK_T  = 1.95f; // моргнуть при возврате в центр
constexpr float ROLL_SPRING   = 0.20f;

// ---------- ПЛАНИРОВЩИК ПОВЕДЕНИЙ ----------
constexpr uint32_t FIRST_BEH_MIN = 5000, FIRST_BEH_MAX = 9000;   // после reset()
constexpr uint32_t BEH_GAP_MIN   = 6000, BEH_GAP_MAX   = 14000;  // между поведениями
constexpr uint16_t W_SQUINT = 38, W_WHISTLE = 27, W_ROLL = 15, W_SIGH = 20; // веса
constexpr float    BOREDOM_START_S   = 45.0f; // с этого момента «скучает»
constexpr uint16_t BOREDOM_ROLL_BONUS = 25;   // максимум добавки к весу закатывания

// ---------- ОБЩИЕ ПРУЖИНЫ ----------
constexpr float SHAPE_SPRING = 0.35f; // формы глаз
constexpr float MOUTH_SPRING = 0.22f;
constexpr float FACE_SPRING  = 0.08f; // возврат лица к FACE_CX/FACE_CY
constexpr float PAR_SPRING   = 0.35f; // сглаживание параллакса

// ============================ утилиты ========================================
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float p)    { return a + (b - a) * p; }
inline float smooth01(float p) { p = clampf(p, 0.0f, 1.0f); return p * p * (3.0f - 2.0f * p); }
inline float frand01() { return (float)random(0, 10001) * 0.0001f; }
inline float frand(float a, float b) { return a + (b - a) * frand01(); }
inline uint32_t urand(uint32_t a, uint32_t b) { return (uint32_t)random((long)a, (long)b + 1); }

// Пересчёт «доли за кадр @50Гц» под фактический dt. Быстрый путь — если кадр
// пришёл вовремя, powf не вызывается.
inline float springK(float perFrame, float dt) {
    if (fabsf(dt - 0.02f) < 0.003f) return perFrame;
    return 1.0f - powf(1.0f - perFrame, dt * 50.0f);
}

} // namespace

// =============================================================================
//                                  RESET
// =============================================================================
void AnimIdle::reset() {
    const uint32_t now = millis();
    _lastMs = _startMs = now;
    _t = 0.0f;

    // Подхватываем геометрию, оставленную предыдущей анимацией — старт бесшовный.
    float cx, cy, sp;
    FaceState::loadGeometry(cx, cy, sp, _lw, _lh, _lr, _rw, _rh, _rr, _mw, _mh);
    _faceX = cx; _faceY = cy; _spacing = sp;
    _parX = _parY = 0.0f;

    _gazeX = _gazeY = _tgtGX = _tgtGY = 0.0f;
    _tremX = _tremY = _driftX = _driftY = 0.0f;
    _saccDX = _saccDY = 0.0f;
    _nextSaccadeMs = now + urand(600, 1400);
    _corrPending = false;
    _burstLeft = 0;

    _breathT = (BR_IN + BR_HOLD + BR_OUT); // стартуем с паузы (расслаблен)
    _breathScale = 1.0f;
    _breath = _sighAmp = 0.0f;
    _nextSighMs = now + urand(SIGH_MIN_MS, SIGH_MAX_MS);

    _blinkK = 1.0f; _blinkPhase = 0; _blinkDouble = false;
    _nextBlinkMs = now + urand(1200, 3000);

    _sqL = _sqR = _bias = 0.0f;

    _mode = IDLE_NORMAL;
    _modeStartMs = now; _modeDurMs = 0;
    _nextBehaviorMs = now + urand(FIRST_BEH_MIN, FIRST_BEH_MAX);
    _sqType = 0; _dir = 1; _yawn = false;
    _rollExhaled = _rollBlinked = false;
    _whPhase = 0.0f; _whFreq = 3.5f; _whAccent = 0.0f; _nextNoteMs = now;
}

float AnimIdle::idleSeconds() const {
    return (float)(millis() - _startMs) * 0.001f;
}

void AnimIdle::triggerBehavior(Mode m) {
    const uint32_t now = millis();
    if (m == IDLE_NORMAL) { if (_mode != IDLE_NORMAL) leaveMode(now); }
    else enterMode(m, now);
}

// =============================================================================
//                              ПЛАНИРОВЩИК
// =============================================================================
void AnimIdle::updateScheduler(uint32_t now) {
    if (_mode != IDLE_NORMAL) {
        if ((uint32_t)(now - _modeStartMs) >= _modeDurMs) leaveMode(now);
        return;
    }
    // Глубокий вздох идёт по своему независимому таймеру (25–45 с).
    if ((int32_t)(now - _nextSighMs) >= 0) { enterMode(IDLE_SIGH, now); return; }

    if ((int32_t)(now - _nextBehaviorMs) >= 0) {
        // «Скука»: чем дольше никто не трогает, тем охотнее закатывает глаза.
        uint16_t wRoll = W_ROLL;
        const float idle = idleSeconds();
        if (idle > BOREDOM_START_S) {
            float bonus = (idle - BOREDOM_START_S) * 0.5f;
            if (bonus > (float)BOREDOM_ROLL_BONUS) bonus = (float)BOREDOM_ROLL_BONUS;
            wRoll += (uint16_t)bonus;
        }
        const uint16_t total = W_SQUINT + W_WHISTLE + wRoll + W_SIGH;
        uint16_t r = (uint16_t)random(0, total);
        if      (r < W_SQUINT)                       enterMode(IDLE_SQUINT,   now);
        else if ((r -= W_SQUINT)  < W_WHISTLE)       enterMode(IDLE_WHISTLE,  now);
        else if ((r -= W_WHISTLE) < wRoll)           enterMode(IDLE_EYE_ROLL, now);
        else                                         enterMode(IDLE_SIGH,     now);
    }
}

void AnimIdle::enterMode(Mode m, uint32_t now) {
    _mode = m;
    _modeStartMs = now;
    switch (m) {
        case IDLE_SQUINT:
            _sqType = (uint8_t)random(0, 3);
            _modeDurMs = urand(SQ_DUR_MIN, SQ_DUR_MAX);
            break;
        case IDLE_WHISTLE:
            _dir = random(0, 2) ? 1 : -1;
            _whPhase = 0.0f; _whAccent = 0.0f;
            _whFreq = frand(WH_FREQ_MIN, WH_FREQ_MAX);
            _nextNoteMs = now + urand(WH_NOTE_MIN, WH_NOTE_MAX);
            _modeDurMs = urand(WH_DUR_MIN, WH_DUR_MAX);
            break;
        case IDLE_EYE_ROLL:
            _dir = random(0, 2) ? 1 : -1;
            _rollExhaled = _rollBlinked = false;
            _modeDurMs = (uint32_t)(ROLL_DUR_S * 1000.0f);
            break;
        case IDLE_SIGH:
            _yawn = frand01() < YAWN_CHANCE;
            _modeDurMs = (uint32_t)((SIGH_IN + SIGH_HOLD + SIGH_OUT + SIGH_RELAX) * 1000.0f);
            break;
        default:
            _modeDurMs = 0;
            break;
    }
}

void AnimIdle::leaveMode(uint32_t now) {
    if (_mode == IDLE_SIGH) {
        _nextSighMs = now + urand(SIGH_MIN_MS, SIGH_MAX_MS);
        // Продолжаем обычное дыхание с фазы покоя — без рывка.
        _breathT = (BR_IN + BR_HOLD + BR_OUT) * _breathScale;
    }
    _mode = IDLE_NORMAL;
    _nextBehaviorMs = now + urand(BEH_GAP_MIN, BEH_GAP_MAX);
    _nextSaccadeMs  = now + urand(300, 900);
}

// =============================================================================
//                               ПОВЕДЕНИЯ
// =============================================================================
void AnimIdle::applyBehavior(uint32_t now, float dt, Intent& in) {
    const float e = (float)(now - _modeStartMs) * 0.001f; // секунд в режиме
    switch (_mode) {
        case IDLE_SQUINT:   behaviorSquint(e, in);            break;
        case IDLE_WHISTLE:  behaviorWhistle(now, dt, e, in);  break;
        case IDLE_EYE_ROLL: behaviorEyeRoll(now, e, in);      break;
        case IDLE_SIGH:     behaviorSigh(e, in);              break;
        default: break;
    }
}

// --- ПРИЩУР -----------------------------------------------------------------
// Тип 0/1: один глаз подозрительно прищурен (в основном верхним веком),
//          взгляд чуть уходит в сторону этого глаза, рот слегка поджат.
// Тип 2:   оба глаза уютно прищурены снизу («улыбка глазами»), рот шире.
void AnimIdle::behaviorSquint(float /*e*/, Intent& in) {
    in.gazeOverride = true;
    in.gazeK = _k.soft;
    if (_sqType == 2) {
        in.sqL = in.sqR = SQ_COZY;
        in.bias = +1.0f;
        in.gx = 0.0f;
        in.gy = 0.25f * SACC_Y;           // мягкий взгляд чуть вниз
        in.mw = SQ_COZY_MOUTH_W;          // лёгкая улыбка
        in.mh = MOUTH_H;
        in.mouthDY = -1.0f;
    } else {
        const float side = (_sqType == 0) ? -1.0f : 1.0f;
        in.sqL = (_sqType == 0) ? SQ_ONE_EYE : SQ_ONE_EYE_OTHER;
        in.sqR = (_sqType == 1) ? SQ_ONE_EYE : SQ_ONE_EYE_OTHER;
        in.bias = SQ_ONE_EYE_BIAS;
        in.gx = side * 0.45f * SACC_X;    // косится в сторону прищуренного глаза
        in.gy = 0.10f * SACC_Y;
        in.mw = MOUTH_W - 2.0f;           // рот чуть поджат
        in.mh = MOUTH_H;
    }
}

// --- СВИСТ ------------------------------------------------------------------
// Взгляд уходит вверх-вбок, рот сворачивается в трубочку (круг ~11x11),
// затем в такт «нотам» (3–4.2 Гц, меняются каждые 280–700 мс) пульсирует рот,
// покачиваются глаза по Y и всё лицо по X. Иногда «высокая нота» — рот круглее.
// Во второй половине фразы взгляд мечтательно съезжает ближе к центру.
void AnimIdle::behaviorWhistle(uint32_t now, float dt, float e, Intent& in) {
    const float dur = (float)_modeDurMs * 0.001f;
    // Огибающая: плавный вход (0.45 с) и выход (0.6 с), чтобы вибрация
    // не включалась/выключалась рывком.
    const float env = clampf(fminf(e / 0.45f, (dur - e) / 0.6f), 0.0f, 1.0f);

    // Смена ноты
    if ((int32_t)(now - _nextNoteMs) >= 0) {
        _whFreq = frand(WH_FREQ_MIN, WH_FREQ_MAX);
        _nextNoteMs = now + urand(WH_NOTE_MIN, WH_NOTE_MAX);
        if (frand01() < WH_ACCENT_CHANCE) _whAccent = WH_ACCENT;
    }
    _whAccent *= (1.0f - _k.mouth * 0.6f); // акцент плавно затухает

    _whPhase += kTwoPI * _whFreq * dt;
    if (_whPhase > kTwoPI) _whPhase -= kTwoPI;
    const float s = sinf(_whPhase);

    in.gazeOverride = true;
    in.gazeK = _k.soft;
    const float sideMix = (e < dur * 0.55f) ? 1.0f : 0.35f;
    in.gx = (float)_dir * WH_GAZE_X * SACC_X * sideMix;
    in.gy = WH_GAZE_Y * SACC_Y;

    in.mw = WH_MOUTH + _whAccent;
    in.mh = WH_MOUTH + _whAccent * 0.6f;
    in.mouthOsc = s * WH_OSC_MOUTH * env;
    in.mouthDY  = -2.0f * env;                 // трубочка чуть приподнята
    in.eyeDY    = s * WH_OSC_EYE_Y * env;      // глаза «кивают» в ритм
    in.faceDX   = sinf(_t * 5.2f) * WH_SWAY_X * env; // лёгкое покачивание

    in.sqL = in.sqR = WH_SQUINT;               // довольный прищур
    in.bias = +1.0f;
}

// --- ЗАКАТЫВАНИЕ ГЛАЗ --------------------------------------------------------
// Взгляд уходит вверх (-1.6*SACC_Y), по дуге вбок, веки устало опускаются
// (bias = -1 → верхнее веко), пауза, медленный возврат с выдохом и морганием.
void AnimIdle::behaviorEyeRoll(uint32_t now, float e, Intent& in) {
    uint8_t i = 0;
    while (i + 2 < ROLL_N && e >= ROLL_KEYS[i + 1].t) ++i;
    const RollKey& a = ROLL_KEYS[i];
    const RollKey& b = ROLL_KEYS[i + 1];
    const float p = smooth01((e - a.t) / (b.t - a.t));

    in.gazeOverride = true;
    in.gazeK = _k.roll;
    in.gx = lerpf(a.gx, b.gx, p) * (float)_dir * SACC_X;
    in.gy = lerpf(a.gy, b.gy, p) * SACC_Y;
    const float sq = lerpf(a.sq, b.sq, p);
    in.sqL = in.sqR = sq;
    in.bias = lerpf(a.bias, b.bias, p);
    in.mw = MOUTH_W - 6.0f * sq;     // недовольно поджатый рот
    in.mh = MOUTH_H;

    // Выдох при возврате: переводим дыхание в фазу выдоха с «полной грудью».
    if (!_rollExhaled && e >= ROLL_EXHALE_T) {
        _rollExhaled = true;
        _breathT = (BR_IN + BR_HOLD) * _breathScale;
        if (_breath < 1.0f) _breath = 1.0f;
    }
    if (!_rollBlinked && e >= ROLL_BLINK_T) {
        _rollBlinked = true;
        startBlink();
    }
}

// --- ГЛУБОКИЙ ВЗДОХ / ЗЕВОК --------------------------------------------------
// Вдох 1.1 с (глаза расширяются, взгляд чуть вверх) → задержка 0.35 с →
// выдох 1.9 с (глаза «тяжелеют», взгляд чуть вниз) → 0.9 с расслабление ниже
// нуля. В 40% случаев — зевок: рот раскрывается до 15x18, глаза жмурятся.
void AnimIdle::behaviorSigh(float e, Intent& in) {
    const float t1 = SIGH_IN, t2 = t1 + SIGH_HOLD, t3 = t2 + SIGH_OUT;
    float amp;
    in.gazeOverride = true;
    in.gazeK = _k.soft;
    in.gx = _tgtGX;                // по X взгляд не трогаем
    in.gy = 0.0f;

    if (e < t1) {                                         // ВДОХ
        const float s = smooth01(e / t1);
        amp = SIGH_DEPTH * s;
        in.eyeDH = SIGH_WIDEN_H * s;
        in.eyeDW = SIGH_WIDEN_W * s;
        in.gy = -0.25f * SACC_Y * s;
        if (_yawn) {
            const float q = smooth01((e / t1 - 0.35f) / 0.65f);
            in.sqL = in.sqR = YAWN_SQUEEZE * q;
            in.bias = -0.3f;
            in.mw = lerpf(MOUTH_W, YAWN_MOUTH_W, q);
            in.mh = lerpf(MOUTH_H, YAWN_MOUTH_H, q);
            in.mouthDY = 2.0f * q;
        }
    } else if (e < t2) {                                  // ЗАДЕРЖКА
        amp = SIGH_DEPTH;
        in.eyeDH = SIGH_WIDEN_H;
        in.eyeDW = SIGH_WIDEN_W;
        in.gy = -0.25f * SACC_Y;
        if (_yawn) {
            in.sqL = in.sqR = YAWN_SQUEEZE; in.bias = -0.3f;
            in.mw = YAWN_MOUTH_W; in.mh = YAWN_MOUTH_H; in.mouthDY = 2.0f;
        }
    } else if (e < t3) {                                  // ВЫДОХ
        const float p = (e - t2) / SIGH_OUT;
        const float ease = sinf(p * kHalfPI);             // быстро начинается, мягко гаснет
        amp = SIGH_DEPTH - (SIGH_DEPTH + SIGH_UNDER) * ease;
        in.eyeDH = SIGH_WIDEN_H * (1.0f - ease);
        in.eyeDW = SIGH_WIDEN_W * (1.0f - ease);
        const float droop = sinf(p * kPI);
        in.sqL = in.sqR = SIGH_DROOP * droop;             // веки тяжелеют
        in.bias = -1.0f;
        in.gy = 0.35f * SACC_Y * droop;                   // взгляд чуть опускается
        if (_yawn) {
            in.mw = lerpf(YAWN_MOUTH_W, MOUTH_W, ease);
            in.mh = lerpf(YAWN_MOUTH_H, MOUTH_H, ease);
            in.mouthDY = 2.0f * (1.0f - ease);
        }
    } else {                                              // РАССЛАБЛЕНИЕ
        const float p = smooth01((e - t3) / SIGH_RELAX);
        amp = -SIGH_UNDER * (1.0f - p);
    }
    _sighAmp = amp;
}

// =============================================================================
//                                 ДЫХАНИЕ
// =============================================================================
// Форма цикла: вдох — ease-in-out (0→1), пауза — едва заметное расслабление,
// выдох — 1-sin(p·π/2) (быстрое начало, мягкий «дотяг»), пауза — лёгкое
// проседание ниже нуля (грудь опускается чуть ниже покоя). Длина каждого
// цикла случайно варьируется ±10%, чтобы не было метрономной повторяемости.
void AnimIdle::updateBreathing(float dt) {
    float amp;
    if (_mode == IDLE_SIGH) {
        amp = _sighAmp;                       // вздох управляет дыханием сам
    } else {
        const float cycle = (BR_IN + BR_HOLD + BR_OUT + BR_PAUSE) * _breathScale;
        _breathT += dt;
        if (_breathT >= cycle) {
            _breathT -= cycle;
            _breathScale = frand(BR_SCALE_MIN, BR_SCALE_MAX);
        }
        const float t = _breathT / _breathScale;
        if (t < BR_IN) {
            const float p = t / BR_IN;
            amp = 0.5f - 0.5f * cosf(p * kPI);
        } else if (t < BR_IN + BR_HOLD) {
            const float p = (t - BR_IN) / BR_HOLD;
            amp = 1.0f - 0.04f * p;
        } else if (t < BR_IN + BR_HOLD + BR_OUT) {
            const float p = (t - BR_IN - BR_HOLD) / BR_OUT;
            amp = 0.96f * (1.0f - sinf(p * kHalfPI));
        } else {
            const float p = (t - BR_IN - BR_HOLD - BR_OUT) / BR_PAUSE;
            amp = -0.06f * sinf(p * kPI);
        }
    }
    _breath += (amp - _breath) * _k.breath; // сглаживаем стыки фаз
}

// =============================================================================
//                                  ВЗГЛЯД
// =============================================================================
void AnimIdle::scheduleSaccade(uint32_t now) {
    float nx, ny;
    if (_burstLeft > 0) {
        // Серия мелких уточняющих скачков вокруг текущей цели
        --_burstLeft;
        nx = clampf(_tgtGX + frand(-0.25f, 0.25f) * SACC_X, -SACC_X, SACC_X);
        ny = clampf(_tgtGY + frand(-0.20f, 0.20f) * SACC_Y, -SACC_Y, SACC_Y);
        _nextSaccadeMs = now + urand(160, 420);
    } else {
        const float r = frand01();
        if (r < 0.30f) {                               // вернуться почти в центр
            nx = frand(-0.15f, 0.15f) * SACC_X;  ny = frand(-0.15f, 0.15f) * SACC_Y;
        } else if (r < 0.75f) {                        // средняя саккада
            nx = frand(-0.60f, 0.60f) * SACC_X;  ny = frand(-0.50f, 0.50f) * SACC_Y;
        } else {                                       // далёкий взгляд
            nx = frand(-1.00f, 1.00f) * SACC_X;  ny = frand(-0.80f, 0.80f) * SACC_Y;
        }
        _burstLeft = (frand01() < BURST_CHANCE) ? (uint8_t)urand(1, 2) : 0;
        const bool longFix = (_burstLeft == 0) && (frand01() < FIX_LONG_CHANCE);
        _nextSaccadeMs = now + (longFix ? urand(FIX_LONG_MIN, FIX_LONG_MAX)
                                        : urand(FIX_SHORT_MIN, FIX_SHORT_MAX));
    }
    _saccDX = nx - _tgtGX;
    _saccDY = ny - _tgtGY;
    _tgtGX = nx; _tgtGY = ny;
    // Коррекционная микро-саккада через ~130 мс после крупной (как у людей)
    _corrPending = (fabsf(_saccDX) + fabsf(_saccDY)) > 6.0f;
    _corrMs = now + 130;
}

void AnimIdle::updateGaze(uint32_t now, float dt, const Intent& in) {
    float k;
    if (in.gazeOverride) {
        _tgtGX = clampf(in.gx, -SACC_X * 1.7f, SACC_X * 1.7f);
        _tgtGY = clampf(in.gy, -SACC_Y * 1.8f, SACC_Y * 1.8f);
        k = in.gazeK;
        _nextSaccadeMs = now + urand(300, 900);  // после поведения — короткая пауза
        _corrPending = false;
        _burstLeft = 0;
    } else {
        if ((int32_t)(now - _nextSaccadeMs) >= 0) scheduleSaccade(now);
        if (_corrPending && (int32_t)(now - _corrMs) >= 0) {
            _corrPending = false;
            _tgtGX = clampf(_tgtGX + _saccDX * CORR_FRACTION, -SACC_X, SACC_X);
            _tgtGY = clampf(_tgtGY + _saccDY * CORR_FRACTION, -SACC_Y, SACC_Y);
        }
        k = _k.gaze;
    }
    _gazeX += (_tgtGX - _gazeX) * k;
    _gazeY += (_tgtGY - _gazeY) * k;

    // Микро-тремор: случайная цель каждый кадр, сглаженная пружиной
    _tremX += (frand(-TREMOR_AMP, TREMOR_AMP) - _tremX) * _k.tremor;
    _tremY += (frand(-TREMOR_AMP, TREMOR_AMP) - _tremY) * _k.tremor;

    // Медленный дрейф: сумма двух несоизмеримых синусов → не повторяется
    _driftX = DRIFT_AMP_X * (0.6f * sinf(_t * 0.37f) + 0.4f * sinf(_t * 0.91f + 1.7f));
    _driftY = DRIFT_AMP_Y * (0.6f * sinf(_t * 0.29f + 0.8f) + 0.4f * sinf(_t * 1.13f));
    (void)dt;
}

// =============================================================================
//                                 МОРГАНИЕ
// =============================================================================
void AnimIdle::startBlink() {
    if (_blinkPhase != 0) return;
    _blinkPhase = 1;
    _blinkDouble = frand01() < BLINK_DOUBLE_CHANCE;
}

void AnimIdle::updateBlink(uint32_t now, float dt) {
    if (_blinkPhase == 0) {
        // Во время закатывания глаз — не моргаем (моргание там по сценарию)
        const bool allowed = (_mode != IDLE_EYE_ROLL);
        if (allowed && (int32_t)(now - _nextBlinkMs) >= 0) startBlink();
        return;
    }
    if (_blinkPhase == 1) {                       // закрываем быстро
        _blinkK -= dt / BLINK_CLOSE_S;
        if (_blinkK <= 0.0f) { _blinkK = 0.0f; _blinkPhase = 2; }
    } else {                                      // открываем мягче
        _blinkK += dt / BLINK_OPEN_S;
        if (_blinkK >= 1.0f) {
            _blinkK = 1.0f; _blinkPhase = 0;
            if (_blinkDouble) {
                _blinkDouble = false;
                _nextBlinkMs = now + urand(90, 160);
            } else {
                uint32_t mn = BLINK_MIN, mx = BLINK_MAX;
                if (_mode == IDLE_WHISTLE) { mn += 1500; mx += 2500; } // свистя, моргают реже
                _nextBlinkMs = now + urand(mn, mx);
            }
        }
    }
}

// =============================================================================
//                                  РЕНДЕР
// =============================================================================
void AnimIdle::drawBlob(PocketSprite& s, float cx, float cy, float w, float h, float r) {
    if (w < 1.0f) w = 1.0f;
    if (h < 1.0f) h = 1.0f;
    const float rMax = (w < h ? w : h) * 0.5f;
    r = clampf(r, 0.0f, rMax);

    const int16_t iw = (int16_t)floorf(w + 0.5f);
    const int16_t ih = (int16_t)floorf(h + 0.5f);
    const int16_t ir = (int16_t)floorf(r + 0.5f);
    const int16_t x  = (int16_t)floorf(cx - w * 0.5f + 0.5f);
    const int16_t y  = (int16_t)floorf(cy - h * 0.5f + 0.5f);

    const FacePalette& p = FaceState::current;
    // 3-Layer Glow: внешнее свечение → внутреннее → ядро
    s.fillRoundRect(x - GLOW2_PAD, y - GLOW2_PAD, iw + 2 * GLOW2_PAD, ih + 2 * GLOW2_PAD,
                    ir + GLOW2_PAD, p.colorGlow2);
    s.fillRoundRect(x - GLOW1_PAD, y - GLOW1_PAD, iw + 2 * GLOW1_PAD, ih + 2 * GLOW1_PAD,
                    ir + GLOW1_PAD, p.colorGlow1);
    s.fillRoundRect(x, y, iw, ih, ir, p.colorEye);
}

// =============================================================================
//                                  КАДР
// =============================================================================
void AnimIdle::runStep(PocketSprite& sprite, float parX, float parY) {
    // ---- 1. Время ----
    const uint32_t now = millis();
    float dt = (float)(now - _lastMs) * 0.001f;
    _lastMs = now;
    dt = clampf(dt, 0.005f, 0.05f);
    _t += dt;

    _k.gaze   = springK(GAZE_SPRING, dt);
    _k.soft   = springK(GAZE_SPRING_SOFT, dt);
    _k.roll   = springK(ROLL_SPRING, dt);
    _k.shape  = springK(SHAPE_SPRING, dt);
    _k.squint = springK(SQ_SPRING, dt);
    _k.mouth  = springK(_mode == IDLE_WHISTLE ? WH_MOUTH_SPRING : MOUTH_SPRING, dt);
    _k.breath = springK(BR_SPRING, dt);
    _k.face   = springK(FACE_SPRING, dt);
    _k.tremor = springK(TREMOR_SPRING, dt);
    _k.par    = springK(PAR_SPRING, dt);

    // Палитра: если менеджер уже вызывает updateColorLerp() сам — эту строку
    // можно убрать, двойной вызов лишь ускорит переход цвета.
    FaceState::updateColorLerp();

    // ---- 2. Поведение → намерение кадра ----
    updateScheduler(now);
    Intent in = {};
    in.mw = MOUTH_W;
    in.mh = MOUTH_H;
    applyBehavior(now, dt, in);

    // ---- 3. Физика ----
    updateBreathing(dt);
    updateGaze(now, dt, in);
    updateBlink(now, dt);

    _sqL  += (in.sqL  - _sqL)  * _k.squint;
    _sqR  += (in.sqR  - _sqR)  * _k.squint;
    _bias += (in.bias - _bias) * _k.squint;

    _parX += (parX - _parX) * _k.par;
    _parY += (parY - _parY) * _k.par;

    _faceX   += (FACE_CX      - _faceX)   * _k.face;
    _faceY   += (FACE_CY      - _faceY)   * _k.face;
    _spacing += (FACE_SPACING - _spacing) * _k.face;

    // Итоговое смещение взгляда = саккады + тремор + дрейф + параллакс IMU
    const float gX = _gazeX + _tremX + _driftX + _parX;
    const float gY = _gazeY + _tremY + _driftY + _parY;

    // ---- 4. Формы глаз ----
    const float baseH = EYE_H + _breath * BR_H_AMP + in.eyeDH - gY * LOOK_UP_STRETCH;
    const float baseW = EYE_W - _breath * BR_W_AMP + in.eyeDW;

    // Прищур: уменьшаем высоту; bias решает, какое веко двигается.
    // Нижнее веко вверх (bias>0) → центр глаза поднимается; верхнее вниз → опускается.
    float hL = baseH * (1.0f - _sqL * SQ_DEPTH);
    float hR = baseH * (1.0f - _sqR * SQ_DEPTH);
    const float dyL = -_bias * (baseH - hL) * 0.5f;
    const float dyR = -_bias * (baseH - hR) * 0.5f;

    // 3D-перспектива: ближний к направлению взгляда глаз крупнее,
    // дальний сдвигается чуть меньше (параллакс глазниц).
    const float look = clampf(gX / (SACC_X + 16.0f), -1.0f, 1.0f);
    float wL = baseW * (1.0f - look * PERSP_GAIN);
    float wR = baseW * (1.0f + look * PERSP_GAIN);
    hL *= (1.0f - look * PERSP_GAIN * 0.6f);
    hR *= (1.0f + look * PERSP_GAIN * 0.6f);
    const float offL = gX * (1.0f - fmaxf(0.0f,  look) * (1.0f - FAR_EYE_LAG));
    const float offR = gX * (1.0f - fmaxf(0.0f, -look) * (1.0f - FAR_EYE_LAG));

    _lw += (wL - _lw) * _k.shape;  _lh += (hL - _lh) * _k.shape;
    _rw += (wR - _rw) * _k.shape;  _rh += (hR - _rh) * _k.shape;
    _lr = fminf(EYE_R, fminf(_lw, _lh) * 0.5f);
    _rr = fminf(EYE_R, fminf(_rw, _rh) * 0.5f);

    // ---- 5. Рот ----
    _mw += (in.mw - _mw) * _k.mouth;
    _mh += (in.mh - _mh) * _k.mouth;
    const float mwDraw = fmaxf(2.0f, _mw + in.mouthOsc);
    const float mhDraw = fmaxf(2.0f, _mh + in.mouthOsc);
    const float mrDraw = fminf(mwDraw, mhDraw) * 0.5f;

    // ---- 6. Позиции ----
    const float faceX = _faceX + in.faceDX;
    const float eyeY  = _faceY + in.faceDY + in.eyeDY - _breath * BR_Y_AMP + gY;
    const float lcx = faceX - _spacing * 0.5f + offL;
    const float rcx = faceX + _spacing * 0.5f + offR;
    const float lcy = eyeY + dyL;
    const float rcy = eyeY + dyR;
    const float mx  = faceX + gX * MOUTH_FOLLOW_X;
    const float my  = _faceY + in.faceDY + MOUTH_DY + gY * MOUTH_FOLLOW_Y
                    - _breath * BR_MOUTH_Y + in.mouthDY;

    // Моргание: веко идёт сверху → высота сжимается, центр чуть опускается
    const float kv = smooth01(_blinkK);
    const float lhDraw = fmaxf(BLINK_MIN_H, _lh * kv);
    const float rhDraw = fmaxf(BLINK_MIN_H, _rh * kv);
    const float lidDrop = (1.0f - kv) * 4.0f;

    // ---- 7. Рендер ----
    sprite.fillSprite(FaceState::current.colorBg);
    drawBlob(sprite, lcx, lcy + lidDrop, _lw, lhDraw, _lr);
    drawBlob(sprite, rcx, rcy + lidDrop, _rw, rhDraw, _rr);
    drawBlob(sprite, mx,  my,            mwDraw, mhDraw, mrDraw);

    // ---- 8. Геометрия для бесшовной передачи следующей анимации ----
    FaceState::saveGeometry(faceX + gX, eyeY, _spacing,
                            _lw, lhDraw, _lr,
                            _rw, rhDraw, _rr,
                            mwDraw, mhDraw);
}
