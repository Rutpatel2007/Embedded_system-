#ifndef CONFIG_H
#define CONFIG_H

// ==========================================
// TrueSense Hardware & GPIO Pin Map
// ==========================================

// I2C Bus Configuration (INA219, DS3231 RTC)
#define I2C_SDA 21
#define I2C_SCL 22

// SPI Bus Configuration (microSD Card Module)
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK  18
#define SD_CS   5

// Analog Inputs
// Note: GPIO 34 is input-only and connected to MQ-135 output via 10k/20k voltage divider.
#define MQ_ADC_PIN 34

// Digital Outputs / Relay Control
// Controls Domain 2 (Equipment / Fan power circuit)
#define RELAY_PIN 4

// I2C Peripheral Addresses
#define INA219_I2C_ADDRESS 0x40
#define RTC_ADDRESS        0x68

// ==========================================
// Sensor & Algorithm Operating Constants
// (Subject to physical validation & tuning)
// ==========================================

// MQ-135 Sampling & Attenuation Settings
#define MQ_ADC_ATTEN           ADC_11db        // 0-3.3V range
#define MQ_DIVIDER_R1_OHMS     10000.0f         // Top resistor in voltage divider
#define MQ_DIVIDER_R2_OHMS     20000.0f         // Bottom resistor in voltage divider

// Default System Sampling & Processing Intervals
#define SENSOR_SAMPLE_INTERVAL_MS 500   // 500 ms sampling rate
#define NETWORK_SYNC_INTERVAL_MS 30000  // 30 seconds sync window

// Layer 1 Physical Plausibility Parameters
#define GAS_BASELINE_WINDOW_SEC  60     // 60-second rolling baseline
#define GAS_SPIKE_RATIO_THRESHOLD 1.20f // 20% increase over baseline triggers spike detection
#define POWER_ACTIVE_THRESHOLD_MW 50.0f // Equipment active threshold in mW
#define POWER_HISTORY_WINDOW_SEC  90     // 90-second trailing power history window

// Layer 2 Sensor Identity Parameters
#define FINGERPRINT_POINTS       20     // Number of points in warm-up curve
#define FINGERPRINT_SIM_THRESHOLD 0.85f // Cosine similarity threshold for identity match

#endif // CONFIG_H
