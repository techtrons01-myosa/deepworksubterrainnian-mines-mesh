import { SerialPort } from 'serialport';
import { ReadlineParser } from '@serialport/parser-readline';
import { stmts } from './db.js';

export class SerialGateway {
  constructor(portPath = 'COM7', baudRate = 115200) {
    this.portPath = portPath;
    this.baudRate = baudRate;
    this.port = null;
    this.parser = null;
    this.nodes = {};
    this.firstSeen = {};
    this.eventListeners = [];
    this.settings = {
      nodeCount: 1,
      geofenceRadius: 50,
      buzzerEnabled: true,
      buzzerVolume: 0.5,
      alarmOnDisconnect: true,
      groundLevelDatum: 50.0, // Default ground level elevation datum
      groundReferenceNode: 'MYO-3681F4',
      nodeCalibrationOffsets: {},
      evacuationActive: false,
    };
    this.isConnected = false;
    this.reconnectTimer = null;
  }

  start() {
    this._connect();
  }

  _connect() {
    if (this.port && this.port.isOpen) {
      try { this.port.close(); } catch (_) {}
    }

    console.log(`[SerialGateway] Connecting to ${this.portPath} @ ${this.baudRate} baud...`);

    this.port = new SerialPort({
      path: this.portPath,
      baudRate: this.baudRate,
      autoOpen: false,
    });

    this.parser = this.port.pipe(new ReadlineParser({ delimiter: '\n' }));

    this.port.open((err) => {
      if (err) {
        console.warn(`[SerialGateway] Port ${this.portPath} open error:`, err.message);
        this.isConnected = false;
        this._scheduleReconnect();
        return;
      }
      this.isConnected = true;
      console.log(`[SerialGateway] Connected to ${this.portPath} (ESP32-S3 Gateway)`);
      // Initial sync of datum
      setTimeout(() => {
        this.sendDatum(this.settings.groundLevelDatum);
      }, 500);
    });

    this.parser.on('data', (line) => {
      this._handleLine(line.trim());
    });

    this.port.on('error', (err) => {
      console.warn(`[SerialGateway] Serial error:`, err.message);
      this._scheduleReconnect();
    });

    this.port.on('close', () => {
      console.warn(`[SerialGateway] Port ${this.portPath} closed. Reconnecting in 3s...`);
      this.isConnected = false;
      this._scheduleReconnect();
    });
  }

  _scheduleReconnect() {
    if (this.reconnectTimer) return;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this._connect();
    }, 3000);
  }

  _handleLine(line) {
    if (!line || !line.startsWith('{')) return;
    try {
      const msg = JSON.parse(line);
      if (msg.type === 'TELEMETRY') {
        this._ingestTelemetry(msg);
      } else if (msg.type === 'ALERT') {
        this._emitEvent({ type: 'ALERT', ...msg });
      } else if (msg.type === 'GATEWAY_STATUS') {
        console.log(`[SerialGateway Status]`, msg);
      }
    } catch (err) {
      // Ignore JSON parse errors from partial lines
    }
  }

  _computeRelativePosition(nodeId, rssi = -50, altitude = 0) {
    const rawRssi = typeof rssi === 'number' ? rssi : -50;
    const dist = Math.max(1.0, Math.min(30.0, Math.pow(10, (-45 - rawRssi) / 22.0)));
    let hash = 0;
    for (let i = 0; i < nodeId.length; i++) hash = (hash << 5) - hash + nodeId.charCodeAt(i);
    const angle = Math.abs(hash % 360) * (Math.PI / 180);

    const datum = parseFloat(this.settings.groundLevelDatum) || 50.0;
    const relDepth = datum - (altitude || datum);

    return {
      x: parseFloat((7.0 + dist * Math.cos(angle) * 0.4).toFixed(2)),
      y: parseFloat((7.0 + dist * Math.sin(angle) * 0.4).toFixed(2)),
      z: parseFloat(relDepth.toFixed(2)),
      confidence: parseFloat((0.85 + Math.min(0.12, (rawRssi + 80) / 250)).toFixed(2)),
    };
  }

  _ingestTelemetry(data) {
    const id = data.node_id;
    if (!id) return;

    const wasOffline = !this.nodes[id] || this.nodes[id].status !== 'ONLINE';
    const now = Date.now();
    if (!this.firstSeen[id]) {
      this.firstSeen[id] = now;
    }

    const uptime = data.uptime !== undefined ? Number(data.uptime) : Math.floor((now - this.firstSeen[id]) / 1000);
    const pos = this._computeRelativePosition(id, data.rssi, data.altitude);
    const datum = parseFloat(this.settings.groundLevelDatum) || 50.0;
    const depthBelowGround = datum - (data.altitude ?? datum);
    const linksCount = data.links !== undefined ? Number(data.links) : 1;

        // Gather all active peer miner node IDs on the mesh + the Gateway Coordinator
    const activePeers = Object.keys(this.nodes).filter(k => k !== id && this.nodes[k].status === 'ONLINE');
    const allNeighbors = ['GATEWAY-COM7', ...activePeers];

    const nodeRecord = {
      node_id:     id,
      mac:         data.mac || '00:00:00:00:00:00',
      firmware:    '1.0.0',
      status:      'ONLINE',
      temperature: data.temperature ?? 29.5,
      pressure:    data.pressure ?? 1012.3,
      altitude:    data.altitude ?? 25.0,
      baseAltitude: datum,
      relative_depth: parseFloat(depthBelowGround.toFixed(2)),
      accel:       data.accel || { x: 0, y: 0, z: 1.0 },
      gyro:        data.gyro || { x: 0, y: 0, z: 0 },
      proximity:   data.proximity ?? 0,
      ambientLight: data.ambientLight ?? 0,
      batteryPct:  data.batteryPct ?? 100,
      faultMask:   data.faultMask ?? 0,
      uptime:      uptime,
      links:       allNeighbors.length,
      espnow:      true,
      ble:         true,
      wifi:        false,
      rssi:        data.rssi ?? -50,
      neighbors:   allNeighbors,
      position:    pos,
      lastSeen:    now,
      last_seen:   now,
    };

    this.nodes[id] = nodeRecord;

    // Keep all other active nodes updated with the new neighbor
    for (const peerId of activePeers) {
      if (this.nodes[peerId] && Array.isArray(this.nodes[peerId].neighbors)) {
        if (!this.nodes[peerId].neighbors.includes(id)) {
          this.nodes[peerId].neighbors.push(id);
          this.nodes[peerId].links = this.nodes[peerId].neighbors.length;
        }
      }
    }

    // Database persistence
    try {
      stmts.upsertNode(nodeRecord);
      stmts.insertTelemetry({
        node_id:     id,
        timestamp:   now,
        temperature: nodeRecord.temperature,
        pressure:    nodeRecord.pressure,
        altitude:    nodeRecord.altitude,
        accel_x:     nodeRecord.accel.x,
        accel_y:     nodeRecord.accel.y,
        accel_z:     nodeRecord.accel.z,
        gyro_x:      nodeRecord.gyro.x,
        gyro_y:      nodeRecord.gyro.y,
        gyro_z:      nodeRecord.gyro.z,
        espnow:      1,
        ble:         1,
        wifi:        0,
        neighbors:   JSON.stringify(nodeRecord.neighbors),
        rssi:        nodeRecord.rssi,
      });
    } catch (err) {
      // Ignore DB write conflicts
    }

    if (wasOffline) {
      this._emitEvent({ type: 'NODE_RECONNECTED', nodeId: id });
    }
  }

  // Check for node timeouts (> 12 seconds)
  tickTimeouts() {
    const now = Date.now();
    const events = [];

    for (const [id, node] of Object.entries(this.nodes)) {
      if (node.status === 'ONLINE' && now - (node.lastSeen || 0) > 12000) {
        node.status = 'OFFLINE';
        events.push({ type: 'NODE_DISCONNECTED', nodeId: id });
      }
    }

    return { nodes: this.nodes, events };
  }

  snapshot() {
    return { ...this.nodes };
  }

  getNodes() {
    return Object.values(this.nodes);
  }

  getSettings() {
    return { ...this.settings };
  }

  updateSettings(newSettings) {
    this.settings = { ...this.settings, ...newSettings };
    if (newSettings.groundLevelDatum !== undefined) {
      this.sendDatum(newSettings.groundLevelDatum);
    }
    return { success: true, settings: this.settings };
  }

  sendDatum(datum) {
    const val = parseFloat(datum) || 50.0;
    this.settings.groundLevelDatum = val;
    if (this.port && this.port.isOpen) {
      this.port.write(`SET_DATUM:${val.toFixed(2)}\n`);
      console.log(`[SerialGateway] Sent ground datum -> SET_DATUM:${val.toFixed(2)}`);
    }
  }

  evacuate() {
    this.settings.evacuationActive = true;
    if (this.port && this.port.isOpen) {
      this.port.write('EVACUATE\n');
      console.log('[SerialGateway] Sent emergency evacuation -> EVACUATE');
    }
    return { success: true, status: 'EVACUATE_SENT' };
  }

  muteNode(nodeId = 'ALL') {
    if (this.port && this.port.isOpen) {
      this.port.write(`MUTE:${nodeId}\n`, (err) => {
        if (err) console.error('[SerialGateway] Failed to send MUTE command:', err.message);
        else console.log(`[SerialGateway] Sent MUTE command -> MUTE:${nodeId}`);
      });
      return { success: true, message: `Mute sent to ${nodeId}` };
    }
    return { success: false, message: 'Gateway serial port not connected' };
  }

  cancelEvacuation() {
    this.settings.evacuationActive = false;
    if (this.port && this.port.isOpen) {
      this.port.write('CANCEL_EVACUATE\n');
      console.log('[SerialGateway] Sent cancel evacuation -> CANCEL_EVACUATE');
    }
    return { success: true, status: 'EVACUATION_CLEARED' };
  }

  onEvent(cb) {
    this.eventListeners.push(cb);
  }

  _emitEvent(ev) {
    for (const cb of this.eventListeners) {
      try { cb(ev); } catch (_) {}
    }
  }
}


export default SerialGateway;




