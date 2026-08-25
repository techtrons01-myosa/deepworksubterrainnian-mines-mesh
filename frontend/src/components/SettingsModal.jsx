import React, { useState, useEffect } from 'react';

export default function SettingsModal({ isOpen, onClose, settings, onUpdateSettings, nodes = {} }) {
  if (!isOpen) return null;

  const [activeTab, setActiveTab] = useState('ground'); // 'ground', 'fleet', 'geofence'

  // Geofence & Buzzer state
  const [radius, setRadius] = useState(settings.geofenceRadius ?? 50);
  const [buzzerEnabled, setBuzzerEnabled] = useState(settings.buzzerEnabled ?? true);
  const [alarmOnDisconnect, setAlarmOnDisconnect] = useState(settings.alarmOnDisconnect ?? true);
  const [buzzerVolume, setBuzzerVolume] = useState(settings.buzzerVolume ?? 0.5);

  // Ground level calibration state
  const [groundDatum, setGroundDatum] = useState(settings.groundLevelDatum ?? 25.0);
  const [refNode, setRefNode] = useState(settings.groundReferenceNode ?? 'MYO-3681F4');
  const [offsets, setOffsets] = useState(settings.nodeCalibrationOffsets ?? {});

  // Known altitude input per node for calibration
  const [knownAlts, setKnownAlts] = useState({});

  // Mine Shaft Levels state
  const defaultShaftLevels = [
    { label: 'Surface Portal', alt: 25.0, color: 'var(--cyan)' },
    { label: 'Shaft 1 / Upper Gallery', alt: 10.0, color: 'var(--green)' },
    { label: 'Level 2 / Mid Drift', alt: -5.0, color: 'var(--purple)' },
    { label: 'Level 3 / Deep Stope', alt: -20.0, color: 'var(--amber)' },
    { label: 'Shaft Sump', alt: -40.0, color: 'var(--red)' },
  ];
  const [shafts, setShafts] = useState(settings.shaftLevels || defaultShaftLevels);

  const [saveStatus, setSaveStatus] = useState('');

  // Keep state synchronized with external settings updates
  useEffect(() => {
    if (settings.groundLevelDatum !== undefined) {
      setGroundDatum(settings.groundLevelDatum);
    }
    if (settings.nodeCalibrationOffsets) {
      setOffsets(settings.nodeCalibrationOffsets);
    }
    if (settings.shaftLevels) {
      setShafts(settings.shaftLevels);
    }
    if (settings.geofenceRadius !== undefined) setRadius(settings.geofenceRadius);
    if (settings.buzzerEnabled !== undefined) setBuzzerEnabled(settings.buzzerEnabled);
    if (settings.alarmOnDisconnect !== undefined) setAlarmOnDisconnect(settings.alarmOnDisconnect);
  }, [settings]);

  const nodeList = Object.values(nodes);

  // Handle saving geofence & buzzer settings
  const handleSaveGeofence = () => {
    onUpdateSettings({
      geofenceRadius: radius,
      buzzerEnabled,
      alarmOnDisconnect,
      buzzerVolume,
    });
    setSaveStatus('Geofence & Buzzer settings saved successfully!');
    setTimeout(() => setSaveStatus(''), 3000);
  };

  // Handle saving calibration settings
  const handleSaveCalibration = () => {
    const datumVal = parseFloat(groundDatum) || 0;
    onUpdateSettings({
      groundLevelDatum: datumVal,
      groundReferenceNode: refNode,
      nodeCalibrationOffsets: offsets,
      shaftLevels: shafts,
    });
    setSaveStatus(`Ground reference set to ${datumVal.toFixed(1)}m and mine shaft levels saved!`);
    setTimeout(() => setSaveStatus(''), 3500);
  };

  const handleUpdateShaft = (idx, field, val) => {
    const next = [...shafts];
    next[idx] = { ...next[idx], [field]: field === 'alt' ? parseFloat(val) || 0 : val };
    setShafts(next);
    onUpdateSettings({ shaftLevels: next });
  };

  const handleAddShaft = () => {
    const next = [
      ...shafts,
      { label: `Level ${shafts.length + 1}`, alt: (parseFloat(groundDatum) || 25) - (shafts.length * 15), color: 'var(--cyan)' },
    ];
    setShafts(next);
    onUpdateSettings({ shaftLevels: next });
  };

  const handleRemoveShaft = (idx) => {
    const next = shafts.filter((_, i) => i !== idx);
    setShafts(next);
    onUpdateSettings({ shaftLevels: next });
  };

  // Calibrate node to known actual elevation
  const handleCalibrateKnown = (nodeId) => {
    const rawAlt = nodes[nodeId]?.altitude ?? 25.0;
    const targetAlt = parseFloat(knownAlts[nodeId]);
    if (isNaN(targetAlt)) return;

    const newOffset = rawAlt - targetAlt;
    const newOffsets = {
      ...offsets,
      [nodeId]: newOffset,
    };
    setOffsets(newOffsets);
    onUpdateSettings({ nodeCalibrationOffsets: newOffsets });
    setSaveStatus(`Calibrated ${nodeId} to exact altitude ${targetAlt.toFixed(1)}m (offset: ${newOffset.toFixed(1)}m)!`);
    setTimeout(() => setSaveStatus(''), 3500);
  };

  // Tare specific node to ground level (0.0m depth)
  const handleTareNode = (nodeId) => {
    const rawAlt = nodes[nodeId]?.altitude ?? 25.0;
    const newOffsets = {
      ...offsets,
      [nodeId]: rawAlt - (parseFloat(groundDatum) || 0),
    };
    setOffsets(newOffsets);
    onUpdateSettings({ nodeCalibrationOffsets: newOffsets });
    setSaveStatus(`Tared ${nodeId} to Ground Level (0.0m)!`);
    setTimeout(() => setSaveStatus(''), 3000);
  };

  // Quick presets
  const presets = [
    { label: 'Surface Entry (80.5m)', val: 80.5 },
    { label: 'Level 1 Portal (55.0m)', val: 55.0 },
    { label: 'Level 2 Hub (45.0m)', val: 45.0 },
    { label: 'Standard Datum (25.0m)', val: 25.0 },
    { label: '0.0m (Sea Level)', val: 0.0 },
  ];

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="modal-card" style={{ maxWidth: 900 }} onClick={e => e.stopPropagation()}>
        {/* Header */}
        <div className="modal-header">
          <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
            <div className="status-badge" style={{ padding: '6px 10px', background: 'rgba(6,214,245,0.15)', borderColor: 'var(--cyan)' }}>
              ⚙️
            </div>
            <div>
              <div className="modal-title" style={{ fontSize: 16 }}>Mine System & Elevation Calibration</div>
              <div style={{ fontSize: 11, color: 'var(--text-muted)', fontFamily: 'JetBrains Mono, monospace' }}>
                MYOSA HARDWARE · GROUND DATUM · SHAFT LEVELS · SENSOR CALIBRATION
              </div>
            </div>
          </div>
          <button className="btn btn-ghost" onClick={onClose} style={{ fontSize: 18, lineHeight: 1 }}>✕</button>
        </div>

        {/* Navigation Tabs */}
        <div style={{ display: 'flex', borderBottom: '1px solid var(--border)', background: 'rgba(0,0,0,0.2)' }}>
          <button
            className={`tab-btn ${activeTab === 'ground' ? 'active' : ''}`}
            onClick={() => setActiveTab('ground')}
            style={{ flex: 1, padding: '12px 0', fontSize: 12, fontWeight: 700, color: 'var(--amber)' }}
          >
            📐 Ground & Sensor Elevation Calibration
          </button>
          <button
            className={`tab-btn ${activeTab === 'geofence' ? 'active' : ''}`}
            onClick={() => setActiveTab('geofence')}
            style={{ flex: 1, padding: '12px 0', fontSize: 12, fontWeight: 700 }}
          >
            ⚡ Mesh Radius & Safety Buzzer
          </button>
          <button
            className={`tab-btn ${activeTab === 'fleet' ? 'active' : ''}`}
            onClick={() => setActiveTab('fleet')}
            style={{ flex: 1, padding: '12px 0', fontSize: 12, fontWeight: 700 }}
          >
            🔘 Hardware Nodes Fleet
          </button>
        </div>

        {/* Modal Body */}
        <div className="modal-body" style={{ maxHeight: 'calc(80vh - 140px)', overflowY: 'auto', padding: 24 }}>
          {saveStatus && (
            <div style={{
              background: 'rgba(16, 217, 138, 0.15)',
              border: '1px solid var(--green)',
              borderRadius: 8,
              padding: '10px 16px',
              marginBottom: 20,
              fontSize: 12,
              color: 'var(--green)',
              fontWeight: 600,
              display: 'flex',
              alignItems: 'center',
              gap: 8,
            }}>
              <span>✓</span> {saveStatus}
            </div>
          )}

          {/* TAB: Ground & Shaft Calibration */}
          {activeTab === 'ground' && (
            <div style={{ display: 'flex', flexDirection: 'column', gap: 24 }}>
              {/* Reference Ground Level Input */}
              <div style={{
                background: 'rgba(6, 214, 245, 0.03)',
                border: '1px solid rgba(6, 214, 245, 0.2)',
                borderRadius: 12,
                padding: 20,
              }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start', marginBottom: 14 }}>
                  <div>
                    <div style={{ display: 'flex', alignItems: 'center', gap: 8, fontSize: 14, fontWeight: 700, color: 'var(--cyan)' }}>
                      📐 Reference Ground Level Altitude (Datum Baseline)
                    </div>
                    <div style={{ fontSize: 12, color: 'var(--text-muted)', marginTop: 4 }}>
                      Input your chosen ground surface elevation. All miner nodes and shaft depths will be measured with respect to this baseline level.
                    </div>
                  </div>
                  <div style={{
                    padding: '6px 14px',
                    background: 'rgba(167, 139, 250, 0.1)',
                    borderRadius: 6,
                    border: '1px solid rgba(167, 139, 250, 0.3)'
                  }}>
                    <span style={{ fontSize: 11, color: 'var(--text-muted)' }}>Baseline: </span>
                    <strong style={{ fontSize: 14, color: 'var(--purple)', fontFamily: 'JetBrains Mono, monospace' }}>
                      {parseFloat(groundDatum || 0).toFixed(1)} m
                    </strong>
                  </div>
                </div>

                {/* Input & Apply Button */}
                <div style={{ display: 'flex', gap: 12, alignItems: 'center', marginBottom: 16 }}>
                  <div style={{ position: 'relative', flex: 1 }}>
                    <input
                      type="number"
                      step="0.1"
                      className="input-field"
                      style={{
                        width: '100%',
                        fontSize: 16,
                        fontFamily: 'JetBrains Mono, monospace',
                        fontWeight: 700,
                        padding: '10px 60px 10px 16px',
                        border: '1px solid var(--border-bright)',
                        borderRadius: 8,
                      }}
                      value={groundDatum}
                      onChange={e => {
                        const val = e.target.value;
                        setGroundDatum(val);
                        const numericVal = parseFloat(val);
                        if (!isNaN(numericVal)) {
                          onUpdateSettings({ groundLevelDatum: numericVal });
                        }
                      }}
                      placeholder="e.g. 25.0 or 55.0"
                    />
                    <span style={{
                      position: 'absolute',
                      right: 14,
                      top: '50%',
                      transform: 'translateY(-50%)',
                      fontSize: 11,
                      color: 'var(--text-muted)',
                      fontWeight: 700,
                    }}>METERS</span>
                  </div>

                  <button
                    type="button"
                    className="btn btn-cyan"
                    style={{ height: 44, padding: '0 20px', fontSize: 13, fontWeight: 700 }}
                    onClick={handleSaveCalibration}
                    id="apply-datum-btn"
                  >
                    ✓ Apply Reference Level
                  </button>
                </div>

                {/* Quick Presets */}
                <div style={{ display: 'flex', alignItems: 'center', gap: 8, flexWrap: 'wrap' }}>
                  <span style={{ fontSize: 11, color: 'var(--text-muted)', fontWeight: 700, letterSpacing: 0.5 }}>
                    PRESETS:
                  </span>
                  {presets.map(preset => (
                    <button
                      key={preset.label}
                      type="button"
                      className={`btn ${Math.abs(parseFloat(groundDatum || 0) - preset.val) < 0.1 ? 'btn-cyan' : 'btn-ghost'}`}
                      style={{ padding: '4px 10px', fontSize: 10, fontFamily: 'monospace' }}
                      onClick={() => {
                        setGroundDatum(preset.val);
                        onUpdateSettings({ groundLevelDatum: preset.val });
                        setSaveStatus(`Ground reference switched to ${preset.val}m`);
                        setTimeout(() => setSaveStatus(''), 2500);
                      }}
                    >
                      {preset.label}
                    </button>
                  ))}
                  <button
                    type="button"
                    className="btn btn-ghost"
                    style={{ padding: '4px 10px', fontSize: 10, marginLeft: 'auto', color: 'var(--amber)' }}
                    onClick={() => {
                      setGroundDatum(0);
                      setOffsets({});
                      onUpdateSettings({ groundLevelDatum: 0, nodeCalibrationOffsets: {} });
                      setSaveStatus('Calibration reset to raw sea-level datum (0.0m)');
                      setTimeout(() => setSaveStatus(''), 2500);
                    }}
                  >
                    ↺ Reset All Offsets
                  </button>
                </div>
              </div>

              {/* Node Altitude Calibrator & Table */}
              <div style={{
                background: 'rgba(0, 0, 0, 0.2)',
                border: '1px solid var(--border)',
                borderRadius: 12,
                padding: 16,
              }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 12 }}>
                  <div>
                    <div style={{ fontSize: 13, fontWeight: 700, color: 'var(--cyan)' }}>
                      📍 Sensor Altitude Calibration & Live Depths
                    </div>
                    <div style={{ fontSize: 11, color: 'var(--text-muted)' }}>
                      Calibrate individual sensor elevation to your exact known height or set baseline ground datum
                    </div>
                  </div>
                  <span className="status-badge status-online" style={{ fontSize: 10 }}>
                    {nodeList.filter(n => n.status === 'ONLINE').length} Active Nodes
                  </span>
                </div>

                <div style={{ overflowX: 'auto' }}>
                  <table className="node-table" style={{ width: '100%', fontSize: 11 }}>
                    <thead>
                      <tr>
                        <th>NODE ID</th>
                        <th>RAW SENSOR (MSL)</th>
                        <th>CALIBRATED ALTITUDE</th>
                        <th>DEPTH WRT GROUND ({parseFloat(groundDatum || 0).toFixed(1)}m)</th>
                        <th>CALIBRATE TO KNOWN ALTITUDE</th>
                        <th>ACTIONS</th>
                      </tr>
                    </thead>
                    <tbody>
                      {nodeList.map(n => {
                        const raw = n.altitude ?? 25.0;
                        const datum = parseFloat(groundDatum || 0);
                        const offset = offsets[n.node_id] ?? 0;
                        const effectiveAlt = raw - offset;
                        const depth = datum - effectiveAlt;
                        const isAbove = depth < -0.1;

                        return (
                          <tr key={n.node_id}>
                            <td style={{ fontWeight: 800, fontFamily: 'JetBrains Mono, monospace', color: 'var(--cyan)' }}>
                              {n.node_id}
                            </td>
                            <td style={{ fontFamily: 'JetBrains Mono, monospace', color: 'var(--text-secondary)' }}>
                              {raw.toFixed(1)} m
                            </td>
                            <td style={{ fontFamily: 'JetBrains Mono, monospace', fontWeight: 700, color: 'var(--green)' }}>
                              {effectiveAlt.toFixed(1)} m
                            </td>
                            <td>
                              <span
                                className="status-badge"
                                style={{
                                  background: isAbove ? 'rgba(16, 217, 138, 0.15)' : 'rgba(251, 191, 36, 0.15)',
                                  borderColor: isAbove ? 'var(--green)' : 'var(--amber)',
                                  color: isAbove ? 'var(--green)' : 'var(--amber)',
                                  fontFamily: 'JetBrains Mono, monospace',
                                  fontSize: 10,
                                }}
                              >
                                {isAbove ? `▲ +${Math.abs(depth).toFixed(1)}m Above Ground` : `▼ ${depth.toFixed(1)}m Underground`}
                              </span>
                            </td>
                            <td>
                              <div style={{ display: 'flex', gap: 4, alignItems: 'center' }}>
                                <input
                                  type="number"
                                  step="0.5"
                                  placeholder="Known m"
                                  className="input-field"
                                  style={{ width: 70, fontSize: 11, padding: '3px 6px' }}
                                  value={knownAlts[n.node_id] ?? ''}
                                  onChange={e => setKnownAlts({ ...knownAlts, [n.node_id]: e.target.value })}
                                />
                                <button
                                  type="button"
                                  className="btn btn-cyan"
                                  style={{ padding: '3px 8px', fontSize: 10 }}
                                  onClick={() => handleCalibrateKnown(n.node_id)}
                                >
                                  Calibrate
                                </button>
                              </div>
                            </td>
                            <td>
                              <div style={{ display: 'flex', gap: 6 }}>
                                <button
                                  type="button"
                                  className="btn btn-ghost"
                                  style={{ padding: '2px 8px', fontSize: 10 }}
                                  onClick={() => handleTareNode(n.node_id)}
                                >
                                  🎯 Tare to 0m
                                </button>
                                <button
                                  type="button"
                                  className="btn btn-ghost"
                                  style={{ padding: '2px 8px', fontSize: 10 }}
                                  onClick={() => {
                                    setGroundDatum(effectiveAlt.toFixed(1));
                                    onUpdateSettings({ groundLevelDatum: effectiveAlt });
                                    setSaveStatus(`Ground reference set to ${n.node_id}'s altitude (${effectiveAlt.toFixed(1)}m)!`);
                                    setTimeout(() => setSaveStatus(''), 2500);
                                  }}
                                >
                                  Set as Datum
                                </button>
                              </div>
                            </td>
                          </tr>
                        );
                      })}
                    </tbody>
                  </table>
                </div>
              </div>

              {/* Mine Shaft Levels Calibrator */}
              <div style={{
                background: 'rgba(0, 0, 0, 0.25)',
                border: '1px solid var(--border)',
                borderRadius: 12,
                padding: 18,
              }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 12 }}>
                  <div>
                    <div style={{ fontSize: 13, fontWeight: 700, color: 'var(--cyan)' }}>
                      🏗️ Calibrate Mine Shaft & Drift Levels
                    </div>
                    <div style={{ fontSize: 11, color: 'var(--text-muted)' }}>
                      Customize shaft levels, elevations, and tags displayed on the Cross-Section Elevation Map.
                    </div>
                  </div>
                  <button
                    type="button"
                    className="btn btn-cyan"
                    style={{ padding: '4px 12px', fontSize: 11, fontWeight: 700 }}
                    onClick={handleAddShaft}
                  >
                    + Add Shaft Level
                  </button>
                </div>

                <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
                  {shafts.map((shaft, idx) => (
                    <div
                      key={idx}
                      style={{
                        display: 'flex',
                        alignItems: 'center',
                        gap: 12,
                        background: 'rgba(255, 255, 255, 0.03)',
                        padding: '8px 12px',
                        borderRadius: 6,
                        border: '1px solid var(--border)',
                      }}
                    >
                      <span style={{ fontSize: 11, color: 'var(--text-muted)', width: 20 }}>#{idx + 1}</span>
                      <input
                        type="text"
                        className="input-field"
                        style={{ flex: 1, fontSize: 12, padding: '4px 8px' }}
                        value={shaft.label}
                        onChange={e => handleUpdateShaft(idx, 'label', e.target.value)}
                        placeholder="Shaft name / tag"
                      />
                      <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                        <span style={{ fontSize: 11, color: 'var(--text-muted)' }}>Elevation:</span>
                        <input
                          type="number"
                          step="1"
                          className="input-field"
                          style={{ width: 80, fontSize: 12, fontFamily: 'monospace', padding: '4px 8px' }}
                          value={shaft.alt}
                          onChange={e => handleUpdateShaft(idx, 'alt', e.target.value)}
                        />
                        <span style={{ fontSize: 11, color: 'var(--text-muted)' }}>m</span>
                      </div>
                      <button
                        type="button"
                        className="btn btn-ghost"
                        style={{ padding: '4px 8px', color: 'var(--red)', fontSize: 12 }}
                        onClick={() => handleRemoveShaft(idx)}
                      >
                        ✕
                      </button>
                    </div>
                  ))}
                </div>
              </div>
            </div>
          )}

          {/* TAB: Geofence & Buzzer */}
          {activeTab === 'geofence' && (
            <div style={{ display: 'flex', flexDirection: 'column', gap: 20 }}>
              <div style={{ background: 'rgba(0,0,0,0.2)', border: '1px solid var(--border)', borderRadius: 12, padding: 20 }}>
                <div style={{ fontSize: 14, fontWeight: 700, color: 'var(--cyan)', marginBottom: 12 }}>
                  ⚡ Geofence Maximum Safe Radius
                </div>
                <div style={{ display: 'flex', alignItems: 'center', gap: 16 }}>
                  <input
                    type="range"
                    min="10"
                    max="150"
                    value={radius}
                    onChange={e => setRadius(parseInt(e.target.value, 10))}
                    style={{ flex: 1 }}
                  />
                  <span style={{ fontSize: 16, fontWeight: 700, fontFamily: 'monospace', color: 'var(--cyan)', minWidth: 60 }}>
                    {radius} m
                  </span>
                </div>
              </div>

              <div style={{ background: 'rgba(0,0,0,0.2)', border: '1px solid var(--border)', borderRadius: 12, padding: 20 }}>
                <div style={{ fontSize: 14, fontWeight: 700, color: 'var(--cyan)', marginBottom: 12 }}>
                  🔔 Miner Node Alarm & Hardware Buzzer
                </div>
                <label style={{ display: 'flex', alignItems: 'center', gap: 10, cursor: 'pointer', marginBottom: 12 }}>
                  <input
                    type="checkbox"
                    checked={buzzerEnabled}
                    onChange={e => setBuzzerEnabled(e.target.checked)}
                  />
                  <span>Enable hardware buzzer alerts on nodes during emergency</span>
                </label>
                <label style={{ display: 'flex', alignItems: 'center', gap: 10, cursor: 'pointer' }}>
                  <input
                    type="checkbox"
                    checked={alarmOnDisconnect}
                    onChange={e => setAlarmOnDisconnect(e.target.checked)}
                  />
                  <span>Trigger alarm when a miner goes OFFLINE (&gt; 12 seconds)</span>
                </label>
              </div>

              <button className="btn btn-cyan" onClick={handleSaveGeofence} style={{ alignSelf: 'flex-start' }}>
                Save Geofence Settings
              </button>
            </div>
          )}

          {/* TAB: Fleet */}
          {activeTab === 'fleet' && (
            <div style={{ padding: 20, textAlign: 'center', color: 'var(--text-muted)' }}>
              <div style={{ fontSize: 14, fontWeight: 700, color: 'var(--text-primary)', marginBottom: 8 }}>
                MYOSA Fleet Operations Active
              </div>
              <div style={{ fontSize: 12 }}>
                Miner Nodes linked to ESP32-S3 Zero Gateway on COM7. Mesh operating on Channel 1 (2.4 GHz).
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
