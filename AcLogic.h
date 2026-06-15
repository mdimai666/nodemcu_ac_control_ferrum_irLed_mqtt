#pragma once
#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>

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
  uint16_t _pin;
  IRrecv* _irrecv;
  ACState _state;

  uint8_t getFerrumTemp(uint8_t tempCode) {
    // Таблица из ваших дампов
    switch (tempCode) {
      case 0x4: return 24;  // 0x40, 0x44, 0x48, 0x4C -> младший ниббл 0x4 = 24°C
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

  void decodeCoolix(uint32_t code) {
    // Код выключения
    if (code == 0xB27BE0 || code == 0xB27BE1) {
      _state.power = false;
      return;
    }

    _state.power = true;

    uint8_t b2 = (code >> 8) & 0xFF;  // Байт 2 (режим / скорость)
    uint8_t b3 = code & 0xFF;         // Байт 3 (температура + базовая маска режима)

    // 1. Декодируем режим (объединенная группа по младшему нибблу байта b3)
    uint8_t lowNibble = b3 & 0x0F;

    if (lowNibble == 0x08) {
      _state.mode = ACMode::MODE_AUTO;  // AUTO: 0xB21FC8 (на конце 8)
    } else if (lowNibble == 0x00) {
      _state.mode = ACMode::MODE_COOL;  // COOL: 0xB2BF40 (на конце 0)
    } else if (lowNibble == 0x04) {
      // Разделяем DRY и FAN, так как у обоих младший ниббл равен 4
      if (b2 == 0x1F) {
        _state.mode = ACMode::MODE_DRY;  // DRY: 0xB21FC4 (группа 0x1F)
      } else {
        _state.mode = ACMode::MODE_FAN;  // FAN: 0xB2BFE4 (группа 0xBF)
      }
    } else if (lowNibble == 0x0C) {
      _state.mode = ACMode::MODE_HEAT;  // HEAT: 0xB2BF4C (на конце C)
    } else {
      _state.mode = ACMode::MODE_AUTO;  // fallback
    }

    // 2. Декодируем температуру (старший ниббл байта 3)
    uint8_t tempCode = (b3 >> 4) & 0x0F;
    _state.targetTemp = getFerrumTemp(tempCode);

    // 3. Декодируем скорость вентилятора из байта 2
    if (_state.mode == ACMode::MODE_COOL || _state.mode == ACMode::MODE_HEAT || _state.mode == ACMode::MODE_FAN) {
      uint8_t fanCode = (b2 >> 4) & 0x0F;  // Старший ниббл байта b2
      switch (fanCode) {
        case 0xB: _state.fan = ACFan::FAN_AUTO; break;    // 0xBF -> 0xB
        case 0x9: _state.fan = ACFan::FAN_LOW; break;     // 0x9F -> 0x9
        case 0x5: _state.fan = ACFan::FAN_MEDIUM; break;  // 0x5F -> 0x5
        case 0x3: _state.fan = ACFan::FAN_HIGH; break;    // 0x3F -> 0x3
        default: _state.fan = ACFan::FAN_AUTO; break;
      }
    } else {
      // В AUTO и DRY вентилятор всегда AUTO
      _state.fan = ACFan::FAN_AUTO;
    }

    if (code == 0xB5F5A5) {
      // Код кнопки DISPLAY. Переключаем состояние (если поле поддерживается вашим классом)
      _state.displayOn = !_state.displayOn;
      Serial.println("Команда: Переключение Дисплея (Display ON/OFF)");
      return;  // Выходим из функции, так как этот код не содержит данных о режиме/температуре
    }
  }

public:
  IrAC(uint16_t pin)
    : _pin(pin), _irrecv(nullptr) {
    _state.targetTemp = 24;
  }

  ~IrAC() {
    if (_irrecv != nullptr) delete _irrecv;
  }

  void begin(const bool pullup = false) {
    _irrecv = new IRrecv(_pin);
    if (pullup) {
      pinMode(_pin, INPUT_PULLUP);
    } else {
      pinMode(_pin, INPUT);
    }
    _irrecv->enableIRIn(true);
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

  void setPower(bool on) {
    _state.power = on;
  }
  void setMode(ACMode mode) {
    _state.mode = mode;
  }
  void setTemperature(uint8_t temp) {
    _state.targetTemp = temp;
  }
  void setFan(ACFan speed) {
    _state.fan = speed;
  }
  void setTurbo(bool on) {
    _state.turbo = on;
  }
  void setHealth(bool on) {
    _state.health = on;
  }
  void setSleep(bool on) {
    _state.sleep = on;
  }
  void setDisplay(bool on) {
    _state.displayOn = on;
  }

  ACState getState() const {
    return _state;
  }
};