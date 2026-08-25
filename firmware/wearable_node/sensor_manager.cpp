// ============================================================
//  MYOSA Node Firmware — Sensor Manager Implementation
//  File    : sensor_manager.cpp
//
//  Sensor configuration (confirmed from kit catalog):
//
//    BMP180   — pressure + altitude ONLY (I2C 0x77)
//               (No usable temperature in this config)
//
//    MPU6050  — accelerometer + gyroscope + ATMOSPHERE temperature
//               (I2C 0x68 or 0x69 auto-detected)
//               Die temperature ≈ ambient + small offset (~3°C)
//               Raise-to-wake: gyro magnitude > threshold wakes display
//
//    APDS9960 — proximity + ambient light + color (I2C 0x39)
//               Gesture mode DISABLED (conflicts with color/light)
//               Used with BMP180 for buried-person detection
//
//  Mine Safety Alerts:
//    Atmosphere temp > MINE_TEMP_MAX_C   → MINE_ALERT buzzer
//    Pressure out of range               → MINE_ALERT buzzer
//
//  Buried Person Detection (APDS9960 + BMP180):
//    Proximity HIGH + very dark + pressure elevated + confirmed
//    5 consecutive readings → MINE_ALERT + SOS broadcast
//
//  I2C: 100kHz standard mode (safe for cheap GY-xxx modules)
//  MPU6050: auto-detects 0x68 (AD0=LOW) then 0x69 (AD0=HIGH)
// ============================================================
#include "sensor_manager.h"
#include "system_manager.h"
#include "alert_manager.h"
#include "display_manager.h"    // for pushAlert()

#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_APDS9960.h>

namespace SensorManager {

// ---- Driver instances ----
static Adafruit_BMP085   bmp;
static Adafruit_MPU6050  mpu;
static Adafruit_APDS9960 apds;

// ---- State flags ----
static bool    s_bmpOk   = false;
static bool    s_mpuOk   = false;
static bool    s_apdsOk  = false;
static uint8_t s_mpuAddr = ADDR_MPU6050;

// ---- Sensor snapshot ----
static SensorData s_data;

// ---- MPU filter state ----
static float s_axF = 0, s_ayF = 0, s_azF = 0;
static float s_gxF = 0, s_gyF = 0, s_gzF = 0;

// ---- Raise-to-wake (dynamic motion reference) ----
static const float MOTION_WAKE_THRESHOLD_DPS = 100.0f;  // deliberate wrist flip
static const float MOTION_WAKE_ACCEL_G       = 0.60f;   // deliberate lift motion
static float s_gravX = 0, s_gravY = 0, s_gravZ = 1.0f;
static bool  s_gravInit = false;
static bool  s_motionDetected = false;

// ---- APDS9960 color state ----
static uint16_t s_colorR = 0, s_colorG = 0, s_colorB = 0, s_colorC = 0;

// ---- Buried person detection ----
// Requires: APDS proximity HIGH + darkness + BMP pressure elevated
static float    s_pressureBaseline   = 0.0f;
static bool     s_pressureBaselined  = false;
static uint8_t  s_buriedCount        = 0;          // consecutive buried readings
static bool     s_buriedDetected     = false;
static uint32_t s_lastBuriedBuzzMs   = 0;
static uint32_t s_lastBuriedCheckMs  = 0;

// ---- Mine safety alert state ----
static uint32_t s_lastMineAlertMs = 0;
static const uint32_t MINE_ALERT_INTERVAL_MS = 5000UL;

// ---- Sensor read timer ----
static uint32_t s_lastReadMs = 0;
static uint32_t s_lastMpuMs  = 0;   // separate fast-poll for raise-to-wake


// ---- Gesture FIFO (kept for API compatibility, gesture mode OFF) ----
static const uint8_t GESTURE_QUEUE_DEPTH = 4;
static GestureEvent  s_gestureQueue[GESTURE_QUEUE_DEPTH];
static uint8_t       s_gHead = 0, s_gTail = 0, s_gCount = 0;

static void _pushGesture(GestureEvent ev) {
    if (s_gCount >= GESTURE_QUEUE_DEPTH) return;
    s_gestureQueue[s_gTail] = ev;
    s_gTail = (s_gTail + 1) % GESTURE_QUEUE_DEPTH;
    s_gCount++;
}

// ============================================================
// I2C Helper
// ============================================================
static bool _i2cProbe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission(true) == 0; // 0 = ACK (device present)
}

// ============================================================
// ---- BMP180 — pressure + altitude + ambient temp ----
// ============================================================
static void _readBMP() {
    if (!s_bmpOk) {
        static uint32_t s_lastBmpRetry = 0;
        if (millis() - s_lastBmpRetry >= 2000UL) {
            s_lastBmpRetry = millis();
            if (_i2cProbe(0x77) && bmp.begin()) {
                s_bmpOk = true;
                SystemManager::clearFault(FAULT_BMP180_INIT);
                DBG("SensorManager: BMP180 re-connected OK!");
            }
        }
        return;
    }

    float pres = bmp.readPressure() / 100.0f;    // Pa → hPa
    // readAltitude() uses calibrated sea-level reference in Pascals (Pa)
    float alt  = bmp.readAltitude(SEA_LEVEL_PRESSURE_PA) + ELEVATION_CALIBRATION_OFFSET_M;
    float temp = bmp.readTemperature();          // °C from BMP180

    if (isnan(pres) || pres < 1.0f) {
        SystemManager::registerFault(FAULT_BMP180_READ);
        s_data.bmpValid = false;
        return;
    }
    SystemManager::clearFault(FAULT_BMP180_READ);
    s_data.pressure       = pres;
    s_data.altitude       = alt;
    s_data.bmpTemperature = temp;
    s_data.bmpValid       = true;

    // Calibrate pressure baseline on first valid reading
    if (!s_pressureBaselined) {
        s_pressureBaseline  = pres;
        s_pressureBaselined = true;
        DBG("SensorManager: Pressure baseline = %.1f hPa (T_bmp = %.1f C)", pres, temp);
    }

    // ---- Mine ambient temperature & pressure safety check ----
    uint32_t now = millis();
    if ((now - s_lastMineAlertMs) >= MINE_ALERT_INTERVAL_MS) {
        if (temp > MINE_TEMP_MAX_C) {
            AlertManager::play(BuzzerPattern::MINE_ALERT);
            DisplayManager::pushAlert("HIGH TEMP ALERT!");
            s_lastMineAlertMs = now;
            DBG("SensorManager: MINE HIGH AMBIENT TEMP %.1f C (> %.1f C)", temp, MINE_TEMP_MAX_C);
        } else if (pres > MINE_PRESSURE_MAX_HPA || pres < MINE_PRESSURE_MIN_HPA) {
            AlertManager::play(BuzzerPattern::MINE_ALERT);
            DisplayManager::pushAlert("PRESSURE ALERT!");
            s_lastMineAlertMs = now;
            DBG("SensorManager: MINE PRESSURE %.0f hPa", pres);
        }
    }
}

// Direct raw I2C burst read helper (14 bytes: Accel 6B, Temp 2B, Gyro 6B)
static bool _readMpuDirect(uint8_t addr, float* ax, float* ay, float* az,
                           float* gx, float* gy, float* gz, float* tempC) {
    Wire.beginTransmission(addr);
    Wire.write(0x3B); // ACCEL_XOUT_H
    if (Wire.endTransmission(false) != 0) return false;

    if (Wire.requestFrom(addr, (uint8_t)14) != 14) return false;

    int16_t rawAx = (int16_t)((Wire.read() << 8) | Wire.read());
    int16_t rawAy = (int16_t)((Wire.read() << 8) | Wire.read());
    int16_t rawAz = (int16_t)((Wire.read() << 8) | Wire.read());
    int16_t rawT  = (int16_t)((Wire.read() << 8) | Wire.read());
    int16_t rawGx = (int16_t)((Wire.read() << 8) | Wire.read());
    int16_t rawGy = (int16_t)((Wire.read() << 8) | Wire.read());
    int16_t rawGz = (int16_t)((Wire.read() << 8) | Wire.read());

    // ±4g scale: 8192 LSB/g
    *ax = (float)rawAx / 8192.0f;
    *ay = (float)rawAy / 8192.0f;
    *az = (float)rawAz / 8192.0f;

    // Temperature formula: Temp_degC = (raw / 340.0) + 36.53
    *tempC = ((float)rawT / 340.0f) + 36.53f;

    // ±250 deg/s scale: 131.0 LSB/(deg/s)
    *gx = (float)rawGx / 131.0f;
    *gy = (float)rawGy / 131.0f;
    *gz = (float)rawGz / 131.0f;

    return true;
}

// Direct raw I2C configure MPU6050
static bool _initMpuHardware(uint8_t addr) {
    // 1. Wake up from sleep (PWR_MGMT_1 = 0x00, internal clock)
    Wire.beginTransmission(addr);
    Wire.write(0x6B);
    Wire.write(0x00);
    if (Wire.endTransmission(true) != 0) return false;
    delay(30);

    // 2. Clock source = PLL with X Gyro (PWR_MGMT_1 = 0x01)
    Wire.beginTransmission(addr);
    Wire.write(0x6B);
    Wire.write(0x01);
    Wire.endTransmission(true);
    delay(10);

    // 3. DLPF config = 44Hz (CONFIG = 0x03)
    Wire.beginTransmission(addr);
    Wire.write(0x1A);
    Wire.write(0x03);
    Wire.endTransmission(true);

    // 4. Gyro range = ±250 deg/s (GYRO_CONFIG = 0x00)
    Wire.beginTransmission(addr);
    Wire.write(0x1B);
    Wire.write(0x00);
    Wire.endTransmission(true);

    // 5. Accel range = ±4g (ACCEL_CONFIG = 0x08)
    Wire.beginTransmission(addr);
    Wire.write(0x1C);
    Wire.write(0x08);
    Wire.endTransmission(true);

    return true;
}

// ============================================================
// ---- MPU6050 — accel + gyro + atmosphere temperature ----
// ============================================================
static void _readMPU() {
    if (!s_mpuOk) return;

    float ax = 0, ay = 0, az = 0;
    float gx = 0, gy = 0, gz = 0;
    float tempC = 0;

    if (!_readMpuDirect(s_mpuAddr, &ax, &ay, &az, &gx, &gy, &gz, &tempC)) {
        // Retry once on failure
        if (!_readMpuDirect(s_mpuAddr, &ax, &ay, &az, &gx, &gy, &gz, &tempC)) {
            SystemManager::registerFault(FAULT_MPU6050_READ);
            s_data.mpuValid = false;
            return;
        }
    }
    SystemManager::clearFault(FAULT_MPU6050_READ);

    // ---- Exponential moving-average low-pass filter ----
    const float af = MPU_FILTER_ALPHA;
    s_axF = af * s_axF + (1.0f - af) * ax;
    s_ayF = af * s_ayF + (1.0f - af) * ay;
    s_azF = af * s_azF + (1.0f - af) * az;
    s_gxF = af * s_gxF + (1.0f - af) * gx;
    s_gyF = af * s_gyF + (1.0f - af) * gy;
    s_gzF = af * s_gzF + (1.0f - af) * gz;

    s_data.accelX = s_axF; s_data.accelY = s_ayF; s_data.accelZ = s_azF;
    s_data.gyroX  = s_gxF; s_data.gyroY  = s_gyF; s_data.gyroZ  = s_gzF;
    s_data.mpuTemperature = tempC;
    s_data.mpuValid = true;

    // ---- Dynamic tracking gravity baseline (adapts to any rest angle) ----
    if (!s_gravInit) {
        s_gravX = s_axF;
        s_gravY = s_ayF;
        s_gravZ = s_azF;
        s_gravInit = true;
    } else {
        const float beta = 0.95f; // slowly tracks static orientation
        s_gravX = beta * s_gravX + (1.0f - beta) * s_axF;
        s_gravY = beta * s_gravY + (1.0f - beta) * s_ayF;
        s_gravZ = beta * s_gravZ + (1.0f - beta) * s_azF;
    }

    // ---- Raise-to-wake: deliberate dynamic movement only ----
    float gyroMag = sqrtf(s_gxF * s_gxF + s_gyF * s_gyF + s_gzF * s_gzF);
    float dynX = s_axF - s_gravX;
    float dynY = s_ayF - s_gravY;
    float dynZ = s_azF - s_gravZ;
    float dynamicAccel = sqrtf(dynX * dynX + dynY * dynY + dynZ * dynZ);
    if ((gyroMag > MOTION_WAKE_THRESHOLD_DPS) || (dynamicAccel > MOTION_WAKE_ACCEL_G)) {
        s_motionDetected = true;
    }
}

// ============================================================
// Direct raw I2C configure APDS9960 (supports all ID variants: 0xAB, 0x9C, 0xA8, etc.)
// ============================================================
static bool _initApdsHardware() {
    uint8_t addr = 0x39;
    if (!_i2cProbe(addr)) return false;

    // 1. Disable all features first
    Wire.beginTransmission(addr);
    Wire.write(0x80); // ENABLE register
    Wire.write(0x00);
    if (Wire.endTransmission(true) != 0) return false;
    delay(10);

    // 2. Integration time (ATIME = 0xDB -> ~100ms)
    Wire.beginTransmission(addr);
    Wire.write(0x81);
    Wire.write(0xDB);
    Wire.endTransmission(true);

    // 3. Wait time (WTIME = 0xFF -> ~2.78ms)
    Wire.beginTransmission(addr);
    Wire.write(0x83);
    Wire.write(0xFF);
    Wire.endTransmission(true);

    // 4. Proximity pulse count (PPULSE = 0x87 -> 8 pulses, 16us)
    Wire.beginTransmission(addr);
    Wire.write(0x8E);
    Wire.write(0x87);
    Wire.endTransmission(true);

    // 5. Control (CONTROL = 0x20 -> PGAIN 2X, AGAIN 1X, 100mA)
    Wire.beginTransmission(addr);
    Wire.write(0x8F);
    Wire.write(0x20);
    Wire.endTransmission(true);

    // 6. Enable Power + ALS + Proximity (ENABLE = 0x07 -> PON | AEN | PEN)
    Wire.beginTransmission(addr);
    Wire.write(0x80);
    Wire.write(0x07);
    if (Wire.endTransmission(true) != 0) return false;
    delay(10);

    return true;
}

static bool _readApdsDirect(uint8_t* prox, uint16_t* lux, uint16_t* r, uint16_t* g, uint16_t* b) {
    uint8_t addr = 0x39;
    
    // Read Proximity from 0x9C (PDATA)
    Wire.beginTransmission(addr);
    Wire.write(0x9C);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(addr, (uint8_t)1) != 1) return false;
    *prox = Wire.read();

    // Read Clear, Red, Green, Blue from 0x94 (CDATAL..BDATAH = 8 bytes)
    Wire.beginTransmission(addr);
    Wire.write(0x94);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(addr, (uint8_t)8) != 8) return false;

    uint16_t c = (uint16_t)(Wire.read() | (Wire.read() << 8));
    *r = (uint16_t)(Wire.read() | (Wire.read() << 8));
    *g = (uint16_t)(Wire.read() | (Wire.read() << 8));
    *b = (uint16_t)(Wire.read() | (Wire.read() << 8));
    *lux = c;

    return true;
}

// ============================================================
// ---- APDS9960 — proximity + ambient light + color ----
// ============================================================
static void _readAPDS() {
    if (!s_apdsOk) return;

    uint8_t prox = 0;
    uint16_t lux = 0, r = 0, g = 0, b = 0;
    if (_readApdsDirect(&prox, &lux, &r, &g, &b)) {
        s_data.proximity    = prox;
        s_data.ambientLight = lux;
        s_colorR = r; s_colorG = g; s_colorB = b; s_colorC = lux;
        s_data.apdsValid    = true;
        SystemManager::clearFault(FAULT_APDS_READ);
    } else {
        s_data.apdsValid = false;
        SystemManager::registerFault(FAULT_APDS_READ);
    }
}

// ============================================================
// ---- Buried Person Detection ----
// Uses APDS9960 (proximity + darkness) + BMP180 (pressure rise)
// Requires BURIED_CONFIRM_COUNT consecutive hits to confirm
// ============================================================
static void _checkBuried() {
    if (!s_apdsOk || !s_bmpOk || !s_pressureBaselined) return;

    bool proxClose   = (s_data.proximity    > BURIED_PROX_MIN);
    bool veryDark    = (s_data.ambientLight < BURIED_LIGHT_MAX);
    bool pressureUp  = (s_data.pressure - s_pressureBaseline) > BURIED_PRESSURE_DELTA;

    // All three conditions must be true simultaneously
    bool buried_cond = proxClose && veryDark && pressureUp;

    if (buried_cond) {
        if (s_buriedCount < BURIED_CONFIRM_COUNT) s_buriedCount++;
    } else {
        // Conditions not met — gradually clear counter
        if (s_buriedCount > 0) s_buriedCount--;
        if (s_buriedCount == 0) s_buriedDetected = false;
    }

    // ---- Confirm buried after sustained readings ----
    if (s_buriedCount >= BURIED_CONFIRM_COUNT) {
        s_buriedDetected = true;
        uint32_t now = millis();
        if ((now - s_lastBuriedBuzzMs) >= BURIED_BUZZ_INTERVAL_MS) {
            AlertManager::play(BuzzerPattern::MINE_ALERT);
            DisplayManager::pushAlert("BURIED ALERT!");
            s_lastBuriedBuzzMs = now;
            DBG("SensorManager: BURIED ALERT (P=%.1fhPa Prox=%u Lux=%u)",
                s_data.pressure, s_data.proximity, s_data.ambientLight);
        }
    }
}

// ---- Public API ----
// ============================================================

/** Hard-wake MPU6050 by writing directly to PWR_MGMT_1 (reg 0x6B).
 *  The MPU6050 ships in SLEEP mode — the Adafruit library should clear it,
 *  but doing it manually first guarantees a clean start. */
static void _mpuWake(uint8_t addr) {
    Wire.beginTransmission(addr);
    Wire.write(0x6B);   // PWR_MGMT_1 register
    Wire.write(0x00);   // clear SLEEP bit, select internal oscillator
    Wire.endTransmission(true);
    delay(50);           // let MPU stabilise after wake
}

bool init() {
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(100000UL); // 100kHz standard mode
    delay(100);

    memset(&s_data, 0, sizeof(s_data));

    // ---- 1. BMP180 (0x77) — pressure + altitude + true ambient temp ----
    if (_i2cProbe(0x77)) {
        if (bmp.begin()) {
            s_bmpOk = true;
            SystemManager::clearFault(FAULT_BMP180_INIT);
            DBG("SensorManager: BMP180 OK at 0x77 [pressure+altitude+temp]");
        } else {
            SystemManager::registerFault(FAULT_BMP180_INIT);
            DBG("SensorManager: BMP180 begin() FAIL");
        }
    } else {
        SystemManager::registerFault(FAULT_BMP180_INIT);
        DBG("SensorManager: BMP180 NOT FOUND at 0x77");
    }

    // ---- 2. MPU6050 — auto-detect 0x69 (MYOSA default) then 0x68 ----
    {
        static const uint8_t MPU_ADDRS[] = { 0x69, 0x68 };
        for (uint8_t i = 0; i < 2 && !s_mpuOk; i++) {
            uint8_t addr = MPU_ADDRS[i];
            if (!_i2cProbe(addr)) continue;

            if (_initMpuHardware(addr)) {
                s_mpuAddr = addr;
                s_mpuOk   = true;
                s_gravInit = false;
                SystemManager::clearFault(FAULT_MPU6050_INIT);
                DBG("SensorManager: MPU6050 OK at 0x%02X [accel+gyro+die.temp]", addr);
            }
        }
        if (!s_mpuOk) {
            SystemManager::registerFault(FAULT_MPU6050_INIT);
            DBG("SensorManager: MPU6050 OFFLINE (checked 0x69 and 0x68)");
        }
    }

    // ---- 3. APDS9960 (0x39) — direct hardware driver ----
    if (_initApdsHardware()) {
        s_apdsOk = true;
        SystemManager::clearFault(FAULT_APDS_INIT);
        DBG("SensorManager: APDS9960 OK at 0x39");
    } else {
        SystemManager::registerFault(FAULT_APDS_INIT);
        DBG("SensorManager: APDS9960 OFFLINE at 0x39");
    }

    s_lastReadMs = millis();
    s_lastMpuMs  = millis();
    return s_bmpOk || s_mpuOk || s_apdsOk;
}

void update() {
    uint32_t now = millis();

    // ---- Fast MPU poll (100ms) for responsive raise-to-wake ----
    if (now - s_lastMpuMs >= 100UL) {
        s_lastMpuMs = now;
        _readMPU();   // updates s_motionDetected
    }

    // ---- Slow sensor poll (2s interval) ----
    if (now - s_lastReadMs < SENSOR_READ_INTERVAL_MS) return;
    s_lastReadMs = now;

    s_data.timestamp = now;
    _readBMP();
    _readAPDS();
    _checkBuried();

    // ---- Hot-plug Auto-retry offline sensors every 3 seconds ----
    static uint32_t s_retryMs = 0;
    if ((!s_mpuOk || !s_apdsOk) && (now - s_retryMs) >= 3000UL) {
        s_retryMs = now;

        if (!s_mpuOk) {
            static const uint8_t MPU_ADDRS[] = { 0x69, 0x68 };
            for (uint8_t i = 0; i < 2 && !s_mpuOk; i++) {
                uint8_t addr = MPU_ADDRS[i];
                if (!_i2cProbe(addr)) continue;
                if (_initMpuHardware(addr)) {
                    s_mpuAddr = addr;
                    s_mpuOk   = true;
                    s_gravInit = false;
                    SystemManager::clearFault(FAULT_MPU6050_INIT);
                    DBG("SensorManager: MPU6050 HOT-PLUG OK at 0x%02X", addr);
                }
            }
        }

        if (!s_apdsOk) {
            if (_initApdsHardware()) {
                s_apdsOk = true;
                SystemManager::clearFault(FAULT_APDS_INIT);
                DBG("SensorManager: APDS9960 HOT-PLUG OK at 0x39");
            }
        }
    }

    DBG("SensorManager: P=%.0fhPa alt=%.0fm | "
        "ax=%.2f ay=%.2f az=%.2f | gx=%.0f gy=%.0f gz=%.0f | "
        "T_atm=%.1fC | prox=%u lux=%u | buried=%d",
        s_data.pressure, s_data.altitude,
        s_data.accelX, s_data.accelY, s_data.accelZ,
        s_data.gyroX,  s_data.gyroY,  s_data.gyroZ,
        s_data.mpuTemperature,
        s_data.proximity, s_data.ambientLight,
        (int)s_buriedDetected);
}


const SensorData& getSensorData() {
    return s_data;
}

GestureEvent popGesture() {
    if (s_gCount == 0) return GestureEvent::NONE;
    GestureEvent ev = s_gestureQueue[s_gHead];
    s_gHead = (s_gHead + 1) % GESTURE_QUEUE_DEPTH;
    s_gCount--;
    return ev;
}

bool isAnyOnline() {
    return s_bmpOk || s_mpuOk || s_apdsOk;
}

bool isMpuOk() {
    return s_mpuOk;
}

bool isMotionDetected() {
    bool m = s_motionDetected;
    s_motionDetected = false;   // auto-clear after read
    return m;
}

bool isBuriedDetected() {
    return s_buriedDetected;
}

} // namespace SensorManager
