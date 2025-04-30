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
bool flag = 0;

void setup() {
  Serial.begin(9600);
  radio.begin           ();
  radio.setChannel      (0x4c);
  radio.setDataRate     (RF24_2MBPS);
  radio.setPALevel      (RF24_PA_MAX);
  radio.enableAckPayload();
  radio.enableDynamicPayloads();
  
  radio.openWritingPipe (0xAABBCCDD77LL);

  radio.openReadingPipe (1, 0xAABBCCDD11LL);
  radio.openReadingPipe (2, 0xAABBCCDD22LL);
  radio.openReadingPipe (3, 0xAABBCCDD33LL);
  radio.openReadingPipe (4, 0xAABBCCDD44LL);
  radio.startListening  ();                                  // Включаем приемник, начинаем прослушивать открытые трубы.
  pinMode(button, INPUT_PULLUP);  //подключение кнопки
  pinMode(red_led, OUTPUT);      //подключение красного светодиода
  pinMode(green_led, OUTPUT);      //подключение зеленого светодиода
  digitalWrite(green_led, 1);
}

int transmit(int trans) {
  radio.flush_tx();
  txData[0] = trans;
  radio.stopListening();
  if (radio.write(&txData, sizeof(txData))) {
    Serial.print("Отправка "); Serial.println(txData[0]);
  } else Serial.println("Феил отправки");
  delay(200);
  if (radio.write(&txData, sizeof(txData))) {
    Serial.print("Отправка "); Serial.println(txData[0]);
  } else Serial.println("Феил отправки");
  delay(200);
  radio.startListening();
  radio.flush_tx();
  flag = 0;
}

int ledik(int flagg) {
  digitalWrite(red_led, flagg);
  digitalWrite(green_led, !flagg);
}

void loop() {
  if (radio.available() && !flag) {
    radio.read(&myData, sizeof(myData));
    ackData[0] = myData[0] * 11;
    radio.writeAckPayload (myData[0], &ackData, sizeof(ackData));
    Serial.print("Принял от ");
    Serial.println(myData[0]);
    flag = 1;
    ledik(flag);
  }
  if (flag) transmit(15);
  butt = digitalRead(button);
  if (!butt) {
    transmit(10);
    ledik(flag);
  }
  if (radio.rxFifoFull()) radio.flush_tx();
}
