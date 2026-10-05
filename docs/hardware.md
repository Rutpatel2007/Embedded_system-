# TrueSense Hardware Specification & Contract

This document defines the authoritative single-node hardware configuration, pin mapping, power domain architecture, and electrical constraints for the TrueSense sensor node.

---

## 1. GPIO Pin & Peripheral Mapping

| Peripheral | Sub-System / Function | ESP32 GPIO | Logic Level / Protocol | Notes / Restraints |
| :--- | :--- | :--- | :--- | :--- |
| **I2C SDA** | INA219 / DS3231 SDA | `GPIO 21` | 3.3V I2C | Shared I2C Bus |
| **I2C SCL** | INA219 / DS3231 SCL | `GPIO 22` | 3.3V I2C | Shared I2C Bus |
| **SPI MOSI**| microSD MOSI | `GPIO 23` | 3.3V SPI | Shared SPI Bus |
| **SPI MISO**| microSD MISO | `GPIO 19` | 3.3V SPI | Shared SPI Bus |
| **SPI SCK** | microSD SCK | `GPIO 18` | 3.3V SPI | Shared SPI Bus |
| **SPI CS**  | microSD Chip Select | `GPIO 5` | 3.3V Digital Output | Dedicated CS |
| **MQ Analog**| MQ-135 Gas Sensor ADC | `GPIO 34` | 0 – 3.3V Analog Input | **Input-Only Pin**. Requires 10k/20k Voltage Divider |
| **Relay**   | Equipment Relay Control| `GPIO 4` | 3.3V Digital Output | Controls Domain 2 equipment power |

> **Restricted Pins**: `GPIO 0`, `GPIO 2`, and `GPIO 12` must **NOT** be used for peripheral connections due to ESP32 boot-strapping pin requirements.

---

## 2. I2C Bus Addressing

- **INA219 Power Monitor**: `0x40`
- **DS3231 RTC**: `0x68`

---

## 3. Power Architecture & Domain Isolation

To preserve Layer 1 physical plausibility verification, power domains **MUST** remain strictly separated. The INA219 sensor measures monitored equipment power consumption only—it **must not** measure MQ heater power consumption.

```text
               +--------------------------------------------+
               |             5V DC Power Source             |
               +----------------------+---------------------+
                                      |
                 +--------------------+--------------------+
                 |                                         |
                 v                                         v
       [ Domain 1: MQ Heater ]                 [ Domain 2: Equipment ]
          5V Always-ON Line                       5V Switched Line
                 |                                         |
                 v                                         v
         +---------------+                         +---------------+
         | MQ-135 Heater |                         |  Relay Common |
         +---------------+                         +-------+-------|
                                                           |
                                                           v
                                                    +--------------+
                                                    | INA219 V+/V- | (In series with equipment)
                                                    +------+-------+
                                                           |
                                                           v
                                                   [ Fan / Load ]
```

### Domain 1 — MQ Heater Power Domain
- **Voltage**: 5V DC
- **Source**: Directly powered from the main 5V rail.
- **Behavior**: **Always ON**. Never routed through the relay or through the INA219 current path.

### Domain 2 — Monitored Equipment Power Domain
- **Voltage**: 5V DC
- **Source**: Routed from 5V rail through the 1-channel Relay (controlled by `GPIO 4`).
- **Sensing**: The INA219 current sensor shunt resistor is placed **in series** along the high-side positive rail feeding the load/fan.
- **Behavior**: Controlled programmatically for testing/simulation.

### Domain 3 — ESP32 & Logic Power Domain
- **Voltage**: 3.3V DC (regulated via ESP32 onboard regulator or HW-228 buck converter).
- **Peripherals**: ESP32 MCU, DS3231 logic, microSD module logic, I2C bus pull-ups.

---

## 4. Signal Conditioning & Voltage Divider Circuit

The MQ-135 analog output ($V_{OUT\_MQ}$) ranges from $0\text{V}$ up to $5\text{V}$. Since ESP32 ADC pins (`GPIO 34`) can only accept up to $3.3\text{V}$, a resistive voltage divider is required:

```text
MQ-135 AOUT (0 - 5V)
       |
     [R1: 10 kΩ]
       |
       +---> ESP32 GPIO 34 (Max ~3.3V)
       |
     [R2: 20 kΩ]
       |
      GND
```

### Conversion Formula:
$$V_{\text{ADC}} = V_{\text{OUT\_MQ}} \times \frac{R_2}{R_1 + R_2} = V_{\text{OUT\_MQ}} \times \frac{20\text{k}\Omega}{10\text{k}\Omega + 20\text{k}\Omega} = V_{\text{OUT\_MQ}} \times \frac{2}{3}$$

$$V_{\text{OUT\_MQ}} = V_{\text{ADC}} \times 1.5$$

---

## 5. Critical Hardware Constraints & Notes

1. **MQ-135 Burn-in Requirement**:
   - The MQ-135 sensing element requires a **24 to 48 hour continuous burn-in period** under power before sensor resistance $R_S$ stabilizes.
   - Raw ADC values during initial power-on cannot be trusted for scientific concentration without calibration.
2. **DS3231 RTC Battery**:
   - The DS3231 module **must** have a CR2032 coin cell installed to ensure clock continuity across system power reboots and offline periods.
3. **ESP32 ADC Non-Linearity**:
   - ESP32 ADCs exhibit non-linear response at very low ($<0.1\text{V}$) and very high ($>3.1\text{V}$) input voltages. Attenuation setting `ADC_ATTEN_DB_11` should be configured.
