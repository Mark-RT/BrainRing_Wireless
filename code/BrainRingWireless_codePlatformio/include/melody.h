// Неблокирующий плеер мелодий для пассивного бузера + набор мелодий
#pragma once

#include <Arduino.h>
#include <avr/pgmspace.h>

struct Note
{
    uint16_t freq; // Гц, 0 = пауза
    uint16_t dur;  // мс
};

// Ноты (Гц)
#define N_E4 330
#define N_F4 349
#define N_FS4 370
#define N_G4 392
#define N_C5 523
#define N_D5 587
#define N_E5 659
#define N_G5 784
#define N_A5 880
#define N_B5 988
#define N_C6 1047
#define N_D6 1175
#define N_E6 1319
#define N_G6 1568

// --- ПОБЕДА (3 варианта, выбираются случайно) ---
const Note WIN_FANFARE[] PROGMEM = {
    {N_C5, 90}, {0, 20}, {N_E5, 90}, {0, 20}, {N_G5, 90}, {0, 20}, {N_C6, 350}};
const Note WIN_TRILL[] PROGMEM = {
    {N_G5, 60}, {N_A5, 60}, {N_B5, 60}, {N_D6, 60}, {0, 40}, {N_B5, 60}, {N_G6, 300}};
const Note WIN_TADA[] PROGMEM = {
    {N_E5, 80}, {0, 50}, {N_E5, 80}, {0, 50}, {N_G5, 80}, {0, 30}, {N_E6, 400}};

// --- ПРОИГРЫШ ---
const Note LOSE_MELODY[] PROGMEM = {
    {N_G4, 180}, {0, 30}, {N_FS4, 180}, {0, 30}, {N_F4, 180}, {0, 30}, {N_E4, 420}};

// --- СЛУЖЕБНЫЕ ЗВУКИ ---
const Note SOUND_CLICK[] PROGMEM = {{1200, 15}};
const Note SOUND_CONFIRM[] PROGMEM = {{N_C5, 70}, {N_E5, 70}, {N_G5, 70}, {N_C6, 150}};
const Note ADMIN_PRESS_SOUND[] PROGMEM = {{1500, 80}, {0, 60}, {1500, 80}, {0, 60}, {2000, 150}};
const Note ADMIN_RESET_SOUND[] PROGMEM = {{2000, 25}};

#define MELODY(arr) arr, (uint8_t)(sizeof(arr) / sizeof(Note))

class MelodyPlayer
{
public:
    explicit MelodyPlayer(uint8_t pin) : _pin(pin) {}

    void begin()
    {
        pinMode(_pin, OUTPUT);
    }

    void start(const Note *melody, uint8_t len)
    {
        _melody = melody;
        _len = len;
        _idx = 0;
        _playing = true;
        playCurrent();
    }

    void stop()
    {
        _playing = false;
        noTone(_pin);
    }

    bool isPlaying() const { return _playing; }

    void update()
    {
        if (!_playing)
            return;
        if (millis() - _timer >= _dur)
        {
            _idx++;
            if (_idx < _len)
                playCurrent();
            else
                stop();
        }
    }

private:
    void playCurrent()
    {
        Note n;
        memcpy_P(&n, &_melody[_idx], sizeof(n));
        _timer = millis();
        _dur = n.dur;
        noTone(_pin); // Сброс глюка ядра LGT8F
        if (n.freq > 0)
            tone(_pin, n.freq); // БЕЗ 3-ГО АРГУМЕНТА!
    }

    uint8_t _pin;
    const Note *_melody = nullptr;
    uint8_t _len = 0;
    uint8_t _idx = 0;
    bool _playing = false;
    uint32_t _timer = 0;
    uint16_t _dur = 0;
};
