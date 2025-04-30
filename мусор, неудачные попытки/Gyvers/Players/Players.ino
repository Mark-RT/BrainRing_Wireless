#define G433_FAST
#include <Gyver433.h>
Gyver433_TX<2> tx;
Gyver433_RX<3, 2> rx;

#define num_player 4
#define button 5  //кнопка
#define mosfet 4  //мосфет
#define green_led 6  //красный светодиод
#define red_led 7  //зеленый светодиод

byte butt;
bool flag = 0;
bool flag2 = 0;

void setup() {
  Serial.begin(9600);
  attachInterrupt(1, isr, CHANGE);
  pinMode(button, INPUT_PULLUP);  //подключение кнопки
  pinMode(mosfet, OUTPUT);      //подключение мосфета
  pinMode(red_led, OUTPUT);      //подключение красного светодиода
  pinMode(green_led, OUTPUT);      //подключение зеленого светодиода
  digitalWrite(green_led, 1);
}

void isr() {
  rx.tickISR();
}
int dataTX[2];
void loop() {
  again:
  butt = digitalRead(button);
  if (butt == 0 && flag == 0) {
    dataTX[0] = num_player;
    dataTX[1] = 72;
    tx.sendData(dataTX);
    flag = 1;
  }
  
  digitalWrite(13, 0);
  if (rx.gotData()) {
    int dataRX[1];
    if (rx.readData(dataRX)) Serial.println(dataRX[0]);
    else {
      Serial.println("Data error");
      goto again;
    }
    switch (dataRX[0]) {
      case num_player*11:
        digitalWrite(mosfet, 1);
        digitalWrite(red_led, 1);
        digitalWrite(green_led, 0);
        flag2 = 1;
        break;

      case 10: // выиграл 2
        digitalWrite(mosfet, 0);
        digitalWrite(red_led, 0);
        digitalWrite(green_led, 1);
        digitalWrite(13, 0);
        flag = 0; flag2 == 0;
        break;

      default:
        if (flag2 == 0) {
          digitalWrite(13, 1);
          digitalWrite(mosfet, 0);
          digitalWrite(red_led, 1);
          digitalWrite(green_led, 0);
        }
        break;
    }
  }
}
