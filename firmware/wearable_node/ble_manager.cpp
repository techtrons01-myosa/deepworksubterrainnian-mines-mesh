// ============================================================
//  MYOSA Node Firmware — BLE Manager Implementation
//  File    : ble_manager.cpp
// ============================================================
#include "ble_manager.h"
#include "system_manager.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

namespace BleManager {

// ---- Private state ----
static bool            s_bleOk          = false;
static bool            s_suspended      = false;
static BLEServer*      s_pServer        = nullptr;
static BLEAdvertising* s_pAdvertising   = nullptr;
static BLEScan*        s_pScan          = nullptr;

static uint32_t  s_lastScanMs    = 0;
static bool      s_scanRunning   = false;

// ---- Peer tracking (RSSI) ----
static const uint8_t MAX_BLE_PEERS = 10;
struct BlePeer {
    char    name[BLE_DEVICE_NAME_MAX];
    int8_t  rssi;
    uint32_t lastSeen;
};
static BlePeer s_peers[MAX_BLE_PEERS];
static uint8_t s_peerCount = 0;

// ---- Scan callback ----
class MyScanCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override {
        if (!dev.haveName()) return;
        const char* name = dev.getName().c_str();
        if (strncmp(name, MYOSA_BLE_PREFIX, strlen(MYOSA_BLE_PREFIX)) != 0) return;

        int8_t rssi = (int8_t)dev.getRSSI();
        uint32_t now = millis();
        DBG("BleManager: seen %s RSSI=%d", name, rssi);

        // Update or add peer record
        for (uint8_t i = 0; i < s_peerCount; i++) {
            if (strncmp(s_peers[i].name, name, BLE_DEVICE_NAME_MAX - 1) == 0) {
                s_peers[i].rssi     = rssi;
                s_peers[i].lastSeen = now;
                return;
            }
        }
        if (s_peerCount < MAX_BLE_PEERS) {
            strncpy(s_peers[s_peerCount].name, name, BLE_DEVICE_NAME_MAX - 1);
            s_peers[s_peerCount].name[BLE_DEVICE_NAME_MAX - 1] = '\0';
            s_peers[s_peerCount].rssi     = rssi;
            s_peers[s_peerCount].lastSeen = now;
            s_peerCount++;
        }
    }
};

static MyScanCallbacks s_scanCB;

// ---- Public API ----

bool init(const char* nodeId) {
    char bleName[BLE_DEVICE_NAME_MAX];
    snprintf(bleName, sizeof(bleName), "%s%s", MYOSA_BLE_PREFIX, nodeId + 4); // "MYOSA-" + last part of ID
    // If nodeId is "MYO-AABBCC", strip "MYO-" prefix → "MYOSA-AABBCC"
    // Rebuild properly:
    snprintf(bleName, sizeof(bleName), "MYOSA-%s", nodeId + 4); // nodeId+4 skips "MYO-"

    BLEDevice::init(bleName);
    s_pServer      = BLEDevice::createServer();
    s_pAdvertising = BLEDevice::getAdvertising();

    // Add custom service UUID to advertisement
    s_pAdvertising->addServiceUUID(MYOSA_SERVICE_UUID);
    s_pAdvertising->setScanResponse(true);
    s_pAdvertising->setMinPreferred(0x06);
    s_pAdvertising->setMinPreferred(0x12);
    s_pAdvertising->start();

    // Configure scanner
    s_pScan = BLEDevice::getScan();
    s_pScan->setAdvertisedDeviceCallbacks(&s_scanCB, false);
    s_pScan->setActiveScan(true);
    s_pScan->setInterval(100);
    s_pScan->setWindow(99);

    s_bleOk    = true;
    s_lastScanMs = millis();
    DBG("BleManager: advertising as '%s'", bleName);
    return true;
}

static void _onScanComplete(BLEScanResults results) {
    s_scanRunning = false;
    DBG("BleManager: async scan done, MYOSA peers=%d", s_peerCount);
}

void update() {
    if (!s_bleOk || s_suspended) return;

    // Evict stale peers (older than 2× scan interval)
    uint32_t now = millis();
    for (uint8_t i = 0; i < s_peerCount; ) {
        if ((now - s_peers[i].lastSeen) > (BLE_SCAN_INTERVAL_MS * 2)) {
            // Compact array
            for (uint8_t j = i; j < s_peerCount - 1; j++) {
                s_peers[j] = s_peers[j + 1];
            }
            s_peerCount--;
        } else {
            i++;
        }
    }

    // Start a new non-blocking scan if interval has elapsed and none is running
    if (!s_scanRunning && (now - s_lastScanMs) >= BLE_SCAN_INTERVAL_MS) {
        DBG("BleManager: starting async BLE scan (%ds)...", BLE_SCAN_DURATION_S);
        s_pScan->clearResults();
        s_scanRunning = true;
        s_lastScanMs  = now;
        s_pScan->start(BLE_SCAN_DURATION_S, _onScanComplete, false);
    }
}

bool isBleOk() {
    return s_bleOk && !s_suspended;
}

bool myosaPeerSeen() {
    if (!s_bleOk) return false;
    uint32_t now = millis();
    for (uint8_t i = 0; i < s_peerCount; i++) {
        if ((now - s_peers[i].lastSeen) < (BLE_SCAN_INTERVAL_MS * 3)) {
            return true;
        }
    }
    return false;
}

int8_t nearestPeerRssi() {
    if (!s_bleOk || s_peerCount == 0) return 0;
    int8_t best = -127;
    for (uint8_t i = 0; i < s_peerCount; i++) {
        if (s_peers[i].rssi > best) best = s_peers[i].rssi;
    }
    return best;
}

void suspend() {
    if (!s_bleOk || s_suspended) return;
    if (s_pAdvertising) s_pAdvertising->stop();
    if (s_pScan && s_pScan->isScanning()) s_pScan->stop();
    s_suspended   = true;
    s_scanRunning = false;
    DBG("BleManager: suspended");
}

void resume() {
    if (!s_bleOk || !s_suspended) return;
    if (s_pAdvertising) s_pAdvertising->start();
    s_suspended  = false;
    s_lastScanMs = millis();
    DBG("BleManager: resumed");
}

} // namespace BleManager
