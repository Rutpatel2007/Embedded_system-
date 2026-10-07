#ifndef RELAY_H
#define RELAY_H

#include <Arduino.h>

class RelayController {
public:
    RelayController(uint8_t pin = 4);
    
    // Initialize relay GPIO pin and set default state (OFF)
    void begin(bool activeHigh = true);
    
    // Power ON equipment
    void on();
    
    // Power OFF equipment
    void off();
    
    // Toggle relay state
    void toggle();
    
    // Return current power state (true = ON, false = OFF)
    bool isEquipmentOn() const;

private:
    uint8_t _pin;
    bool _activeHigh;
    bool _isOn;
};

#endif // RELAY_H
