#define G433_FAST
#include <Gyver433.h>
Gyver433_TX<2> tx;
Gyver433_RX<3, 4> rx;

#define button 4  //главная кнопка
#define red_led 5
#define green_led 6

byte butt;

byte flag = 0; //разрешает/запрещает свет победителю
byte trans;
int dataTX[1];
void setup() {
  Serial.begin(9600);
  attachInterrupt(1, isr, CHANGE);
  pinMode(button, INPUT_PULLUP);
  pinMode(red_led, OUTPUT);      //подключение красного светодиода
  pinMode(green_led, OUTPUT);      //подключение зеленого светодиода
  digitalWrite(green_led, 1);
}

void isr() {
  rx.tickISR();
}

int sendTX(int num) {
  digitalWrite(green_led, 0);
  digitalWrite(red_led, 1);
  dataTX[0] = num;
  tx.sendData(dataTX);
  for (int i = 0; i < 6; i++) {
       dataTX[0] = trans;
       tx.sendData(dataTX);
       Serial.print(trans);
       Serial.println("Отправляю!");
       delay(200);
     }
  flag = 1;
  trans = 0;
}

void loop() {
  if (rx.gotData() && flag == 0) {
    int dataRX[2];
    if (rx.readData(dataRX)) Serial.println(dataRX[0]);
    else {
      Serial.println("Data error");
      goto again;
    }
    // выведем также качество соединения
    Serial.print("RSSI: ");
    Serial.println(rx.getRSSI());
    if (dataRX[1] == 72) {
      switch (dataRX[0]) {
        case 1:
          trans = 11;
          flag = 1;
          break;

        case 2: // выиграл 2
          trans = 22;
          flag = 1;
          break;

        case 3: // выиграл 3
          trans = 33;
          flag = 1;
          break;

        case 4: // выиграл 4
          trans = 44;
          flag = 1;
          break;

        default:
          break;
      }
    }
    sendTX(trans);
    trans = 0;
  }
again:
  butt = digitalRead(button);
  if (!butt) {
    digitalWrite(green_led, 1);
    digitalWrite(red_led, 0);
     for (int i = 0; i < 4; i++) {
       dataTX[0] = 10;
       tx.sendData(dataTX);
       Serial.println("Сброс!");
       }
    flag = 0;
    dataTX[0] = 10;
    tx.sendData(dataTX);
    Serial.println("Сброс!");
    delay(50);
  }

  /* if (flag == 1) {
     digitalWrite(green_led, 0);
     digitalWrite(red_led, 1);
     delay(200);
     dataTX[0] = trans;
     tx.sendData(dataTX);
     Serial.print(trans);
     Serial.println("Отправляю!");

     for (int i = 0; i < 8; i++) {
       dataTX[0] = trans;
       tx.sendData(dataTX);
       Serial.print(trans);
       Serial.println("Отправляю!");
       delay(200);
     }
     flag = 2;
     trans = 0;
    }*/
}
