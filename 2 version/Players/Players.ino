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

#define numPlayer 4

#define mosfet 4  //мосфет
#define button 5
#define green_led 6  //красный светодиод
#define red_led 7  //зеленый светодиод

byte butt;
bool flag = 0;
unsigned long timing;

int txData[1];
int rxData[1];
byte address[][6] = {"1Node", "2Node", "3Node", "4Node", "5Node", "6Node"}; //возможные номера труб

void setup() {
  Serial.begin(57600);
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
  txData[0] = 10 + numPlayer;
}

void win() {
  //Serial.print("win");
  digitalWrite(mosfet, 1);
  digitalWrite(red_led, 1);
  digitalWrite(green_led, 0);
  flag = 1;
}

void reseting() {
  //Serial.print("reseting");
  txData[0] = 10 + numPlayer;
  digitalWrite(mosfet, 0);
  digitalWrite(red_led, 0);
  digitalWrite(green_led, 1);
  flag = 0;
}

void loose() {
  //Serial.print("loose");
  txData[0] = 10 + numPlayer;
  digitalWrite(mosfet, 0);
  digitalWrite(red_led, 1);
  digitalWrite(green_led, 0);
  flag = 1;
}

void loop() {
  butt = digitalRead(button);
    if (!butt && !flag) {
      flag = 1;
      txData[0] = numPlayer;
    }

  if (millis() - timing > (110 + (numPlayer * 2))) {
    if (radio.write(&txData, sizeof(txData))) {
      Serial.println("Отправляю: ");
      //Serial.print(txData[0]);
    } else Serial.println("Феил отправки");

    if (radio.isAckPayloadAvailable()) {
      radio.read(&rxData, sizeof(rxData));
      if (rxData[0] == 10 && txData[0] == 10 + numPlayer) reseting(); // отпр 13
      else if (rxData[0] == 10 + numPlayer) win(); // отпр 3
      else loose();
    }
    timing = millis();
    /*Serial.print(",  Получаю:");
    Serial.println(rxData[0]);*/
  }
}
