// ============================================================
//  MYOSA Node Firmware — Proximity Manager Implementation
//  File    : proximity_manager.cpp
// ============================================================
#include "proximity_manager.h"
#include "ble_manager.h"
#include "sensor_manager.h"
#include "mesh_manager.h"

namespace ProximityManager {

static uint8_t  s_nearestProx    = 0;
static uint32_t s_lastUpdateMs   = 0;
static const uint32_t UPDATE_INTERVAL_MS = 2000;

/**
 * Map BLE RSSI (dBm) to a 0–255 proximity score.
 * Scale: -40 dBm → 255 (close), -100 dBm → 0 (far)
 */
static uint8_t _rssiToProximity(int8_t rssi) {
    if (rssi >= -40) return 255;
    if (rssi <= -100) return 0;
    // Linear map from [-100, -40] → [0, 255]
    return (uint8_t)(((int16_t)rssi + 100) * 255 / 60);
}

void init() {
    s_nearestProx  = 0;
    s_lastUpdateMs = 0;
    DBG("ProximityManager: init OK");
}

void update() {
    if ((millis() - s_lastUpdateMs) < UPDATE_INTERVAL_MS) return;
    s_lastUpdateMs = millis();

    // --- BLE RSSI contribution ---
    int8_t bleRssi = BleManager::nearestPeerRssi();
    uint8_t bleProx = (bleRssi != 0) ? _rssiToProximity(bleRssi) : 0;

    // --- APDS9960 proximity contribution ---
    const SensorData& sd = SensorManager::getSensorData();
    uint8_t apdsProx = sd.apdsValid ? sd.proximity : 0;

    // --- Weighted fusion (60% BLE, 40% APDS) ---
    // Only use APDS if its value is meaningfully non-zero (>20)
    // to avoid noise affecting the estimate when no object is near.
    uint8_t fused;
    if (apdsProx > 20 && bleProx > 0) {
        fused = (uint8_t)((bleProx * 60 + apdsProx * 40) / 100);
    } else if (bleProx > 0) {
        fused = bleProx;
    } else if (apdsProx > 20) {
        fused = apdsProx;
    } else {
        fused = 0;
    }

    s_nearestProx = fused;

    // Push proximity estimate into the neighbour records
    // (apply APDS to the closest BLE-known peer)
    const NodeRecord* peers = MeshManager::getNeighbors();
    // (NodeRecord is read-only here; proximity_estimate stored externally)
    // Full bidirectional integration can be extended via a MeshManager setter.

    DBG("ProximityManager: bleProx=%d apdsProx=%d fused=%d", bleProx, apdsProx, fused);
}

uint8_t nearestProximity() {
    return s_nearestProx;
}

} // namespace ProximityManager
