#include "AcLogic.h"

// Указываем пин D5 (в NodeMCU пин D5 соответствует GPIO14)
const uint16_t kRecvPin = D5;
IrAC ac(kRecvPin);

void setup() {
  Serial.begin(115200);

  // Запуск ИК-приемника
  ac.begin(true);

  Serial.println("Start");
  Serial.println("Ожидание сигналов от пульта кондиционера...");
}

void loop() {
  // Вызываем handle в каждом цикле loop. 
  // Метод вернет true только если пульт передал новое состояние
  if (ac.handle()) {
    Serial.println("\n[!] СОСТОЯНИЕ КОНДИЦИОНЕРА ИЗМЕНИЛОСЬ [!]");
    
    // Получаем текущую структуру состояния
    ACState current = ac.getState();
    
    // Выводим обновленные параметры в консоль
    Serial.print("Питание: "); Serial.println(current.power ? "ВКЛ" : "ВЫКЛ");
    
    if (current.power || true) {
      Serial.print("Режим: ");
      switch (current.mode) {
        case ACMode::MODE_AUTO: Serial.println("AUTO"); break;
        case ACMode::MODE_COOL: Serial.println("COOL (Охлаждение)"); break;
        case ACMode::MODE_DRY:  Serial.println("DRY (Осушение)"); break;
        case ACMode::MODE_HEAT: Serial.println("HEAT (Обогрев)"); break;
        case ACMode::MODE_FAN:  Serial.println("FAN (Вентиляция)"); break;
      }
      
      Serial.print("Температура: "); Serial.print(current.targetTemp); Serial.println(" °C");
      
      Serial.print("Вентилятор: ");
      switch (current.fan) {
        case ACFan::FAN_AUTO:   Serial.println("AUTO"); break;
        case ACFan::FAN_LOW:    Serial.println("LOW (Низкий)"); break;
        case ACFan::FAN_MEDIUM: Serial.println("MEDIUM (Средний)"); break;
        case ACFan::FAN_HIGH:   Serial.println("HIGH (Высокий)"); break;
      }
    }
    Serial.println("----------------------------------------");
  }
  
  yield(); // Системный сброс таймаутов ESP8266
}
