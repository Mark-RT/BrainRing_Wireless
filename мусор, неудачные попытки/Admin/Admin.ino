#include <VirtualWire.h>
#include <VirtualWire_Config.h>

#define RX 2      //приёмник
#define TX 3      //передатчик
#define button 4  //главная кнопка
#define led 5

byte butt;

char *controller;

bool flag = 0; //разрешает/запрещает свет победителю

void setup() {
  Serial.begin(9600);
  /// ПЕРЕДАТЧИК /////
  vw_set_ptt_inverted(true); //
  vw_set_tx_pin(TX);   //пин подключения
  vw_setup(4000);// скорость передачи данных в Kbps

  ////////  ПРИЁМНИК ///////
  vw_set_rx_pin(RX);
  vw_setup(4000); // бит в секунду

  pinMode(button, INPUT_PULLUP);
  pinMode(led, OUTPUT);      //подключение красного светодиода
  vw_rx_start(); // запуск фазовой автоподстройки частоты (ФАПЧ)
}

void loop() {
  butt = !digitalRead(button);
  uint8_t buf[VW_MAX_MESSAGE_LEN];
  uint8_t buflen = VW_MAX_MESSAGE_LEN;
  if (vw_get_message(buf, &buflen)) //
  {
    digitalWrite(led, 1);
    if (buf[0] == '1' && flag == 0) {
      for(int i=0; i<5; i++) {
      controller = "A" ;
      vw_send((uint8_t *)controller, strlen(controller));
      vw_wait_tx(); // ждем, пока отправится все сообщение
      flag = 1;
      }
     Serial.println("1 player");
    }
    else if (buf[0] == '2' && flag == 0) {
      for(int i=0; i<5; i++) {
      controller = "B" ;
      vw_send((uint8_t *)controller, strlen(controller));
      vw_wait_tx(); // ждем, пока отправится все сообщение
      flag = 1;
      }
      Serial.println("2 player");
    }
    else if (buf[0] == '3' && flag == 0) {
      controller = "C" ;
      vw_send((uint8_t *)controller, strlen(controller));
      vw_wait_tx(); // ждем, пока отправится все сообщение
      flag = 1;
      Serial.println("3 player");
    }
    else if (buf[0] == '4' && flag == 0) {
      controller = "D" ;
      vw_send((uint8_t *)controller, strlen(controller));
      vw_wait_tx(); // ждем, пока отправится все сообщение
      flag = 1;
      Serial.println("4 player");
    }
  }
 /* if (flag == 1) {
      controller = "R" ;
      vw_send((uint8_t *)controller, strlen(controller));
      vw_wait_tx(); // ждем, пока отправится все сообщение
      Serial.println("Fail");
    }*/

  if (butt == 1) {
    controller = "E" ;
    vw_send((uint8_t *)controller, strlen(controller));
    vw_wait_tx(); // ждем, пока отправится все сообщение
    flag = 0;
    buf[0] = 0;
    digitalWrite(led, 0);
    Serial.println("Reset");
  }
}
