class HealthMonitor {
  compute(nodes) {
    const all    = Object.values(nodes);
    const online = all.filter(n => n.status === 'ONLINE');

    if (all.length === 0) return { health: 0, online: 0, offline: 0, total: 0, links: 0, avgRssi: 0 };

    const meshPct  = online.length / all.length;
    const avgRssi  = online.length
      ? online.reduce((s, n) => s + n.rssi, 0) / online.length
      : -90;

    // Count unique bidirectional links
    const seen  = new Set();
    let links   = 0;
    for (const n of online) {
      for (const nb of (n.neighbors || [])) {
        if (nodes[nb]?.status !== 'ONLINE') continue;
        const key = [n.node_id, nb].sort().join(':');
        if (!seen.has(key)) { seen.add(key); links++; }
      }
    }

    // Health 0–100: 80 % uptime factor + 20 % RSSI quality
    const rssiQ   = Math.max(0, Math.min(1, (avgRssi + 88) / 55));  // normalise -88..-33
    const health  = Math.round(meshPct * 80 + rssiQ * 20);

    return {
      health,
      online:  online.length,
      offline: all.length - online.length,
      total:   all.length,
      links,
      avgRssi: Math.round(avgRssi),
    };
  }
}

export default HealthMonitor;
