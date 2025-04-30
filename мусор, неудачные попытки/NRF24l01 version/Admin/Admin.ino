#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
RF24 radio(9, 10);
int RXData[1];
int TXData[2];
uint8_t pipe;
#define button 5  //главная кнопка
#define green_led 6
#define red_led 7
byte butt;
bool flag = 0;
void setup() {
  Serial.begin(9600);
  radio.begin();                                  // Инициируем работу модуля nRF24L01+.
  radio.setAutoAck(1);         //режим подтверждения приёма, 1 вкл 0 выкл
  radio.setRetries(0,15);     //(время между попыткой достучаться, число попыток)
  radio.enableAckPayload();    //разрешить отсылку данных в ответ на входящий сигнал
  radio.setPayloadSize(32);     //размер пакета, в байтах
  radio.enableAckPayload();                                  // Указываем что в пакетах подтверждения приёма есть блок с пользовательскими данными.
  radio.enableDynamicPayloads();
  radio.setChannel(0x60);
  radio.setDataRate     (RF24_1MBPS);                        // Указываем скорость передачи данных (RF24_250KBPS, RF24_1MBPS, RF24_2MBPS), RF24_1MBPS - 1Мбит/сек.
  radio.setPALevel      (RF24_PA_MAX);
  
  radio.openWritingPipe (0xAABBCCDD99LL);// Разрешить динамически изменяемый размер блока данных на всех трубах.
  radio.openReadingPipe (1, 0xAABBCCDD11LL);                 // Открываем 1 трубу с адресом 0xAABBCCDD11, для приема данных.
  radio.openReadingPipe (2, 0xAABBCCDD22LL);                 // Открываем 2 трубу с адресом 2 передатчика 0xAABBCCDD22, для приема данных.
  radio.openReadingPipe (3, 0xAABBCCDD33LL);                 // Открываем 3 трубу с адресом 3 передатчика 0xAABBCCDD33, для приема данных.
  radio.openReadingPipe (4, 0xAABBCCDD44LL);
  
  radio.powerUp();
  radio.startListening  ();                                  // Включаем приемник, начинаем прослушивать открытые трубы.
  pinMode(button, INPUT_PULLUP);  //подключение кнопки
  pinMode(red_led, OUTPUT);      //подключение красного светодиода
  pinMode(green_led, OUTPUT);      //подключение зеленого светодиода
  digitalWrite(green_led, 1);
}

void loop(void) {
  byte pipeNo, gotByte;                          
    while( radio.available(&pipeNo)){    // слушаем эфир со всех труб
      radio.read( &gotByte, 1 );         // чиатем входящий сигнал
      radio.writeAckPayload(pipeNo,&gotByte, 1 );  // отправляем обратно то что приняли
      Serial.print("Recieved: "); Serial.println(gotByte); 
   }
  /*if (radio.available(&pipe)) {
    radio.read(&RXData, sizeof(RXData));
    Serial.print("Пришли данные от ");
    Serial.print(pipe);
    Serial.print("\t");
    Serial.println(RXData[0]);
    radio.stopListening  ();
    switch (pipe) {
      case 1:
        TXData[0] = 1;
        TXData[1] = 2;
        radio.writeAckPayload (1, &TXData, sizeof(TXData) );
        if (radio.write(&TXData, sizeof(TXData))) {       // Отправляем данные из массива myData указывая сколько байт массива мы хотим отправить.
          Serial.println("Отправка обратной связи");
        } else {
          Serial.println("Феил обратной связи");
        }

        radio.startListening ();
        break;

      /*case 2:
        ackData[0] = 22;
        radio.writeAckPayload (2, &ackData, sizeof(ackData) );
        break;

        case 3:
        ackData[0] = 33;
        radio.writeAckPayload (3, &ackData, sizeof(ackData) );
        break;

        case 4:
        ackData[0] = 44;
        radio.writeAckPayload (4, &ackData, sizeof(ackData) );
        break;
      
      default:
        Serial.println("Default");
        break;
    }
  }*/
  
  butt = digitalRead(button);
  if (butt == 0 && !flag) {
    radio.stopListening();
    TXData[0] = 11;
        TXData[1] = 12;
    Serial.println("Нажал");
    if (radio.write(&TXData, sizeof(TXData))) { 
      radio.startListening();
      Serial.println("Отправка успешна");
    } else {
      radio.startListening();
      Serial.println("Феил отправки");
    }
  }
  if (butt == 1 && flag) {
    flag = !flag;
  }
}
