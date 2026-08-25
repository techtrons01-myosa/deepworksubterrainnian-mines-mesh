// ============================================================
//  MYOSA Node Firmware — System Manager Implementation
//  File    : system_manager.cpp
// ============================================================
#include "system_manager.h"
#include <esp_system.h>
#include <esp_mac.h>       // esp_read_mac() — ESP32 Arduino 3.x / ESP-IDF 5.x

namespace SystemManager {

// ---------- Private state ----------
static char     s_nodeId[NODE_ID_MAX_LEN]  = {0};
static uint8_t  s_mac[6]                   = {0};
static uint16_t s_faultMask                = FAULT_NONE;
static uint32_t s_bootTime                 = 0;

// ---- CRC-8 (Maxim/Dallas) used across the project ----
// Exposed here so other modules can call it without a
// separate utility file.
static uint8_t _crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    while (len--) {
        uint8_t byte = *data++;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if ((crc ^ byte) & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
            byte <<= 1;
        }
    }
    return crc;
}

// ---- Build node ID from MAC or override ----
static void _buildNodeId() {
    esp_read_mac(s_mac, ESP_MAC_WIFI_STA);  // ESP-IDF 5.x API

#ifdef NODE_ID_OVERRIDE
    strncpy(s_nodeId, NODE_ID_OVERRIDE, NODE_ID_MAX_LEN - 1);
    s_nodeId[NODE_ID_MAX_LEN - 1] = '\0';
#else
    // Use last 3 MAC bytes → "MYO-AABBCC"
    snprintf(s_nodeId, sizeof(s_nodeId), "MYO-%02X%02X%02X",
             s_mac[3], s_mac[4], s_mac[5]);
#endif
}

// ---------- Public API ----------

void init() {
    s_bootTime   = millis();
    s_faultMask  = FAULT_NONE;

    _buildNodeId();

    DBG("SystemManager: node=%s  MAC=%02X:%02X:%02X:%02X:%02X:%02X",
        s_nodeId,
        s_mac[0], s_mac[1], s_mac[2],
        s_mac[3], s_mac[4], s_mac[5]);

    // Expose CRC-8 to other translation units via a global function
    // (other .cpp files include system_manager.h and call the extern below)
}

void update() {
    // Watchdog feed / periodic health checks can be added here
}

const char* getNodeId() {
    return s_nodeId;
}

const uint8_t* getMac() {
    return s_mac;
}

void registerFault(uint16_t faultBits) {
    s_faultMask |= faultBits;
    DBG("SystemManager: FAULT registered  mask=0x%04X", s_faultMask);
}

void clearFault(uint16_t faultBits) {
    s_faultMask &= ~faultBits;
    DBG("SystemManager: FAULT cleared  mask=0x%04X", s_faultMask);
}

uint16_t getFaultMask() {
    return s_faultMask;
}

bool hasFault() {
    return s_faultMask != FAULT_NONE;
}

void reboot(const char* reason) {
    DBG("SystemManager: REBOOT requested — %s", reason ? reason : "");
    Serial.flush();
    delay(200);
    ESP.restart();
}

uint32_t uptimeSeconds() {
    return (millis() - s_bootTime) / 1000UL;
}

} // namespace SystemManager

// ---- Global CRC-8 helper (used by mesh_manager and telemetry_manager) ----
uint8_t myosa_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    while (len--) {
        uint8_t byte = *data++;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if ((crc ^ byte) & 0x80) { crc = (crc << 1) ^ 0x31; }
            else                      { crc <<= 1; }
            byte <<= 1;
        }
    }
    return crc;
}
