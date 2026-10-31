#include <Arduino.h>
#include <EEPROM.h>
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

// --- EEPROM ---
#define EE_MAGIC_ADDR 0
#define EE_BRIGHTNESS_ADDR 1
#define EE_CHECK_ADDR 2
#define EE_MAGIC 0xB8

// --- НАСТРОЙКИ ---
const uint16_t DECISION_WINDOW_MS = 30; // окно сравнения почти одновременных нажатий
const uint32_t RESET_CHECK_MS = 60000;  // дольше этого после сброса проверка "нажал до сброса" не нужна
const uint16_t LED_FRAME_MS = 20;
const uint16_t RESET_RELEASE_MS = 50;   // кнопка Сброс должна быть отпущена столько, чтобы сработать снова

RF24 radio(CE_PIN, CSN_PIN);
microLED<NUM_LEDS, PIN_LED_STRIP, MLED_NO_CLOCK, LED_WS2812, ORDER_GRB, CLI_AVER> strip;
MelodyPlayer buzzer(PIN_BUZZER);

AdminState gameState = {0, 0, 0};

// --- КНОПКА СБРОСА ---
volatile bool resetHit = false;
volatile uint32_t resetIsrUs = 0;
volatile bool resetArmed = true; // взводится только после стабильного отпускания кнопки
uint32_t resetDownMs = 0;        // когда кнопку последний раз видели нажатой

uint32_t resetUs = 0; // момент последнего сброса (micros)
uint32_t resetMs = 0; // он же в millis (для защиты от переполнения micros)
bool resetHoldPending = false; // кнопка Сброс ещё держится после срабатывания: ждём удержания

void isrReset()
{
    if (resetArmed)
    {
        resetArmed = false;
        resetIsrUs = micros();
        resetHit = true;
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

// --- ЯРКОСТЬ ЛЕНТ ИГРОКОВ ---
uint8_t stripBrightness = 0;

uint8_t loadStripBrightness()
{
    uint8_t level = EEPROM.read(EE_BRIGHTNESS_ADDR);
    if (EEPROM.read(EE_MAGIC_ADDR) == EE_MAGIC && EEPROM.read(EE_CHECK_ADDR) == (uint8_t)~level &&
        level < BRIGHTNESS_LEVELS)
        return level;
    return 0;
}

// Следующий уровень: запоминаем и сообщаем сигналами (3 = 100%, 2 = 70%, 1 = 50%)
void cycleStripBrightness()
{
    stripBrightness = (stripBrightness + 1) % BRIGHTNESS_LEVELS;
    EEPROM.update(EE_BRIGHTNESS_ADDR, stripBrightness);
    EEPROM.update(EE_CHECK_ADDR, (uint8_t)~stripBrightness);
    EEPROM.update(EE_MAGIC_ADDR, EE_MAGIC);
    buzzer.start(SOUND_LEVEL, 5 - 2 * stripBrightness);
    Serial.print("Strip brightness level ");
    Serial.println(stripBrightness);
}

// Перезаряжаем ACK payload актуальным статусом (без устаревших копий в FIFO)
void loadAckPayload()
{
    gameState.brightness = stripBrightness;
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
    // Дребезг при отпускании не должен давать второй сброс: он стёр бы нажатие игрока
    if (digitalRead(PIN_BUTTON) == LOW)
        resetDownMs = millis();
    else if (!resetArmed && millis() - resetDownMs > RESET_RELEASE_MS)
    {
        resetArmed = true;
        resetHoldPending = false;
    }

    // Удержание Сброса -> следующий уровень яркости лент
    if (resetHoldPending && digitalRead(PIN_BUTTON) == LOW && millis() - resetMs >= HOLD_MS)
    {
        resetHoldPending = false;
        cycleStripBrightness();
        loadAckPayload();
    }

    if (!resetHit)
        return;

    noInterrupts();
    resetUs = resetIsrUs;
    resetHit = false;
    interrupts();
    resetMs = millis();
    resetHoldPending = true;

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

// --- САЙМОН ---
// Включается удержанием кнопки при запуске. Админ ведёт игру, игроки только отображают SimonState.
const uint16_t SIMON_BOOT_HOLD_MS = 3000; // столько держать кнопку при включении
const uint8_t SIMON_MAX_SEQ = 64;
const uint8_t SIMON_LIVES = 3;
const uint8_t SIMON_REPLAYS = 3;          // повторов показа на игру
const uint16_t SIMON_LOST_MS = 1500;      // участник молчит дольше -> пауза
const uint16_t SIMON_BACK_MS = 500;       // все ответили за это время -> продолжаем
const uint16_t SIMON_READY_STEP_MS = 700;
const uint16_t SIMON_INTRO_MS = 1000;
const uint16_t SIMON_SHOW_LEAD_MS = 400;  // пауза перед первым шагом показа
const uint16_t SIMON_OK_MS = 600;
const uint16_t SIMON_ERROR_MS = 1500;
const uint16_t SIMON_HURRY_MS = 1000;     // меньше этого на нажатие -> ленты мигают
const uint32_t SIMON_DUP_PRESS_US = 80000; // ближе по времени = повтор доставки того же нажатия

bool simonMode = false;
SimonState simon = {SIMON_LOBBY, 0, 0, 0, SIMON_LIVES, 0, 0, 0};
PolledButton simonButton(PIN_BUTTON);

uint32_t lastSeenMs[MAX_PLAYERS + 1];
uint8_t seenMask = 0;

uint8_t roster[MAX_PLAYERS]; // участники текущей игры
uint8_t rosterCount = 0;

uint8_t seq[SIMON_MAX_SEQ];
uint8_t seqLen = 0;
uint8_t level = 0;
uint8_t replaysLeft = 0;
bool cleanRound = true;

uint16_t onMs = 0;
uint16_t gapMs = 0;
uint16_t inputTimeoutMs = 0;

uint32_t phaseSince = 0;
uint32_t stepSince = 0;
uint8_t showIdx = 0;
bool showLit = false;
uint8_t readyLeft = 0;

uint8_t inputIdx = 0;
uint32_t inputStartUs = 0;

// Нажатия судим с задержкой, в порядке времени нажатия, а не прихода пакетов
struct SimonPress
{
    bool pending;
    bool seen;          // lastUs действителен
    uint32_t lastUs;    // время последнего нажатия
    uint32_t arrivedMs; // когда пришёл пакет
};
SimonPress presses[MAX_PLAYERS + 1];

uint8_t revealTicks = 0; // сколько очков уже оттикал подсчёт

// Перезаряжаем ACK payload актуальным состоянием игры
void loadSimonPayload()
{
    simon.brightness = stripBrightness;
    radio.flush_tx();
    radio.writeAckPayload(0, &simon, sizeof(simon));
}

void simonSetPhase(SimonPhase phase, uint8_t active = 0)
{
    simon.phase = phase;
    simon.active = active;
    simon.stepId++;
    phaseSince = millis();
    stepSince = phaseSince;
    loadSimonPayload();
}

bool simonOnline(uint8_t id, uint16_t withinMs)
{
    return (seenMask & (1 << id)) && millis() - lastSeenMs[id] < withinMs;
}

uint8_t simonRandomStep()
{
    uint8_t pick;
    do
    {
        pick = roster[random(rosterCount)];
    } while (rosterCount > 1 && seqLen >= 2 && seq[seqLen - 1] == pick && seq[seqLen - 2] == pick);
    return pick;
}

void simonNextRound()
{
    level++;
    uint8_t grow = (level >= 6 && random(100) < 25) ? 2 : 1;
    while (grow-- && seqLen < SIMON_MAX_SEQ)
    {
        uint8_t step = simonRandomStep();
        seq[seqLen++] = step;
    }

    int16_t on = max(220, 600 - 25 * (int16_t)level);
    on = (int32_t)on * random(85, 116) / 100;
    simon.flags = 0;
    if (level >= 5 && random(100) < 20)
    {
        simon.flags |= SIMON_FLAG_BURST;
        on = max(180, on * 7 / 10);
    }
    if (level >= 9 && random(100) < 15)
        simon.flags |= SIMON_FLAG_SILENT;

    onMs = on;
    gapMs = max(100, on / 3);
    inputTimeoutMs = max(1500, 3500 - 100 * (int16_t)level);
    cleanRound = true;

    Serial.print("Round ");
    Serial.print(level);
    Serial.print(" len ");
    Serial.print(seqLen);
    Serial.print(" on ");
    Serial.print(onMs);
    Serial.print(" flags ");
    Serial.println(simon.flags);
}

void simonStartIntro()
{
    if (simon.flags & (SIMON_FLAG_BURST | SIMON_FLAG_SILENT))
        buzzer.start(MELODY(SIMON_TWIST));
    simonSetPhase(SIMON_INTRO);
}

void simonStartShow()
{
    showIdx = 0;
    showLit = false;
    simon.flags &= ~SIMON_FLAG_HURRY;
    simonSetPhase(SIMON_SHOW);
}

void simonStartInput()
{
    inputIdx = 0;
    inputStartUs = micros();
    simon.progress = 0;
    for (uint8_t i = 1; i <= MAX_PLAYERS; i++)
    {
        presses[i].pending = false;
        presses[i].seen = false;
    }
    simonSetPhase(SIMON_INPUT);
}

void simonGameOver(bool win)
{
    simon.flags = win ? SIMON_FLAG_WIN : 0;
    revealTicks = 0;
    if (win)
        buzzer.start(MELODY(WIN_FANFARE));
    else
        buzzer.start(MELODY(LOSE_MELODY));
    simonSetPhase(SIMON_OVER);
    Serial.print("GAME OVER, score ");
    Serial.println(simon.score);
}

void simonStartGame()
{
    randomSeed(micros()); // момент клика случаен: разные игры после каждого включения
    rosterCount = 0;
    for (uint8_t id = 1; id <= MAX_PLAYERS; id++)
        if (simonOnline(id, SIMON_LOST_MS))
            roster[rosterCount++] = id;

    if (rosterCount == 0)
    {
        buzzer.start(MELODY(SIMON_DENY));
        return;
    }

    seqLen = 0;
    level = 0;
    replaysLeft = SIMON_REPLAYS;
    simon.score = 0;
    simon.lives = SIMON_LIVES;
    simon.flags = 0;
    readyLeft = 3;
    simon.progress = readyLeft;
    buzzer.start(MELODY(SIMON_BEEP));
    simonSetPhase(SIMON_READY);

    Serial.print("Simon start, players: ");
    Serial.println(rosterCount);
}

void simonMistake(uint8_t culprit)
{
    simon.lives--;
    cleanRound = false;
    simon.flags &= ~SIMON_FLAG_HURRY;
    buzzer.start(MELODY(SIMON_DENY));
    simonSetPhase(SIMON_ERROR, culprit);

    Serial.print("Mistake: Player ");
    Serial.print(culprit);
    Serial.print(", lives ");
    Serial.println(simon.lives);
}

void simonRoundDone()
{
    simon.score++;
    if (cleanRound)
        simon.score++;
    if (simon.flags & (SIMON_FLAG_BURST | SIMON_FLAG_SILENT))
        simon.score++;
    buzzer.start(MELODY(SIMON_ROUND_OK));
    simonSetPhase(SIMON_OK);
}

void simonRegisterPress(uint8_t id, uint32_t pressUs)
{
    if (simon.phase != SIMON_INPUT || (int32_t)(pressUs - inputStartUs) < 0)
        return;

    SimonPress &p = presses[id];
    if (p.seen)
    {
        int32_t diff = (int32_t)(pressUs - p.lastUs);
        if (diff < 0)
            diff = -diff;
        if ((uint32_t)diff < SIMON_DUP_PRESS_US)
            return; // ACK потерялся, игрок доставил то же нажатие ещё раз
    }
    p.seen = true;
    p.lastUs = pressUs;
    p.pending = true;
    p.arrivedMs = millis();
}

void simonHandleRadio()
{
    while (radio.available())
    {
        uint32_t nowUs = micros();
        PlayerPacket pkt;
        radio.read(&pkt, sizeof(pkt));

        if (pkt.id >= 1 && pkt.id <= MAX_PLAYERS)
        {
            lastSeenMs[pkt.id] = millis();
            seenMask |= 1 << pkt.id;
            if (pkt.type == PKT_PRESS)
                simonRegisterPress(pkt.id, nowUs - pkt.pressAgeUs);
        }

        // ОБЯЗАТЕЛЬНО: перезаряжаем трубу текущим статусом для следующего запроса
        loadSimonPayload();
    }
}

bool simonInRoster(uint8_t id)
{
    for (uint8_t i = 0; i < rosterCount; i++)
        if (roster[i] == id)
            return true;
    return false;
}

// Судим самое раннее нажатие, когда окно на его опоздавших соседей закрылось.
// true = нажатие ещё ждёт своего окна (таймаут в этот момент не считаем)
bool simonJudgePress()
{
    uint8_t first = 0;
    for (uint8_t id = 1; id <= MAX_PLAYERS; id++)
        if (presses[id].pending && (first == 0 || (int32_t)(presses[id].lastUs - presses[first].lastUs) < 0))
            first = id;

    if (first == 0)
        return false;
    if (millis() - presses[first].arrivedMs < DECISION_WINDOW_MS)
        return true;

    presses[first].pending = false;
    if (!simonInRoster(first)) // подключился уже после старта игры
        return false;

    if (first != seq[inputIdx])
    {
        simonMistake(first);
        return false;
    }

    inputIdx++;
    stepSince = millis();
    simon.progress = (uint16_t)inputIdx * 255 / seqLen;
    simon.flags &= ~SIMON_FLAG_HURRY;
    if (inputIdx >= seqLen)
        simonRoundDone();
    else
        loadSimonPayload();
    return false;
}

void simonUpdateInput()
{
    if (simonJudgePress() || simon.phase != SIMON_INPUT)
        return;

    uint32_t elapsed = millis() - stepSince;
    if (elapsed >= inputTimeoutMs)
    {
        simonMistake(seq[inputIdx]);
        return;
    }
    bool hurry = inputTimeoutMs - elapsed < SIMON_HURRY_MS;
    if (hurry != (bool)(simon.flags & SIMON_FLAG_HURRY))
    {
        simon.flags ^= SIMON_FLAG_HURRY;
        loadSimonPayload();
    }
}

void simonUpdateShow()
{
    uint32_t elapsed = millis() - stepSince;
    if (showLit)
    {
        if (elapsed < onMs)
            return;
        showLit = false;
        showIdx++;
        simon.active = 0;
    }
    else
    {
        if (elapsed < (showIdx == 0 ? SIMON_SHOW_LEAD_MS : gapMs))
            return;
        if (showIdx >= seqLen)
        {
            simonStartInput();
            return;
        }
        showLit = true;
        simon.active = seq[showIdx];
    }
    simon.stepId++;
    stepSince = millis();
    loadSimonPayload();
}

void simonUpdatePhase()
{
    uint32_t t = millis() - phaseSince;

    switch (simon.phase)
    {
    case SIMON_LOBBY:
        break;

    case SIMON_READY:
        if (t < SIMON_READY_STEP_MS)
            break;
        if (--readyLeft > 0)
        {
            simon.progress = readyLeft;
            buzzer.start(MELODY(SIMON_BEEP));
            simonSetPhase(SIMON_READY);
        }
        else
        {
            simonNextRound();
            simonStartIntro();
        }
        break;

    case SIMON_INTRO:
        if (t >= SIMON_INTRO_MS)
            simonStartShow();
        break;

    case SIMON_SHOW:
        simonUpdateShow();
        break;

    case SIMON_INPUT:
        simonUpdateInput();
        break;

    case SIMON_OK:
        if (t < SIMON_OK_MS)
            break;
        if (seqLen >= SIMON_MAX_SEQ)
            simonGameOver(true);
        else
        {
            simonNextRound();
            simonStartIntro();
        }
        break;

    case SIMON_ERROR:
        if (t < SIMON_ERROR_MS)
            break;
        if (simon.lives == 0)
            simonGameOver(false);
        else
            simonStartIntro(); // та же последовательность заново
        break;

    case SIMON_PAUSE:
    {
        bool allBack = true;
        for (uint8_t i = 0; i < rosterCount; i++)
            if (!simonOnline(roster[i], SIMON_BACK_MS))
                allBack = false;
        if (allBack)
            simonStartIntro();
        break;
    }

    case SIMON_OVER:
        if (simonRevealed(t, simon.score) > revealTicks)
        {
            revealTicks++;
            buzzer.start(MELODY(SOUND_CLICK));
            if (revealTicks == simon.score)
                buzzer.start(MELODY(SOUND_CONFIRM));
        }
        break;
    }
}

// Участник пропал: жизнь не снимаем, ждём его и показываем заново
void simonCheckLink()
{
    if (simon.phase != SIMON_INTRO && simon.phase != SIMON_SHOW && simon.phase != SIMON_INPUT)
        return;
    for (uint8_t i = 0; i < rosterCount; i++)
    {
        if (!simonOnline(roster[i], SIMON_LOST_MS))
        {
            Serial.print("Lost link: Player ");
            Serial.println(roster[i]);
            simon.flags &= ~SIMON_FLAG_HURRY;
            simonSetPhase(SIMON_PAUSE, roster[i]);
            return;
        }
    }
}

void simonHandleButton()
{
    ButtonEvent ev = simonButton.poll();
    if (ev == BTN_NONE)
        return;

    if (ev == BTN_LONG)
    {
        if (simon.phase == SIMON_LOBBY)
        {
            cycleStripBrightness();
            loadSimonPayload();
        }
        else if (simon.phase != SIMON_OVER)
            simonGameOver(false);
        return;
    }

    switch (simon.phase)
    {
    case SIMON_LOBBY:
        simonStartGame();
        break;

    case SIMON_INPUT: // показать последовательность ещё раз
        if (replaysLeft == 0)
        {
            buzzer.start(MELODY(SIMON_DENY));
            break;
        }
        replaysLeft--;
        cleanRound = false;
        buzzer.start(MELODY(SIMON_REPLAY));
        simonStartShow();
        Serial.print("Replay, left ");
        Serial.println(replaysLeft);
        break;

    case SIMON_OVER:
        simon.score = 0;
        simon.lives = SIMON_LIVES;
        simon.flags = 0;
        buzzer.start(MELODY(ADMIN_RESET_SOUND));
        simonSetPhase(SIMON_LOBBY);
        break;

    default:
        break;
    }
}

mData simonLivesColor()
{
    if (simon.lives >= 3)
        return mGreen;
    return simon.lives == 2 ? mYellow : mRed;
}

void simonUpdateLed()
{
    if (millis() - lastLedFrame < LED_FRAME_MS)
        return;
    lastLedFrame = millis();
    uint32_t t = millis() - phaseSince;

    switch (simon.phase)
    {
    case SIMON_LOBBY: // N вспышек = число игроков на связи
    {
        uint8_t online = 0;
        for (uint8_t id = 1; id <= MAX_PLAYERS; id++)
            if (simonOnline(id, SIMON_LOST_MS))
                online++;
        uint16_t phase = t % (online * 300 + 1000);
        bool flash = phase < online * 300 && (phase % 300) < 150;
        strip.fill(scaleColor(mPurple, flash ? 255 : 25));
        break;
    }

    case SIMON_READY:
        strip.fill(t < 200 ? scaleColor(mWhite, 255) : scaleColor(mWhite, 0));
        break;

    case SIMON_SHOW:
        strip.fill(simon.active ? playerColor(simon.active) : scaleColor(mWhite, 0));
        break;

    case SIMON_OK:
        strip.fill(mGreen);
        break;

    case SIMON_ERROR:
        strip.fill(mRed);
        break;

    case SIMON_PAUSE:
        strip.fill(scaleColor(mOrange, 30 + triangleWave(t, 1500) * 225 / 255));
        break;

    case SIMON_OVER:
        strip.fill((simon.flags & SIMON_FLAG_WIN) ? mWheel(t % 1530) : mData(mRed));
        break;

    default: // INTRO, INPUT: цвет показывает оставшиеся жизни
        strip.fill(scaleColor(simonLivesColor(), 100));
        break;
    }
    strip.show();
}

void simonLoop()
{
    simonHandleButton();
    simonHandleRadio();
    simonCheckLink();
    simonUpdatePhase();
    simonUpdateLed();
}

// Кнопка зажата при включении и держится 3 с? Отпустили раньше -> обычный брейн-ринг
bool simonBootHold()
{
    uint32_t start = millis();
    uint32_t lastDown = start;
    uint32_t lastFrameMs = 0;
    uint8_t ticks = 0;

    while (millis() - lastDown < RESET_RELEASE_MS)
    {
        uint32_t t = millis() - start;
        if (digitalRead(PIN_BUTTON) == LOW)
            lastDown = millis();
        if (t >= SIMON_BOOT_HOLD_MS)
            return true;

        if (t / 500 > ticks)
        {
            ticks = t / 500;
            buzzer.start(MELODY(SOUND_CLICK));
        }
        if (millis() - lastFrameMs >= LED_FRAME_MS)
        {
            lastFrameMs = millis();
            strip.fill(scaleColor(mPurple, t * 255 / SIMON_BOOT_HOLD_MS));
            strip.show();
        }
        buzzer.update();
    }
    strip.clear();
    strip.show();
    return false;
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

    // Нагрузка для IP5305 (выключена до первого импульса)
    pinMode(PIN_KEEPALIVE, OUTPUT);
    digitalWrite(PIN_KEEPALIVE, LOW);

    // Светодиод
    strip.setBrightness(100);
    strip.clear();
    strip.show();

    stripBrightness = loadStripBrightness();

    // Режим: кнопка зажата при включении и держится 3 с -> Саймон, иначе брейн-ринг
    if (digitalRead(PIN_BUTTON) == LOW)
    {
        simonMode = simonBootHold();
        if (!simonMode)
        {
            resetArmed = false; // дребезг отпускания не должен дать ложный сброс
            resetDownMs = millis();
        }
    }

    if (simonMode)
    {
        randomSeed(analogRead(A1) ^ micros()); // A1 на плате Admin свободен
        simonButton.sync();
        buzzer.start(MELODY(SOUND_CONFIRM));
    }
    else
        attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), isrReset, FALLING);

    // Радио
    radioSetupCommon(radio);
    radio.openReadingPipe(0, RADIO_ADDRESS);
    radio.powerUp();
    radio.startListening();

    // ЗАРАНЕЕ заряжаем трубу нулевым статусом
    if (simonMode)
        loadSimonPayload();
    else
        loadAckPayload();
    Serial.println(simonMode ? "Admin ready (Simon)" : "Admin ready");
}

void loop()
{
    if (simonMode)
        simonLoop();
    else
    {
        handleReset();   // 1. ПРИОРИТЕТ: кнопка Сброс
        handleRadio();   // 2. Слушаем эфир, собираем нажатия
        resolveWinner(); // 3. Окно закрылось -> объявляем победителя
        updateLed();     // 4. Световая индикация
    }
    buzzer.update(); // 5. Звук
    handleKeepAlive(); // 6. Не даём IP5305 уснуть
}
