// Прошивка игрока: ОДНА для всех плат.
// ID (= цвет) хранится в EEPROM и выбирается кнопкой:
//  - память пустая -> лента мигает белым, удержать кнопку 2 с -> меню выбора цвета
//  - держать кнопку при включении 2 с -> меню выбора цвета (смена ID)
//  - в меню: клик = следующий цвет, удержание 2 с = сохранить
// 1-Красный, 2-Зеленый, 3-Синий, 4-Желтый, 5-Белый
#include <Arduino.h>
#include <EEPROM.h>
#include "brainring.h"
#include "melody.h"

// --- ПИНЫ ---
#define CE_PIN 8
#define CSN_PIN 9
#define PIN_BUZZER 2    // Пассивный бузер
#define PIN_BUTTON 3    // Аркадная кнопка
#define PIN_LED_STRIP 4 // Лента WS2812B
#define PIN_BTN_LED 6   // Светодиод внутри кнопки (через резистор)
#define PIN_NOISE A6    // Свободный аналоговый пин для randomSeed

#define NUM_LEDS 20 // Кол-во диодов в ленте

// --- EEPROM ---
#define EE_MAGIC_ADDR 0
#define EE_ID_ADDR 1
#define EE_CHECK_ADDR 2
#define EE_MAGIC 0xB7

// --- НАСТРОЙКИ ---
const uint16_t PING_INTERVAL = 100;      // обычный опрос Админа, мс
const uint16_t FAST_PING_INTERVAL = 15;  // частый опрос сразу после нажатия
const uint16_t FAST_PING_DURATION = 300; // сколько длится частый опрос
const uint16_t PRESS_GIVEUP_MS = 1500;   // столько пытаемся доставить нажатие
const uint16_t LINK_TIMEOUT_MS = 1000;   // нет ответа дольше -> "нет связи"
const uint16_t HOLD_MS = 2000;           // длинное удержание кнопки
const uint32_t RELEASE_STABLE_US = 20000; // кнопка должна быть отпущена столько перед нажатием
const uint16_t FRAME_MS = 20;            // 50 кадров/с

RF24 radio(CE_PIN, CSN_PIN);
microLED<NUM_LEDS, PIN_LED_STRIP, MLED_NO_CLOCK, LED_WS2818, ORDER_GRB, CLI_AVER> strip;
MelodyPlayer buzzer(PIN_BUZZER);

uint8_t playerId = 0;
mData myColor;

// Состояния стейт-машины
enum State
{
  STATE_SETUP_EMPTY, // в памяти нет ID, ждём удержания кнопки
  STATE_SETUP_MENU,  // выбор цвета
  STATE_SETUP_SAVED, // подтверждение сохранения
  STATE_NO_LINK,     // нет связи с Админом
  STATE_IDLE,        // ждём нажатия
  STATE_WON,
  STATE_LOST
};
State currentState = STATE_SETUP_EMPTY;
uint32_t stateSince = 0;

uint8_t menuId = 1;       // кандидат в меню выбора
uint8_t resultRound = 0xFF; // раунд, для которого уже сыграна мелодия

// --- РАДИО / СИНХРОНИЗАЦИЯ ---
bool gameActive = false;
uint32_t lastPingTime = 0;
uint32_t lastAckTime = 0;
uint32_t fastPingUntil = 0;
uint32_t lastFrame = 0;

// --- КНОПКА В ИГРЕ (прерывание) ---
volatile bool pressPending = false;
volatile uint32_t pressMicros = 0;
volatile uint32_t lastReleaseUs = 0;

void isrButton()
{
  uint32_t now = micros();
  if (digitalRead(PIN_BUTTON) == HIGH) // отпускание
  {
    lastReleaseUs = now;
    return;
  }
  // Нажатие засчитываем, только если до этого кнопка была стабильно отпущена (антидребезг)
  if (!gameActive || pressPending || now - lastReleaseUs < RELEASE_STABLE_US)
    return;
  pressMicros = now;
  pressPending = true;
}

// --- КНОПКА В МЕНЮ (опрос) ---
enum ButtonEvent
{
  BTN_NONE,
  BTN_CLICK,
  BTN_LONG
};

bool btnDown = false;
bool btnLongFired = false;
uint32_t btnDownSince = 0;
uint32_t btnLastChange = 0;

void buttonSync()
{
  btnDown = digitalRead(PIN_BUTTON) == LOW;
  btnLongFired = btnDown; // уже зажатая кнопка не даст ложного клика при отпускании
  btnDownSince = millis();
  btnLastChange = millis();
}

ButtonEvent pollButton()
{
  bool down = digitalRead(PIN_BUTTON) == LOW;
  if (down != btnDown && millis() - btnLastChange > 20)
  {
    btnDown = down;
    btnLastChange = millis();
    if (down)
    {
      btnDownSince = millis();
      btnLongFired = false;
    }
    else if (!btnLongFired)
      return BTN_CLICK;
  }
  if (btnDown && !btnLongFired && millis() - btnDownSince >= HOLD_MS)
  {
    btnLongFired = true;
    return BTN_LONG;
  }
  return BTN_NONE;
}

// --- EEPROM ---
uint8_t loadPlayerId()
{
  uint8_t id = EEPROM.read(EE_ID_ADDR);
  if (EEPROM.read(EE_MAGIC_ADDR) == EE_MAGIC && EEPROM.read(EE_CHECK_ADDR) == (uint8_t)~id &&
      id >= 1 && id <= MAX_PLAYERS)
    return id;
  return 0;
}

void savePlayerId(uint8_t id)
{
  EEPROM.update(EE_ID_ADDR, id);
  EEPROM.update(EE_CHECK_ADDR, (uint8_t)~id);
  EEPROM.update(EE_MAGIC_ADDR, EE_MAGIC);
}

// --- СОСТОЯНИЯ ---
void enterState(State newState, bool withSound)
{
  currentState = newState;
  stateSince = millis();

  switch (newState)
  {
  case STATE_IDLE: // Раунд сброшен, готовы к нажатию
    buzzer.stop(); // Обрываем звук, если нажали Сброс
    break;
  case STATE_WON:
    if (withSound)
    {
      static const Note *const wins[] = {WIN_FANFARE, WIN_TRILL, WIN_TADA};
      static const uint8_t lens[] = {sizeof(WIN_FANFARE) / sizeof(Note), sizeof(WIN_TRILL) / sizeof(Note),
                                     sizeof(WIN_TADA) / sizeof(Note)};
      uint8_t i = random(3);
      buzzer.start(wins[i], lens[i]);
    }
    break;
  case STATE_LOST: // Нажал кто-то другой
    if (withSound)
      buzzer.start(MELODY(LOSE_MELODY));
    break;
  case STATE_NO_LINK:
    buzzer.stop();
    break;
  default:
    break;
  }
}

// Смена состояния только если оно реально изменилось
void changeState(State newState)
{
  if (currentState != newState)
    enterState(newState, false);
}

// Анализируем ответ Админа
void applyAdminState(const AdminState &s)
{
  if (s.winner == 0)
  {
    changeState(STATE_IDLE);
    return;
  }
  State target = (s.winner == playerId) ? STATE_WON : STATE_LOST;
  bool newRound = s.round != resultRound;
  if (currentState != target || newRound)
  {
    enterState(target, newRound); // мелодия - один раз за раунд
    resultRound = s.round;
  }
}

// --- РАДИО ---
// Отправка пакета Админу и обработка ответа (ACK payload)
bool sendPacket(uint8_t type, uint32_t pressAgeUs)
{
  PlayerPacket pkt = {type, playerId, pressAgeUs};
  if (!radio.write(&pkt, sizeof(pkt)))
    return false;

  lastAckTime = millis();
  while (radio.available())
  {
    AdminState s;
    radio.read(&s, sizeof(s));
    applyAdminState(s);
  }
  return true;
}

// Доставляем нажатие, пока Админ не подтвердит приём.
// Перед каждой попыткой пересчитываем "возраст" нажатия -> Админ знает точный момент нажатия.
void handlePress()
{
  if (!pressPending)
    return;

  uint32_t start = millis();
  bool delivered = false;
  while (millis() - start < PRESS_GIVEUP_MS)
  {
    noInterrupts();
    uint32_t pressedAt = pressMicros;
    interrupts();
    if (sendPacket(PKT_PRESS, micros() - pressedAt))
    {
      delivered = true;
      break;
    }
    delayMicroseconds(random(250, 2000)); // разносим повторы при коллизии
    buzzer.update();
  }
  pressPending = false;

  if (delivered)
    fastPingUntil = millis() + FAST_PING_DURATION;
  Serial.println(delivered ? "Press sent" : "Press FAIL");
}

void handlePing()
{
  uint16_t interval = (int32_t)(fastPingUntil - millis()) > 0 ? FAST_PING_INTERVAL : PING_INTERVAL;
  if (millis() - lastPingTime < interval)
    return;
  lastPingTime = millis();
  sendPacket(PKT_PING, 0);

  if (millis() - lastAckTime > LINK_TIMEOUT_MS)
    changeState(STATE_NO_LINK);
}

void startGame()
{
  myColor = playerColor(playerId);

  radioSetupCommon(radio);
  radio.setRetries(playerId + 1, 3); // у каждого игрока своя задержка повтора: (id+2)*250 мкс
  radio.openWritingPipe(RADIO_ADDRESS);
  radio.powerUp();
  radio.stopListening();

  lastAckTime = millis();
  enterState(STATE_NO_LINK, false); // до первого ответа Админа
  gameActive = true;
  Serial.print("Player ");
  Serial.println(playerId);
}

// --- МЕНЮ ВЫБОРА ЦВЕТА ---
void handleSetup()
{
  ButtonEvent ev = pollButton();

  if (currentState == STATE_SETUP_EMPTY)
  {
    if (ev == BTN_LONG)
    {
      buzzer.start(MELODY(SOUND_CLICK));
      enterState(STATE_SETUP_MENU, false);
    }
  }
  else if (currentState == STATE_SETUP_MENU)
  {
    if (ev == BTN_CLICK)
    {
      menuId = menuId % MAX_PLAYERS + 1;
      buzzer.start(MELODY(SOUND_CLICK));
    }
    else if (ev == BTN_LONG)
    {
      playerId = menuId;
      savePlayerId(playerId);
      buzzer.start(MELODY(SOUND_CONFIRM));
      enterState(STATE_SETUP_SAVED, false);
    }
  }
  else if (currentState == STATE_SETUP_SAVED && millis() - stateSince > 900)
  {
    startGame();
  }
}

// Яркость светодиода кнопки 0..255 с гамма-коррекцией (глазу ШИМ кажется нелинейным)
void setButtonLed(uint8_t level)
{
  analogWrite(PIN_BTN_LED, (uint16_t)level * level / 255);
}

// --- АНИМАЦИИ ЛЕНТЫ + КНОПКИ ---
// Кнопка светит белым, цвет даёт её пластик, поэтому анимируем только яркость - синхронно с лентой
void renderStrip()
{
  if (millis() - lastFrame < FRAME_MS)
    return;
  lastFrame = millis();
  uint32_t t = millis() - stateSince;
  uint8_t btn = 0;

  switch (currentState)
  {
  case STATE_SETUP_EMPTY: // мигаем белым: "выбери цвет"
    if ((t / 500) % 2)
      strip.clear();
    else
    {
      strip.fill(scaleColor(mWhite, 120));
      btn = 255;
    }
    break;

  case STATE_SETUP_MENU: // показываем кандидата, кнопка часто мигает: "идёт выбор"
    strip.fill(playerColor(menuId));
    btn = ((t / 250) % 2) ? 0 : 255;
    break;

  case STATE_SETUP_SAVED: // 3 вспышки выбранным цветом
    if ((t / 150) % 2)
      strip.clear();
    else
    {
      strip.fill(playerColor(playerId));
      btn = 255;
    }
    break;

  case STATE_NO_LINK: // тусклая красная точка бегает по ленте, кнопка "моргает" раз в круг
  {
    strip.clear();
    uint8_t pos = (t / 60) % NUM_LEDS;
    strip.set(pos, scaleColor(mRed, 80));
    btn = (pos < 2) ? 120 : 0;
    break;
  }

  case STATE_IDLE: // "дыхание" цветом игрока, кнопка дышит в такт
  {
    uint8_t level = 30 + (uint16_t)triangleWave(t, 2500) * 225 / 255;
    strip.fill(scaleColor(myColor, level));
    btn = level;
    break;
  }

  case STATE_WON:
    if (t < 1500) // вспышки + белый бегущий огонь, кнопка стробит в такт вспышкам
    {
      bool flash = (t / 100) % 2;
      strip.fill(flash ? myColor : scaleColor(myColor, 60));
      uint8_t pos = (t / 25) % NUM_LEDS;
      strip.set(pos, mWhite);
      strip.set(NUM_LEDS - 1 - pos, mWhite);
      btn = flash ? 255 : 40;
    }
    else
    {
      strip.fill(myColor);
      btn = 255;
    }
    break;

  case STATE_LOST: // гаснем от краёв к центру и остаёмся тусклыми
  {
    mData dim = scaleColor(myColor, 25);
    strip.fill(myColor);
    uint8_t edge = (t >= 600) ? NUM_LEDS / 2 : t * (NUM_LEDS / 2) / 600;
    for (uint8_t i = 0; i < edge; i++)
    {
      strip.set(i, dim);
      strip.set(NUM_LEDS - 1 - i, dim);
    }
    // Кнопка: гаснет за 0.6 с, тёмная до 2 с, затем мягко пульсирует 10%..50%
    if (t < 600)
      btn = 255 - t * 255 / 600;
    else if (t < 2000)
      btn = 0;
    else
      btn = 26 + (uint16_t)triangleWave(t - 2000, 2000) * (128 - 26) / 255;
    break;
  }
  }
  strip.show();
  setButtonLed(btn);
}

void setup()
{
  Serial.begin(57600);
  randomSeed(analogRead(PIN_NOISE) ^ micros());

  pinMode(PIN_BTN_LED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  buzzer.begin();

  // ===== WS2812 =====
  strip.setBrightness(100);
  strip.clear();
  strip.show();
  delay(1); // между вызовами show должна быть пауза минимум 40 мкс !!!!

  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), isrButton, CHANGE);

  playerId = loadPlayerId();

  // Кнопка зажата при включении -> держим 2 с и попадаем в меню выбора цвета
  bool forceMenu = false;
  if (digitalRead(PIN_BUTTON) == LOW)
  {
    uint32_t t = millis();
    while (digitalRead(PIN_BUTTON) == LOW && millis() - t < HOLD_MS)
      ;
    forceMenu = millis() - t >= HOLD_MS;
  }

  buttonSync();
  if (forceMenu)
  {
    menuId = playerId ? playerId : 1;
    buzzer.start(MELODY(SOUND_CLICK));
    enterState(STATE_SETUP_MENU, false);
  }
  else if (playerId == 0)
    enterState(STATE_SETUP_EMPTY, false);
  else
    startGame();
}

void loop()
{
  if (gameActive)
  {
    handlePress(); // 1. ПРИОРИТЕТ: игрок ударил по кнопке
    handlePing();  // 2. ФОНОВЫЙ ОПРОС: синхронизация статуса
  }
  else
    handleSetup();

  renderStrip();   // 3. Анимации
  buzzer.update(); // 4. Асинхронный звук
}
