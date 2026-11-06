#include <Brain.h>

Brain::Brain(Eyes* eyes_ptr) {
    eyes = eyes_ptr;
}

void Brain::init() {
    currentEmotion = EMOTION_NEUTRAL;
    lastActionTime = millis();
}

void Brain::update() {
    unsigned long ms = millis();

    // Робот принимает решение чаще (каждые 1.5 - 4 секунды)
    if (ms - lastActionTime > random(1500, 4000)) {
        
        // Кидаем кубик от 0 до 9
        int r = random(0, 10); 

        if (r < 6) {
            // === 60% ШАНС: АНИМАЦИЯ БЕЗДЕЙСТВИЯ (Осмотр по сторонам) ===
            
            int offsetX = random(-50, 51); // Случайный сдвиг влево-вправо (-50 до 50)
            int offsetY = random(-20, 21); // Случайный сдвиг вверх-вниз (-20 до 20)
            
            // 30% шанс, что при осмотре он прищурится с подозрением
            bool isSquinting = (random(0, 3) == 0); 

            eyes->lookAt(offsetX, offsetY, isSquinting);

        } else if (r == 6) {
            // === 10% ШАНС: Просто посмотреть прямо ===
            currentEmotion = EMOTION_NEUTRAL;
            eyes->setEmotion(currentEmotion);

        } else {
            // === 30% ШАНС: Включить глубокую эмоцию ===
            int emoRoll = random(1, 5);
            if (emoRoll == 1) currentEmotion = EMOTION_SUSPICIOUS;
            if (emoRoll == 2) currentEmotion = EMOTION_HAPPY;
            if (emoRoll == 3) currentEmotion = EMOTION_MUSIC;
            if (emoRoll == 4) currentEmotion = EMOTION_SLEEP;

            eyes->setEmotion(currentEmotion);
        }

        lastActionTime = ms;
    }
}

void Brain::triggerEvent(String eventName) {
    if (eventName == "SHAKE") {
        currentEmotion = EMOTION_ANGRY;
        eyes->setEmotion(currentEmotion);
        lastActionTime = millis(); 
    }
}
