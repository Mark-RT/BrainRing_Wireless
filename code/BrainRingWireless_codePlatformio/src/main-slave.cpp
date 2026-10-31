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
#define PIN_KEEPALIVE A3 // Нагрузка Q2+R10: не даёт IP5305 отключиться при малом токе

#define NUM_LEDS 52 // Кол-во диодов в ленте

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
const uint32_t RELEASE_STABLE_US = 20000; // кнопка должна быть отпущена столько перед нажатием
const uint16_t FRAME_MS = 20;            // 50 кадров/с
const uint16_t WIN_ANIM_MS = 3500;       // длительность анимации победителя
const uint8_t BTN_LED_MIN = 26;          // 10% ШИМ: нижняя граница яркости кнопки, она и так тусклая
const uint16_t BRIGHTNESS_PREVIEW_MS = 1000; // столько лента горит ровно после смены яркости
// Значения setBrightness (идёт через гамму microLED): реальный ШИМ 33, 23 и 17 = 100%, 70% и 50%
const uint8_t STRIP_BRIGHTNESS[BRIGHTNESS_LEVELS] = {100, 82, 68};
const uint16_t SIMON_PING_INTERVAL = 25; // опрос Админа в Саймоне: подсветка следует за его командами
const uint16_t SIMON_PRESS_FLASH_MS = 150; // вспышка и нота на своём пульте при нажатии
const uint16_t SIMON_NOTE_MAX_MS = 1500;   // нота показа не тянется дольше, даже если связь пропала
const uint8_t SIMON_RULER_LEN = 45;        // LED на линейку очков (дальше - жизни)

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
  STATE_LOST,
  STATE_SIMON        // Саймон: подфазу задаёт Админ в SimonState
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
PolledButton menuButton(PIN_BUTTON);

// --- САЙМОН ---
SimonState simon = {0xFF, 0, 0, 0, 0, 0, 0}; // последнее состояние от Админа
uint32_t simonPhaseSince = 0;
uint32_t simonStepSince = 0;
uint32_t pressFlashUntil = 0;
bool noteOn = false;
uint32_t noteUntil = 0;

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

// Уровень яркости приходит от Админа в каждом ответе
bool brightnessKnown = false;
uint8_t brightnessLevel = 0;
uint32_t brightnessPreviewUntil = 0;

void applyBrightness(uint8_t level)
{
  if (level >= BRIGHTNESS_LEVELS || (brightnessKnown && level == brightnessLevel))
    return;
  if (brightnessKnown) // при включении просто применяем, показываем только смену
    brightnessPreviewUntil = millis() + BRIGHTNESS_PREVIEW_MS;
  brightnessKnown = true;
  brightnessLevel = level;
  strip.setBrightness(STRIP_BRIGHTNESS[level]);
}

// Анализируем ответ Админа
void applyAdminState(const AdminState &s)
{
  applyBrightness(s.brightness);
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
    if (!newRound)
      stateSince = millis() - WIN_ANIM_MS; // связь вернулась в том же раунде: без повтора вспышек, сразу ровное свечение
    resultRound = s.round;
  }
}

// --- САЙМОН: звук своего цвета ---
void simonNote(uint16_t durationMs)
{
  buzzer.hold(SIMON_TONES[playerId - 1]);
  noteOn = true;
  noteUntil = millis() + durationMs;
}

void simonNoteOff()
{
  if (!noteOn)
    return;
  noteOn = false;
  buzzer.stop();
}

void simonUpdateNote()
{
  if (noteOn && (int32_t)(millis() - noteUntil) >= 0)
    simonNoteOff();
}

// Анализируем ответ Админа в режиме Саймон
void applySimonState(const SimonState &s)
{
  applyBrightness(s.brightness);
  bool newPhase = s.phase != simon.phase;
  bool newStep = s.stepId != simon.stepId;
  simon = s;

  if (currentState != STATE_SIMON)
    enterState(STATE_SIMON, false);
  if (newPhase)
    simonPhaseSince = millis();
  if (newStep)
    simonStepSince = millis();

  if (s.phase == SIMON_SHOW)
  {
    if (s.active != playerId)
      simonNoteOff();
    else if (newStep && !(s.flags & SIMON_FLAG_SILENT))
      simonNote(SIMON_NOTE_MAX_MS);
  }
  else if (s.phase != SIMON_INPUT)
    simonNoteOff();

  if (s.phase == SIMON_ERROR && newStep && s.active == playerId)
    buzzer.start(MELODY(LOSE_MELODY));
}

void renderStrip();

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
    uint8_t len = radio.getDynamicPayloadSize();
    if (len == sizeof(SimonState))
    {
      SimonState s;
      radio.read(&s, sizeof(s));
      applySimonState(s);
    }
    else if (len == sizeof(AdminState))
    {
      AdminState s;
      radio.read(&s, sizeof(s));
      applyAdminState(s);
    }
    else
      radio.flush_rx();
  }
  return true;
}

// Доставляем нажатие, пока Админ не подтвердит приём.
// Перед каждой попыткой пересчитываем "возраст" нажатия -> Админ знает точный момент нажатия.
void handlePress()
{
  if (!pressPending)
    return;

  if (currentState == STATE_SIMON)
  {
    if (simon.phase != SIMON_INPUT) // нажатия вне ввода не считаются
    {
      pressPending = false;
      return;
    }
    // Отклик сразу на своём пульте, не дожидаясь Админа
    pressFlashUntil = millis() + SIMON_PRESS_FLASH_MS;
    simonNote(SIMON_PRESS_FLASH_MS);
    lastFrame = 0;
    renderStrip();
  }

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
  uint16_t interval = (int32_t)(fastPingUntil - millis()) > 0 ? FAST_PING_INTERVAL
                      : currentState == STATE_SIMON           ? SIMON_PING_INTERVAL
                                                              : PING_INTERVAL;
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
  radio.setRetries(playerId + 1, 15); // у каждого игрока своя задержка повтора: (id+2)*250 мкс
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
  ButtonEvent ev = menuButton.poll();

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

// Яркость светодиода кнопки 0..255 с гамма-коррекцией (глазу ШИМ кажется нелинейным).
// Уровень 0 = BTN_LED_MIN: кнопка не гаснет совсем
void setButtonLed(uint8_t level)
{
  uint16_t gamma = (uint16_t)level * level / 255;
  analogWrite(PIN_BTN_LED, BTN_LED_MIN + gamma * (255 - BTN_LED_MIN) / 255);
}

// Линейка очков: 1 очко = 1 LED, каждый 5-й белый; после полного круга старый круг остаётся тусклым
void drawScoreRuler(uint8_t score)
{
  uint8_t laps = score / SIMON_RULER_LEN;
  uint8_t rest = score % SIMON_RULER_LEN;
  for (uint8_t i = 0; i < SIMON_RULER_LEN; i++)
  {
    if (i < rest)
      strip.set(i, (i % 5 == 4) ? mData(mWhite) : scaleColor(myColor, 90));
    else if (laps)
      strip.set(i, scaleColor(myColor, 15));
  }
}

// Жизни: красные точки в конце ленты
void drawLives(uint8_t lives)
{
  for (uint8_t i = 0; i < lives; i++)
    strip.set(NUM_LEDS - 1 - i * 2, mRed);
}

// Анимации Саймона, возвращает яркость кнопки
uint8_t renderSimon()
{
  uint32_t t = millis() - simonPhaseSince;
  uint8_t btn = 0;
  strip.clear();

  switch (simon.phase)
  {
  case SIMON_LOBBY: // змейка своим цветом: "ты в игре, ждём старта"
  {
    uint8_t head = (t / 40) % NUM_LEDS;
    for (uint8_t i = 0; i < 8; i++)
      strip.set((head + NUM_LEDS - i) % NUM_LEDS, scaleColor(myColor, 255 - i * 30));
    btn = triangleWave(t, 2500);
    break;
  }

  case SIMON_READY: // вспышка на каждый счёт отсчёта
    if (millis() - simonStepSince < 250)
    {
      strip.fill(myColor);
      btn = 255;
    }
    break;

  case SIMON_INTRO: // особый раунд объявляем стробом, затем счёт и жизни
    if ((simon.flags & (SIMON_FLAG_BURST | SIMON_FLAG_SILENT)) && t < 400)
    {
      if ((t / 60) % 2)
        strip.fill(mWhite);
      btn = ((t / 60) % 2) ? 255 : 0;
    }
    else
    {
      drawScoreRuler(simon.score);
      drawLives(simon.lives);
    }
    break;

  case SIMON_SHOW: // горит только активный, остальные почти тёмные
    if (simon.active == playerId)
    {
      strip.fill(myColor);
      btn = 255;
    }
    else
      strip.fill(scaleColor(myColor, 6));
    break;

  case SIMON_INPUT:
    if ((int32_t)(pressFlashUntil - millis()) > 0) // только что нажал
    {
      strip.fill(myColor);
      btn = 255;
    }
    else if (!(simon.flags & SIMON_FLAG_HURRY) || (t / 125) % 2) // времени мало -> мигаем
    {
      uint8_t filled = (uint16_t)simon.progress * NUM_LEDS / 255;
      strip.fill(scaleColor(myColor, 8));
      for (uint8_t i = 0; i < filled; i++)
        strip.set(i, scaleColor(myColor, 90));
      btn = 80;
    }
    break;

  case SIMON_OK: // бегущий огонь своим цветом
  {
    strip.fill(scaleColor(myColor, 40));
    uint8_t pos = (t / 12) % NUM_LEDS;
    for (uint8_t i = 0; i < 6; i++)
      strip.set((pos + i) % NUM_LEDS, myColor);
    btn = 200;
    break;
  }

  case SIMON_ERROR: // виновник стробит красным, остальные тёмные
    if (simon.active == playerId && (t / 80) % 2 == 0)
    {
      strip.fill(mRed);
      btn = 255;
    }
    break;

  case SIMON_PAUSE: // ждём участника, который пропал со связи
    strip.fill(scaleColor(mOrange, 10 + (uint16_t)triangleWave(t, 1500) * 100 / 255));
    break;

  case SIMON_OVER: // счёт набирается по одному LED
    drawScoreRuler(simonRevealed(t, simon.score));
    btn = triangleWave(t, 2000) / 2;
    break;
  }
  return btn;
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
    if (t < WIN_ANIM_MS) // вспышки + белый бегущий огонь, кнопка стробит в такт вспышкам
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

  case STATE_SIMON:
    btn = renderSimon();
    break;
  }
  if ((int32_t)(brightnessPreviewUntil - millis()) > 0) // лента ровно горит: видно новую яркость
    strip.fill(myColor);
  strip.show();
  setButtonLed(btn);
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
  randomSeed(analogRead(PIN_NOISE) ^ micros());

  pinMode(PIN_BTN_LED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  buzzer.begin();

  // Нагрузка для IP5305 (выключена до первого импульса)
  pinMode(PIN_KEEPALIVE, OUTPUT);
  digitalWrite(PIN_KEEPALIVE, LOW);

  // ===== WS2812 =====
  strip.setBrightness(STRIP_BRIGHTNESS[0]);
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

  menuButton.sync();
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
  simonUpdateNote();
  handleKeepAlive(); // 5. Не даём IP5305 уснуть
}
