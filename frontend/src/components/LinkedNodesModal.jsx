import React, { useMemo } from 'react';
import StatusBadge from './StatusBadge.jsx';
import { fmt, rssiQuality } from '../utils/formatters.js';

export default function LinkedNodesModal({
  sourceNode,
  nodes = {},
  allNodes = {},
  settings = {},
  onSelectNode = () => {},
  onNodeClick = () => {},
  onClose = () => {},
}) {
  if (!sourceNode) return null;

  const nodeMap = Object.keys(nodes).length > 0 ? nodes : allNodes;
  const datum = parseFloat(settings.groundLevelDatum || 0);

  // Calibrate node altitude
  const getAlt = (n) => {
    if (!n) return 0;
    const raw = n.altitude ?? 45;
    const offset = settings.nodeCalibrationOffsets?.[n.node_id] ?? 0;
    return raw - datum - offset;
  };

  const sourceAlt = getAlt(sourceNode);
  const isSourceOnline = sourceNode.status === 'ONLINE';

  const handleInspect = (targetNode) => {
    if (onSelectNode) onSelectNode(targetNode);
    if (onNodeClick) onNodeClick(targetNode);
  };

  // Build full linked nodes list
  const linkedNodes = useMemo(() => {
    const rawNeighbors = sourceNode.neighbors || [];
    const list = [];

    // 1. Gateway Link (Always present for active nodes)
    list.push({
      node_id: 'GATEWAY-COM7',
      mac: '50:78:7D:18:EC:C4',
      status: 'ONLINE',
      role: 'Surface Portal Mesh Root',
      rssi: sourceNode.rssi ?? -45,
      altitude: datum,
      altDiff: datum - sourceAlt,
      distanceM: Math.abs(datum - sourceAlt),
      isGateway: true,
      espnow: true,
      ble: true,
    });

    // 2. Peer miner nodes
    const peerIds = Object.keys(nodeMap).filter(id => id !== sourceNode.node_id);
    for (const id of peerIds) {
      const peer = nodeMap[id];
      if (!peer) continue;

      const isOnline = peer.status === 'ONLINE';
      const alt = getAlt(peer);
      const altDiff = alt - sourceAlt;

      let distanceM = 5.0;
      if (sourceNode.position && peer.position) {
        const dx = (peer.position.x - sourceNode.position.x) * 10;
        const dy = (peer.position.y - sourceNode.position.y) * 10;
        const dz = (altDiff);
        distanceM = Math.hypot(dx, dy, dz);
      }

      list.push({
        ...peer,
        altitude: alt,
        altDiff,
        distanceM: Math.max(1.0, distanceM),
        isGateway: false,
      });
    }

    return list;
  }, [sourceNode, nodeMap, settings, sourceAlt, datum]);

  const onlineCount = linkedNodes.filter(n => n.status === 'ONLINE').length;

  return (
    <div className="modal-overlay" onClick={onClose}>
      <div
        className="modal-panel linked-nodes-panel"
        onClick={e => e.stopPropagation()}
        id="linked-nodes-modal"
        style={{ width: 'min(860px, 95vw)', maxHeight: '88vh', overflowY: 'auto' }}
      >
        {/* Modal Header */}
        <div className="modal-header">
          <div>
            <div style={{ display: 'flex', alignItems: 'center', gap: 12, marginBottom: 4 }}>
              <span className="modal-title-tag">DEEPWORKS RF MESH TOPOLOGY</span>
              <span className="badge badge-info" style={{ fontSize: 11, padding: '3px 10px' }}>
                {onlineCount} / {linkedNodes.length} ACTIVE LINKS
              </span>
            </div>
            <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
              <span style={{ fontSize: 18, fontWeight: 800, color: '#ffffff', fontFamily: 'var(--font-display)', letterSpacing: 1 }}>
                LINKED NODES FOR
              </span>
              <span className="modal-node-id" style={{ fontSize: 22, color: 'var(--gold)' }}>
                ⛏️ {sourceNode.node_id}
              </span>
              <StatusBadge status={sourceNode.status} />
            </div>
          </div>
          <button className="modal-close" onClick={onClose} id="modal-links-close-btn" title="Close">
            ✕
          </button>
        </div>

        {/* Source Context Bar */}
        <div className="linked-source-bar" style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 10, padding: '12px 18px', background: 'rgba(0,0,0,0.3)', borderBottom: '1px solid var(--border)' }}>
          <div className="source-bar-item">
            <div style={{ fontSize: 9, color: 'var(--text-muted)', fontFamily: 'var(--font-hud)' }}>SOURCE ELEVATION</div>
            <div style={{ color: 'var(--purple)', fontWeight: 800, fontSize: 14 }}>
              {isSourceOnline ? fmt.alt(sourceAlt) : '—'}
            </div>
          </div>
          <div className="source-bar-item">
            <div style={{ fontSize: 9, color: 'var(--text-muted)', fontFamily: 'var(--font-hud)' }}>SOURCE RSSI</div>
            <div style={{ color: isSourceOnline ? rssiQuality(sourceNode.rssi).color : 'var(--text-muted)', fontWeight: 800, fontSize: 14 }}>
              {isSourceOnline ? fmt.rssi(sourceNode.rssi) : '—'}
            </div>
          </div>
          <div className="source-bar-item">
            <div style={{ fontSize: 9, color: 'var(--text-muted)', fontFamily: 'var(--font-hud)' }}>COORDINATES</div>
            <div style={{ color: 'var(--cyan)', fontWeight: 800, fontSize: 14 }}>
              {sourceNode.position ? `(${fmt.pos(sourceNode.position.x)}, ${fmt.pos(sourceNode.position.y)})` : '—'}
            </div>
          </div>
          <div className="source-bar-item">
            <div style={{ fontSize: 9, color: 'var(--text-muted)', fontFamily: 'var(--font-hud)' }}>MESH PROTOCOL</div>
            <div style={{ color: 'var(--green)', fontWeight: 800, fontSize: 14 }}>
              ESP-NOW 2.4GHz
            </div>
          </div>
        </div>

        {/* Modal Body: Linked Nodes Grid */}
        <div className="modal-body" style={{ padding: '20px 24px 28px' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 14 }}>
            <div style={{ fontSize: 11, fontWeight: 700, letterSpacing: '1.5px', color: 'var(--text-muted)', textTransform: 'uppercase' }}>
              DIRECT LINKED NEIGHBORS ({linkedNodes.length})
            </div>
            <div style={{ fontSize: 11, color: 'var(--cyan)', fontStyle: 'italic' }}>
              👉 Click on any Node card to inspect details
            </div>
          </div>

          <div className="linked-nodes-grid" style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(240px, 1fr))', gap: 14 }}>
            {linkedNodes.map(nb => {
              const isOnline = nb.status === 'ONLINE';
              const q = isOnline ? rssiQuality(nb.rssi) : { color: 'var(--text-muted)', label: 'OFFLINE' };

              return (
                <div
                  key={nb.node_id}
                  className={`card ${isOnline ? 'online' : 'offline'}`}
                  onClick={() => !nb.isGateway && handleInspect(nb)}
                  style={{
                    cursor: nb.isGateway ? 'default' : 'pointer',
                    padding: '14px 16px',
                    border: nb.isGateway ? '1px solid var(--green)' : '1px solid var(--border)',
                    background: nb.isGateway ? 'rgba(16, 185, 129, 0.08)' : 'var(--bg-card)',
                    borderRadius: 'var(--radius-md)',
                  }}
                  title={nb.isGateway ? 'Hardware Gateway Coordinator' : `Click to inspect ${nb.node_id}`}
                >
                  <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 8 }}>
                    <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                      <span style={{ fontSize: 16 }}>{nb.isGateway ? '📡' : '⛏️'}</span>
                      <span style={{ fontWeight: 800, fontSize: 13, color: nb.isGateway ? 'var(--green)' : '#ffffff' }}>
                        {nb.node_id}
                      </span>
                    </div>
                    <StatusBadge status={nb.status} />
                  </div>

                  <div style={{ fontSize: 11, color: 'var(--text-secondary)', marginBottom: 8 }}>
                    {nb.role || 'Subsurface Wearable Unit'}
                  </div>

                  <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6, fontSize: 11, fontFamily: 'var(--font-mono)' }}>
                    <div>
                      <span style={{ color: 'var(--text-muted)', fontSize: 9 }}>SIGNAL:</span><br />
                      <span style={{ color: q.color, fontWeight: 700 }}>{fmt.rssi(nb.rssi)}</span>
                    </div>
                    <div>
                      <span style={{ color: 'var(--text-muted)', fontSize: 9 }}>DISTANCE:</span><br />
                      <span style={{ color: 'var(--cyan)', fontWeight: 700 }}>
                        {nb.distanceM !== null ? `${nb.distanceM.toFixed(1)}m` : '—'}
                      </span>
                    </div>
                    <div>
                      <span style={{ color: 'var(--text-muted)', fontSize: 9 }}>ELEVATION:</span><br />
                      <span style={{ color: 'var(--purple)', fontWeight: 700 }}>
                        {typeof nb.altitude === 'number' ? fmt.alt(nb.altitude) : '—'}
                      </span>
                    </div>
                    <div>
                      <span style={{ color: 'var(--text-muted)', fontSize: 9 }}>BATTERY:</span><br />
                      <span style={{ color: 'var(--green)', fontWeight: 700 }}>
                        {nb.batteryPct !== undefined ? `${nb.batteryPct}%` : '100%'}
                      </span>
                    </div>
                  </div>
                </div>
              );
            })}
          </div>
        </div>
      </div>
    </div>
  );
}
