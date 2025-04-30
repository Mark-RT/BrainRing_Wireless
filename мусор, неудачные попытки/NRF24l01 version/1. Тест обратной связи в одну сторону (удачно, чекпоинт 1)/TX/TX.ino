#include <SPI.h>                                               // Подключаем библиотеку для работы с шиной SPI.
#include <nRF24L01.h>                                          // Подключаем файл настроек из библиотеки RF24.
#include <RF24.h>                                              // Подключаем библиотеку для работы с nRF24L01+.
RF24     radio(9, 10);                                         // Создаём объект radio для работы с библиотекой RF24, указывая номера выводов модуля (CE, SS)

#define numPlayer 2

#define mosfet 4  //мосфет
#define button 5  //кнопка
#define green_led 6  //красный светодиод
#define red_led 7  //зеленый светодиод
byte butt;
bool flag = 0;
bool flag2 = 0;
int myData[1];                                            // Объявляем массив для приёма и хранения данных (до 32 байт включительно).
int ackData[1];
void setup() {
  Serial.begin(9600);
  radio.begin           ();                                  // Инициируем работу модуля nRF24L01+.
  radio.setChannel      (0x4c);                                // Указываем канал передачи данных (от 0 до 125), 27 - значит передача данных осуществляется на частоте 2,427 ГГц.
  radio.setDataRate     (RF24_2MBPS);                        // Указываем скорость передачи данных (RF24_250KBPS, RF24_1MBPS, RF24_2MBPS), RF24_1MBPS - 1Мбит/сек.
  radio.setPALevel      (RF24_PA_MAX);                       // Указываем мощность передатчика (RF24_PA_MIN=-18dBm, RF24_PA_LOW=-12dBm, RF24_PA_HIGH=-6dBm, RF24_PA_MAX=0dBm).
  radio.enableAckPayload();                                  // Указываем что в пакетах подтверждения приёма есть блок с пользовательскими данными.
  radio.enableDynamicPayloads();
  
  radio.openWritingPipe (0xAABBCCDD22LL);                    // Открываем трубу с адресом 0xAABBCCDD11 для передачи данных (передатчик может одновременно вещать только по одной трубе).

  pinMode(button, INPUT_PULLUP);  //подключение кнопки
  pinMode(mosfet, OUTPUT);      //подключение мосфета
  pinMode(red_led, OUTPUT);      //подключение красного светодиода
  pinMode(green_led, OUTPUT);      //подключение зеленого светодиода

  myData[0] = numPlayer;
}

void win() {
  digitalWrite(mosfet, 1);
  digitalWrite(red_led, 1);
  digitalWrite(green_led, 0);
}

void loop() {
  butt = !digitalRead(button);
  if (butt == 0 && !flag) {
    flag = !flag;
    if (radio.write(&myData, sizeof(myData))) {
      digitalWrite(red_led, LOW);
      digitalWrite(green_led, HIGH);
      Serial.print("Отправка "); Serial.println(myData[0]);
    } else {
      digitalWrite(red_led, HIGH);
      digitalWrite(green_led, LOW);
      Serial.println("Феил отправки");
    }
    if ( radio.isAckPayloadAvailable() ) {
      digitalWrite(red_led, LOW);
      digitalWrite(green_led, HIGH);
      radio.read(&ackData, sizeof(ackData));
      Serial.print("Обратная связь "); Serial.println(ackData[0]);
    } else {
      digitalWrite(red_led, HIGH);
      digitalWrite(green_led, LOW);
      Serial.println("Феил обратной связи");
    }
    delay(600);
  }
  if (butt == 1 && flag) {
    flag = !flag;
  }
}
