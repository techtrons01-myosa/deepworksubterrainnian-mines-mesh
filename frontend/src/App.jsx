import React, { useState, useEffect, useRef, useCallback } from 'react';
import { useWebSocket } from './hooks/useWebSocket.js';
import Sidebar       from './components/Sidebar.jsx';
import Dashboard     from './components/Dashboard.jsx';
import NetworkMap    from './components/NetworkMap.jsx';
import NodeTable     from './components/NodeTable.jsx';
import AlertPanel    from './components/AlertPanel.jsx';
import ChartPanel    from './components/ChartPanel.jsx';
import NodeDetail    from './components/NodeDetail.jsx';
import LinkedNodesModal from './components/LinkedNodesModal.jsx';
import SettingsModal from './components/SettingsModal.jsx';
import { playAlarmBurst } from './utils/audioAlert.js';

const WS_URL       = 'ws://localhost:3001';
const API_URL      = 'http://localhost:3001/api';
const MAX_HISTORY  = 60;   // data points kept per node

const DEFAULT_SETTINGS = {
  nodeCount: 1,
  geofenceRadius: 50,
  buzzerEnabled: true,
  buzzerVolume: 0.5,
  alarmOnDisconnect: true,
  groundLevelDatum: 50.0,
  groundReferenceNode: 'MYO-3681F4',
  nodeCalibrationOffsets: {},
};

export default function App() {
  const [view,              setView]              = useState('map');
  const [selectedNode,      setSelectedNode]      = useState(null);
  const [linkedNodesSource, setLinkedNodesSource] = useState(null);
  const [history,           setHistory]           = useState({});  // nodeId -> [{timestamp,temperature,...}]
  const [isSettingsOpen, setIsSettingsOpen] = useState(false);
  const [isEvacuating,   setIsEvacuating]   = useState(false);
  const [adminAlarmSilenced, setAdminAlarmSilenced] = useState(false);
  const [mutedNodeIds, setMutedNodeIds] = useState(() => {
    try {
      const saved = localStorage.getItem('myosa_muted_nodes');
      return saved ? new Set(JSON.parse(saved)) : new Set();
    } catch {
      return new Set();
    }
  });

  const prevOfflineCountRef = useRef(0);

  const [settings, setSettings] = useState(() => {
    try {
      const saved = localStorage.getItem('myosa_settings');
      return saved ? { ...DEFAULT_SETTINGS, ...JSON.parse(saved) } : DEFAULT_SETTINGS;
    } catch {
      return DEFAULT_SETTINGS;
    }
  });

  const { state, connected } = useWebSocket(WS_URL);
  const [clock, setClock] = useState('');

  const nodes = state?.nodes || {};
  const links = state?.links || [];
  const health = state?.health || {};
  const alerts = state?.alerts || [];

  // Track offline nodes that are not muted
  const unmutedOfflineNodes = Object.values(nodes).filter(n => n.status === 'OFFLINE' && !mutedNodeIds.has(n.node_id));
  const hasActiveUnmutedOffline = unmutedOfflineNodes.length > 0;

  // Toggle Mute for specific node
  const toggleMuteNode = useCallback((nodeId) => {
    setMutedNodeIds(prev => {
      const next = new Set(prev);
      if (next.has(nodeId)) {
        next.delete(nodeId);
      } else {
        next.add(nodeId);
      }
      try {
        localStorage.setItem('myosa_muted_nodes', JSON.stringify([...next]));
      } catch (_) {}
      return next;
    });
  }, []);

  // Un-silence alarm when a new unmuted node disconnects; reset when all reconnect
  useEffect(() => {
    if (unmutedOfflineNodes.length > prevOfflineCountRef.current) {
      setAdminAlarmSilenced(false); // New disconnect -> trigger alarm
    } else if (unmutedOfflineNodes.length === 0) {
      setAdminAlarmSilenced(false); // All reconnected -> reset
    }
    prevOfflineCountRef.current = unmutedOfflineNodes.length;
  }, [unmutedOfflineNodes.length]);

  // Continuous Admin Audio Alarm (Evacuation Siren OR Unmuted Node Disconnect Alarm)
  useEffect(() => {
    // 1. Emergency Evacuation continuous alarm
    if (isEvacuating) {
      const id = setInterval(() => {
        playAlarmBurst({ pulses: 3, frequency: 1200, volume: settings.buzzerVolume ?? 0.5 });
      }, 1500);
      return () => clearInterval(id);
    }

    // 2. Continuous Disconnect Alarm (sounds when unmuted node is OFFLINE and master alarm is not silenced)
    if (hasActiveUnmutedOffline && !adminAlarmSilenced && settings.alarmOnDisconnect !== false) {
      const id = setInterval(() => {
        playAlarmBurst({ pulses: 2, frequency: 880, volume: settings.buzzerVolume ?? 0.5 });
      }, 1800);
      return () => clearInterval(id);
    }
  }, [isEvacuating, hasActiveUnmutedOffline, adminAlarmSilenced, settings]);

  // Fetch initial settings from backend API
  useEffect(() => {
    fetch(`${API_URL}/settings`)
      .then(res => res.json())
      .then(data => {
        if (data && typeof data === 'object') {
          setSettings(prev => ({ ...prev, ...data }));
        }
      })
      .catch(() => {});
  }, []);

  // Save settings changes
  const handleUpdateSettings = useCallback(async (newSettings) => {
    const updated = { ...settings, ...newSettings };
    setSettings(updated);
    try {
      localStorage.setItem('myosa_settings', JSON.stringify(updated));
      await fetch(`${API_URL}/settings`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(newSettings),
      });
    } catch (err) {
      console.warn('[Settings] Failed to persist to backend:', err);
    }
  }, [settings]);

  // Toggle Emergency Evacuation
  const handleToggleEvacuation = useCallback(async () => {
    if (!isEvacuating) {
      if (window.confirm('⚠️ TRIGGER EMERGENCY EVACUATION?\n\nThis will transmit an emergency siren and EVACUATE NOW alert to ALL active wearable nodes across the entire subsurface mesh.')) {
        setIsEvacuating(true);
        try {
          await fetch(`${API_URL}/evacuate`, { method: 'POST' });
        } catch (err) {
          console.error('Evacuate error:', err);
        }
      }
    } else {
      setIsEvacuating(false);
      try {
        await fetch(`${API_URL}/evacuate/cancel`, { method: 'POST' });
      } catch (err) {
        console.error('Cancel evacuate error:', err);
      }
    }
  }, [isEvacuating]);

  // Live clock
  useEffect(() => {
    const tick = () => setClock(new Date().toLocaleTimeString('en-GB', { hour12: false }));
    tick();
    const id = setInterval(tick, 1000);
    return () => clearInterval(id);
  }, []);

  // Accumulate rolling history for charts
  useEffect(() => {
    if (!state?.nodes) return;
    setHistory(prev => {
      const next = { ...prev };
      for (const [id, node] of Object.entries(state.nodes)) {
        if (node.status !== 'ONLINE') continue;
        const prev_arr = next[id] || [];
        const entry = {
          timestamp:   state.timestamp,
          temperature: node.temperature,
          pressure:    node.pressure,
          altitude:    node.altitude,
          rssi:        node.rssi,
          accel_x:     node.accel?.x,
          accel_y:     node.accel?.y,
          accel_z:     node.accel?.z,
          gyro_x:      node.gyro?.x,
          gyro_y:      node.gyro?.y,
          gyro_z:      node.gyro?.z,
        };
        next[id] = [...prev_arr, entry].slice(-MAX_HISTORY);
      }
      return next;
    });
  }, [state?.timestamp, state?.nodes]);

  const critCount = alerts.filter(a => a.severity === 'CRITICAL' && !a.acknowledged).length;

  const handleNodeClick = useCallback(node => setSelectedNode(node), []);
  const handleLinksClick = useCallback(node => setLinkedNodesSource(node), []);

  if (!connected && !state) {
    return (
      <div className="connecting-overlay">
        <div style={{ display: 'flex', alignItems: 'center', gap: 14 }}>
          <div className="header-logo" style={{ width: 44, height: 44, fontSize: 22 }}>⛏️</div>
          <div>
            <div style={{ fontSize: 26, fontWeight: 900, color: '#ffffff', fontFamily: 'var(--font-display)', letterSpacing: 2 }}>
              DEEPWORKS <span style={{ color: 'var(--gold)' }}>MINING</span>
            </div>
            <div style={{ color: 'var(--text-muted)', fontSize: 11, fontFamily: 'var(--font-hud)', letterSpacing: 2 }}>
              MYOSA CENTRAL TELEMETRY ENGINE
            </div>
          </div>
        </div>
        <div className="connecting-spinner" />
        <div style={{ color: 'var(--text-secondary)', fontSize: 13, fontFamily: 'var(--font-hud)' }}>
          Connecting to ESP32-S3 Hardware Gateway (COM7)...
        </div>
        <div style={{ color: 'var(--text-muted)', fontSize: 11, fontFamily: 'var(--font-mono)' }}>
          ws://localhost:3001
        </div>
      </div>
    );
  }

  const renderView = () => {
    switch (view) {
      case 'dashboard': return (
        <Dashboard
          nodes={nodes}
          health={health}
          alerts={alerts}
          history={history}
          onNodeClick={handleNodeClick}
          onLinksClick={handleLinksClick}
          settings={settings}
        />
      );
      case 'map': return (
        <NetworkMap
          nodes={nodes}
          links={links}
          onNodeClick={handleNodeClick}
          settings={settings}
        />
      );
      case 'nodes': return (
        <NodeTable
          nodes={nodes}
          mutedNodeIds={mutedNodeIds}
          onToggleMute={toggleMuteNode}
          onNodeClick={handleNodeClick}
          onLinksClick={handleLinksClick}
          settings={settings}
        />
      );
      case 'alerts': return <AlertPanel alerts={alerts} />;
      case 'charts': return <ChartPanel nodes={nodes} history={history} settings={settings} />;
      default: return null;
    }
  };

  return (
    <div className="app-shell">
      {/* Header */}
      <header className="app-header">
        <div className="header-brand">
          <div className="header-logo">⛏️</div>
          <div className="header-title-wrap">
            <div className="header-title">DEEPWORKS <span>MINING</span></div>
            <div className="header-sub">MYOSA Subsurface Mesh Monitor</div>
          </div>
        </div>

        <div className="header-right">
          <div className="header-stat">
            <div className={`dot ${connected ? 'dot-green' : 'dot-red'}`} />
            {connected ? 'SUB-SURFACE LINK ACTIVE' : 'RECONNECTING'}
          </div>

          <div className="header-stat">
            <div className="dot dot-green" />
            GATEWAY (COM7)
          </div>

          <div className="header-stat">
            <span style={{ color: 'var(--text-muted)' }}>MINERS:</span>
            <span style={{ color: 'var(--gold)', fontWeight: 800 }}>{health.online ?? '--'}</span>
            /
            <span style={{ color: 'var(--text-secondary)' }}>{health.total ?? '--'}</span>
          </div>

          {/* Admin Disconnect Alarm Banner & Master Silence Button */}
          {hasActiveUnmutedOffline && (
            <div style={{
              display: 'flex',
              alignItems: 'center',
              gap: 8,
              background: 'rgba(255, 23, 68, 0.15)',
              border: '1px solid #ff1744',
              borderRadius: 6,
              padding: '3px 10px',
              animation: !adminAlarmSilenced ? 'pulse 1s infinite' : 'none',
            }}>
              <span style={{ fontSize: 11, fontWeight: 900, color: '#ff5252' }}>
                🚨 {unmutedOfflineNodes.length} NODE{unmutedOfflineNodes.length > 1 ? 'S' : ''} DISCONNECTED!
              </span>
              <button
                type="button"
                id="admin-silence-alarm-btn"
                className="btn"
                style={{
                  padding: '2px 8px',
                  fontSize: 10,
                  fontWeight: 800,
                  background: adminAlarmSilenced ? 'rgba(255, 255, 255, 0.1)' : '#ff1744',
                  color: '#ffffff',
                  borderColor: '#ff1744',
                  cursor: 'pointer',
                }}
                onClick={() => setAdminAlarmSilenced(!adminAlarmSilenced)}
                title="Click to Mute or Unmute all disconnect alarms on PC"
              >
                {adminAlarmSilenced ? '🔇 ALARM MUTED' : '🔕 MUTE ALARM'}
              </button>
            </div>
          )}

          {/* Emergency Evacuation Broadcast Button */}
          <button
            id="header-evacuate-btn"
            className="btn"
            style={{
              padding: '5px 12px',
              fontSize: 11,
              fontWeight: 800,
              background: isEvacuating ? '#ff1744' : 'rgba(255, 23, 68, 0.15)',
              color: isEvacuating ? '#ffffff' : '#ff5252',
              borderColor: '#ff1744',
              cursor: 'pointer',
              letterSpacing: 1,
            }}
            onClick={handleToggleEvacuation}
          >
            🚨 {isEvacuating ? 'CANCEL EVACUATION' : 'EMERGENCY EVACUATE'}
          </button>

          {/* Tactical Config Button */}
          <button
            id="header-settings-btn"
            className="btn btn-ghost"
            style={{ padding: '5px 12px', fontSize: 11, borderColor: 'var(--border-bright)', color: 'var(--gold)' }}
            onClick={() => setIsSettingsOpen(true)}
          >
            ⚙️ SYSTEM CONFIG
          </button>

          <div className="header-clock">{clock}</div>
          <div className="sim-badge" style={{ background: 'rgba(0, 230, 118, 0.15)', color: '#00e676', borderColor: '#00e676' }}>
            📡 LIVE MESH (COM7)
          </div>
        </div>
      </header>

      {/* Sidebar */}
      <Sidebar
        view={view}
        setView={setView}
        critCount={critCount}
        health={health}
        connected={connected}
        onOpenSettings={() => setIsSettingsOpen(true)}
        settings={settings}
      />

      {/* Main Content Area */}
      <main className="app-main">
        {renderView()}
      </main>

      {/* Modals & Overlays */}
      {selectedNode && (
        <NodeDetail
          node={selectedNode}
          nodes={nodes}
          history={history[selectedNode.node_id] || []}
          isMuted={mutedNodeIds.has(selectedNode.node_id)}
          onToggleMute={toggleMuteNode}
          onClose={() => setSelectedNode(null)}
          settings={settings}
        />
      )}

      {linkedNodesSource && (
        <LinkedNodesModal
          sourceNode={linkedNodesSource}
          nodes={nodes}
          allNodes={nodes}
          settings={settings}
          onClose={() => setLinkedNodesSource(null)}
          onSelectNode={(node) => {
            setLinkedNodesSource(null);
            setSelectedNode(node);
          }}
          onNodeClick={(node) => {
            setLinkedNodesSource(null);
            setSelectedNode(node);
          }}
        />
      )}

      <SettingsModal
        isOpen={isSettingsOpen}
        onClose={() => setIsSettingsOpen(false)}
        settings={settings}
        onUpdateSettings={handleUpdateSettings}
        nodes={nodes}
      />
    </div>
  );
}
