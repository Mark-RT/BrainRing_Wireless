#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
RF24 radio(9, 10);

#define button 5
#define green_led 6
#define red_led 7

int myData[1];
int ackData[1];

byte butt;
byte koef = 0;
byte winner = 0;
bool check_flag = 0;
bool butt_flag = 0;
bool last_flag = 0;
byte address[][6] = {"1Node", "2Node", "3Node", "4Node", "5Node", "6Node"}; //возможные номера труб
byte pipeNo;

void setup() {
  Serial.begin(9600);
  radio.begin();
  radio.setChannel(0x60);
  radio.setDataRate(RF24_2MBPS);
  radio.setPALevel(RF24_PA_MAX);
  radio.enableAckPayload();
  radio.setPayloadSize(2);
  radio.openReadingPipe(0, address[0]);
  radio.powerUp();
  radio.startListening();

  pinMode(button, INPUT_PULLUP);
  pinMode(red_led, OUTPUT);      //подключение красного светодиода
  pinMode(green_led, OUTPUT);      //подключение зеленого светодиода
  digitalWrite(green_led, 1);
}

void loop() {
  if (radio.available(&pipeNo)) {
    radio.read(&myData, sizeof(myData));
    if (myData[0] > 0 && myData[0] < 5 && !check_flag) {
      Serial.println("winner");
      winner = myData[0];
      koef = 1;
      check_flag = 1;
      butt_flag = 1;
      last_flag = 1;
      digitalWrite(red_led, 1);
      digitalWrite(green_led, 0);
    }
    else if (myData[0] > 10 && myData[0] < 15 && !butt_flag) {
      Serial.println("reseting");
      check_flag = 0;
      ackData[0] = 10;
      radio.writeAckPayload (pipeNo, &ackData, sizeof(ackData));
    }
    else if (myData[0] > 0 && myData[0] < 15 && check_flag && butt_flag && last_flag) {
      Serial.println("win / loose");
      ackData[0] = (winner * koef) + 10;
      radio.writeAckPayload (pipeNo, &ackData, sizeof(ackData));
    }
    else if (myData[0] > 0 && myData[0] < 5 && !butt_flag && !last_flag) {
      Serial.println("RESETTT");
      check_flag = 0;
      ackData[0] = 10;
      radio.writeAckPayload (pipeNo, &ackData, sizeof(ackData));
    }

    /*Serial.print(myData[0]);
      Serial.print("\t");
      Serial.println(ackData[0]);*/
  }

  butt = digitalRead(button);
  if (!butt) {
    koef = 0;
    butt_flag = 0;
    last_flag = 0;
    digitalWrite(red_led, 0);
    digitalWrite(green_led, 1);
  }
}
