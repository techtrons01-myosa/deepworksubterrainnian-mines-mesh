// ============================================================
//  MYOSA NODE FIRMWARE
//  File    : NODE.ino
//  Platform: ESP32-WROOM-32E  (Arduino IDE + ESP32 board pkg)
//  Version : 1.0.0
//
//  Architecture:
//    setup()  — ordered initialisation of all manager modules
//    loop()   — non-blocking update tick for every module
//
//  Required Arduino Libraries (install via Library Manager):
//    • Adafruit BMP085 Library      (BMP180 driver)
//    • Adafruit MPU6050             (IMU driver)
//    • Adafruit APDS9960 Library    (gesture/proximity)
//    • Adafruit SSD1306             (OLED driver)
//    • Adafruit GFX Library         (graphics primitives)
//    • ESP32 BLE Arduino            (built-in with ESP32 pkg)
//    • esp_now.h / WiFi.h           (built-in with ESP32 pkg)
//
//  Board Settings:
//    Board      : ESP32 Dev Module (or ESP32-WROOM-32E)
//    Flash Freq : 80 MHz
//    Upload Spd : 921600
//    CPU Freq   : 240 MHz
//    Partition  : Default 4MB with spiffs
// ============================================================

// ---- Module headers ----
#include "config.h"
#include "types.h"
#include "system_manager.h"
#include "sensor_manager.h"
#include "display_manager.h"
#include "input_manager.h"
#include "mesh_manager.h"
#include "ble_manager.h"
#include "wifi_manager.h"
#include "proximity_manager.h"
#include "power_manager.h"
#include "alert_manager.h"
#include "telemetry_manager.h"

// ============================================================
//  SETUP
// ============================================================
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(200);   // Allow serial to settle

    Serial.println("\n========================================");
    Serial.println("       MYOSA NODE FIRMWARE  v1.0.0      ");
    Serial.println("========================================");

    // ---- 1. System Manager (must be first — builds node ID) ----
    SystemManager::init();
    const char*    nodeId  = SystemManager::getNodeId();
    const uint8_t* selfMac = SystemManager::getMac();
    DBG("Node ID : %s", nodeId);

    // ---- 2. Alert Manager (early — needed by later inits) ----
    AlertManager::init();

    // ---- 3. Sensors ----
    if (!SensorManager::init()) {
        DBG("WARNING: All sensors offline — continuing in degraded mode");
    }

    // ---- 4. Display ----
    if (!DisplayManager::init()) {
        DBG("WARNING: OLED init failed — display unavailable");
    }

    // ---- 5. Input (HW-763) ----
    InputManager::init();

    // ---- 6. Mesh / ESP-NOW
    //         WiFi.mode(WIFI_STA) is called inside MeshManager::init()
    //         so call this BEFORE BLE init to avoid radio conflicts ----
    if (!MeshManager::init(selfMac)) {
        DBG("CRITICAL: ESP-NOW failed — mesh unavailable");
    }

    // ---- 7. BLE ----
    if (!BleManager::init(nodeId)) {
        DBG("WARNING: BLE init failed — BLE unavailable");
    }

    // ---- 8. Wi-Fi (starts in OFF state) ----
    WiFiManager::init();

    // ---- 9. Proximity ----
    ProximityManager::init();

    // ---- 10. Power Manager ----
    PowerManager::init();

    // ---- 11. Telemetry ----
    TelemetryManager::init();

    // ---- Boot complete ----
    AlertManager::play(BuzzerPattern::BOOT);
    DisplayManager::setScreen(MenuScreen::HOME);

    DBG("=== Boot complete  faults=0x%04X ===", SystemManager::getFaultMask());
    Serial.println("========================================\n");
}

// ============================================================
//  LOOP — non-blocking update tick
// ============================================================
void loop() {
    // Input must be first so gesture/touch events are fresh
    InputManager::update();

    // Power manager — only toggles OLED display, CPU always runs
    PowerManager::update();

    // Sensor acquisition + gesture queue fill
    SensorManager::update();

    // Display rendering + menu navigation
    DisplayManager::update();

    // Mesh heartbeat + watchdog
    MeshManager::update();

    // BLE scanning
    BleManager::update();

    // Wi-Fi condition evaluation + state machine
    WiFiManager::update();

    // Proximity fusion
    ProximityManager::update();

    // Telemetry broadcast
    TelemetryManager::update();

    // Alert/buzzer pattern playback
    AlertManager::update();

    // System housekeeping
    SystemManager::update();
}
