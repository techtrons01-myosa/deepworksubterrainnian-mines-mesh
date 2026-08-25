/**
 * Estimates a node's relative position using a weighted-centroid approach
 * based on neighbour positions and RSSI values (higher RSSI = closer = higher weight).
 */
class LocationEngine {
  estimate(nodeId, node, allNodes) {
    const onlineNeighbours = (node.neighbors || []).filter(
      nb => allNodes[nb]?.status === 'ONLINE' && allNodes[nb]?.position
    );

    if (onlineNeighbours.length === 0) return node.position;

    let wx = 0, wy = 0, wTotal = 0;

    for (const nb of onlineNeighbours) {
      const nbNode = allNodes[nb];
      // Convert RSSI (dBm) to a linear distance-proxy weight
      // rssi ≈ -40 (strong) → weight ≈ high, rssi ≈ -90 (weak) → weight ≈ low
      const weight = Math.pow(10, (nbNode.rssi + 40) / 30);
      wx     += nbNode.position.x * weight;
      wy     += nbNode.position.y * weight;
      wTotal += weight;
    }

    if (wTotal === 0) return node.position;

    const confidence = Math.min(0.99, 0.55 + onlineNeighbours.length * 0.08 + Math.random() * 0.05);

    return {
      x:          parseFloat((wx / wTotal).toFixed(2)),
      y:          parseFloat((wy / wTotal).toFixed(2)),
      confidence: parseFloat(confidence.toFixed(2)),
    };
  }
}

export default LocationEngine;
