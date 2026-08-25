// ============================================================
//  MYOSA Node Firmware — Configuration
//  File    : config.h
//  Purpose : All compile-time constants, pin assignments, and
//            tunable parameters.  Change values here; do NOT
//            scatter magic numbers throughout the code.
// ============================================================
#pragma once

// ------------------------------------------------------------
// Node Identity
// ------------------------------------------------------------
// Uncomment and set to force a specific ID (e.g. "MYO-001").
// When commented out the ID is derived from the last 3 bytes
// of the ESP32's MAC address: "MYO-AABBCC".
// #define NODE_ID_OVERRIDE "MYO-001"

// Maximum length of a node-ID string (including NUL terminator)
#define NODE_ID_MAX_LEN  12

// ------------------------------------------------------------
// I²C Bus
// ------------------------------------------------------------
#define PIN_SDA   21
#define PIN_SCL   22

// ------------------------------------------------------------
// Peripheral GPIO
// ------------------------------------------------------------
#define PIN_HW763   4    // HW-763 capacitive touch module (SIG pin, active-LOW)
//
// 3-Pin Active Buzzer wiring:
//   Pin 1 GND → ESP32 GND
//   Pin 2 VCC → ESP32 3.3V  (or 5V if your buzzer needs it)
//   Pin 3 SIG → ESP32 GPIO25   (HIGH = buzzer ON)
#define PIN_BUZZER  25   // Active buzzer SIG pin (OUTPUT, HIGH=ON)


// ------------------------------------------------------------
// Sensor I²C Addresses
// ------------------------------------------------------------
#define ADDR_BMP180    0x77   // BMP180 (fixed)
#define ADDR_MPU6050   0x68   // MPU6050 AD0 LOW=0x68, HIGH=0x69
#define ADDR_APDS9960  0x39   // APDS9960 (fixed)
#define ADDR_SSD1306   0x3C   // SSD1306 OLED (0x3C or 0x3D)

// ------------------------------------------------------------
// OLED Display
// ------------------------------------------------------------
#define OLED_WIDTH   128
#define OLED_HEIGHT   64
#define OLED_RESET    -1   // Share Arduino reset pin

// ------------------------------------------------------------
// Sensor Timing (milliseconds)
// ------------------------------------------------------------
#define SENSOR_READ_INTERVAL_MS   2000UL   // Full sensor cycle

// MPU6050 low-pass filter coefficient (0.0–1.0).
// Higher = smoother but more lag.
#define MPU_FILTER_ALPHA   0.85f

// APDS9960 proximity change threshold (0–255).
// Smaller = more sensitive.
#define APDS_PROX_THRESHOLD   10

// ---- Altitude & Barometric Calibration ----
// Sea-level atmospheric reference pressure in Pascals (Pa).
// Calibrated to 49.2m actual elevation at local atmospheric pressure (~1010 hPa):
#define SEA_LEVEL_PRESSURE_PA          101600.0f  // Pa (49.2m actual elevation calibration)
#define ELEVATION_CALIBRATION_OFFSET_M      0.0f  // Calibration offset in meters

// ------------------------------------------------------------
// Mesh / ESP-NOW
// ------------------------------------------------------------
#define MAX_MESH_NODES           20         // Max peer records
#define HEARTBEAT_INTERVAL_MS    1500UL     // Rapid 1.5s heartbeat for continuous live dBm tracking
#define NODE_LOSS_TIMEOUT_MS    10000UL     // Node loss timeout (10s)
#define ESPNOW_CHANNEL           1          // Wi-Fi/ESP-NOW channel
#define ESPNOW_MAX_PAYLOAD      200         // Bytes (ESP-NOW max 250)
#define PACKET_MAGIC            0x4D59u     // "MY"

// ESP-NOW broadcast MAC
#define ESPNOW_BROADCAST_ADDR   { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }

// ------------------------------------------------------------
// BLE
// ------------------------------------------------------------
#define MYOSA_BLE_PREFIX       "MYOSA-"
#define BLE_DEVICE_NAME_MAX    16
// UUIDs are arbitrary but consistent across all nodes
#define MYOSA_SERVICE_UUID     "4d594f53-4131-0000-0000-000000000001"
#define MYOSA_CHAR_UUID        "4d594f53-4131-0000-0000-000000000002"
#define BLE_SCAN_INTERVAL_MS   12000UL   // How often to run a BLE scan
#define BLE_SCAN_DURATION_S    3         // Duration of each scan window

// ------------------------------------------------------------
// Wi-Fi — Anti-Flap State Machine
// ------------------------------------------------------------
#define WIFI_ACTIVATION_DELAY_MS    5000UL    // Wait after condition met
#define WIFI_DEACTIVATION_DELAY_MS  10000UL   // Cool-down after disconnect
#define WIFI_CONNECTION_TIMEOUT_MS  15000UL   // Max time to connect
#define WIFI_MAX_RETRIES            3

// Credentials are intentionally blank; set before upload.
#define WIFI_SSID     ""
#define WIFI_PASSWORD ""

// ------------------------------------------------------------
// Power Management & Auto-Sleep
// ------------------------------------------------------------
// Hold HW-763 for this long to trigger display OFF/ON toggle manually
#define SLEEP_LONG_PRESS_MS    3000UL   // 3.0 seconds long-press to prevent accidental sleep

// Auto-sleep display after this idle duration (no touch / page change)
#define AUTO_SLEEP_TIMEOUT_MS  8000UL   // 8 seconds auto sleep on all screens

// ESP32 light-sleep wake sources
// (GPIO wake = PIN_HW763 rising edge)
#define LIGHT_SLEEP_ENABLED   0

// ------------------------------------------------------------
// Alert / Buzzer
// ------------------------------------------------------------
// All durations in milliseconds
#define BUZZ_SHORT_MS        100
#define BUZZ_LONG_MS         500
#define BUZZ_PAUSE_MS        150
#define BUZZ_DISCONNECT_MS   800   // long beep used in NODE_LOST pattern

// ---- Mine Safety Alert Thresholds ----
// Buzzer fires MINE_ALERT pattern when sensor crosses these limits.
// Temperature source: ambient temperature from BMP180 / MPU6050
#define MINE_TEMP_MAX_C        45.0f   // °C — mine warning threshold (hot environment)
// Pressure from BMP180
#define MINE_PRESSURE_MAX_HPA 1050.0f  // hPa — unusual high pressure alert
#define MINE_PRESSURE_MIN_HPA  950.0f  // hPa — unusual low pressure (altitude change)

// ---- Disconnect Buzzer ----
// 5.0s continuous beep followed by 2.0s break = 7000ms cycle
#define DISCONNECT_BUZZ_INTERVAL_MS  7000UL

// ---- Buried Person Detection ----
// APDS9960 + BMP180 used together to detect rubble burial:
//   Proximity  > BURIED_PROX_MIN  (something very close — rubble)
//   AmbLight   < BURIED_LIGHT_MAX (very dark — no light through rubble)
//   Pressure   > baseline + BURIED_PRESSURE_DELTA (rubble weight)
//   Consecutive readings >= BURIED_CONFIRM_COUNT before alert fires
#define BURIED_PROX_MIN           200     // 0-255, rubble close to sensor
#define BURIED_LIGHT_MAX           30     // raw lux count, very dark
#define BURIED_PRESSURE_DELTA     3.0f    // hPa above baseline = compressed
#define BURIED_CONFIRM_COUNT        5     // 5 × SENSOR_READ_INTERVAL_MS = 10s
#define BURIED_BUZZ_INTERVAL_MS  4000UL   // repeat MINE_ALERT every 4s when buried


// ------------------------------------------------------------
// Telemetry
// ------------------------------------------------------------
// Minimum gap between outbound SENSOR_DATA packets
#define TELEM_MIN_INTERVAL_MS  2000UL


// ------------------------------------------------------------
// Debug / Serial
// ------------------------------------------------------------
#define SERIAL_BAUD    115200
#define DEBUG_ENABLED  1

#if DEBUG_ENABLED
  #define DBG(fmt, ...)  Serial.printf("[%8lu] " fmt "\n", millis(), ##__VA_ARGS__)
#else
  #define DBG(fmt, ...)  do {} while (0)
#endif
