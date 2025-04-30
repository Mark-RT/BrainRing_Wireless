#include <iarduino_RF433_Transmitter.h>                   // Подключаем библиотеку для работы с передатчиком FS1000A
iarduino_RF433_Transmitter TX(2);                     // номер вывода к которому подключён передатчик
#include <iarduino_RF433_Receiver.h>                      // Подключаем библиотеку для работы с приёмником MX-RM-5V
iarduino_RF433_Receiver RX(3);                         // Создаём объект radio для работы с библиотекой iarduino_RF433, указывая номер вывода к которому подключён приёмник (можно подключать только к выводам использующим внешние прерывания)
int RXdata[1];
int TXdata[1];

#define num_player 3

#define button 5  //кнопка
#define mosfet 4  //мосфет
#define green_led 6  //красный светодиод
#define red_led 7  //зеленый светодиод

byte butt;
bool flag = 0;
bool flag2 = 0;
void setup() {
  Serial.begin(9600);
  RX.begin();                                        // Инициируем работу передатчика FS1000A (в качестве параметра можно указать скорость ЧИСЛО бит/сек, тогда можно не вызывать функцию setDataRate)
  RX.setDataRate     (i433_3KBPS);                   // Указываем скорость передачи данных (i433_5KBPS, i433_4KBPS, i433_3KBPS, i433_2KBPS, i433_1KBPS, i433_500BPS, i433_100BPS), i433_1KBPS - 1кбит/сек
  RX.openReadingPipe (5);
  RX.startListening();

  TX.begin();                                        // Инициируем работу передатчика FS1000A (в качестве параметра можно указать скорость ЧИСЛО бит/сек, тогда можно не вызывать функцию setDataRate)
  TX.setDataRate     (i433_3KBPS);                   // Указываем скорость передачи данных (i433_5KBPS, i433_4KBPS, i433_3KBPS, i433_2KBPS, i433_1KBPS, i433_500BPS, i433_100BPS), i433_1KBPS - 1кбит/сек
  TX.openWritingPipe (num_player);

  pinMode(button, INPUT_PULLUP);  //подключение кнопки
  pinMode(mosfet, OUTPUT);      //подключение мосфета
  pinMode(red_led, OUTPUT);      //подключение красного светодиода
  pinMode(green_led, OUTPUT);      //подключение зеленого светодиода
  digitalWrite(green_led, 1);
}

void loop() {
  butt = digitalRead(button);
  if (butt == 0 && flag == 0) {
    TXdata[0] = num_player;
    TX.write(&TXdata, sizeof(TXdata));
    flag = 1;
    Serial.println("НАЖАТА кнопка");
  }

  if (RX.available()) {                              // Если в буфере имеются принятые данные
    RX.read(&RXdata, sizeof(RXdata));                  // Читаем данные в массив data и указываем сколько байт читать
    Serial.println("Чтото получил");
    switch (RXdata[0]) {
      case num_player*11: // выиграл
        digitalWrite(mosfet, 1);
        digitalWrite(red_led, 1);
        digitalWrite(green_led, 0);
        break;

      case 10: // сброс
        digitalWrite(mosfet, 0);
        digitalWrite(red_led, 0);
        digitalWrite(green_led, 1);
        flag = 0; flag2 = 0;
        break;

      default:
        digitalWrite(mosfet, 0);
        digitalWrite(red_led, 1);
        digitalWrite(green_led, 0);
        break;
    }
  }
}
