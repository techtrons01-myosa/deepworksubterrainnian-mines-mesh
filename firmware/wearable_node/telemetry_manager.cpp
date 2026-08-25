// ============================================================
//  MYOSA Node Firmware — Telemetry Manager Implementation
//  File    : telemetry_manager.cpp
// ============================================================
#include "telemetry_manager.h"
#include "sensor_manager.h"
#include "mesh_manager.h"

namespace TelemetryManager {

static uint32_t s_lastSendMs = 0;

void init() {
    s_lastSendMs = 0;  // Force immediate first send
    DBG("TelemetryManager: init OK (interval=%lums)", TELEM_MIN_INTERVAL_MS);
}

void update() {
    if ((millis() - s_lastSendMs) < TELEM_MIN_INTERVAL_MS) return;
    sendNow();
}

void sendNow() {
    s_lastSendMs = millis();

    const SensorData& d = SensorManager::getSensorData();

    SensorPayload sp = {};
    sp.temperature  = d.bmpValid ? d.bmpTemperature : (d.mpuValid ? d.mpuTemperature : 0.0f);
    sp.pressure     = d.pressure;
    sp.altitude     = d.altitude;
    sp.accelX       = d.accelX;
    sp.accelY       = d.accelY;
    sp.accelZ       = d.accelZ;
    sp.gyroX        = d.gyroX;
    sp.gyroY        = d.gyroY;
    sp.gyroZ        = d.gyroZ;
    sp.proximity    = d.proximity;
    sp.ambientLight = d.ambientLight;

    MeshManager::broadcast(PacketType::SENSOR_DATA,
                           (const uint8_t*)&sp, sizeof(sp));
    DBG("TelemetryManager: SENSOR_DATA broadcast");
}

} // namespace TelemetryManager
