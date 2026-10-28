#include <Arduino.h>
#include "brainring.h"
#include "melody.h"

// --- ПИНЫ ---
#define CE_PIN A0   // плата Admin Rev. 2.1: CE перенесён с D8 на A0 (разводка в один слой)
#define CSN_PIN 9
#define PIN_BUZZER 2
#define PIN_BUTTON 3     // Кнопка СБРОС (Reset)
#define PIN_LED_STRIP 4  // Индикатор Админа (WS2812B)
#define PIN_KEEPALIVE A3 // Нагрузка Q2+R9: не даёт IP5305 отключиться при малом токе

#define NUM_LEDS 1 // У админа 1 светодиод для статуса

// --- НАСТРОЙКИ ---
const uint16_t DECISION_WINDOW_MS = 30; // окно сравнения почти одновременных нажатий
const uint32_t RESET_CHECK_MS = 60000;  // дольше этого после сброса проверка "нажал до сброса" не нужна
const uint16_t LED_FRAME_MS = 20;

RF24 radio(CE_PIN, CSN_PIN);
microLED<NUM_LEDS, PIN_LED_STRIP, MLED_NO_CLOCK, LED_WS2812, ORDER_GRB, CLI_AVER> strip;
MelodyPlayer buzzer(PIN_BUZZER);

AdminState gameState = {0, 0};

// --- КНОПКА СБРОСА ---
volatile bool resetHit = false;
volatile uint32_t resetIsrUs = 0;
volatile unsigned long lastRstTime = 0;

uint32_t resetUs = 0; // момент последнего сброса (micros)
uint32_t resetMs = 0; // он же в millis (для защиты от переполнения micros)

void isrReset()
{
    if (millis() - lastRstTime > 60)
    {
        resetIsrUs = micros();
        resetHit = true;
        lastRstTime = millis();
    }
}

// --- ОКНО РЕШЕНИЯ: кто нажал раньше всех ---
bool windowOpen = false;
uint32_t windowStartMs = 0;
uint8_t bestId = 0;
uint32_t bestPressUs = 0;
uint32_t runnerUpDeltaUs = 0; // отрыв от второго (для отладки)
uint8_t runnerUpId = 0;

// --- ИНДИКАЦИЯ ---
enum LedMode
{
    LED_IDLE,
    LED_RESET_FLASH,
    LED_WINNER
};
LedMode ledMode = LED_IDLE;
uint32_t ledModeSince = 0;
uint32_t lastLedFrame = 0;

void setLedMode(LedMode mode)
{
    ledMode = mode;
    ledModeSince = millis();
}

// Перезаряжаем ACK payload актуальным статусом (без устаревших копий в FIFO)
void loadAckPayload()
{
    radio.flush_tx();
    radio.writeAckPayload(0, &gameState, sizeof(gameState));
}

// Нажатие произошло после последнего сброса?
bool pressedAfterReset(uint32_t pressAgeUs, uint32_t nowUs)
{
    if (millis() - resetMs > RESET_CHECK_MS)
        return true;
    return pressAgeUs <= nowUs - resetUs;
}

void handleReset()
{
    if (!resetHit)
        return;

    noInterrupts();
    resetUs = resetIsrUs;
    resetHit = false;
    interrupts();
    resetMs = millis();

    gameState.winner = 0;
    gameState.round++;
    windowOpen = false;
    loadAckPayload();

    buzzer.start(MELODY(ADMIN_RESET_SOUND));
    setLedMode(LED_RESET_FLASH);
    Serial.print("RESET, round ");
    Serial.println(gameState.round);
}

void registerPress(uint8_t id, uint32_t pressUs)
{
    if (!windowOpen)
    {
        windowOpen = true;
        windowStartMs = millis();
        bestId = id;
        bestPressUs = pressUs;
        runnerUpId = 0;
        return;
    }
    if (id == bestId)
        return; // повтор того же нажатия

    int32_t diff = (int32_t)(pressUs - bestPressUs);
    if (diff < 0) // этот игрок нажал раньше текущего лидера
    {
        runnerUpId = bestId;
        runnerUpDeltaUs = (uint32_t)(-diff);
        bestId = id;
        bestPressUs = pressUs;
    }
    else if (runnerUpId == 0 || (uint32_t)diff < runnerUpDeltaUs)
    {
        runnerUpId = id;
        runnerUpDeltaUs = (uint32_t)diff;
    }
}

void handleRadio()
{
    while (radio.available())
    {
        uint32_t nowUs = micros();
        PlayerPacket pkt;
        radio.read(&pkt, sizeof(pkt));

        if (pkt.type == PKT_PRESS && pkt.id >= 1 && pkt.id <= MAX_PLAYERS && gameState.winner == 0)
        {
            if (pressedAfterReset(pkt.pressAgeUs, nowUs))
                registerPress(pkt.id, nowUs - pkt.pressAgeUs);
            else
            {
                Serial.print("Ignored (pressed before reset): Player ");
                Serial.println(pkt.id);
            }
        }

        // ОБЯЗАТЕЛЬНО: перезаряжаем трубу текущим статусом для следующего запроса
        loadAckPayload();
    }
}

void resolveWinner()
{
    if (!windowOpen || millis() - windowStartMs < DECISION_WINDOW_MS)
        return;

    windowOpen = false;
    gameState.winner = bestId; // Фиксируем победителя!
    loadAckPayload();

    buzzer.start(MELODY(ADMIN_PRESS_SOUND));
    setLedMode(LED_WINNER);

    Serial.print("WINNER: Player ");
    Serial.print(bestId);
    if (runnerUpId)
    {
        Serial.print(" (Player ");
        Serial.print(runnerUpId);
        Serial.print(" +");
        Serial.print(runnerUpDeltaUs / 1000.0, 1);
        Serial.print(" ms)");
    }
    Serial.println();
}

void updateLed()
{
    if (millis() - lastLedFrame < LED_FRAME_MS)
        return;
    lastLedFrame = millis();
    uint32_t t = millis() - ledModeSince;

    switch (ledMode)
    {
    case LED_IDLE: // тусклое зелёное "дыхание" = готов
        strip.fill(scaleColor(mGreen, 5 + triangleWave(t, 3000) / 5));
        break;

    case LED_RESET_FLASH: // короткая белая вспышка
        if (t < 100)
            strip.fill(mWhite);
        else
        {
            setLedMode(LED_IDLE);
            strip.clear();
        }
        break;

    case LED_WINNER: // 3 вспышки цветом победителя, затем ровно
        if (t < 480 && (t / 80) % 2)
            strip.clear();
        else
            strip.fill(playerColor(gameState.winner));
        break;
    }
    strip.show();
}

// --- ПОДДЕРЖАНИЕ ПИТАНИЯ IP5305 ---
// IP5305 выключает выход 5V, если ток долго меньше ~45 мА.
// Раз в 15 с включаем нагрузку ~100 мА на 300 мс (в среднем ~2 мА).
const unsigned long KEEPALIVE_PERIOD = 15000;
const unsigned long KEEPALIVE_PULSE = 300;

unsigned long keepAliveTimer = 0;
bool keepAliveOn = false;

void handleKeepAlive()
{
    if (!keepAliveOn && millis() - keepAliveTimer >= KEEPALIVE_PERIOD)
    {
        digitalWrite(PIN_KEEPALIVE, HIGH);
        keepAliveOn = true;
        keepAliveTimer = millis();
    }
    else if (keepAliveOn && millis() - keepAliveTimer >= KEEPALIVE_PULSE)
    {
        digitalWrite(PIN_KEEPALIVE, LOW);
        keepAliveOn = false;
        keepAliveTimer = millis();
    }
}

void setup()
{
    Serial.begin(57600);

    // Кнопка и звук
    buzzer.begin();
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), isrReset, FALLING);

    // Нагрузка для IP5305 (выключена до первого импульса)
    pinMode(PIN_KEEPALIVE, OUTPUT);
    digitalWrite(PIN_KEEPALIVE, LOW);

    // Светодиод
    strip.setBrightness(100);
    strip.clear();
    strip.show();

    // Радио
    radioSetupCommon(radio);
    radio.openReadingPipe(0, RADIO_ADDRESS);
    radio.powerUp();
    radio.startListening();

    // ЗАРАНЕЕ заряжаем трубу нулевым статусом
    loadAckPayload();
    Serial.println("Admin ready");
}

void loop()
{
    handleReset();   // 1. ПРИОРИТЕТ: кнопка Сброс
    handleRadio();   // 2. Слушаем эфир, собираем нажатия
    resolveWinner(); // 3. Окно закрылось -> объявляем победителя
    updateLed();     // 4. Световая индикация
    buzzer.update(); // 5. Звук
    handleKeepAlive(); // 6. Не даём IP5305 уснуть
}
