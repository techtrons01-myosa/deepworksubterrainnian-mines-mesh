import React, { useState, useMemo, useCallback } from 'react';
import StatusBadge from './StatusBadge.jsx';
import { fmt, rssiQuality } from '../utils/formatters.js';

function BoolChip({ on }) {
  return (
    <span className={`proto-chip ${on ? 'chip-on' : 'chip-off'}`}>
      {on ? 'ON' : 'OFF'}
    </span>
  );
}

export default function NodeTable({
  nodes = {},
  mutedNodeIds = new Set(),
  onToggleMute = () => {},
  onNodeClick = () => {},
  onLinksClick = () => {},
  settings = {},
}) {
  const [search,  setSearch]  = useState('');
  const [sortKey, setSortKey] = useState('node_id');
  const [sortDir, setSortDir] = useState(1);
  const [filter,  setFilter]  = useState('ALL');

  const getCalibratedAltitude = useCallback((node) => {
    if (!node) return 0;
    const raw = node.altitude ?? 45;
    const datum = parseFloat(settings.groundLevelDatum || 0);
    const offset = settings.nodeCalibrationOffsets?.[node.node_id] ?? 0;
    return raw - datum - offset;
  }, [settings.groundLevelDatum, settings.nodeCalibrationOffsets]);

  const nodeList = useMemo(() => {
    let list = Object.values(nodes).map(n => ({
      ...n,
      altitude: getCalibratedAltitude(n),
    }));

    if (search.trim()) {
      const q = search.toLowerCase();
      list = list.filter(n =>
        n.node_id.toLowerCase().includes(q) ||
        (n.mac && n.mac.toLowerCase().includes(q))
      );
    }

    if (filter === 'ONLINE')  list = list.filter(n => n.status === 'ONLINE');
    if (filter === 'OFFLINE') list = list.filter(n => n.status !== 'ONLINE');

    list.sort((a, b) => {
      let va = a[sortKey];
      let vb = b[sortKey];
      if (typeof va === 'string') return sortDir * va.localeCompare(vb);
      return sortDir * ((va ?? 0) - (vb ?? 0));
    });

    return list;
  }, [nodes, search, filter, sortKey, sortDir, getCalibratedAltitude]);

  const handleSort = (key) => {
    if (sortKey === key) {
      setSortDir(d => -d);
    } else {
      setSortKey(key);
      setSortDir(1);
    }
  };

  const onlineCount  = Object.values(nodes).filter(n => n.status === 'ONLINE').length;
  const offlineCount = Object.values(nodes).filter(n => n.status !== 'ONLINE').length;

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
      {/* Header */}
      <div className="section-header">
        <div>
          <h1 className="section-title">Miner Node Registry</h1>
          <div className="section-sub">REGISTERED SUB-SURFACE RF TELEMETRY UNITS & WEARABLES</div>
        </div>
        <div style={{ display: 'flex', gap: 10, alignItems: 'center' }}>
          <span className="badge badge-online">{onlineCount} ACTIVE ON-SHIFT</span>
          {offlineCount > 0 && (
            <span className="badge badge-offline">{offlineCount} OFFLINE / SURFACE</span>
          )}
        </div>
      </div>

      {/* Filter / Search Toolbar */}
      <div className="table-toolbar">
        <div className="search-box">
          <span style={{ color: 'var(--cyan)' }}>🔍</span>
          <input
            type="text"
            placeholder="Search by ID or MAC..."
            value={search}
            onChange={e => setSearch(e.target.value)}
            className="search-input"
            id="node-table-search"
          />
        </div>

        <div className="tab-bar" style={{ margin: 0 }}>
          {['ALL', 'ONLINE', 'OFFLINE'].map(f => (
            <div
              key={f}
              className={`tab-item ${filter === f ? 'active' : ''}`}
              onClick={() => setFilter(f)}
              id={`filter-${f.toLowerCase()}`}
            >
              {f}
            </div>
          ))}
        </div>
      </div>

      {/* Table */}
      <div className="node-table-wrap">
        <div style={{ overflowX: 'auto' }}>
          <table className="node-table">
            <thead>
              <tr>
                <th onClick={() => handleSort('node_id')} style={{ cursor: 'pointer' }}>ID ↕</th>
                <th onClick={() => handleSort('status')}  style={{ cursor: 'pointer' }}>STATUS ↕</th>
                <th onClick={() => handleSort('temperature')} style={{ cursor: 'pointer' }}>TEMP ↕</th>
                <th onClick={() => handleSort('pressure')}    style={{ cursor: 'pointer' }}>PRESSURE ↕</th>
                <th onClick={() => handleSort('altitude')}    style={{ cursor: 'pointer', color: 'var(--purple)' }}>ALTITUDE ↕</th>
                <th onClick={() => handleSort('rssi')}        style={{ cursor: 'pointer' }}>RSSI ↕</th>
                <th>ESP-NOW</th>
                <th>BLE</th>
                <th>WI-FI</th>
                <th>LINKS</th>
                <th onClick={() => handleSort('uptime')}    style={{ cursor: 'pointer' }}>UPTIME ↕</th>
                <th>LAST SEEN</th>
                <th>ALARM ACTION</th>
              </tr>
            </thead>
            <tbody>
              {nodeList.length === 0 ? (
                <tr>
                  <td colSpan={13} style={{ textAlign: 'center', color: 'var(--text-muted)', padding: '36px 0' }}>
                    No miner nodes match current filter.
                  </td>
                </tr>
              ) : (
                nodeList.map(n => {
                  const isOnline = n.status === 'ONLINE';
                  const q = isOnline ? rssiQuality(n.rssi) : { color: 'var(--text-muted)', label: 'OFFLINE' };
                  const onlineNb = (n.neighbors && n.neighbors.length > 0) ? n.neighbors.length : (isOnline ? 1 : 0);
                  const isMuted = mutedNodeIds.has(n.node_id);

                  return (
                    <tr key={n.node_id} onClick={() => onNodeClick(n)} id={`row-${n.node_id}`} style={{ cursor: 'pointer' }}>
                      <td className="node-id">{n.node_id}</td>
                      <td><StatusBadge status={n.status} /></td>
                      <td style={{ color: 'var(--amber)'  }}>{isOnline ? fmt.temp(n.temperature) : '—'}</td>
                      <td style={{ color: 'var(--blue)'   }}>{isOnline ? fmt.pres(n.pressure)    : '—'}</td>
                      <td style={{ color: 'var(--purple)', fontWeight: 700 }}>{isOnline ? fmt.alt(n.altitude) : '—'}</td>
                      <td style={{ color: isOnline ? q.color : 'var(--text-muted)' }}>
                        {isOnline ? `${fmt.rssi(n.rssi)} · ${q.label}` : '—'}
                      </td>
                      <td><BoolChip on={isOnline && n.espnow} /></td>
                      <td><BoolChip on={isOnline && n.ble}    /></td>
                      <td><BoolChip on={isOnline && n.wifi}   /></td>
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
                            id={`node-table-links-${n.node_id}`}
                          >
                            <span style={{ fontSize: 9, opacity: 0.8 }}>🔗</span>
                            {onlineNb}
                          </button>
                        ) : (
                          <span style={{ color: 'var(--text-muted)' }}>—</span>
                        )}
                      </td>
                      <td style={{ color: 'var(--text-secondary)' }}>
                        {isOnline ? fmt.uptime(n.uptime) : '—'}
                      </td>
                      <td style={{ color: 'var(--text-muted)' }}>
                        {fmt.time(n.lastSeen || n.last_seen)}
                      </td>
                      {/* Mute Node Action Column */}
                      <td>
                        <button
                          type="button"
                          className="btn"
                          style={{
                            padding: '3px 8px',
                            fontSize: 10,
                            fontWeight: 700,
                            background: isMuted ? 'rgba(255, 176, 32, 0.15)' : (isOnline ? 'rgba(255, 255, 255, 0.06)' : 'rgba(255, 23, 68, 0.15)'),
                            color: isMuted ? 'var(--gold)' : (isOnline ? 'var(--text-secondary)' : '#ff5252'),
                            borderColor: isMuted ? 'var(--gold)' : (isOnline ? 'var(--border)' : '#ff1744'),
                            cursor: 'pointer',
                          }}
                          onClick={(e) => {
                            e.stopPropagation();
                            onToggleMute(n.node_id);
                          }}
                          title={isMuted ? "Click to Unmute Alarm for this node" : "Click to Mute Disconnect Alarm for this node"}
                        >
                          {isMuted ? '🔇 Muted' : '🔕 Mute Alarm'}
                        </button>
                      </td>
                    </tr>
                  );
                })
              )}
            </tbody>
          </table>
        </div>
      </div>

      <div style={{ fontSize: 11, color: 'var(--text-muted)', fontFamily: 'monospace' }}>
        Showing {nodeList.length} / {Object.keys(nodes).length} nodes · Muted Nodes: {mutedNodeIds.size}
      </div>
    </div>
  );
}
