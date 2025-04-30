#include <iarduino_RF433_Transmitter.h>                   // Подключаем библиотеку для работы с передатчиком FS1000A
iarduino_RF433_Transmitter TX(2);                     // номер вывода к которому подключён передатчик
#include <iarduino_RF433_Receiver.h>                      // Подключаем библиотеку для работы с приёмником MX-RM-5V
iarduino_RF433_Receiver RX(3);                         // Создаём объект radio для работы с библиотекой iarduino_RF433, указывая номер вывода к которому подключён приёмник (можно подключать только к выводам использующим внешние прерывания)
int RXdata[1];
int TXdata[1];

#define button 4  //главная кнопка
#define red_led 5
#define green_led 6

byte butt;
byte trans = 0;

bool flag = 0; //разрешает/запрещает свет победителю

void setup() {
  Serial.begin(9600);
  RX.begin();                                        // Инициируем работу передатчика FS1000A (в качестве параметра можно указать скорость ЧИСЛО бит/сек, тогда можно не вызывать функцию setDataRate)
  RX.setDataRate     (i433_3KBPS);                   // Указываем скорость передачи данных (i433_5KBPS, i433_4KBPS, i433_3KBPS, i433_2KBPS, i433_1KBPS, i433_500BPS, i433_100BPS), i433_1KBPS - 1кбит/сек
  RX.openReadingPipe(1);
  RX.openReadingPipe(2);
  RX.openReadingPipe(3);
  RX.openReadingPipe(4);
  RX.startListening();

  TX.begin();                                        // Инициируем работу передатчика FS1000A (в качестве параметра можно указать скорость ЧИСЛО бит/сек, тогда можно не вызывать функцию setDataRate)
  TX.setDataRate     (i433_3KBPS);                   // Указываем скорость передачи данных (i433_5KBPS, i433_4KBPS, i433_3KBPS, i433_2KBPS, i433_1KBPS, i433_500BPS, i433_100BPS), i433_1KBPS - 1кбит/сек
  TX.openWritingPipe(5);

  pinMode(button, INPUT_PULLUP);
  pinMode(red_led, OUTPUT);      //подключение красного светодиода
  pinMode(green_led, OUTPUT);      //подключение зеленого светодиода
  digitalWrite(green_led, 1);
}

void loop() {
  if (RX.available() && !flag) {
    RX.read(&RXdata, sizeof(RXdata));
    digitalWrite(green_led, 0);
    digitalWrite(red_led, 1);
    switch (RXdata[0]) {
      case 1: // выиграл 1
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
  
  if (flag) {
    for (int i=0; i<30; i++){
    TXdata[0] = trans; // выиграл
    TX.write(&TXdata, sizeof(TXdata));
    delay(5);
    }
  }
  
  butt = digitalRead(button);
  if (butt == 0) {
    TXdata[0] = 10;
    TX.write(&TXdata, sizeof(TXdata));
    flag = 0;
    trans = 0;
    digitalWrite(green_led, 1);
    digitalWrite(red_led, 0);
  }
}
