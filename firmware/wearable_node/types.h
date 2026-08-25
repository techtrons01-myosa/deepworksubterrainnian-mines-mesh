// ============================================================
//  MYOSA Node Firmware — Shared Types
//  File    : types.h
//  Purpose : All enumerations, packet structures, and shared
//            data types used across every firmware module.
// ============================================================
#pragma once
#include <Arduino.h>
#include <stdint.h>
#include "config.h"

// ------------------------------------------------------------
// Enumerations
// ------------------------------------------------------------

/** Identifies every packet type that travels over ESP-NOW. */
enum class PacketType : uint8_t {
    HEARTBEAT        = 0x01,
    SENSOR_DATA      = 0x02,
    NODE_DISCOVERY   = 0x03,
    NODE_STATUS      = 0x04,
    NEIGHBOR_UPDATE  = 0x05,
    LOCATION_UPDATE  = 0x06,
    ALERT            = 0x07,
    WIFI_REQUEST     = 0x08,
    WIFI_STATUS      = 0x09,
    SLEEP            = 0x0A,
    WAKE             = 0x0B,
    CONFIG_SYNC      = 0x0D,
};

/** Bitmask of failed subsystems; multiple faults can coexist. */
enum SystemFault : uint16_t {
    FAULT_NONE          = 0x0000,
    FAULT_BMP180_INIT   = 0x0001,
    FAULT_BMP180_READ   = 0x0002,
    FAULT_MPU6050_INIT  = 0x0004,
    FAULT_MPU6050_READ  = 0x0008,
    FAULT_APDS_INIT     = 0x0010,
    FAULT_APDS_READ     = 0x0020,
    FAULT_OLED_INIT     = 0x0040,
    FAULT_ESPNOW_INIT   = 0x0080,
    FAULT_BLE_INIT      = 0x0100,
    FAULT_WIFI_CONNECT  = 0x0200,
    FAULT_BAD_PACKET    = 0x0400,
};

/** OLED menu screens — 1 dedicated info screen per sensor/subsystem. */
enum class MenuScreen : uint8_t {
    HOME          = 0,  // Node status & mesh overview
    TEMPERATURE   = 1,  // BMP180 ambient temperature & safety status
    PRESSURE_ALT  = 2,  // BMP180 barometric pressure & altitude
    ACCELEROMETER = 3,  // MPU6050 3-axis accelerometer & gravity
    GYROSCOPE     = 4,  // MPU6050 3-axis gyroscope & motion
    LIGHT_RGB     = 5,  // APDS9960 ambient light & RGB color
    PROXIMITY     = 6,  // APDS9960 proximity & rubble detection
    NETWORK       = 7,  // ESP-NOW, BLE, WiFi & node count
    LOCATOR       = 8,  // Continuous real-time RF dBm locator
    ALERTS        = 9,  // Alert history log
    SCREEN_COUNT  = 10,
};

/** Node power state. */
enum class PowerState : uint8_t {
    ACTIVE        = 0,
    SLEEP_PENDING = 1,
    SLEEPING      = 2,
    WAKE_PENDING  = 3,
};

/** Wi-Fi anti-flap state machine states. */
enum class WiFiState : uint8_t {
    IDLE          = 0,
    CONDITION_MET = 1,
    ACTIVATING    = 2,
    CONNECTED     = 3,
    DEACTIVATING  = 4,
};

/** Buzzer alert patterns. */
enum class BuzzerPattern : uint8_t {
    NONE             = 0,
    BOOT             = 1,
    NODE_DISCOVERED  = 2,
    NODE_LOST        = 3,
    NETWORK_CRITICAL = 4,
    FAULT            = 5,
    MINE_ALERT       = 6,   // temperature/pressure safety threshold exceeded
    DISCONNECTED     = 7,   // all mesh nodes lost — plays continuously
    EVACUATION       = 8,   // emergency evacuation siren from gateway
};

/** Decoded APDS9960 gesture events. */
enum class GestureEvent : uint8_t {
    NONE  = 0,
    UP    = 1,
    DOWN  = 2,
    LEFT  = 3,
    RIGHT = 4,
};

/** Peer node lifecycle status. */
enum class NodeStatus : uint8_t {
    UNKNOWN  = 0,
    ACTIVE   = 1,
    SLEEPING = 2,
    LOST     = 3,
};

/** HW-763 press classification. */
enum class TouchEvent : uint8_t {
    NONE         = 0,
    SHORT_PRESS  = 1,   // Single tap
    LONG_PRESS   = 2,   // Hold 1.8s
    TRIPLE_PRESS = 3,   // Triple tap (used in Locator mode toggle)
};

// ------------------------------------------------------------
// Core Data Structures
// ------------------------------------------------------------

/** Unified sensor snapshot — populated every SENSOR_READ_INTERVAL_MS. */
struct SensorData {
    // BMP180 — pressure + altitude + temperature
    float   pressure;          // hPa
    float   altitude;          // m (relative to sea level)
    float   bmpTemperature;    // °C (from BMP180)
    bool    bmpValid;

    // MPU6050 (filtered)
    float   accelX, accelY, accelZ;  // g
    float   gyroX,  gyroY,  gyroZ;   // deg/s
    float   mpuTemperature;           // °C (internal die temperature)
    bool    mpuValid;

    // APDS9960
    uint8_t  proximity;       // 0–255
    uint16_t ambientLight;    // lux (raw count)
    bool     apdsValid;

    uint32_t timestamp;        // millis() when snapshot was taken
};

/** State record for one known peer node. */
struct NodeRecord {
    char       nodeId[NODE_ID_MAX_LEN];  // "MYO-AABBCC\0"
    uint8_t    mac[6];                   // ESP-NOW MAC
    int8_t     espnowRssi;              // Last ESP-NOW signal
    int8_t     bleRssi;                 // Last BLE signal
    uint8_t    proximityEstimate;        // Fused 0–255
    uint32_t   lastSeen;                 // millis()
    uint8_t    neighborCount;
    NodeStatus status;
    bool       espnowKnown;
    bool       bleKnown;
    SensorData lastSensorData;
};

// ------------------------------------------------------------
// Wire Packet (ESP-NOW — max 250 bytes total)
// ------------------------------------------------------------

/** Every ESP-NOW message uses this envelope. */
struct __attribute__((packed)) MyosaPacket {
    uint16_t magic;                            // PACKET_MAGIC = 0x4D59
    uint8_t  senderMac[6];                     // Originating node MAC
    uint8_t  packetType;                       // PacketType enum value
    uint32_t timestamp;                        // millis() at send time
    uint16_t sequence;                         // Rolling counter (0–65535)
    uint8_t  hopCount;                         // Remaining hops / TTL
    uint8_t  payloadLen;                       // Bytes used in payload[]
    uint8_t  payload[ESPNOW_MAX_PAYLOAD];      // Variable-length body
    uint8_t  crc8;                             // CRC-8 of all preceding bytes
};

// ------------------------------------------------------------
// Typed Payloads  (cast payload[] to these structs)
// ------------------------------------------------------------

/** HEARTBEAT payload. */
struct __attribute__((packed)) HeartbeatPayload {
    uint8_t  espnowOk;       // 1 = operational
    uint8_t  bleOk;
    uint8_t  wifiOn;
    uint8_t  neighborCount;
    uint8_t  sensorStatus;   // bit0=BMP, bit1=MPU, bit2=APDS
    uint8_t  batteryPct;     // 0–100 placeholder
    uint16_t faultMask;      // SystemFault bitmask
    char     nodeId[NODE_ID_MAX_LEN];
};

/** SENSOR_DATA payload — subset of SensorData for wire. */
struct __attribute__((packed)) SensorPayload {
    float   temperature;
    float   pressure;
    float   altitude;
    float   accelX, accelY, accelZ;
    float   gyroX,  gyroY,  gyroZ;
    uint8_t proximity;
    uint16_t ambientLight;
};

/** ALERT payload. */
struct __attribute__((packed)) AlertPayload {
    char    originId[NODE_ID_MAX_LEN];   // Node raising the alert
    char    subjectId[NODE_ID_MAX_LEN];  // Node the alert is about
    uint8_t alertCode;                   // Mirrors BuzzerPattern
    char    message[40];
};

/** NODE_DISCOVERY payload. */
struct __attribute__((packed)) DiscoveryPayload {
    char    nodeId[NODE_ID_MAX_LEN];
    uint8_t mac[6];
    uint8_t neighborCount;
    uint8_t capabilities;   // bit0=BMP, bit1=MPU, bit2=APDS, bit3=OLED
};

/** WIFI_STATUS payload. */
struct __attribute__((packed)) WifiStatusPayload {
    uint8_t wifiOn;
    uint8_t connected;
    int8_t  rssi;
    char    ssid[33];
};

/** CONFIG_SYNC payload — sent by Gateway to sync ground datum & evacuation. */
struct __attribute__((packed)) ConfigSyncPayload {
    float    groundDatum;       // Ground level datum (meters AMSL)
    uint32_t serverTimestamp;   // Epoch / sync time
    uint8_t  evacuateActive;    // 1 = Emergency Evacuation Active
};
