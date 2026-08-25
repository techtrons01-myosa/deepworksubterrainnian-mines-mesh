import React from 'react';

export default function StatusBadge({ status }) {
  const cls  = status === 'ONLINE' ? 'badge-online' : 'badge-offline';
  const dot  = status === 'ONLINE' ? 'dot-green'   : 'dot-red';
  return (
    <span className={`badge ${cls}`}>
      <span className={`dot ${dot}`} style={{ width: 6, height: 6 }} />
      {status}
    </span>
  );
}
