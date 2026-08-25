import React from 'react';
import { AreaChart, Area, Tooltip, ResponsiveContainer } from 'recharts';
import { fmt, rssiQuality, healthColor } from '../utils/formatters.js';

function InfoRow({ label, value, valueColor }) {
  return (
    <div className="info-row">
      <div className="info-label">{label}</div>
      <div className="info-value" style={valueColor ? { color: valueColor } : {}}>{value}</div>
    </div>
  );
}

function CommChip({ label, active }) {
  return (
    <div className={`comm-chip ${active ? 'active' : 'inactive'}`}>
      <span>{active ? '●' : '○'}</span>
      {label}
    </div>
  );
}

function HealthBar({ label, value }) {
  const color = healthColor(value);
  return (
    <div className="health-bar-row">
      <div className="health-bar-label">
        <span>{label}</span>
        <span className="health-bar-pct" style={{ color }}>{value}%</span>
      </div>
      <div className="health-bar-track">
        <div className="health-bar-fill" style={{ width: `${value}%`, background: color }} />
      </div>
    </div>
  );
}

function MiniChart({ data, dataKey, color, label }) {
  if (!data || data.length < 2) return <div style={{ color: 'var(--text-muted)', fontSize: 11, padding: '12px 0' }}>Collecting data…</div>;
  return (
    <div>
      <div style={{ fontSize: 10, color: 'var(--text-muted)', marginBottom: 4, fontFamily: 'monospace', letterSpacing: 1 }}>
        {label}
      </div>
      <ResponsiveContainer width="100%" height={60}>
        <AreaChart data={data} margin={{ top: 0, right: 0, bottom: 0, left: 0 }}>
          <defs>
            <linearGradient id={`nd-${dataKey}`} x1="0" y1="0" x2="0" y2="1">
              <stop offset="5%"  stopColor={color} stopOpacity={0.3} />
              <stop offset="95%" stopColor={color} stopOpacity={0} />
            </linearGradient>
          </defs>
          <Area type="monotone" dataKey={dataKey} stroke={color} strokeWidth={1.5}
                fill={`url(#nd-${dataKey})`} dot={false} isAnimationActive={false} />
          <Tooltip
            contentStyle={{ background: 'var(--bg-surface)', border: '1px solid var(--border-bright)', borderRadius: 8, fontSize: 11 }}
            itemStyle={{ color }}
            labelFormatter={() => ''}
            formatter={v => [typeof v === 'number' ? v.toFixed(2) : v, label]}
          />
        </AreaChart>
      </ResponsiveContainer>
    </div>
  );
}

export default function NodeDetail({
  node,
  nodes = {},
  history = [],
  isMuted = false,
  onToggleMute = () => {},
  onSelectNode = () => {},
  onClose = () => {},
}) {
  if (!node) return null;
  const isOnline = node.status === 'ONLINE';
  const q = isOnline ? rssiQuality(node.rssi) : { color: 'var(--text-muted)', label: 'OFFLINE' };

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="modal-panel" onClick={e => e.stopPropagation()} id="node-detail-modal">
        {/* Header */}
        <div className="modal-header">
          <div style={{ display: 'flex', alignItems: 'center', gap: 16 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <span style={{ fontSize: 20 }}>⛏️</span>
              <div className="modal-node-id">{node.node_id}</div>
            </div>
            <span className={`badge ${isOnline ? 'badge-online' : 'badge-offline'}`} style={{ fontSize: 11 }}>
              <span className={`dot ${isOnline ? 'dot-green' : 'dot-red'}`} style={{ width: 7, height: 7 }} />
              {node.status}
            </span>
            {node.alertState === 'CRITICAL' && (
              <span className="badge badge-offline">🚨 HAZARD ALERT</span>
            )}
            {/* Per-Node Mute Button */}
            <button
              type="button"
              className="btn"
              style={{
                padding: '3px 10px',
                fontSize: 10,
                fontWeight: 800,
                background: isMuted ? 'rgba(255, 176, 32, 0.15)' : 'rgba(255, 23, 68, 0.15)',
                color: isMuted ? 'var(--gold)' : '#ff5252',
                borderColor: isMuted ? 'var(--gold)' : '#ff1744',
                cursor: 'pointer',
              }}
              onClick={() => onToggleMute(node.node_id)}
              title={isMuted ? "Node disconnect alarm is MUTED" : "Click to Mute disconnect alarm for this node"}
            >
              {isMuted ? '🔇 ALARM MUTED (CLICK TO UNMUTE)' : '🔕 MUTE DISCONNECT ALARM'}
            </button>
          </div>
          <button className="modal-close" onClick={onClose} id="modal-close-btn">✕</button>
        </div>

        <div className="modal-body">
          <div className="modal-sections">
            {/* Identity */}
            <div className="modal-section">
              <div className="modal-section-title">Miner Identity & Control</div>
              <InfoRow label="Node ID"         value={node.node_id} />
              <InfoRow label="MAC Address"     value={node.mac || '—'} />
              <InfoRow label="Firmware Build"  value={node.firmware ? `v${node.firmware}` : '—'} />
              <InfoRow label="Shift Uptime"    value={isOnline ? fmt.uptime(node.uptime) : '—'} />
              <InfoRow label="Last Telemetry"  value={fmt.datetime(node.lastSeen || node.last_seen)} />
              <InfoRow label="Alarm Status"    value={isMuted ? 'MUTED (Admin)' : 'ACTIVE (Alert Enabled)'}
                        valueColor={isMuted ? 'var(--gold)' : 'var(--green)'} />
            </div>

            {/* Sensors */}
            <div className="modal-section">
              <div className="modal-section-title">Subsurface Sensors</div>
              <InfoRow label="Ambient Temp"   value={isOnline ? fmt.temp(node.temperature) : '—'} valueColor="var(--amber)" />
              <InfoRow label="Baro Pressure"  value={isOnline ? fmt.pres(node.pressure)    : '—'} valueColor="var(--blue)" />
              <InfoRow label="Stope Altitude" value={isOnline ? fmt.alt(node.altitude)     : '—'} valueColor="var(--purple)" />
              <InfoRow label="Accel (X,Y,Z)"
                value={isOnline ? fmt.accel(node.accel?.x ?? 0, node.accel?.y ?? 0, node.accel?.z ?? 0) : '—'} />
              <InfoRow label="Gyro (X,Y,Z)"
                value={isOnline ? fmt.accel(node.gyro?.x ?? 0, node.gyro?.y ?? 0, node.gyro?.z ?? 0) : '—'} />
            </div>

            {/* Location */}
            <div className="modal-section">
              <div className="modal-section-title">Spatial Location & Signal</div>
              <InfoRow label="Subsurface Depth"
                value={isOnline ? `${(node.relative_depth ?? 0).toFixed(1)} m below surface` : '—'}
                valueColor="var(--gold)" />
              <InfoRow label="Grid (X, Y)"
                value={node.position ? `(${fmt.pos(node.position.x)}, ${fmt.pos(node.position.y)})` : '—'} />
              <InfoRow label="Signal RSSI"
                value={isOnline ? `${fmt.rssi(node.rssi)} · ${q.label}` : '—'}
                valueColor={q.color} />
              <InfoRow label="Battery Charge"
                value={isOnline ? fmt.pct(node.batteryPct) : '—'}
                valueColor="var(--green)" />
            </div>
          </div>

          {/* Sparklines */}
          {history.length >= 2 && (
            <div className="modal-charts-grid" style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 14, marginTop: 18 }}>
              <MiniChart data={history} dataKey="temperature" color="var(--amber)"  label="TEMPERATURE (°C)" />
              <MiniChart data={history} dataKey="pressure"    color="var(--blue)"   label="PRESSURE (hPa)" />
              <MiniChart data={history} dataKey="altitude"    color="var(--purple)" label="STOPE DEPTH (m)" />
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
