#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <microLED.h>

// --- ПИНЫ ---
#define CE_PIN 8
#define CSN_PIN 9
#define PIN_BUZZER 2
#define PIN_BUTTON 3    // Кнопка СБРОС (Reset)
#define PIN_LED_STRIP 4 // Индикатор Админа (WS2812B)

#define NUM_LEDS 1 // У админа 1 светодиод для статуса
#define COLOR_DEPTH 3

RF24 radio(CE_PIN, CSN_PIN);
microLED<NUM_LEDS, PIN_LED_STRIP, MLED_NO_CLOCK, LED_WS2812, ORDER_GRB, CLI_AVER> strip;

byte address[][6] = {"Admin", "Node1"};
byte pipeNo = 0;

// Массив цветов: 1-Кр, 2-Зел, 3-Син, 4-Жел, 5-Бел
mData playerColors[5] = {mRed, mGreen, mBlue, mYellow, mWhite};

// --- СИСТЕМНЫЕ ПЕРЕМЕННЫЕ АДМИНА ---
volatile bool resetHit = false;
uint8_t globalGameState = 0; // 0 = Ждем, 1-5 = Победитель
uint8_t incomingData = 0;

// --- АСИНХРОННАЯ МЕЛОДИЯ ПОВЕЩЕНИЯ ---
const int winFreq[] = {800, 0, 1200, 0, 1500};
const int winDur[] = {100, 50, 200, 50, 300};
const int winTotalNotes = 5;

int currentNote = 0;
unsigned long noteTimer = 0;
bool isPlaying = false;

void startWinSequence()
{
    currentNote = 0;
    isPlaying = true;
    noteTimer = millis();
    if (winFreq[0] > 0)
        tone(PIN_BUZZER, winFreq[0], winDur[0]);
}

void stopBuzzer()
{
    isPlaying = false;
    noTone(PIN_BUZZER);
}

void handleMelody()
{
    if (!isPlaying)
        return;
    if (millis() - noteTimer >= winDur[currentNote])
    {
        currentNote++;
        if (currentNote < winTotalNotes)
        {
            noteTimer = millis();
            if (winFreq[currentNote] > 0)
                tone(PIN_BUZZER, winFreq[currentNote], winDur[currentNote]);
        }
        else
        {
            isPlaying = false;
        }
    }
}

// Прерывание кнопки СБРОС
void isrReset()
{
    resetHit = true;
}

void setup()
{
    Serial.begin(57600);

    // Кнопка и звук
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), isrReset, FALLING);

    // Светодиод
    strip.setBrightness(100);
    strip.clear();
    strip.show();

    // Радио
    radio.begin();
    radio.setChannel(0x60);
    radio.setDataRate(RF24_2MBPS);
    radio.setPALevel(RF24_PA_MAX);
    radio.enableAckPayload();
    radio.openReadingPipe(0, address[0]);
    radio.powerUp();
    radio.startListening();

    // ЗАРАНЕЕ заряжаем трубу нулевым статусом
    radio.writeAckPayload(0, &globalGameState, sizeof(globalGameState));
}

void loop()
{
    // 1. ПРИОРИТЕТ: Нажата кнопка Сброс на пульте Админа
    if (resetHit)
    {
        resetHit = false;
        globalGameState = 0; // Сбрасываем игру
        stopBuzzer();
        strip.clear(); // Гасим светодиод Админа
        strip.show();
        delay(1);

        // Обновляем ответ в радиомодуле
        radio.writeAckPayload(0, &globalGameState, sizeof(globalGameState));
        Serial.println("System RESET");
    }

    // 2. ОБРАБОТКА РАДИО: Слушаем эфир
    if (radio.available(&pipeNo))
    {
        radio.read(&incomingData, sizeof(incomingData));

        // Если прилетел боевой пакет от игрока (1-5) И игра сейчас свободна (0)
        if (incomingData > 0 && incomingData <= 5 && globalGameState == 0)
        {
            globalGameState = incomingData; // Фиксируем победителя!
            Serial.print("WINNER: Player ");
            Serial.println(globalGameState);

            // Включаем индикацию на пульте Админа
            strip.fill(playerColors[globalGameState - 1]);
            strip.show();
            startWinSequence();
        }

        // ОБЯЗАТЕЛЬНО: Перезаряжаем трубу текущим статусом игры для следующего запроса
        radio.writeAckPayload(0, &globalGameState, sizeof(globalGameState));
    }

    // 3. ЗВУК: Крутим асинхронную мелодию
    handleMelody();
}