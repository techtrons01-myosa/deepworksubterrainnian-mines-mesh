// ============================================================
//  MYOSA Node Firmware — Mesh Manager (ESP-NOW)
//  File    : mesh_manager.h
//  Purpose : ESP-NOW peer lifecycle, heartbeat TX, packet RX/TX,
//            neighbour table, and node-loss watchdog.
// ============================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace MeshManager {

    /** Initialise ESP-NOW (WiFi must be in STA mode first).
     *  Registers FAULT_ESPNOW_INIT on failure. */
    bool init(const uint8_t* selfMac);

    /** Call every loop() — fires heartbeats, checks timeouts. */
    void update();

    // ---- Transmission ----
    /** Broadcast a packet to all peers (255:255:255:255:255:255). */
    bool broadcast(PacketType type, const uint8_t* payload, uint8_t len);

    /** Unicast a packet to a specific peer MAC. */
    bool sendTo(const uint8_t* mac, PacketType type,
                const uint8_t* payload, uint8_t len);

    // ---- Neighbour table ----
    /** Returns current count of ACTIVE peers. */
    uint8_t     getNeighborCount();

    /** Returns a pointer to the neighbour table (read-only). */
    const NodeRecord* getNeighbors();

    /** Returns true if a peer with the given MAC is ACTIVE. */
    bool        isPeerActive(const uint8_t* mac);

    // ---- Status ----
    bool        isEspNowOk();
    const char* getEspNowStatusStr();

    // ---- Sequence counter (used by TelemetryManager) ----
    uint16_t    nextSequence();

    // ---- Stop / resume for sleep/wake ----
    void suspend();
    void resume(const uint8_t* selfMac);

    // ---- Sub-surface Ground Datum & Evacuation ----
    float getGroundDatum();
    bool  isEvacuationActive();
    void  cancelEvacuation();

} // namespace MeshManager
