import React, { useMemo, useCallback } from 'react';
import { AreaChart, Area, ResponsiveContainer } from 'recharts';
import StatusBadge from './StatusBadge.jsx';
import { fmt, rssiQuality } from '../utils/formatters.js';

function StatCard({ icon, value, label, color, accent }) {
  return (
    <div className={`stat-card stat-${accent}`}>
      <div className="stat-card-header">
        <span className="stat-card-icon">{icon}</span>
        <span style={{ fontSize: 9, fontFamily: 'var(--font-hud)', color: 'var(--text-muted)', fontWeight: 800 }}>TELEMETRY</span>
      </div>
      <div>
        <div className="stat-card-value" style={{ color: color || '#ffffff' }}>{value}</div>
        <div className="stat-card-label">{label}</div>
      </div>
    </div>
  );
}

function MiniSparkline({ data, dataKey, color }) {
  if (!data || data.length < 2) return null;
  return (
    <ResponsiveContainer width="100%" height={40}>
      <AreaChart data={data} margin={{ top: 0, right: 0, bottom: 0, left: 0 }}>
        <defs>
          <linearGradient id={`sg-${dataKey}`} x1="0" y1="0" x2="0" y2="1">
            <stop offset="5%"  stopColor={color} stopOpacity={0.35} />
            <stop offset="95%" stopColor={color} stopOpacity={0} />
          </linearGradient>
        </defs>
        <Area type="monotone" dataKey={dataKey} stroke={color} strokeWidth={1.7}
              fill={`url(#sg-${dataKey})`} dot={false} isAnimationActive={false} />
      </AreaChart>
    </ResponsiveContainer>
  );
}

export default function Dashboard({ nodes = {}, health = {}, alerts = [], history = {}, onNodeClick = () => {}, onLinksClick = () => {}, settings = {} }) {
  const getCalibratedAltitude = useCallback((node) => {
    if (!node) return 0;
    const raw = node.altitude ?? 45;
    const datum = parseFloat(settings.groundLevelDatum || 0);
    const offset = settings.nodeCalibrationOffsets?.[node.node_id] ?? 0;
    return raw - datum - offset;
  }, [settings.groundLevelDatum, settings.nodeCalibrationOffsets]);

  const nodeList = useMemo(() => {
    return Object.values(nodes).map(n => ({
      ...n,
      altitude: getCalibratedAltitude(n),
    }));
  }, [nodes, getCalibratedAltitude]);

  const online  = nodeList.filter(n => n.status === 'ONLINE');
  const offline = nodeList.filter(n => n.status !== 'ONLINE');

  const avgTemp = online.length
    ? (online.reduce((s, n) => s + (n.temperature || 0), 0) / online.length).toFixed(1)
    : '--';

  const avgAlt = online.length
    ? (online.reduce((s, n) => s + (n.altitude || 0), 0) / online.length).toFixed(1)
    : '--';

  const avgPres = online.length
    ? (online.reduce((s, n) => s + (n.pressure || 0), 0) / online.length).toFixed(1)
    : '--';

  const recentAlerts = alerts.slice(0, 8);

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 20 }}>
      <div className="section-header">
        <div>
          <h1 className="section-title">Mine Operations Overview</h1>
          <div className="section-sub">DEEPWORKS SUB-SURFACE TELEMETRY & WORKER SAFETY GRID</div>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
          <div className="dot dot-amber" />
          <span style={{ fontSize: 11, color: 'var(--gold)', fontFamily: 'var(--font-hud)', fontWeight: 800, letterSpacing: 1 }}>
            LIVE TELEMETRY ACTIVE
          </span>
        </div>
      </div>

      {/* Stat Cards Grid */}
      <div className="stat-grid">
        <StatCard icon="👥" value={health.total  ?? '--'} label="Total Miners" color="#ffffff" accent="amber" />
        <StatCard icon="⛏️" value={health.online  ?? '--'} label="Active On-Shift" color="var(--green)" accent="green" />
        <StatCard icon="📴" value={health.offline ?? '--'} label="Surface / Offline" color="var(--red)" accent="red" />
        <StatCard icon="🏔️" value={avgAlt !== '--' ? `${avgAlt}m` : '--'} label="Mean Elevation" color="var(--purple)" accent="purple" />
        <StatCard icon="📶" value={`${health.health ?? '--'}%`} label="Mesh Integrity" color="var(--gold)" accent="amber" />
        <StatCard icon="🔗" value={health.links   ?? '--'} label="Active RF Links" color="var(--cyan)" accent="cyan" />
      </div>

      {/* Main two-column Layout */}
      <div className="dash-grid">
        {/* Node quick-list */}
        <div className="card">
          <div className="card-header">
            <span className="card-title">Active Miner Node Telemetry</span>
            <span style={{ fontSize: 11, color: 'var(--gold)', fontFamily: 'var(--font-hud)', fontWeight: 800 }}>
              {online.length}/{nodeList.length} MINERS ONLINE
            </span>
          </div>
          <div className="card-body" style={{ padding: 0 }}>
            <table className="node-table" style={{ fontSize: 12 }}>
              <thead>
                <tr>
                  <th>MINER ID</th>
                  <th>STATUS</th>
                  <th>TEMP</th>
                  <th style={{ color: 'var(--purple)' }}>STOPE ELEVATION</th>
                  <th>PRESSURE</th>
                  <th>SIGNAL RSSI</th>
                  <th>MESH LINKS</th>
                  <th>BATTERY</th>
                </tr>
              </thead>
              <tbody>
                {nodeList.map(n => {
                  const isOnline = n.status === 'ONLINE';
                  const q = isOnline ? rssiQuality(n.rssi) : { color: 'var(--text-muted)', label: 'OFFLINE' };
                  const onlineNb = (n.neighbors && n.neighbors.length > 0) ? n.neighbors.length : (isOnline ? 1 : 0);

                  return (
                    <tr
                      key={n.node_id}
                      onClick={() => onNodeClick(n)}
                      style={{ cursor: 'pointer' }}
                      id={`dash-row-${n.node_id}`}
                    >
                      <td className="node-id">{n.node_id}</td>
                      <td><StatusBadge status={n.status} /></td>
                      <td style={{ color: 'var(--amber)' }}>{isOnline ? fmt.temp(n.temperature) : '—'}</td>
                      <td style={{ color: 'var(--purple)', fontWeight: 700 }}>
                        {isOnline ? fmt.alt(n.altitude) : '—'}
                      </td>
                      <td style={{ color: 'var(--blue)' }}>{isOnline ? fmt.pres(n.pressure) : '—'}</td>
                      <td style={{ color: isOnline ? q.color : 'var(--text-muted)' }}>
                        {isOnline ? `${fmt.rssi(n.rssi)} · ${q.label}` : '—'}
                      </td>
                      <td>
                        {isOnline ? (
                          <button
                            type="button"
                            className="links-count-btn"
                            onClick={(e) => {
                              e.stopPropagation();
                              onLinksClick(n);
                            }}
                            title={`Click to inspect ${onlineNb} linked nodes connected to ${n.node_id}`}
                            id={`dash-links-${n.node_id}`}
                          >
                            <span style={{ fontSize: 9, opacity: 0.8 }}>🔗</span>
                            {onlineNb}
                          </button>
                        ) : (
                          <span style={{ color: 'var(--text-muted)' }}>—</span>
                        )}
                      </td>
                      <td style={{ color: isOnline ? 'var(--green)' : 'var(--text-muted)' }}>
                        {isOnline ? fmt.pct(n.batteryPct) : '—'}
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          </div>
        </div>

        {/* Hazard alerts list */}
        <div className="card">
          <div className="card-header">
            <span className="card-title">Recent Sub-surface Alerts</span>
            <span style={{ fontSize: 11, color: 'var(--text-muted)', fontFamily: 'var(--font-hud)' }}>
              {recentAlerts.length} RECORDED
            </span>
          </div>
          <div className="card-body" style={{ padding: '8px 12px', display: 'flex', flexDirection: 'column', gap: 8 }}>
            {recentAlerts.length === 0 ? (
              <div className="empty-state" style={{ padding: '30px 0' }}>
                <div style={{ fontSize: 24 }}>🛡️</div>
                <div style={{ fontSize: 12, color: 'var(--text-muted)' }}>All tunnels safe. Zero active hazards.</div>
              </div>
            ) : (
              recentAlerts.map((a, idx) => (
                <div key={idx} className={`alert-item alert-${a.severity?.toLowerCase() || 'info'}`} style={{ padding: '8px 12px' }}>
                  <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 2 }}>
                    <span style={{ fontWeight: 800, fontSize: 11, color: a.severity === 'CRITICAL' ? '#ff5252' : 'var(--gold)' }}>
                      {a.severity === 'CRITICAL' ? '🚨 CRITICAL' : '⚠️ WARNING'}: {a.type || 'HAZARD'}
                    </span>
                    <span style={{ fontSize: 10, color: 'var(--text-muted)', fontFamily: 'var(--font-mono)' }}>
                      {fmt.time(a.timestamp)}
                    </span>
                  </div>
                  <div style={{ fontSize: 11, color: 'var(--text-secondary)' }}>{a.message}</div>
                </div>
              ))
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
