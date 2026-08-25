import React, { useEffect, useRef, useState, useMemo, useCallback } from 'react';
import * as d3 from 'd3';
import { fmt, rssiQuality } from '../utils/formatters.js';

const NODE_R = 20;

export default function NetworkMap({ nodes, links, onNodeClick, settings = {}, onOpenSettings }) {
  const svgRef = useRef(null);

  // Ground level datum reference (in meters)
  const groundDatum = parseFloat(settings.groundLevelDatum ?? 25.0);
  const [altitudeX, setAltitudeX] = useState(groundDatum);

  // Mine Shaft Levels (customizable from settings)
  const shaftLevels = useMemo(() => {
    if (settings.shaftLevels && Array.isArray(settings.shaftLevels) && settings.shaftLevels.length > 0) {
      return settings.shaftLevels;
    }
    return [
      { label: 'Surface Portal', alt: groundDatum, color: 'var(--cyan)' },
      { label: 'Shaft 1 / Gallery', alt: groundDatum - 15, color: 'var(--green)' },
      { label: 'Level 2 / Mid Drift', alt: groundDatum - 30, color: 'var(--purple)' },
      { label: 'Level 3 / Deep Stope', alt: groundDatum - 45, color: 'var(--amber)' },
      { label: 'Shaft Sump', alt: groundDatum - 60, color: 'var(--red)' },
    ];
  }, [settings.shaftLevels, groundDatum]);

  // Keep altitudeX synchronized when groundDatum changes
  useEffect(() => {
    setAltitudeX(groundDatum);
  }, [groundDatum]);

  // Filter mode: 'ALL', 'ABOVE', 'BELOW'
  const [filterMode, setFilterMode] = useState('ALL');

  // Tooltip state
  const [tooltip, setTooltip] = useState({ visible: false, x: 0, y: 0, node: null });

  // Hovered node highlight
  const [hoveredNodeId, setHoveredNodeId] = useState(null);

  const nodeList = useMemo(() => {
    return Object.values(nodes).map(n => {
      const rawAlt = n.altitude ?? n.baseAltitude ?? 25.0;
      const offset = settings.nodeCalibrationOffsets?.[n.node_id] ?? 0;
      const effectiveAlt = rawAlt - offset;
      const depthBelowGround = groundDatum - effectiveAlt;
      const relAboveGround = effectiveAlt - groundDatum;

      return {
        ...n,
        rawAltitude: rawAlt,
        effectiveAlt: effectiveAlt,
        depthBelowGround: depthBelowGround,
        relAboveGround: relAboveGround,
        altitude: effectiveAlt,
      };
    });
  }, [nodes, groundDatum, settings.nodeCalibrationOffsets]);

  // Render Cross-Section Elevation Map with D3
  useEffect(() => {
    const svg = d3.select(svgRef.current);
    if (!svg.node()) return;

    const width = svgRef.current.clientWidth || 900;
    const height = svgRef.current.clientHeight || 600;

    svg.attr('viewBox', `0 0 ${width} ${height}`);
    svg.selectAll('*').remove();

    const defs = svg.append('defs');

    // Glow filters
    const filterDefs = [
      { id: 'glow-green', col: '#10b981' },
      { id: 'glow-red', col: '#ef4444' },
      { id: 'glow-cyan', col: '#ffb020' },
      { id: 'glow-purple', col: '#a78bfa' },
      { id: 'glow-amber', col: '#f59e0b' },
    ];
    filterDefs.forEach(f => {
      const filter = defs.append('filter')
        .attr('id', f.id)
        .attr('x', '-50%').attr('y', '-50%')
        .attr('width', '200%').attr('height', '200%');
      filter.append('feGaussianBlur').attr('stdDeviation', '4').attr('result', 'blur');
      const merge = filter.append('feMerge');
      merge.append('feMergeNode').attr('in', 'blur');
      merge.append('feMergeNode').attr('in', 'SourceGraphic');
    });

    // Reference plane gradient
    const refGrad = defs.append('linearGradient')
      .attr('id', 'ref-plane-grad')
      .attr('x1', '0%').attr('y1', '0%').attr('x2', '100%').attr('y2', '0%');
    refGrad.append('stop').attr('offset', '0%').attr('stop-color', 'rgba(255,176,32,0)');
    refGrad.append('stop').attr('offset', '15%').attr('stop-color', 'rgba(255,176,32,0.7)');
    refGrad.append('stop').attr('offset', '50%').attr('stop-color', 'rgba(167,139,250,0.9)');
    refGrad.append('stop').attr('offset', '85%').attr('stop-color', 'rgba(255,176,32,0.7)');
    refGrad.append('stop').attr('offset', '100%').attr('stop-color', 'rgba(255,176,32,0)');

    // Above zone gradient
    const aboveGrad = defs.append('linearGradient')
      .attr('id', 'above-zone-grad')
      .attr('x1', '0%').attr('y1', '100%').attr('x2', '0%').attr('y2', '0%');
    aboveGrad.append('stop').attr('offset', '0%').attr('stop-color', 'rgba(16, 217, 138, 0.08)');
    aboveGrad.append('stop').attr('offset', '100%').attr('stop-color', 'rgba(16, 217, 138, 0.01)');

    // Below zone gradient
    const belowGrad = defs.append('linearGradient')
      .attr('id', 'below-zone-grad')
      .attr('x1', '0%').attr('y1', '0%').attr('x2', '0%').attr('y2', '100%');
    belowGrad.append('stop').attr('offset', '0%').attr('stop-color', 'rgba(245, 158, 11, 0.08)');
    belowGrad.append('stop').attr('offset', '100%').attr('stop-color', 'rgba(245, 158, 11, 0.01)');

    const zoomGroup = svg.append('g').attr('class', 'zoom-root');
    const zoom = d3.zoom().scaleExtent([0.4, 3]).on('zoom', e => {
      zoomGroup.attr('transform', e.transform);
    });
    svg.call(zoom);

    // Calculate dynamic altitude scale domain
    const allAlts = nodeList.map(n => n.effectiveAlt).concat(shaftLevels.map(s => s.alt)).concat([groundDatum, altitudeX]);
    const minAltDomain = Math.min(-20, Math.min(...allAlts) - 15);
    const maxAltDomain = Math.max(minAltDomain + 50, Math.max(...allAlts) + 20);

    const margin = { top: 50, right: 180, bottom: 60, left: 90 };
    const innerW = width - margin.left - margin.right;
    const innerH = height - margin.top - margin.bottom;

    const altScale = d3.scaleLinear().domain([minAltDomain, maxAltDomain]).range([innerH, 0]);
    const lateralScale = d3.scaleLinear().domain([0, 16]).range([margin.left + 40, width - margin.right - 40]);

    const mappedNodes = nodeList.map((n, idx) => {
      const alt = n.effectiveAlt;
      let posX = (n.position?.x && n.position.x >= 1.0 && n.position.x <= 15.0)
        ? n.position.x
        : (6.0 + (idx * 3.0) % 8.0);

      const cx = lateralScale(posX);
      const cy = margin.top + altScale(alt);
      const isAbove = alt > altitudeX + 0.3;
      const isBelow = alt < altitudeX - 0.3;
      const delta = alt - altitudeX;
      const depth = groundDatum - alt;

      return { ...n, cx, cy, alt, isAbove, isBelow, delta, depth };
    });

    const elevGrid = zoomGroup.append('g').attr('class', 'elevation-grid');

    // Altitude Grid Ticks
    const step = (maxAltDomain - minAltDomain) > 80 ? 20 : 10;
    const altTicks = [];
    for (let v = Math.ceil(minAltDomain / step) * step; v <= maxAltDomain; v += step) {
      altTicks.push(v);
    }

    altTicks.forEach(tickVal => {
      const yPos = margin.top + altScale(tickVal);
      elevGrid.append('line')
        .attr('x1', margin.left)
        .attr('y1', yPos)
        .attr('x2', width - margin.right)
        .attr('y2', yPos)
        .attr('stroke', tickVal === groundDatum ? 'rgba(255,176,32,0.4)' : 'rgba(6,214,245,0.06)')
        .attr('stroke-dasharray', tickVal === groundDatum ? null : '4,4');

      elevGrid.append('text')
        .attr('x', margin.left - 14)
        .attr('y', yPos + 4)
        .attr('text-anchor', 'end')
        .attr('fill', tickVal === groundDatum ? 'var(--gold)' : 'var(--text-muted)')
        .attr('font-size', '11px')
        .attr('font-weight', tickVal === groundDatum ? '700' : '400')
        .attr('font-family', 'JetBrains Mono, monospace')
        .text(`${tickVal} m`);
    });

    // Lateral Grid Lines
    for (let lx = 2; lx <= 14; lx += 3) {
      const xPos = lateralScale(lx);
      elevGrid.append('line')
        .attr('x1', xPos)
        .attr('y1', margin.top)
        .attr('x2', xPos)
        .attr('y2', height - margin.bottom)
        .attr('stroke', 'rgba(6,214,245,0.03)')
        .attr('stroke-dasharray', '2,4');
    }

    // Render Calibrated Mine Shaft Levels on the right side
    shaftLevels.forEach(sl => {
      const yPos = margin.top + altScale(sl.alt);
      if (yPos >= margin.top && yPos <= height - margin.bottom) {
        elevGrid.append('line')
          .attr('x1', margin.left)
          .attr('y1', yPos)
          .attr('x2', width - margin.right)
          .attr('y2', yPos)
          .attr('stroke', sl.color || 'var(--cyan)')
          .attr('stroke-width', 1)
          .attr('stroke-dasharray', '6,6')
          .attr('opacity', 0.4);

        // Shaft Level Badge & Elevation Label
        const badgeG = elevGrid.append('g')
          .attr('transform', `translate(${width - margin.right + 12}, ${yPos})`);

        badgeG.append('rect')
          .attr('x', 0)
          .attr('y', -9)
          .attr('width', 160)
          .attr('height', 18)
          .attr('rx', 4)
          .attr('fill', 'rgba(5, 16, 30, 0.85)')
          .attr('stroke', sl.color || 'var(--cyan)')
          .attr('stroke-width', 0.8);

        badgeG.append('text')
          .attr('x', 8)
          .attr('y', 3)
          .attr('fill', sl.color || 'var(--cyan)')
          .attr('font-size', '10px')
          .attr('font-weight', '700')
          .attr('font-family', 'Inter, sans-serif')
          .text(`${sl.label} (${sl.alt.toFixed(0)}m)`);
      }
    });

    // Axis labels
    elevGrid.append('text')
      .attr('transform', `rotate(-90)`)
      .attr('x', -(margin.top + innerH / 2))
      .attr('y', margin.left - 55)
      .attr('text-anchor', 'middle')
      .attr('fill', 'var(--text-secondary)')
      .attr('font-size', '12px')
      .attr('font-weight', '700')
      .attr('letter-spacing', '1.5px')
      .attr('font-family', 'Inter, sans-serif')
      .text('AMSL ELEVATION (METERS)');

    elevGrid.append('text')
      .attr('x', margin.left + innerW / 2)
      .attr('y', height - margin.bottom + 36)
      .attr('text-anchor', 'middle')
      .attr('fill', 'var(--text-muted)')
      .attr('font-size', '11px')
      .attr('font-family', 'JetBrains Mono, monospace')
      .text('← WEST — SUB-SURFACE LATERAL SHAFT SPREAD — EAST →');

    // Reference Datum Cutoff Line
    const refY = margin.top + altScale(altitudeX);

    // Shaded zones
    elevGrid.append('rect')
      .attr('x', margin.left)
      .attr('y', margin.top)
      .attr('width', innerW)
      .attr('height', Math.max(0, refY - margin.top))
      .attr('fill', 'url(#above-zone-grad)')
      .attr('pointer-events', 'none');

    elevGrid.append('rect')
      .attr('x', margin.left)
      .attr('y', refY)
      .attr('width', innerW)
      .attr('height', Math.max(0, height - margin.bottom - refY))
      .attr('fill', 'url(#below-zone-grad)')
      .attr('pointer-events', 'none');

    const planeGroup = zoomGroup.append('g').attr('class', 'reference-plane-layer');

    planeGroup.append('line')
      .attr('x1', margin.left - 20)
      .attr('y1', refY)
      .attr('x2', width - margin.right + 20)
      .attr('y2', refY)
      .attr('stroke', 'url(#ref-plane-grad)')
      .attr('stroke-width', 2.5)
      .attr('filter', 'url(#glow-cyan)');

    planeGroup.append('line')
      .attr('x1', margin.left)
      .attr('y1', refY)
      .attr('x2', width - margin.right)
      .attr('y2', refY)
      .attr('stroke', '#ffffff')
      .attr('stroke-width', 1)
      .attr('stroke-dasharray', '8,4')
      .attr('opacity', 0.9);

    const tagW = 240;
    const tagH = 26;
    const tagG = planeGroup.append('g')
      .attr('transform', `translate(${margin.left + 20}, ${refY - tagH / 2})`)
      .style('cursor', 'ns-resize')
      .call(
        d3.drag().on('drag', event => {
          const newY = Math.max(margin.top, Math.min(height - margin.bottom, event.y));
          const newAlt = altScale.invert(newY - margin.top);
          setAltitudeX(Math.round(newAlt * 2) / 2);
        })
      );

    tagG.append('rect')
      .attr('width', tagW)
      .attr('height', tagH)
      .attr('rx', 6)
      .attr('fill', 'rgba(5, 16, 30, 0.95)')
      .attr('stroke', 'var(--cyan)')
      .attr('stroke-width', 1.5)
      .attr('filter', 'url(#glow-cyan)');

    tagG.append('text')
      .attr('x', tagW / 2)
      .attr('y', tagH / 2 + 4)
      .attr('text-anchor', 'middle')
      .attr('fill', 'var(--cyan)')
      .attr('font-size', '11px')
      .attr('font-weight', '700')
      .attr('font-family', 'JetBrains Mono, monospace')
      .text(`⚡ GROUND DATUM = ${altitudeX.toFixed(1)} m`);

    // Nodes Layer
    const nodeGroup = zoomGroup.append('g').attr('class', 'nodes-layer');

    mappedNodes.forEach(d => {
      if (filterMode === 'ABOVE' && !d.isAbove) return;
      if (filterMode === 'BELOW' && !d.isBelow) return;

      const isOnline = d.status === 'ONLINE';
      const isHovered = hoveredNodeId === d.node_id;

      let statusColor = '#f05252';
      let glowFilter = 'url(#glow-red)';
      let badgeLabel = 'OFFLINE';

      if (isOnline) {
        if (d.isAbove) {
          statusColor = '#10d98a';
          glowFilter = 'url(#glow-green)';
          badgeLabel = `▲ +${d.delta.toFixed(1)}m ABOVE`;
        } else if (d.isBelow) {
          statusColor = '#fbbf24';
          glowFilter = 'url(#glow-amber)';
          badgeLabel = `▼ ${Math.abs(d.delta).toFixed(1)}m DEEP`;
        } else {
          statusColor = '#06d6f5';
          glowFilter = 'url(#glow-cyan)';
          badgeLabel = `● AT SURFACE`;
        }
      }

      const nodeG = nodeGroup.append('g')
        .attr('class', 'node-g')
        .attr('transform', `translate(${d.cx}, ${d.cy})`)
        .style('cursor', 'pointer')
        .on('click', event => {
          event.stopPropagation();
          onNodeClick(d);
        })
        .on('mouseenter', event => {
          setHoveredNodeId(d.node_id);
          const rect = svgRef.current.getBoundingClientRect();
          setTooltip({
            visible: true,
            x: event.clientX - rect.left + 15,
            y: event.clientY - rect.top - 10,
            node: d,
          });
        })
        .on('mouseleave', () => {
          setHoveredNodeId(null);
          setTooltip(t => ({ ...t, visible: false }));
        });

      if (isOnline) {
        nodeGroup.append('line')
          .attr('x1', d.cx).attr('y1', refY)
          .attr('x2', d.cx).attr('y2', d.cy)
          .attr('stroke', statusColor)
          .attr('stroke-width', 1.5)
          .attr('stroke-dasharray', '2,3')
          .attr('opacity', 0.6);
      }

      nodeG.append('circle')
        .attr('r', NODE_R)
        .attr('fill', isOnline ? 'rgba(5, 16, 30, 0.95)' : 'rgba(240, 82, 82, 0.15)')
        .attr('stroke', statusColor)
        .attr('stroke-width', 2)
        .attr('filter', glowFilter);

      nodeG.append('text')
        .attr('text-anchor', 'middle')
        .attr('dy', '-4px')
        .attr('font-family', 'JetBrains Mono, monospace')
        .attr('font-size', '9px')
        .attr('font-weight', '700')
        .attr('fill', statusColor)
        .text(d.node_id);

      nodeG.append('text')
        .attr('text-anchor', 'middle')
        .attr('dy', '8px')
        .attr('font-family', 'JetBrains Mono, monospace')
        .attr('font-size', '8px')
        .attr('font-weight', '700')
        .attr('fill', '#ffffff')
        .text(isOnline ? `${d.alt.toFixed(1)}m` : 'OFFLINE');

      if (isOnline) {
        const badgeG = nodeG.append('g')
          .attr('transform', 'translate(0, -28)');

        const bWidth = badgeLabel.length * 6.5 + 8;
        badgeG.append('rect')
          .attr('x', -bWidth / 2)
          .attr('y', -8)
          .attr('width', bWidth)
          .attr('height', 14)
          .attr('rx', 3)
          .attr('fill', d.isAbove ? 'rgba(16, 217, 138, 0.2)' : 'rgba(251, 191, 36, 0.2)')
          .attr('stroke', statusColor)
          .attr('stroke-width', 0.8);

        badgeG.append('text')
          .attr('text-anchor', 'middle')
          .attr('dy', '3px')
          .attr('font-family', 'JetBrains Mono, monospace')
          .attr('font-size', '8px')
          .attr('font-weight', '700')
          .attr('fill', statusColor)
          .text(badgeLabel);
      }
    });

  }, [nodeList, links, altitudeX, filterMode, hoveredNodeId, groundDatum, shaftLevels]);

  return (
    <div className="network-map-wrapper" style={{ position: 'relative', width: '100%', height: '100%' }}>
      {/* Top Map Action Bar */}
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '10px 16px', background: 'rgba(5, 16, 30, 0.7)', borderBottom: '1px solid var(--border)' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
          <span style={{ fontSize: 13, fontWeight: 800, color: 'var(--gold)', letterSpacing: 1 }}>
            ⛰️ SUB-SURFACE CROSS-SECTION ELEVATION PROFILE
          </span>
        </div>

        {/* Action Controls & Calibration shortcut */}
        <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
          <button
            className="btn btn-cyan"
            style={{ fontSize: 11, padding: '4px 12px', fontWeight: 700 }}
            onClick={onOpenSettings}
          >
            ⚙️ Calibrate Ground & Shaft Levels
          </button>
        </div>
      </div>

      <svg ref={svgRef} style={{ width: '100%', height: 'calc(100% - 50px)', background: 'radial-gradient(ellipse at center, rgba(6, 214, 245, 0.03) 0%, rgba(5, 16, 30, 0.98) 100%)' }} />

      {/* Tooltip */}
      {tooltip.visible && tooltip.node && (
        <div
          style={{
            position: 'absolute',
            left: tooltip.x,
            top: tooltip.y,
            background: 'rgba(5, 16, 30, 0.95)',
            border: '1px solid var(--cyan)',
            borderRadius: 6,
            padding: '8px 12px',
            pointerEvents: 'none',
            zIndex: 100,
            fontSize: 11,
            fontFamily: 'JetBrains Mono, monospace',
            color: '#fff',
            boxShadow: '0 4px 16px rgba(0,0,0,0.6)',
          }}
        >
          <div style={{ fontWeight: 800, color: 'var(--cyan)' }}>{tooltip.node.node_id}</div>
          <div>AMSL Altitude: {tooltip.node.effectiveAlt.toFixed(1)} m</div>
          <div>Ground Datum: {groundDatum.toFixed(1)} m</div>
          <div style={{ color: tooltip.node.isAbove ? '#10d98a' : '#fbbf24', fontWeight: 700 }}>
            {tooltip.node.isAbove ? `▲ +${tooltip.node.delta.toFixed(1)}m Above Ground` : `▼ ${Math.abs(tooltip.node.delta).toFixed(1)}m Deep in Mine`}
          </div>
          <div>Signal: {tooltip.node.rssi} dBm</div>
          <div>Temp: {tooltip.node.temperature?.toFixed(1)} °C</div>
          <div>Pressure: {tooltip.node.pressure?.toFixed(1)} hPa</div>
        </div>
      )}
    </div>
  );
}
