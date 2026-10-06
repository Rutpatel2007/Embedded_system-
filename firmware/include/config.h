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

// Layer 2 Sensor Identity Parameters (UNVALIDATED DEVELOPMENT THRESHOLDS)
#define FINGERPRINT_POINTS          20     // Number of points in warm-up curve
#define FINGERPRINT_SIM_THRESHOLD    0.85f // UNVALIDATED DEVELOPMENT THRESHOLD: Similarity >= 0.85 -> SENSOR_OK
#define FINGERPRINT_REVIEW_THRESHOLD 0.70f // UNVALIDATED DEVELOPMENT THRESHOLD: 0.70 <= Similarity < 0.85 -> DEGRADED_REVIEW

// Layer 2 Admin Security Password (FOR SERIAL ENROLLMENT AUTHORIZATION ONLY)
#define L2_ADMIN_PASSWORD           "TrueSenseAdmin2026"

// Layer 2 NVS Storage Configuration
#define NVS_L2_NAMESPACE            "truesense_l2"
#define NVS_L2_KEY_FINGERPRINT      "enrolled_fp"
#define L2_SCHEMA_VERSION           0x0001

// ==========================================
// Phase 2 RTOS, Queue & Ring Buffer Config
// ==========================================

// Uncomment to enable simulation mode (hardware-independent test harness)
#define USE_MOCK_SENSORS

// FreeRTOS Queue & Ring Buffer Sizes
#define SENSOR_QUEUE_LEN          10     // Bounded queue capacity between SensorTask & AlgorithmTask
#define GAS_RING_BUFFER_SIZE     120     // 60-second rolling gas history (120 samples @ 500ms)
#define POWER_RING_BUFFER_SIZE   180     // 90-second trailing power history (180 samples @ 500ms)

// FreeRTOS Task Stack Sizes & Priorities
#define STACK_SIZE_SENSOR_TASK    4096
#define STACK_SIZE_ALGO_TASK      4096
#define STACK_SIZE_HEALTH_TASK    2048

#define PRIORITY_SENSOR_TASK      3      // High priority for acquisition timing
#define PRIORITY_ALGO_TASK        2      // Medium priority for processing
#define PRIORITY_HEALTH_TASK      1      // Low priority for diagnostics

// Mock Simulation Scenarios
enum MockScenario {
    SCENARIO_NORMAL,               // Baseline gas + low noise + stable equipment
    SCENARIO_PLAUSIBLE,            // Equipment ON -> delayed gas emission rise -> equipment OFF
    SCENARIO_SUSPICIOUS,           // Unexpected gas spike with 0 mW equipment activity
    SCENARIO_FAULT,                // Sensor disconnection / invalid ADC / timestamp failure
    SCENARIO_MATCHING_SENSOR,      // Live sensor matching reference fingerprint
    SCENARIO_REPLACED_SENSOR,      // Live sensor baseline shifted significantly (e.g. 2.2V vs 1.0V)
    SCENARIO_UNENROLLED_NODE,      // Sensor identity unenrolled
    SCENARIO_INSUFFICIENT_HISTORY, // Buffer contains <2 valid features
    SCENARIO_WARM_REBOOT,          // MCU reboot without 5V power cycle (warmupValid = false)
    SCENARIO_COLD_BOOT             // Cold 5V power-on (warmupValid = true)
};

#endif // CONFIG_H
