import { Router } from 'express';
import { stmts } from '../db.js';

const router = Router();

router.get('/nodes', (req, res) => {
  res.json(req.gateway ? req.gateway.getNodes() : stmts.getAllNodes());
});

router.get('/settings', (req, res) => {
  if (!req.gateway) return res.status(500).json({ error: 'Gateway not available' });
  res.json(req.gateway.getSettings());
});

router.post('/settings', (req, res) => {
  if (!req.gateway) return res.status(500).json({ error: 'Gateway not available' });
  const result = req.gateway.updateSettings(req.body);
  res.json(result);
});

router.post('/settings/datum', (req, res) => {
  if (!req.gateway) return res.status(500).json({ error: 'Gateway not available' });
  const datum = parseFloat(req.body.datum);
  req.gateway.sendDatum(datum);
  res.json({ success: true, datum });
});

router.post('/evacuate', (req, res) => {
  if (!req.gateway) return res.status(500).json({ error: 'Gateway not available' });
  const result = req.gateway.evacuate();
  res.json(result);
});

router.post('/nodes/:id/mute', (req, res) => {
  if (!req.gateway) return res.status(500).json({ error: 'Gateway not available' });
  const result = req.gateway.muteNode(req.params.id);
  res.json(result);
});

router.post('/nodes/mute-all', (req, res) => {
  if (!req.gateway) return res.status(500).json({ error: 'Gateway not available' });
  const result = req.gateway.muteNode('ALL_NODES');
  res.json(result);
});

router.post('/evacuate/cancel', (req, res) => {
  if (!req.gateway) return res.status(500).json({ error: 'Gateway not available' });
  const result = req.gateway.cancelEvacuation();
  res.json(result);
});

router.post('/cancel-evacuate', (req, res) => {
  if (!req.gateway) return res.status(500).json({ error: 'Gateway not available' });
  const result = req.gateway.cancelEvacuation();
  res.json(result);
});

router.get('/nodes/:id', (req, res) => {
  const node = stmts.getNode(req.params.id);
  if (!node) return res.status(404).json({ error: 'Node not found' });
  res.json(node);
});

router.get('/nodes/:id/telemetry', (req, res) => {
  const limit = Math.min(500, parseInt(req.query.limit) || 120);
  const rows  = stmts.getNodeTelemetry(req.params.id, limit);
  res.json(rows.reverse());
});

router.get('/alerts', (_req, res) => {
  res.json(stmts.getAlerts());
});

router.post('/alerts/:id/acknowledge', (req, res) => {
  stmts.acknowledgeAlert(req.params.id);
  res.json({ success: true });
});

export default router;


