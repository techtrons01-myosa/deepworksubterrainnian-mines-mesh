// ============================================================
//  MYOSA Node Firmware — Display Manager Implementation
//  File    : display_manager.cpp
// ============================================================
#include "display_manager.h"
#include "sensor_manager.h"
#include "system_manager.h"
#include "input_manager.h"    // HW-763 SHORT_PRESS → next screen
#include "alert_manager.h"

// Forward-declare MeshManager functions to avoid circular include.
// MeshManager must implement these.
namespace MeshManager {
    uint8_t           getNeighborCount();
    bool              isEspNowOk();
    const char*       getEspNowStatusStr();
    const NodeRecord* getNeighbors();   // for LOCATOR screen
    float             getGroundDatum();
    bool              isEvacuationActive();
    void              cancelEvacuation();
}
namespace BleManager {
    bool        isBleOk();
}
namespace WiFiManager {
    bool        isWifiOn();
}
// Note: SensorManager::popGesture() intentionally NOT used here.
// Navigation is HW-763 only (SHORT_PRESS = next screen).

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

namespace DisplayManager {

// ---- Display instance ----
static Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// ---- State ----
static MenuScreen s_screen      = MenuScreen::HOME;
static MenuScreen s_prevScreen   = MenuScreen::HOME;  // restored on displayOn()

static bool       s_displayOn   = false;
static uint32_t   s_lastRender  = 0;
static uint32_t   s_lastInput   = 0;
static const uint32_t RENDER_INTERVAL_MS = 250; // ~4 Hz

// ---- LOCATOR scroll state ----
static int8_t s_locatorScroll = 0;  // index of top-visible node

// ---- Alert log ----
static const uint8_t ALERT_LOG_SIZE = 4;
static char s_alertLog[ALERT_LOG_SIZE][33];  // 32 chars + NUL
static uint8_t s_alertHead = 0;
static uint8_t s_alertCount = 0;

// ---- Internal helpers ----

static void _clearLine(uint8_t y, uint8_t h = 10) {
    oled.fillRect(0, y, OLED_WIDTH, h, SSD1306_BLACK);
}

/** Draw centred text at a given Y pixel. */
static void _centreText(const char* str, uint8_t y) {
    int16_t x1, y1;
    uint16_t w, h;
    oled.getTextBounds(str, 0, y, &x1, &y1, &w, &h);
    oled.setCursor((OLED_WIDTH - w) / 2, y);
    oled.print(str);
}

/** Draw a thin top-bar with page indicator dots. */
static void _drawPageBar() {
    uint8_t total = (uint8_t)MenuScreen::SCREEN_COUNT;
    uint8_t cur   = (uint8_t)s_screen;
    uint8_t dotW  = 5;
    uint8_t gapW  = 3;
    uint8_t totalW = total * dotW + (total - 1) * gapW;
    uint8_t startX = (OLED_WIDTH - totalW) / 2;
    for (uint8_t i = 0; i < total; i++) {
        uint8_t x = startX + i * (dotW + gapW);
        if (i == cur) {
            oled.fillRect(x, 0, dotW, 3, SSD1306_WHITE);
        } else {
            oled.drawRect(x, 0, dotW, 3, SSD1306_WHITE);
        }
    }
}

// ---- Screen renderers ----

static void _renderHome() {
    const char* id = SystemManager::getNodeId();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    _centreText("MYOSA CORE", 6);
    oled.drawLine(0, 16, OLED_WIDTH, 16, SSD1306_WHITE);

    char buf[24];
    snprintf(buf, sizeof(buf), "NODE: %s", id);
    _centreText(buf, 20);

    if (SensorManager::isBuriedDetected()) {
        oled.fillRect(0, 31, OLED_WIDTH, 10, SSD1306_WHITE);
        oled.setTextColor(SSD1306_BLACK);
        _centreText("!! BURIED ALERT !!", 32);
        oled.setTextColor(SSD1306_WHITE);
    } else {
        bool fault = SystemManager::hasFault();
        snprintf(buf, sizeof(buf), "STATUS: %s", fault ? "FAULT" : "ONLINE");
        _centreText(buf, 32);
    }

    uint8_t nCount = MeshManager::getNeighborCount();
    snprintf(buf, sizeof(buf), "MESH: %s (%d %s)",
             MeshManager::isEspNowOk() ? "OK" : "OFF",
             nCount, nCount == 1 ? "peer" : "peers");
    _centreText(buf, 44);

    snprintf(buf, sizeof(buf), "UP: %lus  CH: %d", SystemManager::uptimeSeconds(), ESPNOW_CHANNEL);
    _centreText(buf, 55);
}

static void _renderTemperature() {
    const SensorData& d = SensorManager::getSensorData();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    _centreText("TEMPERATURE", 6);
    oled.drawLine(0, 16, OLED_WIDTH, 16, SSD1306_WHITE);

    char buf[24];
    float temp = d.bmpValid ? d.bmpTemperature : (d.mpuValid ? (d.mpuTemperature - 12.0f) : -99.0f);

    if (temp > -90.0f) {
        oled.setTextSize(2);
        snprintf(buf, sizeof(buf), "%.1f C", temp);
        _centreText(buf, 22);

        oled.setTextSize(1);
        if (temp > MINE_TEMP_MAX_C) {
            oled.fillRect(0, 42, OLED_WIDTH, 10, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
            _centreText("! HIGH HEAT ALERT !", 43);
            oled.setTextColor(SSD1306_WHITE);
        } else {
            snprintf(buf, sizeof(buf), "STATUS: SAFE (<%.0fC)", MINE_TEMP_MAX_C);
            _centreText(buf, 42);
        }
        _centreText("Sensor: BMP180 (0x77)", 54);
    } else {
        _centreText("-- C", 26);
        _centreText("Sensor Offline", 44);
    }
}

static void _renderPressureAlt() {
    const SensorData& d = SensorManager::getSensorData();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    _centreText("BAROMETER / DEPTH", 6);
    oled.drawLine(0, 16, OLED_WIDTH, 16, SSD1306_WHITE);

    char buf[24];
    if (d.bmpValid) {
        snprintf(buf, sizeof(buf), "PRES: %.1f hPa", d.pressure);
        oled.setCursor(4, 20); oled.print(buf);

        snprintf(buf, sizeof(buf), "ALT:  %.1f m AMSL", d.altitude);
        oled.setCursor(4, 31); oled.print(buf);

        float datum = MeshManager::getGroundDatum();
        float depthBelowGround = datum - d.altitude;  // Positive = distance UNDERGROUND

        if (fabsf(depthBelowGround) < 0.5f) {
            snprintf(buf, sizeof(buf), "DEPTH: 0.0m [SURF]");
        } else if (depthBelowGround > 0.0f) {
            snprintf(buf, sizeof(buf), "DEPTH: %.1fm [MINE]", depthBelowGround);
        } else {
            snprintf(buf, sizeof(buf), "ABOVE: +%.1fm [SURF]", -depthBelowGround);
        }
        oled.setCursor(4, 43); oled.print(buf);

        snprintf(buf, sizeof(buf), "GROUND: %.1fm (GW)", datum);
        oled.setCursor(4, 54); oled.print(buf);
    } else {
        _centreText("BMP180 OFFLINE", 30);
        _centreText("Check I2C (0x77)", 44);
    }
}

static void _renderAccelerometer() {
    const SensorData& d = SensorManager::getSensorData();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    _centreText("ACCELEROMETER", 6);
    oled.drawLine(0, 16, OLED_WIDTH, 16, SSD1306_WHITE);

    char buf[24];
    if (d.mpuValid) {
        snprintf(buf, sizeof(buf), "AX:%+.2f g", d.accelX);
        oled.setCursor(4, 20); oled.print(buf);

        snprintf(buf, sizeof(buf), "AY:%+.2f g", d.accelY);
        oled.setCursor(4, 31); oled.print(buf);

        snprintf(buf, sizeof(buf), "AZ:%+.2f g", d.accelZ);
        oled.setCursor(4, 42); oled.print(buf);

        float mag = sqrtf(d.accelX * d.accelX + d.accelY * d.accelY + d.accelZ * d.accelZ);
        snprintf(buf, sizeof(buf), "|A|: %.2f g", mag);
        oled.setCursor(4, 54); oled.print(buf);

        // Visual tilt indicator bar
        int8_t barX = (int8_t)(d.accelX * 25.0f);
        if (barX < -25) barX = -25;
        if (barX > 25)  barX = 25;
        oled.drawRect(80, 24, 44, 8, SSD1306_WHITE);
        oled.drawLine(102, 22, 102, 34, SSD1306_WHITE);
        oled.fillRect(102 + (barX < 0 ? barX : 0), 26, abs(barX), 4, SSD1306_WHITE);
    } else {
        _centreText("MPU6050 OFFLINE", 30);
        _centreText("Check I2C (0x69/0x68)", 44);
    }
}

static void _renderGyroscope() {
    const SensorData& d = SensorManager::getSensorData();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    _centreText("GYROSCOPE", 6);
    oled.drawLine(0, 16, OLED_WIDTH, 16, SSD1306_WHITE);

    char buf[24];
    if (d.mpuValid) {
        snprintf(buf, sizeof(buf), "GX: %+.1f dps", d.gyroX);
        oled.setCursor(4, 20); oled.print(buf);

        snprintf(buf, sizeof(buf), "GY: %+.1f dps", d.gyroY);
        oled.setCursor(4, 31); oled.print(buf);

        snprintf(buf, sizeof(buf), "GZ: %+.1f dps", d.gyroZ);
        oled.setCursor(4, 42); oled.print(buf);

        float gmag = sqrtf(d.gyroX * d.gyroX + d.gyroY * d.gyroY + d.gyroZ * d.gyroZ);
        snprintf(buf, sizeof(buf), "MOTION: %s (%.0f)", gmag > 15.0f ? "ACTIVE" : "REST", gmag);
        oled.setCursor(4, 54); oled.print(buf);
    } else {
        _centreText("MPU6050 OFFLINE", 30);
        _centreText("Check I2C (0x69/0x68)", 44);
    }
}

static void _renderLightColor() {
    const SensorData& d = SensorManager::getSensorData();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    _centreText("LIGHT & COLOR", 6);
    oled.drawLine(0, 16, OLED_WIDTH, 16, SSD1306_WHITE);

    char buf[24];
    if (d.apdsValid) {
        snprintf(buf, sizeof(buf), "AMBIENT: %u Lux", d.ambientLight);
        oled.setCursor(4, 20); oled.print(buf);

        // Light level bar
        uint8_t barW = (uint8_t)(((uint32_t)d.ambientLight * 118) / 3000);
        if (barW > 118) barW = 118;
        oled.drawRect(4, 32, 120, 6, SSD1306_WHITE);
        if (barW > 0) oled.fillRect(5, 33, barW, 4, SSD1306_WHITE);

        snprintf(buf, sizeof(buf), "RGB SENSOR: ACTIVE");
        oled.setCursor(4, 43); oled.print(buf);

        snprintf(buf, sizeof(buf), "APDS9960 (0x39)");
        oled.setCursor(4, 54); oled.print(buf);
    } else {
        _centreText("APDS9960 OFFLINE", 30);
        _centreText("Check I2C (0x39)", 44);
    }
}

static void _renderProximity() {
    const SensorData& d = SensorManager::getSensorData();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    _centreText("PROXIMITY", 6);
    oled.drawLine(0, 16, OLED_WIDTH, 16, SSD1306_WHITE);

    char buf[24];
    if (d.apdsValid) {
        snprintf(buf, sizeof(buf), "PROX: %u / 255", d.proximity);
        oled.setCursor(4, 20); oled.print(buf);

        // Distance bar
        uint8_t barW = (uint8_t)(((uint16_t)d.proximity * 118) / 255);
        oled.drawRect(4, 32, 120, 7, SSD1306_WHITE);
        if (barW > 0) oled.fillRect(5, 33, barW, 5, SSD1306_WHITE);

        if (SensorManager::isBuriedDetected()) {
            oled.fillRect(0, 44, OLED_WIDTH, 10, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
            _centreText("!! BURIED ALERT !!", 45);
            oled.setTextColor(SSD1306_WHITE);
        } else {
            _centreText("RUBBLE STATUS: CLEAR", 45);
        }
        _centreText("Gesture / Proximity IC", 56);
    } else {
        _centreText("APDS9960 OFFLINE", 30);
        _centreText("Check I2C (0x39)", 44);
    }
}

static void _renderNetwork() {
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    _centreText("NETWORK MESH", 6);
    oled.drawLine(0, 16, OLED_WIDTH, 16, SSD1306_WHITE);

    char buf[24];
    snprintf(buf, sizeof(buf), "ESP-NOW: CH %d [OK]", ESPNOW_CHANNEL);
    oled.setCursor(4, 20); oled.print(buf);

    snprintf(buf, sizeof(buf), "BLE:     %s", BleManager::isBleOk() ? "OK (ACTIVE)" : "FAIL");
    oled.setCursor(4, 31); oled.print(buf);

    snprintf(buf, sizeof(buf), "Wi-Fi:   %s", WiFiManager::isWifiOn() ? "CONNECTED" : "STANDBY");
    oled.setCursor(4, 42); oled.print(buf);

    uint8_t n = MeshManager::getNeighborCount();
    snprintf(buf, sizeof(buf), "NODES:   %d %s", n, n == 1 ? "PEER" : "PEERS");
    oled.setCursor(4, 53); oled.print(buf);
}

// ---- Locator Mode State ----
enum class LocatorMode : uint8_t {
    AUTO_NEAREST = 0,   // Automatically tracks nearest node (highest dBm)
    CYCLE_SELECT = 1,   // Single tap cycles through available nodes
    LOCKED       = 2,   // Locked onto a specifically selected node
};
static LocatorMode s_locatorMode = LocatorMode::AUTO_NEAREST;
static int8_t      s_cycleIdx    = 0;
static char        s_lockedNodeId[NODE_ID_MAX_LEN] = {0};
static uint8_t     s_lockedMac[6] = {0};

bool isLocatorCycleMode() {
    return (s_locatorMode == LocatorMode::CYCLE_SELECT);
}

static void _renderLocator() {
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    // Dynamic sweep radar indicator
    static uint8_t s_radarAnim = 0;
    s_radarAnim = (s_radarAnim + 1) % 4;
    char dotBuf[5] = "";
    if (s_radarAnim == 1) strcpy(dotBuf, ".");
    else if (s_radarAnim == 2) strcpy(dotBuf, "..");
    else if (s_radarAnim == 3) strcpy(dotBuf, "...");

    // Header based on mode
    char title[28];
    if (s_locatorMode == LocatorMode::AUTO_NEAREST) {
        snprintf(title, sizeof(title), "LOCATOR [AUTO]%s", dotBuf);
    } else if (s_locatorMode == LocatorMode::CYCLE_SELECT) {
        snprintf(title, sizeof(title), "LOCATOR [CYCLE]%s", dotBuf);
    } else {
        snprintf(title, sizeof(title), "LOCATOR [LOCKED]%s", dotBuf);
    }
    _centreText(title, 4);
    oled.drawLine(0, 14, OLED_WIDTH, 14, SSD1306_WHITE);

    const NodeRecord* nodes = MeshManager::getNeighbors();
    // Gather all active/known peers
    int8_t validIndices[MAX_MESH_NODES];
    uint8_t validCount = 0;
    for (uint8_t i = 0; i < MAX_MESH_NODES; i++) {
        if (nodes[i].status == NodeStatus::ACTIVE || nodes[i].status == NodeStatus::SLEEPING) {
            if (nodes[i].espnowKnown) {
                validIndices[validCount++] = i;
            }
        }
    }

    if (validCount == 0) {
        _centreText("Searching for nodes...", 28);
        _centreText("Continuous dB Loop Active", 42);
        _centreText("ESP-NOW CH 1 Broadcast", 54);
        return;
    }

    // Determine target peer based on mode
    int8_t targetIdx = -1;
    if (s_locatorMode == LocatorMode::AUTO_NEAREST) {
        int8_t bestRssi = -128;
        for (uint8_t k = 0; k < validCount; k++) {
            uint8_t idx = validIndices[k];
            if (nodes[idx].espnowRssi > bestRssi) {
                bestRssi  = nodes[idx].espnowRssi;
                targetIdx = idx;
            }
        }
    } else if (s_locatorMode == LocatorMode::CYCLE_SELECT) {
        if (s_cycleIdx >= (int8_t)validCount) s_cycleIdx = 0;
        if (s_cycleIdx < 0) s_cycleIdx = 0;
        targetIdx = validIndices[s_cycleIdx];
    } else { // LOCKED
        for (uint8_t k = 0; k < validCount; k++) {
            uint8_t idx = validIndices[k];
            if (memcmp(nodes[idx].mac, s_lockedMac, 6) == 0 ||
                (strlen(s_lockedNodeId) > 0 && strcmp(nodes[idx].nodeId, s_lockedNodeId) == 0)) {
                targetIdx = idx;
                break;
            }
        }
        if (targetIdx < 0) {
            // Locked node temporarily out of range
            targetIdx = validIndices[0];
        }
    }

    if (targetIdx < 0) targetIdx = validIndices[0];
    const NodeRecord& n = nodes[targetIdx];

    char buf[28];
    if (s_locatorMode == LocatorMode::CYCLE_SELECT) {
        snprintf(buf, sizeof(buf), "> %s (%d/%d) <",
                 strlen(n.nodeId) > 0 ? n.nodeId : "PEER",
                 s_cycleIdx + 1, validCount);
    } else if (s_locatorMode == LocatorMode::LOCKED) {
        snprintf(buf, sizeof(buf), "TARGET: %s [LOCK]", strlen(n.nodeId) > 0 ? n.nodeId : "PEER");
    } else {
        snprintf(buf, sizeof(buf), "TARGET: %s (%d nodes)", strlen(n.nodeId) > 0 ? n.nodeId : "PEER", validCount);
    }
    oled.setCursor(4, 18); oled.print(buf);

    // Large dBm reading
    oled.setTextSize(2);
    snprintf(buf, sizeof(buf), "%d dBm", n.espnowRssi);
    oled.setCursor(4, 30); oled.print(buf);

    // Signal Quality & Distance estimate
    oled.setTextSize(1);
    const char* qual = "WEAK";
    if (n.espnowRssi >= -60) qual = "EXCELLENT";
    else if (n.espnowRssi >= -75) qual = "STRONG";
    else if (n.espnowRssi >= -85) qual = "FAIR";

    float estDist = powf(10.0f, (float)(-45 - n.espnowRssi) / (10.0f * 2.2f));
    if (estDist > 50.0f) estDist = 50.0f;
    snprintf(buf, sizeof(buf), "%s  ~%.1fm", qual, estDist);
    oled.setCursor(4, 48); oled.print(buf);

    // Visual dynamic RSSI signal bar (10 segments)
    int8_t barCount = (int8_t)((n.espnowRssi + 95) / 5.5f);
    if (barCount < 0)  barCount = 0;
    if (barCount > 10) barCount = 10;
    oled.drawRect(88, 30, 36, 14, SSD1306_WHITE);
    if (barCount > 0) {
        uint8_t fillW = (uint8_t)((barCount * 32) / 10);
        oled.fillRect(90, 32, fillW, 10, SSD1306_WHITE);
    }

    // Bottom hint line
    if (s_locatorMode == LocatorMode::AUTO_NEAREST) {
        _centreText("3x-TAP: SELECT MODE", 57);
    } else if (s_locatorMode == LocatorMode::CYCLE_SELECT) {
        _centreText("TAP:NEXT | HOLD:LOCK", 57);
    } else {
        _centreText("3x-TAP: BACK TO AUTO", 57);
    }
}

static void _renderAlerts() {
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    _centreText("ALERT LOG", 4);
    oled.drawLine(0, 14, OLED_WIDTH, 14, SSD1306_WHITE);

    if (s_alertCount == 0) {
        _centreText("No active alerts", 28);
        _centreText("System normal", 42);
        char buf[24];
        snprintf(buf, sizeof(buf), "FAULT MASK: 0x%04X", SystemManager::getFaultMask());
        _centreText(buf, 55);
        return;
    }

    for (uint8_t i = 0; i < 3 && i < s_alertCount; i++) {
        int8_t idx = ((int8_t)s_alertHead - 1 - i + ALERT_LOG_SIZE) % ALERT_LOG_SIZE;
        oled.setCursor(2, 17 + i * 14);
        oled.print(s_alertLog[idx]);
    }

    char buf[24];
    snprintf(buf, sizeof(buf), "UP: %lus | F:0x%04X", SystemManager::uptimeSeconds(), SystemManager::getFaultMask());
    oled.setCursor(2, 56); oled.print(buf);
}

// ---- Public API ----

bool init() {
    if (!oled.begin(SSD1306_SWITCHCAPVCC, ADDR_SSD1306)) {
        SystemManager::registerFault(FAULT_OLED_INIT);
        DBG("DisplayManager: SSD1306 FAIL");
        return false;
    }
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.display();
    s_displayOn   = true;
    s_screen      = MenuScreen::HOME;
    s_locatorMode = LocatorMode::AUTO_NEAREST;
    s_cycleIdx    = 0;
    s_lastInput   = millis();
    DBG("DisplayManager: SSD1306 OK (10 screens + locator modes ready)");
    return true;
}

void update() {
    // ---- HW-763 Multi-Action Navigation ----

    // 1. TRIPLE_PRESS (used on Locator screen to toggle Auto vs Cycle vs Exit)
    if (InputManager::peekEvent() == TouchEvent::TRIPLE_PRESS) {
        InputManager::popEvent();

        if (!s_displayOn) {
            displayOn();
            AlertManager::play(BuzzerPattern::NODE_DISCOVERED);
        } else if (s_screen == MenuScreen::LOCATOR) {
            if (s_locatorMode == LocatorMode::AUTO_NEAREST) {
                s_locatorMode = LocatorMode::CYCLE_SELECT;
                s_cycleIdx    = 0;
                AlertManager::play(BuzzerPattern::NODE_DISCOVERED);
                DBG("DisplayManager: LOCATOR -> CYCLE_SELECT mode");
            } else {
                s_locatorMode = LocatorMode::AUTO_NEAREST;
                AlertManager::play(BuzzerPattern::NODE_DISCOVERED);
                DBG("DisplayManager: LOCATOR -> AUTO_NEAREST mode");
            }
        }
        s_lastInput = millis();
    }

    // 2. SHORT_PRESS (single tap)
    else if (InputManager::peekEvent() == TouchEvent::SHORT_PRESS) {
        InputManager::popEvent();

        if (MeshManager::isEvacuationActive()) {
            MeshManager::cancelEvacuation();
            AlertManager::silence();
            DisplayManager::pushAlert("EVAC ACKNOWLEDGED");
            DBG("DisplayManager: EVACUATION silenced via touch");
        } else if (!s_displayOn) {
            displayOn();
            AlertManager::play(BuzzerPattern::NODE_DISCOVERED);
        } else if (s_screen == MenuScreen::LOCATOR && s_locatorMode == LocatorMode::CYCLE_SELECT) {
            // Cycle to next node inside Locator (DO NOT change main screen)
            const NodeRecord* nodes = MeshManager::getNeighbors();
            uint8_t validCount = 0;
            for (uint8_t i = 0; i < MAX_MESH_NODES; i++) {
                if (nodes[i].status == NodeStatus::ACTIVE || nodes[i].status == NodeStatus::SLEEPING) {
                    if (nodes[i].espnowKnown) validCount++;
                }
            }
            if (validCount > 1) {
                s_cycleIdx = (s_cycleIdx + 1) % validCount;
            } else {
                s_cycleIdx = 0;
            }
            AlertManager::play(BuzzerPattern::NODE_DISCOVERED);
            DBG("DisplayManager: LOCATOR CYCLE -> node index %d (of %d)", s_cycleIdx, validCount);
        } else {
            // Advance to next menu screen
            uint8_t next = ((uint8_t)s_screen + 1) % (uint8_t)MenuScreen::SCREEN_COUNT;
            s_screen        = (MenuScreen)next;
            s_locatorScroll = 0;
            DBG("DisplayManager: SHORT_PRESS -> screen %d", (int)s_screen);
        }
        s_lastInput = millis();
    }

    // 3. LONG_PRESS (Lock in target if in Locator Cycle mode)
    else if (InputManager::peekEvent() == TouchEvent::LONG_PRESS) {
        if (MeshManager::isEvacuationActive()) {
            InputManager::popEvent();
            MeshManager::cancelEvacuation();
            AlertManager::silence();
        } else if (s_displayOn && s_screen == MenuScreen::LOCATOR && s_locatorMode == LocatorMode::CYCLE_SELECT) {
            InputManager::popEvent();
            // Lock in currently selected peer
            const NodeRecord* nodes = MeshManager::getNeighbors();
            int8_t validIndices[MAX_MESH_NODES];
            uint8_t validCount = 0;
            for (uint8_t i = 0; i < MAX_MESH_NODES; i++) {
                if (nodes[i].status == NodeStatus::ACTIVE || nodes[i].status == NodeStatus::SLEEPING) {
                    if (nodes[i].espnowKnown) validIndices[validCount++] = i;
                }
            }
            if (validCount > 0) {
                if (s_cycleIdx >= (int8_t)validCount) s_cycleIdx = 0;
                uint8_t chosen = validIndices[s_cycleIdx];
                memcpy(s_lockedMac, nodes[chosen].mac, 6);
                strncpy(s_lockedNodeId, nodes[chosen].nodeId, NODE_ID_MAX_LEN - 1);
                s_locatorMode = LocatorMode::LOCKED;
                AlertManager::play(BuzzerPattern::NODE_DISCOVERED);
                DBG("DisplayManager: LOCATOR LOCKED ON %s", s_lockedNodeId);
            }
            s_lastInput = millis();
        }
    }

    // In Emergency Evacuation mode, NEVER turn display off!
    if (MeshManager::isEvacuationActive()) {
        if (!s_displayOn) {
            displayOn();
        }
        s_lastInput = millis(); // Reset idle timer continuously
    }

    // Auto-sleep display after idle on ANY screen (only when NOT evacuating)
    if (s_displayOn && !MeshManager::isEvacuationActive() && (millis() - s_lastInput >= AUTO_SLEEP_TIMEOUT_MS)) {
        displayOff();
        DBG("DisplayManager: idle -> Auto-sleep OLED OFF");
    }

    if (!s_displayOn) return;

    // Render at ~4 Hz
    if ((millis() - s_lastRender) < RENDER_INTERVAL_MS) return;
    s_lastRender = millis();

    // Emergency Evacuation flashing overlay
    if (MeshManager::isEvacuationActive()) {
        static bool s_flash = false;
        s_flash = !s_flash;
        oled.clearDisplay();
        if (s_flash) {
            oled.fillRect(0, 0, OLED_WIDTH, OLED_HEIGHT, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
        } else {
            oled.setTextColor(SSD1306_WHITE);
        }
        oled.setTextSize(1);
        _centreText("====================", 4);
        _centreText("! EVACUATE NOW !", 16);
        _centreText("SUB-SURFACE ALARM", 30);
        _centreText("PROCEED TO SURFACE", 44);
        _centreText("====================", 56);
        oled.display();
        return;
    }

    oled.clearDisplay();
    _drawPageBar();

    switch (s_screen) {
        case MenuScreen::HOME:          _renderHome();          break;
        case MenuScreen::TEMPERATURE:   _renderTemperature();   break;
        case MenuScreen::PRESSURE_ALT:  _renderPressureAlt();   break;
        case MenuScreen::ACCELEROMETER: _renderAccelerometer(); break;
        case MenuScreen::GYROSCOPE:     _renderGyroscope();     break;
        case MenuScreen::LIGHT_RGB:     _renderLightColor();    break;
        case MenuScreen::PROXIMITY:     _renderProximity();     break;
        case MenuScreen::NETWORK:       _renderNetwork();       break;
        case MenuScreen::LOCATOR:       _renderLocator();       break;
        case MenuScreen::ALERTS:        _renderAlerts();        break;
        default: break;
    }

    oled.display();
}

void setScreen(MenuScreen screen) {
    s_screen    = screen;
    s_lastInput = millis();
}

MenuScreen getScreen() {
    return s_screen;
}

void pushAlert(const char* msg) {
    strncpy(s_alertLog[s_alertHead], msg, 32);
    s_alertLog[s_alertHead][32] = '\0';
    s_alertHead = (s_alertHead + 1) % ALERT_LOG_SIZE;
    if (s_alertCount < ALERT_LOG_SIZE) s_alertCount++;

    // Log silently — do NOT force screen to ALERTS.
    // Jumping screens on every alert interrupts navigation and confuses users.
    // User can browse to ALERTS page manually via SHORT_PRESS.
    DBG("DisplayManager: alert logged -> %s (on screen %d)", msg, (int)s_screen);
}

static uint32_t s_lastSleepMs = 0;

void displayOff() {
    if (!s_displayOn) return;
    s_prevScreen = s_screen;   // save current screen before blanking
    oled.clearDisplay();
    oled.display();
    oled.ssd1306_command(SSD1306_DISPLAYOFF);
    s_displayOn   = false;
    s_lastSleepMs = millis();
    DBG("DisplayManager: OLED OFF (saved screen=%d, sleepAt=%lu)", (int)s_prevScreen, s_lastSleepMs);
}

void displayOn() {
    if (s_displayOn) return;
    oled.ssd1306_command(SSD1306_DISPLAYON);
    s_displayOn = true;
    s_screen    = s_prevScreen;   // restore — NOT forced to HOME
    s_lastInput = millis();        // reset idle timer
    DBG("DisplayManager: OLED ON (restored screen=%d)", (int)s_screen);
}

bool isDisplayOn() {
    return s_displayOn;
}

void resetIdleTimer() {
    s_lastInput = millis();
}

uint32_t getLastSleepMs() {
    return s_lastSleepMs;
}

} // namespace DisplayManager
