#include <SPI.h>                                               // Подключаем библиотеку для работы с шиной SPI.
#include <nRF24L01.h>                                          // Подключаем файл настроек из библиотеки RF24.
#include <RF24.h>                                              // Подключаем библиотеку для работы с nRF24L01+.
RF24     radio(9, 10);                                         // Создаём объект radio для работы с библиотекой RF24, указывая номера выводов модуля (CE, SS)
int      TXData[1];                                            // Объявляем массив для хранения и передачи данных.
int      RXData[2];
#define mosfet 4  //мосфет
#define button 5  //кнопка
#define green_led 6  //красный светодиод
#define red_led 7  //зеленый светодиод
byte butt;
bool flag = 0;
bool flag2 = 0;
byte num_player = 1;
uint8_t pipe;
byte counter;
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
  radio.setPALevel      (RF24_PA_MAX);                       // Указываем мощность передатчика (RF24_PA_MIN=-18dBm, RF24_PA_LOW=-12dBm, RF24_PA_HIGH=-6dBm, RF24_PA_MAX=0dBm).

  radio.openWritingPipe (0xAABBCCDD11LL);                    // Открываем трубу с адресом 0xAABBCCDD11 для передачи данных (передатчик может одновременно вещать только по одной трубе).
  radio.openReadingPipe (1, 0xAABBCCDD99LL);
  
  radio.powerUp();
  radio.stopListening();
  pinMode(button, INPUT_PULLUP);  //подключение кнопки
  pinMode(mosfet, OUTPUT);      //подключение мосфета
  pinMode(red_led, OUTPUT);      //подключение красного светодиода
  pinMode(green_led, OUTPUT);      //подключение зеленого светодиода
  digitalWrite(green_led, 1);
}

void win() {
  digitalWrite(mosfet, 1);
  digitalWrite(red_led, 1);
  digitalWrite(green_led, 0);
}

int transmit(){
  radio.stopListening();
  if (radio.write(&TXData, sizeof(TXData))) return 1;
  else return 0;
  radio.startListening();
}

void loop(void) {
  byte gotByte;
  Serial.print("Sending... ");Serial.println(counter);
  
  unsigned long last_time = micros();         //запоминаем время отправки
  
  if ( radio.write(&counter,1) ){                 //отправляем значение counter
    if(!radio.available()){                     //если получаем пустой ответ
      Serial.print("Empty, "); Serial.print(" Time: "); Serial.print(micros()-last_time); Serial.println(" microseconds"); Serial.println();
    }else{      
      while(radio.available() ){                      // если в ответе что-то есть
        radio.read( &gotByte, 1 );                  // читаем
        Serial.print("Anser: "); Serial.print(gotByte); Serial.print(" Time: "); Serial.print(micros()-last_time); Serial.println(" microseconds"); Serial.println();                                 
      counter++; 
      }
    }
    
  }else{   Serial.println("Fail"); }    
  
  delay(1000);
  /*butt = !digitalRead(button);
  byte gotByte, RxByte;
  unsigned long last_time = micros();         //запоминаем время отправки
  if (butt == 0 && !flag) {
    flag = !flag;
    //if (transmit()) Serial.println("Отправка успешна");
    //else Serial.println("Феил отправки");
    if ( radio.write(&num_player,1) ){                 //отправляем значение counter
    if(!radio.available()){                     //если получаем пустой ответ
      Serial.print("Empty, "); Serial.print(" Time: "); Serial.print(micros()-last_time); Serial.println(" microseconds"); Serial.println();
    }else{      
      while(radio.available() ){                      // если в ответе что-то есть
        radio.read( &gotByte, 1 );                  // читаем
        Serial.print("Anser: "); Serial.print(gotByte); Serial.print(" Time: "); Serial.print(micros()-last_time); Serial.println(" microseconds"); Serial.println();                                
      }
    }
    
  }else{   Serial.println("Fail"); }
  flag2 = 0;
  }
  if (butt == 1 && flag) {
    flag = !flag;
  }
  if (radio.available(0xAABBCCDD99LL) && flag2) {                     // Если в буфере имеются принятые данные из пакета подтверждения приёма, то ...
    radio.read(&RxByte, 1);                 // Читаем данные из буфера в массив ackData указывая сколько всего байт может поместиться в массив.
    Serial.print("Обратная связь ");
    Serial.println(RxByte);
  } //else Serial.println("Феил получения");
  delay(1000);*/
}
