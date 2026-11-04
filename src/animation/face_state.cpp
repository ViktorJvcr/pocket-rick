#include "face_state.h"

FacePalette FaceState::current;
FacePalette FaceState::target;

float FaceState::cx = FACE_CX;
float FaceState::cy = FACE_CY;
float FaceState::spacing = FACE_SPACING;
float FaceState::lw = EYE_W, FaceState::lh = EYE_H, FaceState::lr = EYE_R;
float FaceState::rw = EYE_W, FaceState::rh = EYE_H, FaceState::rr = EYE_R;
float FaceState::mw = MOUTH_W, FaceState::mh = MOUTH_H;

uint16_t FaceState::rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void FaceState::refreshPacked(FacePalette& p) {
    p.colorEye   = rgb565((uint8_t)lroundf(p.rEye),   (uint8_t)lroundf(p.gEye),   (uint8_t)lroundf(p.bEye));
    p.colorGlow1 = rgb565((uint8_t)lroundf(p.rGlow1), (uint8_t)lroundf(p.gGlow1), (uint8_t)lroundf(p.bGlow1));
    p.colorGlow2 = rgb565((uint8_t)lroundf(p.rGlow2), (uint8_t)lroundf(p.gGlow2), (uint8_t)lroundf(p.bGlow2));
    p.colorBg    = 0x0000;
}

void FaceState::init() {
    cx = FACE_CX; cy = FACE_CY; spacing = FACE_SPACING;
    lw = rw = EYE_W; lh = rh = EYE_H; lr = rr = EYE_R;
    mw = MOUTH_W; mh = MOUTH_H;

    setTargetPalette(IDLE_EYE_R, IDLE_EYE_G, IDLE_EYE_B,
                     IDLE_G1_R,  IDLE_G1_G,  IDLE_G1_B,
                     IDLE_G2_R,  IDLE_G2_G,  IDLE_G2_B);
    current = target;                  // без плавного "включения" на старте
    refreshPacked(current);
}

void FaceState::setTargetPalette(uint8_t rE, uint8_t gE, uint8_t bE,
                                 uint8_t rG1, uint8_t gG1, uint8_t bG1,
                                 uint8_t rG2, uint8_t gG2, uint8_t bG2) {
    target.rEye = rE;    target.gEye = gE;    target.bEye = bE;
    target.rGlow1 = rG1; target.gGlow1 = gG1; target.bGlow1 = bG1;
    target.rGlow2 = rG2; target.gGlow2 = gG2; target.bGlow2 = bG2;
    refreshPacked(target);
}

void FaceState::updateColorLerp(float speed) {
    float* c = &current.rEye;
    float* t = &target.rEye;
    for (int i = 0; i < 9; ++i) c[i] += (t[i] - c[i]) * speed;   // 9 float-каналов подряд
    refreshPacked(current);
}

void FaceState::prepareTransition(bool squint) {
    if (!squint) return;
    lh = rh = SQUINT_H;
    lr = rr = SQUINT_R;
    // ширина не трогается — следующая эмоция сама "раскроет" глаза пружиной
}

void FaceState::loadGeometry(float &outCx, float &outCy, float &outSp,
                             float &outLw, float &outLh, float &outLr,
                             float &outRw, float &outRh, float &outRr,
                             float &outMw, float &outMh) {
    outCx = cx; outCy = cy; outSp = spacing;
    outLw = lw; outLh = lh; outLr = lr;
    outRw = rw; outRh = rh; outRr = rr;
    outMw = mw; outMh = mh;
}

void FaceState::saveGeometry(float inCx, float inCy, float inSp,
                             float inLw, float inLh, float inLr,
                             float inRw, float inRh, float inRr,
                             float inMw, float inMh) {
    cx = inCx; cy = inCy; spacing = inSp;
    lw = inLw; lh = inLh; lr = inLr;
    rw = inRw; rh = inRh; rr = inRr;
    mw = inMw; mh = inMh;
}
