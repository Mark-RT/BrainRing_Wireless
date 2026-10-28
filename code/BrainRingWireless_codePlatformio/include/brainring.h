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
};

// Общая настройка NRF24L01 для обеих сторон
inline void radioSetupCommon(RF24 &radio)
{
    radio.begin();
    radio.setChannel(RADIO_CHANNEL);
    radio.setDataRate(RF24_1MBPS);
    radio.setPALevel(RF24_PA_MAX);
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
