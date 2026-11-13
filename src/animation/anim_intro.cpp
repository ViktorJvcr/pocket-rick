#include "anim_intro.h"
#include <Arduino.h>

namespace AnimIntro {
namespace {

constexpr int kWidth   = 240;
constexpr int kHeight  = 200;
constexpr int kScreenY = 0;

constexpr int kCenterX = 120;
constexpr int kCenterY = 100;

constexpr uint32_t kFrameMs = 18;
constexpr uint32_t kTotalMs = 4300;

// Хронометраж сцен
constexpr uint32_t kScene1End = 500;   // 1. Сердце - Крест - Робот
constexpr uint32_t kScene2End = 950;   // 2. Крест - Череп - Крест
constexpr uint32_t kScene3End = 1550;  // 3. Большой развернутый крест + череп
constexpr uint32_t kScene4End = 2050;  // 4. Сердце с глазами - Крест - Робот с глазами
constexpr uint32_t kReelsEnd  = 3850;  // 5. Однорукий бандит (прокрутка)
                                       // 3850-4300: Фиксация [Сердце - Робот - Сердце] + вспышка

constexpr uint16_t kNeon = 0x87E0; // Кибер-салатовый акцент RGB565

constexpr int kColumns[3] = {42, 120, 198};

enum class Icon : uint8_t {
    Heart,
    Cross,
    Robot,
    Skull,
    HeartEyes,
    RobotEyes
};

constexpr Icon kScene1[3] = { Icon::Heart, Icon::Cross, Icon::Robot };
constexpr Icon kScene2[3] = { Icon::Cross, Icon::Skull, Icon::Cross };
constexpr Icon kScene4[3] = { Icon::HeartEyes, Icon::Cross, Icon::RobotEyes };

// Символы на барабане
constexpr Icon kReelSymbols[6] = {
    Icon::Heart,
    Icon::Cross,
    Icon::Robot,
    Icon::Skull,
    Icon::HeartEyes,
    Icon::RobotEyes
};

// Точки старта и остановки слот-машины:
// Барабан 0: 12 % 6 = Heart
// Барабан 1: 14 % 6 = Robot
// Барабан 2: 18 % 6 = Heart
constexpr int kReelStart[3] = {4, 1, 5};
constexpr int kReelStop[3]  = {12, 14, 18};

// Каскадная остановка барабанов слева направо
constexpr uint32_t kReelDuration[3] = { 1200, 1450, 1750 };

constexpr int kReelPitch = 70;
constexpr int kWindowTop = 44;
constexpr int kWindowEnd = 156;

int roundPixel(float val) {
    return static_cast<int>(val >= 0.0f ? val + 0.5f : val - 0.5f);
}

void fillQuad(TFT_eSprite& s, int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, uint16_t color) {
    s.fillTriangle(x0, y0, x1, y1, x2, y2, color);
    s.fillTriangle(x0, y0, x2, y2, x3, y3, color);
}

void drawCross(TFT_eSprite& s, int cx, int cy, int size, uint16_t color) {
    const int h = size / 2;
    const int w = size / 7;
    fillQuad(s, cx - h, cy - h + w, cx - h + w, cy - h, cx + h, cy + h - w, cx + h - w, cy + h, color);
    fillQuad(s, cx + h - w, cy - h, cx + h, cy - h + w, cx - h + w, cy + h, cx - h, cy + h - w, color);
}

void drawHeart(TFT_eSprite& s, int cx, int cy, int size, uint16_t color, bool eyes) {
    const int r = size / 4;
    const int topY = cy - size / 6;

    s.fillCircle(cx - r, topY, r, color);
    s.fillCircle(cx + r, topY, r, color);
    s.fillTriangle(cx - size / 2, topY, cx + size / 2, topY, cx, cy + size / 2, color);

    if (eyes) {
        const int eyeY = cy - size / 10;
        const int eyeX = size / 6;
        const int eyeR = size / 10;
        const uint16_t eyeColor = (color == TFT_WHITE) ? TFT_BLACK : TFT_WHITE;

        s.fillCircle(cx - eyeX, eyeY, eyeR, eyeColor);
        s.fillCircle(cx + eyeX, eyeY, eyeR, eyeColor);

        if (eyeColor == TFT_WHITE) {
            const int pupilR = size / 24;
            s.fillCircle(cx - eyeX, eyeY, pupilR, TFT_BLACK);
            s.fillCircle(cx + eyeX, eyeY, pupilR, TFT_BLACK);
        }
    }
}

void drawRobot(TFT_eSprite& s, int cx, int cy, int size, uint16_t color, bool glowingEyes) {
    // Антенна
    s.fillRect(cx - size / 25, cy - size * 55 / 100, size * 2 / 25, size / 4, color);
    s.fillCircle(cx, cy - size * 60 / 100, size / 16, glowingEyes ? kNeon : TFT_RED);

    // Уши
    s.fillRect(cx - size * 60 / 100, cy - size / 10, size / 8, size / 4, color);
    s.fillRect(cx + size * 47 / 100, cy - size / 10, size / 8, size / 4, color);

    // Квадратная голова
    s.fillRoundRect(cx - size / 2, cy - size * 34 / 100, size, size * 76 / 100, size / 9, color);

    // Глаза
    const int eyeY = cy - size / 9;
    const int eyeW = size / 4;
    const int eyeH = size / 6;
    const int leftEyeX  = cx - size / 3;
    const int rightEyeX = cx + size / 12;

    s.fillRect(leftEyeX,  eyeY, eyeW, eyeH, TFT_BLACK);
    s.fillRect(rightEyeX, eyeY, eyeW, eyeH, TFT_BLACK);

    if (glowingEyes) {
        s.fillRect(leftEyeX + 2, eyeY + 2, eyeW - 4, eyeH - 4, kNeon);
        s.fillRect(rightEyeX + 2, eyeY + 2, eyeW - 4, eyeH - 4, kNeon);
    }

    // Рот
    s.fillRect(cx - size / 4, cy + size / 4, size / 2, size / 20, TFT_BLACK);
}

void drawSkull(TFT_eSprite& s, int cx, int cy, int size, uint16_t color) {
    // Черепная коробка
    s.fillCircle(cx, cy - size / 10, size * 42 / 100, color);
    // Скулы
    s.fillRoundRect(cx - size * 36 / 100, cy - size / 12, size * 72 / 100, size * 42 / 100, size / 10, color);
    // Челюсть
    s.fillRoundRect(cx - size / 4, cy + size / 5, size / 2, size * 32 / 100, size / 16, color);

    // Глазницы
    s.fillCircle(cx - size / 6, cy - size / 25, size / 9, TFT_BLACK);
    s.fillCircle(cx + size / 6, cy - size / 25, size / 9, TFT_BLACK);

    // Нос
    s.fillTriangle(cx, cy + size / 12, cx - size / 13, cy + size / 4, cx + size / 13, cy + size / 4, TFT_BLACK);

    // Зубы
    for (int i = -1; i <= 1; ++i) {
        s.fillRect(cx + i * size / 8 - size / 48, cy + size * 34 / 100, size / 24, size / 5, TFT_BLACK);
    }
}

void drawIcon(TFT_eSprite& s, Icon icon, int cx, int cy, int size) {
    switch (icon) {
        case Icon::Heart:     drawHeart(s, cx, cy, size, TFT_RED, false); break;
        case Icon::HeartEyes: drawHeart(s, cx, cy, size, TFT_RED, true); break;
        case Icon::Cross:     drawCross(s, cx, cy, size, TFT_WHITE); break;
        case Icon::Robot:     drawRobot(s, cx, cy, size, TFT_WHITE, false); break;
        case Icon::RobotEyes: drawRobot(s, cx, cy, size, TFT_WHITE, true); break;
        case Icon::Skull:     drawSkull(s, cx, cy, size, TFT_WHITE); break;
    }
}

void drawTrio(TFT_eSprite& s, const Icon* icons, uint32_t age, bool reveal) {
    const uint32_t growth = age < 100U ? age : 100U;
    const int size = 46 + static_cast<int>(growth * 8U / 100U);
    const int jitter = (!reveal && age < 65U) ? (((age / 18U) & 1U) ? 3 : -3) : 0;

    for (int i = 0; i < 3; ++i) {
        if (reveal && age < static_cast<uint32_t>(i * 45)) continue;
        drawIcon(s, icons[i], kColumns[i] + jitter, kCenterY, size);
    }
}

void drawGlitch(TFT_eSprite& s, uint32_t age, unsigned seed) {
    if (age >= 70U) return;
    const unsigned tick = static_cast<unsigned>(age / 18U);
    const int y1 = 27 + static_cast<int>((tick * 37U + seed * 23U) % 142U);
    const int y2 = 24 + static_cast<int>((tick * 19U + seed * 41U) % 149U);
    const int x2 = static_cast<int>((tick * 53U + seed * 29U) % 160U);

    s.fillRect(7, y1, 226, 2, kNeon);
    s.fillRect(x2, y2, 65, 3, TFT_RED);

    if (age < 22U) {
        s.fillRect(0, 95, kWidth, 10, TFT_WHITE);
    }
}

void drawEmblem(TFT_eSprite& s, uint32_t age) {
    const uint32_t growth = age < 120U ? age : 120U;
    const int crossSize = 140 + static_cast<int>(growth * 12U / 120U);

    drawCross(s, kCenterX, kCenterY, crossSize, TFT_RED);
    s.fillCircle(kCenterX, kCenterY, 52, TFT_BLACK);
    drawSkull(s, kCenterX, kCenterY, 84, TFT_WHITE);
}

Icon reelIcon(int index) {
    int wrapped = index % 6;
    if (wrapped < 0) wrapped += 6;
    return kReelSymbols[wrapped];
}

void drawReels(TFT_eSprite& s, uint32_t age) {
    for (int column = 0; column < 3; ++column) {
        float position;
        if (age >= kReelDuration[column]) {
            position = static_cast<float>(kReelStop[column]);
        } else {
            const float u = static_cast<float>(age) / static_cast<float>(kReelDuration[column]);
            const float remaining = 1.0f - u;
            const float eased = 1.0f - remaining * remaining * remaining;
            position = static_cast<float>(kReelStart[column]) +
                       static_cast<float>(kReelStop[column] - kReelStart[column]) * eased;
        }

        const int base = static_cast<int>(position);
        for (int index = base - 1; index <= base + 2; ++index) {
            const float offset = (static_cast<float>(index) - position) * static_cast<float>(kReelPitch);
            const int y = kCenterY + roundPixel(offset);
            drawIcon(s, reelIcon(index), kColumns[column], y, 48);
        }
    }

    // Каше / маски сверху и снизу
    s.fillRect(0, 0, kWidth, kWindowTop, TFT_BLACK);
    s.fillRect(0, kWindowEnd, kWidth, kHeight - kWindowEnd, TFT_BLACK);

    // Рамки слотов
    for (int column = 0; column < 3; ++column) {
        const bool stopped = age >= kReelDuration[column];
        const uint16_t frameColor = stopped ? kNeon : TFT_WHITE;

        s.drawRoundRect(kColumns[column] - 33, kWindowTop - 2, 66, kWindowEnd - kWindowTop + 4, 5, frameColor);
        if (stopped) {
            s.fillRect(kColumns[column] - 7, kWindowEnd + 8, 14, 3, kNeon);
        }
    }
}

void renderFrame(TFT_eSprite& s, uint32_t elapsed) {
    s.fillSprite(TFT_BLACK);

    if (elapsed < kScene1End) {
        // Сцена 1: Сердце - Крест - Робот
        drawTrio(s, kScene1, elapsed, true);
        return;
    }

    if (elapsed < kScene2End) {
        // Сцена 2: Крест - Череп - Крест
        const uint32_t age = elapsed - kScene1End;
        drawTrio(s, kScene2, age, false);
        drawGlitch(s, age, 1);
        return;
    }

    if (elapsed < kScene3End) {
        // Сцена 3: Развернутый большой крест на фоне черепа
        const uint32_t age = elapsed - kScene2End;
        drawEmblem(s, age);
        drawGlitch(s, age, 2);
        return;
    }

    if (elapsed < kScene4End) {
        // Сцена 4: Сердце с глазами - Крест - Робот с глазами
        const uint32_t age = elapsed - kScene3End;
        drawTrio(s, kScene4, age, false);
        drawGlitch(s, age, 3);
        return;
    }

    if (elapsed < kReelsEnd) {
        // Сцена 5: Слот-машина с остановкой на Сердце - Робот - Сердце
        drawReels(s, elapsed - kScene4End);
        return;
    }

    // Финал: Сердце - Робот - Сердце зафиксированы
    drawReels(s, kReelsEnd - kScene4End);
    const uint32_t finalAge = elapsed - kReelsEnd;

    if (finalAge >= 50U && finalAge < 90U) {
        s.fillSprite(TFT_WHITE); // стильная белая вспышка
    } else {
        s.fillRect(33, 176, 174, 3, kNeon);
    }
}

} // namespace

void play(TFT_eSPI& tft, TFT_eSprite& sprite) {
    if (!sprite.created()) return;

    tft.fillScreen(TFT_BLACK);
    const uint32_t startedAt = millis();

    while (true) {
        const uint32_t frameStartedAt = millis();
        const uint32_t elapsed = frameStartedAt - startedAt;

        if (elapsed >= kTotalMs) break;

        renderFrame(sprite, elapsed);
        sprite.pushSprite(0, kScreenY);

        const uint32_t frameTime = millis() - frameStartedAt;
        if (frameTime < kFrameMs) {
            delay(kFrameMs - frameTime);
        } else {
            delay(1);
        }
    }

    // Завершение: очищаем спрайт перед штатным экраном лица
    sprite.fillSprite(TFT_BLACK);
    sprite.pushSprite(0, kScreenY);
}

} // namespace AnimIntro
