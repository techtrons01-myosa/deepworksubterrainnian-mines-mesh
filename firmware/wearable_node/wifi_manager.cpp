// ============================================================
//  MYOSA Node Firmware — Wi-Fi Manager Implementation
//  File    : wifi_manager.cpp
//
//  State machine:
//
//   IDLE  ──(both conditions met)──►  CONDITION_MET
//     ▲                                    │
//     │                               (activation delay)
//     │                                    │
//     │                                    ▼
//     │                             ACTIVATING
//     │                          (connect attempt)
//     │                                    │
//     │                         ┌──────────┴──────────┐
//     │                      Success               Timeout/Fail
//     │                         │                       │
//     │                         ▼                    retry or
//     │                    CONNECTED               ──► IDLE
//     │               (do upload work)
//     │                         │
//     │               (work done or timeout)
//     │                         │
//     │                         ▼
//     │                  DEACTIVATING
//     │               (cool-down delay)
//     │                         │
//     └─────────────────────────┘
//
// ============================================================
#include "wifi_manager.h"
#include "mesh_manager.h"
#include "ble_manager.h"
#include "system_manager.h"
#include "display_manager.h"
#include "alert_manager.h"

#include <WiFi.h>

namespace WiFiManager {

// ---- Private state ----
static WiFiState s_state           = WiFiState::IDLE;
static uint32_t  s_stateEnterMs    = 0;
static uint8_t   s_retryCount      = 0;
static bool      s_externalRequest = false;

static void _enterState(WiFiState newState) {
    s_state        = newState;
    s_stateEnterMs = millis();
    DBG("WiFiManager: → %d", (int)newState);
}

static bool _dualConditionMet() {
    if (strlen(WIFI_SSID) == 0) return false; // Never auto-activate if no SSID configured
    bool espnowOk = MeshManager::isEspNowOk() && (MeshManager::getNeighborCount() > 0);
    bool bleOk    = BleManager::myosaPeerSeen();
    return (espnowOk && bleOk) || s_externalRequest;
}

static void _startWifi() {
    if (strlen(WIFI_SSID) == 0) return;
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    DBG("WiFiManager: connecting to SSID '%s'", WIFI_SSID);
}

static void _stopWifi() {
    WiFi.disconnect(false); // Disconnect STA without shutting down radio
    DBG("WiFiManager: OFF (ESP-NOW preserved)");
}

// ---- Public API ----

void init() {
    s_state        = WiFiState::IDLE;
    s_retryCount   = 0;
    s_externalRequest = false;
    DBG("WiFiManager: init  state=IDLE (ESP-NOW STA mode preserved)");
}

void update() {
    uint32_t now     = millis();
    uint32_t elapsed = now - s_stateEnterMs;

    switch (s_state) {

        // --------------------------------------------------
        case WiFiState::IDLE:
            if (_dualConditionMet()) {
                DBG("WiFiManager: dual condition MET → CONDITION_MET");
                _enterState(WiFiState::CONDITION_MET);
            }
            break;

        // --------------------------------------------------
        case WiFiState::CONDITION_MET:
            if (!_dualConditionMet()) {
                // Condition lost before delay expired — back to IDLE
                DBG("WiFiManager: condition lost → IDLE");
                _enterState(WiFiState::IDLE);
                s_externalRequest = false;
            } else if (elapsed >= WIFI_ACTIVATION_DELAY_MS) {
                _enterState(WiFiState::ACTIVATING);
                _startWifi();
            }
            break;

        // --------------------------------------------------
        case WiFiState::ACTIVATING:
            if (WiFi.status() == WL_CONNECTED) {
                s_retryCount = 0;
                DBG("WiFiManager: CONNECTED  IP=%s  RSSI=%d",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());

                // Notify mesh
                WifiStatusPayload ws = {};
                ws.wifiOn    = 1;
                ws.connected = 1;
                ws.rssi      = (int8_t)WiFi.RSSI();
                strncpy(ws.ssid, WiFi.SSID().c_str(), 32);
                MeshManager::broadcast(PacketType::WIFI_STATUS,
                                       (const uint8_t*)&ws, sizeof(ws));
                _enterState(WiFiState::CONNECTED);

            } else if (elapsed >= WIFI_CONNECTION_TIMEOUT_MS) {
                s_retryCount++;
                DBG("WiFiManager: connect timeout (retry %d/%d)", s_retryCount, WIFI_MAX_RETRIES);
                if (s_retryCount >= WIFI_MAX_RETRIES) {
                    SystemManager::registerFault(FAULT_WIFI_CONNECT);
                    AlertManager::play(BuzzerPattern::FAULT);
                    _stopWifi();
                    _enterState(WiFiState::DEACTIVATING);
                } else {
                    _stopWifi();
                    delay(100);
                    _startWifi();
                    s_stateEnterMs = millis();  // Reset timeout
                }
            }
            break;

        // --------------------------------------------------
        case WiFiState::CONNECTED:
            // Remain connected while dual condition holds
            // (Upper-level app does its work here)
            if (!_dualConditionMet() || elapsed >= WIFI_CONNECTION_TIMEOUT_MS) {
                DBG("WiFiManager: disconnecting → DEACTIVATING");
                _stopWifi();
                s_externalRequest = false;

                WifiStatusPayload ws = {};
                ws.wifiOn = 0;
                MeshManager::broadcast(PacketType::WIFI_STATUS,
                                       (const uint8_t*)&ws, sizeof(ws));
                _enterState(WiFiState::DEACTIVATING);
            }
            break;

        // --------------------------------------------------
        case WiFiState::DEACTIVATING:
            // Cool-down period — no reconnects allowed yet
            if (elapsed >= WIFI_DEACTIVATION_DELAY_MS) {
                DBG("WiFiManager: cool-down done → IDLE");
                _enterState(WiFiState::IDLE);
            }
            break;
    }
}

bool isWifiOn() {
    return s_state == WiFiState::ACTIVATING ||
           s_state == WiFiState::CONNECTED;
}

bool isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void requestActivation() {
    if (s_state == WiFiState::IDLE || s_state == WiFiState::CONDITION_MET) {
        s_externalRequest = true;
        if (s_state == WiFiState::IDLE) {
            _enterState(WiFiState::CONDITION_MET);
        }
        DBG("WiFiManager: external activation request");
    }
}

void forceOff() {
    _stopWifi();
    s_externalRequest = false;
    s_retryCount      = 0;
    _enterState(WiFiState::IDLE);
    DBG("WiFiManager: forced OFF");
}

WiFiState getState() {
    return s_state;
}

} // namespace WiFiManager
