/**
 * db.js — SQLite database layer using sql.js (pure JS, no native compilation needed)
 *
 * sql.js keeps the DB in memory and serialises it to disk periodically.
 * This is fine for our monitoring use-case (non-critical persistence, mostly
 * for historical chart data and alert logs).
 */
import initSqlJs from 'sql.js';
import { readFileSync, writeFileSync, existsSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const DB_PATH   = join(__dirname, 'myosa.db');

// ── Initialise sql.js and load/create the database ───────────────────────────
const SQL  = await initSqlJs();
const data = existsSync(DB_PATH) ? readFileSync(DB_PATH) : null;
export const db = new SQL.Database(data);

// Persist to disk every 30 seconds
setInterval(() => {
  try {
    writeFileSync(DB_PATH, db.export());
  } catch (_) { /* ignore */ }
}, 30_000);

// ── Schema ─────────────────────────────────────────────────────────────────────
db.run(`
  CREATE TABLE IF NOT EXISTS nodes (
    node_id     TEXT PRIMARY KEY,
    mac         TEXT,
    firmware    TEXT DEFAULT '1.2.3',
    status      TEXT DEFAULT 'OFFLINE',
    last_seen   INTEGER,
    configuration TEXT DEFAULT '{}'
  );

  CREATE TABLE IF NOT EXISTS telemetry (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    node_id     TEXT,
    timestamp   INTEGER,
    temperature REAL,
    pressure    REAL,
    altitude    REAL,
    accel_x     REAL,
    accel_y     REAL,
    accel_z     REAL,
    gyro_x      REAL,
    gyro_y      REAL,
    gyro_z      REAL,
    espnow      INTEGER,
    ble         INTEGER,
    wifi        INTEGER,
    neighbors   TEXT,
    rssi        INTEGER
  );

  CREATE TABLE IF NOT EXISTS locations (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    node_id     TEXT,
    timestamp   INTEGER,
    x           REAL,
    y           REAL,
    confidence  REAL
  );

  CREATE TABLE IF NOT EXISTS connections (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    node_a      TEXT,
    node_b      TEXT,
    technology  TEXT,
    rssi        INTEGER,
    timestamp   INTEGER
  );

  CREATE TABLE IF NOT EXISTS alerts (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    node_id      TEXT,
    type         TEXT,
    severity     TEXT,
    message      TEXT,
    timestamp    INTEGER,
    acknowledged INTEGER DEFAULT 0
  );

  CREATE INDEX IF NOT EXISTS idx_tel_node ON telemetry (node_id, timestamp);
  CREATE INDEX IF NOT EXISTS idx_alt_ts   ON alerts    (timestamp);
`);

// ── Thin wrapper to run a query with named params ─────────────────────────────
function run(sql, params = {}) {
  db.run(sql, params);
}

function all(sql, params = []) {
  const stmt = db.prepare(sql);
  stmt.bind(params);
  const rows = [];
  while (stmt.step()) rows.push(stmt.getAsObject());
  stmt.free();
  return rows;
}

function get(sql, params = []) {
  return all(sql, params)[0] ?? null;
}

// ── Public statement helpers ───────────────────────────────────────────────────
export const stmts = {
  upsertNode: (p) => run(`
    INSERT INTO nodes (node_id, mac, firmware, status, last_seen)
    VALUES (:node_id, :mac, :firmware, :status, :last_seen)
    ON CONFLICT(node_id) DO UPDATE SET
      status    = excluded.status,
      last_seen = excluded.last_seen
  `, p),

  getNode:    (id)  => get('SELECT * FROM nodes WHERE node_id = ?', [id]),
  getAllNodes: ()    => all('SELECT * FROM nodes ORDER BY node_id'),

  insertTelemetry: (p) => run(`
    INSERT INTO telemetry
      (node_id, timestamp, temperature, pressure, altitude,
       accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z,
       espnow, ble, wifi, neighbors, rssi)
    VALUES
      (:node_id, :timestamp, :temperature, :pressure, :altitude,
       :accel_x, :accel_y, :accel_z, :gyro_x, :gyro_y, :gyro_z,
       :espnow, :ble, :wifi, :neighbors, :rssi)
  `, p),

  getNodeTelemetry: (nodeId, limit) => all(
    'SELECT * FROM telemetry WHERE node_id = ? ORDER BY timestamp DESC LIMIT ?',
    [nodeId, limit]
  ),

  insertLocation: (p) => run(`
    INSERT INTO locations (node_id, timestamp, x, y, confidence)
    VALUES (:node_id, :timestamp, :x, :y, :confidence)
  `, p),

  insertAlert: (p) => run(`
    INSERT INTO alerts (node_id, type, severity, message, timestamp)
    VALUES (:node_id, :type, :severity, :message, :timestamp)
  `, p),

  getAlerts: () => all('SELECT * FROM alerts ORDER BY timestamp DESC LIMIT 200'),

  acknowledgeAlert: (id) => run('UPDATE alerts SET acknowledged = 1 WHERE id = ?', [id]),
};

export default db;
