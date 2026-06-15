#pragma once
#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>

enum class ACMode : uint8_t {
  MODE_AUTO = 0,
  MODE_COOL = 1,
  MODE_DRY = 2,
  MODE_HEAT = 3,
  MODE_FAN = 4
};

enum class ACFan : uint8_t {
  FAN_AUTO = 0,
  FAN_LOW = 1,
  FAN_MEDIUM = 2,
  FAN_HIGH = 3
};

struct ACState {
  bool power = false;
  ACMode mode = ACMode::MODE_AUTO;
  ACFan fan = ACFan::FAN_AUTO;
  uint8_t targetTemp = 0;
  bool turbo = false;
  bool health = false;
  bool sleep = false;
  bool displayOn = true;
  float indoorTemp = 0.0f;
  float outdoorTemp = 0.0f;
  int16_t outdoorTempRaw = 0;
  uint8_t compressorPower = 0;

  bool operator==(const ACState& other) const {
    return power == other.power && mode == other.mode && fan == other.fan && targetTemp == other.targetTemp && turbo == other.turbo && health == other.health && sleep == other.sleep && displayOn == other.displayOn && fabs(indoorTemp - other.indoorTemp) < 0.01f && fabs(outdoorTemp - other.outdoorTemp) < 0.01f && outdoorTempRaw == other.outdoorTempRaw && compressorPower == other.compressorPower;
  }

  bool operator!=(const ACState& other) const {
    return !(*this == other);
  }
};

class IrAC {
private:
  uint16_t _recvPin;
  uint16_t _sendPin;
  IRrecv* _irrecv;
  IRsend* _irsend;
  ACState _state;

  bool _recvPin_pullup;

  uint8_t getFerrumTempCode(uint8_t temp) {
    // Обратное преобразование температуры в код
    switch (temp) {
      case 17: return 0x0;
      case 18: return 0x1;
      case 19: return 0x3;
      case 20: return 0x2;
      case 21: return 0x6;
      case 22: return 0x7;
      case 23: return 0x5;
      case 24: return 0x4;
      case 25: return 0xC;
      case 26: return 0xD;
      case 27: return 0x9;
      case 28: return 0x8;
      case 29: return 0xA;
      case 30: return 0xB;
      default: return 0x4;  // 24°C по умолчанию
    }
  }

  uint8_t getFerrumTemp(uint8_t tempCode) {
    // Таблица из ваших дампов
    switch (tempCode) {
      case 0x4: return 24;
      case 0x0: return 17;
      case 0x1: return 18;
      case 0x3: return 19;
      case 0x2: return 20;
      case 0x6: return 21;
      case 0x7: return 22;
      case 0x5: return 23;
      case 0xC: return 25;
      case 0xD: return 26;
      case 0x9: return 27;
      case 0x8: return 28;
      case 0xA: return 29;
      case 0xB: return 30;
      default: return 17;
    }
  }

  uint32_t buildCoolixCommand() {
    uint32_t code = 0xB20000;  // Базовая команда

    // Команда выключения
    if (!_state.power) {
      return 0xB27BE0;
    }

    uint8_t b2 = 0;  // Байт 2 (режим / скорость)
    uint8_t b3 = 0;  // Байт 3 (температура + базовая маска режима)

    // 1. Устанавливаем режим и базовую маску
    switch (_state.mode) {
      case ACMode::MODE_AUTO:
        b3 = 0x08;  // AUTO: младший ниббл = 8
        b2 = 0x1F;  // Группа для AUTO
        break;
      case ACMode::MODE_COOL:
        b3 = 0x00;  // COOL: младший ниббл = 0
        b2 = 0xBF;
        break;
      case ACMode::MODE_DRY:
        b3 = 0x04;  // DRY: младший ниббл = 4
        b2 = 0x1F;
        break;
      case ACMode::MODE_HEAT:
        b3 = 0x0C;  // HEAT: младший ниббл = C
        b2 = 0xBF;
        break;
      case ACMode::MODE_FAN:
        b3 = 0x04;  // FAN: младший ниббл = 4
        b2 = 0xBF;
        break;
    }

    // 2. Устанавливаем температуру (старший ниббл b3)
    uint8_t tempCode = getFerrumTempCode(_state.targetTemp);
    b3 = (tempCode << 4) | (b3 & 0x0F);

    // 3. Устанавливаем скорость вентилятора для соответствующих режимов
    if (_state.mode == ACMode::MODE_COOL || _state.mode == ACMode::MODE_HEAT || _state.mode == ACMode::MODE_FAN) {
      uint8_t fanCode = 0xB;  // AUTO по умолчанию
      switch (_state.fan) {
        case ACFan::FAN_AUTO: fanCode = 0xB; break;
        case ACFan::FAN_LOW: fanCode = 0x9; break;
        case ACFan::FAN_MEDIUM: fanCode = 0x5; break;
        case ACFan::FAN_HIGH: fanCode = 0x3; break;
      }
      b2 = (fanCode << 4) | (b2 & 0x0F);
    }

    // Собираем код
    code = (code & 0xFFFF00) | (b2 << 8) | b3;

    // Для режима FAN нужна особая маска
    if (_state.mode == ACMode::MODE_FAN) {
      code = 0xB2BF00 | (b2 << 8) | b3;
    }

    // Для AUTO/DRY своя маска
    if (_state.mode == ACMode::MODE_AUTO || _state.mode == ACMode::MODE_DRY) {
      code = 0xB21F00 | (b2 << 8) | b3;
    }

    return code;
  }

  void sendCoolixCommand(uint32_t command) {
    if (_irsend != nullptr) {
      Serial.print("[SEND] COOLIX command: 0x");
      Serial.println(command, HEX);
      _irsend->sendCOOLIX(command);
      delay(50);  // Небольшая задержка между командами
    }
  }

  void decodeCoolix(uint32_t code) {
    // Код выключения
    if (code == 0xB27BE0 || code == 0xB27BE1) {
      _state.power = false;
      return;
    }

    if (code == 0xB5F5A5) {
      // Код кнопки DISPLAY
      _state.displayOn = !_state.displayOn;
      Serial.println("Команда: Переключение Дисплея (Display ON/OFF)");
      return;
    }

    _state.power = true;

    uint8_t b2 = (code >> 8) & 0xFF;
    uint8_t b3 = code & 0xFF;
    uint8_t lowNibble = b3 & 0x0F;

    // Декодируем режим
    if (lowNibble == 0x08) {
      _state.mode = ACMode::MODE_AUTO;
    } else if (lowNibble == 0x00) {
      _state.mode = ACMode::MODE_COOL;
    } else if (lowNibble == 0x04) {
      if (b2 == 0x1F) {
        _state.mode = ACMode::MODE_DRY;
      } else {
        _state.mode = ACMode::MODE_FAN;
      }
    } else if (lowNibble == 0x0C) {
      _state.mode = ACMode::MODE_HEAT;
    }

    // Декодируем температуру
    uint8_t tempCode = (b3 >> 4) & 0x0F;
    _state.targetTemp = getFerrumTemp(tempCode);

    // Декодируем скорость вентилятора
    if (_state.mode == ACMode::MODE_COOL || _state.mode == ACMode::MODE_HEAT || _state.mode == ACMode::MODE_FAN) {
      uint8_t fanCode = (b2 >> 4) & 0x0F;
      switch (fanCode) {
        case 0xB: _state.fan = ACFan::FAN_AUTO; break;
        case 0x9: _state.fan = ACFan::FAN_LOW; break;
        case 0x5: _state.fan = ACFan::FAN_MEDIUM; break;
        case 0x3: _state.fan = ACFan::FAN_HIGH; break;
        default: _state.fan = ACFan::FAN_AUTO; break;
      }
    } else {
      _state.fan = ACFan::FAN_AUTO;
    }
  }

public:
  IrAC(uint16_t recvPin, uint16_t sendPin)
    : _recvPin(recvPin), _sendPin(sendPin), _irrecv(nullptr), _irsend(nullptr) {
    _state.targetTemp = 24;
  }

  ~IrAC() {
    if (_irrecv != nullptr) delete _irrecv;
    if (_irsend != nullptr) delete _irsend;
  }

  void begin(const bool pullup = false) {
    // Инициализация приемника
    _irrecv = new IRrecv(_recvPin);
    _recvPin_pullup = pullup;
    if (pullup) {
      pinMode(_recvPin, INPUT_PULLUP);
    } else {
      pinMode(_recvPin, INPUT);
    }
    // _irrecv->enableIRIn(true); // только так работает плата отдельно
    _irrecv->enableIRIn(_recvPin_pullup);

    // Инициализация передатчика
    _irsend = new IRsend(_sendPin);
    _irsend->begin();
  }

  bool handle() {
    if (_irrecv == nullptr) return false;

    decode_results results;
    if (_irrecv->decode(&results)) {
      bool stateChanged = false;

      if (results.decode_type == decode_type_t::COOLIX) {
        Serial.print("[DUMP] HEX: 0x");
        unsigned long low = results.value & 0xFFFFFFFF;
        Serial.print(low, HEX);
        Serial.print(", Bits: ");
        Serial.println(results.bits);

        if (results.bits == 24) {
          ACState oldState = _state;
          decodeCoolix((uint32_t)results.value);

          if (_state != oldState) {
            stateChanged = true;
          }
        }
      } else {
        Serial.print("[DUMP UNKNOWN] Protocol: ");
        Serial.print((int)results.decode_type);
        Serial.print(", Bits: ");
        Serial.println(results.bits);
      }

      _irrecv->resume();
      return stateChanged;
    }
    return false;
  }

  // Методы установки параметров с автоматической отправкой
  void setPower(bool on) {
    _state.power = on;
    sendState();
  }

  void setMode(ACMode mode) {
    _state.mode = mode;
    sendState();
  }

  void setTemperature(uint8_t temp) {
    temp = constrain(temp, 17, 30);
    _state.targetTemp = temp;
    sendState();
  }

  void setFan(ACFan speed) {
    _state.fan = speed;
    sendState();
  }

  void setTurbo(bool on) {
    _state.turbo = on;
    sendState();  // Турбо может влиять на отправляемую команду
  }

  void setHealth(bool on) {
    _state.health = on;
    sendState();
  }

  void setSleep(bool on) {
    _state.sleep = on;
    sendState();
  }

  void setDisplay(bool on) {
    _state.displayOn = on;
    // sendState();
    sendCommandWithDisable(0xB5F5A5);
  }

  void sendState() {
    if (_irsend != nullptr) {
      _irrecv->disableIRIn();
      delay(50);
      uint32_t command = buildCoolixCommand();
      sendCoolixCommand(command);
      delay(50);
      _irrecv->enableIRIn(_recvPin_pullup);
    }
  }

  void sendCommandWithDisable(uint32_t command) {
    if (_irsend != nullptr) {
      _irrecv->disableIRIn();
      delay(50);
      sendCoolixCommand(command);
      delay(50);
      _irrecv->enableIRIn(_recvPin_pullup);
    }
  }

  ACState getState() const {
    return _state;
  }
};