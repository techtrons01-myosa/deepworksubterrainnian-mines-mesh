import express        from 'express';
import { WebSocketServer } from 'ws';
import { createServer } from 'http';
import cors            from 'cors';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import HardwareSerialGateway from './serialGateway.js';
import AlertManager     from './alertManager.js';
import HealthMonitor    from './healthMonitor.js';
import MeshTopology     from './meshTopology.js';
import apiRouter        from './routes/api.js';

const __dirname = dirname(fileURLToPath(import.meta.url));

// Express + HTTP
const app = express();
app.use(cors({ origin: '*' }));
app.use(express.json());

// Serve built frontend assets
const FRONTEND_DIST = join(__dirname, '../frontend/dist');
app.use(express.static(FRONTEND_DIST));

const server = createServer(app);

// WebSocket
const wss = new WebSocketServer({ server });

function broadcast(data) {
  const msg = JSON.stringify(data);
  wss.clients.forEach(ws => { if (ws.readyState === 1) ws.send(msg); });
}

// Engines
const gateway = new HardwareSerialGateway('COM7', 115200);
const alerts  = new AlertManager();
const health  = new HealthMonitor();
const topo    = new MeshTopology();

// Attach engines to request
app.use((req, _res, next) => {
  req.gateway = gateway;
  req.sim     = gateway; // Compatibility alias
  req.alerts  = alerts;
  req.topo    = topo;
  req.health  = health;
  next();
});

app.use('/api', apiRouter);

// Fallback for SPA routing
app.get('*', (req, res, next) => {
  if (req.path.startsWith('/api') || req.path.startsWith('/ws')) return next();
  res.sendFile(join(FRONTEND_DIST, 'index.html'));
});

// Alert listener -> push immediately to all clients
alerts.onAlert(alert => broadcast({ type: 'ALERT', payload: alert }));

// Gateway event listener
gateway.onEvent(ev => {
  if (ev.type === 'NODE_DISCONNECTED') {
    alerts.fire(ev.nodeId, 'NODE_DISCONNECTED', 'CRITICAL', `Node ${ev.nodeId} disconnected from mesh`);
  } else if (ev.type === 'NODE_RECONNECTED') {
    alerts.fire(ev.nodeId, 'NODE_RECONNECTED', 'INFO', `Node ${ev.nodeId} reconnected to mesh`);
  } else if (ev.type === 'ALERT') {
    alerts.fire(ev.nodeId || 'GATEWAY', 'HARDWARE_ALERT', 'WARNING', ev.message || 'Node Alert');
  }
});

// Periodic Mesh Update Loop (1 Hz)
setInterval(() => {
  const { nodes, events } = gateway.tickTimeouts();

  // Check for weak RSSI warnings
  for (const [id, n] of Object.entries(nodes)) {
    if (n.status !== 'ONLINE') continue;
    if (n.rssi < -82) {
      alerts.fire(id, 'WEAK_SIGNAL', 'WARNING', `Node ${id} weak signal (${n.rssi} dBm)`);
    }
  }

  const links = topo.update(nodes);
  const stats = health.compute(nodes);

  broadcast({
    type: 'STATE_UPDATE',
    payload: {
      nodes,
      links,
      health:    stats,
      alerts:    alerts.getAll().slice(0, 30),
      timestamp: Date.now(),
    },
  });
}, 1000);

// Send snapshot on first connect
wss.on('connection', ws => {
  const nodes = gateway.snapshot();
  const links = topo.getLinks();
  const stats = health.compute(nodes);

  ws.send(JSON.stringify({
    type: 'STATE_UPDATE',
    payload: { nodes, links, health: stats, alerts: alerts.getAll(), timestamp: Date.now() },
  }));

  console.log(`[WS] Client connected (total: ${wss.clients.size})`);
});

// Start
const PORT = process.env.PORT || 3001;
server.listen(PORT, () => {
  console.log('');
  console.log('  ======================================================');
  console.log('    DEEPWORKS MINING — MYOSA HARDWARE CENTRAL GATEWAY');
  console.log('  ======================================================');
  console.log(`  🚀  Application -> http://localhost:${PORT}`);
  console.log(`  📡  WebSocket   -> ws://localhost:${PORT}`);
  console.log(`  🔌  Gateway     -> ESP32-S3 Zero on COM7 (115200 baud)`);
  console.log('  ✅  0 Demo values: Real Sub-surface RF Mesh Active');
  console.log('');
  gateway.start();
});

