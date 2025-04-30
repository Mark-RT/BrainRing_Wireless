#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
RF24 radio(9, 10);

#define button 5  //главная кнопка
#define green_led 6
#define red_led 7

int myData[1];
int ackData[1];
int txData[1];

byte butt;
bool butt_flag = 0;
byte value = 0;
bool check_flag = 0;
void setup() {
  Serial.begin(9600);
  radio.begin           ();
  radio.setChannel      (0x4c);
  radio.setDataRate     (RF24_2MBPS);
  radio.setPALevel      (RF24_PA_MAX);
  radio.enableAckPayload();
  radio.enableDynamicPayloads();

  radio.openReadingPipe (0, 0xAABBCCDD11LL);
  radio.openReadingPipe (1, 0xAABBCCDD22LL);
  radio.openReadingPipe (2, 0xAABBCCDD33LL);
  radio.openReadingPipe (3, 0xAABBCCDD44LL);
  radio.startListening  ();

  pinMode(button, INPUT_PULLUP);  //подключение кнопки
  pinMode(red_led, OUTPUT);      //подключение красного светодиода
  pinMode(green_led, OUTPUT);      //подключение зеленого светодиода
  digitalWrite(green_led, 1);
}

void loop() {
  if (radio.available()) {
    radio.read(&myData, sizeof(myData));
    Serial.println(myData[0]);
    switch (myData[0]) {
      case 1:
        if (!check_flag) {
          check_flag = 1;
          ackData[0] = myData[0] * 11;
          radio.writeAckPayload (myData[0] - 1, &ackData, sizeof(ackData));
          value = 15;
          digitalWrite(red_led, 1);
          digitalWrite(green_led, 0);
        }
        break;

      case 2:
        if (!check_flag) {
          check_flag = 1;
          ackData[0] = myData[0] * 11;
          radio.writeAckPayload (myData[0] - 1, &ackData, sizeof(ackData));
          value = 15;
          digitalWrite(red_led, 1);
          digitalWrite(green_led, 0);
        }
        break;

      case 3:
        if (!check_flag) {
          check_flag = 1;
          ackData[0] = myData[0] * 11;
          radio.writeAckPayload (myData[0] - 1, &ackData, sizeof(ackData));
          value = 15;
          digitalWrite(red_led, 1);
          digitalWrite(green_led, 0);
        }
        break;

      case 4:
        if (!check_flag) {
          check_flag = 1;
          ackData[0] = myData[0] * 11;
          radio.writeAckPayload (myData[0] - 1, &ackData, sizeof(ackData));
          value = 15;
          digitalWrite(red_led, 1);
          digitalWrite(green_led, 0);
        }
        break;

      case 5:
        ackData[0] = value;
        radio.writeAckPayload (myData[0] - 5, &ackData, sizeof(ackData));
        break;

      case 6:
        ackData[0] = value;
        radio.writeAckPayload (myData[0] - 5, &ackData, sizeof(ackData));
        break;

      case 7:
        ackData[0] = value;
        radio.writeAckPayload (myData[0] - 5, &ackData, sizeof(ackData));
        break;

      case 8:
        ackData[0] = value;
        radio.writeAckPayload (myData[0] - 5, &ackData, sizeof(ackData));
        break;

      default:
        break;
    }
  }
  butt = digitalRead(button);
  if (!butt && !butt_flag) {
    butt_flag = 1;
    check_flag = 0;
    value = 10;
    digitalWrite(red_led, 0);
    digitalWrite(green_led, 1);
  }
  if (butt && butt_flag) butt_flag = 0;
  if (radio.rxFifoFull()) radio.flush_tx();
}
