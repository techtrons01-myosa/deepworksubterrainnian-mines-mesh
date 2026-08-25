class MeshTopology {
  constructor() { this.links = []; }

  update(nodes) {
    const seen  = new Set();
    this.links  = [];

    for (const [id, node] of Object.entries(nodes)) {
      if (node.status !== 'ONLINE') continue;

      for (const nb of (node.neighbors || [])) {
        if (nodes[nb]?.status !== 'ONLINE') continue;

        const key = [id, nb].sort().join('::');
        if (seen.has(key)) continue;
        seen.add(key);

        this.links.push({
          source:     id,
          target:     nb,
          rssi:       node.rssi,
          technology: node.espnow ? 'ESP-NOW' : 'BLE',
        });
      }
    }

    return this.links;
  }

  getLinks() { return this.links; }
}

export default MeshTopology;
