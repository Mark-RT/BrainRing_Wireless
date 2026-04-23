#include <Arduino.h>

#define PLAYER_ID 5 // Уникальный ID игрока (1 - 5)

#include <SPI.h>
#include <RF24.h>
#define CE_PIN 8
#define CSN_PIN 9
RF24 radio(CE_PIN, CSN_PIN);
byte address[][6] = {"Admin", "Node1"}; // Адреса труб

#define PIN_LED_STRIP 4 // Лента WS2812B
#define NUM_LEDS 12     // Кол-во диодов в ленте
#define COLOR_DEPTH 3
#include <microLED.h> // подключаем библу
microLED<NUM_LEDS, PIN_LED_STRIP, MLED_NO_CLOCK, LED_WS2818, ORDER_GRB, CLI_AVER> strip;
// Массив базовых цветов microLED (тип данных mData)
// 1-Красный, 2-Зеленый, 3-Синий, 4-Желтый, 5-Белый
mData playerColors[5] = {mRed, mGreen, mBlue, mYellow, mWhite};
mData myColor;

// --- ПИНЫ ---
#define PIN_BUZZER 2  // Пассивный бузер
#define PIN_BUTTON 3  // Аркадная кнопка
#define PIN_BTN_LED 6 // Светодиод внутри кнопки (через резистор)

// Системные переменные
unsigned long lastPingTime = 0;
const int PING_INTERVAL = 100; // Как часто опрашиваем Админа (в миллисекундах)

// --- НАСТРОЙКИ АСИНХРОННОЙ МЕЛОДИИ ---
// 0 в массиве частот означает тишину (паузу)
const int winFreq[] = {800, 0, 1200, 0, 1500};
const int winDur[] = {100, 50, 200, 50, 300};
const int winTotalNotes = 5;

int currentNote = 0;
unsigned long noteTimer = 0;
bool isPlaying = false;

// Состояния стейт-машины
enum State
{
  STATE_IDLE,
  STATE_WON,
  STATE_BLOCKED
};
State currentState = STATE_IDLE;

volatile bool buttonHit = false; // Флаг мгновенного нажатия
volatile unsigned long lastBtnTime = 0;
void isrButton()
{
  // Если статус IDLE и с прошлого нажатия прошло больше хх мс
  if (currentState == STATE_IDLE && (millis() - lastBtnTime > 60))
  {
    buttonHit = true;
    lastBtnTime = millis();
  }
}

// Запуск мелодии с самого начала
void startWinSequence()
{
  currentNote = 0;
  isPlaying = true;
  noteTimer = millis();

  if (winFreq[0] > 0)
  {
    noTone(PIN_BUZZER);           // Глушим старый звук для надежности
    tone(PIN_BUZZER, winFreq[0]); // БЕЗ 3-ГО АРГУМЕНТА!
  }
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
      {
        noTone(PIN_BUZZER);                     // Сброс глюка ядра LGT8F
        tone(PIN_BUZZER, winFreq[currentNote]); // БЕЗ 3-ГО АРГУМЕНТА!
      }
      else
      {
        noTone(PIN_BUZZER); // Отрабатываем паузу (0 в массиве)
      }
    }
    else
    {
      isPlaying = false;
      noTone(PIN_BUZZER); // Глушим окончательно
    }
  }
}

// Функция управления светом и звуком при смене состояний
void applyStateVisuals()
{
  if (currentState == STATE_IDLE)
  {
    // Раунд сброшен, всё гасим, готовы к нажатию
    digitalWrite(PIN_BTN_LED, HIGH);
    strip.clear(); // очистка буфера (выключить диоды, чёрный цвет)
    strip.show();  // вывод изменений на ленту
    delay(1);
    stopBuzzer(); // <--- Обрываем звук, если нажали Сброс
  }
  else if (currentState == STATE_WON)
  {
    // Мы победили! Включаем наш цвет на максимум
    digitalWrite(PIN_BTN_LED, HIGH);
    strip.fill(myColor); // заливаем цветом игрока
    strip.show();        // выводим изменения
    delay(1);
    startWinSequence(); // <--- Асинхронно запускаем победную мелодию
  }
  else if (currentState == STATE_BLOCKED)
  {
    // Нажал кто-то другой.
    digitalWrite(PIN_BTN_LED, LOW);
    stopBuzzer(); // <--- Для надежности тоже глушим звук
  }
}

// Функция смены текущего состояния с защитой от ложных срабатываний
void changeState(State newState)
{
  if (currentState != newState)
  {
    currentState = newState;
    applyStateVisuals(); // Обновляем индикацию только если статус реально изменился
  }
}

// Отправка пакета Админу и мгновенная обработка ответа (ACK Payload)
void sendToAdmin(byte dataToSend)
{
  if (radio.write(&dataToSend, sizeof(dataToSend)))
  {
    Serial.println("send");
    // Serial.print(txData[0]);
  }
  else
    Serial.println("fail");

  if (radio.isAckPayloadAvailable())
  {
    byte globalState;
    radio.read(&globalState, sizeof(globalState));

    // Анализируем ответ Админа (0 = Идет игра, 1-5 = Номер победителя)
    if (globalState == 0)
    {
      changeState(STATE_IDLE);
    }
    else if (globalState == PLAYER_ID)
    {
      changeState(STATE_WON);
    }
    else
    {
      changeState(STATE_BLOCKED);
    }
  }
}

void setup()
{
  Serial.begin(57600);

  // ===== NRF24L01 =====
  radio.begin();
  radio.setChannel(0x60);
  radio.setDataRate(RF24_1MBPS);
  radio.setPALevel(RF24_PA_MAX);
  radio.setRetries(5, 10);
  radio.enableAckPayload();
  radio.openWritingPipe(address[0]);
  radio.powerUp();
  radio.stopListening();

  // ===== WS2812 =====
  if (PLAYER_ID >= 1 && PLAYER_ID <= 5)
    myColor = playerColors[PLAYER_ID - 1];
  else
    myColor = mWhite;
  strip.setBrightness(100);
  strip.clear(); // очистка буфера (выключить диоды, чёрный цвет)
  strip.show();  // вывод изменений на ленту
  delay(1);      // между вызовами show должна быть пауза минимум 40 мкс !!!!

  pinMode(PIN_BTN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), isrButton, FALLING);
}

void loop()
{
  if (buttonHit) // 1. ПРИОРИТЕТ: Игрок ударил по кнопке
  {
    Serial.println("Press");
    buttonHit = false;
    sendToAdmin(PLAYER_ID); // Отправляем Админу свой боевой ID (я нажал!)
  }

  if (millis() - lastPingTime > PING_INTERVAL) // 2. ФОНОВЫЙ ОПРОС: Синхронизация статуса (Пинг)
  {
    lastPingTime = millis();
    if (!buttonHit) // Если по кнопке сейчас не бьют, отправляем Админу код 0 (просто спросить статус)
      sendToAdmin(0);
  }

  // Асинхронный обработчик звука
  handleMelody();
}