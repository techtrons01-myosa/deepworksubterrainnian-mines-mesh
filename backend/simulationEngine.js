import { stmts } from './db.js';

// ─── Static mesh topology ────────────────────────────────────────────────────
export const TOPOLOGY = {
  'MYO-001': ['MYO-002', 'MYO-003'],
  'MYO-002': ['MYO-001', 'MYO-004', 'MYO-005'],
  'MYO-003': ['MYO-001', 'MYO-006'],
  'MYO-004': ['MYO-002', 'MYO-007'],
  'MYO-005': ['MYO-002', 'MYO-008', 'MYO-009'],
  'MYO-006': ['MYO-003', 'MYO-010'],
  'MYO-007': ['MYO-004'],
  'MYO-008': ['MYO-005'],
  'MYO-009': ['MYO-005', 'MYO-010'],
  'MYO-010': ['MYO-006', 'MYO-009'],
};

// Base anchor positions on a 16×16 virtual grid
const BASE_POS = {
  'MYO-001': { x: 2.0,  y: 8.0  },
  'MYO-002': { x: 5.5,  y: 8.0  },
  'MYO-003': { x: 2.0,  y: 12.0 },
  'MYO-004': { x: 9.0,  y: 5.0  },
  'MYO-005': { x: 9.0,  y: 11.0 },
  'MYO-006': { x: 2.0,  y: 15.5 },
  'MYO-007': { x: 12.5, y: 3.5  },
  'MYO-008': { x: 13.0, y: 9.5  },
  'MYO-009': { x: 12.0, y: 14.0 },
  'MYO-010': { x: 6.5,  y: 15.5 },
};

// Base altitude distribution (in meters) representing different mine levels/shafts
const BASE_ALTITUDES = {
  'MYO-001': 82.5, // Surface Portal / Entry
  'MYO-002': 68.0, // Shaft 1 - Upper Gallery
  'MYO-003': 58.4, // Shaft 2 - Upper Level
  'MYO-004': 48.6, // Mid Level Drift East
  'MYO-005': 42.0, // Central Distribution Hub
  'MYO-006': 26.5, // Lower Tunnel Stope
  'MYO-007': 54.2, // Upper East Ramp
  'MYO-008': 35.8, // Midway Stope
  'MYO-009': 18.4, // Deep Extraction Pit
  'MYO-010': 9.2,  // Sump / Lower Access Way
};

const NODE_COUNT = 10;
const TICK_MS    = 2000;

// ─── Utility helpers ─────────────────────────────────────────────────────────
const rnd  = () => Math.random();
const jit  = (v, a) => v + (rnd() - 0.5) * 2 * a;
const clmp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const mac  = i => `AA:BB:CC:DD:EE:${i.toString(16).padStart(2, '0').toUpperCase()}`;
const pad  = i => String(i).padStart(3, '0');

class SimulationEngine {
  constructor() {
    this.nodes     = {};
    this.tickCount = 0;
    this.settings = {
      geofenceRadius: 50,
      buzzerEnabled: true,
      groundLevelDatum: 0.0,
      groundReferenceNode: 'MYO-001',
      nodeCalibrationOffsets: {},
    };
    this._initNodes();
  }

  _initNodes(count = NODE_COUNT) {
    this.nodes = {};
    for (let i = 1; i <= count; i++) {
      const id = `MYO-${pad(i)}`;
      this._createNode(id, i);
    }
  }

  _createNode(id, i) {
    const baseAlt = BASE_ALTITUDES[id] ?? (15 + (i * 7) % 70);
    const pos = BASE_POS[id] ?? { x: 2.0 + ((i * 3.5) % 12), y: 3.0 + ((i * 2.8) % 11) };
    
    // Connect to adjacent nodes
    const neighbors = TOPOLOGY[id] || (i > 1 ? [`MYO-${pad(i - 1)}`] : []);

    this.nodes[id] = {
      node_id:   id,
      mac:       mac(i),
      firmware:  '1.2.3',
      status:    'ONLINE',
      temperature: jit(28.5, 2),
      pressure:    jit(1008 - (baseAlt * 0.12), 4),
      altitude:    jit(baseAlt, 1.2),
      baseAltitude: baseAlt,
      accel:  { x: jit(0, 0.05), y: jit(0, 0.05), z: jit(9.81, 0.05) },
      gyro:   { x: 0, y: 0, z: 0 },
      espnow:    true,
      ble:       true,
      wifi:      i === 1,
      rssi:      -45 - Math.floor(rnd() * 25),
      neighbors: [...neighbors],
      position: { x: pos.x, y: pos.y, confidence: 0.88 + rnd() * 0.1 },
      last_seen:       Date.now(),
      uptime:          Math.floor(rnd() * 86400),
      alertState:      'NORMAL',
      disconnectUntil: 0,
      health: {
        node:          95 + Math.floor(rnd() * 5),
        sensor:        96 + Math.floor(rnd() * 4),
        communication: 90 + Math.floor(rnd() * 8),
      },
    };

    try {
      stmts.upsertNode({
        ':node_id':  id,
        ':mac':      mac(i),
        ':firmware': '1.2.3',
        ':status':   'ONLINE',
        ':last_seen': Date.now(),
      });
    } catch (_) {}
  }

  addNode(params = {}) {
    const existingIds = Object.keys(this.nodes);
    const nextIdx = existingIds.length + 1;
    const id = params.node_id || `MYO-${pad(nextIdx)}`;
    
    if (this.nodes[id]) {
      return { success: false, message: `Node ${id} already exists` };
    }

    const baseAlt = params.altitude ? parseFloat(params.altitude) : (BASE_ALTITUDES[id] ?? 45.0);
    const pos = params.position || { x: jit(8, 4), y: jit(8, 4) };

    this.nodes[id] = {
      node_id:   id,
      mac:       params.mac || mac(nextIdx),
      firmware:  params.firmware || '1.2.3',
      status:    'ONLINE',
      temperature: jit(28.5, 2),
      pressure:    jit(1008 - (baseAlt * 0.12), 4),
      altitude:    baseAlt,
      baseAltitude: baseAlt,
      accel:  { x: jit(0, 0.05), y: jit(0, 0.05), z: jit(9.81, 0.05) },
      gyro:   { x: 0, y: 0, z: 0 },
      espnow:    true,
      ble:       true,
      wifi:      false,
      rssi:      -48 - Math.floor(rnd() * 20),
      neighbors: existingIds.slice(0, 2),
      position: { x: pos.x, y: pos.y, confidence: 0.92 },
      last_seen:       Date.now(),
      uptime:          0,
      alertState:      'NORMAL',
      disconnectUntil: 0,
      health: { node: 96, sensor: 98, communication: 94 },
    };

    try {
      stmts.upsertNode({
        ':node_id':  id,
        ':mac':      this.nodes[id].mac,
        ':firmware': '1.2.3',
        ':status':   'ONLINE',
        ':last_seen': Date.now(),
      });
    } catch (_) {}

    return { success: true, node: this.nodes[id] };
  }

  removeNode(nodeId) {
    if (!this.nodes[nodeId]) {
      return { success: false, message: `Node ${nodeId} does not exist` };
    }
    delete this.nodes[nodeId];

    // Remove from other nodes neighbors
    Object.values(this.nodes).forEach(n => {
      n.neighbors = (n.neighbors || []).filter(nb => nb !== nodeId);
    });

    return { success: true, removedId: nodeId };
  }

  setNodeCount(targetCount) {
    const count = Math.max(1, Math.min(30, parseInt(targetCount, 10) || 10));
    const currentKeys = Object.keys(this.nodes);

    if (count > currentKeys.length) {
      // Add nodes
      for (let i = currentKeys.length + 1; i <= count; i++) {
        const id = `MYO-${pad(i)}`;
        if (!this.nodes[id]) {
          this._createNode(id, i);
        }
      }
    } else if (count < currentKeys.length) {
      // Remove excess nodes
      const toRemove = currentKeys.slice(count);
      toRemove.forEach(id => this.removeNode(id));
    }

    return { success: true, count: Object.keys(this.nodes).length, nodes: this.nodes };
  }

  getSettings() {
    return {
      ...this.settings,
      nodeCount: Object.keys(this.nodes).length,
    };
  }

  updateSettings(newSettings = {}) {
    this.settings = {
      ...this.settings,
      ...newSettings,
    };
    if (newSettings.nodeCount && newSettings.nodeCount !== Object.keys(this.nodes).length) {
      this.setNodeCount(newSettings.nodeCount);
    }
    return { success: true, settings: this.getSettings() };
  }

  tick() {
    this.tickCount++;
    const now    = Date.now();
    const events = [];

    for (const [id, n] of Object.entries(this.nodes)) {
      // ── Reconnection ─────────────────────────────────────────────────────
      if (n.status === 'OFFLINE' && now > n.disconnectUntil) {
        n.status     = 'ONLINE';
        n.last_seen  = now;
        n.alertState = 'NORMAL';
        events.push({ type: 'NODE_RECONNECTED', nodeId: id });
      }

      if (n.status === 'OFFLINE') continue;

      // ── Random disconnection (~1.5 % / tick; MYO-001 is immune) ─────────
      if (id !== 'MYO-001' && rnd() < 0.015) {
        n.status          = 'OFFLINE';
        n.alertState      = 'CRITICAL';
        n.disconnectUntil = now + 15_000 + rnd() * 45_000;
        events.push({ type: 'NODE_DISCONNECTED', nodeId: id });
        try {
          stmts.upsertNode({ ':node_id': id, ':mac': n.mac, ':firmware': n.firmware, ':status': 'OFFLINE', ':last_seen': now });
        } catch (_) {}
        continue;
      }

      // ── Sensor drift ─────────────────────────────────────────────────────
      n.temperature = clmp(jit(n.temperature, 0.08), 20, 40);
      n.pressure    = clmp(jit(n.pressure,    0.25), 980, 1030);
      const baseAlt = n.baseAltitude ?? 45.0;
      n.altitude    = clmp(jit(n.altitude, 0.15), Math.max(0, baseAlt - 3.5), baseAlt + 3.5);

      const moving = rnd() < 0.08;
      if (moving) {
        n.accel = { x: jit(0, 0.4), y: jit(0, 0.4), z: jit(9.81, 0.3) };
        n.gyro  = { x: jit(0, 2.5), y: jit(0, 2.5), z: jit(0, 2.5) };
      } else {
        n.accel = { x: jit(0, 0.03), y: jit(0, 0.03), z: jit(9.81, 0.04) };
        n.gyro  = { x: 0, y: 0, z: 0 };
      }

      n.rssi = clmp(jit(n.rssi, 1.5), -88, -32);

      n.position.x          = clmp(jit(n.position.x, 0.03), 0, 16);
      n.position.y          = clmp(jit(n.position.y, 0.03), 0, 16);
      n.position.confidence = clmp(jit(n.position.confidence, 0.008), 0.5, 0.99);

      const rssiScore = n.rssi > -60 ? 98 : n.rssi > -72 ? 84 : 68;
      n.health.communication = clmp(Math.round(jit(rssiScore, 3)), 40, 100);
      n.health.sensor        = clmp(Math.round(jit(n.health.sensor, 0.8)), 70, 100);
      n.health.node          = Math.round((n.health.communication + n.health.sensor) / 2);

      n.last_seen = now;
      n.uptime   += 2;

      // ── Persist every 5 ticks ─────────────────────────────────────────────
      if (this.tickCount % 5 === 0) {
        try {
          stmts.insertTelemetry({
            ':node_id':     id,
            ':timestamp':   now,
            ':temperature': n.temperature,
            ':pressure':    n.pressure,
            ':altitude':    n.altitude,
            ':accel_x':     n.accel.x,
            ':accel_y':     n.accel.y,
            ':accel_z':     n.accel.z,
            ':gyro_x':      n.gyro.x,
            ':gyro_y':      n.gyro.y,
            ':gyro_z':      n.gyro.z,
            ':espnow':      1,
            ':ble':         1,
            ':wifi':        n.wifi ? 1 : 0,
            ':neighbors':   JSON.stringify(n.neighbors),
            ':rssi':        n.rssi,
          });
          stmts.upsertNode({ ':node_id': id, ':mac': n.mac, ':firmware': n.firmware, ':status': n.status, ':last_seen': now });
          stmts.insertLocation({ ':node_id': id, ':timestamp': now, ':x': n.position.x, ':y': n.position.y, ':confidence': n.position.confidence });
        } catch (_) {}
      }
    }

    return { nodes: this.nodes, events };
  }

  start(cb) {
    this._interval = setInterval(() => cb(this.tick()), TICK_MS);
  }

  stop() {
    clearInterval(this._interval);
  }

  snapshot() {
    return this.nodes;
  }
}

export default SimulationEngine;
