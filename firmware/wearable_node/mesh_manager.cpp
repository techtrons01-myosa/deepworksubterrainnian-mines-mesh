// ============================================================
//  MYOSA Node Firmware — Mesh Manager Implementation
//  File    : mesh_manager.cpp
// ============================================================
#include "mesh_manager.h"
#include "system_manager.h"
#include "alert_manager.h"
#include "display_manager.h"
#include "telemetry_manager.h"
#include "sensor_manager.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>     // esp_wifi_set_channel()

// ---- Forward declarations ----
// CRC-8 helper defined in system_manager.cpp
extern uint8_t myosa_crc8(const uint8_t* data, size_t len);

namespace MeshManager {

// ---- Private state ----
static bool       s_espNowOk        = false;
static bool       s_suspended       = false;
static uint16_t   s_txSeq           = 0;

// ---- Sub-surface Ground Datum & Evacuation State ----
static float      s_groundDatum     = 25.0f;
static bool       s_evacuationActive = false;

// ---- Neighbour table ----
static NodeRecord s_neighbors[MAX_MESH_NODES];
static uint8_t    s_neighborCount = 0;

// ---- Heartbeat timer ----
static uint32_t s_lastHeartbeatMs = 0;

// ---- Watchdog timer ----
static uint32_t s_lastWatchdogMs      = 0;
static const uint32_t WATCHDOG_CHECK_MS = 2000;

// ---- Disconnect buzzer & Gateway sync timers ----
static uint32_t s_lastDisconnectBuzzMs = 0;
static uint32_t s_lastGatewaySyncMs    = 0;
static bool     s_hadNeighbors         = false;  // true after first node seen

// ---- Self MAC (stored during init) ----
static uint8_t s_selfMac[6] = {0};

// ---- Broadcast MAC ----
static const uint8_t BROADCAST_MAC[6] = ESPNOW_BROADCAST_ADDR;

// ---- Thread-safe receive queue ----
// ESP-NOW callback fires on the WiFi task (Core 0); loop() runs on Core 1.
// The callback must NEVER call flash-resident functions (DisplayManager,
// AlertManager, Serial.printf, etc.) — doing so causes cache-fault crashes
// when the flash cache is briefly locked during WiFi operations.
// Solution: callback just memcpy()s the raw packet into a lock-free ring
// buffer. update() drains the buffer on Core 1 where everything is safe.
struct RxEntry {
    uint8_t srcMac[6];
    int8_t  rssi;
    uint8_t raw[sizeof(MyosaPacket)];
    bool    valid;
};
static const uint8_t    RX_QUEUE_SIZE = 6;
static RxEntry          s_rxBuf[RX_QUEUE_SIZE];
static volatile uint8_t s_rxHead = 0;   // written by WiFi task
static volatile uint8_t s_rxTail = 0;   // consumed by loop()

// ---- Periodic re-discovery timer ----
static uint32_t s_lastDiscoveryMs = 0;
static const uint32_t DISCOVERY_INTERVAL_MS = 30000UL;  // re-broadcast every 30s

// ---- Packet building ----
static MyosaPacket s_txPkt;

static void _buildPacket(PacketType type, const uint8_t* payload, uint8_t len) {
    memset(&s_txPkt, 0, sizeof(s_txPkt));
    s_txPkt.magic      = PACKET_MAGIC;
    memcpy(s_txPkt.senderMac, s_selfMac, 6);
    s_txPkt.packetType = (uint8_t)type;
    s_txPkt.timestamp  = millis();
    s_txPkt.sequence   = s_txSeq++;
    s_txPkt.hopCount   = 3;
    s_txPkt.payloadLen = len;
    if (payload && len > 0) {
        memcpy(s_txPkt.payload, payload, len);
    }
    // CRC covers everything before the CRC byte
    size_t crcLen = sizeof(MyosaPacket) - 1;
    s_txPkt.crc8 = myosa_crc8((const uint8_t*)&s_txPkt, crcLen);
}

// ---- Neighbour helpers ----
static NodeRecord* _findOrCreatePeer(const uint8_t* mac) {
    for (uint8_t i = 0; i < s_neighborCount; i++) {
        if (memcmp(s_neighbors[i].mac, mac, 6) == 0) return &s_neighbors[i];
    }
    if (s_neighborCount >= MAX_MESH_NODES) return nullptr;
    NodeRecord* rec = &s_neighbors[s_neighborCount++];
    memset(rec, 0, sizeof(NodeRecord));
    memcpy(rec->mac, mac, 6);
    rec->status     = NodeStatus::UNKNOWN;
    rec->espnowKnown = true;
    return rec;
}

static void _registerEspNowPeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) return;
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
}

// ---- Receive callback — Core 0 WiFi task ----
// CRITICAL: This runs on a different core from loop(). It must NOT call any
// flash-resident code. Only memcpy into the ring buffer, then return immediately.
static void _onReceive(const esp_now_recv_info_t* recv_info,
                        const uint8_t* data, int len) {
    if (len < (int)(sizeof(MyosaPacket) - ESPNOW_MAX_PAYLOAD)) return;

    uint8_t next = (s_rxHead + 1) % RX_QUEUE_SIZE;
    if (next == s_rxTail) return;  // queue full — drop packet

    RxEntry& e = s_rxBuf[s_rxHead];
    memcpy(e.srcMac, recv_info->src_addr, 6);
    e.rssi = (recv_info->rx_ctrl != nullptr) ? (int8_t)recv_info->rx_ctrl->rssi : (int8_t)-60;
    memset(e.raw, 0, sizeof(MyosaPacket));
    memcpy(e.raw, data, (len > (int)sizeof(MyosaPacket)) ? sizeof(MyosaPacket) : len);
    e.valid  = true;
    s_rxHead = next;   // publish atomically (single-byte write is atomic on Xtensa)
}

// ---- Packet processing — called from loop() on Core 1 ----
// All heavy logic runs here: DisplayManager, AlertManager, DBG are safe.
static void _processPkt(const uint8_t* mac, int8_t rssi, const uint8_t* raw) {
    const MyosaPacket* pkt = reinterpret_cast<const MyosaPacket*>(raw);

    // Validate magic
    if (pkt->magic != PACKET_MAGIC) {
        SystemManager::registerFault(FAULT_BAD_PACKET);
        DBG("MeshManager: bad magic 0x%04X from %02X:%02X:%02X:%02X:%02X:%02X",
            pkt->magic, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return;
    }

    // Validate CRC
    uint8_t expectedCrc = myosa_crc8(raw, sizeof(MyosaPacket) - 1);
    if (pkt->crc8 != expectedCrc) {
        // Allow CONFIG_SYNC and ALERT through even if minor CRC padding difference
        if (pkt->packetType != (uint8_t)PacketType::CONFIG_SYNC && pkt->packetType != (uint8_t)PacketType::ALERT) {
            SystemManager::registerFault(FAULT_BAD_PACKET);
            DBG("MeshManager: CRC FAIL from %02X:%02X:%02X:%02X:%02X:%02X (got 0x%02X exp 0x%02X)",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                pkt->crc8, expectedCrc);
            return;
        }
    }

    // Ignore own packets
    if (memcmp(pkt->senderMac, s_selfMac, 6) == 0) return;

    // Find or create neighbour record
    NodeRecord* peer = _findOrCreatePeer(mac);
    if (!peer) return;
    peer->lastSeen    = millis();
    peer->espnowKnown = true;
    peer->espnowRssi  = rssi;

    PacketType type = (PacketType)pkt->packetType;
    DBG("MeshManager: RX type=0x%02X seq=%d from %02X%02X",
        pkt->packetType, pkt->sequence, mac[4], mac[5]);

    switch (type) {
        case PacketType::HEARTBEAT: {
            if (pkt->payloadLen < sizeof(HeartbeatPayload)) break;
            const HeartbeatPayload* hb =
                reinterpret_cast<const HeartbeatPayload*>(pkt->payload);
            strncpy(peer->nodeId, hb->nodeId, NODE_ID_MAX_LEN - 1);
            peer->neighborCount = hb->neighborCount;
            bool wasLost = (peer->status == NodeStatus::LOST ||
                            peer->status == NodeStatus::UNKNOWN);
            peer->status = NodeStatus::ACTIVE;
            _registerEspNowPeer(mac);
            if (wasLost) {
                char msg[40];
                snprintf(msg, sizeof(msg), "NODE %s JOINED", peer->nodeId);
                DisplayManager::pushAlert(msg);
                AlertManager::play(BuzzerPattern::NODE_DISCOVERED);
                DBG("MeshManager: peer %s ACTIVE (re-joined)", peer->nodeId);
            }
            break;
        }
        case PacketType::NODE_DISCOVERY: {
            if (pkt->payloadLen < sizeof(DiscoveryPayload)) break;
            const DiscoveryPayload* disc =
                reinterpret_cast<const DiscoveryPayload*>(pkt->payload);
            strncpy(peer->nodeId, disc->nodeId, NODE_ID_MAX_LEN - 1);
            bool wasLost = (peer->status == NodeStatus::LOST ||
                            peer->status == NodeStatus::UNKNOWN);
            peer->status = NodeStatus::ACTIVE;
            _registerEspNowPeer(mac);
            if (wasLost || strlen(peer->nodeId) == 0) {
                char msg[40];
                snprintf(msg, sizeof(msg), "NODE %s JOINED", peer->nodeId);
                DisplayManager::pushAlert(msg);
                AlertManager::play(BuzzerPattern::NODE_DISCOVERED);
            }
            DBG("MeshManager: discovered %s", disc->nodeId);
            break;
        }
        case PacketType::ALERT: {
            if (pkt->payloadLen < sizeof(AlertPayload)) break;
            const AlertPayload* alert =
                reinterpret_cast<const AlertPayload*>(pkt->payload);
            DisplayManager::pushAlert(alert->message);
            if (alert->alertCode == (uint8_t)BuzzerPattern::EVACUATION || strstr(alert->message, "EVACUAT")) {
                float currentAlt = SensorManager::getSensorData().altitude;
                // Only activate evacuation if underground
                if (currentAlt < (s_groundDatum - 0.5f)) {
                    s_evacuationActive = true;
                    AlertManager::play(BuzzerPattern::EVACUATION);
                } else {
                    DisplayManager::pushAlert("SURFACE SAFE");
                }
            } else if (alert->alertCode == 0 || strstr(alert->message, "CLEAR") || strstr(alert->message, "SAFE")) {
                s_evacuationActive = false;
                AlertManager::silence();
            } else {
                AlertManager::play((BuzzerPattern)alert->alertCode);
            }
            break;
        }
        case PacketType::CONFIG_SYNC: {
            if (pkt->payloadLen < sizeof(ConfigSyncPayload)) break;
            const ConfigSyncPayload* sync =
                reinterpret_cast<const ConfigSyncPayload*>(pkt->payload);
            s_lastGatewaySyncMs = millis(); // Gateway is healthy and communicating
            if (sync->groundDatum > -100.0f && sync->groundDatum < 9000.0f) {
                s_groundDatum = sync->groundDatum;
                DBG("MeshManager: Ground datum synced from gateway: %.2f m", s_groundDatum);
            }
            if (sync->evacuateActive == 1) {
                float currentAlt = SensorManager::getSensorData().altitude;
                if (currentAlt < (s_groundDatum - 0.5f)) {
                    s_evacuationActive = true;
                    DisplayManager::pushAlert("! EVACUATE NOW !");
                    AlertManager::play(BuzzerPattern::EVACUATION);
                }
            } else {
                if (s_evacuationActive) {
                    s_evacuationActive = false;
                    DisplayManager::pushAlert("ALL CLEAR");
                    AlertManager::silence();
                }
            }
            break;
        }
        case PacketType::SLEEP: {
            if (peer->status != NodeStatus::SLEEPING) {
                peer->status = NodeStatus::SLEEPING;
                DBG("MeshManager: peer %s SLEEPING", peer->nodeId);
            }
            break;
        }
        case PacketType::WAKE: {
            peer->status = NodeStatus::ACTIVE;
            DBG("MeshManager: peer %s WOKE", peer->nodeId);
            break;
        }
        default:
            break;
    }
}


static void _onSent(const wifi_tx_info_t* tx_info, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        DBG("MeshManager: TX FAIL (status=%d)", (int)status);
    }
}

// ---- Heartbeat payload builder ----
static void _sendHeartbeat() {
    HeartbeatPayload hb = {};
    hb.espnowOk      = s_espNowOk ? 1 : 0;
    hb.bleOk         = 1;
    hb.wifiOn        = 0;
    hb.neighborCount = MeshManager::getNeighborCount();   // active only
    hb.sensorStatus  = 0x07;
    hb.batteryPct    = 100;
    hb.faultMask     = SystemManager::getFaultMask();
    strncpy(hb.nodeId, SystemManager::getNodeId(), NODE_ID_MAX_LEN - 1);

    broadcast(PacketType::HEARTBEAT, (const uint8_t*)&hb, sizeof(hb));
    DBG("MeshManager: heartbeat TX (active_neighbors=%d)", MeshManager::getNeighborCount());
}

// ---- Node-loss watchdog ----
static void _checkNodeLoss() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < s_neighborCount; i++) {
        NodeRecord& rec = s_neighbors[i];
        if (rec.status == NodeStatus::ACTIVE || rec.status == NodeStatus::UNKNOWN) {
            if ((now - rec.lastSeen) > NODE_LOSS_TIMEOUT_MS) {
                rec.status = NodeStatus::LOST;
                char msg[40];
                if (strlen(rec.nodeId) > 0) {
                    snprintf(msg, sizeof(msg), "NODE %s LOST", rec.nodeId);
                } else {
                    snprintf(msg, sizeof(msg), "NODE %02X%02X LOST", rec.mac[4], rec.mac[5]);
                }
                DBG("MeshManager: %s", msg);
                DisplayManager::pushAlert(msg);
                AlertManager::play(BuzzerPattern::NODE_LOST);

                // Propagate the loss alert through the mesh
                AlertPayload ap = {};
                strncpy(ap.originId, SystemManager::getNodeId(), NODE_ID_MAX_LEN - 1);
                strncpy(ap.subjectId, rec.nodeId, NODE_ID_MAX_LEN - 1);
                ap.alertCode = (uint8_t)BuzzerPattern::NODE_LOST;
                strncpy(ap.message, msg, sizeof(ap.message) - 1);
                broadcast(PacketType::ALERT, (const uint8_t*)&ap, sizeof(ap));
            }
        }
    }
}

// ---- Public API ----

bool init(const uint8_t* selfMac) {
    memcpy(s_selfMac, selfMac, 6);
    s_neighborCount = 0;
    memset(s_neighbors, 0, sizeof(s_neighbors));

    // WiFi must be in STA mode (no AP connection needed for ESP-NOW)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Force both boards onto the same physical RF channel.
    // Without this, boards in STA mode without AP may use different channels
    // and ESP-NOW broadcasts will never be received.
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    DBG("MeshManager: WiFi locked to channel %d", ESPNOW_CHANNEL);

    if (esp_now_init() != ESP_OK) {
        SystemManager::registerFault(FAULT_ESPNOW_INIT);
        DBG("MeshManager: ESP-NOW INIT FAIL");
        return false;
    }

    esp_now_register_recv_cb(_onReceive);
    esp_now_register_send_cb(_onSent);

    // Register broadcast peer
    _registerEspNowPeer(BROADCAST_MAC);

    s_espNowOk         = true;
    s_lastHeartbeatMs  = 0;  // Force immediate first heartbeat
    s_lastWatchdogMs   = millis();
    DBG("MeshManager: ESP-NOW OK  channel=%d", ESPNOW_CHANNEL);

    // Announce ourselves
    DiscoveryPayload disc = {};
    strncpy(disc.nodeId, SystemManager::getNodeId(), NODE_ID_MAX_LEN - 1);
    memcpy(disc.mac, selfMac, 6);
    disc.neighborCount = 0;
    disc.capabilities  = 0x07;  // BMP+MPU+APDS
    broadcast(PacketType::NODE_DISCOVERY, (const uint8_t*)&disc, sizeof(disc));

    return true;
}

void update() {
    if (!s_espNowOk || s_suspended) return;

    uint32_t now = millis();

    // ---- Drain the receive queue (packets copied by WiFi-task callback) ----
    // All processing happens here on Core 1 where flash is always accessible.
    while (s_rxTail != s_rxHead) {
        const RxEntry& e = s_rxBuf[s_rxTail];
        _processPkt(e.srcMac, e.rssi, e.raw);
        s_rxTail = (s_rxTail + 1) % RX_QUEUE_SIZE;
    }

    // ---- Heartbeat ----
    if ((now - s_lastHeartbeatMs) >= HEARTBEAT_INTERVAL_MS) {
        s_lastHeartbeatMs = now;
        _sendHeartbeat();
    }

    // ---- Periodic NODE_DISCOVERY re-broadcast ----
    // Allows nodes that reboot to re-join without needing a full power cycle
    // on the main node. Both sides hear the discovery and re-register.
    if ((now - s_lastDiscoveryMs) >= DISCOVERY_INTERVAL_MS) {
        s_lastDiscoveryMs = now;
        DiscoveryPayload disc = {};
        strncpy(disc.nodeId, SystemManager::getNodeId(), NODE_ID_MAX_LEN - 1);
        memcpy(disc.mac, s_selfMac, 6);
        disc.neighborCount = MeshManager::getNeighborCount();
        disc.capabilities  = 0x07;
        broadcast(PacketType::NODE_DISCOVERY, (const uint8_t*)&disc, sizeof(disc));
        DBG("MeshManager: periodic NODE_DISCOVERY TX");
    }

    // ---- Watchdog ----
    if ((now - s_lastWatchdogMs) >= WATCHDOG_CHECK_MS) {
        s_lastWatchdogMs = now;
        _checkNodeLoss();
    }

    // ---- Evacuation Management (Auto-clears if at/above ground level) ----
    float currentAlt = SensorManager::getSensorData().altitude;
    bool isAboveGround = (currentAlt >= (s_groundDatum - 0.5f));

    if (s_evacuationActive && isAboveGround) {
        s_evacuationActive = false;
        AlertManager::silence();
        DisplayManager::pushAlert("SURFACE SAFE");
        DBG("MeshManager: Evacuation auto-cleared (miner at surface %.1fm >= datum %.1fm)",
            currentAlt, s_groundDatum);
    }

    // Siren bursts spaced by 4 seconds (only when underground)
    if (s_evacuationActive && !isAboveGround) {
        static uint32_t s_lastEvacMs = 0;
        if ((now - s_lastEvacMs) >= 4000UL) {
            AlertManager::play(BuzzerPattern::EVACUATION);
            s_lastEvacMs = now;
        }
    }

    // ---- Continuous DISCONNECTED buzzer ----
    // Fires while we have had at least one node and all are now LOST.
    // Replays every DISCONNECT_BUZZ_INTERVAL_MS (5s ON + 2s OFF).
    uint8_t active = 0;
    for (uint8_t i = 0; i < s_neighborCount; i++) {
        if (s_neighbors[i].status == NodeStatus::ACTIVE) active++;
    }
    if (s_neighborCount > 0) s_hadNeighbors = true;

    // Healthy if peer nodes are active OR Gateway is communicating downlinks
    bool isNetworkConnected = (active > 0) || ((now - s_lastGatewaySyncMs) < 8000UL);

    if (s_hadNeighbors && !isNetworkConnected && !s_evacuationActive) {
        // All nodes & gateway gone — play DISCONNECTED repeatedly (5s on, 2s off)
        if ((now - s_lastDisconnectBuzzMs) >= DISCONNECT_BUZZ_INTERVAL_MS) {
            AlertManager::play(BuzzerPattern::DISCONNECTED);
            s_lastDisconnectBuzzMs = now;
            DBG("MeshManager: DISCONNECTED — no active peers or gateway");
        }
    } else {
        // Connected — reset timer
        s_lastDisconnectBuzzMs = now - DISCONNECT_BUZZ_INTERVAL_MS;
    }
}

bool broadcast(PacketType type, const uint8_t* payload, uint8_t len) {
    if (!s_espNowOk) return false;
    _buildPacket(type, payload, len);
    return esp_now_send(BROADCAST_MAC, (uint8_t*)&s_txPkt, sizeof(MyosaPacket)) == ESP_OK;
}

bool sendTo(const uint8_t* mac, PacketType type, const uint8_t* payload, uint8_t len) {
    if (!s_espNowOk) return false;
    _registerEspNowPeer(mac);
    _buildPacket(type, payload, len);
    return esp_now_send(mac, (uint8_t*)&s_txPkt, sizeof(MyosaPacket)) == ESP_OK;
}

uint8_t getNeighborCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < s_neighborCount; i++) {
        if (s_neighbors[i].status == NodeStatus::ACTIVE) count++;
    }
    return count;
}

const NodeRecord* getNeighbors() {
    return s_neighbors;
}

bool isPeerActive(const uint8_t* mac) {
    for (uint8_t i = 0; i < s_neighborCount; i++) {
        if (memcmp(s_neighbors[i].mac, mac, 6) == 0) {
            return s_neighbors[i].status == NodeStatus::ACTIVE;
        }
    }
    return false;
}

bool isEspNowOk() {
    return s_espNowOk && !s_suspended;
}

const char* getEspNowStatusStr() {
    if (!s_espNowOk)  return "FAIL";
    if (s_suspended)  return "SLEEP";
    return "OK";
}

uint16_t nextSequence() {
    return s_txSeq++;
}

void suspend() {
    if (!s_espNowOk || s_suspended) return;
    // Send SLEEP notification before going offline
    broadcast(PacketType::SLEEP, nullptr, 0);
    delay(50);
    esp_now_deinit();
    s_suspended = true;
    DBG("MeshManager: suspended");
}

void resume(const uint8_t* selfMac) {
    if (!s_suspended) return;
    s_suspended = false;
    s_espNowOk  = false;
    init(selfMac);
    broadcast(PacketType::WAKE, nullptr, 0);
    DBG("MeshManager: resumed");
}

float getGroundDatum() {
    return s_groundDatum;
}

bool isEvacuationActive() {
    return s_evacuationActive;
}

void cancelEvacuation() {
    s_evacuationActive = false;
    AlertManager::silence();
    DisplayManager::pushAlert("ALL CLEAR");
}

} // namespace MeshManager
