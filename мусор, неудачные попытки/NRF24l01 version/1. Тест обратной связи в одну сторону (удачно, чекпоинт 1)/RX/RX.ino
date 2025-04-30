#include <SPI.h>                                               // Подключаем библиотеку для работы с шиной SPI.
#include <nRF24L01.h>                                          // Подключаем файл настроек из библиотеки RF24.
#include <RF24.h>                                              // Подключаем библиотеку для работы с nRF24L01+.
RF24     radio(9, 10);                                         // Создаём объект radio для работы с библиотекой RF24, указывая номера выводов модуля (CE, SS).
int      myData[1];                                            // Объявляем массив для приёма и хранения данных (до 32 байт включительно).
int      ackData[1];                                           // Объявляем массив для передачи данных в пакете подтверждения приёма (до 32 байт включительно).
//
void setup() {
  Serial.begin(9600);
  radio.begin();                                             // Инициируем работу nRF24L01+
  radio.setChannel      (0x4c);                                // Указываем канал передачи данных (от 0 до 125), 27 - значит передача данных осуществляется на частоте 2,427 ГГц.
  radio.setDataRate     (RF24_2MBPS);                        // Указываем скорость передачи данных (RF24_250KBPS, RF24_1MBPS, RF24_2MBPS), RF24_1MBPS - 1Мбит/сек.
  radio.setPALevel      (RF24_PA_MAX);                       // Указываем мощность передатчика (RF24_PA_MIN=-18dBm, RF24_PA_LOW=-12dBm, RF24_PA_HIGH=-6dBm, RF24_PA_MAX=0dBm).
  radio.enableAckPayload();                                  // Указываем что в пакетах подтверждения приёма есть блок с пользовательскими данными.
  radio.enableDynamicPayloads();                             // Разрешить динамически изменяемый размер блока данных на всех трубах.
 
  radio.openReadingPipe (1, 0xAABBCCDD11LL);                 // Открываем 1 трубу с адресом 0xAABBCCDD11, для приема данных.
  radio.openReadingPipe (2, 0xAABBCCDD22LL);
  radio.openReadingPipe (3, 0xAABBCCDD33LL);
  radio.openReadingPipe (4, 0xAABBCCDD44LL);
  radio.startListening  ();                                  // Включаем приемник, начинаем прослушивать открытые трубы.
}                                                              // В модуле имеется 3 буфера FIFO, значит в них одновременно может находиться до трёх разных или одинаковых данных для ответа по одной или разным трубам.

void loop() {
  //byte pipeNo;
  if (radio.available()) {
    radio.read(&myData, sizeof(myData));
    Serial.print("Принял от ");
    Serial.println(myData[0]);
    ackData[0] = myData[0] * 11;
    radio.writeAckPayload (myData[0], &ackData, sizeof(ackData));
    }
    }
