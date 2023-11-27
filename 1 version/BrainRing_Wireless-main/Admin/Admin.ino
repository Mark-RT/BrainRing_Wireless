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
byte address[][6] = {"1Node", "2Node", "3Node", "4Node", "5Node", "6Node"}; //возможные номера труб
byte pipeNo;

void setup() {
  radio.begin();
  radio.setChannel(0x60);
  radio.setDataRate(RF24_2MBPS);
  radio.setPALevel(RF24_PA_MAX);
  radio.enableAckPayload();
  radio.setPayloadSize(2);
  radio.openReadingPipe(0, address[0]);
  radio.powerUp();
  radio.startListening();

  pinMode(button, INPUT_PULLUP);  //подключение кнопки
  pinMode(red_led, OUTPUT);      //подключение красного светодиода
  pinMode(green_led, OUTPUT);      //подключение зеленого светодиода
  digitalWrite(green_led, 1);
}

void loop() {
  if (radio.available(&pipeNo)) {
    radio.read(&myData, sizeof(myData));
    if (myData[0] == 1) {
      if (!check_flag) {
        check_flag = 1;
        ackData[0] = 11;
        radio.writeAckPayload (pipeNo, &ackData, sizeof(ackData));
        value = 15;
        digitalWrite(red_led, 1);
        digitalWrite(green_led, 0);
      }
    } else {
      ackData[0] = value;
      radio.writeAckPayload (pipeNo, &ackData, sizeof(ackData));
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
}
