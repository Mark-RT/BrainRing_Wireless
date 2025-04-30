#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
RF24 radio(9, 10);

#include <EncButton.h>
Button btn(5);

#define green_led 6
#define red_led 7

int myData[1];
int ackData[1];
int txData[1];

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

  pinMode(red_led, OUTPUT);      //подключение красного светодиода
  pinMode(green_led, OUTPUT);      //подключение зеленого светодиода
  digitalWrite(green_led, 1);
}

void transmit(int num_player) {
  if (!check_flag) {
    check_flag = 1;
    ackData[0] = num_player * 11;
    radio.writeAckPayload (pipeNo, &ackData, sizeof(ackData));
    value = 15;
    digitalWrite(red_led, 1);
    digitalWrite(green_led, 0);
  }
}

void loop() {
  if (radio.available(&pipeNo)) {
    radio.read(&myData, sizeof(myData));
    switch (myData[0]) {
      case 1:
        transmit(myData[0]);
        break;

      case 2:
        transmit(myData[0]);
        break;

      case 3:
        transmit(myData[0]);
        break;

      case 4:
        transmit(myData[0]);
        break;

      default:
        ackData[0] = value;
        radio.writeAckPayload (pipeNo, &ackData, sizeof(ackData));
        break;
    }
  }

  if (btn.tick()) {
    if (btn.press()) {
      check_flag = 0;
      value = 10;
      digitalWrite(red_led, 0);
      digitalWrite(green_led, 1);
    }
  }
}
