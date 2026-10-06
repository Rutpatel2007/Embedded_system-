#include "relay.h"

RelayController::RelayController(uint8_t pin)
    : _pin(pin), _activeHigh(true), _isOn(false) {}

void RelayController::begin(bool activeHigh) {
    _activeHigh = activeHigh;
    pinMode(_pin, OUTPUT);
    off(); // Ensure equipment defaults to OFF on boot
}

void RelayController::on() {
    digitalWrite(_pin, _activeHigh ? HIGH : LOW);
    _isOn = true;
}

void RelayController::off() {
    digitalWrite(_pin, _activeHigh ? LOW : HIGH);
    _isOn = false;
}

void RelayController::toggle() {
    if (_isOn) {
        off();
    } else {
        on();
    }
}

bool RelayController::isEquipmentOn() const {
    return _isOn;
}
