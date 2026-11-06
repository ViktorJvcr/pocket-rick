#include <Eyes.h>
#include <Config.h>

Eyes::Eyes(TFT_eSPI* tft_ptr, TFT_eSprite* sprite_ptr) {
    tft = tft_ptr;
    sprite = sprite_ptr;
}

void Eyes::init() {
    colorEye   = tft->color565(50, 255, 100);
    colorGlow1 = tft->color565(10, 120, 30);
    colorGlow2 = tft->color565(0, 40, 10);
    
    cx = 120; cy = 135;
    lw = eyeBaseW; lh = eyeBaseH;
    rw = eyeBaseW; rh = eyeBaseH;
    mw = 15; mh = 4;
    
    // Инициализация частиц (нот)
    for(int i=0; i<3; i++) notes[i].active = false;

    setEmotion(EMOTION_NEUTRAL);
}

void Eyes::blink() {
    if (!isBlinking) {
        isBlinking = true;
        isRecovering = false;
        t_lh = 8; t_rh = 8; // <--- СДЕЛАЛИ ПРИЩУР НЕ ТАКИМ УЗКИМ (было 4)
        lastBlinkTime = millis();
    }
}

void Eyes::setEmotion(Emotion emo) {
    currentEmotion = emo; // Запоминаем текущую эмоцию

    t_lw = eyeBaseW; t_rw = eyeBaseW;
    t_cx = 120; t_cy = 135;
    t_mw = 15; t_mh = 4;
    if (!isBlinking) { t_lh = eyeBaseH; t_rh = eyeBaseH; }

    switch (emo) {
        case EMOTION_NEUTRAL: break;
        case EMOTION_SUSPICIOUS:
            t_cy = 155;
            if (!isBlinking) { t_lh = 40; t_rh = 40; }
            t_mw = 25; t_mh = 2;
            break;
        case EMOTION_MUSIC:
            t_lw = 35; if (!isBlinking) t_lh = 55;
            t_rw = 60; if (!isBlinking) t_rh = 90;
            t_cy = 125; t_mw = 12; t_mh = 12;
            break;
        case EMOTION_HAPPY:
            t_lw = 65; t_rw = 65;
            if (!isBlinking) { t_lh = 25; t_rh = 25; }
            t_mw = 20; t_mh = 6; t_cy = 145;
            break;
        case EMOTION_SLEEP:
             t_cy = 165; // <--- Чуть приподняли глаза во сне
             if (!isBlinking) { t_lh = 12; t_rh = 12; }
             t_mw = 10; t_mh = 2;
             break;
    }
}

void Eyes::updatePhysics() {
    unsigned long ms = millis();

    if (ms - lastBlinkTime > random(3000, 7000) && !isBlinking) blink();
    
    if (isBlinking && ms - lastBlinkTime > 120) {
        isBlinking = false;
        isRecovering = true;
        setEmotion(currentEmotion); // Возвращаем форму после моргания
        lastBlinkTime = ms;
    }
    if (isRecovering && ms - lastBlinkTime > 400) isRecovering = false;

    float breathY = sin(ms / 400.0) * 3.0;
    float current_t_cy = t_cy + breathY;
    float speed = isBlinking ? 0.5 : (isRecovering ? 0.2 : 0.12);

    cx += (t_cx - cx) * speed; cy += (current_t_cy - cy) * speed;
    lw += (t_lw - lw) * speed; lh += (t_lh - lh) * speed;
    rw += (t_rw - rw) * speed; rh += (t_rh - rh) * speed;
    mw += (t_mw - mw) * speed; mh += (t_mh - mh) * speed;

    // --- ФИЗИКА НОТ (ВЕРНУЛИ!) ---
    if (currentEmotion == EMOTION_MUSIC && ms - lastNoteTime > 600) {
        for (int i = 0; i < 3; i++) {
            if (!notes[i].active) {
                notes[i].x = cx; notes[i].y = cy + 45;
                notes[i].life = 1.0; notes[i].active = true;
                break;
            }
        }
        lastNoteTime = ms;
    }

    for (int i = 0; i < 3; i++) {
        if (notes[i].active) {
            notes[i].y -= 1.5;
            notes[i].x += sin(ms / 150.0 + i) * 1.0;
            notes[i].life -= 0.015;
            if (notes[i].life <= 0) notes[i].active = false;
        }
    }
}

void Eyes::drawFrame() {
    sprite->fillSprite(TFT_BLACK);

    // Отрисовка нот (голубым цветом)
    for (int i = 0; i < 3; i++) {
        if (notes[i].active) {
            int nx = notes[i].x; int ny = notes[i].y;
            sprite->fillCircle(nx, ny, 4, TFT_CYAN);
            sprite->drawLine(nx + 3, ny, nx + 3, ny - 10, TFT_CYAN);
            sprite->drawLine(nx + 3, ny - 10, nx + 7, ny - 7, TFT_CYAN);
        }
    }

    float lx = cx - (eyeSpacing / 2); float rx = cx + (eyeSpacing / 2);

    int l_x = lx - (lw / 2); int l_y = cy - (lh / 2);
    int safeRadiusL = min((int)eyeRadius, min((int)lw / 2, (int)lh / 2));
    sprite->fillRoundRect(l_x - 6, l_y - 6, lw + 12, lh + 12, safeRadiusL + 4, colorGlow2);
    sprite->fillRoundRect(l_x - 3, l_y - 3, lw + 6,  lh + 6,  safeRadiusL + 2, colorGlow1);
    sprite->fillRoundRect(l_x, l_y, lw, lh, safeRadiusL, colorEye);

    int r_x = rx - (rw / 2); int r_y = cy - (rh / 2);
    int safeRadiusR = min((int)eyeRadius, min((int)rw / 2, (int)rh / 2));
    sprite->fillRoundRect(r_x - 6, r_y - 6, rw + 12, rh + 12, safeRadiusR + 4, colorGlow2);
    sprite->fillRoundRect(r_x - 3, r_y - 3, rw + 6,  rh + 6,  safeRadiusR + 2, colorGlow1);
    sprite->fillRoundRect(r_x, r_y, rw, rh, safeRadiusR, colorEye);

    // --- РОТ С ОГРАНИЧИТЕЛЕМ ЭКРАНА ---
    int m_x = cx - (mw / 2); 
    int m_y = cy + 50;
    
    // Если рот опускается ниже 150 пикселей - останавливаем его!
    if (m_y > 150) m_y = 150; 

    if (mh > 8) sprite->fillCircle(cx, m_y + (mh/2), mw/2, colorEye);
    else sprite->fillRoundRect(m_x, m_y, mw, mh, 2, colorEye);

    sprite->pushSprite(0, 0);
}

void Eyes::update() {
    updatePhysics();
    drawFrame();
}

void Eyes::lookAt(int offsetX, int offsetY, bool squint) {
    currentEmotion = EMOTION_NEUTRAL; // Это вариант спокойного состояния

    t_lw = eyeBaseW; 
    t_rw = eyeBaseW;
    
    // Сдвигаем цель (куда смотрят глаза)
    t_cx = 120 + offsetX;
    t_cy = 135 + offsetY;
    
    t_mw = 15; 
    t_mh = 4;

    if (!isBlinking) {
        if (squint) {
            // Если нужно прищуриться (подозрение)
            t_lh = 35; 
            t_rh = 35;
        } else {
            // Обычный, широко открытый взгляд
            t_lh = eyeBaseH; 
            t_rh = eyeBaseH;
        }
    }
}