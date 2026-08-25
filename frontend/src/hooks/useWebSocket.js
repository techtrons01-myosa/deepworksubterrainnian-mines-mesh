import { useState, useEffect, useRef, useCallback } from 'react';

export function useWebSocket(url) {
  const [state,     setState]     = useState(null);
  const [connected, setConnected] = useState(false);
  const ws       = useRef(null);
  const retryRef = useRef(null);

  const connect = useCallback(() => {
    try {
      ws.current = new WebSocket(url);

      ws.current.onopen = () => {
        setConnected(true);
        clearTimeout(retryRef.current);
      };

      ws.current.onmessage = e => {
        try {
          const msg = JSON.parse(e.data);
          if (msg.type === 'STATE_UPDATE') setState(msg.payload);
        } catch (_) { /* ignore parse errors */ }
      };

      ws.current.onclose = () => {
        setConnected(false);
        retryRef.current = setTimeout(connect, 3000);
      };

      ws.current.onerror = () => {
        ws.current?.close();
      };
    } catch (_) {
      retryRef.current = setTimeout(connect, 3000);
    }
  }, [url]);

  useEffect(() => {
    connect();
    return () => {
      clearTimeout(retryRef.current);
      ws.current?.close();
    };
  }, [connect]);

  return { state, connected };
}
