#include <mybase_esp32_OTA_webserial.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "AcLogic.h"
#include "wifi_config.h"

// Указываем пин D5 (в NodeMCU пин D5 соответствует GPIO14)
const uint16_t kRecvPin = D5;
IrAC ac(kRecvPin);

// Настройки MQTT
const char* mqttServer = "192.168.3.6";
const int mqttPort = 1883;  // стандартный порт MQTT
WiFiClient espClient;
PubSubClient mqttClient(espClient);
String mqttClientId = "nodemcu-ac-control-ferrum-ir_led-";

// MQTT Топики
const char* topic_status_json = "/home/cabinet/ferrum/ac/status";
const char* topic_get_request = "/home/cabinet/ferrum/ac/get";
const char* topic_cmd_power = "/home/cabinet/ferrum/ac/cmd/power";
const char* topic_cmd_temp = "/home/cabinet/ferrum/ac/cmd/temp";
const char* topic_cmd_mode = "/home/cabinet/ferrum/ac/cmd/mode";
const char* topic_cmd_fan = "/home/cabinet/ferrum/ac/cmd/fan";
const char* topic_cmd_errors = "/home/cabinet/ferrum/ac/cmd/errors";

void setup() {
  Serial.begin(115200);

  Serial.println("Booting...");
  pinMode(ledPin, OUTPUT);

  setupWiFi(WIFI_SSID, WIFI_PASSWORD);
  setupOTA("nodemcu-ac-control-ferrum-ir_led-mqtt", "1234");

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Запуск ИК-приемника
  ac.begin(true);

  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(mqttCallback);

  Serial.println("Start");
  Serial.println("Ожидание сигналов от пульта кондиционера...");
}

void loop() {
  ArduinoOTA.handle();  // OTA обязателен
  // handleBlink();        // Мигание LED
  // webSerial.loop();

  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  // Вызываем handle в каждом цикле loop.
  // Метод вернет true только если пульт передал новое состояние
  if (ac.handle()) {
    // webSerial.print("Состояние изменилось, отправка в MQTT...");
    Serial.println("Состояние изменилось, отправка в MQTT...");
    printStateToSerial();
    publishStateJson();
  }

  yield();  // Системный сброс таймаутов ESP8266
}

void printStateToSerial() {
  // Получаем текущую структуру состояния
  ACState current = ac.getState();

  // Выводим обновленные параметры в консоль
  Serial.print("Питание: ");
  Serial.println(current.power ? "ВКЛ" : "ВЫКЛ");

  if (current.power || true) {
    Serial.print("Режим: ");
    switch (current.mode) {
      case ACMode::MODE_AUTO: Serial.println("AUTO"); break;
      case ACMode::MODE_COOL: Serial.println("COOL (Охлаждение)"); break;
      case ACMode::MODE_DRY: Serial.println("DRY (Осушение)"); break;
      case ACMode::MODE_HEAT: Serial.println("HEAT (Обогрев)"); break;
      case ACMode::MODE_FAN: Serial.println("FAN (Вентиляция)"); break;
    }

    Serial.print("Температура: ");
    Serial.print(current.targetTemp);
    Serial.println(" °C");

    Serial.print("Вентилятор: ");
    switch (current.fan) {
      case ACFan::FAN_AUTO: Serial.println("AUTO"); break;
      case ACFan::FAN_LOW: Serial.println("LOW (Низкий)"); break;
      case ACFan::FAN_MEDIUM: Serial.println("MEDIUM (Средний)"); break;
      case ACFan::FAN_HIGH: Serial.println("HIGH (Высокий)"); break;
    }
  }
  Serial.println("----------------------------------------");
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Подключение к MQTT...");

    auto clientId = mqttClientId + String(ESP.getChipId(), HEX);
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("Успешно подключено!");
      // Подписываемся на топики управления
      mqttClient.subscribe(topic_cmd_power);
      mqttClient.subscribe(topic_cmd_temp);
      mqttClient.subscribe(topic_cmd_mode);
      mqttClient.subscribe(topic_get_request);
    } else {
      Serial.print("Ошибка, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" Пробуем снова через 5 секунд...");
      delay(5000);
    }
  }
}

// Обработка входящих команд из MQTT
void mqttCallback(char* topic_ch, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  message.trim();
  String topic = String(topic_ch);

  // webSerial.print(">input mqtt [" + topic + "]: " + message);
  Serial.println(">input mqtt [" + topic + "]: " + message);

  if (topic == topic_get_request) {
    // Принудительный запрос JSON-статуса
    publishStateJson();
    return;
  }

  // auto oldValues = ac.getState();

  if (topic == topic_cmd_power) {
    if (message == "ON" || message == "1")
      ac.setPower(true);
    else if (message == "OFF" || message == "0")
      ac.setPower(true);
    else
      mqttClient.publish(topic_cmd_errors, "Unknown power value", true);

  } else if (topic == topic_cmd_temp) {
    int temp = message.toInt();
    if (temp >= 16 && temp <= 32) ac.setTemperature(temp);
  } else if (topic == topic_cmd_mode) {
    if (message == "cool") ac.setMode(ACMode::MODE_COOL);
    else if (message == "heat") ac.setMode(ACMode::MODE_HEAT);
    else if (message == "dry") ac.setMode(ACMode::MODE_DRY);
    else if (message == "fan") ac.setMode(ACMode::MODE_FAN);
    else if (message == "auto") ac.setMode(ACMode::MODE_AUTO);
  } else if (topic == topic_cmd_fan) {
    if (message == "low") ac.setFan(ACFan::FAN_LOW);
    else if (message == "medium") ac.setFan(ACFan::FAN_MEDIUM);
    else if (message == "high") ac.setFan(ACFan::FAN_HIGH);
    else if (message == "auto") ac.setFan(ACFan::FAN_AUTO);
  } else {
    mqttClient.publish(topic_cmd_errors, ("Unknown cmd: " + message).c_str(), true);
    return;
  }

  // if (oldValues != ac.getState()) {
  //   //что что изменили
  //   publishStateJson();
  // }
}

String modeToString(ACMode mode) {
  switch (mode) {
    case ACMode::MODE_COOL: return "cool";
    case ACMode::MODE_HEAT: return "heat";
    case ACMode::MODE_DRY: return "dry";
    case ACMode::MODE_FAN: return "fan";
    default: return "auto";
  }
}

String fanToString(ACFan fan) {
  switch (fan) {
    case ACFan::FAN_LOW: return "low";
    case ACFan::FAN_MEDIUM: return "medium";
    case ACFan::FAN_HIGH: return "high";
    default: return "auto";
  }
}

// Функция отправки состояния в MQTT в формате JSON
void publishStateJson() {
  ACState state = ac.getState();
  JsonDocument doc;

  doc["power"] = state.power ? "ON" : "OFF";
  doc["mode"] = modeToString(state.mode);
  doc["fan_speed"] = fanToString(state.fan);
  doc["target_temp"] = state.targetTemp;
  doc["indoor_temp"] = state.indoorTemp;

  char buffer[256];
  serializeJson(doc, buffer);
  mqttClient.publish(topic_status_json, buffer, true);  // true - флаг Retain
}