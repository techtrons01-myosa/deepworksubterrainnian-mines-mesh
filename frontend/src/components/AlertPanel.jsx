import React, { useState } from 'react';
import { fmt, severityIcon } from '../utils/formatters.js';

const FILTERS = ['ALL', 'CRITICAL', 'WARNING', 'INFO'];

export default function AlertPanel({ alerts = [] }) {
  const [filter, setFilter]  = useState('ALL');
  const [acked,  setAcked]   = useState(new Set());

  const visible = alerts.filter(a =>
    (filter === 'ALL' || a.severity === filter) && !acked.has(a.id)
  );

  const critCount = alerts.filter(a => a.severity === 'CRITICAL' && !acked.has(a.id)).length;
  const warnCount = alerts.filter(a => a.severity === 'WARNING'  && !acked.has(a.id)).length;

  const acknowledge = (id, e) => {
    e.stopPropagation();
    setAcked(prev => new Set([...prev, id]));
    fetch(`/api/alerts/${id}/acknowledge`, { method: 'POST' }).catch(() => {});
  };

  const acknowledgeAll = () => {
    const ids = visible.map(a => a.id);
    setAcked(prev => new Set([...prev, ...ids]));
  };

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 20 }}>
      <div className="section-header">
        <div>
          <h1 className="section-title">Subsurface Hazard & Safety Log</h1>
          <div className="section-sub">REAL-TIME TELEMETRY ALERTS — ENVIRONMENTAL · GAS · DISCONNECTION · BOUNDARY</div>
        </div>
        <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
          {critCount > 0 && <span className="badge badge-offline">🚨 {critCount} CRITICAL HAZARDS</span>}
          {warnCount > 0 && <span className="badge badge-warning">⚠️ {warnCount} WARNINGS</span>}
          {visible.length > 0 && (
            <button className="btn btn-gold" onClick={acknowledgeAll} id="ack-all-btn">
              ✓ Acknowledge All
            </button>
          )}
        </div>
      </div>

      <div className="tab-bar">
        {FILTERS.map(f => (
          <div
            key={f}
            className={`tab-item ${filter === f ? 'active' : ''}`}
            onClick={() => setFilter(f)}
          >
            {f} {f === 'CRITICAL' && critCount > 0 ? `(${critCount})` : ''}
          </div>
        ))}
      </div>

      <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
        {visible.length === 0 ? (
          <div className="empty-state" style={{ padding: '60px 20px', background: 'var(--bg-card)', borderRadius: 'var(--radius-md)' }}>
            <div style={{ fontSize: 32 }}>🛡️</div>
            <div style={{ fontSize: 14, color: 'var(--text-secondary)', marginTop: 8 }}>
              No active alerts matching filter. All subsurface zones reporting normal status.
            </div>
          </div>
        ) : (
          visible.map(a => (
            <div
              key={a.id}
              className={`card alert-item alert-${a.severity?.toLowerCase() || 'info'}`}
              style={{
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'space-between',
                padding: '14px 18px',
                borderLeft: a.severity === 'CRITICAL' ? '4px solid #ff1744' : (a.severity === 'WARNING' ? '4px solid var(--gold)' : '4px solid var(--cyan)'),
              }}
            >
              <div style={{ display: 'flex', alignItems: 'center', gap: 14 }}>
                <span style={{ fontSize: 20 }}>{severityIcon(a.severity)}</span>
                <div>
                  <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: 2 }}>
                    <span style={{ fontWeight: 800, fontSize: 13, color: a.severity === 'CRITICAL' ? '#ff5252' : '#ffffff' }}>
                      {a.type || 'HAZARD_ALERT'}
                    </span>
                    {a.nodeId && (
                      <span className="badge badge-info" style={{ fontFamily: 'var(--font-mono)', fontSize: 10 }}>
                        {a.nodeId}
                      </span>
                    )}
                    <span style={{ fontSize: 11, color: 'var(--text-muted)', fontFamily: 'var(--font-mono)' }}>
                      {fmt.time(a.timestamp)}
                    </span>
                  </div>
                  <div style={{ fontSize: 12, color: 'var(--text-secondary)' }}>{a.message}</div>
                </div>
              </div>

              <button
                className="btn btn-ghost"
                style={{ fontSize: 11, padding: '4px 10px' }}
                onClick={(e) => acknowledge(a.id, e)}
              >
                Dismiss
              </button>
            </div>
          ))
        )}
      </div>
    </div>
  );
}
