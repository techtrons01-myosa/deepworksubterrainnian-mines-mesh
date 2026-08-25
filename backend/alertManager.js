import { stmts } from './db.js';

const DEDUP_MS = 30_000;

class AlertManager {
  constructor() {
    this._recent    = new Map();
    this._listeners = [];
    this.latestAlerts = [];
  }

  onAlert(fn) { this._listeners.push(fn); }

  fire(nodeId, type, severity, message) {
    const key = `${nodeId}:${type}`;
    const now = Date.now();

    if (this._recent.has(key) && now - this._recent.get(key) < DEDUP_MS) return null;
    this._recent.set(key, now);

    const alert = { id: now + Math.random(), node_id: nodeId, type, severity, message, timestamp: now, acknowledged: 0 };

    try {
      stmts.insertAlert({ ':node_id': nodeId, ':type': type, ':severity': severity, ':message': message, ':timestamp': now });
    } catch (_) {}

    this.latestAlerts.unshift(alert);
    if (this.latestAlerts.length > 100) this.latestAlerts.pop();

    this._listeners.forEach(fn => fn(alert));
    return alert;
  }

  getAll() { return this.latestAlerts; }
}

export default AlertManager;
