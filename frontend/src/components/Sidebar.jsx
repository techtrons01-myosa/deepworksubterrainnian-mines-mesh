import React from 'react';

const NAV = [
  { id: 'dashboard', icon: '⛏️', label: 'Mine Overview' },
  { id: 'map',       icon: '🗺️', label: 'Subsurface Map' },
  { id: 'nodes',     icon: '👥', label: 'Miner Nodes' },
  { id: 'alerts',    icon: '🚨', label: 'Hazard Alerts' },
  { id: 'charts',    icon: '📊', label: 'Telemetry Charts' },
];

export default function Sidebar({
  view,
  setView,
  critCount = 0,
  health = {},
  connected = true,
  onOpenSettings = () => {},
  settings = {},
}) {
  return (
    <aside className="sidebar">
      <div className="sidebar-section-label">Operations Nav</div>

      {NAV.map(item => (
        <div
          key={item.id}
          id={`nav-${item.id}`}
          className={`nav-item ${view === item.id ? 'active' : ''}`}
          onClick={() => setView(item.id)}
          role="button"
          tabIndex={0}
          onKeyDown={e => e.key === 'Enter' && setView(item.id)}
        >
          <span className="nav-icon">{item.icon}</span>
          {item.label}
          {item.id === 'alerts' && critCount > 0 && (
            <span className="nav-badge">{critCount}</span>
          )}
        </div>
      ))}

      {/* Settings Navigation Item */}
      <div
        id="nav-settings"
        className="nav-item"
        style={{ marginTop: 6, borderColor: 'rgba(255, 176, 32, 0.2)', color: 'var(--gold)' }}
        onClick={onOpenSettings}
        role="button"
        tabIndex={0}
        onKeyDown={e => e.key === 'Enter' && onOpenSettings()}
      >
        <span className="nav-icon" style={{ filter: 'drop-shadow(0 0 5px var(--gold))', color: 'var(--gold)' }}>⚙️</span>
        Calibration & Geofence
        <span style={{
          marginLeft: 'auto',
          fontSize: 9,
          padding: '2px 5px',
          borderRadius: 3,
          background: 'rgba(255, 176, 32, 0.15)',
          color: 'var(--gold)',
          fontFamily: 'var(--font-hud)',
          fontWeight: 800
        }}>
          CONFIG
        </span>
      </div>

      <div className="sidebar-footer">
        <div className="sidebar-section-label" style={{ paddingTop: 0 }}>Perimeter & Safety</div>

        <div className="gateway-status">
          <div className={`dot ${connected ? 'dot-green' : 'dot-red'}`} style={{ width: 6, height: 6 }} />
          <span style={{ fontSize: 11, fontWeight: 700 }}>Gateway Stope 01</span>
          <span style={{ marginLeft: 'auto', color: connected ? 'var(--green)' : 'var(--red)', fontSize: 10, fontWeight: 800 }}>
            {connected ? 'ONLINE' : 'OFFLINE'}
          </span>
        </div>

        {/* Mesh Boundary & Buzzer Info Chips */}
        <div style={{
          marginTop: 8,
          display: 'grid',
          gridTemplateColumns: '1fr 1fr',
          gap: 6,
          fontSize: 9,
          fontFamily: 'var(--font-mono)'
        }}>
          <div style={{
            background: 'var(--bg-deep)',
            border: '1px solid var(--border)',
            borderRadius: 4,
            padding: '5px 7px',
            color: 'var(--text-secondary)'
          }}>
            <div style={{ color: 'var(--text-muted)', fontSize: 8, fontFamily: 'var(--font-hud)' }}>GEOFENCE</div>
            <div style={{ color: 'var(--gold)', fontWeight: 800 }}>{settings.geofenceRadius ?? 50}m</div>
          </div>
          <div style={{
            background: 'var(--bg-deep)',
            border: '1px solid var(--border)',
            borderRadius: 4,
            padding: '5px 7px',
            color: 'var(--text-secondary)'
          }}>
            <div style={{ color: 'var(--text-muted)', fontSize: 8, fontFamily: 'var(--font-hud)' }}>SIREN/BUZZER</div>
            <div style={{ color: settings.buzzerEnabled !== false ? 'var(--green)' : 'var(--red)', fontWeight: 800 }}>
              {settings.buzzerEnabled !== false ? 'ACTIVE 🔊' : 'MUTED 🔇'}
            </div>
          </div>
        </div>

        <div style={{ marginTop: 10, display: 'flex', flexDirection: 'column', gap: 4 }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 10, color: 'var(--text-secondary)', fontFamily: 'var(--font-hud)' }}>
            <span>MESH INTEGRITY</span>
            <span style={{ color: 'var(--gold)', fontFamily: 'var(--font-mono)', fontWeight: 800 }}>
              {health.health ?? '--'}%
            </span>
          </div>
          <div style={{ height: 4, background: 'rgba(0, 0, 0, 0.4)', borderRadius: 2, overflow: 'hidden', border: '1px solid var(--border)' }}>
            <div style={{
              height: '100%',
              width: `${health.health ?? 0}%`,
              background: 'linear-gradient(90deg, var(--gold), var(--green))',
              transition: 'width 1s ease',
            }} />
          </div>
        </div>

        <div style={{ marginTop: 10, display: 'flex', justifyContent: 'space-between', fontSize: 9, color: 'var(--text-muted)', fontFamily: 'var(--font-hud)' }}>
          <span>DEEPWORKS CORE</span>
          <span style={{ color: 'var(--gold)', fontWeight: 800 }}>v2.4.0</span>
        </div>
      </div>
    </aside>
  );
}
