import React, { useState, useMemo } from 'react';
import {
  AreaChart, Area, LineChart, Line,
  XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer,
} from 'recharts';
import { fmt } from '../utils/formatters.js';

const TABS = [
  { id: 'environmental', label: '🌱 Environmental' },
  { id: 'motion',        label: '⚡ Motion' },
  { id: 'network',       label: '📡 Network' },
];

const CHART_STYLE = {
  background: 'var(--bg-surface)',
  border: '1px solid var(--border-bright)',
  borderRadius: 10,
  fontSize: 11,
  color: 'var(--text-primary)',
  padding: '8px 12px',
};

const TICK_STYLE = { fill: 'var(--text-muted)', fontSize: 10, fontFamily: 'JetBrains Mono, monospace' };

function CustomTooltip({ active, payload, label, unit }) {
  if (!active || !payload?.length) return null;
  return (
    <div style={CHART_STYLE} className="map-tooltip">
      <div style={{ fontSize: 10, color: 'var(--text-muted)', marginBottom: 6 }}>{label}</div>
      {payload.map(p => (
        <div key={p.name} style={{ color: p.color, fontSize: 12, fontFamily: 'monospace' }}>
          {p.name}: {typeof p.value === 'number' ? p.value.toFixed(2) : p.value} {unit || ''}
        </div>
      ))}
    </div>
  );
}

function ChartCard({ title, children }) {
  return (
    <div className="card" style={{ marginBottom: 20 }}>
      <div className="card-header">
        <span className="card-title">{title}</span>
      </div>
      <div className="card-body" style={{ height: 220, padding: '12px 16px 4px 4px' }}>
        {children}
      </div>
    </div>
  );
}

export default function ChartPanel({ nodes = {}, history = {}, settings = {} }) {
  const [activeTab, setActiveTab] = useState('environmental');
  const [selectedId, setSelectedId] = useState('');

  const nodeIds = Object.keys(nodes);
  const currentId = selectedId || nodeIds[0] || '';
  const node = nodes[currentId];
  const rawHistory = history[currentId] || [];

  const chartData = useMemo(() => {
    const datum = parseFloat(settings.groundLevelDatum || 0);
    const offset = settings.nodeCalibrationOffsets?.[currentId] ?? 0;

    return rawHistory.map(entry => ({
      time:        fmt.time(entry.timestamp),
      temperature: entry.temperature,
      pressure:    entry.pressure,
      altitude:    typeof entry.altitude === 'number' ? (entry.altitude - datum - offset) : null,
      rssi:        entry.rssi,
      accel_x:     entry.accel_x,
      accel_y:     entry.accel_y,
      accel_z:     entry.accel_z,
      gyro_x:      entry.gyro_x,
      gyro_y:      entry.gyro_y,
      gyro_z:      entry.gyro_z,
    }));
  }, [rawHistory, settings, currentId]);

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 20 }}>
      <div className="section-header">
        <div>
          <h1 className="section-title">Subsurface Telemetry Charts</h1>
          <div className="section-sub">
            REAL-TIME ROLLING SENSOR & MESH RF METRICS — {rawHistory.length} HISTORICAL SAMPLES
          </div>
        </div>

        {/* Tab switcher + node picker */}
        <div style={{ display: 'flex', gap: 10, alignItems: 'center', flexWrap: 'wrap' }}>
          <div className="tab-bar" style={{ margin: 0 }}>
            {TABS.map(t => (
              <div
                key={t.id}
                className={`tab-item ${activeTab === t.id ? 'active' : ''}`}
                onClick={() => setActiveTab(t.id)}
              >
                {t.label}
              </div>
            ))}
          </div>

          <div style={{ display: 'flex', alignItems: 'center', gap: 8, background: 'var(--bg-card)', border: '1px solid var(--border)', borderRadius: 6, padding: '4px 10px' }}>
            <select
              value={currentId}
              onChange={e => setSelectedId(e.target.value)}
              className="form-select"
              style={{ background: 'transparent', border: 'none', color: 'var(--gold)', fontWeight: 800, cursor: 'pointer', outline: 'none' }}
              id="chart-node-select"
            >
              {nodeIds.map(id => (
                <option key={id} value={id} style={{ background: '#11141a', color: '#ffffff' }}>
                  {id} {nodes[id]?.status === 'ONLINE' ? '🟢' : '🔴'}
                </option>
              ))}
            </select>
            {node && (
              <span style={{ fontSize: 10, color: 'var(--text-muted)', fontFamily: 'monospace' }}>
                MAC: {node.mac}
              </span>
            )}
          </div>
        </div>
      </div>

      {/* Environmental tab */}
      {activeTab === 'environmental' && (
        <>
          <ChartCard title="TEMPERATURE (°C)">
            <ResponsiveContainer width="100%" height="100%">
              <AreaChart data={chartData}>
                <defs>
                  <linearGradient id="g-temp" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="5%"  stopColor="var(--gold)" stopOpacity={0.3} />
                    <stop offset="95%" stopColor="var(--gold)" stopOpacity={0} />
                  </linearGradient>
                </defs>
                <CartesianGrid strokeDasharray="3 3" stroke="rgba(255,255,255,0.04)" />
                <XAxis dataKey="time" tick={TICK_STYLE} />
                <YAxis domain={['auto', 'auto']} tick={TICK_STYLE} unit="°C" />
                <Tooltip content={<CustomTooltip unit="°C" />} />
                <Area type="monotone" dataKey="temperature" name="Temp" stroke="var(--gold)" strokeWidth={2}
                      fill="url(#g-temp)" isAnimationActive={false} />
              </AreaChart>
            </ResponsiveContainer>
          </ChartCard>

          <ChartCard title="PRESSURE (hPa)">
            <ResponsiveContainer width="100%" height="100%">
              <AreaChart data={chartData}>
                <defs>
                  <linearGradient id="g-pres" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="5%"  stopColor="var(--blue)" stopOpacity={0.3} />
                    <stop offset="95%" stopColor="var(--blue)" stopOpacity={0} />
                  </linearGradient>
                </defs>
                <CartesianGrid strokeDasharray="3 3" stroke="rgba(255,255,255,0.04)" />
                <XAxis dataKey="time" tick={TICK_STYLE} />
                <YAxis domain={['auto', 'auto']} tick={TICK_STYLE} unit=" hPa" />
                <Tooltip content={<CustomTooltip unit="hPa" />} />
                <Area type="monotone" dataKey="pressure" name="Pressure" stroke="var(--blue)" strokeWidth={2}
                      fill="url(#g-pres)" isAnimationActive={false} />
              </AreaChart>
            </ResponsiveContainer>
          </ChartCard>

          <ChartCard title="CALIBRATED SUB-SURFACE DEPTH / ELEVATION (m)">
            <ResponsiveContainer width="100%" height="100%">
              <AreaChart data={chartData}>
                <defs>
                  <linearGradient id="g-alt" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="5%"  stopColor="var(--purple)" stopOpacity={0.3} />
                    <stop offset="95%" stopColor="var(--purple)" stopOpacity={0} />
                  </linearGradient>
                </defs>
                <CartesianGrid strokeDasharray="3 3" stroke="rgba(255,255,255,0.04)" />
                <XAxis dataKey="time" tick={TICK_STYLE} />
                <YAxis domain={['auto', 'auto']} tick={TICK_STYLE} unit="m" />
                <Tooltip content={<CustomTooltip unit="m" />} />
                <Area type="monotone" dataKey="altitude" name="Elevation" stroke="var(--purple)" strokeWidth={2}
                      fill="url(#g-alt)" isAnimationActive={false} />
              </AreaChart>
            </ResponsiveContainer>
          </ChartCard>
        </>
      )}

      {/* Motion tab */}
      {activeTab === 'motion' && (
        <>
          <ChartCard title="3-AXIS ACCELERATION (g)">
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={chartData}>
                <CartesianGrid strokeDasharray="3 3" stroke="rgba(255,255,255,0.04)" />
                <XAxis dataKey="time" tick={TICK_STYLE} />
                <YAxis domain={[-3, 3]} tick={TICK_STYLE} unit="g" />
                <Tooltip content={<CustomTooltip unit="g" />} />
                <Legend wrapperStyle={{ fontSize: 11 }} />
                <Line type="monotone" dataKey="accel_x" name="Accel X" stroke="var(--red)"   strokeWidth={1.5} dot={false} isAnimationActive={false} />
                <Line type="monotone" dataKey="accel_y" name="Accel Y" stroke="var(--green)" strokeWidth={1.5} dot={false} isAnimationActive={false} />
                <Line type="monotone" dataKey="accel_z" name="Accel Z" stroke="var(--blue)"  strokeWidth={1.5} dot={false} isAnimationActive={false} />
              </LineChart>
            </ResponsiveContainer>
          </ChartCard>

          <ChartCard title="3-AXIS GYROSCOPE (deg/s)">
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={chartData}>
                <CartesianGrid strokeDasharray="3 3" stroke="rgba(255,255,255,0.04)" />
                <XAxis dataKey="time" tick={TICK_STYLE} />
                <YAxis domain={['auto', 'auto']} tick={TICK_STYLE} unit="°/s" />
                <Tooltip content={<CustomTooltip unit="°/s" />} />
                <Legend wrapperStyle={{ fontSize: 11 }} />
                <Line type="monotone" dataKey="gyro_x" name="Gyro X" stroke="var(--gold)"   strokeWidth={1.5} dot={false} isAnimationActive={false} />
                <Line type="monotone" dataKey="gyro_y" name="Gyro Y" stroke="var(--cyan)"   strokeWidth={1.5} dot={false} isAnimationActive={false} />
                <Line type="monotone" dataKey="gyro_z" name="Gyro Z" stroke="var(--purple)" strokeWidth={1.5} dot={false} isAnimationActive={false} />
              </LineChart>
            </ResponsiveContainer>
          </ChartCard>
        </>
      )}

      {/* Network tab */}
      {activeTab === 'network' && (
        <ChartCard title="RF LINK RSSI (dBm)">
          <ResponsiveContainer width="100%" height="100%">
            <AreaChart data={chartData}>
              <defs>
                <linearGradient id="g-rssi" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="5%"  stopColor="var(--cyan)" stopOpacity={0.3} />
                  <stop offset="95%" stopColor="var(--cyan)" stopOpacity={0} />
                </linearGradient>
              </defs>
              <CartesianGrid strokeDasharray="3 3" stroke="rgba(255,255,255,0.04)" />
              <XAxis dataKey="time" tick={TICK_STYLE} />
              <YAxis domain={[-95, -30]} tick={TICK_STYLE} unit=" dBm" />
              <Tooltip content={<CustomTooltip unit="dBm" />} />
              <Area type="monotone" dataKey="rssi" name="RSSI" stroke="var(--cyan)" strokeWidth={2}
                    fill="url(#g-rssi)" isAnimationActive={false} />
            </AreaChart>
          </ResponsiveContainer>
        </ChartCard>
      )}
    </div>
  );
}
