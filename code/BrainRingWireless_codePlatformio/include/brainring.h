// Общие настройки и протокол BrainRing Wireless (Admin + Players)
#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>

#ifndef COLOR_DEPTH
#define COLOR_DEPTH 3
#endif
#include <microLED.h>

// --- РАДИО ---
#define RADIO_CHANNEL 0x60
const byte RADIO_ADDRESS[6] = "Admin";

// --- ИГРОКИ ---
#define MAX_PLAYERS 5
// ID 1..5: 1-Красный, 2-Зеленый, 3-Синий, 4-Желтый, 5-Белый
const mData PLAYER_COLORS[MAX_PLAYERS] = {mRed, mGreen, mBlue, mYellow, mWhite};

inline mData playerColor(uint8_t id)
{
    return (id >= 1 && id <= MAX_PLAYERS) ? PLAYER_COLORS[id - 1] : mWhite;
}

// --- ЯРКОСТЬ ЛЕНТЫ ---
// Уровень хранит и раздаёт Админ: 0 = 100%, 1 = 70%, 2 = 50%
#define BRIGHTNESS_LEVELS 3

// --- ПРОТОКОЛ ---
enum PacketType : uint8_t
{
    PKT_PING = 0,  // "какой статус игры?"
    PKT_PRESS = 1, // "я нажал!"
};

// Игрок -> Админ
struct __attribute__((packed)) PlayerPacket
{
    uint8_t type;        // PacketType
    uint8_t id;          // ID игрока 1..5
    uint32_t pressAgeUs; // сколько мкс прошло с момента нажатия (для PKT_PRESS)
};

// Админ -> Игрок (ACK payload)
struct __attribute__((packed)) AdminState
{
    uint8_t winner; // 0 = ждём нажатия, 1..5 = победитель
    uint8_t round;  // номер раунда, растёт при каждом сбросе
    uint8_t brightness; // уровень яркости ленты 0..BRIGHTNESS_LEVELS-1
};

// --- САЙМОН ---
// Режим определяется по размеру ACK payload: sizeof(SimonState) = Саймон, sizeof(AdminState) = брейн-ринг
enum SimonPhase : uint8_t
{
    SIMON_LOBBY,  // ждём старта, игроки подключаются
    SIMON_READY,  // отсчёт 3-2-1
    SIMON_INTRO,  // показ счёта и жизней перед раундом
    SIMON_SHOW,   // Админ показывает последовательность
    SIMON_INPUT,  // команда повторяет
    SIMON_OK,     // раунд пройден
    SIMON_ERROR,  // ошибка, минус жизнь
    SIMON_PAUSE,  // участник пропал со связи
    SIMON_OVER    // конец игры, итоговый счёт
};

#define SIMON_FLAG_HURRY 0x01  // осталось меньше секунды на нажатие
#define SIMON_FLAG_SILENT 0x02 // тихий раунд: показ без нот
#define SIMON_FLAG_BURST 0x04  // спурт: показ быстрее обычного
#define SIMON_FLAG_WIN 0x08    // пройдены все шаги

// Админ -> Игрок (ACK payload в режиме Саймон)
struct __attribute__((packed)) SimonState
{
    uint8_t phase;    // SimonPhase
    uint8_t active;   // SHOW: кто светит (0 = пауза), ERROR: виновник, PAUSE: кто пропал
    uint8_t stepId;   // растёт с каждым шагом и сменой фазы
    uint8_t score;    // очки команды
    uint8_t lives;    // оставшиеся жизни
    uint8_t progress; // INPUT: 0..255 доля введённых шагов, READY: номер отсчёта
    uint8_t flags;    // SIMON_FLAG_*
    uint8_t brightness; // уровень яркости ленты 0..BRIGHTNESS_LEVELS-1
};

const uint16_t SIMON_REVEAL_DELAY_MS = 1200; // пауза перед подсчётом очков в конце игры
const uint16_t SIMON_REVEAL_STEP_MS = 120;   // одно очко при подсчёте

// Счёт набран к моменту t после начала фазы OVER
inline uint8_t simonRevealed(uint32_t t, uint8_t score)
{
    if (t < SIMON_REVEAL_DELAY_MS)
        return 0;
    uint32_t n = (t - SIMON_REVEAL_DELAY_MS) / SIMON_REVEAL_STEP_MS;
    return n < score ? n : score;
}

// --- КНОПКА (опрос): клик и длинное удержание ---
const uint16_t HOLD_MS = 2000; // длинное удержание кнопки

enum ButtonEvent
{
    BTN_NONE,
    BTN_CLICK,
    BTN_LONG
};

class PolledButton
{
public:
    explicit PolledButton(uint8_t pin) : _pin(pin) {}

    void sync()
    {
        _down = digitalRead(_pin) == LOW;
        _longFired = _down; // уже зажатая кнопка не даст ложного клика при отпускании
        _downSince = millis();
        _lastChange = millis();
    }

    ButtonEvent poll()
    {
        bool down = digitalRead(_pin) == LOW;
        if (down != _down && millis() - _lastChange > 20)
        {
            _down = down;
            _lastChange = millis();
            if (down)
            {
                _downSince = millis();
                _longFired = false;
            }
            else if (!_longFired)
                return BTN_CLICK;
        }
        if (_down && !_longFired && millis() - _downSince >= HOLD_MS)
        {
            _longFired = true;
            return BTN_LONG;
        }
        return BTN_NONE;
    }

private:
    uint8_t _pin;
    bool _down = false;
    bool _longFired = false;
    uint32_t _downSince = 0;
    uint32_t _lastChange = 0;
};

// Общая настройка NRF24L01 для обеих сторон
inline void radioSetupCommon(RF24 &radio)
{
    radio.begin();
    radio.setChannel(RADIO_CHANNEL);
    radio.setDataRate(RF24_1MBPS);
    radio.setPALevel(RF24_PA_LOW); // на PA_MAX рядом стоящие платы почти не слышат друг друга (<1% пакетов)
    radio.enableAckPayload();
}

// Треугольная волна 0..255 с периодом periodMs (для "дыхания")
inline uint8_t triangleWave(uint32_t t, uint16_t periodMs)
{
    uint16_t phase = t % periodMs;
    uint16_t half = periodMs / 2;
    if (phase < half)
        return (uint32_t)phase * 255 / half;
    return (uint32_t)(periodMs - phase) * 255 / half;
}

// Цвет с яркостью level (0..255)
inline mData scaleColor(mData c, uint8_t level)
{
    return getFade(c, 255 - level);
}
