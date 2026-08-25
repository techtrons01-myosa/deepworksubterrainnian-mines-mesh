// ============================================================
//  MYOSA Mesh Gateway Firmware — ESP32-S3 Zero
//  Board   : Waveshare ESP32-S3-Zero / Generic ESP32-S3
//  Port    : COM7
//  Purpose : Transparent bridge between the MYOSA ESP-NOW mesh
//            and DeepWorks PC monitoring software.
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ------------------------------------------------------------
// Constants & Definitions
// ------------------------------------------------------------
#define ESPNOW_CHANNEL        1
#define PACKET_MAGIC          0x4D59u     // "MY"
#define ESPNOW_MAX_PAYLOAD   200
#define NODE_ID_MAX_LEN       12

// Downlink Packet Types
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

// Wire Envelope (Fixed 217 bytes)
struct __attribute__((packed)) MyosaPacket {
    uint16_t magic;
    uint8_t  senderMac[6];
    uint8_t  packetType;
    uint32_t timestamp;
    uint16_t sequence;
    uint8_t  hopCount;
    uint8_t  payloadLen;
    uint8_t  payload[ESPNOW_MAX_PAYLOAD];
    uint8_t  crc8;
};

// Typed Payloads
struct __attribute__((packed)) HeartbeatPayload {
    uint8_t  espnowOk;
    uint8_t  bleOk;
    uint8_t  wifiOn;
    uint8_t  neighborCount;
    uint8_t  sensorStatus;
    uint8_t  batteryPct;
    uint16_t faultMask;
    char     nodeId[NODE_ID_MAX_LEN];
};

struct __attribute__((packed)) SensorPayload {
    float    temperature;
    float    pressure;
    float    altitude;
    float    accelX, accelY, accelZ;
    float    gyroX,  gyroY,  gyroZ;
    uint8_t  proximity;
    uint16_t ambientLight;
};

struct __attribute__((packed)) AlertPayload {
    char     originId[NODE_ID_MAX_LEN];
    char     subjectId[NODE_ID_MAX_LEN];
    uint8_t  alertCode;
    char     message[40];
};

struct __attribute__((packed)) ConfigSyncPayload {
    float    groundDatum;       // Ground level datum (meters AMSL)
    uint32_t serverTimestamp;   // Epoch / sync time
    uint8_t  evacuateActive;    // 1 = Emergency Evacuation Active
};

// Broadcast address
static const uint8_t ESPNOW_BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// Node cache record
struct CachedNode {
    char     nodeId[NODE_ID_MAX_LEN];
    uint8_t  mac[6];
    int8_t   rssi;
    float    temperature;
    float    pressure;
    float    altitude;
    float    accelX, accelY, accelZ;
    float    gyroX, gyroY, gyroZ;
    uint8_t  proximity;
    uint16_t ambientLight;
    uint8_t  batteryPct;
    uint16_t faultMask;
    uint32_t lastSeen;
    bool     active;
};

#define MAX_NODES 32
static CachedNode s_nodes[MAX_NODES];
static uint8_t    s_nodeCount = 0;
static float      s_currentGroundDatum = 25.0f; // Default datum (25m)
static bool       s_evacuationActive   = false; // Emergency evacuation state

// Standard MYOSA CRC-8 (Polynomial 0x31) — EXACT MATCH WITH SYSTEM_MANAGER.CPP!
static uint8_t _myosa_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    while (len--) {
        uint8_t byte = *data++;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if ((crc ^ byte) & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static CachedNode* _getOrCreateNode(const uint8_t* mac, const char* nodeId) {
    for (uint8_t i = 0; i < s_nodeCount; i++) {
        if (memcmp(s_nodes[i].mac, mac, 6) == 0) {
            if (nodeId && strlen(nodeId) > 0) {
                strncpy(s_nodes[i].nodeId, nodeId, NODE_ID_MAX_LEN - 1);
            }
            return &s_nodes[i];
        }
    }
    if (s_nodeCount < MAX_NODES) {
        CachedNode* n = &s_nodes[s_nodeCount++];
        memset(n, 0, sizeof(CachedNode));
        memcpy(n->mac, mac, 6);
        if (nodeId && strlen(nodeId) > 0) {
            strncpy(n->nodeId, nodeId, NODE_ID_MAX_LEN - 1);
        } else {
            snprintf(n->nodeId, sizeof(n->nodeId), "MYO-%02X%02X%02X", mac[3], mac[4], mac[5]);
        }
        n->accelZ = 1.0f;
        return n;
    }
    return &s_nodes[0];
}

// ------------------------------------------------------------
// ESP-NOW Broadcast Helper
// ------------------------------------------------------------
static void _broadcastDownlink(PacketType ptype, const uint8_t* payload, uint8_t len) {
    static uint16_t s_seq = 0;
    MyosaPacket pkt = {};
    memset(&pkt, 0, sizeof(MyosaPacket));
    pkt.magic = PACKET_MAGIC;
    WiFi.macAddress(pkt.senderMac);
    pkt.packetType = (uint8_t)ptype;
    pkt.timestamp  = millis();
    pkt.sequence   = ++s_seq;
    pkt.hopCount   = 5;
    pkt.payloadLen = len;
    if (payload && len > 0) {
        memcpy(pkt.payload, payload, len);
    }
    size_t crcLen = sizeof(MyosaPacket) - 1;
    pkt.crc8 = _myosa_crc8((const uint8_t*)&pkt, crcLen);

    esp_now_send(ESPNOW_BROADCAST, (const uint8_t*)&pkt, sizeof(MyosaPacket));
}

// ------------------------------------------------------------
// ESP-NOW Receive Callback
// ------------------------------------------------------------
static void _onEspNowRecv(const esp_now_recv_info_t* info, const uint8_t* incomingData, int len) {
    if (len < (int)(sizeof(MyosaPacket) - ESPNOW_MAX_PAYLOAD)) return;

    const MyosaPacket* pkt = (const MyosaPacket*)incomingData;
    if (pkt->magic != PACKET_MAGIC) return;



    int8_t rssi = info->rx_ctrl->rssi;
    uint32_t now = millis();

    CachedNode* node = _getOrCreateNode(pkt->senderMac, nullptr);
    node->rssi     = rssi;
    node->lastSeen = now;
    node->active   = true;

    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             node->mac[0], node->mac[1], node->mac[2],
             node->mac[3], node->mac[4], node->mac[5]);

    PacketType ptype = (PacketType)pkt->packetType;

    if (ptype == PacketType::HEARTBEAT && pkt->payloadLen >= sizeof(HeartbeatPayload)) {
        const HeartbeatPayload* hb = (const HeartbeatPayload*)pkt->payload;
        if (strlen(hb->nodeId) > 0) {
            strncpy(node->nodeId, hb->nodeId, NODE_ID_MAX_LEN - 1);
        }
        node->batteryPct = hb->batteryPct;
        node->faultMask  = hb->faultMask;
    }
    else if (ptype == PacketType::SENSOR_DATA && pkt->payloadLen >= sizeof(SensorPayload)) {
        const SensorPayload* sp = (const SensorPayload*)pkt->payload;
        node->temperature  = sp->temperature;
        node->pressure     = sp->pressure;
        node->altitude     = sp->altitude;
        node->accelX       = sp->accelX;
        node->accelY       = sp->accelY;
        node->accelZ       = sp->accelZ;
        node->gyroX        = sp->gyroX;
        node->gyroY        = sp->gyroY;
        node->gyroZ        = sp->gyroZ;
        node->proximity    = sp->proximity;
        node->ambientLight = sp->ambientLight;
    }
    else if (ptype == PacketType::ALERT && pkt->payloadLen >= sizeof(AlertPayload)) {
        const AlertPayload* ap = (const AlertPayload*)pkt->payload;
        Serial.printf("{\"type\":\"ALERT\",\"nodeId\":\"%s\",\"subjectId\":\"%s\",\"code\":%u,\"message\":\"%s\"}\n",
                      ap->originId, ap->subjectId, ap->alertCode, ap->message);
    }

    float relativeDepth = s_currentGroundDatum - node->altitude;

    uint32_t uptimeSec = (now / 1000UL);
    uint8_t linkCount = (node->proximity > 0 || node->rssi > -90) ? 1 : 1;

    Serial.printf("{\"type\":\"TELEMETRY\",\"node_id\":\"%s\",\"mac\":\"%s\",\"temperature\":%.2f,\"pressure\":%.2f,\"altitude\":%.2f,\"relative_depth\":%.2f,\"accel\":{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f},\"gyro\":{\"x\":%.1f,\"y\":%.1f,\"z\":%.1f},\"rssi\":%d,\"proximity\":%u,\"ambientLight\":%u,\"batteryPct\":%u,\"faultMask\":%u,\"uptime\":%lu,\"links\":%u,\"status\":\"ONLINE\",\"timestamp\":%lu}\n",
                  node->nodeId, macStr,
                  node->temperature, node->pressure, node->altitude, relativeDepth,
                  node->accelX, node->accelY, node->accelZ,
                  node->gyroX, node->gyroY, node->gyroZ,
                  node->rssi, node->proximity, node->ambientLight,
                  node->batteryPct > 0 ? node->batteryPct : 100,
                  node->faultMask, uptimeSec, linkCount, now);
}

// ------------------------------------------------------------
// Setup & Loop
// ------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(200);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) {
        Serial.println("{\"type\":\"GATEWAY_STATUS\",\"status\":\"ERROR\",\"message\":\"ESP-NOW init failed\"}");
        return;
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, ESPNOW_BROADCAST, 6);
    peerInfo.channel = ESPNOW_CHANNEL;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    esp_now_register_recv_cb(_onEspNowRecv);

    Serial.println("{\"type\":\"GATEWAY_STATUS\",\"status\":\"READY\",\"channel\":1,\"message\":\"MYOSA ESP32-S3 Gateway Active\"}");
}

static String s_inputLine = "";

void loop() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (s_inputLine.length() > 0) {
                s_inputLine.trim();

                // SET_DATUM:XX.X
                if (s_inputLine.indexOf("SET_DATUM") >= 0) {
                    float newDatum = s_currentGroundDatum;
                    int idx = s_inputLine.indexOf(":");
                    if (idx >= 0) {
                        newDatum = s_inputLine.substring(idx + 1).toFloat();
                    }
                    if (newDatum > -100.0f && newDatum < 9000.0f) {
                        s_currentGroundDatum = newDatum;
                        ConfigSyncPayload sync = { s_currentGroundDatum, millis(), 0 };
                        for (int k = 0; k < 3; k++) {
                            _broadcastDownlink(PacketType::CONFIG_SYNC, (const uint8_t*)&sync, sizeof(sync));
                            delay(5);
                        }
                        Serial.printf("{\"type\":\"GATEWAY_ACK\",\"cmd\":\"SET_DATUM\",\"datum\":%.2f,\"status\":\"OK\"}\n", s_currentGroundDatum);
                    }
                }
                // CANCEL / CLEAR EVACUATION
                else if (s_inputLine.indexOf("CANCEL_EVACUATE") >= 0 || s_inputLine.indexOf("CLEAR_EVACUATE") >= 0) {
                    s_evacuationActive = false;
                    ConfigSyncPayload sync = { s_currentGroundDatum, millis(), 0 };
                    AlertPayload alert = {};
                    strncpy(alert.originId, "GATEWAY-S3", NODE_ID_MAX_LEN - 1);
                    strncpy(alert.subjectId, "ALL_NODES", NODE_ID_MAX_LEN - 1);
                    alert.alertCode = 0x00; // ALL CLEAR
                    strncpy(alert.message, "ALL CLEAR - SAFE", sizeof(alert.message) - 1);

                    for (int k = 0; k < 5; k++) {
                        _broadcastDownlink(PacketType::ALERT, (const uint8_t*)&alert, sizeof(alert));
                        _broadcastDownlink(PacketType::CONFIG_SYNC, (const uint8_t*)&sync, sizeof(sync));
                        delay(10);
                    }
                    Serial.println("{\"type\":\"GATEWAY_ACK\",\"cmd\":\"CANCEL_EVACUATE\",\"status\":\"ALL_CLEAR\"}");
                }
                // MUTE / SILENCE COMMAND: MUTE:MYO-XXXXXX or MUTE:ALL
                else if (s_inputLine.indexOf("MUTE") >= 0 || s_inputLine.indexOf("SILENCE") >= 0) {
                    String target = "ALL_NODES";
                    int idx = s_inputLine.indexOf(":");
                    if (idx >= 0) {
                        target = s_inputLine.substring(idx + 1);
                        target.trim();
                    }
                    AlertPayload alert = {};
                    strncpy(alert.originId, "GATEWAY-S3", NODE_ID_MAX_LEN - 1);
                    strncpy(alert.subjectId, target.c_str(), NODE_ID_MAX_LEN - 1);
                    alert.alertCode = 0x00; // SILENCE / MUTE
                    strncpy(alert.message, "MUTE", sizeof(alert.message) - 1);

                    for (int k = 0; k < 5; k++) {
                        _broadcastDownlink(PacketType::ALERT, (const uint8_t*)&alert, sizeof(alert));
                        delay(5);
                    }
                    Serial.printf("{\"type\":\"GATEWAY_ACK\",\"cmd\":\"MUTE\",\"target\":\"%s\",\"status\":\"SILENCED\"}\n", target.c_str());
                }
                // EVACUATE
                else if (s_inputLine.indexOf("EVACUATE") >= 0) {
                    s_evacuationActive = true;
                    AlertPayload alert = {};
                    strncpy(alert.originId, "GATEWAY-S3", NODE_ID_MAX_LEN - 1);
                    strncpy(alert.subjectId, "ALL_NODES", NODE_ID_MAX_LEN - 1);
                    alert.alertCode = 0x08;
                    strncpy(alert.message, "EMERGENCY EVACUATION NOW", sizeof(alert.message) - 1);

                    ConfigSyncPayload sync = { s_currentGroundDatum, millis(), 1 };
                    for (int k = 0; k < 3; k++) {
                        _broadcastDownlink(PacketType::ALERT, (const uint8_t*)&alert, sizeof(alert));
                        _broadcastDownlink(PacketType::CONFIG_SYNC, (const uint8_t*)&sync, sizeof(sync));
                        delay(5);
                    }

                    Serial.println("{\"type\":\"GATEWAY_ACK\",\"cmd\":\"EVACUATE\",\"status\":\"BROADCAST_SENT\"}");
                }
                // PING
                else if (s_inputLine.indexOf("PING") >= 0) {
                    Serial.printf("{\"type\":\"GATEWAY_STATUS\",\"status\":\"ONLINE\",\"nodes\":%u,\"datum\":%.2f}\n",
                                  s_nodeCount, s_currentGroundDatum);
                }

                s_inputLine = "";
            }
        } else {
            s_inputLine += c;
        }
    }

    // Periodic sync broadcast of ground datum & evacuation status every 2 seconds
    static uint32_t s_lastSyncMs = 0;
    if (millis() - s_lastSyncMs >= 2000UL) {
        s_lastSyncMs = millis();
        ConfigSyncPayload sync = { s_currentGroundDatum, millis(), (uint8_t)(s_evacuationActive ? 1 : 0) };
        _broadcastDownlink(PacketType::CONFIG_SYNC, (const uint8_t*)&sync, sizeof(sync));
    }
}







