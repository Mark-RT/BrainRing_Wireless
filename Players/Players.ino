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

#define numPlayer 1

#define mosfet 4  //мосфет
#define button 5  //кнопка
#define green_led 6  //красный светодиод
#define red_led 7  //зеленый светодиод

byte butt;
bool flag = 0;
bool flag2 = 0;
unsigned long timing;

int myData[1];
int ackData[1];
int txData[1];
int rxData[1];
byte address[][6] = {"1Node", "2Node", "3Node", "4Node", "5Node", "6Node"}; //возможные номера труб

void setup() {
  Serial.begin(9600);
  radio.begin();
  radio.setChannel(0x60);
  radio.setDataRate(RF24_2MBPS);
  radio.setPALevel(RF24_PA_MAX);
  radio.setRetries(4, 9);
  radio.enableAckPayload();
  radio.setPayloadSize(2);
  
  radio.openWritingPipe(address[0]);
  radio.powerUp();
  radio.stopListening();

  pinMode(button, INPUT_PULLUP);
  pinMode(mosfet, OUTPUT);
  pinMode(red_led, OUTPUT);
  pinMode(green_led, OUTPUT);
  digitalWrite(green_led, 1);
  myData[0] = numPlayer;
  txData[0] = 5;
}

void win() {
  digitalWrite(mosfet, 1);
  digitalWrite(red_led, 1);
  digitalWrite(green_led, 0);
  flag2 = 1;
}

void reseting() {
  digitalWrite(mosfet, 0);
  digitalWrite(red_led, 0);
  digitalWrite(green_led, 1);
  flag = 0; flag2 = 0;
}

void loose() {
  digitalWrite(mosfet, 0);
  digitalWrite(red_led, 1);
  digitalWrite(green_led, 0);
  flag = 1;
}

void loop() {
  butt = digitalRead(button);
  if (!butt && !flag) {
    flag = 1;
    if (radio.write(&myData, sizeof(myData))) {
      Serial.println("Кноп отправка ");
    } else Serial.println("Кноп феил отправки");

    if (radio.isAckPayloadAvailable()) {
      radio.read(&ackData, sizeof(ackData));
      if (ackData[0] == 11) win();
      Serial.println("Кноп ОС");
    } else Serial.println("Кноп феил ОС");
  }

  if (millis() - timing > 140) {
    if (radio.write(&txData, sizeof(txData))) {
      Serial.println("Отпр");
    } else Serial.println("Феил отправки");

    if (radio.isAckPayloadAvailable()) {
      radio.read(&rxData, sizeof(rxData));
      if (rxData[0] == 15) {
        if (!flag2) loose();
      } else reseting();
    } else Serial.println("Феил");
    timing = millis();
  }
}
