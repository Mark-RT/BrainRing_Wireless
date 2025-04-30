// Если используется китайский клон LGT8F328P, то выбрать такие её настройки
// для прошивки:
// Плата: "LGT8F328 + 16 MHz crystal"
// Clock: "External 16 MHz"
// Variant: "328P-LQFP32"
// Upload speed: "57600"

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
RF24 radio(9, 10);

#define numPlayer 2

#define mosfet 4  //мосфет
#define button 5  //кнопка
#define green_led 6  //красный светодиод
#define red_led 7  //зеленый светодиод

byte butt;
bool flag = 0;
bool flag2 = 0;

int myData[1];
int ackData[1];
int rxData[1];

void setup() {
  Serial.begin(9600);
  radio.begin           ();
  radio.setChannel      (0x4c);
  radio.setDataRate     (RF24_2MBPS);
  radio.setPALevel      (RF24_PA_MAX);
  radio.setRetries(5, 10);
  radio.enableAckPayload();
  //radio.setPayloadSize(2);
  radio.enableDynamicPayloads();

  radio.openWritingPipe (0xAABBCCDD22LL);

  radio.openReadingPipe (1, 0xAABBCCDD77LL);
  radio.startListening();
  pinMode(button, INPUT_PULLUP);
  pinMode(mosfet, OUTPUT);
  pinMode(red_led, OUTPUT);
  pinMode(green_led, OUTPUT);
  digitalWrite(green_led, 1);
  myData[0] = numPlayer;
  if (radio.isPVariant()) Serial.println("OK");
  radio.setAutoAck( 0xAABBCCDD77LL , false );
}

void win() {
  digitalWrite(mosfet, 1);
  digitalWrite(red_led, 1);
  digitalWrite(green_led, 0);
  flag2 = 1;
  radio.flush_tx();
}

void reseting() {
  digitalWrite(mosfet, 0);
  digitalWrite(red_led, 0);
  digitalWrite(green_led, 1);
  flag = 0; flag2 = 0;
  radio.flush_tx();
}

void loose() {
  digitalWrite(mosfet, 0);
  digitalWrite(red_led, 1);
  digitalWrite(green_led, 0);
  flag = 1;
  radio.flush_tx();
}

void loop() {
  butt = digitalRead(button);
  if (!butt && !flag) {
    Serial.print("НАЖАЛ ");
    //radio.flush_tx();
    //myData[0] = numPlayer;
    radio.stopListening();
    flag = 1;
    if (radio.write(&myData, sizeof(myData))) {
      Serial.print("Отправка "); Serial.println(myData[0]);
    } else Serial.println("Феил отправки");
    radio.startListening();
    if (radio.isAckPayloadAvailable()) {
      radio.read(&ackData, sizeof(ackData));
      if (ackData[0] == numPlayer * 11) win();
      Serial.print("Обратная связь "); Serial.println(ackData[0]);
    } else Serial.println("Феил обратной связи");
  }

  if (radio.available(0xAABBCCDD77LL)) {
    radio.read(&rxData, sizeof(rxData));
    Serial.print("Обратное получение "); Serial.println(rxData[0]);
    if (rxData[0] == 10) reseting();
    else {
      if (!flag2) loose();
    }
  }
  if (radio.rxFifoFull()) {
    Serial.println("Буфер полный!");
    radio.flush_tx();
  }
}
