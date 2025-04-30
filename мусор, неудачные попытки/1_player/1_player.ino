//1 игрок:  1 - передача, R - проиграл/красный свет,
// A - победил, E - потушить всё

#include <VirtualWire.h>
#include <VirtualWire_Config.h>

#define button 5  //кнопка
#define mosfet 4  //мосфет
#define green_led 6  //красный светодиод
#define red_led 7  //зеленый светодиод
#define RX 3      //приёмник
#define TX 2      //передатчик

char *controller;

byte butt;
bool flag = 0;
bool flag2 = 0;

void setup() {
  /// ПЕРЕДАТЧИК /////
  vw_set_ptt_inverted(true); //
  vw_set_tx_pin(TX);   //пин подключения
  vw_setup(4000);// скорость передачи данных в Kbps

  ////////  ПРИЁМНИК ///////
  vw_set_rx_pin(RX);
  vw_setup(4000); // бит в секунду

  pinMode(button, INPUT_PULLUP);  //подключение кнопки
  pinMode(mosfet, OUTPUT);      //подключение мосфета
  pinMode(red_led, OUTPUT);      //подключение красного светодиода
  pinMode(green_led, OUTPUT);      //подключение зеленого светодиода
  vw_rx_start(); // запуск фазовой автоподстройки частоты (ФАПЧ)
  digitalWrite(green_led, 1);
}

void loop() {
  butt = digitalRead(button);
  if (butt == 0 && flag == 0) {
    controller = "1";
    vw_send((uint8_t *)controller, strlen(controller));
    vw_wait_tx(); // ждем, пока отправится все сообщение
    flag = 1;
  }

  uint8_t buf[VW_MAX_MESSAGE_LEN];
  uint8_t buflen = VW_MAX_MESSAGE_LEN;
  if (vw_get_message(buf, &buflen)) //
  {
    if (buf[0] == 'A') {
      digitalWrite(mosfet, 1);
      digitalWrite(red_led, 1);
      digitalWrite(green_led, 0);
      flag2 = 1;
    }
    else if (buf[0] == 'B') {
      digitalWrite(mosfet, 0);
      digitalWrite(red_led, 1);
      digitalWrite(green_led, 0);
    }
    else if (buf[0] == 'E') {
      digitalWrite(mosfet, 0);
      digitalWrite(red_led, 0);
      digitalWrite(green_led, 1);
      flag = 0; flag2 = 0;
    }
  }
}
