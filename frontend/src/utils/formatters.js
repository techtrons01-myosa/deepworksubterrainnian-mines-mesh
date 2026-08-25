export const fmt = {
  temp:    v => (v !== undefined && !isNaN(v)) ? `${v.toFixed(1)} °C` : '--',
  pres:    v => (v !== undefined && !isNaN(v)) ? `${v.toFixed(1)} hPa` : '--',
  alt:     v => (v !== undefined && !isNaN(v)) ? `${v.toFixed(1)} m` : '--',
  rssi:    v => (v !== undefined && !isNaN(v)) ? `${Math.round(v)} dBm` : '--',
  pct:     v => (v !== undefined && !isNaN(v)) ? `${Math.round(v)} %` : '--',
  uptime: (s) => {
    if (s === undefined || s === null || isNaN(s) || s < 0) return '0s';
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    if (h > 0) return `${h}h ${m}m`;
    const sec = Math.floor(s % 60);
    if (m > 0) return `${m}m ${sec}s`;
    return `${sec}s`;
  },
  time: ts => {
    if (!ts) return '—';
    const d = new Date(ts);
    return isNaN(d.getTime()) ? '—' : d.toLocaleTimeString('en-GB', { hour12: false });
  },
  datetime: ts => {
    if (!ts) return '—';
    const d = new Date(ts);
    return isNaN(d.getTime()) ? '—' : d.toLocaleTimeString('en-GB', { hour12: false });
  },
  accel: (x, y, z) => `${(x||0).toFixed(2)}, ${(y||0).toFixed(2)}, ${(z||0).toFixed(2)}`,
  pos: v => (v !== undefined && !isNaN(v)) ? v.toFixed(2) : '0.00',
  conf: v => `${Math.round((v||0) * 100)} %`,
};

export function rssiQuality(rssi) {
  if (rssi > -55) return { label: 'Excellent', color: 'var(--green)' };
  if (rssi > -67) return { label: 'Good',      color: 'var(--cyan)' };
  if (rssi > -78) return { label: 'Fair',       color: 'var(--amber)' };
  return                 { label: 'Weak',       color: 'var(--red)' };
}

export function healthColor(pct) {
  if (pct >= 90) return 'var(--green)';
  if (pct >= 70) return 'var(--amber)';
  return 'var(--red)';
}

export function severityIcon(sev) {
  if (sev === 'CRITICAL') return '🚨';
  if (sev === 'WARNING')  return '⚠️';
  return 'ℹ️';
}
